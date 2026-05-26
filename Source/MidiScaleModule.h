#pragma once

#include <array>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

class MidiScaleModule
{
public:
    static constexpr int numScaleTypes = 16;
    static constexpr int numCorrectionModes = 3;

    enum class ScaleType
    {
        Major = 0,
        NaturalMinor,
        Dorian,
        Phrygian,
        Lydian,
        Mixolydian,
        Locrian,
        HarmonicMinor,
        MelodicMinor,
        MajorPentatonic,
        MinorPentatonic,
        Blues,
        WholeTone,
        Chromatic,
        PhrygianDominant,
        Custom
    };

    enum class CorrectionMode
    {
        Nearest = 0,
        Up,
        Down
    };

    static juce::StringArray rootNames();
    static juce::StringArray scaleNames();
    static juce::StringArray correctionModeNames();
    static int defaultCustomMask() noexcept;
    static int maskForScaleType (ScaleType scaleType, int customMask = defaultCustomMask()) noexcept;
    static int mapNoteToScale (int noteNumber, int rootNote, ScaleType scaleType,
                               CorrectionMode correctionMode, int customMask = defaultCustomMask()) noexcept;

    void reset();
    void setConfig (bool shouldEnable, int rootNote, ScaleType scaleType,
                    CorrectionMode correctionMode, int customMask,
                    const std::array<int, 12>& pitchClassRemap, int semitoneTranspose);
    void process (juce::MidiBuffer& midi);
    void process (const juce::MidiBuffer& input, juce::MidiBuffer& output);

private:
    struct ActiveNoteStack
    {
        std::array<int, 32> mappedNotes {};
        int depth = 0;
    };

    static int normaliseCustomMask (int customMask) noexcept;
    static int maskForScale (ScaleType scaleType, int customMask) noexcept;
    static bool noteIsInMask (int noteNumber, int rootNote, int mask) noexcept;
    int applyRemapAndTranspose (int noteNumber) const noexcept;
    static int activeIndex (int channel, int originalNote) noexcept;

    void pushActiveMapping (int channel, int originalNote, int mappedNote) noexcept;
    int popActiveMapping (int channel, int originalNote) noexcept;
    void clearChannel (int channel) noexcept;

    bool enabled = false;
    int root = 0;
    ScaleType scale = ScaleType::Major;
    CorrectionMode correction = CorrectionMode::Nearest;
    int custom = defaultCustomMask();
    std::array<int, 12> remap { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    int transpose = 0;
    std::array<ActiveNoteStack, 16 * 128> activeNotes;
};
