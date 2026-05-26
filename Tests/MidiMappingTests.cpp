#include "../Source/MidiEngine.h"

#include <iostream>
#include <vector>

namespace
{
    struct Event
    {
        enum class Type { NoteOn, NoteOff, CC, AllNotesOff };
        Type type = Type::NoteOn;
        int channel = 1;
        int data1 = -1;
        int data2 = 0;
    };

    std::vector<Event> collect (juce::MidiBuffer& midi)
    {
        std::vector<Event> events;
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn())
                events.push_back({ Event::Type::NoteOn, msg.getChannel(), msg.getNoteNumber(), msg.getVelocity() });
            else if (msg.isNoteOff())
                events.push_back({ Event::Type::NoteOff, msg.getChannel(), msg.getNoteNumber(), 0 });
            else if (msg.isController())
            {
                if (msg.getControllerNumber() == 123 || msg.getControllerNumber() == 120)
                    events.push_back({ Event::Type::AllNotesOff, msg.getChannel(), msg.getControllerNumber(), 0 });
                else
                    events.push_back({ Event::Type::CC, msg.getChannel(),
                                       msg.getControllerNumber(), msg.getControllerValue() });
            }
        }
        midi.clear();
        return events;
    }

    std::vector<Event> notesOnly (const std::vector<Event>& events)
    {
        std::vector<Event> notes;
        for (const auto& e : events)
            if (e.type == Event::Type::NoteOn || e.type == Event::Type::NoteOff)
                notes.push_back(e);
        return notes;
    }

    int countType (const std::vector<Event>& events, Event::Type type)
    {
        int count = 0;
        for (const auto& e : events)
            if (e.type == type)
                ++count;
        return count;
    }

    bool hasCc (const std::vector<Event>& events, int cc)
    {
        for (const auto& e : events)
            if (e.type == Event::Type::CC && e.data1 == cc)
                return true;
        return false;
    }

    bool allNotesAndCcsUseChannel (const std::vector<Event>& events, int channel)
    {
        for (const auto& e : events)
        {
            if (e.type == Event::Type::AllNotesOff)
                continue;

            if (e.channel != channel)
                return false;
        }
        return true;
    }

    void expect (bool ok, const char* name, int& failed)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << name << "\n";
        if (! ok)
            ++failed;
    }

    void configureBasicMajor (MidiEngine& engine)
    {
        engine.prepare(44100.0, 256);
        engine.lowestMidi.store(36);
        engine.rangeOctaves.store(1);
        engine.rangeLowOctave.store(0);
        engine.rangeHighOctave.store(1);
        engine.scaleMode.store(0);
        engine.retriggerMs.store(0.0f);
        engine.energyMacro.store(1.0f);
    }
}

int main()
{
    int failed = 0;
    MidiEngine engine;
    juce::MidiBuffer midi;

    configureBasicMajor(engine);
    engine.setX(0, 0, 0.0f);
    engine.setY(0, 0, 1.0f);
    engine.setOn(0, 0, true);
    engine.renderMidi(midi, 256);
    auto events = notesOnly(collect(midi));
    expect(events.size() == 1 && events[0].type == Event::Type::NoteOn
        && events[0].channel == 1 && events[0].data1 == 36 && events[0].data2 == 127,
        "X=0 maps to root note and high Y maps to high velocity", failed);

    {
        struct AudienceScaleExpectation
        {
            int audienceScale = 0;
            int highestMidi = 47;
            const char* name = "";
        };

        const AudienceScaleExpectation expectations[] {
            { 0, 47, "Major" },
            { 1, 46, "Natural Minor" },
            { 2, 45, "Pentatonic" },
            { 3, 46, "Dorian" },
            { 4, 47, "Lydian" },
            { 5, 47, "Harmonic Minor" },
            { 6, 46, "Whole Tone" }
        };

        for (const auto& e : expectations)
        {
            configureBasicMajor(engine);
            engine.scaleMode.store(MidiEngine::scaleModeForAudienceScaleIndex(e.audienceScale));
            engine.setX(0, 0, 0.999f);
            engine.setY(0, 0, 1.0f);
            engine.setOn(0, 0, true);
            engine.renderMidi(midi, 256);
            events = notesOnly(collect(midi));
            expect(events.size() == 1 && events[0].data1 == e.highestMidi,
                   e.name,
                   failed);
        }
    }

    configureBasicMajor(engine);
    engine.setX(0, 0, 0.0f);
    engine.setY(0, 0, 1.0f);
    engine.setOn(0, 0, true);
    engine.renderMidi(midi, 256);
    collect(midi);

    engine.setX(0, 0, 0.03f);
    engine.renderMidi(midi, 256);
    events = notesOnly(collect(midi));
    expect(events.empty(), "same note region does not spam Note On", failed);

    engine.setX(0, 0, 0.999f);
    engine.renderMidi(midi, 256);
    events = notesOnly(collect(midi));
    expect(events.size() == 2
        && events[0].type == Event::Type::NoteOff && events[0].data1 == 36
        && events[1].type == Event::Type::NoteOn  && events[1].data1 == 47,
        "crossing scale region turns old note off and starts new quantized note", failed);

    configureBasicMajor(engine);
    engine.rangeLowOctave.store(2);
    engine.rangeHighOctave.store(4);
    engine.setX(0, 0, 0.0f);
    engine.setOn(0, 0, true);
    engine.renderMidi(midi, 256);
    events = notesOnly(collect(midi));
    expect(events.size() == 1 && events[0].data1 == 60,
        "range low octave shifts the scale floor", failed);

    engine.setOn(0, 0, false);
    engine.renderMidi(midi, 256);
    events = notesOnly(collect(midi));
    expect(events.size() == 1 && events[0].type == Event::Type::NoteOff && events[0].data1 == 60,
        "participant removal sends Note Off", failed);

    configureBasicMajor(engine);
    engine.channelMode.store((int) MidiEngine::MidiChannelMode::PerRow);
    engine.setX(4, 0, 0.0f);
    engine.setOn(4, 0, true);
    engine.renderMidi(midi, 256);
    events = notesOnly(collect(midi));
    expect(events.size() == 1 && events[0].channel == 5,
        "Per Row channel mode maps row to MIDI channel", failed);

    configureBasicMajor(engine);
    engine.setX(0, 0, 0.0f);
    engine.setY(0, 0, 0.0f);
    engine.setOn(0, 0, true);
    engine.renderMidi(midi, 256);
    const auto lowVelocity = notesOnly(collect(midi))[0].data2;
    engine.clearAllSeats();
    engine.renderMidi(midi, 256);
    collect(midi);
    engine.setX(0, 0, 0.0f);
    engine.setY(0, 0, 1.0f);
    engine.setOn(0, 0, true);
    engine.renderMidi(midi, 256);
    const auto highVelocity = notesOnly(collect(midi))[0].data2;
    expect(lowVelocity < highVelocity && lowVelocity >= 1 && highVelocity <= 127,
        "Y position maps to MIDI velocity", failed);

    configureBasicMajor(engine);
    engine.setX(0, 0, 0.0f);
    engine.setY(0, 0, 0.75f);
    engine.setOn(0, 0, true);
    engine.renderMidi(midi, 256);
    auto all = collect(midi);
    expect(hasCc(all, 1) && hasCc(all, 11),
        "participant movement/Y mappings emit rate-limited CC1 and CC11", failed);

    configureBasicMajor(engine);
    engine.setX(0, 0, 0.0f);
    engine.setOn(0, 0, true);
    engine.renderMidi(midi, 256);
    collect(midi);
    engine.lowestMidi.store(37);
    engine.requestRetuneActiveNotes();
    engine.renderMidi(midi, 256);
    all = collect(midi);
    events = notesOnly(all);
    expect(countType(all, Event::Type::AllNotesOff) >= 16
        && ! events.empty()
        && events.back().type == Event::Type::NoteOn
        && events.back().data1 == 37,
        "root/scale changes send All Notes Off and retrigger active seats", failed);

    configureBasicMajor(engine);
    engine.setX(0, 0, 0.0f);
    engine.setOn(0, 0, true);
    engine.renderMidi(midi, 256);
    collect(midi);
    engine.clearAllSeats();
    engine.renderMidi(midi, 256);
    all = collect(midi);
    expect(countType(all, Event::Type::AllNotesOff) >= 16,
        "Clear All sends All Notes Off on MIDI channels", failed);

    configureBasicMajor(engine);
    engine.channelMode.store((int) MidiEngine::MidiChannelMode::PerRow);
    engine.setX(11, 0, 0.5f);
    engine.setY(11, 0, 0.5f);
    engine.setOn(11, 0, true);
    engine.renderMidi(midi, 256);
    collect(midi);
    engine.channel.store(16);
    engine.channelMode.store((int) MidiEngine::MidiChannelMode::Single);
    engine.requestRetuneActiveNotes();
    engine.renderMidi(midi, 256);
    all = collect(midi);
    events = notesOnly(all);
    expect(! events.empty() && events.back().type == Event::Type::NoteOn && events.back().channel == 16,
        "switching to Single Channel retunes active notes onto the base channel", failed);

    engine.renderMidi(midi, 2048);
    collect(midi);
    engine.setY(11, 0, 0.9f);
    engine.renderMidi(midi, 256);
    all = collect(midi);
    expect(! all.empty() && allNotesAndCcsUseChannel(all, 16),
        "Single Channel sends participant CCs only on the base channel", failed);

    MidiEngine firstInstance;
    MidiEngine secondInstance;
    juce::MidiBuffer firstMidi;
    juce::MidiBuffer secondMidi;
    configureBasicMajor(firstInstance);
    configureBasicMajor(secondInstance);
    firstInstance.channel.store(1);
    secondInstance.channel.store(2);
    firstInstance.setX(0, 0, 0.25f);
    firstInstance.setY(0, 0, 1.0f);
    firstInstance.setOn(0, 0, true);
    secondInstance.setX(0, 0, 0.25f);
    secondInstance.setY(0, 0, 1.0f);
    secondInstance.setOn(0, 0, true);
    firstInstance.renderMidi(firstMidi, 256);
    secondInstance.renderMidi(secondMidi, 256);
    const auto firstEvents = notesOnly(collect(firstMidi));
    const auto secondEvents = notesOnly(collect(secondMidi));
    expect(firstEvents.size() == 1 && secondEvents.size() == 1
        && firstEvents[0].channel == 1 && secondEvents[0].channel == 2
        && firstEvents[0].data1 == secondEvents[0].data1,
        "two generator instances keep independent Single Channel outputs", failed);

    configureBasicMajor(engine);
    engine.ccEnabled.store(false);
    engine.setX(0, 0, 0.0f);
    engine.setY(0, 0, 0.9f);
    engine.setOn(0, 0, true);
    engine.renderMidi(midi, 256);
    all = collect(midi);
    expect(countType(all, Event::Type::CC) == 0 && countType(all, Event::Type::NoteOn) == 1,
        "CC output can be disabled for simple note/velocity/channel devices", failed);

    std::cout << "\nSummary: " << (failed == 0 ? "ok" : "failed") << "\n";
    return failed == 0 ? 0 : 1;
}
