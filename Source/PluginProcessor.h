#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "AdaptiveCrowdGovernor.h"
#include "AtomicScaleCatalog.h"
#include "AtomicScaleMap.h"
#include "CrowdExpressionMacros.h"
#include "CrowdTimeField.h"
#include "FactoryPresets.h"
#include "GlobalConductorHub.h"
#include "MidiAudienceModel.h"
#include "MidiPitchMap.h"
#include "MpeMidiOutput.h"
#include "OscBridge.h"
#include "OscFingerRouter.h"
#include "PressureAwareSafetyGovernor.h"
#include "Simulator.h"
#include "SourceQualityController.h"

// Cosmic Microwave is intentionally a silent instrument shell: keeping the
// existing stereo instrument contract preserves Ableton placement and VST3
// session identity, while all runtime output is MIDI.
class AudienceProcessor : public juce::AudioProcessor,
                          private juce::Timer,
                          private juce::HighResolutionTimer,
                          private juce::AsyncUpdater
{
public:
    enum class FreshRouteAssignmentState
    {
        pending = 0,
        assigned,
        exhausted,
        preserved
    };

    AudienceProcessor();
    ~AudienceProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&,
                               juce::MidiBuffer&) override;

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
    double getTailLengthSeconds() const override { return 120.0; }

    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int index) override
    {
        return index == 0 ? juce::String { "Default" } : juce::String {};
    }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Message-thread/editor surface.
    juce::AudioProcessorValueTreeState apvts;
    OscFingerRouter fingerRouter;
    SourceQualityController sourceQualityController;
    MidiAudienceModel audienceModel;
    OscBridge osc;
    Simulator simulator;

    int getUdpPort() const noexcept { return udpPort.load(std::memory_order_relaxed); }
    void setUdpPort (int port);
    juce::String oscStatus;
    void panic();

    int getMidiNotesSent() const noexcept    { return mpeOut.getMidiNotesSent(); }
    int getScheduledMidiNoteCount() const noexcept
    {
        return mpeOut.getScheduledNoteCount();
    }
    int getPhysicalMidiNoteCount() const noexcept
    {
        return mpeOut.getPhysicalNoteCount();
    }
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
    double getNoteDurationBpm() const noexcept
    {
        return noteDurationBpm.load(std::memory_order_relaxed);
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
    struct EffectiveTimeFieldPolicy
    {
        CrowdTimeField::Mode mode = CrowdTimeField::Mode::Flow;
        int attacksPerStep = CosmicFactoryPresets::maxAttacksPerStep;
        int activeLimit = CosmicFactoryPresets::maxActiveVoices;
        int spreadSlots = 16;
        bool admissionOpen = true;
    };
    // One coherent audio-thread snapshot after Adaptive, Safety Governor,
    // Global Conductor and per-block Flow clamps have all been applied.
    // Packing the fields into one atomic prevents the editor from combining
    // limits published by different callbacks.
    EffectiveTimeFieldPolicy getEffectiveTimeFieldPolicy() const noexcept;
    int getGovernorRecentSourceCount() const noexcept
    {
        return governorRecentSourceCount.load(std::memory_order_relaxed);
    }
    int getGovernorObservedDensity() const noexcept
    {
        return governorObservedDensity.load(std::memory_order_relaxed);
    }
    int getGovernorEffectiveAttacksPerStep() const noexcept
    {
        return governorEffectiveAttacks.load(std::memory_order_relaxed);
    }
    int getGovernorEffectiveActiveVoices() const noexcept
    {
        return governorEffectiveActive.load(std::memory_order_relaxed);
    }
    int getGovernorEffectiveSpreadSlots() const noexcept
    {
        return governorEffectiveSpread.load(std::memory_order_relaxed);
    }
    int getGovernorBand() const noexcept
    {
        return governorBand.load(std::memory_order_relaxed);
    }
    int getSafetyGovernorState() const noexcept
    {
        return safetyGovernorState.load(std::memory_order_relaxed);
    }
    uint32_t getSafetyGovernorReasonBits() const noexcept
    {
        return safetyGovernorReasons.load(std::memory_order_relaxed);
    }
    double getIngressEventsPerSecond() const noexcept
    {
        return safetyIngressRate.load(std::memory_order_relaxed);
    }
    double getProcessDeadlineRatio() const noexcept
    {
        return lastProcessDeadlineRatio.load(std::memory_order_relaxed);
    }
    double getExternalFifoPressure() const noexcept
    {
        return safetyExternalFifoPressure.load(std::memory_order_relaxed);
    }
    double getExternalFifoOldestAgeSeconds() const noexcept
    {
        return externalFifoOldestAgeSeconds.load(std::memory_order_relaxed);
    }
    SourceQualityController::Output getSourceQualityOutput() const noexcept
    {
        // Both the processor timer and editor timer are message-thread owned.
        // The audio callback consumes only sourceQualityAdmissionOpen below.
        return sourceQualityController.getOutput();
    }
    void startSourceQualityCheck();
    void stopSourceQualityCheck();
    bool isSourceQualityCheckArmed() const noexcept
    {
        return sourceQualityController.isArmed();
    }
    int getConductorSnapshotSource() const noexcept
    {
        return static_cast<int>(conductorAudioPolicy.load(
            std::memory_order_acquire) & 0x3u);
    }
    int getConductorAttackQuota() const noexcept
    {
        return static_cast<int>((conductorAudioPolicy.load(
            std::memory_order_acquire) >> 2u) & 0x1fu);
    }
    int getConductorVoiceQuota() const noexcept
    {
        return static_cast<int>((conductorAudioPolicy.load(
            std::memory_order_acquire) >> 7u) & 0x1fu);
    }
    int getConductorActiveZoneCount() const noexcept
    {
        return conductorActiveZones.load(std::memory_order_relaxed);
    }
    int getConductorLeaderPort() const noexcept
    {
        return conductorLeaderPort.load(std::memory_order_relaxed);
    }
    int getConductorRegistrationStatus() const noexcept
    {
        return conductorRegistrationStatus.load(std::memory_order_relaxed);
    }
    bool getCrowdMacroEffectiveEnabled() const noexcept
    {
        return crowdMacroEffectiveEnabled.load(std::memory_order_relaxed);
    }
    int getCrowdMacroActiveSources() const noexcept
    {
        return crowdMacroActiveSources.load(std::memory_order_relaxed);
    }
    double getCrowdMacroDensity() const noexcept
    {
        return crowdMacroDensity.load(std::memory_order_relaxed);
    }
    double getCrowdMacroCentroidX() const noexcept
    {
        return crowdMacroCentroidX.load(std::memory_order_relaxed);
    }
    double getCrowdMacroCentroidY() const noexcept
    {
        return crowdMacroCentroidY.load(std::memory_order_relaxed);
    }
    double getCrowdMacroMotion() const noexcept
    {
        return crowdMacroMotion.load(std::memory_order_relaxed);
    }
    int getCrowdMacroDensityCcValue() const noexcept
    {
        return crowdMacroDensityCcValue.load(std::memory_order_relaxed);
    }
    int getCrowdMacroCentroidXCcValue() const noexcept
    {
        return crowdMacroCentroidXCcValue.load(std::memory_order_relaxed);
    }
    int getCrowdMacroCentroidYCcValue() const noexcept
    {
        return crowdMacroCentroidYCcValue.load(std::memory_order_relaxed);
    }
    int getCrowdMacroMotionCcValue() const noexcept
    {
        return crowdMacroMotionCcValue.load(std::memory_order_relaxed);
    }
    int getMidiOutputOptionIndex() const noexcept { return midiOutputOptionIndex.load(std::memory_order_relaxed); }
    bool isMidiOutputReady() const noexcept { return midiOutputReady.load(std::memory_order_acquire); }
    int getResolvedMidiOutputOptionIndex();
    uint32_t getMidiOutputRouteRevision() const noexcept { return midiOutputRouteRevision.load(std::memory_order_acquire); }
    void setMidiOutputOptionIndex (int index);
    int getMidiOutputPath() const noexcept;
    int getExpectedZone() const noexcept { return osc.getExpectedZone(); }

    // Message-thread factory presets. Zone A is the new-instance default;
    // B-H keep the same musical setup while selecting sequential UDP ports and
    // follower roles. Host-restored state remains authoritative.
    static int getNumFactoryPresets() noexcept;
    static juce::String getFactoryPresetName (int index);
    void applyFactoryPreset (int index);
    int getMatchingFactoryPresetIndex() const noexcept;
    FreshRouteAssignmentState getFreshRouteAssignmentState() const noexcept
    {
        return static_cast<FreshRouteAssignmentState> (
            freshRouteAssignmentState.load (std::memory_order_acquire));
    }
    void retryFreshRouteAssignment();

private:
    friend struct ProcessorMidiHotplugTestAccess;
    friend struct ProcessorAutoRoutingTestAccess;

    void timerCallback() override;
    void hiResTimerCallback() override;
    void handleAsyncUpdate() override;
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void cacheParameterPointers();
    void updatePitchMap();
    bool processIncomingMidi (const juce::MidiBuffer&, int numSamples);
    void releaseAllIncomingMidiNotes() noexcept;
    struct WatchdogCancelRenderResult
    {
        int processedCount = 0;
        std::uint64_t resetAcknowledgedTarget = 0;
    };
    WatchdogCancelRenderResult renderOutgoingMidi (
        juce::MidiBuffer& midiMessages, int numSamples, bool outputEnabled,
        const MpeMidiOutput::MpeConfig& midiConfig,
        const CrowdTimeField::Config& timeConfig,
        const CrowdTimeField::ClockFrame& clockFrame,
        bool resetAlreadyEmitted);
    WatchdogCancelRenderResult renderTimedOutgoingMidi (
        juce::MidiBuffer& midiMessages, int numSamples, bool outputEnabled,
        const MpeMidiOutput::MpeConfig& midiConfig,
        const CrowdTimeField::Config& timeConfig,
        const CrowdTimeField::ClockFrame& clockFrame,
        bool resetAlreadyEmitted);
    void acknowledgeWatchdogCancelRenderResult (
        const WatchdogCancelRenderResult& result) noexcept;
    MpeMidiOutput::MpeConfig buildMpeConfig() const;
    CrowdTimeField::Config buildTimeFieldConfig (
        const MpeMidiOutput::MpeConfig& midiConfig) const noexcept;
    CrowdTimeField::ClockFrame captureTimeFieldClock (int numSamples,
                                                       double monotonicSeconds) const noexcept;
    void requestTimeFieldRehydrate() noexcept;
    std::uint64_t snapshotTimeFieldRehydrateRequest() const noexcept;
    bool isTimeFieldRehydratePending (std::uint64_t requestTarget) const noexcept;
    void acknowledgeTimeFieldRehydrate (std::uint64_t requestTarget) noexcept;
    void rehydrateTimeFieldFromCanonical() noexcept;
    void recordIncomingMidiDebugEvents (const juce::MidiBuffer& midiMessages) noexcept;
    void queueMidiToExternalOutput (const juce::MidiBuffer& midiMessages,
                                    double blockStartTimeMs,
                                    int numSamples,
                                    std::uint32_t blockResetGeneration) noexcept;
    void requestExternalMidiReset (std::atomic<bool>& resetFlag) noexcept;
    void serviceExternalWatchdogFallback (std::uint32_t now,
                                          int expiredLiveTouches) noexcept;
    bool isExternalMidiResetPending() const noexcept;
    void acknowledgeExternalMidiReset() noexcept;
    void drainExternalMidiOutputQueue();
    void discardExternalMidiOutputQueue() noexcept;
    void sendExternalResetSweep();
    void sendImmediateAllNotesOffToExternal (bool processingAlreadySuspended = false);
    void sendImmediateExternalAllNotesOffOnly();
    void closeMidiOutput();
    void setMidiOutputOptionIndexInternal (int index);
    void setUdpPortInternal (int port, bool reopenVirtualEndpoint = true);
    void restoreMidiOutputRoute (int routeKind,
                                 const juce::String& deviceIdentifier,
                                 int legacyOptionIndex);
    void withholdVirtualMidiOutputForUnavailableUdp();
    void commitExplicitRouteEditLocked();
    bool applyPendingRouteSnapshotLocked();
    void drainPendingApvtsStateQueue();
    void performPendingRuntimeResetLocked (std::uint32_t generation);
    void maintainPhysicalMidiOutputRouteLocked (std::uint32_t nowMs);
    void maintainVirtualMidiOutputRouteLocked (std::uint32_t nowMs);
    void refreshGlobalConductor();
    void updateSourceQualityController (std::uint32_t nowMs) noexcept;
    void restartSourceQualityEpoch (std::uint32_t nowMs) noexcept;
    SourceQualityController::ExternalCounters
        getSourceQualityExternalCounters() const noexcept;
    void unregisterGlobalConductor() noexcept;
    void deactivateExternalMidiForRestoreLocked();
    bool deactivatePendingOscForRestoreLocked();
    bool handleFreshRouteAssignment();
    bool freshRouteAssignmentIsEligibleLocked (bool requireUnclaimedOsc = true);
    void disableFreshRouteAssignmentLocked() noexcept;

    struct CrowdMacroRoutingConfig
    {
        bool effectiveEnabled = false;
        int channelChoice = 0; // 0..15 = one channel, 16 = broadcast.
        std::array<int, 4> controllers { 20, 21, 22, 23 };
        int rateChoice = 1;
        double rateHz = 10.0;
    };

    CrowdMacroRoutingConfig buildCrowdMacroRoutingConfig (
        bool outputEnabled, bool safetyEnabled) const noexcept;
    void renderCrowdExpressionMacros (
        juce::MidiBuffer& midiMessages, int numSamples,
        double monotonicSeconds, const CrowdMacroRoutingConfig&,
        bool resetBoundary) noexcept;
    void resetCrowdExpressionMacros() noexcept;

    static constexpr size_t realtimeMidiBufferReserveBytes = 262144;
    static constexpr size_t realtimeMidiInputBudgetBytes = 131072;
    static constexpr int realtimeMidiInputEventLimit = 256;
    static constexpr int midiLifecycleEventBudget = 64;
    // The audio path emits at most 64 OSC/retrigger lifecycles and 256 filtered
    // MIDI note-thru events per block. Leave several blocks of
    // headroom for timestamped output while the dedicated sender catches up;
    // overflow still has ordered panic recovery and held-finger rehydration.
    static constexpr int externalMidiQueueSize = 16384;
    juce::MidiBuffer midiInputScratch;
    juce::MidiBuffer midiRenderScratch;
    bool midiRenderScratchLoanedToHost = false;
    // Audio-thread-owned transition latch. reset() mutates it only while the
    // callback gate is closed, so bypass can publish one bounded reset without
    // repeating the 16-channel sweep on every bypassed block.
    bool bypassResetEmitted = false;
    std::array<OscFingerRouter::Event, midiLifecycleEventBudget> fingerEventScratch {};
    std::array<int, midiLifecycleEventBudget> flowMotionVoiceScratch {};
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
    CrowdExpressionMacros crowdExpressionMacros;
    CrowdExpressionMacros::Input crowdMacroInputScratch;
    CrowdExpressionMacros::Output crowdMacroOutput;
    CrowdMacroRoutingConfig lastCrowdMacroConfig;
    double crowdMacroLastAnalysisSeconds = 0.0;
    bool crowdMacroAnalysisClockInitialised = false;
    bool crowdMacroConfigInitialised = false;
    AdaptiveCrowdGovernor adaptiveCrowdGovernor;
    PressureAwareSafetyGovernor pressureSafetyGovernor;
    PressureAwareSafetyGovernor::Output safetyOutput {};
    double governorLastUpdateSeconds = 0.0;
    bool governorControlClockInitialised = false;
    int governorLastVoiceLimit = CosmicFactoryPresets::maxActiveVoices;
    double safetyLastUpdateSeconds = 0.0;
    uint32_t safetyLastOscMessages = 0;
    uint32_t safetyLastDroppedMotion = 0;
    bool safetyClockInitialised = false;
    bool pitchMapChangedThisBlock = false;
    // Control/state paths may request a canonical rebuild while processBlock is
    // already running. A monotonic request/ack pair avoids both a plain-bool
    // data race and the lost-wakeup ABA where audio clears a newer request.
    std::atomic<std::uint64_t> timeFieldRehydrateRequestGeneration { 1 };
    std::uint64_t timeFieldRehydrateAcknowledgedGeneration = 0; // audio owner

    struct PackedMidiEvent
    {
        juce::uint8 size = 0;
        juce::uint8 data[3] {};
        double dueTimeMs = 0.0;
        std::uint32_t resetGeneration = 0;
    };

    juce::AbstractFifo externalMidiFifo { externalMidiQueueSize };
    std::array<PackedMidiEvent, externalMidiQueueSize> externalMidiEvents {};
    std::atomic<uint32_t> externalMidiDropped { 0 };
    std::atomic<bool> externalMidiPanicPending { false };
    std::atomic<bool> externalMidiProducerQuarantined { false };
    std::atomic<bool> externalTransportResetPending { false };
    // A monotonically tagged request/ack pair closes the boolean ABA window
    // where releaseResources can publish a second reset while the sender is
    // completing the first. The audio path only performs lock-free loads and
    // one fetch_add when it requests overflow/path-change recovery.
    std::atomic<std::uint32_t> externalMidiResetRequestGeneration { 0 };
    std::atomic<std::uint32_t> externalMidiResetAcknowledgedGeneration { 0 };
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
    std::atomic<int> midiOutputOptionIndex { CosmicFactoryPresets::midiOutputOption };
    std::atomic<bool> midiOutputReady { false };
    std::atomic<uint32_t> midiOutputRouteRevision { 0 };
    juce::String midiOutputStatus { "Virtual MIDI port pending" };
    int midiOutputRouteKind = 0; // 0 host, 1 virtual, 2 physical
    juce::String midiOutputDeviceIdentifier;

    // Serialises publication of complete host-state/factory-preset snapshots
    // and route mutations. The audio thread never takes either lock. Locks are
    // deliberately released before APVTS/host callbacks are invoked.
    juce::CriticalSection stateTransactionLock;
    juce::CriticalSection pendingStateLock;
    // A host may synchronously request state while a factory recall or APVTS
    // restore is notifying parameters. Serve the complete target snapshot
    // instead of a partially applied tree. Protected by stateTransactionLock.
    bool stateSnapshotOverrideInProgress = false;
    std::uint32_t stateSnapshotOverrideGeneration = 0;
    juce::ValueTree stateSnapshotOverride;
    // APVTS replacement can synchronously re-enter setStateInformation. The
    // outermost owner drains this latest-wins queue after every callback has
    // unwound, preventing an older replaceState from overwriting a nested one.
    bool stateMutationOwnerActive = false;
    juce::ValueTree pendingApvtsState;
    std::uint32_t pendingApvtsGeneration = 0;
    bool routeSnapshotApplyInProgress = false;
    std::uint32_t pendingRuntimeResetGeneration = 0;
    std::uint32_t pendingFactoryRuntimeGeneration = 0;
    // A newly inserted instance owns no route until its first message-thread
    // timer tick. The tick claims the lowest free A-H factory UDP port with a
    // retained exclusive bind. Any host restore or explicit route edit turns
    // this off permanently so saved Ableton projects never reshuffle zones.
    bool freshRouteAssignmentEligible = true; // guarded by stateTransactionLock
    std::atomic<int> freshRouteAssignmentState {
        static_cast<int> (FreshRouteAssignmentState::pending)
    };
    std::uint32_t lastFreshRouteAssignmentAttemptMs = 0;

    // Publish restore intent before XML parsing. The first message-thread
    // timer can then quarantine automatic route assignment while a host worker
    // is still decoding a saved snapshot. The counter supports nested or
    // concurrent host callbacks without letting an earlier invalid blob clear
    // a later valid restore's intent.
    std::atomic<std::uint32_t> hostStateRestoreIntentCount { 0 };

    // A valid state generation retires the previous complete route in two
    // phases. CoreMIDI and Conductor can be made fail-closed synchronously on
    // the host control thread; OscBridge's UI-visible state is mutated only by
    // the message thread through AsyncUpdater (with Timer as a fallback).
    // Guarded by stateTransactionLock.
    std::uint32_t pendingOscDeactivationGeneration = 0;

    // The APVTS and the external route complete in two phases. Host MIDI may
    // resume after the complete parameter tree is installed; OSC/external MIDI
    // remains fail-closed until the message-thread route generation is ready.
    // This also delays fresh-instance endpoint ownership by one timer tick,
    // giving an Ableton-restored B-H instance a chance to avoid transiently
    // claiming the Zone A endpoint.
    std::atomic<std::uint32_t> stateRestoreGeneration { 1 };
    std::atomic<std::uint32_t> stateRestoreCompletedGeneration { 1 };
    std::atomic<std::uint32_t> stateRouteReadyGeneration { 0 };
    // Audio-thread acknowledgement for the bounded Host/Mirror reset that
    // terminates notes emitted by the previous state generation. Unlike the
    // message-thread runtime reset, this must still work in a headless/offline
    // host whose JUCE Timer never gets a tick.
    std::atomic<std::uint32_t> pendingHostMidiResetGeneration { 0 };
    std::atomic<bool> pendingStateApply { true };
    std::uint32_t pendingStateGeneration = 1;
    std::atomic<int> udpPort { CosmicFactoryPresets::firstUdpPort };
    int pendingUdpPort = CosmicFactoryPresets::firstUdpPort;
    int pendingMidiOutputOption = CosmicFactoryPresets::midiOutputOption;
    int pendingMidiOutputRouteKind = CosmicFactoryPresets::midiOutputRouteKind;
    juce::String pendingMidiOutputDeviceIdentifier;

    MpeMidiOutput mpeOut;
    // Control-thread panic/route mutation handshake. processBlock increments
    // before consulting the gate; a control mutation closes the gate and
    // waits for the bounded in-flight callback to retire before touching any
    // audio-owned scheduler, Time Field, Governor or FIFO state.
    std::atomic<bool> controlAudioMutationGate { false };
    std::atomic<int> audioCallbacksInFlight { 0 };
    std::atomic<std::uint32_t> latestAudioBlockDurationMs { 0 };
    // Watchdog Cancel acknowledgement crosses message/audio threads through
    // cumulative counters, so a 256-source expiry cannot be mistaken for done
    // after the first 64-event realtime block.
    std::atomic<std::uint64_t> watchdogCancelsPublished { 0 };
    std::atomic<std::uint64_t> watchdogCancelsProcessed { 0 };
    // Message-timer-only fallback state.
    bool externalWatchdogCancelPending = false;
    bool externalWatchdogResetRequested = false;
    std::uint64_t externalWatchdogCancelTargetCount = 0;
    std::uint32_t externalWatchdogCancelSinceMs = 0;
    std::uint32_t externalWatchdogCancelDrainBlocks = 1;
    std::uint32_t externalWatchdogResetAckGeneration = 0;
    double currentSampleRate = 44100.0;
    int lastMidiOutputType = CosmicFactoryPresets::midiOutputType;
    int lastMidiOutputPath = CosmicFactoryPresets::midiOutputPath;
    int lastNormalMidiRoutingMode = CosmicFactoryPresets::normalMidiRoutingMode;
    int lastNormalMidiChannel = CosmicFactoryPresets::normalMidiChannel;
    int lastMpeBendRange = 2;
    int lastMpeMaster = 1;
    int lastMpeMemberFirst = 2;
    int lastMpeMemberLast = 16;
    int lastMpeSetupEnabled = 1;
    int lastMpePitchMode = 0;
    int lastTimeMode = CosmicFactoryPresets::timeMode;
    int lastClockSource = CosmicFactoryPresets::clockSource;
    int lastGridDivision = CosmicFactoryPresets::gridDivision;
    float lastInternalBpm = CosmicFactoryPresets::internalBpm;
    float lastGatePercent = CosmicFactoryPresets::gatePercent;
    bool hostTransportStateInitialised = false;
    bool lastHostTransportPlaying = false;
    bool lastExclusiveUdpPort = CosmicFactoryPresets::exclusiveUdpPort != 0;
    int lastExpectedZoneChoice = -1;
    std::uint32_t lastOscRetryMs = 0;
    std::uint32_t lastMidiHotplugScanMs = 0;
    std::uint32_t lastVirtualMidiRetryMs = 0;

    std::atomic<double> timeFieldBpm { CosmicFactoryPresets::internalBpm };
    std::atomic<double> noteDurationBpm { CosmicFactoryPresets::internalBpm };
    std::atomic<int> timeFieldPending { 0 };
    std::atomic<int> timeFieldActive { 0 };
    std::atomic<uint32_t> timeFieldMerged { 0 };
    std::atomic<bool> timeFieldClockLocked { false };
    std::atomic<std::uint64_t> effectiveTimeFieldPolicyPacked { 0 };
    std::atomic<int> governorRecentSourceCount { 0 };
    std::atomic<int> governorObservedDensity { 0 };
    std::atomic<int> governorEffectiveAttacks { 4 };
    std::atomic<int> governorEffectiveActive { 8 };
    std::atomic<int> governorEffectiveSpread { 1 };
    std::atomic<int> governorBand { 0 };
    std::atomic<int> safetyGovernorState { 0 };
    std::atomic<uint32_t> safetyGovernorReasons { 0 };
    std::atomic<double> safetyIngressRate { 0.0 };
    std::atomic<double> safetyExternalFifoPressure { 0.0 };
    // Runtime-only Ready Gate. Fresh/saved projects always start open/BYPASS;
    // the operator explicitly arms a new 64/128/256 source census.
    std::atomic<bool> sourceQualityAdmissionOpen { true };
    std::uint32_t sourceQualityLastUpdateMs = 0;
    bool sourceQualityUpdateClockInitialised = false;
    std::atomic<double> externalFifoOldestAgeSeconds { 0.0 };
    std::atomic<double> lastProcessDeadlineRatio { 0.0 };
    std::atomic<bool> crowdMacroEffectiveEnabled { false };
    std::atomic<int> crowdMacroActiveSources { 0 };
    std::atomic<double> crowdMacroDensity { 0.0 };
    std::atomic<double> crowdMacroCentroidX { 0.5 };
    std::atomic<double> crowdMacroCentroidY { 0.5 };
    std::atomic<double> crowdMacroMotion { 0.0 };
    std::atomic<int> crowdMacroDensityCcValue { 0 };
    std::atomic<int> crowdMacroCentroidXCcValue { 64 };
    std::atomic<int> crowdMacroCentroidYCcValue { 64 };
    std::atomic<int> crowdMacroMotionCcValue { 0 };
    GlobalConductorHub::Handle conductorHandle {};
    int conductorRegisteredPort = 0;
    int conductorRegisteredRoleChoice = -1;
    int conductorRegisteredGroup = -1;
    int conductorRegisteredZoneChoice = -1;
    std::atomic<int> conductorRegistrationStatus {
        (int) GlobalConductorHub::RegistrationStatus::InvalidPort };
    // Source + both realtime quotas are published as one release/acquire word.
    // The audio callback can therefore never combine a new Global source with
    // stale quotas from an earlier control-timer publication.
    std::atomic<std::uint32_t> conductorAudioPolicy {
        (0u) | (4u << 2u) | (8u << 7u) };
    std::atomic<int> conductorActiveZones { 0 };
    std::atomic<int> conductorLeaderPort { 0 };

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
        std::atomic<float>* midiOutputPath = nullptr;
        std::atomic<float>* expectedZone = nullptr;
        std::atomic<float>* exclusiveUdpPort = nullptr;
        std::atomic<float>* safetyGovernorEnabled = nullptr;
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
        std::atomic<float>* crowdGovernorEnabled = nullptr;
        std::atomic<float>* conductorRole = nullptr;
        std::atomic<float>* conductorGroup = nullptr;
        std::atomic<float>* conductorAttackBudget = nullptr;
        std::atomic<float>* conductorVoiceBudget = nullptr;
        std::atomic<float>* crowdMacrosEnabled = nullptr;
        std::atomic<float>* crowdMacroChannel = nullptr;
        std::atomic<float>* crowdMacroDensityCc = nullptr;
        std::atomic<float>* crowdMacroCentroidXCc = nullptr;
        std::atomic<float>* crowdMacroCentroidYCc = nullptr;
        std::atomic<float>* crowdMacroMotionCc = nullptr;
        std::atomic<float>* crowdMacroRate = nullptr;
        std::atomic<float>* noteDuration = nullptr;
        std::atomic<float>* sourceCapacity = nullptr;
        std::atomic<float>* ensembleSameNoteMode = nullptr;
    } rawParams;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudienceProcessor)
};
