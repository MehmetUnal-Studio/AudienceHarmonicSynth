#include "../Source/CrowdTimeField.h"
#include "../Source/MpeMidiOutput.h"

#include <array>
#include <cmath>
#include <iostream>

namespace
{
    int failed = 0;

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

    std::cout << "\nSummary: " << (failed == 0 ? "ok" : "failed") << "\n";
    return failed == 0 ? 0 : 1;
}
