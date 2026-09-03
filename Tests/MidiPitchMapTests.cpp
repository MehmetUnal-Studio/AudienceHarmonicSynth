#include "../Source/MidiPitchMap.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace
{
    void expect (bool ok, const char* name, int& failed)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << name << "\n";
        if (! ok)
            ++failed;
    }

    template <size_t N>
    bool tableMatches (const MidiPitchMap& map, const std::array<int, N>& expected)
    {
        if (map.getScaleTableSize() != (int) N)
            return false;

        for (size_t i = 0; i < N; ++i)
            if (map.getScaleMidi((int) i) != expected[i])
                return false;

        return true;
    }
}

int main()
{
    int failed = 0;
    MidiPitchMap map;

    map.configure(0, 2, (int) MidiPitchMap::ScaleMode::Major, 1);
    expect(tableMatches(map, std::array<int, 7> { 36, 38, 40, 41, 43, 45, 47 }),
           "Major has the expected scale notes", failed);

    map.configure(0, 2, (int) MidiPitchMap::ScaleMode::NaturalMinor, 1);
    expect(tableMatches(map, std::array<int, 7> { 36, 38, 39, 41, 43, 44, 46 }),
           "Natural Minor has the expected scale notes", failed);

    map.configure(0, 2, (int) MidiPitchMap::ScaleMode::Pentatonic, 1);
    expect(tableMatches(map, std::array<int, 5> { 36, 38, 40, 43, 45 }),
           "Pentatonic has the expected scale notes", failed);

    map.configure(0, 2, (int) MidiPitchMap::ScaleMode::Dorian, 1);
    expect(tableMatches(map, std::array<int, 7> { 36, 38, 39, 41, 43, 45, 46 }),
           "Dorian has the expected scale notes", failed);

    map.configure(0, 2, (int) MidiPitchMap::ScaleMode::Lydian, 1);
    expect(tableMatches(map, std::array<int, 7> { 36, 38, 40, 42, 43, 45, 47 }),
           "Lydian has the expected scale notes", failed);

    map.configure(0, 2, (int) MidiPitchMap::ScaleMode::HarmonicMinor, 1);
    expect(tableMatches(map, std::array<int, 7> { 36, 38, 39, 41, 43, 44, 47 }),
           "Harmonic Minor has the expected scale notes", failed);

    map.configure(0, 2, (int) MidiPitchMap::ScaleMode::WholeTone, 1);
    expect(tableMatches(map, std::array<int, 6> { 36, 38, 40, 42, 44, 46 }),
           "Whole Tone has the expected scale notes", failed);

    map.configure(0, 2, (int) MidiPitchMap::ScaleMode::Major, 1);
    expect(map.xToStep(-10.0f) == 0 && map.xToMidi(-10.0f) == 36,
           "X below zero maps to the first pitch", failed);
    expect(map.xToStep(0.0f) == 0 && map.xToMidi(0.0f) == 36,
           "X zero maps to the first pitch", failed);
    expect(map.xToStep(std::nextafter(1.0f, 0.0f)) == 6
        && map.xToMidi(std::nextafter(1.0f, 0.0f)) == 47
        && std::isfinite(map.xToFrequencyHz(std::nextafter(1.0f, 0.0f))),
           "X immediately below one maps to the last pitch", failed);
    expect(map.xToStep(1.0f) == 6 && map.xToMidi(1.0f) == 47,
           "X one maps safely to the last pitch", failed);
    expect(map.xToStep(10.0f) == 6 && map.xToMidi(10.0f) == 47,
           "X above one maps to the last pitch", failed);
    expect(map.xToStep(std::numeric_limits<float>::quiet_NaN()) == 0,
           "NaN X maps deterministically to the first pitch", failed);

    expect(MidiPitchMap::rootMidiFor(0, 2) == 36
        && MidiPitchMap::rootMidiFor(9, 3) == 57,
        "Pitch class plus octave follows MIDI octave numbering", failed);
    expect(MidiPitchMap::rootMidiFor(-50, -50) == 0
        && MidiPitchMap::rootMidiFor(50, 50) == 127,
        "Root MIDI calculation clamps invalid inputs", failed);

    map.configure(99, 3, 99, 0);
    expect(map.getRootPitchClass() == 11
        && map.getRootMidi() == 59
        && map.getScaleMode() == (int) MidiPitchMap::ScaleMode::WholeTone
        && map.getOctaveCount() == 1,
        "Root, scale mode and low octave count are clamped", failed);

    map.configure(-99, -99, -99, 1);
    expect(map.getRootPitchClass() == 0
        && map.getRootOctave() == MidiPitchMap::minRootOctave
        && map.getRootMidi() == 12
        && map.getScaleMode() == (int) MidiPitchMap::ScaleMode::Major,
        "Low root and scale configuration is clamped", failed);

    map.configure(99, 99, 99, 1);
    expect(map.getRootPitchClass() == 11
        && map.getRootOctave() == MidiPitchMap::maxRootOctave
        && map.getRootMidi() == 95
        && map.getScaleMode() == (int) MidiPitchMap::ScaleMode::WholeTone,
        "High root and scale configuration is clamped", failed);

    map.configure(0, 2, (int) MidiPitchMap::ScaleMode::Major, 99);
    expect(map.getOctaveCount() == MidiPitchMap::maxOctaves
        && map.getScaleTableSize() == 42
        && map.getScaleMidi(41) == 107,
        "Octave count is clamped to six", failed);

    map.configure(11, 6, (int) MidiPitchMap::ScaleMode::Major, 6);
    bool highTableValid = map.getScaleTableSize() > 0
                       && map.getScaleTableSize() <= MidiPitchMap::maxTableSize;
    for (int i = 0; i < map.getScaleTableSize(); ++i)
        highTableValid = highTableValid
                      && map.getScaleMidi(i) >= 0
                      && map.getScaleMidi(i) <= 127
                      && std::isfinite(map.getScaleFrequencyHz(i))
                      && map.getScaleFrequencyHz(i) > 0.0;
    expect(highTableValid, "High roots truncate safely to finite MIDI pitches", failed);

    map.configure(0, 2, (int) MidiPitchMap::ScaleMode::Major, 1);
    expect(map.findNearestScaleStepForMidi(39) == 1
        && map.getScaleMidi(map.findNearestScaleStepForMidi(39)) == 38,
        "Nearest-step ties resolve toward the lower pitch", failed);
    expect(map.getScaleMidi(-1) == -1
        && map.getScaleMidi(map.getScaleTableSize()) == -1
        && map.getScaleFrequencyHz(-1) == 0.0,
        "Out-of-range table access is safe", failed);

    bool allModesFinite = true;
    for (int mode = 0; mode < MidiPitchMap::numScaleModes; ++mode)
    {
        map.configure(7, 4, mode, 6);
        allModesFinite = allModesFinite
                      && map.getScaleTableSize() > 0
                      && map.getScaleTableSize() <= MidiPitchMap::maxTableSize;

        for (int step = 0; step < map.getScaleTableSize(); ++step)
            allModesFinite = allModesFinite
                          && std::isfinite(map.getScaleFrequencyHz(step))
                          && map.getScaleFrequencyHz(step) > 0.0;
    }
    expect(allModesFinite, "All seven modes produce finite fixed-capacity tables", failed);

    std::cout << "\nSummary: " << (failed == 0 ? "ok" : "failed") << "\n";
    return failed == 0 ? 0 : 1;
}
