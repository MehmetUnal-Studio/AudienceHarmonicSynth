#pragma once

#include <array>

// Fixed-capacity tonal pitch table used by the MIDI-only Cosmic Microwave
// signal path. Reconfiguration and lookup never allocate memory.
class MidiPitchMap
{
public:
    enum class ScaleMode : int
    {
        Major = 0,
        NaturalMinor,
        Pentatonic,
        Dorian,
        Lydian,
        HarmonicMinor,
        WholeTone
    };

    struct Pitch
    {
        int step = -1;
        int midiNote = -1;
        double frequencyHz = 0.0;

        bool isValid() const noexcept { return step >= 0 && midiNote >= 0; }
    };

    static constexpr int numScaleModes = 7;
    static constexpr int minOctaves = 1;
    static constexpr int maxOctaves = 6;
    static constexpr int minRootOctave = 0;
    static constexpr int maxRootOctave = 6;
    static constexpr int maxTableSize = 128;

    MidiPitchMap() noexcept;

    // rootOctave follows the standard MIDI convention where C-1 is note 0,
    // C0 is note 12 and C2 is note 36. Invalid inputs are clamped.
    void configure (int rootPitchClass, int rootOctave,
                    int scaleMode, int octaveCount) noexcept;

    int getRootPitchClass() const noexcept { return configuredRootPitchClass; }
    int getRootOctave() const noexcept     { return configuredRootOctave; }
    int getRootMidi() const noexcept       { return configuredRootMidi; }
    int getScaleMode() const noexcept      { return configuredScaleMode; }
    int getOctaveCount() const noexcept    { return configuredOctaves; }

    int getScaleTableSize() const noexcept     { return tableSize; }
    int getScaleStepsPerOctave() const noexcept;
    int getScaleMidi (int step) const noexcept;
    double getScaleFrequencyHz (int step) const noexcept;
    Pitch getPitch (int step) const noexcept;

    // The range is divided into equal scale-step regions. Values below zero
    // select the first step and values at or above one select the last step.
    int xToStep (float normalisedX) const noexcept;
    int xToMidi (float normalisedX) const noexcept;
    double xToFrequencyHz (float normalisedX) const noexcept;
    Pitch xToPitch (float normalisedX) const noexcept;

    // Returns the closest table index. Ties resolve toward the lower pitch.
    int findNearestScaleStepForMidi (int midiNote) const noexcept;

    static int rootMidiFor (int rootPitchClass, int rootOctave) noexcept;
    static const char* scaleName (int scaleMode) noexcept;

private:
    std::array<int, maxTableSize> midiTable {};
    std::array<double, maxTableSize> frequencyTable {};
    int tableSize = 0;
    int configuredRootPitchClass = 0;
    int configuredRootOctave = 2;
    int configuredRootMidi = 36;
    int configuredScaleMode = 0;
    int configuredOctaves = 4;
};
