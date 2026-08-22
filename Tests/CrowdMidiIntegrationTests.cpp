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

    std::cout << "\nSummary: " << (failed == 0 ? "ok" : "failed") << "\n";
    return failed == 0 ? 0 : 1;
}
