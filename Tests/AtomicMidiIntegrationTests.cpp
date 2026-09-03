#include "../Source/AtomicScaleCatalog.h"
#include "../Source/MpeMidiOutput.h"

#include <iostream>
#include <limits>
#include <vector>

namespace
{
    int failed = 0;

    void expect (bool condition, const char* name)
    {
        std::cout << (condition ? "PASS  " : "FAIL  ") << name << "\n";
        if (! condition)
            ++failed;
    }

    std::vector<juce::MidiMessage> messages (const juce::MidiBuffer& buffer)
    {
        std::vector<juce::MidiMessage> result;
        for (const auto metadata : buffer)
            result.emplace_back(metadata.data, metadata.numBytes);
        return result;
    }

    bool containsControllerOrBend (const std::vector<juce::MidiMessage>& output)
    {
        for (const auto& message : output)
            if (message.isController()
                || message.isChannelPressure()
                || message.isPitchWheel()
                || message.isAftertouch())
                return true;
        return false;
    }
}

int main()
{
    const auto& catalog = AtomicScaleCatalog::instance();
    auto helium = catalog.getMap(1, 1); // He / Extended
    helium.setRootPitchClassAndOctave(0, 2, 4);

    const auto heliumPitch = helium.xToPitch(0.37f);
    expect(heliumPitch.isValid() && heliumPitch.degreeIndex >= 0
               && heliumPitch.degreeIndex < catalog.getDegreeCount(1, 1),
           "Helium Extended projects a valid catalog degree");

    MpeMidiOutput::NoteEvent event;
    event.type = MpeMidiOutput::NoteEvent::NoteOn;
    event.sourceId = 170;      // source 17 / finger 0 semantic voice
    event.participantId = 17;  // base-1 channel map wraps to channel 1
    event.frequencyHz = heliumPitch.frequencyHz;
    event.velocity = 0.75f;
    event.x = 0.37f;
    event.y = 0.75f;

    MpeMidiOutput normal;
    MpeMidiOutput::MpeConfig normalConfig;
    normalConfig.outputType = 1;
    normalConfig.normalRoutingMode = 1;
    juce::MidiBuffer normalOutput;
    normal.render(normalConfig, &event, 1, normalOutput, 64);
    const auto normalMessages = messages(normalOutput);
    int normalNote = -1;
    int normalChannel = -1;
    for (const auto& message : normalMessages)
    {
        if (message.isNoteOn())
        {
            normalNote = message.getNoteNumber();
            normalChannel = message.getChannel();
        }
    }
    expect(normalNote == heliumPitch.midiNote && normalChannel == 1,
           "Notes Only uses the nearest Atomic note on the participant channel");
    expect(normalMessages.size() == 1 && ! containsControllerOrBend(normalMessages),
           "Atomic Notes Only attack emits no CC, pressure or pitch bend");

    auto iron = catalog.getMap(24, 3); // Fe / Scientific
    iron.setRootPitchClassAndOctave(9, 1, 6);
    const auto ironLow = iron.xToPitch(0.0f);
    const auto ironHigh = iron.xToPitch(1.0f);
    const auto ironNan = iron.xToPitch(std::numeric_limits<float>::quiet_NaN());
    expect(ironLow.isValid() && ironHigh.isValid()
               && ironHigh.step == iron.getScaleTableSize() - 1
               && ironNan.step == ironLow.step,
           "Scientific Atomic endpoints and non-finite X remain bounded");

    event.frequencyHz = iron.xToPitch(0.63f).frequencyHz;
    juce::MidiBuffer switched;
    normal.render(normalConfig, &event, 1, switched, 64);
    const auto switchedMessages = messages(switched);
    bool sawOn = false;
    for (const auto& message : switchedMessages)
    {
        sawOn = sawOn || message.isNoteOn();
    }
    expect(sawOn && switchedMessages.size() == 1
               && normal.getScheduledNoteCount() == 2
               && ! containsControllerOrBend(switchedMessages),
           "Atomic map changes preserve the old fixed tail and start the new note");

    juce::MidiBuffer tailOffs;
    normal.render(normalConfig, nullptr, 0, tailOffs, 2937);
    int noteOffs = 0;
    for (const auto& message : messages(tailOffs))
        noteOffs += message.isNoteOff() ? 1 : 0;
    expect(noteOffs == 2 && normal.getScheduledNoteCount() == 0,
           "Atomic pitch tails close on their independent sample deadlines");

    std::cout << "\nSummary: " << (failed == 0 ? "ok" : "failed") << "\n";
    return failed == 0 ? 0 : 1;
}
