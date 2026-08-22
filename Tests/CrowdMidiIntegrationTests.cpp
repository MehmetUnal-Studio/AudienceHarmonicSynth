#include "../Source/CrowdLfoGate.h"
#include "../Source/CrowdTimeField.h"
#include "../Source/MidiAudienceModel.h"
#include "../Source/MpeMidiOutput.h"

#include <array>
#include <atomic>
#include <cmath>
#include <iostream>

namespace
{
    int failed = 0;
    std::atomic<std::uint32_t> fakeNowMs { 100 };

    std::uint32_t fakeMonotonicClock() noexcept
    {
        return fakeNowMs.load(std::memory_order_relaxed);
    }

    void expect (bool condition, const char* name)
    {
        std::cout << (condition ? "PASS  " : "FAIL  ") << name << "\n";
        if (! condition)
            ++failed;
    }

    double frequencyForSource (int sourceId)
    {
        const int semitones = sourceId == 1 ? 0 : sourceId == 2 ? 1 : 2;
        return 440.0 * std::pow(2.0, (double) semitones / 12.0);
    }

    CrowdTimeField::ClockFrame frameAt (double ppq)
    {
        CrowdTimeField::ClockFrame frame;
        frame.sampleRate = 48000.0;
        frame.numSamples = 512;
        frame.hostValid = true;
        frame.isPlaying = true;
        frame.bpm = 120.0;
        frame.ppqPosition = ppq;
        return frame;
    }

    bool hasPhysicalNote (const juce::MidiBuffer& buffer, bool noteOn,
                          int channel)
    {
        for (const auto metadata : buffer)
        {
            if (metadata.numBytes < 3)
                continue;
            const auto* bytes = metadata.data;
            const int type = bytes[0] & 0xf0;
            const bool isOn = type == 0x90 && bytes[2] != 0;
            const bool isOff = type == 0x80 || (type == 0x90 && bytes[2] == 0);
            if ((noteOn ? isOn : isOff)
                && (bytes[0] & 0x0f) + 1 == channel)
                return true;
        }
        return false;
    }

    bool hasPhysicalNoteAt (const juce::MidiBuffer& buffer, bool noteOn,
                            int channel, int sampleOffset)
    {
        for (const auto metadata : buffer)
        {
            if (metadata.numBytes < 3 || metadata.samplePosition != sampleOffset)
                continue;

            const auto* bytes = metadata.data;
            const int type = bytes[0] & 0xf0;
            const bool isOn = type == 0x90 && bytes[2] != 0;
            const bool isOff = type == 0x80 || (type == 0x90 && bytes[2] == 0);
            if ((noteOn ? isOn : isOff)
                && (bytes[0] & 0x0f) + 1 == channel)
                return true;
        }
        return false;
    }

    CrowdTimeField::ClockFrame preciseTimeFrame (int numSamples, double ppq)
    {
        CrowdTimeField::ClockFrame frame;
        frame.sampleRate = 1000.0;
        frame.numSamples = numSamples;
        frame.hostValid = true;
        frame.isPlaying = true;
        frame.bpm = 60.0;
        frame.ppqPosition = ppq;
        return frame;
    }

    CrowdLfoGate::ClockFrame preciseLfoFrame (int numSamples,
                                               double monotonicSeconds)
    {
        CrowdLfoGate::ClockFrame frame;
        frame.sampleRate = 1000.0;
        frame.numSamples = numSamples;
        frame.monotonicSeconds = monotonicSeconds;
        frame.fallbackBpm = 60.0;
        return frame;
    }

    CrowdLfoGate::Config squareTimeGate()
    {
        CrowdLfoGate::Config config;
        config.enabled = true;
        config.waveform = CrowdLfoGate::Waveform::Square;
        config.rateMode = CrowdLfoGate::RateMode::Hertz;
        config.rateHz = 1.0;
        return config;
    }

    int translateGateTransitions (
        const CrowdLfoGate::OutputBlock& lfo,
        std::array<CrowdTimeField::InputEvent,
                   CrowdLfoGate::kMaxTransitionsPerBlock>& events)
    {
        for (int index = 0; index < lfo.count; ++index)
        {
            const auto& transition = lfo.transitions[static_cast<std::size_t>(index)];
            events[static_cast<std::size_t>(index)] = {
                transition.type == CrowdLfoGate::Transition::Type::Open
                    ? CrowdTimeField::InputEvent::Type::GateOpen
                    : CrowdTimeField::InputEvent::Type::GateClose,
                -1,
                -1,
                transition.sampleOffset
            };
        }
        return lfo.count;
    }

    const CrowdTimeField::OutputEvent* firstScheduled (
        const CrowdTimeField::OutputBlock& output,
        CrowdTimeField::OutputEvent::Type type)
    {
        for (int index = 0; index < output.count; ++index)
            if (output.events[static_cast<std::size_t>(index)].type == type)
                return &output.events[static_cast<std::size_t>(index)];
        return nullptr;
    }
}

int main()
{
    CrowdTimeField field;
    CrowdTimeField::Config timing;
    timing.mode = CrowdTimeField::Mode::Grid;
    timing.clockSource = CrowdTimeField::ClockSource::Host;
    timing.division = CrowdTimeField::Division::Sixteenth;
    timing.maxAttacksPerStep = 4;
    timing.maxActive = 16;
    timing.spreadSlots = 1;

    constexpr std::array<int, 3> sources { 1, 2, 17 };
    std::array<CrowdTimeField::InputEvent, sources.size()> ons {};
    for (size_t index = 0; index < sources.size(); ++index)
    {
        ons[index].type = CrowdTimeField::InputEvent::Type::On;
        ons[index].sourceId = sources[index];
        ons[index].voiceId = CrowdTimeField::voiceIdFor(sources[index], 0);
        ons[index].sampleOffset = 0;
    }

    CrowdTimeField::OutputBlock scheduledOn;
    field.process(timing, frameAt(0.24), ons.data(), (int) ons.size(), scheduledOn);
    expect(! scheduledOn.resetRequested && scheduledOn.count == 3,
           "three source intents become three scheduled attacks");
    expect(scheduledOn.events[0].sampleOffset == 240
               && scheduledOn.events[1].sampleOffset == 240
               && scheduledOn.events[2].sampleOffset == 240,
           "host PPQ places the shared 1/16 boundary at exact sample 240");

    MpeMidiOutput midi;
    MpeMidiOutput::MpeConfig midiConfig;
    midiConfig.outputType = 1;
    midiConfig.normalRoutingMode = 1;

    std::array<MpeMidiOutput::NoteEvent, sources.size()> midiOns {};
    for (size_t index = 0; index < sources.size(); ++index)
    {
        const auto& scheduled = scheduledOn.events[index];
        auto& event = midiOns[index];
        event.type = MpeMidiOutput::NoteEvent::NoteOn;
        event.sourceId = scheduled.voiceId;
        event.participantId = scheduled.sourceId;
        event.sampleOffset = scheduled.sampleOffset;
        event.frequencyHz = frequencyForSource(scheduled.sourceId);
        event.velocity = 0.8f;
        event.x = 0.5f;
        event.y = 0.8f;
    }

    juce::MidiBuffer noteOns;
    midi.render(midiConfig, midiOns.data(), (int) midiOns.size(), noteOns, 512);
    std::array<bool, 3> foundOn {};
    for (const auto metadata : noteOns)
    {
        const auto* bytes = metadata.data;
        if (metadata.numBytes < 3 || (bytes[0] & 0xf0) != 0x90 || bytes[2] == 0)
            continue;
        const int channel = (bytes[0] & 0x0f) + 1;
        const int note = bytes[1] & 0x7f;
        foundOn[0] = foundOn[0] || (channel == 1 && note == 69);
        foundOn[1] = foundOn[1] || (channel == 2 && note == 70);
        foundOn[2] = foundOn[2] || (channel == 1 && note == 71);
        expect(metadata.samplePosition == 240,
               "every scheduled physical NoteOn retains the grid sample offset");
    }
    expect(foundOn[0] && foundOn[1] && foundOn[2],
           "source 1/2/17 retain stable channels 1/2/1 after scheduling");

    std::array<CrowdTimeField::InputEvent, sources.size()> offs {};
    for (size_t index = 0; index < sources.size(); ++index)
    {
        offs[index].type = CrowdTimeField::InputEvent::Type::Off;
        offs[index].sourceId = sources[index];
        offs[index].voiceId = CrowdTimeField::voiceIdFor(sources[index], 0);
        offs[index].sampleOffset = 10;
    }

    CrowdTimeField::OutputBlock scheduledOff;
    field.process(timing, frameAt(0.24 + 512.0 * 120.0 / (60.0 * 48000.0)),
                  offs.data(), (int) offs.size(), scheduledOff);
    expect(! scheduledOff.resetRequested && scheduledOff.count == 3,
           "three releases survive the timing layer without loss");

    std::array<MpeMidiOutput::NoteEvent, sources.size()> midiOffs {};
    for (size_t index = 0; index < sources.size(); ++index)
    {
        const auto& scheduled = scheduledOff.events[index];
        midiOffs[index].type = MpeMidiOutput::NoteEvent::NoteOff;
        midiOffs[index].sourceId = scheduled.voiceId;
        midiOffs[index].participantId = scheduled.sourceId;
        midiOffs[index].sampleOffset = scheduled.sampleOffset;
    }

    juce::MidiBuffer noteOffs;
    midi.render(midiConfig, midiOffs.data(), (int) midiOffs.size(), noteOffs, 512);
    int physicalOffs = 0;
    bool channelsCorrect = true;
    for (const auto metadata : noteOffs)
    {
        const auto* bytes = metadata.data;
        if (metadata.numBytes < 3 || (bytes[0] & 0xf0) != 0x80)
            continue;
        ++physicalOffs;
        const int channel = (bytes[0] & 0x0f) + 1;
        const int note = bytes[1] & 0x7f;
        channelsCorrect = channelsCorrect
                       && ((channel == 1 && (note == 69 || note == 71))
                           || (channel == 2 && note == 70));
        expect(metadata.samplePosition == 10,
               "every NoteOff retains its lifecycle sample offset");
    }
    expect(physicalOffs == 3 && channelsCorrect,
           "NoteOffs return on the stored source channels with no stuck owner");

    // Live-phone disconnect watchdog: the canonical model publishes an ordered
    // synthetic Off after exactly 3 s. Verify both immediate Flow and a timed
    // Grid path close the same source-owned MIDI channel.
    for (const auto mode : { CrowdTimeField::Mode::Flow,
                             CrowdTimeField::Mode::Grid })
    {
        OscFingerRouter liveRouter;
        MidiAudienceModel liveModel(liveRouter, &fakeMonotonicClock);
        fakeNowMs.store(100, std::memory_order_relaxed);
        if (mode != CrowdTimeField::Mode::Flow)
            liveModel.setMotionEventForwardingEnabled(false);
        liveRouter.takeResetRequest();
        liveRouter.discardPendingEvents();

        liveModel.setLiveFingerOn(0, 17, 0, true);
        std::array<OscFingerRouter::Event, 8> routed {};
        const int routedOnCount = liveRouter.drain(routed.data(),
                                                   (int) routed.size());
        CrowdTimeField::InputEvent onInput;
        bool foundRoutedOn = false;
        for (int index = 0; index < routedOnCount; ++index)
        {
            if (routed[(size_t) index].type != OscFingerRouter::Event::On)
                continue;
            onInput.type = CrowdTimeField::InputEvent::Type::On;
            onInput.sourceId = routed[(size_t) index].sourceId;
            onInput.voiceId = CrowdTimeField::voiceIdFor(onInput.sourceId, 0);
            foundRoutedOn = true;
        }

        CrowdTimeField scheduler;
        CrowdTimeField::Config watchdogTiming = timing;
        watchdogTiming.mode = mode;
        CrowdTimeField::OutputBlock watchdogAttack;
        scheduler.process(watchdogTiming, frameAt(0.0), &onInput,
                          foundRoutedOn ? 1 : 0, watchdogAttack);

        MpeMidiOutput watchdogMidi;
        MpeMidiOutput::NoteEvent note;
        note.type = MpeMidiOutput::NoteEvent::NoteOn;
        note.sourceId = CrowdTimeField::voiceIdFor(17, 0);
        note.participantId = 17;
        note.frequencyHz = 440.0;
        note.velocity = 0.8f;
        juce::MidiBuffer watchdogOnMidi;
        if (watchdogAttack.count > 0
            && watchdogAttack.events[0].type == CrowdTimeField::OutputEvent::Type::Attack)
        {
            note.sampleOffset = watchdogAttack.events[0].sampleOffset;
            watchdogMidi.render(midiConfig, &note, 1, watchdogOnMidi, 512);
        }

        fakeNowMs.store(3100, std::memory_order_relaxed);
        const int expired = liveModel.expireStaleLiveTouches();
        const int routedOffCount = liveRouter.drain(routed.data(),
                                                    (int) routed.size());
        CrowdTimeField::InputEvent offInput;
        bool foundRoutedOff = false;
        for (int index = 0; index < routedOffCount; ++index)
        {
            if (routed[(size_t) index].type != OscFingerRouter::Event::Off)
                continue;
            offInput.type = CrowdTimeField::InputEvent::Type::Off;
            offInput.sourceId = routed[(size_t) index].sourceId;
            offInput.voiceId = CrowdTimeField::voiceIdFor(offInput.sourceId, 0);
            foundRoutedOff = true;
        }

        CrowdTimeField::OutputBlock watchdogRelease;
        scheduler.process(watchdogTiming,
                          frameAt(512.0 * 120.0 / (60.0 * 48000.0)),
                          &offInput, foundRoutedOff ? 1 : 0,
                          watchdogRelease);
        MpeMidiOutput::NoteEvent release;
        release.type = MpeMidiOutput::NoteEvent::NoteOff;
        release.sourceId = CrowdTimeField::voiceIdFor(17, 0);
        release.participantId = 17;
        juce::MidiBuffer watchdogOffMidi;
        for (int index = 0; index < watchdogRelease.count; ++index)
        {
            if (watchdogRelease.events[(size_t) index].type
                != CrowdTimeField::OutputEvent::Type::Release)
                continue;
            release.sampleOffset = watchdogRelease.events[(size_t) index].sampleOffset;
            watchdogMidi.render(midiConfig, &release, 1, watchdogOffMidi, 512);
        }

        expect(foundRoutedOn && expired == 1 && foundRoutedOff
                   && hasPhysicalNote(watchdogOnMidi, true, 1)
                   && hasPhysicalNote(watchdogOffMidi, false, 1),
               mode == CrowdTimeField::Mode::Flow
                   ? "watchdog Off closes the source-owned channel in Flow"
                   : "watchdog Off closes the source-owned channel in timed Grid");
    }

    // Component seam regression for the Time Gate lifecycle. The actual LFO
    // produces a falling edge at sample 37; Time Field converts it into a
    // semantic Release, and MIDI output must use the channel stored by the
    // participant-owned NoteOn rather than recomputing a packet-level route.
    {
        CrowdTimeField gatedField;
        CrowdTimeField::Config gatedTiming = timing;
        gatedTiming.division = CrowdTimeField::Division::Quarter;

        const int sourceId = 2;
        CrowdTimeField::InputEvent on;
        on.type = CrowdTimeField::InputEvent::Type::On;
        on.sourceId = sourceId;
        on.voiceId = CrowdTimeField::voiceIdFor(sourceId, 0);

        CrowdTimeField::OutputBlock attackOutput;
        gatedField.process(gatedTiming, preciseTimeFrame(1, 0.0),
                           &on, 1, attackOutput);
        const auto* attack = firstScheduled(
            attackOutput, CrowdTimeField::OutputEvent::Type::Attack);

        MpeMidiOutput gatedMidi;
        MpeMidiOutput::NoteEvent midiAttack;
        midiAttack.type = MpeMidiOutput::NoteEvent::NoteOn;
        midiAttack.sourceId = on.voiceId;
        midiAttack.participantId = sourceId;
        midiAttack.frequencyHz = 440.0;
        midiAttack.velocity = 0.8f;
        if (attack != nullptr)
            midiAttack.sampleOffset = attack->sampleOffset;
        juce::MidiBuffer attackMidi;
        gatedMidi.render(midiConfig, &midiAttack, attack != nullptr ? 1 : 0,
                         attackMidi, 1);

        CrowdLfoGate lfo;
        CrowdLfoGate::OutputBlock lfoOutput;
        lfo.process(squareTimeGate(), preciseLfoFrame(100, 0.463), lfoOutput);
        std::array<CrowdTimeField::InputEvent,
                   CrowdLfoGate::kMaxTransitionsPerBlock> gateEvents {};
        const int gateEventCount = translateGateTransitions(lfoOutput, gateEvents);

        CrowdTimeField::OutputBlock releaseOutput;
        gatedField.process(gatedTiming, preciseTimeFrame(100, 0.001),
                           gateEvents.data(), gateEventCount, releaseOutput);
        const auto* release = firstScheduled(
            releaseOutput, CrowdTimeField::OutputEvent::Type::Release);

        MpeMidiOutput::NoteEvent midiRelease;
        midiRelease.type = MpeMidiOutput::NoteEvent::NoteOff;
        midiRelease.sourceId = on.voiceId;
        // Releases are voice-owned: channel lookup must use the NoteOn state,
        // even when no participant hint accompanies the semantic NoteOff.
        midiRelease.participantId = -1;
        if (release != nullptr)
            midiRelease.sampleOffset = release->sampleOffset;
        juce::MidiBuffer releaseMidi;
        gatedMidi.render(midiConfig, &midiRelease, release != nullptr ? 1 : 0,
                         releaseMidi, 100);

        expect(attack != nullptr && hasPhysicalNoteAt(attackMidi, true, 2, 0)
                   && lfoOutput.count == 1
                   && lfoOutput.transitions[0].type
                        == CrowdLfoGate::Transition::Type::Close
                   && lfoOutput.transitions[0].sampleOffset == 37
                   && release != nullptr && release->sampleOffset == 37
                   && release->voiceId == on.voiceId
                   && hasPhysicalNoteAt(releaseMidi, false, 2, 37),
               "LFO Close emits semantic NoteOff on the stored participant channel and sample");
    }

    // Reopening is admission, not a trigger edge. Exercise both timed modes:
    // the held source stays silent at the LFO Open sample and returns only when
    // the next ordinary Time Field tick (and Ensemble lane) arrives.
    for (const auto mode : { CrowdTimeField::Mode::Grid,
                             CrowdTimeField::Mode::Ensemble })
    {
        CrowdTimeField gatedField;
        CrowdTimeField::Config gatedTiming = timing;
        gatedTiming.mode = mode;
        gatedTiming.division = CrowdTimeField::Division::Sixteenth;
        gatedTiming.spreadSlots = 1;

        CrowdTimeField::InputEvent on;
        on.type = CrowdTimeField::InputEvent::Type::On;
        on.sourceId = 9;
        on.voiceId = CrowdTimeField::voiceIdFor(9, 0);
        CrowdTimeField::OutputBlock output;
        gatedField.process(gatedTiming, preciseTimeFrame(1, 0.0), &on, 1, output);

        CrowdLfoGate lfo;
        CrowdLfoGate::OutputBlock lfoOutput;
        std::array<CrowdTimeField::InputEvent,
                   CrowdLfoGate::kMaxTransitionsPerBlock> gateEvents {};

        lfo.process(squareTimeGate(), preciseLfoFrame(100, 0.463), lfoOutput);
        int gateEventCount = translateGateTransitions(lfoOutput, gateEvents);
        gatedField.process(gatedTiming, preciseTimeFrame(100, 0.001),
                           gateEvents.data(), gateEventCount, output);
        const bool closeReleased = firstScheduled(
            output, CrowdTimeField::OutputEvent::Type::Release) != nullptr
                                && output.pendingCount == 1
                                && ! gatedField.isExternalGateOpen();

        // The next absolute LFO block reaches its rising edge at sample 437.
        // Time Field ticks at samples 149 and 399 while still closed, so neither
        // may consume the held intent. No later tick exists in this block.
        lfo.process(squareTimeGate(), preciseLfoFrame(500, 0.563), lfoOutput);
        gateEventCount = translateGateTransitions(lfoOutput, gateEvents);
        gatedField.process(gatedTiming, preciseTimeFrame(500, 0.101),
                           gateEvents.data(), gateEventCount, output);
        const bool openWithoutBurst = gateEventCount == 1
                                   && gateEvents[0].type
                                        == CrowdTimeField::InputEvent::Type::GateOpen
                                   && gateEvents[0].sampleOffset == 437
                                   && firstScheduled(output,
                                        CrowdTimeField::OutputEvent::Type::Attack) == nullptr
                                   && output.pendingCount == 1
                                   && gatedField.isExternalGateOpen();

        // The following block contains the ordinary sixteenth-note boundary at
        // sample 149. This is the first legal re-entry point after Open.
        lfo.process(squareTimeGate(), preciseLfoFrame(200, 1.063), lfoOutput);
        gateEventCount = translateGateTransitions(lfoOutput, gateEvents);
        gatedField.process(gatedTiming, preciseTimeFrame(200, 0.601),
                           gateEvents.data(), gateEventCount, output);
        const auto* reattack = firstScheduled(
            output, CrowdTimeField::OutputEvent::Type::Attack);

        expect(closeReleased && openWithoutBurst && gateEventCount == 0
                   && reattack != nullptr && reattack->sampleOffset == 149
                   && reattack->voiceId == on.voiceId,
               mode == CrowdTimeField::Mode::Grid
                   ? "Grid LFO Open waits for the next ordinary grid tick"
                   : "Ensemble LFO Open waits for the next ordinary lane tick");
    }

    // A complete touch that begins and ends during the closed window is
    // consumed as lifecycle state but must never be replayed after LFO Open.
    {
        CrowdTimeField gatedField;
        CrowdTimeField::Config gatedTiming = timing;
        gatedTiming.division = CrowdTimeField::Division::Quarter;
        CrowdTimeField::OutputBlock output;
        gatedField.process(gatedTiming, preciseTimeFrame(1, 0.0),
                           nullptr, 0, output);

        CrowdLfoGate lfo;
        CrowdLfoGate::OutputBlock lfoOutput;
        std::array<CrowdTimeField::InputEvent,
                   CrowdLfoGate::kMaxTransitionsPerBlock> gateEvents {};
        lfo.process(squareTimeGate(), preciseLfoFrame(100, 0.463), lfoOutput);
        int gateEventCount = translateGateTransitions(lfoOutput, gateEvents);
        gatedField.process(gatedTiming, preciseTimeFrame(100, 0.001),
                           gateEvents.data(), gateEventCount, output);

        std::array<CrowdTimeField::InputEvent, 2> tap {};
        tap[0].type = CrowdTimeField::InputEvent::Type::On;
        tap[0].sourceId = 12;
        tap[0].voiceId = CrowdTimeField::voiceIdFor(12, 0);
        tap[0].sampleOffset = 10;
        tap[1].type = CrowdTimeField::InputEvent::Type::Off;
        tap[1].sourceId = 12;
        tap[1].voiceId = CrowdTimeField::voiceIdFor(12, 0);
        tap[1].sampleOffset = 20;
        gatedField.process(gatedTiming, preciseTimeFrame(100, 0.101),
                           tap.data(), static_cast<int>(tap.size()), output);
        const bool closedTapConsumed = output.count == 0
                                    && output.pendingCount == 0
                                    && output.activeCount == 0;

        // Advance the same LFO timeline to Open, then across subsequent ticks.
        lfo.process(squareTimeGate(), preciseLfoFrame(100, 0.563), lfoOutput);
        gateEventCount = translateGateTransitions(lfoOutput, gateEvents);
        gatedField.process(gatedTiming, preciseTimeFrame(100, 0.201),
                           gateEvents.data(), gateEventCount, output);
        lfo.process(squareTimeGate(), preciseLfoFrame(400, 0.663), lfoOutput);
        gateEventCount = translateGateTransitions(lfoOutput, gateEvents);
        gatedField.process(gatedTiming, preciseTimeFrame(400, 0.301),
                           gateEvents.data(), gateEventCount, output);
        const bool openStayedSilent = gatedField.isExternalGateOpen()
                                   && output.pendingCount == 0
                                   && output.activeCount == 0
                                   && firstScheduled(output,
                                        CrowdTimeField::OutputEvent::Type::Attack) == nullptr;

        lfo.process(squareTimeGate(), preciseLfoFrame(300, 1.063), lfoOutput);
        gateEventCount = translateGateTransitions(lfoOutput, gateEvents);
        gatedField.process(gatedTiming, preciseTimeFrame(300, 0.701),
                           gateEvents.data(), gateEventCount, output);
        expect(closedTapConsumed && openStayedSilent
                   && firstScheduled(output,
                        CrowdTimeField::OutputEvent::Type::Attack) == nullptr
                   && output.pendingCount == 0 && output.activeCount == 0,
               "closed-window short On/Off is never replayed as a ghost note");
    }

    std::cout << "\nSummary: " << (failed == 0 ? "ok" : "failed") << "\n";
    return failed == 0 ? 0 : 1;
}
