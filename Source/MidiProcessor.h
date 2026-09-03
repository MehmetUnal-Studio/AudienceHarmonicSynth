#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "MidiEngine.h"
#include "MidiScaleModule.h"
#include "OscBridge.h"
#include "Simulator.h"

class AudienceMidiProcessor : public juce::AudioProcessor,
                              private juce::Timer,
                              public SeatEventSink
{
public:
    struct MidiMonitorEvent
    {
        uint32_t serial = 0;
        int type = 0;
        int channel = 0;
        int data1 = 0;
        int data2 = 0;
    };

    AudienceMidiProcessor();
    ~AudienceMidiProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override           { return true; }
    bool producesMidi() const override          { return true; }
    bool isMidiEffect() const override          { return true; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override                      { return 1; }
    int getCurrentProgram() override                   { return 0; }
    void setCurrentProgram (int) override              {}
    const juce::String getProgramName (int index) override
    {
        return index == 0 ? juce::String { "Default" } : juce::String {};
    }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& dest) override;
    void setStateInformation (const void* data, int size) override;

    void setX  (int row, int col, float xNorm) override;
    void setY  (int row, int col, float yNorm) override;
    void setOn (int row, int col, bool on) override;

    juce::AudioProcessorValueTreeState apvts;
    MidiEngine engine;
    MidiScaleModule scaleModule;
    OscBridge osc;
    Simulator simulator;

    int udpPort = 6060;
    juce::String oscStatus;
    void setUdpPort (int port);
    void panic();
    juce::String getIncomingMonitorText() const;
    juce::String getOutgoingMonitorText() const;
    std::array<MidiMonitorEvent, 32> getOutgoingMonitorEvents() const;
    juce::String getMidiOutputDescription() const;
    juce::StringArray getMidiOutputOptions() const;
    juce::String getMidiOutputStatus() const;
    int getMidiOutputOptionIndex() const noexcept { return midiOutputOptionIndex.load(std::memory_order_relaxed); }
    void setMidiOutputOptionIndex (int index);

private:
    enum class InputType : int { X = 1, Y, On, Off };
    enum class OutputType : int { NoteOn = 1, NoteOff, CC, AllNotesOff };

    struct InputSlot
    {
        std::atomic<uint32_t> serial { 0 };
        std::atomic<int> type { 0 };
        std::atomic<int> row { 0 };
        std::atomic<int> col { 0 };
        std::atomic<float> value { 0.0f };
    };

    struct OutputSlot
    {
        std::atomic<uint32_t> serial { 0 };
        std::atomic<int> type { 0 };
        std::atomic<int> channel { 0 };
        std::atomic<int> data1 { 0 };
        std::atomic<int> data2 { 0 };
    };

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void cacheParameterPointers();
    void pullParams();
    void logInput (InputType type, int row, int col, float value) noexcept;
    void logOutputMidi (const juce::MidiBuffer& midi) noexcept;
    void timerCallback() override;
    void copyMidiBuffer (const juce::MidiBuffer& source, juce::MidiBuffer& dest);
    void queueMidiToExternal (const juce::MidiBuffer& midi) noexcept;
    void drainExternalMidiQueue();
    void sendImmediateAllNotesOffToExternal();
    void closeMidiOutput();

    struct PackedMidiEvent
    {
        juce::uint8 size = 0;
        juce::uint8 data[3] {};
    };

    static constexpr size_t realtimeMidiBufferReserveBytes = 262144;
    static constexpr int externalMidiQueueSize = 8192;

    int lastChannel = -1;
    int lastRoot = -1;
    int lastRangeLowOctave = -1;
    int lastRangeHighOctave = -1;
    int lastScaleMode = -1;
    int lastTranspose = -999;
    int instanceId = 1;
    double currentSampleRate = 44100.0;
    juce::MidiBuffer renderScratch;
    juce::MidiBuffer scaleScratch;
    bool scaleScratchLoanedToHost = false;
    juce::AbstractFifo externalMidiFifo { externalMidiQueueSize };
    std::array<PackedMidiEvent, externalMidiQueueSize> externalMidiEvents {};
    std::atomic<uint32_t> externalMidiDropped { 0 };
    std::atomic<int> midiOutputOptionIndex { 0 };
    juce::String midiOutputStatus { "Host MIDI Output" };
    juce::CriticalSection midiOutputLock;
    std::unique_ptr<juce::MidiOutput> midiOutput;
    std::array<InputSlot, 32> inputSlots;
    std::array<OutputSlot, 32> outputSlots;
    std::atomic<uint32_t> inputSerial { 0 };
    std::atomic<uint32_t> outputSerial { 0 };

    struct RawParams
    {
        std::atomic<float>* channel = nullptr;
        std::atomic<float>* root = nullptr;
        std::atomic<float>* rangeLowOctave = nullptr;
        std::atomic<float>* rangeHighOctave = nullptr;
        std::atomic<float>* scaleMode = nullptr;
        std::atomic<float>* transpose = nullptr;
        std::atomic<float>* midiScaleEnabled = nullptr;
        std::atomic<float>* midiScaleCorrection = nullptr;
        std::atomic<float>* midiScaleCustomMask = nullptr;
        std::array<std::atomic<float>*, 12> midiScaleRemap {};
    } rawParams;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudienceMidiProcessor)
};
