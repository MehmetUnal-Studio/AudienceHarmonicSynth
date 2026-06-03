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
    int getActiveExternalMidiKeys() const noexcept { return activeExternalMidiKeys.load(std::memory_order_relaxed); }
    int getLastExternalMidiNote() const noexcept { return lastExternalMidiNote.load(std::memory_order_relaxed); }
    int getLastExternalMidiChannel() const noexcept { return lastExternalMidiChannel.load(std::memory_order_relaxed); }
    juce::String getExternalMidiPitchModeName() const;
    juce::String getOutgoingMidiDebugText (int maxEvents = 96) const;
    juce::String getIncomingMidiDebugText (int maxEvents = 96) const;
    juce::String getMidiStateDebugText() const;
    juce::String getMidiDebugReportText() const;
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
    void cacheParameterPointers();
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
    void recordIncomingMidiDebugEvents (const juce::MidiBuffer& midiMessages) noexcept;
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
    std::array<MidiDebugSlot, midiDebugEventQueueSize> incomingMidiDebugEvents {};
    std::atomic<uint32_t> incomingMidiDebugWriteCounter { 0 };

    std::unique_ptr<juce::MidiOutput> midiOutput;
    std::atomic<int> midiOutputOptionIndex { 0 };
    juce::String midiOutputStatus { "Host MIDI Output" };
    double currentSampleRate = 44100.0;
    int instanceId = 1;

    // setStateInformation may run on a background thread (or before prepareToPlay),
    // so it stages the device/network/library work here and the message-thread
    // timer applies it. The release/acquire on the flag publishes the fields below.
    std::atomic<bool> pendingStateApply { false };
    int          pendingUdpPort = 6060;
    int          pendingMidiOutputOption = 0;
    juce::String pendingLibraryName;

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

    struct MidiVoiceDebugSlot
    {
        std::atomic<int> active { 0 };
        std::atomic<int> sourceId { -1 };
        std::atomic<int> channel { 0 };
        std::atomic<int> note { -1 };
        std::atomic<int> pitchBend { 8192 };
        std::atomic<int> age { 0 };
    };

    std::array<MidiVoiceDebugSlot, maxMidiSources> midiVoiceDebug {};
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
    static constexpr int midiInputChannels = 16;
    static constexpr int midiInputNotes = 128;
    static constexpr int midiInputKeyCount = midiInputChannels * midiInputNotes;
    std::array<int, midiInputKeyCount> midiKeyToKeyboardSlot {};
    std::array<int, PartialEngine::MAX_KEYBOARD_SLOTS> keyboardSlotToMidiKey {};
    std::array<std::atomic<int>, PartialEngine::MAX_KEYBOARD_SLOTS> keyboardDebugKeys {};
    std::atomic<int> lastExternalMidiNote { -1 };
    std::atomic<int> lastExternalMidiChannel { -1 };
    std::atomic<int> activeExternalMidiKeys { 0 };
    std::atomic<int> externalMidiPitchModeSnapshot { 0 };

    struct RawParams
    {
        std::atomic<float>* pitch = nullptr;
        std::atomic<float>* layerMix = nullptr;
        std::atomic<float>* attack = nullptr;
        std::atomic<float>* release = nullptr;
        std::atomic<float>* brightness = nullptr;
        std::atomic<float>* movement = nullptr;
        std::atomic<float>* reverb = nullptr;
        std::atomic<float>* delay = nullptr;
        std::atomic<float>* master = nullptr;
        std::atomic<float>* energy = nullptr;
        std::atomic<float>* motionMacro = nullptr;
        std::atomic<float>* toneMacro = nullptr;
        std::atomic<float>* spaceMacro = nullptr;
        std::atomic<float>* signatureMode = nullptr;
        std::atomic<float>* grainSize = nullptr;
        std::atomic<float>* grainDensity = nullptr;
        std::atomic<float>* pitchSpread = nullptr;
        std::atomic<float>* positionJitter = nullptr;
        std::atomic<float>* stereoSpread = nullptr;
        std::atomic<float>* reverseGrains = nullptr;
        std::atomic<float>* freeze = nullptr;
        std::atomic<float>* grainShape = nullptr;
        std::atomic<float>* wetDry = nullptr;
        std::atomic<float>* tapeDrive = nullptr;
        std::atomic<float>* polyphonyMode = nullptr;
        std::atomic<float>* scaleRoot = nullptr;
        std::atomic<float>* scaleRootOctave = nullptr;
        std::atomic<float>* scaleMode = nullptr;
        std::atomic<float>* scaleOctaves = nullptr;
        std::atomic<float>* engineSource = nullptr;
        std::atomic<float>* samplePlaybackMode = nullptr;
        std::atomic<float>* spectralElement = nullptr;
        std::atomic<float>* spectralPartialCount = nullptr;
        std::atomic<float>* spectralPartialSolo = nullptr;
        std::atomic<float>* spectralStretch = nullptr;
        std::atomic<float>* atomicScaleMode = nullptr;
        std::atomic<float>* audioMidiOutputMode = nullptr;
        std::atomic<float>* midiOutputType = nullptr;
        std::atomic<float>* normalMidiChannel = nullptr;
        std::atomic<float>* mpeMasterChannel = nullptr;
        std::atomic<float>* mpeMemberFirstChannel = nullptr;
        std::atomic<float>* mpeMemberLastChannel = nullptr;
        std::atomic<float>* mpePitchBendRange = nullptr;
        std::atomic<float>* mpeSendSetupMessages = nullptr;
        std::atomic<float>* mpePitchMode = nullptr;
        std::atomic<float>* externalMidiPitchMode = nullptr;
    } rawParams;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudienceProcessor)
};
