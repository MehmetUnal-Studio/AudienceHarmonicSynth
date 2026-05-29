#include "../Source/MidiScaleModule.h"

#include <iostream>
#include <vector>

namespace
{
    struct Event
    {
        enum class Type { NoteOn, NoteOff, CC };
        Type type = Type::NoteOn;
        int channel = 1;
        int noteOrCc = 0;
        int value = 0;
        int sample = 0;
    };

    std::vector<Event> collect (const juce::MidiBuffer& midi)
    {
        std::vector<Event> events;
        for (const auto metadata : midi)
        {
            const auto msg = metadata.getMessage();
            if (msg.isNoteOn(false))
                events.push_back({ Event::Type::NoteOn, msg.getChannel(), msg.getNoteNumber(),
                                   msg.getVelocity(), metadata.samplePosition });
            else if (msg.isNoteOff(true))
                events.push_back({ Event::Type::NoteOff, msg.getChannel(), msg.getNoteNumber(),
                                   0, metadata.samplePosition });
            else if (msg.isController())
                events.push_back({ Event::Type::CC, msg.getChannel(), msg.getControllerNumber(),
                                   msg.getControllerValue(), metadata.samplePosition });
        }
        return events;
    }

    void expect (bool ok, const char* name, int& failed)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << name << "\n";
        if (! ok)
            ++failed;
    }

    void processForTest (MidiScaleModule& module, juce::MidiBuffer& midi)
    {
        juce::MidiBuffer output;
        module.process(midi, output);
        midi.swapWith(output);
    }
}

int main()
{
    int failed = 0;

    for (const int note : { 60, 62, 64, 65, 67, 69, 71 })
        expect(MidiScaleModule::mapNoteToScale(note, 0, MidiScaleModule::ScaleType::Major,
                                               MidiScaleModule::CorrectionMode::Nearest) == note,
               "C Major leaves in-scale notes unchanged", failed);

    expect(MidiScaleModule::mapNoteToScale(61, 0, MidiScaleModule::ScaleType::Major,
                                           MidiScaleModule::CorrectionMode::Nearest) == 60,
           "C Major nearest maps C# down on a tie", failed);
    expect(MidiScaleModule::mapNoteToScale(61, 0, MidiScaleModule::ScaleType::Major,
                                           MidiScaleModule::CorrectionMode::Up) == 62,
           "C Major up maps C# to D", failed);
    expect(MidiScaleModule::mapNoteToScale(61, 0, MidiScaleModule::ScaleType::Major,
                                           MidiScaleModule::CorrectionMode::Down) == 60,
           "C Major down maps C# to C", failed);

    for (const int note : { 57, 59, 60, 62, 64, 65, 67 })
        expect(MidiScaleModule::mapNoteToScale(note, 9, MidiScaleModule::ScaleType::NaturalMinor,
                                               MidiScaleModule::CorrectionMode::Nearest) == note,
               "A Natural Minor accepts A B C D E F G", failed);

    bool chromaticOk = true;
    for (int note = 0; note < 128; ++note)
        chromaticOk = chromaticOk
                   && MidiScaleModule::mapNoteToScale(note, 4, MidiScaleModule::ScaleType::Chromatic,
                                                      MidiScaleModule::CorrectionMode::Nearest) == note;
    expect(chromaticOk, "Chromatic leaves every MIDI note unchanged", failed);

    MidiScaleModule module;
    std::array<int, 12> identityRemap { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    module.setConfig(true, 0, MidiScaleModule::ScaleType::Major,
                     MidiScaleModule::CorrectionMode::Nearest, MidiScaleModule::defaultCustomMask(),
                     identityRemap, 0);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(2, 61, (juce::uint8) 100), 12);
    processForTest(module, midi);
    auto events = collect(midi);
    expect(events.size() == 1
        && events[0].type == Event::Type::NoteOn
        && events[0].channel == 2
        && events[0].noteOrCc == 60
        && events[0].value == 100
        && events[0].sample == 12,
        "Note On is scaled while preserving channel, velocity and sample position", failed);

    module.setConfig(true, 0, MidiScaleModule::ScaleType::Major,
                     MidiScaleModule::CorrectionMode::Up, MidiScaleModule::defaultCustomMask(),
                     identityRemap, 0);
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(2, 61), 48);
    processForTest(module, midi);
    events = collect(midi);
    expect(events.size() == 1
        && events[0].type == Event::Type::NoteOff
        && events[0].channel == 2
        && events[0].noteOrCc == 60
        && events[0].sample == 48,
        "Note Off uses the exact mapped note from its Note On", failed);

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(3, 61, (juce::uint8) 80), 0);
    midi.addEvent(juce::MidiMessage::controllerEvent(3, 74, 99), 4);
    module.setConfig(false, 0, MidiScaleModule::ScaleType::Major,
                     MidiScaleModule::CorrectionMode::Nearest, MidiScaleModule::defaultCustomMask(),
                     identityRemap, 0);
    processForTest(module, midi);
    events = collect(midi);
    expect(events.size() == 2
        && events[0].type == Event::Type::NoteOn
        && events[0].channel == 3
        && events[0].noteOrCc == 61
        && events[0].value == 80
        && events[1].type == Event::Type::CC
        && events[1].noteOrCc == 74
        && events[1].value == 99,
        "Bypass returns MIDI notes unchanged and preserves non-note events", failed);

    expect(MidiScaleModule::mapNoteToScale(127, 0, MidiScaleModule::ScaleType::Locrian,
                                           MidiScaleModule::CorrectionMode::Up) == 127,
           "Mapped notes are clamped to the valid MIDI range", failed);

    module.reset();
    auto remap = identityRemap;
    remap[0] = 7;
    module.setConfig(true, 0, MidiScaleModule::ScaleType::Chromatic,
                     MidiScaleModule::CorrectionMode::Nearest, MidiScaleModule::defaultCustomMask(),
                     remap, 0);
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 90), 0);
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 20);
    processForTest(module, midi);
    events = collect(midi);
    expect(events.size() == 2
        && events[0].noteOrCc == 67
        && events[1].noteOrCc == 67,
        "Remap matrix shifts C-class notes to G-class and preserves Note Off mapping", failed);

    std::cout << "\nSummary: " << (failed == 0 ? "ok" : "failed") << "\n";
    return failed == 0 ? 0 : 1;
}
