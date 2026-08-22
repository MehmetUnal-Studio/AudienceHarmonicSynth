#include "../Source/AdaptiveCrowdGovernor.h"
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

    int countOutputType (const CrowdTimeField::OutputBlock& output,
                         CrowdTimeField::OutputEvent::Type type)
    {
        int count = 0;
        for (int index = 0; index < output.count; ++index)
            count += output.events[(size_t) index].type == type ? 1 : 0;
        return count;
    }

    int makeMidiEvents (
        const CrowdTimeField::OutputBlock& scheduled,
        std::array<MpeMidiOutput::NoteEvent,
                   CrowdTimeField::kMaxOutputEvents>& midiEvents)
    {
        int count = 0;
        for (int index = 0; index < scheduled.count; ++index)
        {
            const auto& source = scheduled.events[(size_t) index];
            auto& destination = midiEvents[(size_t) count++];
            destination.sourceId = source.voiceId;
            destination.participantId = source.sourceId;
            destination.sampleOffset = source.sampleOffset;
            destination.frequencyHz = frequencyForSource(source.sourceId);
            destination.velocity = 0.8f;
            destination.x = 0.5f;
            destination.y = 0.8f;

            switch (source.type)
            {
                case CrowdTimeField::OutputEvent::Type::Attack:
                    destination.type = MpeMidiOutput::NoteEvent::NoteOn;
                    break;
                case CrowdTimeField::OutputEvent::Type::Release:
                    destination.type = MpeMidiOutput::NoteEvent::NoteOff;
                    break;
                case CrowdTimeField::OutputEvent::Type::SampleMotion:
                    destination.type = MpeMidiOutput::NoteEvent::Expression;
                    break;
            }
        }
        return count;
    }

    CrowdTimeField::ClockFrame integrationFrame (double ppq, int numSamples)
    {
        auto frame = frameAt(ppq);
        frame.sampleRate = 1000.0;
        frame.numSamples = numSamples;
        frame.monotonicSeconds = ppq * 0.5;
        return frame;
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

    // A large audience selects the densest policy, while the effective active
    // cap remains bounded by the available output-channel pool. Feed that exact
    // recommendation into Grid to prove that it governs admissions rather than
    // merely updating telemetry.
    AdaptiveCrowdGovernor highCrowdGovernor;
    AdaptiveCrowdGovernor::Config highCrowdGovernorConfig;
    highCrowdGovernorConfig.voiceLimit = 15;
    highCrowdGovernorConfig.promotionHoldSeconds = 0.0;
    const auto highCrowd = highCrowdGovernor.update(
        highCrowdGovernorConfig, { 200, 250, 0.0 });
    expect(highCrowd.band == 4
               && highCrowd.maxAttacksPerStep == 2
               && highCrowd.spreadSlots == 16
               && highCrowd.maxActive == 15,
           "high crowd selects the densest profile within the MIDI voice cap");

    {
        CrowdTimeField governedField;
        auto governedTiming = timing;
        governedTiming.maxAttacksPerStep = highCrowd.maxAttacksPerStep;
        governedTiming.maxActive = highCrowd.maxActive;
        governedTiming.spreadSlots = highCrowd.spreadSlots;

        std::array<CrowdTimeField::InputEvent, 16> crowdOns {};
        for (int source = 1; source <= (int) crowdOns.size(); ++source)
        {
            auto& event = crowdOns[(size_t) (source - 1)];
            event.type = CrowdTimeField::InputEvent::Type::On;
            event.sourceId = source;
            event.voiceId = CrowdTimeField::voiceIdFor(source, 0);
        }

        CrowdTimeField::OutputBlock governedOutput;
        governedField.process(governedTiming, frameAt(0.24), crowdOns.data(),
                              (int) crowdOns.size(), governedOutput);
        expect(! governedOutput.resetRequested
                   && countOutputType(governedOutput,
                                      CrowdTimeField::OutputEvent::Type::Attack) == 2
                   && governedOutput.activeCount == 2
                   && governedOutput.pendingCount == 14,
               "high-crowd attack recommendation controls real Grid admission");
    }

    // Exercise the complete Governor -> Time Field -> physical MIDI lifecycle.
    // Start four owned channels under a dense policy, then let the Governor
    // lower all three admission controls. Existing ownership must survive; a
    // fifth participant waits until the population falls below the soft cap.
    {
        AdaptiveCrowdGovernor governor;
        AdaptiveCrowdGovernor::Config governorConfig;
        governorConfig.voiceLimit = 4;
        governorConfig.riseSeconds = 0.05;
        governorConfig.fallSeconds = 0.05;
        governorConfig.promotionHoldSeconds = 0.0;
        governorConfig.demotionHoldSeconds = 0.0;
        governorConfig.demotionHysteresis = 0.0;
        const auto dense = governor.update(governorConfig, { 200, 200, 0.0 });

        CrowdTimeField governedField;
        auto governedTiming = timing;
        governedTiming.maxAttacksPerStep = dense.maxAttacksPerStep;
        governedTiming.maxActive = dense.maxActive;
        governedTiming.spreadSlots = dense.spreadSlots;

        std::array<CrowdTimeField::InputEvent, 4> initialOns {};
        for (int source = 1; source <= (int) initialOns.size(); ++source)
        {
            auto& event = initialOns[(size_t) (source - 1)];
            event.type = CrowdTimeField::InputEvent::Type::On;
            event.sourceId = source;
            event.voiceId = CrowdTimeField::voiceIdFor(source, 0);
        }

        CrowdTimeField::OutputBlock scheduled;
        governedField.process(governedTiming, integrationFrame(0.0, 126),
                              initialOns.data(), (int) initialOns.size(), scheduled);
        const bool fourStarted = ! scheduled.resetRequested
                              && countOutputType(
                                     scheduled,
                                     CrowdTimeField::OutputEvent::Type::Attack) == 4
                              && scheduled.activeCount == 4;

        MpeMidiOutput governedMidi;
        MpeMidiOutput::MpeConfig governedMidiConfig;
        governedMidiConfig.outputType = 1;
        governedMidiConfig.normalRoutingMode = 1;
        std::array<MpeMidiOutput::NoteEvent,
                   CrowdTimeField::kMaxOutputEvents> midiEvents {};
        const int initialMidiCount = makeMidiEvents(scheduled, midiEvents);
        juce::MidiBuffer initialMidi;
        governedMidi.render(governedMidiConfig, midiEvents.data(),
                            initialMidiCount, initialMidi, 126);
        expect(fourStarted
                   && hasPhysicalNote(initialMidi, true, 1)
                   && hasPhysicalNote(initialMidi, true, 2)
                   && hasPhysicalNote(initialMidi, true, 3)
                   && hasPhysicalNote(initialMidi, true, 4),
               "dense policy creates four stable participant-channel owners");

        governorConfig.voiceLimit = 1;
        const auto quiet = governor.update(governorConfig, { 0, 0, 1.0 });
        governedTiming.maxAttacksPerStep = quiet.maxAttacksPerStep;
        governedTiming.maxActive = quiet.maxActive;
        governedTiming.spreadSlots = quiet.spreadSlots;
        governedField.process(governedTiming, integrationFrame(0.252, 1),
                              nullptr, 0, scheduled);

        juce::MidiBuffer policyMidi;
        const int policyMidiCount = makeMidiEvents(scheduled, midiEvents);
        governedMidi.render(governedMidiConfig, midiEvents.data(),
                            policyMidiCount, policyMidi, 1);
        expect(quiet.band == 0
                   && quiet.maxAttacksPerStep == 4
                   && quiet.spreadSlots == 1
                   && quiet.maxActive == 1
                   && ! scheduled.resetRequested
                   && scheduled.activeCount == 4
                   && countOutputType(
                        scheduled,
                        CrowdTimeField::OutputEvent::Type::Release) == 0
                   && ! hasPhysicalNote(policyMidi, false, 1)
                   && ! hasPhysicalNote(policyMidi, false, 2)
                   && ! hasPhysicalNote(policyMidi, false, 3)
                   && ! hasPhysicalNote(policyMidi, false, 4),
               "lower Governor policy changes are soft and never cut owned channels");

        CrowdTimeField::InputEvent newcomer;
        newcomer.type = CrowdTimeField::InputEvent::Type::On;
        newcomer.sourceId = 5;
        newcomer.voiceId = CrowdTimeField::voiceIdFor(5, 0);
        governedField.process(governedTiming, integrationFrame(0.254, 1),
                              &newcomer, 1, scheduled);
        const bool queuedAboveCap = ! scheduled.resetRequested
                                 && scheduled.activeCount == 4
                                 && scheduled.pendingCount == 1
                                 && countOutputType(
                                      scheduled,
                                      CrowdTimeField::OutputEvent::Type::Attack) == 0;

        auto releaseSource = [&] (int source, double ppq,
                                  juce::MidiBuffer& rendered)
        {
            CrowdTimeField::InputEvent release;
            release.type = CrowdTimeField::InputEvent::Type::Off;
            release.sourceId = source;
            release.voiceId = CrowdTimeField::voiceIdFor(source, 0);
            governedField.process(governedTiming, integrationFrame(ppq, 1),
                                  &release, 1, scheduled);
            const int count = makeMidiEvents(scheduled, midiEvents);
            governedMidi.render(governedMidiConfig, midiEvents.data(), count,
                                rendered, 1);
        };

        juce::MidiBuffer canonicalOffMidi;
        releaseSource(3, 0.256, canonicalOffMidi);
        expect(queuedAboveCap
                   && hasPhysicalNote(canonicalOffMidi, false, 3),
               "canonical Off still releases its stored channel under Governor control");

        juce::MidiBuffer releaseOne;
        juce::MidiBuffer releaseTwo;
        releaseSource(1, 0.258, releaseOne);
        releaseSource(2, 0.260, releaseTwo);
        expect(! scheduled.resetRequested
                   && scheduled.activeCount == 1
                   && scheduled.pendingCount == 1
                   && countOutputType(
                        scheduled,
                        CrowdTimeField::OutputEvent::Type::Attack) == 0,
               "new attack remains pending while active population equals the lowered cap");

        juce::MidiBuffer releaseFour;
        releaseSource(4, 0.262, releaseFour);
        const bool underCap = scheduled.activeCount == 0
                           && scheduled.pendingCount == 1;
        governedField.process(governedTiming, integrationFrame(0.264, 119),
                              nullptr, 0, scheduled);
        juce::MidiBuffer admittedMidi;
        const int admittedCount = makeMidiEvents(scheduled, midiEvents);
        governedMidi.render(governedMidiConfig, midiEvents.data(), admittedCount,
                            admittedMidi, 119);
        expect(underCap && ! scheduled.resetRequested
                   && countOutputType(
                        scheduled,
                        CrowdTimeField::OutputEvent::Type::Attack) == 1
                   && scheduled.activeCount == 1
                   && scheduled.pendingCount == 0
                   && hasPhysicalNote(admittedMidi, true, 5),
               "pending participant enters on its original channel once below cap");
    }

    // Flow is explicitly outside the Governor's timing policy. Even if a caller
    // supplies the densest recommendation, all lifecycle transitions still pass
    // immediately instead of being capped or queued.
    {
        CrowdTimeField flowField;
        auto flowTiming = timing;
        flowTiming.mode = CrowdTimeField::Mode::Flow;
        flowTiming.maxAttacksPerStep = highCrowd.maxAttacksPerStep;
        flowTiming.maxActive = highCrowd.maxActive;
        flowTiming.spreadSlots = highCrowd.spreadSlots;
        std::array<CrowdTimeField::InputEvent, 16> flowOns {};
        for (int source = 1; source <= (int) flowOns.size(); ++source)
        {
            auto& event = flowOns[(size_t) (source - 1)];
            event.type = CrowdTimeField::InputEvent::Type::On;
            event.sourceId = source;
            event.voiceId = CrowdTimeField::voiceIdFor(source, 0);
            event.sampleOffset = source - 1;
        }

        CrowdTimeField::OutputBlock flowOutput;
        flowField.process(flowTiming, frameAt(0.0), flowOns.data(),
                          (int) flowOns.size(), flowOutput);
        expect(! flowOutput.resetRequested
                   && countOutputType(flowOutput,
                                      CrowdTimeField::OutputEvent::Type::Attack) == 16
                   && flowOutput.activeCount == 16
                   && flowOutput.pendingCount == 0,
               "Flow bypasses Governor admission limits and preserves immediate lifecycle");
    }

    std::cout << "\nSummary: " << (failed == 0 ? "ok" : "failed") << "\n";
    return failed == 0 ? 0 : 1;
}
