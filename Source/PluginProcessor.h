#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "AtomicScaleCatalog.h"
#include "AtomicScaleMap.h"
#include "CrowdTimeField.h"
#include "MidiAudienceModel.h"
#include "MidiPitchMap.h"
#include "MpeMidiOutput.h"
#include "OscBridge.h"
#include "OscFingerRouter.h"
#include "Simulator.h"

// Cosmic Microwave is intentionally a silent instrument shell: keeping the
// existing stereo instrument contract preserves Ableton placement and VST3
// session identity, while all runtime output is MIDI.
class AudienceProcessor : public juce::AudioProcessor,
                          private juce::Timer,
                          private juce::HighResolutionTimer
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
    bool acceptsMidi() const override
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

    // Message-thread/editor surface.
    juce::AudioProcessorValueTreeState apvts;
    OscFingerRouter fingerRouter;
    MidiAudienceModel audienceModel;
    OscBridge osc;
    Simulator simulator;

    int getUdpPort() const noexcept { return udpPort.load(std::memory_order_relaxed); }
    void setUdpPort (int port);
    juce::String oscStatus;
    void panic();

    int getMidiNotesSent() const noexcept    { return mpeOut.getMidiNotesSent(); }
    int getActiveMpeVoices() const noexcept  { return mpeOut.getActiveMpeVoices(); }
    int getAvailableMpeChannels() const noexcept { return mpeOut.getAvailableMpeChannels(); }
    int getActiveExternalMidiKeys() const noexcept { return activeExternalMidiKeys.load(std::memory_order_relaxed); }
    int getLastExternalMidiNote() const noexcept { return lastExternalMidiNote.load(std::memory_order_relaxed); }
    int getLastExternalMidiChannel() const noexcept { return lastExternalMidiChannel.load(std::memory_order_relaxed); }
    juce::String getExternalMidiPitchModeName() const { return "MIDI Thru"; }
    juce::String getOutgoingMidiDebugText (int maxEvents = 96) const;
    juce::String getIncomingMidiDebugText (int maxEvents = 96) const;
    juce::String getMidiStateDebugText() const;
    juce::String getMidiDebugReportText() const;

    juce::StringArray getMidiOutputOptions() const;
    juce::String getMidiOutputStatus() const;
    juce::String getMidiOutputDescription() const;
    juce::String getVirtualMidiPortName() const;
    juce::String getAtomicElementName (int index) const;
    juce::String getAtomicElementSymbol (int index) const;
    juce::String getAtomicModeName (int index) const;
    int getSelectedAtomicDegreeCount() const noexcept;
    double getSelectedAtomicReferenceWavelengthNm() const noexcept;
    double getTimeFieldBpm() const noexcept
    {
        return timeFieldBpm.load(std::memory_order_relaxed);
    }
    int getTimeFieldPending() const noexcept
    {
        return timeFieldPending.load(std::memory_order_relaxed);
    }
    int getTimeFieldActive() const noexcept
    {
        return timeFieldActive.load(std::memory_order_relaxed);
    }
    uint32_t getTimeFieldMerged() const noexcept
    {
        return timeFieldMerged.load(std::memory_order_relaxed);
    }
    bool getTimeFieldClockLocked() const noexcept
    {
        return timeFieldClockLocked.load(std::memory_order_relaxed);
    }
    int getMidiOutputOptionIndex() const noexcept { return midiOutputOptionIndex.load(std::memory_order_relaxed); }
    int getResolvedMidiOutputOptionIndex();
    uint32_t getMidiOutputRouteRevision() const noexcept { return midiOutputRouteRevision.load(std::memory_order_acquire); }
    void setMidiOutputOptionIndex (int index);

private:
    void timerCallback() override;
    void hiResTimerCallback() override;
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void cacheParameterPointers();
    void updatePitchMap();
    bool processIncomingMidi (const juce::MidiBuffer&);
    void releaseAllIncomingMidiNotes() noexcept;
    void renderOutgoingMidi (juce::MidiBuffer& midiMessages, int numSamples,
                             bool outputEnabled,
                             const CrowdTimeField::ClockFrame& clockFrame,
                             bool resetAlreadyEmitted);
    void renderTimedOutgoingMidi (juce::MidiBuffer& midiMessages, int numSamples,
                                  bool outputEnabled,
                                  const CrowdTimeField::ClockFrame& clockFrame,
                                  bool resetAlreadyEmitted);
    MpeMidiOutput::MpeConfig buildMpeConfig() const;
    CrowdTimeField::Config buildTimeFieldConfig() const noexcept;
    CrowdTimeField::ClockFrame captureTimeFieldClock (int numSamples,
                                                       double monotonicSeconds) const noexcept;
    void rehydrateTimeFieldFromCanonical() noexcept;
    void recordIncomingMidiDebugEvents (const juce::MidiBuffer& midiMessages) noexcept;
    void queueMidiToExternalOutput (const juce::MidiBuffer& midiMessages,
                                    double blockStartTimeMs,
                                    int numSamples) noexcept;
    void drainExternalMidiOutputQueue();
    void discardExternalMidiOutputQueue() noexcept;
    void sendExternalResetSweep();
    void sendImmediateAllNotesOffToExternal (bool processingAlreadySuspended = false);
    void closeMidiOutput();
    void restoreMidiOutputRoute (int routeKind,
                                 const juce::String& deviceIdentifier,
                                 int legacyOptionIndex);

    static constexpr size_t realtimeMidiBufferReserveBytes = 262144;
    static constexpr size_t realtimeMidiInputBudgetBytes = 131072;
    static constexpr int realtimeMidiInputEventLimit = 256;
    static constexpr int midiLifecycleEventBudget = 64;
    // The audio path emits at most 64 OSC/retrigger lifecycles plus 256 MIDI-thru
    // events per block. Leave several blocks of headroom for timestamped output
    // while the dedicated sender catches up; overflow still has ordered panic
    // recovery and held-finger rehydration.
    static constexpr int externalMidiQueueSize = 16384;
    juce::MidiBuffer midiInputScratch;
    juce::MidiBuffer midiRenderScratch;
    bool midiRenderScratchLoanedToHost = false;
    std::array<OscFingerRouter::Event, midiLifecycleEventBudget> fingerEventScratch {};
    std::array<MpeMidiOutput::NoteEvent, midiLifecycleEventBudget> midiNoteEventScratch {};
    std::array<CrowdTimeField::InputEvent, midiLifecycleEventBudget> timeFieldInputScratch {};
    std::array<CrowdTimeField::HeldVoice, CrowdTimeField::kMaxVoices> timeFieldHeldScratch {};
    CrowdTimeField::OutputBlock timeFieldOutputScratch;

    struct FingerMidiState
    {
        bool active = false;
        float x = 0.5f;
        float y = 0.5f;
        int pitchKey = -1;
        double frequencyHz = 261.6255653005986;
    };
    std::array<FingerMidiState, OscFingerRouter::MAX_VOICES> fingerMidiStates {};
    MidiPitchMap pitchMap;
    AtomicScaleMap atomicPitchMap;
    CrowdTimeField crowdTimeField;
    bool retriggerFingerMidi = false;
    int fingerRetriggerCursor = 0;
    bool pitchMapChangedThisBlock = false;
    bool timeFieldRehydratePending = true;

    struct PackedMidiEvent
    {
        juce::uint8 size = 0;
        juce::uint8 data[3] {};
        double dueTimeMs = 0.0;
    };

    juce::AbstractFifo externalMidiFifo { externalMidiQueueSize };
    std::array<PackedMidiEvent, externalMidiQueueSize> externalMidiEvents {};
    std::atomic<uint32_t> externalMidiDropped { 0 };
    std::atomic<bool> externalMidiPanicPending { false };
    std::atomic<bool> externalMidiProducerQuarantined { false };
    std::atomic<bool> externalTransportResetPending { false };
    std::atomic<bool> midiOutputRouteChangedPending { false };

#if COSMIC_MIDI_DIAGNOSTICS
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
    std::array<MidiDebugSlot, midiDebugEventQueueSize> incomingMidiDebugEvents {};
    std::atomic<uint32_t> incomingMidiDebugWriteCounter { 0 };
#endif

    std::unique_ptr<juce::MidiOutput> midiOutput;
    std::atomic<int> midiOutputOptionIndex { 0 };
    std::atomic<uint32_t> midiOutputRouteRevision { 0 };
    juce::String midiOutputStatus { "Host MIDI Output" };
    int midiOutputRouteKind = 0; // 0 host, 1 virtual, 2 physical
    juce::String midiOutputDeviceIdentifier;

    juce::CriticalSection pendingStateLock;
    std::atomic<bool> pendingStateApply { false };
    std::atomic<int> udpPort { 6060 };
    int pendingUdpPort = 6060;
    int pendingMidiOutputOption = 0;
    int pendingMidiOutputRouteKind = -1;
    juce::String pendingMidiOutputDeviceIdentifier;

    MpeMidiOutput mpeOut;
    double currentSampleRate = 44100.0;
    int lastMidiOutputType = 1;
    int lastNormalMidiRoutingMode = 1;
    int lastNormalMidiChannel = 0;
    int lastMpeBendRange = 2;
    int lastMpeMaster = 1;
    int lastMpeMemberFirst = 2;
    int lastMpeMemberLast = 16;
    int lastMpeSetupEnabled = 1;
    int lastMpePitchMode = 0;
    int lastTimeMode = 2;
    int lastClockSource = 0;
    int lastGridDivision = 2;
    int lastMaxAttacksPerStep = 4;
    int lastMaxActiveVoices = 16;
    int lastTemporalSpread = 2;
    float lastInternalBpm = 120.0f;
    float lastGatePercent = 70.0f;

    std::atomic<double> timeFieldBpm { 120.0 };
    std::atomic<int> timeFieldPending { 0 };
    std::atomic<int> timeFieldActive { 0 };
    std::atomic<uint32_t> timeFieldMerged { 0 };
    std::atomic<bool> timeFieldClockLocked { false };

    int lastScaleRootPitchClass = -1;
    int lastScaleRootOctave = -1;
    int lastScaleMode = -1;
    int lastScaleOctaves = -1;
    int lastPitchSystem = -1;
    int lastAtomicElement = -1;
    int lastAtomicScaleMode = -1;

    static constexpr int midiInputChannels = 16;
    static constexpr int midiInputNotes = 128;
    static constexpr int midiInputKeyCount = midiInputChannels * midiInputNotes;
    std::array<bool, midiInputKeyCount> incomingMidiKeys {};
    std::atomic<int> lastExternalMidiNote { -1 };
    std::atomic<int> lastExternalMidiChannel { -1 };
    std::atomic<int> activeExternalMidiKeys { 0 };

    struct RawParams
    {
        std::atomic<float>* scaleRoot = nullptr;
        std::atomic<float>* scaleRootOctave = nullptr;
        std::atomic<float>* scaleMode = nullptr;
        std::atomic<float>* scaleOctaves = nullptr;
        std::atomic<float>* pitchSystem = nullptr;
        std::atomic<float>* spectralElement = nullptr;
        std::atomic<float>* atomicScaleMode = nullptr;
        std::atomic<float>* midiOutputType = nullptr;
        std::atomic<float>* normalMidiRoutingMode = nullptr;
        std::atomic<float>* normalMidiChannel = nullptr;
        std::atomic<float>* mpeZone = nullptr;
        std::atomic<float>* mpePitchBendRange = nullptr;
        std::atomic<float>* mpeSendSetupMessages = nullptr;
        std::atomic<float>* mpePitchMode = nullptr;
        std::atomic<float>* timeMode = nullptr;
        std::atomic<float>* clockSource = nullptr;
        std::atomic<float>* internalBpm = nullptr;
        std::atomic<float>* gridDivision = nullptr;
        std::atomic<float>* maxAttacksPerStep = nullptr;
        std::atomic<float>* maxActiveVoices = nullptr;
        std::atomic<float>* gatePercent = nullptr;
        std::atomic<float>* temporalSpread = nullptr;
    } rawParams;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudienceProcessor)
};
