#include "../Source/PluginProcessor.h"
#include "../Source/PluginStateMigration.h"

#include <atomic>
#include <functional>
#include <iostream>
#include <memory>

struct ProcessorMidiHotplugTestAccess
{
    static void seedLongScheduledNote (AudienceProcessor& processor)
    {
        processor.mpeOut.reset();
        MpeMidiOutput::MpeConfig config;
        config.outputType = 1;
        config.normalMidiChannel = 0;

        MpeMidiOutput::TimingConfig timing;
        timing.sampleRate = 48000.0;
        timing.bpm = 1.0;
        timing.noteDuration = MpeMidiOutput::NoteDuration::Half;

        MpeMidiOutput::NoteEvent note;
        note.type = MpeMidiOutput::NoteEvent::NoteOn;
        note.sourceId = 0;
        note.participantId = 0;
        note.frequencyHz = 440.0;
        note.velocity = 0.8f;

        juce::MidiBuffer ignoredOutput;
        processor.mpeOut.render(config, &note, 1, ignoredOutput, 64, timing);
    }

    static void makeCurrentRouteReady (AudienceProcessor& processor)
    {
        const auto generation = processor.stateRestoreGeneration.load(
            std::memory_order_acquire);
        processor.stateRestoreCompletedGeneration.store(
            generation, std::memory_order_release);
        processor.stateRouteReadyGeneration.store(
            generation, std::memory_order_release);
    }

    static void enqueueImmediateNoteOn (AudienceProcessor& processor)
    {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        processor.externalMidiFifo.prepareToWrite (
            1, start1, size1, start2, size2);
        const int slot = size1 > 0 ? start1 : start2;
        auto& queued = processor.externalMidiEvents[static_cast<std::size_t>(slot)];
        queued.size = 3;
        queued.data[0] = 0x90;
        queued.data[1] = 60;
        queued.data[2] = 100;
        queued.dueTimeMs = 0.0;
        queued.resetGeneration =
            processor.externalMidiResetAcknowledgedGeneration.load(
                std::memory_order_acquire);
        processor.externalMidiFifo.finishedWrite (1);
    }

    static void simulateBooleanAbaWithNewerReset (AudienceProcessor& processor)
    {
        processor.externalMidiResetAcknowledgedGeneration.store (
            1, std::memory_order_release);
        processor.externalMidiResetRequestGeneration.store (
            2, std::memory_order_release);
        processor.externalMidiProducerQuarantined.store (
            false, std::memory_order_release);
        processor.externalMidiPanicPending.store (
            false, std::memory_order_release);
        processor.externalTransportResetPending.store (
            false, std::memory_order_release);
    }

    static void commitOldReservationAfterResetAck (
        AudienceProcessor& processor)
    {
        const auto oldGeneration =
            processor.externalMidiResetAcknowledgedGeneration.load(
                std::memory_order_acquire);
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        processor.externalMidiFifo.prepareToWrite(
            1, start1, size1, start2, size2);
        const int slot = size1 > 0 ? start1 : start2;
        auto& queued = processor.externalMidiEvents[
            static_cast<std::size_t>(slot)];
        queued.size = 3;
        queued.data[0] = 0x90;
        queued.data[1] = 61;
        queued.data[2] = 100;
        queued.dueTimeMs = 0.0;
        queued.resetGeneration = oldGeneration;

        processor.requestExternalMidiReset(
            processor.externalMidiPanicPending);
        processor.externalMidiPanicPending.store(
            false, std::memory_order_release);
        processor.acknowledgeExternalMidiReset();

        // This models the producer publishing its already-reserved slot only
        // after the reset consumer discarded the visible queue and swept.
        processor.externalMidiFifo.finishedWrite(1);
    }

    static void drain (AudienceProcessor& processor)
    {
        processor.drainExternalMidiOutputQueue();
    }

    static int queuedEventCount (AudienceProcessor& processor)
    {
        return processor.externalMidiFifo.getNumReady();
    }

    static void acknowledgeReset (AudienceProcessor& processor)
    {
        processor.acknowledgeExternalMidiReset();
    }

    static void queueWatchdogCancels (AudienceProcessor& processor, int count)
    {
        for (int index = 0; index < count; ++index)
            processor.fingerRouter.pushCancel(
                index % OscFingerRouter::MAX_SOURCES,
                (index / OscFingerRouter::MAX_SOURCES)
                    % OscFingerRouter::MAX_FINGERS);
    }

    static void serviceWatchdog (AudienceProcessor& processor,
                                 std::uint32_t now,
                                 int expiredLiveTouches)
    {
        processor.serviceExternalWatchdogFallback(now, expiredLiveTouches);
    }

    static bool watchdogPending (const AudienceProcessor& processor)
    {
        return processor.externalWatchdogCancelPending;
    }

    static bool watchdogResetRequested (const AudienceProcessor& processor)
    {
        return processor.externalWatchdogResetRequested;
    }

    static std::uint64_t watchdogPublished (const AudienceProcessor& processor)
    {
        return processor.watchdogCancelsPublished.load(
            std::memory_order_acquire);
    }

    static std::uint64_t watchdogProcessed (const AudienceProcessor& processor)
    {
        return processor.watchdogCancelsProcessed.load(
            std::memory_order_acquire);
    }

    static std::uint64_t watchdogTarget (const AudienceProcessor& processor)
    {
        return processor.externalWatchdogCancelTargetCount;
    }

    static std::uint32_t watchdogDrainBlocks (
        const AudienceProcessor& processor)
    {
        return processor.externalWatchdogCancelDrainBlocks;
    }

    static void setWatchdogProcessed (AudienceProcessor& processor,
                                      std::uint64_t count)
    {
        processor.watchdogCancelsProcessed.store(count,
                                                  std::memory_order_release);
    }

    static void setWatchdogPublished (AudienceProcessor& processor,
                                      std::uint64_t count)
    {
        processor.watchdogCancelsPublished.store(count,
                                                  std::memory_order_release);
    }

    static void acknowledgeResetSnapshot (AudienceProcessor& processor,
                                          std::uint64_t target)
    {
        AudienceProcessor::WatchdogCancelRenderResult result;
        result.resetAcknowledgedTarget = target;
        processor.acknowledgeWatchdogCancelRenderResult(result);
    }

    static void setLatestBlockDuration (AudienceProcessor& processor,
                                        std::uint32_t milliseconds)
    {
        processor.latestAudioBlockDurationMs.store(milliseconds,
                                                   std::memory_order_release);
    }

    static void setSyntheticExternalEndpointReady (AudienceProcessor& processor,
                                                   bool ready)
    {
        processor.midiOutputOptionIndex.store(ready ? 1 : 0,
                                               std::memory_order_relaxed);
        processor.midiOutputReady.store(ready, std::memory_order_release);
    }

    static std::uint32_t resetRequestGeneration (
        const AudienceProcessor& processor)
    {
        return processor.externalMidiResetRequestGeneration.load(
            std::memory_order_acquire);
    }

    static void acknowledgeCurrentResetGeneration (AudienceProcessor& processor)
    {
        processor.externalMidiResetAcknowledgedGeneration.store(
            resetRequestGeneration(processor), std::memory_order_release);
    }

    static void setTimeFieldRehydrateLedger (AudienceProcessor& processor,
                                             std::uint64_t requested,
                                             std::uint64_t acknowledged)
    {
        processor.timeFieldRehydrateRequestGeneration.store(
            requested, std::memory_order_release);
        processor.timeFieldRehydrateAcknowledgedGeneration = acknowledged;
    }

    static std::uint64_t snapshotTimeFieldRehydrateRequest (
        const AudienceProcessor& processor)
    {
        return processor.snapshotTimeFieldRehydrateRequest();
    }

    static void requestTimeFieldRehydrate (AudienceProcessor& processor)
    {
        processor.requestTimeFieldRehydrate();
    }

    static void acknowledgeTimeFieldRehydrate (AudienceProcessor& processor,
                                               std::uint64_t target)
    {
        processor.acknowledgeTimeFieldRehydrate(target);
    }

    static bool timeFieldRehydratePending (const AudienceProcessor& processor)
    {
        return processor.isTimeFieldRehydratePending(
            processor.snapshotTimeFieldRehydrateRequest());
    }
};

namespace
{
    int failures = 0;

    void expect (bool condition, const char* name)
    {
        std::cout << (condition ? "PASS  " : "FAIL  ") << name << '\n';
        if (! condition)
            ++failures;
    }

    void setParameter (AudienceProcessor& processor, const char* id,
                       float denormalizedValue)
    {
        if (auto* parameter = processor.apvts.getParameter(id))
            parameter->setValueNotifyingHost(
                parameter->convertTo0to1(denormalizedValue));
    }

    bool isExactChannelSafetySweep (const juce::MidiBuffer& midi)
    {
        std::array<int, 16> allSoundOff {};
        std::array<int, 16> allNotesOff {};
        int eventCount = 0;

        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            const int channelIndex = message.getChannel() - 1;
            if (metadata.samplePosition != 0 || ! message.isController()
                || channelIndex < 0 || channelIndex >= 16)
                return false;

            if (message.getControllerNumber() == 120)
                ++allSoundOff[(std::size_t) channelIndex];
            else if (message.getControllerNumber() == 123)
                ++allNotesOff[(std::size_t) channelIndex];
            else
                return false;
            ++eventCount;
        }

        if (eventCount != 32)
            return false;
        for (int channel = 0; channel < 16; ++channel)
            if (allSoundOff[(std::size_t) channel] != 1
                || allNotesOff[(std::size_t) channel] != 1)
                return false;
        return true;
    }

    bool waitUntil (const std::function<bool()>& predicate, int timeoutMs)
    {
        const auto started = juce::Time::getMillisecondCounter();
        do
        {
            juce::Timer::callPendingTimersSynchronously();
            if (predicate())
                return true;
            juce::Thread::sleep (20);
        }
        while (static_cast<std::uint32_t>(
                   juce::Time::getMillisecondCounter() - started)
               < static_cast<std::uint32_t>(timeoutMs));
        return predicate();
    }

    class MidiSink final : public juce::MidiInputCallback
    {
    public:
        void handleIncomingMidiMessage (juce::MidiInput*,
                                        const juce::MidiMessage& message) override
        {
            if (message.isNoteOn())
                noteOns.fetch_add (1, std::memory_order_relaxed);
            if (message.isController()
                && (message.getControllerNumber() == 120
                    || message.getControllerNumber() == 123))
            {
                resetControllers.fetch_add(1, std::memory_order_relaxed);
            }
        }

        std::atomic<int> noteOns { 0 };
        std::atomic<int> resetControllers { 0 };
    };

    juce::MidiDeviceInfo findOutputByName (const juce::String& name)
    {
        for (const auto& device : juce::MidiOutput::getAvailableDevices())
            if (device.name == name)
                return device;
        return {};
    }

    juce::MemoryBlock makePhysicalRouteState (
        const juce::String& identifier, int optionIndex)
    {
        auto templateProcessor = std::make_unique<AudienceProcessor>();
        auto state = templateProcessor->apvts.copyState();
        state.setProperty ("udpPort", 6062, nullptr);
        state.setProperty ("midiOutputOption", optionIndex, nullptr);
        state.setProperty ("midiOutputRouteKind", 2, nullptr);
        state.setProperty ("midiOutputDeviceIdentifier", identifier, nullptr);
        state.setProperty ("cosmicMicrowaveSchema",
                           CosmicStateMigration::currentSchema, nullptr);

        juce::MemoryBlock data;
        if (auto xml = state.createXml())
            juce::AudioProcessor::copyXmlToBinary (*xml, data);
        return data;
    }

    bool savedRouteMatches (AudienceProcessor& processor,
                            const juce::String& identifier)
    {
        juce::MemoryBlock data;
        processor.getStateInformation (data);
        if (auto xml = juce::AudioProcessor::getXmlFromBinary (
                data.getData(), static_cast<int>(data.getSize())))
        {
            const auto state = juce::ValueTree::fromXml (*xml);
            return static_cast<int>(
                       state.getProperty ("midiOutputRouteKind", -1)) == 2
                && state.getProperty ("midiOutputDeviceIdentifier").toString()
                    == identifier;
        }
        return false;
    }

    void sendHostNote (AudienceProcessor& processor, int note)
    {
        juce::AudioBuffer<float> audio (2, 64);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (
                           1, note, static_cast<juce::uint8>(100)), 0);
        processor.processBlock (audio, midi);
    }

    void testBypassResetAndTailLifecycle()
    {
        auto processor = std::make_unique<AudienceProcessor>();
        processor->prepareToPlay(48000.0, 64);
        ProcessorMidiHotplugTestAccess::seedLongScheduledNote(*processor);
        expect(processor->getScheduledMidiNoteCount() == 1
                   && processor->getPhysicalMidiNoteCount() == 1,
               "long generated tail is active before bypass");

        const auto bypassResetGenerationBefore =
            ProcessorMidiHotplugTestAccess::resetRequestGeneration(*processor);
        juce::AudioBuffer<float> audio(2, 64);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(
                          1, 72, static_cast<juce::uint8>(100)), 0);
        processor->processBlockBypassed(audio, midi);
        expect(isExactChannelSafetySweep(midi)
                   && processor->getScheduledMidiNoteCount() == 0
                   && processor->getPhysicalMidiNoteCount() == 0,
               "first bypass block emits one exact Host sweep and clears every tail");
        expect(ProcessorMidiHotplugTestAccess::resetRequestGeneration(*processor)
                   == bypassResetGenerationBefore + 1u,
               "first bypass block publishes one External reset generation");

        midi.clear();
        processor->processBlockBypassed(audio, midi);
        expect(midi.isEmpty()
                   && ProcessorMidiHotplugTestAccess::resetRequestGeneration(
                          *processor) == bypassResetGenerationBefore + 1u,
               "continued bypass neither passes input nor repeats either reset");

        setParameter(*processor, "midiOutputPath", 0.0f); // Host Only
        ProcessorMidiHotplugTestAccess::makeCurrentRouteReady(*processor);
        midi.clear();
        processor->processBlock(audio, midi);
        expect(isExactChannelSafetySweep(midi),
               "first resumed block emits the pending Host lifecycle reset");
        midi.clear();
        processor->processBlock(audio, midi);
        expect(midi.isEmpty(),
               "pending Host lifecycle reset is consumed exactly once");

        ProcessorMidiHotplugTestAccess::seedLongScheduledNote(*processor);
        const auto lifecycleResetGenerationBefore =
            ProcessorMidiHotplugTestAccess::resetRequestGeneration(*processor);
        processor->reset();
        expect(processor->getScheduledMidiNoteCount() == 0
                   && processor->getPhysicalMidiNoteCount() == 0,
               "AudioProcessor reset clears scheduled and physical ownership");
        expect(ProcessorMidiHotplugTestAccess::resetRequestGeneration(*processor)
                   == lifecycleResetGenerationBefore + 1u,
               "AudioProcessor reset publishes External cleanup without direct I/O");

        midi.clear();
        processor->processBlock(audio, midi);
        expect(isExactChannelSafetySweep(midi),
               "first block after AudioProcessor reset emits one Host sweep");
        midi.clear();
        processor->processBlock(audio, midi);
        expect(midi.isEmpty(),
               "AudioProcessor reset Host sweep is not repeated");
        expect(processor->getTailLengthSeconds() == 120.0,
               "processor reports the bounded 1 BPM 2n maximum tail");
    }

    void testResetGenerationGate()
    {
        auto processor = std::make_unique<AudienceProcessor>();
        ProcessorMidiHotplugTestAccess::enqueueImmediateNoteOn (*processor);

        // Model the boolean ABA window directly: sender 1 cleared the shared
        // boolean while reset request 2 was already newer than its ack. The
        // generation mismatch must still prevent dequeueing the old NoteOn.
        ProcessorMidiHotplugTestAccess::simulateBooleanAbaWithNewerReset (
            *processor);
        ProcessorMidiHotplugTestAccess::drain (*processor);
        expect (ProcessorMidiHotplugTestAccess::queuedEventCount (*processor) == 1,
                "newer reset generation keeps an old queued NoteOn quarantined after boolean ABA");

        ProcessorMidiHotplugTestAccess::acknowledgeReset (*processor);
        ProcessorMidiHotplugTestAccess::drain (*processor);
        expect (ProcessorMidiHotplugTestAccess::queuedEventCount (*processor) == 0,
                "acknowledging the newest reset generation reopens the sender drain");

        auto lateCommit = std::make_unique<AudienceProcessor>();
        ProcessorMidiHotplugTestAccess::commitOldReservationAfterResetAck(
            *lateCommit);
        expect (ProcessorMidiHotplugTestAccess::queuedEventCount(*lateCommit) == 1,
                "old producer reservation can be committed after the reset acknowledgement fixture");
        ProcessorMidiHotplugTestAccess::drain(*lateCommit);
        expect (ProcessorMidiHotplugTestAccess::queuedEventCount(*lateCommit) == 0,
                "generation-stamped late commit is discarded instead of resurrecting a Note On");
    }

    void testWatchdogFallbackLedger()
    {
        constexpr std::uint32_t base = 1000u;

        {
            auto processor = std::make_unique<AudienceProcessor>();
            ProcessorMidiHotplugTestAccess::queueWatchdogCancels(
                *processor, 256);
            ProcessorMidiHotplugTestAccess::serviceWatchdog(
                *processor, base, 256);
            expect (ProcessorMidiHotplugTestAccess::watchdogPending(*processor)
                        && ProcessorMidiHotplugTestAccess::watchdogPublished(
                               *processor) == 256
                        && ProcessorMidiHotplugTestAccess::watchdogTarget(
                               *processor) == 256
                        && ProcessorMidiHotplugTestAccess::watchdogDrainBlocks(
                               *processor) == 5,
                    "256 watchdog Cancels remain one cumulative batch across the 64-event block budget");

            ProcessorMidiHotplugTestAccess::setWatchdogProcessed(
                *processor, 64);
            ProcessorMidiHotplugTestAccess::serviceWatchdog(
                *processor, base + 100u, 0);
            expect (ProcessorMidiHotplugTestAccess::watchdogPending(*processor),
                    "one processed 64-event slice cannot acknowledge a 256-Cancel batch");

            ProcessorMidiHotplugTestAccess::setWatchdogProcessed(
                *processor, 256);
            ProcessorMidiHotplugTestAccess::serviceWatchdog(
                *processor, base + 101u, 0);
            expect (! ProcessorMidiHotplugTestAccess::watchdogPending(*processor),
                    "the watchdog latch clears only after the complete cumulative batch is processed");
        }

        {
            auto processor = std::make_unique<AudienceProcessor>();
            ProcessorMidiHotplugTestAccess::setLatestBlockDuration(
                *processor, 371u);
            ProcessorMidiHotplugTestAccess::queueWatchdogCancels(*processor, 1);
            ProcessorMidiHotplugTestAccess::serviceWatchdog(
                *processor, base, 1);
            ProcessorMidiHotplugTestAccess::serviceWatchdog(
                *processor, base + 300u, 0);
            expect (ProcessorMidiHotplugTestAccess::watchdogPending(*processor),
                    "a healthy 16384-sample style block is not mistaken for a 250 ms callback stall");
        }

        {
            auto processor = std::make_unique<AudienceProcessor>();
            ProcessorMidiHotplugTestAccess::queueWatchdogCancels(*processor, 1);
            ProcessorMidiHotplugTestAccess::serviceWatchdog(
                *processor, base, 1);
            ProcessorMidiHotplugTestAccess::setSyntheticExternalEndpointReady(
                *processor, true);
            const auto requestBefore =
                ProcessorMidiHotplugTestAccess::resetRequestGeneration(
                    *processor);
            ProcessorMidiHotplugTestAccess::serviceWatchdog(
                *processor, base + 251u, 0);
            const auto requested =
                ProcessorMidiHotplugTestAccess::resetRequestGeneration(
                    *processor);
            expect (requested == requestBefore + 1u
                        && ProcessorMidiHotplugTestAccess::watchdogPending(
                               *processor)
                        && ProcessorMidiHotplugTestAccess::watchdogResetRequested(
                               *processor),
                    "a stalled external route requests exactly one independent reset and waits for acknowledgement");

            ProcessorMidiHotplugTestAccess::serviceWatchdog(
                *processor, base + 300u, 0);
            expect (ProcessorMidiHotplugTestAccess::resetRequestGeneration(
                        *processor) == requested,
                    "an unacknowledged watchdog sweep is not requested repeatedly");

            ProcessorMidiHotplugTestAccess::acknowledgeCurrentResetGeneration(
                *processor);
            ProcessorMidiHotplugTestAccess::serviceWatchdog(
                *processor, base + 301u, 0);
            expect (! ProcessorMidiHotplugTestAccess::watchdogPending(*processor)
                        && ProcessorMidiHotplugTestAccess::watchdogProcessed(
                               *processor) == 0,
                    "reset acknowledgement retires only the external fallback latch without forging processed Cancels");
            ProcessorMidiHotplugTestAccess::setSyntheticExternalEndpointReady(
                *processor, false);
        }

        {
            auto processor = std::make_unique<AudienceProcessor>();
            ProcessorMidiHotplugTestAccess::setWatchdogPublished(*processor, 10);

            // The audio reset sampled publication 10 immediately before its
            // discard. A timer publishes batch 11 before the block reaches its
            // final destination acknowledgement. Only the captured boundary
            // may be retired; batch 11 must remain pending for a later block.
            ProcessorMidiHotplugTestAccess::setWatchdogPublished(*processor, 11);
            ProcessorMidiHotplugTestAccess::acknowledgeResetSnapshot(
                *processor, 10);
            expect (ProcessorMidiHotplugTestAccess::watchdogProcessed(*processor)
                        == 10
                    && ProcessorMidiHotplugTestAccess::watchdogPublished(*processor)
                        == 11,
                    "reset acknowledges its exact pre-discard Cancel snapshot, not a later timer publication");
        }
    }

    void testTimeFieldRehydrateRequestLedger()
    {
        auto processor = std::make_unique<AudienceProcessor>();
        ProcessorMidiHotplugTestAccess::setTimeFieldRehydrateLedger(
            *processor, 10, 9);

        // Audio snapshots request 10. A control/state callback publishes 11
        // while that rebuild is in flight. Acknowledging the captured target
        // must leave 11 pending instead of clearing the newer wakeup.
        const auto captured =
            ProcessorMidiHotplugTestAccess::snapshotTimeFieldRehydrateRequest(
                *processor);
        ProcessorMidiHotplugTestAccess::requestTimeFieldRehydrate(*processor);
        ProcessorMidiHotplugTestAccess::acknowledgeTimeFieldRehydrate(
            *processor, captured);
        expect (captured == 10
                    && ProcessorMidiHotplugTestAccess::timeFieldRehydratePending(
                           *processor),
                "Time Field request/ack ledger preserves a concurrent newer rehydrate request");

        const auto newest =
            ProcessorMidiHotplugTestAccess::snapshotTimeFieldRehydrateRequest(
                *processor);
        ProcessorMidiHotplugTestAccess::acknowledgeTimeFieldRehydrate(
            *processor, newest);
        expect (! ProcessorMidiHotplugTestAccess::timeFieldRehydratePending(
                    *processor),
                "Time Field request/ack ledger clears only the exact acknowledged generation");
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    testBypassResetAndTailLifecycle();
    testResetGenerationGate();
    testWatchdogFallbackLedger();
    testTimeFieldRehydrateRequestLedger();

#if JUCE_MAC
    MidiSink sink;
    const auto endpointName = "Cosmic Hotplug Test "
        + juce::String::toHexString (juce::Time::currentTimeMillis());

    auto endpoint = juce::MidiInput::createNewDevice (endpointName, &sink);
    expect (endpoint != nullptr,
            "CoreMIDI virtual destination fixture is created");
    if (endpoint == nullptr)
        return failures == 0 ? 0 : 1;
    endpoint->start();

    juce::MidiDeviceInfo firstDevice;
    const bool appeared = waitUntil ([&]
    {
        firstDevice = findOutputByName (endpointName);
        return firstDevice.identifier.isNotEmpty();
    }, 2000);
    expect (appeared,
            "virtual destination appears in the physical MIDI output list");
    if (! appeared)
        return 1;

    const auto stableIdentifier = firstDevice.identifier;
    int firstOption = 2;
    const auto initialDevices = juce::MidiOutput::getAvailableDevices();
    for (int index = 0; index < initialDevices.size(); ++index)
        if (initialDevices[index].identifier == stableIdentifier)
            firstOption = index + 2;

    endpoint.reset();
    expect (waitUntil ([&]
    {
        for (const auto& device : juce::MidiOutput::getAvailableDevices())
            if (device.identifier == stableIdentifier)
                return false;
        return true;
    }, 2000), "virtual destination can be disconnected deterministically");

    const auto routeState = makePhysicalRouteState (stableIdentifier,
                                                     firstOption);
    auto processor = std::make_unique<AudienceProcessor>();
    processor->prepareToPlay (48000.0, 64);
    processor->setStateInformation (routeState.getData(),
                                    static_cast<int>(routeState.getSize()));
    juce::Timer::callPendingTimersSynchronously();
    expect (! processor->isMidiOutputReady()
                && processor->getResolvedMidiOutputOptionIndex() == -1
                && savedRouteMatches (*processor, stableIdentifier),
            "missing physical route is fail-closed without losing its stable identifier");

    endpoint = juce::MidiInput::createNewDevice (endpointName, &sink);
    expect (endpoint != nullptr,
            "same CoreMIDI destination can be recreated for reconnect");
    if (endpoint == nullptr)
        return 1;
    endpoint->start();

    juce::MidiDeviceInfo reconnectedDevice;
    const bool sameIdentityReturned = waitUntil ([&]
    {
        reconnectedDevice = findOutputByName (endpointName);
        return reconnectedDevice.identifier == stableIdentifier;
    }, 2000);
    expect (sameIdentityReturned,
            "recreated destination retains the same CoreMIDI identifier");

    expect (waitUntil ([&]
    {
        return processor->isMidiOutputReady()
            && processor->getResolvedMidiOutputOptionIndex() > 1;
    }, 2500),
            "processor reopens the reappeared physical route on its bounded timer scan");

    const int notesBeforeFirstSend = sink.noteOns.load (std::memory_order_relaxed);
    sendHostNote (*processor, 60);
    expect (waitUntil ([&]
    {
        return sink.noteOns.load (std::memory_order_relaxed)
            > notesBeforeFirstSend;
    }, 1000), "reconnected route delivers MIDI to the exact saved destination");

    // A real endpoint verifies that bypass publishes reset work to the existing
    // high-resolution sender instead of performing CoreMIDI I/O in the callback.
    juce::Thread::sleep(20);
    const int resetsBeforeBypass =
        sink.resetControllers.load(std::memory_order_relaxed);
    const auto resetGenerationBeforeBypass =
        ProcessorMidiHotplugTestAccess::resetRequestGeneration(*processor);
    {
        juce::AudioBuffer<float> bypassAudio(2, 64);
        juce::MidiBuffer bypassMidi;
        processor->processBlockBypassed(bypassAudio, bypassMidi);
        expect(isExactChannelSafetySweep(bypassMidi),
               "bypass returns the exact Host reset while External I/O stays deferred");
    }
    expect(waitUntil ([&]
    {
        return sink.resetControllers.load(std::memory_order_relaxed)
            >= resetsBeforeBypass + 32;
    }, 1000),
           "bypass reset is delivered by the existing External sender worker");
    juce::Thread::sleep(20);
    expect(sink.resetControllers.load(std::memory_order_relaxed)
               == resetsBeforeBypass + 32
               && ProcessorMidiHotplugTestAccess::resetRequestGeneration(*processor)
                    == resetGenerationBeforeBypass + 1u,
           "External bypass reset is one exact 16-channel sweep and one generation");

    {
        juce::AudioBuffer<float> bypassAudio(2, 64);
        juce::MidiBuffer bypassMidi;
        processor->processBlockBypassed(bypassAudio, bypassMidi);
        expect(bypassMidi.isEmpty(),
               "continued bypass does not repeat the Host reset");
    }
    juce::Thread::sleep(20);
    expect(sink.resetControllers.load(std::memory_order_relaxed)
               == resetsBeforeBypass + 32
               && ProcessorMidiHotplugTestAccess::resetRequestGeneration(*processor)
                    == resetGenerationBeforeBypass + 1u,
           "continued bypass does not repeat the External reset");

    endpoint.reset();
    expect (waitUntil ([&]
    {
        return ! processor->isMidiOutputReady();
    }, 2500),
            "processor closes a physical destination after hot-unplug");
    expect (processor->getResolvedMidiOutputOptionIndex() == -1
                && savedRouteMatches (*processor, stableIdentifier),
            "hot-unplug remains fail-closed and preserves logical project state");

    endpoint = juce::MidiInput::createNewDevice (endpointName, &sink);
    if (endpoint != nullptr)
        endpoint->start();
    expect (endpoint != nullptr && waitUntil ([&]
    {
        return processor->isMidiOutputReady();
    }, 2500), "a second reconnect also recovers without user route reselection");

    processor->releaseResources();
    processor.reset();
    endpoint.reset();
#else
    expect (true, "CoreMIDI hotplug integration test is macOS-only");
#endif

    if (failures != 0)
        std::cerr << failures << " processor MIDI hotplug test(s) failed\n";
    return failures == 0 ? 0 : 1;
}
