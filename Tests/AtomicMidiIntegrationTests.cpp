#include "../Source/AtomicScaleCatalog.h"
#include "../Source/MpeMidiOutput.h"

#include <cmath>
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

    double midiForFrequency (double frequencyHz)
    {
        return 69.0 + 12.0 * std::log2(frequencyHz / 440.0);
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
           "Normal MIDI uses the nearest Atomic note on the participant channel");

    MpeMidiOutput mpe;
    MpeMidiOutput::MpeConfig mpeConfig;
    mpeConfig.outputType = 2;
    mpeConfig.sendSetupMessages = false;
    mpeConfig.pitchBendRangeChoice = 0; // +/-2 st easily covers nearest-note cents
    juce::MidiBuffer mpeOutput;
    mpe.render(mpeConfig, &event, 1, mpeOutput, 64);
    const auto mpeMessages = messages(mpeOutput);
    int mpeNote = -1;
    int mpeChannel = -1;
    int pitchWheel = -1;
    for (const auto& message : mpeMessages)
    {
        if (message.isNoteOn())
        {
            mpeNote = message.getNoteNumber();
            mpeChannel = message.getChannel();
        }
        if (message.isPitchWheel())
            pitchWheel = message.getPitchWheelValue();
    }
    const double reconstructedMidi = (double) mpeNote
                                   + ((double) pitchWheel - 8192.0) / 8192.0 * 2.0;
    expect(mpeNote >= 0 && mpeChannel >= 2 && pitchWheel >= 0
               && std::abs(reconstructedMidi - midiForFrequency(heliumPitch.frequencyHz))
                    < 0.001,
           "MPE note plus pitch bend reconstructs the exact Atomic frequency");

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
    mpe.render(mpeConfig, &event, 1, switched, 64);
    const auto switchedMessages = messages(switched);
    bool sawOff = false;
    bool sawOn = false;
    for (const auto& message : switchedMessages)
    {
        sawOff = sawOff || message.isNoteOff();
        sawOn = sawOn || message.isNoteOn();
    }
    expect(sawOff && sawOn,
           "Atomic map changes retrigger an owned MPE voice without leaving its old note held");

    std::cout << "\nSummary: " << (failed == 0 ? "ok" : "failed") << "\n";
    return failed == 0 ? 0 : 1;
}
