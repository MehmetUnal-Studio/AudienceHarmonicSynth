#include "../Source/PluginProcessor.h"
#include "../Source/PluginStateMigration.h"

#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>

namespace
{
    int failures = 0;

    void expect (bool condition, const char* name)
    {
        std::cout << (condition ? "PASS  " : "FAIL  ") << name << '\n';
        if (! condition)
            ++failures;
    }

    void processOneBlock (AudienceProcessor& processor,
                          juce::AudioBuffer<float>& audio,
                          juce::MidiBuffer& midi)
    {
        audio.clear();
        midi.clear();
        processor.processBlock (audio, midi);
    }

    void processPreparedMidiBlock (AudienceProcessor& processor,
                                   juce::AudioBuffer<float>& audio,
                                   juce::MidiBuffer& midi)
    {
        audio.clear();
        processor.processBlock (audio, midi);
    }

    bool isSafetyResetController (const juce::MidiMessage& message)
    {
        if (! message.isController())
            return false;

        const int controller = message.getControllerNumber();
        return controller == 120 || controller == 123;
    }

    bool isForbiddenPerformanceMessage (const juce::MidiMessage& message)
    {
        return (message.isController() && ! isSafetyResetController (message))
            || message.isChannelPressure()
            || message.isPitchWheel()
            || message.isAftertouch()
            || message.isProgramChange()
            || message.isSysEx();
    }

    void setParameter (AudienceProcessor& processor, const char* id,
                       float denormalizedValue)
    {
        if (auto* parameter = processor.apvts.getParameter (id))
            parameter->setValueNotifyingHost (
                parameter->convertTo0to1 (denormalizedValue));
    }

    int parameterInt (AudienceProcessor& processor, const char* id)
    {
        if (const auto* value = processor.apvts.getRawParameterValue (id))
            return juce::roundToInt (value->load (std::memory_order_relaxed));
        return std::numeric_limits<int>::lowest();
    }

    void pumpMessageLoop (int milliseconds)
    {
        juce::Thread::sleep (milliseconds);
        juce::Timer::callPendingTimersSynchronously();
    }

    class MutablePlayHead final : public juce::AudioPlayHead
    {
    public:
        juce::Optional<juce::AudioPlayHead::PositionInfo>
        getPosition() const override
        {
            juce::AudioPlayHead::PositionInfo position;
            position.setIsPlaying (playing);
            position.setBpm (bpm);
            position.setPpqPosition (ppq);
            return position;
        }

        void setPlaying (bool value) noexcept { playing = value; }
        void advance (int samples, double sampleRate) noexcept
        {
            if (playing && samples > 0 && sampleRate > 0.0)
                ppq += (double) samples / sampleRate * bpm / 60.0;
        }

    private:
        bool playing = true;
        double bpm = 120.0;
        double ppq = 0.0;
    };

    bool settleProcessorControlBoundary (
        AudienceProcessor& processor,
        juce::AudioBuffer<float>& audio,
        juce::MidiBuffer& midi,
        MutablePlayHead* playHead = nullptr)
    {
        constexpr int maxPasses = 24;
        constexpr int requiredStablePasses = 4;
        int stablePasses = 0;
        auto previousRouteRevision = processor.getMidiOutputRouteRevision();

        for (int pass = 0; pass < maxPasses; ++pass)
        {
            // Parameter writes may publish a message-thread route/state
            // transaction through the 60 Hz Timer or the 2 ms HiRes timer.
            // Give both bounded control paths a chance to run, consume the
            // corresponding audio-thread reset, then require four completely
            // idle callbacks on one stable route generation.
            pumpMessageLoop (20);
            processOneBlock (processor, audio, midi);
            if (playHead != nullptr)
                playHead->advance (audio.getNumSamples(), 48000.0);

            bool resetSeen = false;
            for (const auto metadata : midi)
                resetSeen = resetSeen
                         || isSafetyResetController (metadata.getMessage());

            const auto routeRevision = processor.getMidiOutputRouteRevision();
            const bool routeStable = routeRevision == previousRouteRevision;
            const bool routeAssigned = processor.getFreshRouteAssignmentState()
                != AudienceProcessor::FreshRouteAssignmentState::pending;
            const bool audioIdle = processor.getScheduledMidiNoteCount() == 0
                                && processor.getPhysicalMidiNoteCount() == 0
                                && processor.getTimeFieldActive() == 0
                                && processor.getTimeFieldPending() == 0;

            stablePasses = ! resetSeen && routeStable && routeAssigned && audioIdle
                         ? stablePasses + 1 : 0;
            previousRouteRevision = routeRevision;
            if (stablePasses >= requiredStablePasses)
                return true;
        }

        return false;
    }

    bool isExactChannelSafetySweep (const juce::MidiBuffer& midi)
    {
        std::array<int, 16> allSoundOff {};
        std::array<int, 16> allNotesOff {};
        int eventCount = 0;

        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            if (metadata.samplePosition != 0 || ! message.isController())
                return false;

            const int channelIndex = message.getChannel() - 1;
            if (channelIndex < 0 || channelIndex >= 16)
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

    struct ExpectedFactoryParameter
    {
        const char* id;
        int value;
    };

    constexpr ExpectedFactoryParameter factoryParameterBaseline[] {
        { "midiOutputType", 1 },
        { "midiOutputPath", 1 },
        { "exclusiveUdpPort", 1 },
        { "safetyGovernorEnabled", 0 },
        { "normalMidiRoutingMode", 1 },
        { "normalMidiChannel", 0 },
        { "timeMode", 0 },
        { "clockSource", 0 },
        { "internalBpm", 120 },
        { "gridDivision", 3 },
        { "maxAttacksPerStep", 16 },
        { "maxActiveVoices", 16 },
        { "gatePercent", 100 },
        { "temporalSpread", 4 },
        { "crowdGovernorEnabled", 0 },
        { "noteDuration", 3 },
        { "sourceCapacity", 0 },
        { "ensembleSameNoteMode", 0 },
        { "conductorGroup", 0 },
        { "conductorAttackBudget", 16 },
        { "conductorVoiceBudget", 16 },
        { "crowdMacrosEnabled", 0 },
        { "crowdMacroChannel", 0 },
        { "crowdMacroDensityCc", 20 },
        { "crowdMacroCentroidXCc", 21 },
        { "crowdMacroCentroidYCc", 22 },
        { "crowdMacroMotionCc", 23 },
        { "crowdMacroRate", 1 },
        { "pitchSystem", 1 },
        { "scaleRoot", 0 },
        { "scaleRootOctave", 2 },
        { "scaleMode", 0 },
        { "scaleOctaves", 4 },
        { "spectralElement", 28 },
        { "atomicScaleMode", 0 }
    };

    bool commonFactoryParametersMatch (AudienceProcessor& processor)
    {
        for (const auto& expected : factoryParameterBaseline)
            if (parameterInt (processor, expected.id) != expected.value)
                return false;
        return true;
    }

    bool factoryDefaultEndStateMatches (AudienceProcessor& processor)
    {
        return processor.getUdpPort() == 6062
            && commonFactoryParametersMatch (processor)
            && parameterInt (processor, "expectedZone") == 1
            && parameterInt (processor, "conductorRole") == 1
            && processor.getResolvedMidiOutputOptionIndex() == 1
            && processor.getMatchingFactoryPresetIndex() == 0
            && AudienceProcessor::getNumFactoryPresets() == 8;
    }

    bool waitForFactoryDefaultEndState (AudienceProcessor& processor)
    {
        // CoreMIDI may release a just-destroyed virtual source after the OSC
        // port is already free. Production retries that exact endpoint at
        // 1 Hz, so exercise the real bounded recovery path instead of assuming
        // every route has settled after one fixed 80 ms sleep.
        constexpr int pollIntervalMs = 50;
        constexpr int timeoutMs = 1500;
        for (int elapsedMs = 0; elapsedMs < timeoutMs;
             elapsedMs += pollIntervalMs)
        {
            pumpMessageLoop (pollIntervalMs);
            if (factoryDefaultEndStateMatches (processor))
                return true;
        }
        return factoryDefaultEndStateMatches (processor);
    }

    void printFactoryDefaultDiagnostics (AudienceProcessor& processor)
    {
        std::cerr << "factory defaults: port=" << processor.getUdpPort()
                  << " zone=" << parameterInt (processor, "expectedZone")
                  << " role=" << parameterInt (processor, "conductorRole")
                  << " resolvedMidi="
                  << processor.getResolvedMidiOutputOptionIndex()
                  << " preset=" << processor.getMatchingFactoryPresetIndex()
                  << " presetCount=" << AudienceProcessor::getNumFactoryPresets()
                  << " freshRouteState="
                  << static_cast<int> (processor.getFreshRouteAssignmentState())
                  << '\n';

        for (const auto& expected : factoryParameterBaseline)
        {
            const int actual = parameterInt (processor, expected.id);
            if (actual != expected.value)
                std::cerr << "factory parameter mismatch: " << expected.id
                          << " expected=" << expected.value
                          << " actual=" << actual << '\n';
        }
    }

    void setStateParameter (juce::ValueTree& state, const char* id, float value)
    {
        if (auto node = CosmicStateMigration::findParameterNode (state, id);
            node.isValid())
            node.setProperty ("value", value, nullptr);
    }

    juce::MemoryBlock serializeState (juce::ValueTree state)
    {
        juce::MemoryBlock data;
        state.setProperty ("cosmicMicrowaveSchema",
                           CosmicStateMigration::currentSchema, nullptr);
        if (auto xml = state.createXml())
            juce::AudioProcessor::copyXmlToBinary (*xml, data);
        return data;
    }

    class ReentrantRestoreListener final
        : public juce::AudioProcessorParameter::Listener
    {
    public:
        ReentrantRestoreListener (AudienceProcessor& targetProcessor,
                                  const juce::MemoryBlock& nestedState)
            : processor (targetProcessor), state (nestedState)
        {
        }

        void parameterValueChanged (int, float) override
        {
            if (! fired.exchange (true, std::memory_order_acq_rel))
                processor.setStateInformation (state.getData(),
                                               (int) state.getSize());
        }

        void parameterGestureChanged (int, bool) override {}

        bool didFire() const noexcept
        {
            return fired.load (std::memory_order_acquire);
        }

    private:
        AudienceProcessor& processor;
        const juce::MemoryBlock& state;
        std::atomic<bool> fired { false };
    };

    class ReentrantRouteEditListener final
        : public juce::AudioProcessorParameter::Listener
    {
    public:
        explicit ReentrantRouteEditListener (AudienceProcessor& targetProcessor)
            : processor (targetProcessor)
        {
        }

        void parameterValueChanged (int, float) override
        {
            if (! fired.exchange (true, std::memory_order_acq_rel))
            {
                processor.setMidiOutputOptionIndex (0);
                processor.setUdpPort (7777);
            }
        }

        void parameterGestureChanged (int, bool) override {}

        bool didFire() const noexcept
        {
            return fired.load (std::memory_order_acquire);
        }

    private:
        AudienceProcessor& processor;
        std::atomic<bool> fired { false };
    };

    void testProcessorLifecycleSafetyBoundaries (
        juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
    {
        {
            auto processor = std::make_unique<AudienceProcessor>();
            MutablePlayHead playHead;
            processor->setPlayHead (&playHead);
            processor->prepareToPlay (48000.0, audio.getNumSamples());
            pumpMessageLoop (80);
            setParameter (*processor, "midiOutputPath", 0.0f); // Host Only
            setParameter (*processor, "noteDuration", 0.0f);  // 2n
            processOneBlock (*processor, audio, midi);         // settle route

            processor->audienceModel.setFingerX (0, 1, 0, 0.42f);
            processor->audienceModel.setFingerY (0, 1, 0, 0.78f);
            processor->audienceModel.setFingerOn (0, 1, 0, true);
            processOneBlock (*processor, audio, midi);
            bool noteStarted = false;
            for (const auto metadata : midi)
                noteStarted = noteStarted || metadata.getMessage().isNoteOn();
            processOneBlock (*processor, audio, midi);
            for (const auto metadata : midi)
                noteStarted = noteStarted || metadata.getMessage().isNoteOn();
            expect (noteStarted
                        && processor->getScheduledMidiNoteCount() == 1
                        && processor->getPhysicalMidiNoteCount() == 1,
                    "active generated note reaches the production scheduler before transport Stop");

            playHead.setPlaying (false);
            processOneBlock (*processor, audio, midi);
            expect (isExactChannelSafetySweep (midi)
                        && processor->getScheduledMidiNoteCount() == 0
                        && processor->getPhysicalMidiNoteCount() == 0,
                    "playing-to-stopped transition emits one bounded 16-channel sweep and clears notes");

            processOneBlock (*processor, audio, midi);
            expect (midi.isEmpty()
                        && processor->getScheduledMidiNoteCount() == 0
                        && processor->getPhysicalMidiNoteCount() == 0,
                    "stopped transport neither repeats the sweep nor rehydrates a held source");

            processor->setPlayHead (nullptr);
            processor->releaseResources();
        }

        {
            auto processor = std::make_unique<AudienceProcessor>();
            MutablePlayHead playHead;
            processor->setPlayHead (&playHead);
            processor->prepareToPlay (48000.0, audio.getNumSamples());
            pumpMessageLoop (80);
            processOneBlock (*processor, audio, midi); // settle factory route

            processor->audienceModel.setFingerX (0, 1, 0, 0.56f);
            processor->audienceModel.setFingerY (0, 1, 0, 0.81f);
            processor->audienceModel.setFingerOn (0, 1, 0, true);
            processOneBlock (*processor, audio, midi);
            processOneBlock (*processor, audio, midi);
            expect (processor->getScheduledMidiNoteCount() == 1
                        && processor->getPhysicalMidiNoteCount() == 1
                        && processor->audienceModel.getActiveFingerCount() == 1,
                    "active generated note reaches the production scheduler before preset recall");

            processor->applyFactoryPreset (0);
            processOneBlock (*processor, audio, midi);
            expect (isExactChannelSafetySweep (midi)
                        && processor->getScheduledMidiNoteCount() == 0
                        && processor->getPhysicalMidiNoteCount() == 0
                        && processor->audienceModel.getActiveFingerCount() == 0
                        && processor->getMatchingFactoryPresetIndex() == 0,
                    "factory preset recall is a hard boundary with one complete stuck-note cleanup");

            processOneBlock (*processor, audio, midi);
            expect (midi.isEmpty()
                        && processor->getScheduledMidiNoteCount() == 0
                        && processor->getPhysicalMidiNoteCount() == 0,
                    "preset cleanup sweep is acknowledged exactly once");

            processor->setPlayHead (nullptr);
            processor->releaseResources();
        }
    }

    void testGridTailAdmissionReconciliation (
        juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
    {
        auto processor = std::make_unique<AudienceProcessor>();
        MutablePlayHead playHead;
        processor->setPlayHead (&playHead);
        processor->prepareToPlay (48000.0, audio.getNumSamples());
        pumpMessageLoop (80);

        setParameter (*processor, "midiOutputPath", 0.0f);       // Host Only
        setParameter (*processor, "timeMode", 1.0f);            // Grid
        setParameter (*processor, "clockSource", 0.0f);         // Host
        setParameter (*processor, "gridDivision", 4.0f);        // 1/32
        setParameter (*processor, "maxAttacksPerStep", 16.0f);
        setParameter (*processor, "maxActiveVoices", 16.0f);
        setParameter (*processor, "noteDuration", 4.0f);        // 1/32
        setParameter (*processor, "crowdGovernorEnabled", 0.0f);
        setParameter (*processor, "safetyGovernorEnabled", 0.0f);

        // Do not begin counting attacks until every asynchronous factory-route
        // and Time Field configuration boundary has been acknowledged.
        expect (settleProcessorControlBoundary (
                    *processor, audio, midi, &playHead),
                "Grid test settles its route and Time Field control boundary");

        constexpr int sourceCount = 17;
        for (int source = 1; source <= sourceCount; ++source)
        {
            const float x = ((float) source - 0.5f) / (float) sourceCount;
            processor->audienceModel.setFingerX (0, source, 0, x);
            processor->audienceModel.setFingerY (0, source, 0, 0.75f);
            processor->audienceModel.setFingerOn (0, source, 0, true);
        }

        int noteOns = 0;
        int noteOffs = 0;
        for (int block = 0; block < 48; ++block)
        {
            processOneBlock (*processor, audio, midi);
            for (const auto metadata : midi)
            {
                const auto message = metadata.getMessage();
                noteOns += message.isNoteOn() ? 1 : 0;
                noteOffs += message.isNoteOff() ? 1 : 0;
            }
            playHead.advance (audio.getNumSamples(), 48000.0);
        }

        expect(noteOns == sourceCount && noteOffs == sourceCount
                    && processor->getScheduledMidiNoteCount() == 0
                    && processor->getPhysicalMidiNoteCount() == 0
                    && processor->getTimeFieldActive() == 0
                    && processor->getTimeFieldPending() == 0,
                "17 held Grid sources all sound and drain after the first 16 fixed tails end");

        for (int source = 1; source <= sourceCount; ++source)
            processor->audienceModel.setFingerOn (0, source, 0, false);
        processOneBlock (*processor, audio, midi);
        processor->setPlayHead (nullptr);
        processor->releaseResources();
    }

    void testEnsembleSameNoteArticulation (
        juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
    {
        struct Counts
        {
            int noteOns = 0;
            int noteOffs = 0;
        };

        auto renderHeldEnsemble = [&] (int sameNoteMode)
        {
            auto processor = std::make_unique<AudienceProcessor>();
            MutablePlayHead playHead;
            processor->setPlayHead (&playHead);
            processor->prepareToPlay (48000.0, audio.getNumSamples());
            pumpMessageLoop (80);
            setParameter (*processor, "midiOutputPath", 0.0f); // Host Only
            setParameter (*processor, "timeMode", 2.0f);      // Ensemble
            setParameter (*processor, "clockSource", 0.0f);   // Host
            setParameter (*processor, "gridDivision", 3.0f);  // 1/32
            setParameter (*processor, "noteDuration", 1.0f);  // 4n
            setParameter (*processor, "maxAttacksPerStep", 16.0f);
            setParameter (*processor, "maxActiveVoices", 16.0f);
            setParameter (*processor, "gatePercent", 100.0f);
            setParameter (*processor, "temporalSpread", 0.0f); // 1 step
            setParameter (*processor, "crowdGovernorEnabled", 0.0f);
            setParameter (*processor, "safetyGovernorEnabled", 0.0f);
            setParameter (*processor, "ensembleSameNoteMode",
                          (float) sameNoteMode);

            expect (settleProcessorControlBoundary (
                        *processor, audio, midi, &playHead),
                    sameNoteMode == 0
                        ? "Tie Ensemble test settles its control boundary"
                        : "Retrigger Ensemble test settles its control boundary");

            processor->audienceModel.setFingerX (0, 1, 0, 0.42f);
            processor->audienceModel.setFingerY (0, 1, 0, 0.75f);
            processor->audienceModel.setFingerOn (0, 1, 0, true);

            Counts counts;
            for (int block = 0; block < 80; ++block)
            {
                processOneBlock (*processor, audio, midi);
                for (const auto metadata : midi)
                {
                    const auto message = metadata.getMessage();
                    counts.noteOns += message.isNoteOn() ? 1 : 0;
                    counts.noteOffs += message.isNoteOff() ? 1 : 0;
                }
                playHead.advance (audio.getNumSamples(), 48000.0);
            }

            processor->audienceModel.setFingerOn (0, 1, 0, false);
            processor->panic();
            processor->setPlayHead (nullptr);
            processor->releaseResources();
            return counts;
        };

        const auto tied = renderHeldEnsemble (0);
        const auto retriggered = renderHeldEnsemble (1);
        expect (tied.noteOns == 1
                    && retriggered.noteOns >= 4
                    && retriggered.noteOffs >= retriggered.noteOns - 1,
                "Ensemble Tie sustains one attack while Retrigger articulates every same-pitch pulse");

        // A saved Retrigger preference must not leak into Flow. Two ordered
        // On lifecycles for one still-sounding pitch should retain the legacy
        // tied ownership instead of producing a hard physical retrigger.
        auto flow = std::make_unique<AudienceProcessor>();
        MutablePlayHead playHead;
        flow->setPlayHead (&playHead);
        flow->prepareToPlay (48000.0, audio.getNumSamples());
        pumpMessageLoop (80);
        setParameter (*flow, "midiOutputPath", 0.0f);
        setParameter (*flow, "timeMode", 0.0f);
        setParameter (*flow, "noteDuration", 0.0f); // 2n, still sounding
        setParameter (*flow, "ensembleSameNoteMode", 1.0f);
        expect (settleProcessorControlBoundary (*flow, audio, midi, &playHead),
                "Flow same-note scope test settles its control boundary");

        flow->audienceModel.setFingerX (0, 1, 0, 0.42f);
        flow->audienceModel.setFingerY (0, 1, 0, 0.75f);
        flow->audienceModel.setFingerOn (0, 1, 0, true);
        int flowNoteOns = 0;
        for (int block = 0; block < 4; ++block)
        {
            processOneBlock (*flow, audio, midi);
            for (const auto metadata : midi)
                flowNoteOns += metadata.getMessage().isNoteOn() ? 1 : 0;
            playHead.advance (audio.getNumSamples(), 48000.0);
        }
        flow->audienceModel.setFingerOn (0, 1, 0, false);
        flow->audienceModel.setFingerOn (0, 1, 0, true);
        for (int block = 0; block < 4; ++block)
        {
            processOneBlock (*flow, audio, midi);
            for (const auto metadata : midi)
                flowNoteOns += metadata.getMessage().isNoteOn() ? 1 : 0;
            playHead.advance (audio.getNumSamples(), 48000.0);
        }
        expect (flowNoteOns == 1,
                "Flow forces Tie even when the saved Ensemble preference is Retrigger");
        flow->panic();
        flow->setPlayHead (nullptr);
        flow->releaseResources();
    }

    void testEffectiveTimeFieldPolicySnapshot (
        juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
    {
        auto processor = std::make_unique<AudienceProcessor>();
        MutablePlayHead playHead;
        processor->setPlayHead (&playHead);
        processor->prepareToPlay (48000.0, audio.getNumSamples());
        pumpMessageLoop (80);
        setParameter (*processor, "midiOutputPath", 0.0f);
        setParameter (*processor, "timeMode", 1.0f); // Grid
        setParameter (*processor, "maxAttacksPerStep", 7.0f);
        setParameter (*processor, "maxActiveVoices", 9.0f);
        setParameter (*processor, "temporalSpread", 3.0f); // 8 steps
        setParameter (*processor, "crowdGovernorEnabled", 0.0f);
        setParameter (*processor, "safetyGovernorEnabled", 0.0f);
        expect (settleProcessorControlBoundary (
                    *processor, audio, midi, &playHead),
                "effective Time Field policy test settles its control boundary");

        const auto gridPolicy = processor->getEffectiveTimeFieldPolicy();
        expect (gridPolicy.mode == CrowdTimeField::Mode::Grid
                    && gridPolicy.attacksPerStep == 7
                    && gridPolicy.activeLimit == 9
                    && gridPolicy.spreadSlots == 8
                    && gridPolicy.admissionOpen,
                "one coherent snapshot publishes the final timed scheduler policy");

        setParameter (*processor, "timeMode", 0.0f); // Flow
        expect (settleProcessorControlBoundary (
                    *processor, audio, midi, &playHead),
                "Flow effective policy test settles its control boundary");
        const auto flowPolicy = processor->getEffectiveTimeFieldPolicy();
        expect (flowPolicy.mode == CrowdTimeField::Mode::Flow
                    && flowPolicy.attacksPerStep == 64
                    && flowPolicy.activeLimit == CrowdTimeField::kMaxVoices
                    && flowPolicy.admissionOpen,
                "Flow snapshot includes the final per-audio-block attack clamp");

        processor->startSourceQualityCheck();
        processOneBlock (*processor, audio, midi);
        playHead.advance (audio.getNumSamples(), 48000.0);
        const auto heldPolicy = processor->getEffectiveTimeFieldPolicy();
        const auto warmingQuality = processor->getSourceQualityOutput();
        const bool gateHeld = heldPolicy.mode == CrowdTimeField::Mode::Flow
                           && ! heldPolicy.admissionOpen
                           && warmingQuality.armed
                           && warmingQuality.state
                                == SourceQualityController::State::WARMING
                           && ! warmingQuality.admissionOpen;

        processor->audienceModel.setFingerX (0, 0, 0, 0.2f);
        processor->audienceModel.setFingerY (0, 0, 0, 0.8f);
        processor->audienceModel.setFingerOn (0, 0, 0, true);
        pumpMessageLoop (120);
        processOneBlock (*processor, audio, midi);
        const auto simulatorOnlyQuality = processor->getSourceQualityOutput();
        const bool simulatorExcluded = simulatorOnlyQuality.observedSources == 0
                                    && simulatorOnlyQuality.qualifiedSources == 0
                                    && ! processor->getEffectiveTimeFieldPolicy()
                                            .admissionOpen;

        processor->simulator.addRandomSeat();
        pumpMessageLoop (120);
        processOneBlock (*processor, audio, midi);
        const auto activeSimulatorQuality =
            processor->getSourceQualityOutput();
        const bool simulatorExplicitlyBlocks =
            activeSimulatorQuality.simulatorActive
            && SourceQualityController::hasReason(
                activeSimulatorQuality.reasonBits,
                SourceQualityController::ReasonSimulatorActive)
            && ! activeSimulatorQuality.admissionOpen;
        processor->simulator.clear();
        pumpMessageLoop (120);
        processOneBlock (*processor, audio, midi);

        processor->stopSourceQualityCheck();
        processOneBlock (*processor, audio, midi);
        expect (gateHeld && simulatorExcluded && simulatorExplicitlyBlocks
                    && processor->getEffectiveTimeFieldPolicy().admissionOpen
                    && processor->getSourceQualityOutput().state
                        == SourceQualityController::State::BYPASS,
                "Ready Gate closes final admission, excludes simulator evidence, blocks active simulation, and bypasses safely");

        processor->setPlayHead (nullptr);
        processor->releaseResources();
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    juce::AudioBuffer<float> audio (2, 512);
    juce::MidiBuffer midi;

    testProcessorLifecycleSafetyBoundaries (audio, midi);
    testGridTailAdmissionReconciliation (audio, midi);
    testEnsembleSameNoteArticulation (audio, midi);
    testEffectiveTimeFieldPolicySnapshot (audio, midi);

    {
        auto policyProcessorStorage = std::make_unique<AudienceProcessor>();
        auto& policyProcessor = *policyProcessorStorage;
        expect (policyProcessor.getNumPrograms() == 1
                    && policyProcessor.getCurrentProgram() == 0
                    && policyProcessor.getProgramName (0) == "Default"
                    && policyProcessor.getProgramName (-1).isEmpty()
                    && policyProcessor.getProgramName (1).isEmpty(),
                "VST3 program list exposes one stable non-empty Default program");
        policyProcessor.prepareToPlay (48000.0, 512);
        const bool factoryDefaultsSettled =
            waitForFactoryDefaultEndState (policyProcessor);
        if (! factoryDefaultsSettled)
            printFactoryDefaultDiagnostics (policyProcessor);

        expect (policyProcessor.getUdpPort() == 6062
                    && parameterInt (policyProcessor, "expectedZone") == 1
                    && parameterInt (policyProcessor, "conductorRole") == 1,
                "new instances settle on the screenshot-aligned Zone A route");
        expect (commonFactoryParametersMatch (policyProcessor),
                "new instances retain the complete factory parameter baseline");
        expect (policyProcessor.getResolvedMidiOutputOptionIndex() == 1,
                "new instances resolve the Zone A virtual MIDI endpoint");
        expect (policyProcessor.getMatchingFactoryPresetIndex() == 0
                    && AudienceProcessor::getNumFactoryPresets() == 8,
                "new instances match preset 0 in the stable eight-preset bank");

        bool allZonePresetsMatch = true;
        for (int index = 0; index < AudienceProcessor::getNumFactoryPresets(); ++index)
        {
            policyProcessor.applyFactoryPreset (index);
            allZonePresetsMatch = allZonePresetsMatch
                && policyProcessor.getUdpPort() == 6062 + index
                && parameterInt (policyProcessor, "expectedZone") == index + 1
                && parameterInt (policyProcessor, "conductorRole") == (index == 0 ? 1 : 2)
                && commonFactoryParametersMatch (policyProcessor)
                && policyProcessor.getResolvedMidiOutputOptionIndex() == 1
                && policyProcessor.getMatchingFactoryPresetIndex() == index
                && policyProcessor.simulator.getProfile() == Simulator::Profile::human
                && ! policyProcessor.simulator.isRandomMovementOn()
                && AudienceProcessor::getFactoryPresetName (index).contains (
                    "ZONE " + juce::String::charToString (
                        static_cast<juce::juce_wchar> ('A' + index)))
                && AudienceProcessor::getFactoryPresetName (index).contains (
                    juce::String (6062 + index));

            juce::MemoryBlock presetState;
            policyProcessor.getStateInformation (presetState);
            auto restoredPresetStorage = std::make_unique<AudienceProcessor>();
            auto& restoredPreset = *restoredPresetStorage;
            restoredPreset.setStateInformation (presetState.getData(),
                                                (int) presetState.getSize());
            allZonePresetsMatch = allZonePresetsMatch
                && restoredPreset.getUdpPort() == 6062 + index
                && restoredPreset.getResolvedMidiOutputOptionIndex() == 1
                && restoredPreset.getMatchingFactoryPresetIndex() == index
                && parameterInt (restoredPreset, "expectedZone") == index + 1
                && parameterInt (restoredPreset, "conductorRole") == (index == 0 ? 1 : 2)
                && commonFactoryParametersMatch (restoredPreset);
        }
        expect (allZonePresetsMatch,
                "Zone A-H presets and saved state preserve ports, virtual route and one musical baseline");
        policyProcessor.applyFactoryPreset (0);

        // v2.6 keeps these parameters only to read older projects. Their values
        // are inaudible and their controls are hidden, so they must not make the
        // active Zone A performance setup look like a custom preset in the UI.
        setParameter (policyProcessor, "crowdMacrosEnabled", 1.0f);
        setParameter (policyProcessor, "crowdMacroChannel", 16.0f);
        setParameter (policyProcessor, "crowdMacroDensityCc", 90.0f);
        setParameter (policyProcessor, "crowdMacroCentroidXCc", 91.0f);
        setParameter (policyProcessor, "crowdMacroCentroidYCc", 92.0f);
        setParameter (policyProcessor, "crowdMacroMotionCc", 93.0f);
        setParameter (policyProcessor, "crowdMacroRate", 3.0f);
        expect (policyProcessor.getMatchingFactoryPresetIndex() == 0,
                "retired Crowd Macro state does not create a false CUSTOM preset label");

        // The policy test observes the host buffer, so temporarily select Host
        // Only after verifying that the actual factory default is External Only.
        setParameter (policyProcessor, "midiOutputPath", 0.0f);

        // Settle the initial configuration transition, then prove at the real
        // processBlock boundary that legacy/host expression cannot leak through.
        processOneBlock (policyProcessor, audio, midi);
        if (auto* legacyMacros = policyProcessor.apvts.getParameter ("crowdMacrosEnabled"))
            legacyMacros->setValueNotifyingHost (1.0f);

        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 96), 0);
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 11, 13), 1);
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 74, 117), 2);
        midi.addEvent (juce::MidiMessage::channelPressureChange (1, 64), 3);
        midi.addEvent (juce::MidiMessage::pitchWheel (1, 12288), 4);
        midi.addEvent (juce::MidiMessage::aftertouchChange (1, 60, 80), 5);
        midi.addEvent (juce::MidiMessage::programChange (1, 7), 6);
        processPreparedMidiBlock (policyProcessor, audio, midi);

        int noteOnCount = 0;
        bool foundForbidden = false;
        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            noteOnCount += message.isNoteOn() ? 1 : 0;
            foundForbidden = foundForbidden || isForbiddenPerformanceMessage (message);
        }
        expect (noteOnCount == 1 && ! foundForbidden,
                "processBlock passes Note On but rejects CC, pressure, bend, aftertouch and program change");

        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        processPreparedMidiBlock (policyProcessor, audio, midi);
        int noteOffCount = 0;
        bool noteOffAndSafetyOnly = ! midi.isEmpty();
        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            noteOffCount += message.isNoteOff() ? 1 : 0;
            noteOffAndSafetyOnly = noteOffAndSafetyOnly
                                && (message.isNoteOff()
                                    || isSafetyResetController (message));
        }
        expect (noteOffCount == 1 && noteOffAndSafetyOnly,
                "processBlock passes Note Off and emits no performance controllers");

        // Exercise the production processor wiring, not only the standalone
        // scheduler unit. At 48 kHz / 120 BPM, 32n is exactly 3000 samples.
        // Empty callbacks must keep advancing the central deadline heap.
        setParameter (policyProcessor, "noteDuration", 4.0f);
        expect (settleProcessorControlBoundary (
                    policyProcessor, audio, midi),
                "Note Duration test settles pending route and configuration resets");
        policyProcessor.audienceModel.setFingerX (0, 1, 0, 0.42f);
        policyProcessor.audienceModel.setFingerY (0, 1, 0, 0.75f);
        policyProcessor.audienceModel.setFingerOn (0, 1, 0, true);
        processOneBlock (policyProcessor, audio, midi);

        int generatedChannel = 0;
        int generatedNote = -1;
        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            if (message.isNoteOn())
            {
                generatedChannel = message.getChannel();
                generatedNote = message.getNoteNumber();
                break;
            }
        }

        int observedDurationSamples = -1;
        for (int followingBlock = 1; followingBlock <= 6; ++followingBlock)
        {
            processOneBlock (policyProcessor, audio, midi);
            for (const auto metadata : midi)
            {
                const auto message = metadata.getMessage();
                if (message.isNoteOff()
                    && message.getChannel() == generatedChannel
                    && message.getNoteNumber() == generatedNote)
                {
                    observedDurationSamples = followingBlock * audio.getNumSamples()
                                            + metadata.samplePosition;
                }
            }
        }
        expect (generatedChannel == 1 && generatedNote >= 0
                    && observedDurationSamples == 3000
                    && policyProcessor.getScheduledMidiNoteCount() == 0,
                "processor applies saved Note Duration and releases on silent-block sample deadlines");
        policyProcessor.audienceModel.setFingerOn (0, 1, 0, false);
        processOneBlock (policyProcessor, audio, midi);

        // Runtime shrink must retire upper IDs on the control thread, then
        // issue one bounded audio-thread reset before any remaining source is
        // rehydrated. This checks the real APVTS -> timer -> processBlock path.
        setParameter (policyProcessor, "sourceCapacity", 1.0f); // 128
        pumpMessageLoop (25);
        policyProcessor.audienceModel.setFingerX (0, 100, 0, 0.68f);
        policyProcessor.audienceModel.setFingerY (0, 100, 0, 0.80f);
        policyProcessor.audienceModel.setFingerOn (0, 100, 0, true);
        // First block consumes the capacity-change route boundary and rebuilds
        // canonical held state; the following Flow block admits that state.
        processOneBlock (policyProcessor, audio, midi);
        processOneBlock (policyProcessor, audio, midi);
        const bool upperSourceStarted = policyProcessor.getScheduledMidiNoteCount() > 0;

        setParameter (policyProcessor, "sourceCapacity", 0.0f); // 64
        pumpMessageLoop (25);
        processOneBlock (policyProcessor, audio, midi);
        bool shrinkResetSeen = false;
        for (const auto metadata : midi)
            shrinkResetSeen = shrinkResetSeen
                           || isSafetyResetController (metadata.getMessage());
        const int modelCapacityAfterShrink =
            policyProcessor.audienceModel.getSourceCapacity();
        const int simulatorCapacityAfterShrink =
            policyProcessor.simulator.getSourceCapacity();
        const bool upperSourceStillActive =
            policyProcessor.audienceModel.getFingerSnapshot (100, 0).active;
        const int scheduledAfterShrink =
            policyProcessor.getScheduledMidiNoteCount();
        const bool shrinkPassed = upperSourceStarted
                               && modelCapacityAfterShrink == 64
                               && simulatorCapacityAfterShrink == 64
                               && ! upperSourceStillActive
                               && scheduledAfterShrink == 0
                               && shrinkResetSeen;
        if (! shrinkPassed)
            std::cerr << "capacity shrink: started=" << upperSourceStarted
                      << " model=" << modelCapacityAfterShrink
                      << " simulator=" << simulatorCapacityAfterShrink
                      << " active100=" << upperSourceStillActive
                      << " scheduled=" << scheduledAfterShrink
                      << " reset=" << shrinkResetSeen << '\n';
        expect (shrinkPassed,
                "runtime capacity shrink retires upper sources and emits bounded stuck-note cleanup");

        policyProcessor.releaseResources();
    }

    juce::MemoryBlock customSavedState;
    {
        auto freshProcessorStorage = std::make_unique<AudienceProcessor>();
        auto& freshProcessor = *freshProcessorStorage;
        juce::MemoryBlock freshState;
        freshProcessor.getStateInformation (freshState);
        bool pendingFactoryRouteSaved = false;
        if (auto xml = juce::AudioProcessor::getXmlFromBinary (
                freshState.getData(), (int) freshState.getSize()))
        {
            const auto tree = juce::ValueTree::fromXml (*xml);
            pendingFactoryRouteSaved =
                (int) tree.getProperty ("udpPort", -1) == 6062
                && (int) tree.getProperty ("midiOutputOption", -1) == 1
                && (int) tree.getProperty ("midiOutputRouteKind", -1) == 1;
        }
        expect (pendingFactoryRouteSaved,
                "state saved before the first timer tick retains the intended Zone A virtual route");
    }

    {
        auto savedProcessorStorage = std::make_unique<AudienceProcessor>();
        auto& savedProcessor = *savedProcessorStorage;
        savedProcessor.applyFactoryPreset (2);
        setParameter (savedProcessor, "midiOutputPath", 0.0f);
        setParameter (savedProcessor, "timeMode", 2.0f);
        setParameter (savedProcessor, "gatePercent", 82.0f);
        setParameter (savedProcessor, "noteDuration", 1.0f);
        setParameter (savedProcessor, "sourceCapacity", 1.0f);
        setParameter (savedProcessor, "ensembleSameNoteMode", 1.0f);
        setParameter (savedProcessor, "conductorRole", 0.0f);
        savedProcessor.setUdpPort (8000);
        savedProcessor.setMidiOutputOptionIndex (0);
        savedProcessor.getStateInformation (customSavedState);
        expect (savedProcessor.getUdpPort() == 8000,
                "custom state fixture owns its requested UDP port before serialization");
        if (auto xml = juce::AudioProcessor::getXmlFromBinary (
                customSavedState.getData(), (int) customSavedState.getSize()))
        {
            const auto tree = juce::ValueTree::fromXml (*xml);
            expect ((int) tree.getProperty ("udpPort", -1) == 8000
                        && (int) tree.getProperty ("midiOutputRouteKind", -1) == 0,
                    "serialized custom state contains its non-APVTS route snapshot");
        }
    }

    {
        auto restoredProcessorStorage = std::make_unique<AudienceProcessor>();
        auto& restoredProcessor = *restoredProcessorStorage;
        restoredProcessor.setStateInformation (customSavedState.getData(),
                                               (int) customSavedState.getSize());
        pumpMessageLoop (80);
        const int restoredPort = restoredProcessor.getUdpPort();
        const int restoredPath = parameterInt (restoredProcessor, "midiOutputPath");
        const int restoredTime = parameterInt (restoredProcessor, "timeMode");
        const int restoredGate = parameterInt (restoredProcessor, "gatePercent");
        const int restoredDuration = parameterInt (restoredProcessor, "noteDuration");
        const int restoredCapacity = parameterInt (restoredProcessor, "sourceCapacity");
        const int restoredSameNote = parameterInt (
            restoredProcessor, "ensembleSameNoteMode");
        const int restoredRole = parameterInt (restoredProcessor, "conductorRole");
        const int restoredEndpoint = restoredProcessor.getResolvedMidiOutputOptionIndex();
        const int restoredPreset = restoredProcessor.getMatchingFactoryPresetIndex();
        const bool restoredMatches = restoredPort == 8000
                                  && restoredPath == 0
                                  && restoredTime == 2
                                  && restoredGate == 82
                                  && restoredDuration == 1
                                  && restoredCapacity == 1
                                  && restoredSameNote == 1
                                  && restoredRole == 0
                                  && restoredEndpoint == 0
                                  && restoredPreset == -1;
        if (! restoredMatches)
            std::cerr << "restore values: port=" << restoredPort
                      << " path=" << restoredPath
                      << " time=" << restoredTime
                      << " gate=" << restoredGate
                      << " duration=" << restoredDuration
                      << " capacity=" << restoredCapacity
                      << " sameNote=" << restoredSameNote
                      << " role=" << restoredRole
                      << " endpoint=" << restoredEndpoint
                      << " preset=" << restoredPreset << '\n';
        expect (restoredMatches,
                "saved Ableton project state overrides the new-instance factory default atomically");
    }

    {
        auto savedProcessorStorage = std::make_unique<AudienceProcessor>();
        auto& savedProcessor = *savedProcessorStorage;
        setParameter (savedProcessor, "midiOutputType", 0.0f);
        setParameter (savedProcessor, "exclusiveUdpPort", 0.0f);
        setParameter (savedProcessor, "mpeSendSetupMessages", 0.0f);
        setParameter (savedProcessor, "crowdMacrosEnabled", 1.0f);
        setParameter (savedProcessor, "ensembleSameNoteMode", 1.0f);

        juce::MemoryBlock parameterState;
        savedProcessor.getStateInformation (parameterState);

        auto restoredProcessorStorage = std::make_unique<AudienceProcessor>();
        auto& restoredProcessor = *restoredProcessorStorage;
        restoredProcessor.setStateInformation (parameterState.getData(),
                                               (int) parameterState.getSize());

        expect (parameterInt (restoredProcessor, "midiOutputType") == 0
                    && parameterInt (restoredProcessor, "exclusiveUdpPort") == 0
                    && parameterInt (restoredProcessor, "mpeSendSetupMessages") == 0
                    && parameterInt (restoredProcessor, "crowdMacrosEnabled") == 1
                    && parameterInt (restoredProcessor,
                                     "ensembleSameNoteMode") == 1,
                "current-schema MIDI, venue and inert legacy parameters survive processor state round-trip");
    }

    {
        auto stateTemplateStorage = std::make_unique<AudienceProcessor>();
        auto& stateTemplate = *stateTemplateStorage;
        auto partialState = stateTemplate.apvts.copyState();
        partialState.setProperty ("cosmicMicrowaveSchema", 9, nullptr);
        juce::MemoryBlock partialStateData;
        if (auto xml = partialState.createXml())
            juce::AudioProcessor::copyXmlToBinary (*xml, partialStateData);

        auto restoredPartialStorage = std::make_unique<AudienceProcessor>();
        auto& restoredPartial = *restoredPartialStorage;
        restoredPartial.setStateInformation (partialStateData.getData(),
                                             (int) partialStateData.getSize());
        pumpMessageLoop (80);
        expect (restoredPartial.getUdpPort() == 6062
                    && commonFactoryParametersMatch (restoredPartial),
                "current APVTS state missing root route metadata falls back to Zone A virtual routing");
    }

    {
        auto stateTemplateStorage = std::make_unique<AudienceProcessor>();
        auto& stateTemplate = *stateTemplateStorage;
        auto fractionalRoute = stateTemplate.apvts.copyState();
        fractionalRoute.setProperty ("cosmicMicrowaveSchema",
                                     CosmicStateMigration::currentSchema, nullptr);
        fractionalRoute.setProperty ("udpPort", 8123.5, nullptr);
        fractionalRoute.setProperty ("midiOutputOption", 1.5, nullptr);
        fractionalRoute.setProperty ("midiOutputRouteKind", 1.5, nullptr);
        const auto fractionalData = serializeState (fractionalRoute);

        auto restoredFractionalStorage = std::make_unique<AudienceProcessor>();
        auto& restoredFractional = *restoredFractionalStorage;
        restoredFractional.setStateInformation (fractionalData.getData(),
                                                (int) fractionalData.getSize());
        expect (restoredFractional.getUdpPort() == 6062
                    && restoredFractional.getResolvedMidiOutputOptionIndex() == 1,
                "fractional route integers are rejected in favour of safe factory fallbacks");
    }

    {
        constexpr auto missingDeviceId = "cosmic.test.missing-physical-midi";
        auto stateTemplateStorage = std::make_unique<AudienceProcessor>();
        auto& stateTemplate = *stateTemplateStorage;
        auto unavailableRoute = stateTemplate.apvts.copyState();
        unavailableRoute.setProperty ("cosmicMicrowaveSchema",
                                      CosmicStateMigration::currentSchema, nullptr);
        unavailableRoute.setProperty ("udpPort", 6062, nullptr);
        unavailableRoute.setProperty ("midiOutputOption", 99, nullptr);
        unavailableRoute.setProperty ("midiOutputRouteKind", 2, nullptr);
        unavailableRoute.setProperty ("midiOutputDeviceIdentifier",
                                      missingDeviceId, nullptr);
        const auto unavailableData = serializeState (unavailableRoute);

        auto restoredUnavailableStorage = std::make_unique<AudienceProcessor>();
        auto& restoredUnavailable = *restoredUnavailableStorage;
        restoredUnavailable.setStateInformation (unavailableData.getData(),
                                                 (int) unavailableData.getSize());
        juce::MemoryBlock resavedUnavailable;
        restoredUnavailable.getStateInformation (resavedUnavailable);
        bool logicalSelectionPreserved = false;
        if (auto xml = juce::AudioProcessor::getXmlFromBinary (
                resavedUnavailable.getData(), (int) resavedUnavailable.getSize()))
        {
            const auto tree = juce::ValueTree::fromXml (*xml);
            logicalSelectionPreserved =
                (int) tree.getProperty ("midiOutputRouteKind", -1) == 2
                && tree.getProperty ("midiOutputDeviceIdentifier").toString()
                    == missingDeviceId;
        }
        expect (restoredUnavailable.getResolvedMidiOutputOptionIndex() == -1
                    && ! restoredUnavailable.isMidiOutputReady()
                    && logicalSelectionPreserved
                    && restoredUnavailable.getMidiOutputDescription().containsIgnoreCase (
                        "unavailable"),
                "missing saved physical MIDI endpoint remains selected logically and fail-closed");
    }

    {
        auto stateTemplateStorage = std::make_unique<AudienceProcessor>();
        auto& stateTemplate = *stateTemplateStorage;
        auto outerState = stateTemplate.apvts.copyState();
        setStateParameter (outerState, "midiOutputPath", 0.0f);
        setStateParameter (outerState, "timeMode", 2.0f);
        setStateParameter (outerState, "gatePercent", 82.0f);
        setStateParameter (outerState, "pitchSystem", 0.0f);
        outerState.setProperty ("udpPort", 8000, nullptr);
        outerState.setProperty ("midiOutputOption", 0, nullptr);
        outerState.setProperty ("midiOutputRouteKind", 0, nullptr);
        outerState.setProperty ("midiOutputDeviceIdentifier", {}, nullptr);

        auto nestedState = stateTemplate.apvts.copyState();
        setStateParameter (nestedState, "midiOutputPath", 1.0f);
        setStateParameter (nestedState, "timeMode", 0.0f);
        setStateParameter (nestedState, "gatePercent", 100.0f);
        setStateParameter (nestedState, "pitchSystem", 1.0f);
        setStateParameter (nestedState, "expectedZone", 4.0f);
        setStateParameter (nestedState, "conductorRole", 2.0f);
        nestedState.setProperty ("udpPort", 6065, nullptr);
        nestedState.setProperty ("midiOutputOption", 1, nullptr);
        nestedState.setProperty ("midiOutputRouteKind", 1, nullptr);
        nestedState.setProperty ("midiOutputDeviceIdentifier", {}, nullptr);

        const auto outerData = serializeState (outerState);
        const auto nestedData = serializeState (nestedState);
        auto reentrantProcessorStorage = std::make_unique<AudienceProcessor>();
        auto& reentrantProcessor = *reentrantProcessorStorage;
        auto* trigger = reentrantProcessor.apvts.getParameter ("midiOutputPath");
        ReentrantRestoreListener listener (reentrantProcessor, nestedData);
        if (trigger != nullptr)
            trigger->addListener (&listener);
        reentrantProcessor.setStateInformation (outerData.getData(),
                                                (int) outerData.getSize());
        if (trigger != nullptr)
            trigger->removeListener (&listener);

        expect (listener.didFire()
                    && reentrantProcessor.getUdpPort() == 6065
                    && parameterInt (reentrantProcessor, "midiOutputPath") == 1
                    && parameterInt (reentrantProcessor, "timeMode") == 0
                    && parameterInt (reentrantProcessor, "gatePercent") == 100
                    && parameterInt (reentrantProcessor, "pitchSystem") == 1
                    && parameterInt (reentrantProcessor, "expectedZone") == 4
                    && parameterInt (reentrantProcessor, "conductorRole") == 2
                    && reentrantProcessor.getResolvedMidiOutputOptionIndex() == 1,
                "nested state recall is latest-wins with one complete APVTS and route snapshot");
    }

    {
        auto routeEditProcessorStorage = std::make_unique<AudienceProcessor>();
        auto& routeEditProcessor = *routeEditProcessorStorage;
        setParameter (routeEditProcessor, "midiOutputPath", 0.0f);
        auto* trigger = routeEditProcessor.apvts.getParameter ("midiOutputPath");
        ReentrantRouteEditListener listener (routeEditProcessor);
        if (trigger != nullptr)
            trigger->addListener (&listener);
        routeEditProcessor.applyFactoryPreset (0);
        if (trigger != nullptr)
            trigger->removeListener (&listener);

        expect (listener.didFire()
                    && routeEditProcessor.getUdpPort() == 7777
                    && routeEditProcessor.getResolvedMidiOutputOptionIndex() == 0
                    && routeEditProcessor.getMatchingFactoryPresetIndex() == -1,
                "manual route edit inside a preset callback is deferred and wins coherently");
    }

    {
        auto stateTemplateStorage = std::make_unique<AudienceProcessor>();
        auto& stateTemplate = *stateTemplateStorage;
        auto workerState = stateTemplate.apvts.copyState();
        setStateParameter (workerState, "midiOutputPath", 0.0f);
        workerState.setProperty ("udpPort", 8123, nullptr);
        workerState.setProperty ("midiOutputOption", 0, nullptr);
        workerState.setProperty ("midiOutputRouteKind", 0, nullptr);
        workerState.setProperty ("midiOutputDeviceIdentifier", {}, nullptr);
        const auto workerData = serializeState (workerState);

        auto workerRestoredStorage = std::make_unique<AudienceProcessor>();
        auto& workerRestored = *workerRestoredStorage;
        workerRestored.prepareToPlay (48000.0, 512);
        std::thread restoreThread ([&]
        {
            workerRestored.setStateInformation (workerData.getData(),
                                                (int) workerData.getSize());
        });
        restoreThread.join();

        workerRestored.simulator.addRandomSeats (1);
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
        processPreparedMidiBlock (workerRestored, audio, midi);
        int hostNoteCount = 0;
        int hostResetCount = 0;
        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            hostNoteCount += message.isNoteOn() ? 1 : 0;
            hostResetCount += message.isController()
                           && (message.getControllerNumber() == 120
                               || message.getControllerNumber() == 123)
                            ? 1 : 0;
        }
        expect (hostNoteCount == 1 && hostResetCount == 32,
                "worker restore resets the previous Host generation, passes note thru and suppresses OSC/simulator");

        midi.clear();
        processPreparedMidiBlock (workerRestored, audio, midi);
        int repeatedHostResetCount = 0;
        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            repeatedHostResetCount += message.isController()
                                   && (message.getControllerNumber() == 120
                                       || message.getControllerNumber() == 123)
                                    ? 1 : 0;
        }
        expect (repeatedHostResetCount == 0,
                "worker restore Host reset is acknowledged exactly once per state generation");

        setParameter (workerRestored, "gatePercent", 77.0f);
        juce::MemoryBlock automatedPendingState;
        workerRestored.getStateInformation (automatedPendingState);
        bool pendingAutomationSaved = false;
        if (auto xml = juce::AudioProcessor::getXmlFromBinary (
                automatedPendingState.getData(),
                (int) automatedPendingState.getSize()))
        {
            const auto tree = juce::ValueTree::fromXml (*xml);
            const auto gate = CosmicStateMigration::findParameterNode (
                tree, "gatePercent");
            pendingAutomationSaved = gate.isValid()
                && std::abs ((float) gate.getProperty ("value") - 77.0f) < 0.01f;
        }
        expect (pendingAutomationSaved,
                "automation after APVTS restore is saved while its route is still pending");

        workerRestored.setMidiOutputOptionIndex (0);
        workerRestored.setUdpPort (8124);
        juce::MemoryBlock editedState;
        workerRestored.getStateInformation (editedState);
        bool manualRouteWon = false;
        if (auto xml = juce::AudioProcessor::getXmlFromBinary (
                editedState.getData(), (int) editedState.getSize()))
        {
            const auto tree = juce::ValueTree::fromXml (*xml);
            manualRouteWon = (int) tree.getProperty ("udpPort", -1) == 8124
                          && (int) tree.getProperty ("midiOutputOption", -1) == 0
                          && (int) tree.getProperty ("midiOutputRouteKind", -1) == 0;
        }
        expect (manualRouteWon,
                "manual route edit supersedes an older pending worker restore coherently");
        workerRestored.releaseResources();
    }

    {
        // Exercise the production control/audio handshake under genuine
        // overlap. The audio callback must never wait for the control thread;
        // panic and route mutations close the gate, wait for the one in-flight
        // block, mutate bounded state, then reopen it.
        auto concurrentStorage = std::make_unique<AudienceProcessor>();
        auto& concurrent = *concurrentStorage;
        concurrent.prepareToPlay (48000.0, 128);
        concurrent.setMidiOutputOptionIndex (0);

        std::atomic<bool> keepProcessing { true };
        std::atomic<int> processedBlocks { 0 };
        std::thread audioThread ([&]
        {
            juce::AudioBuffer<float> threadAudio (2, 128);
            juce::MidiBuffer threadMidi;
            while (keepProcessing.load (std::memory_order_acquire))
            {
                processOneBlock (concurrent, threadAudio, threadMidi);
                processedBlocks.fetch_add (1, std::memory_order_release);
            }
        });

        while (processedBlocks.load (std::memory_order_acquire) < 32)
            juce::Thread::yield();

        for (int iteration = 0; iteration < 64; ++iteration)
        {
            if ((iteration & 1) == 0)
                concurrent.panic();
            else
                concurrent.setMidiOutputOptionIndex (0);
        }

        keepProcessing.store (false, std::memory_order_release);
        audioThread.join();
        concurrent.panic();

        expect (processedBlocks.load (std::memory_order_acquire) >= 32
                    && concurrent.getScheduledMidiNoteCount() == 0
                    && concurrent.getPhysicalMidiNoteCount() == 0,
                "concurrent audio callbacks, panic and route changes remain bounded and deadlock-free");
        concurrent.releaseResources();
    }

    auto processorStorage = std::make_unique<AudienceProcessor>();
    auto& processor = *processorStorage;
    processor.prepareToPlay (48000.0, 512);
    processor.applyFactoryPreset (0);

    processor.simulator.addCrowdParticipants (25);
    processOneBlock (processor, audio, midi);

    expect (processor.simulator.getSimSeatCount() == 25
                && processor.simulator.getCrowdParticipantCount() == 25
                && processor.getGovernorObservedDensity() == 25,
            "+25 Crowd population reaches the processor Governor observation");

    // The production Governor intentionally requires a 500 ms promotion hold.
    // A second block after that real control-clock interval exercises the same
    // processBlock boundary and profile publication used by a host.
    juce::Thread::sleep (550);
    processOneBlock (processor, audio, midi);

    expect (processor.getGovernorObservedDensity() == 25
                && processor.getGovernorBand() == 2
                && processor.getGovernorEffectiveAttacksPerStep() == 3
                && processor.getGovernorEffectiveSpreadSlots() == 4
                && processor.getGovernorEffectiveActiveVoices() == 12,
            "+25 Crowd promotes to the expected production Governor profile");

    processor.simulator.clear();
    processor.releaseResources();

    if (failures != 0)
        std::cerr << failures << " processor/simulator Governor test(s) failed\n";
    return failures == 0 ? 0 : 1;
}
