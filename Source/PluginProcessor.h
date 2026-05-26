#pragma once

#include <atomic>
#include <array>
#include <memory>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PartialEngine.h"
#include "MidiPitch.h"
#include "OscBridge.h"
#include "Simulator.h"

class DualSeatRouter : public SeatEventSink
{
public:
    explicit DualSeatRouter (PartialEngine& audio)
        : audioEngine(audio)
    {}

    void setX (int row, int col, float xNorm) override
    {
        audioEngine.setX(row, col, xNorm);
    }

    void setY (int row, int col, float yNorm) override
    {
        audioEngine.setY(row, col, yNorm);
    }

    void setOn (int row, int col, bool on) override
    {
        audioEngine.setOn(row, col, on);
    }

private:
    PartialEngine& audioEngine;
};

class AudienceProcessor : public juce::AudioProcessor,
                          private juce::Timer
{
public:
    AudienceProcessor();
    ~AudienceProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi()  const override
    {
       #if JUCE_ANDROID
        return false;
       #else
        return true;
       #endif
    }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // exposed to the editor
    juce::AudioProcessorValueTreeState apvts;
    PartialEngine engine;
    DualSeatRouter seatRouter;
    OscBridge     osc;
    Simulator     simulator;

    int  udpPort = 6060;
    void setUdpPort (int port);
    juce::String oscStatus;
    void setMuted (bool shouldMute) noexcept { muted.store(shouldMute); }
    bool isMuted() const noexcept            { return muted.load(); }
    void panic();
    int getMidiNotesSent() const noexcept    { return midiNotesSent.load(std::memory_order_relaxed); }
    int getActiveMpeVoices() const noexcept  { return activeMpeVoices.load(std::memory_order_relaxed); }
    int getAvailableMpeChannels() const noexcept { return availableMpeChannels.load(std::memory_order_relaxed); }
    juce::String getOutgoingMidiDebugText (int maxEvents = 96) const;
    juce::StringArray getMidiOutputOptions() const;
    juce::String getMidiOutputStatus() const;
    juce::String getMidiOutputDescription() const;
    int getMidiOutputOptionIndex() const noexcept { return midiOutputOptionIndex.load(std::memory_order_relaxed); }
    void setMidiOutputOptionIndex (int index);

    juce::File   sampleDir;          // currently-loaded library folder
    juce::File   libraryRoot;        // parent: Samples/
    juce::String librariesStatus;
    juce::String currentLibraryName;

    juce::StringArray getAvailableLibraries() const;
    void              setSampleDirectory (const juce::File& dir);
    void              setCurrentLibrary  (const juce::String& libraryName);
    void              rescanLibraryRoot();

private:
    void timerCallback() override;
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void pullParams();
    void processIncomingMidiKeyboard (const juce::MidiBuffer&);
    void releaseAllMidiKeyboardNotes();
    void renderOutgoingMidi (juce::MidiBuffer& midiMessages, int numSamples);
    void handleMidiSourceEvent (const PartialEngine::MidiSourceEvent& event,
                                juce::MidiBuffer& midiMessages, int sampleOffset);
    void sendAllMidiNotesOff (juce::MidiBuffer& midiMessages, int sampleOffset);
    void sendMidiResetMessages (juce::MidiBuffer& midiMessages, int sampleOffset);
    void resetMidiOutputState() noexcept;
    void sendMpeSetupIfNeeded (juce::MidiBuffer& midiMessages, int sampleOffset);
    void sendPitchBendRangeRpn (juce::MidiBuffer& midiMessages, int sampleOffset,
                                int channel, int semitones);
    int  allocateMpeChannelForSource (int sourceId, juce::MidiBuffer& midiMessages, int sampleOffset);
    void releaseMpeChannelForSource (int sourceId) noexcept;
    void sendNoteOffForSource (int sourceId, juce::MidiBuffer& midiMessages, int sampleOffset);
    void sendExpressionForSource (int sourceId, const PartialEngine::MidiSourceEvent& event,
                                  juce::MidiBuffer& midiMessages, int sampleOffset, bool force);
    static int bendRangeFromChoice (int choice) noexcept;
    static int velocityFromUnit (float value) noexcept;
    static int pressureFromUnit (float value) noexcept;
    void recordOutgoingMidiDebugEvents (const juce::MidiBuffer& midiMessages) noexcept;
    void queueMidiToExternalOutput (const juce::MidiBuffer& midiMessages) noexcept;
    void drainExternalMidiOutputQueue();
    void sendImmediateAllNotesOffToExternal();
    void closeMidiOutput();

    static constexpr int realtimeScratchBlockSize = 32768;
    static constexpr size_t realtimeMidiBufferReserveBytes = 262144;
    static constexpr int externalMidiQueueSize = 8192;
    juce::AudioBuffer<float> monoScratch;
    juce::MidiBuffer midiRenderScratch;
    bool midiRenderScratchLoanedToHost = false;
    std::array<PartialEngine::MidiSourceEvent, 512> midiSourceScratch {};

    struct PackedMidiEvent
    {
        juce::uint8 size = 0;
        juce::uint8 data[3] {};
    };

    juce::AbstractFifo externalMidiFifo { externalMidiQueueSize };
    std::array<PackedMidiEvent, externalMidiQueueSize> externalMidiEvents {};
    std::atomic<uint32_t> externalMidiDropped { 0 };

    struct MidiDebugSlot
    {
        std::atomic<uint32_t> sequence { 0 };
        std::atomic<int> sampleOffset { 0 };
        std::atomic<int> size { 0 };
        std::atomic<int> byte0 { 0 };
        std::atomic<int> byte1 { 0 };
        std::atomic<int> byte2 { 0 };
    };

    static constexpr int midiDebugEventQueueSize = 256;
    std::array<MidiDebugSlot, midiDebugEventQueueSize> midiDebugEvents {};
    std::atomic<uint32_t> midiDebugWriteCounter { 0 };

    std::unique_ptr<juce::MidiOutput> midiOutput;
    std::atomic<int> midiOutputOptionIndex { 0 };
    juce::String midiOutputStatus { "Host MIDI Output" };
    double currentSampleRate = 44100.0;
    int instanceId = 1;

    struct MidiOutVoiceState
    {
        bool active = false;
        int sourceId = -1;
        int channel = 1;
        int note = -1;
        int pitchBend = 8192;
        int pressure = -1;
        int timbre = -1;
        int expression = -1;
        double frequencyHz = 0.0;
        uint32_t age = 0;
    };

    static constexpr int maxMidiSources = PartialEngine::MAX_SEATS + PartialEngine::MAX_KEYBOARD_SLOTS;
    std::array<MidiOutVoiceState, maxMidiSources> midiOutVoices {};
    std::array<int, 17> mpeChannelOwner {};
    uint32_t midiVoiceAgeCounter = 0;
    bool mpeSetupDirty = true;
    int lastAudioMidiOutputMode = 0;
    int lastMidiOutputType = 0;
    int lastMpeBendRange = 48;
    int lastMpeMemberFirst = 2;
    int lastMpeMemberLast = 16;
    int lastMpeSetupEnabled = 1;
    std::atomic<int> midiNotesSent { 0 };
    std::atomic<int> activeMpeVoices { 0 };
    std::atomic<int> availableMpeChannels { 15 };

    std::atomic<bool> muted { false };
    int lastScaleRoot = -1;
    int lastScaleMode = -1;
    int lastScaleOctaves = -1;
    int lastEngineSource = -1;
    int lastSamplePlaybackMode = -1;
    int lastSpectralElement = -1;
    int lastAtomicScaleMode = -1;
    std::array<int, 128> midiNoteToKeyboardSlot {};
    std::array<int, PartialEngine::MAX_KEYBOARD_SLOTS> keyboardSlotToMidiNote {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudienceProcessor)
};
