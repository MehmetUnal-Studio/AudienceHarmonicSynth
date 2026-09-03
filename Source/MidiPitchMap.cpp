#include "MidiPitchMap.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace
{
    struct ScaleDefinition
    {
        const char* name;
        std::array<int, 7> degrees;
        int count;
    };

    constexpr std::array<ScaleDefinition, MidiPitchMap::numScaleModes> scaleDefinitions { {
        { "Major",          { 0, 2, 4, 5, 7, 9, 11 }, 7 },
        { "Natural Minor",  { 0, 2, 3, 5, 7, 8, 10 }, 7 },
        { "Pentatonic",     { 0, 2, 4, 7, 9, 0,  0  }, 5 },
        { "Dorian",         { 0, 2, 3, 5, 7, 9, 10 }, 7 },
        { "Lydian",         { 0, 2, 4, 6, 7, 9, 11 }, 7 },
        { "Harmonic Minor", { 0, 2, 3, 5, 7, 8, 11 }, 7 },
        { "Whole Tone",     { 0, 2, 4, 6, 8, 10, 0  }, 6 }
    } };

    int clampScaleMode (int scaleMode) noexcept
    {
        return std::clamp(scaleMode, 0, MidiPitchMap::numScaleModes - 1);
    }

    double midiToFrequency (int midiNote) noexcept
    {
        return 440.0 * std::pow(2.0, ((double) midiNote - 69.0) / 12.0);
    }
}

MidiPitchMap::MidiPitchMap() noexcept
{
    configure(0, 2, (int) ScaleMode::Major, 4);
}

void MidiPitchMap::configure (int rootPitchClass, int rootOctave,
                              int scaleMode, int octaveCount) noexcept
{
    configuredRootPitchClass = std::clamp(rootPitchClass, 0, 11);
    configuredRootOctave = std::clamp(rootOctave, minRootOctave, maxRootOctave);
    configuredRootMidi = rootMidiFor(configuredRootPitchClass, configuredRootOctave);
    configuredScaleMode = clampScaleMode(scaleMode);
    configuredOctaves = std::clamp(octaveCount, minOctaves, maxOctaves);

    const auto& scale = scaleDefinitions[(size_t) configuredScaleMode];
    tableSize = 0;

    for (int octave = 0; octave < configuredOctaves; ++octave)
    {
        for (int degree = 0; degree < scale.count; ++degree)
        {
            const int midiNote = configuredRootMidi
                               + octave * 12
                               + scale.degrees[(size_t) degree];

            // A high root plus several octaves may exceed MIDI's 7-bit note
            // range. Stop rather than filling the tail with duplicate note 127.
            if (midiNote > 127 || tableSize >= maxTableSize)
                return;

            midiTable[(size_t) tableSize] = midiNote;
            frequencyTable[(size_t) tableSize] = midiToFrequency(midiNote);
            ++tableSize;
        }
    }
}

int MidiPitchMap::getScaleStepsPerOctave() const noexcept
{
    return scaleDefinitions[(size_t) configuredScaleMode].count;
}

int MidiPitchMap::getScaleMidi (int step) const noexcept
{
    return step >= 0 && step < tableSize ? midiTable[(size_t) step] : -1;
}

double MidiPitchMap::getScaleFrequencyHz (int step) const noexcept
{
    return step >= 0 && step < tableSize ? frequencyTable[(size_t) step] : 0.0;
}

MidiPitchMap::Pitch MidiPitchMap::getPitch (int step) const noexcept
{
    if (step < 0 || step >= tableSize)
        return {};

    return { step, midiTable[(size_t) step], frequencyTable[(size_t) step] };
}

int MidiPitchMap::xToStep (float normalisedX) const noexcept
{
    if (tableSize <= 0)
        return -1;

    if (std::isnan(normalisedX) || normalisedX <= 0.0f)
        return 0;

    if (normalisedX >= 1.0f)
        return tableSize - 1;

    return std::min(tableSize - 1, (int) (normalisedX * (float) tableSize));
}

int MidiPitchMap::xToMidi (float normalisedX) const noexcept
{
    return getScaleMidi(xToStep(normalisedX));
}

double MidiPitchMap::xToFrequencyHz (float normalisedX) const noexcept
{
    return getScaleFrequencyHz(xToStep(normalisedX));
}

MidiPitchMap::Pitch MidiPitchMap::xToPitch (float normalisedX) const noexcept
{
    return getPitch(xToStep(normalisedX));
}

int MidiPitchMap::findNearestScaleStepForMidi (int midiNote) const noexcept
{
    if (tableSize <= 0)
        return -1;

    const int target = std::clamp(midiNote, 0, 127);
    int bestStep = 0;
    int bestDistance = std::abs(midiTable[0] - target);

    for (int step = 1; step < tableSize; ++step)
    {
        const int distance = std::abs(midiTable[(size_t) step] - target);
        if (distance < bestDistance)
        {
            bestStep = step;
            bestDistance = distance;
        }
    }

    return bestStep;
}

int MidiPitchMap::rootMidiFor (int rootPitchClass, int rootOctave) noexcept
{
    const int pitchClass = std::clamp(rootPitchClass, 0, 11);
    const long long midi = ((long long) rootOctave + 1LL) * 12LL + (long long) pitchClass;
    return (int) std::clamp(midi, 0LL, 127LL);
}

const char* MidiPitchMap::scaleName (int scaleMode) noexcept
{
    return scaleDefinitions[(size_t) clampScaleMode(scaleMode)].name;
}
