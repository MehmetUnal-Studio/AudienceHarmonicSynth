#include "MidiScaleModule.h"

#include <cmath>

namespace
{
    constexpr int bit (int pitchClass) noexcept
    {
        return 1 << pitchClass;
    }

    constexpr int maskFromIntervals (std::initializer_list<int> intervals)
    {
        int mask = 0;
        for (const auto interval : intervals)
            mask |= bit(interval);
        return mask;
    }

    constexpr int majorMask          = maskFromIntervals({ 0, 2, 4, 5, 7, 9, 11 });
    constexpr int naturalMinorMask   = maskFromIntervals({ 0, 2, 3, 5, 7, 8, 10 });
    constexpr int harmonicMinorMask  = maskFromIntervals({ 0, 2, 3, 5, 7, 8, 11 });
    constexpr int melodicMinorMask   = maskFromIntervals({ 0, 2, 3, 5, 7, 9, 11 });
    constexpr int majorPentMask      = maskFromIntervals({ 0, 2, 4, 7, 9 });
    constexpr int minorPentMask      = maskFromIntervals({ 0, 3, 5, 7, 10 });
    constexpr int bluesMask          = maskFromIntervals({ 0, 3, 5, 6, 7, 10 });
    constexpr int wholeToneMask      = maskFromIntervals({ 0, 2, 4, 6, 8, 10 });
    constexpr int phrygianDomMask    = maskFromIntervals({ 0, 1, 4, 5, 7, 8, 10 });
    constexpr int dorianMask         = maskFromIntervals({ 0, 2, 3, 5, 7, 9, 10 });
    constexpr int phrygianMask       = maskFromIntervals({ 0, 1, 3, 5, 7, 8, 10 });
    constexpr int lydianMask         = maskFromIntervals({ 0, 2, 4, 6, 7, 9, 11 });
    constexpr int mixolydianMask     = maskFromIntervals({ 0, 2, 4, 5, 7, 9, 10 });
    constexpr int locrianMask        = maskFromIntervals({ 0, 1, 3, 5, 6, 8, 10 });
    constexpr int chromaticMask      = 0x0fff;
}

juce::StringArray MidiScaleModule::rootNames()
{
    return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

juce::StringArray MidiScaleModule::scaleNames()
{
    return { "Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian",
             "Locrian", "Harm. Minor", "Mel. Minor", "Pentatonic", "Min. Pent.",
             "Blues", "Whole Tone", "Chromatic", "Phr. Dom.", "User" };
}

juce::StringArray MidiScaleModule::correctionModeNames()
{
    return { "Nearest", "Up", "Down" };
}

int MidiScaleModule::defaultCustomMask() noexcept
{
    return majorMask;
}

int MidiScaleModule::normaliseCustomMask (int customMask) noexcept
{
    const int masked = customMask & chromaticMask;
    return masked == 0 ? majorMask : masked;
}

int MidiScaleModule::maskForScale (ScaleType scaleType, int customMask) noexcept
{
    switch (scaleType)
    {
        case ScaleType::Major:           return majorMask;
        case ScaleType::NaturalMinor:    return naturalMinorMask;
        case ScaleType::Dorian:          return dorianMask;
        case ScaleType::Phrygian:        return phrygianMask;
        case ScaleType::Lydian:          return lydianMask;
        case ScaleType::Mixolydian:      return mixolydianMask;
        case ScaleType::Locrian:         return locrianMask;
        case ScaleType::HarmonicMinor:   return harmonicMinorMask;
        case ScaleType::MelodicMinor:    return melodicMinorMask;
        case ScaleType::MajorPentatonic: return majorPentMask;
        case ScaleType::MinorPentatonic: return minorPentMask;
        case ScaleType::Blues:           return bluesMask;
        case ScaleType::WholeTone:       return wholeToneMask;
        case ScaleType::Chromatic:       return chromaticMask;
        case ScaleType::PhrygianDominant:return phrygianDomMask;
        case ScaleType::Custom:          return normaliseCustomMask(customMask);
    }

    return majorMask;
}

int MidiScaleModule::maskForScaleType (ScaleType scaleType, int customMask) noexcept
{
    return maskForScale(scaleType, customMask);
}

bool MidiScaleModule::noteIsInMask (int noteNumber, int rootNote, int mask) noexcept
{
    const int pitchClass = ((noteNumber % 12) + 12) % 12;
    const int relative = (pitchClass - juce::jlimit(0, 11, rootNote) + 12) % 12;
    return (mask & bit(relative)) != 0;
}

int MidiScaleModule::mapNoteToScale (int noteNumber, int rootNote, ScaleType scaleType,
                                     CorrectionMode correctionMode, int customMask) noexcept
{
    const int safeNote = juce::jlimit(0, 127, noteNumber);
    const int safeRoot = juce::jlimit(0, 11, rootNote);
    const int mask = maskForScale(scaleType, customMask);

    if (noteIsInMask(safeNote, safeRoot, mask))
        return safeNote;

    auto firstValidUp = [&]() noexcept
    {
        for (int distance = 1; distance <= 12; ++distance)
        {
            const int candidate = safeNote + distance;
            if (noteIsInMask(candidate, safeRoot, mask))
                return juce::jlimit(0, 127, candidate);
        }
        return safeNote;
    };

    auto firstValidDown = [&]() noexcept
    {
        for (int distance = 1; distance <= 12; ++distance)
        {
            const int candidate = safeNote - distance;
            if (noteIsInMask(candidate, safeRoot, mask))
                return juce::jlimit(0, 127, candidate);
        }
        return safeNote;
    };

    switch (correctionMode)
    {
        case CorrectionMode::Up:   return firstValidUp();
        case CorrectionMode::Down: return firstValidDown();
        case CorrectionMode::Nearest:
        default:
            for (int distance = 1; distance <= 12; ++distance)
            {
                const int lower = safeNote - distance;
                if (noteIsInMask(lower, safeRoot, mask))
                    return juce::jlimit(0, 127, lower);

                const int upper = safeNote + distance;
                if (noteIsInMask(upper, safeRoot, mask))
                    return juce::jlimit(0, 127, upper);
            }
            break;
    }

    return safeNote;
}

int MidiScaleModule::activeIndex (int channel, int originalNote) noexcept
{
    const int ch = juce::jlimit(1, 16, channel) - 1;
    const int note = juce::jlimit(0, 127, originalNote);
    return ch * 128 + note;
}

void MidiScaleModule::pushActiveMapping (int channel, int originalNote, int mappedNote) noexcept
{
    auto& stack = activeNotes[(size_t) activeIndex(channel, originalNote)];
    if (stack.depth < (int) stack.mappedNotes.size())
        stack.mappedNotes[(size_t) stack.depth++] = juce::jlimit(0, 127, mappedNote);
    else
        stack.mappedNotes.back() = juce::jlimit(0, 127, mappedNote);
}

int MidiScaleModule::popActiveMapping (int channel, int originalNote) noexcept
{
    auto& stack = activeNotes[(size_t) activeIndex(channel, originalNote)];
    if (stack.depth <= 0)
        return juce::jlimit(0, 127, originalNote);

    return stack.mappedNotes[(size_t) --stack.depth];
}

void MidiScaleModule::clearChannel (int channel) noexcept
{
    const int ch = juce::jlimit(1, 16, channel) - 1;
    for (int note = 0; note < 128; ++note)
        activeNotes[(size_t) (ch * 128 + note)].depth = 0;
}

void MidiScaleModule::reset()
{
    for (auto& stack : activeNotes)
        stack.depth = 0;
}

void MidiScaleModule::setConfig (bool shouldEnable, int rootNote, ScaleType scaleType,
                                 CorrectionMode correctionMode, int customMask,
                                 const std::array<int, 12>& pitchClassRemap, int semitoneTranspose)
{
    enabled = shouldEnable;
    root = juce::jlimit(0, 11, rootNote);
    scale = scaleType;
    correction = correctionMode;
    custom = normaliseCustomMask(customMask);
    for (int i = 0; i < 12; ++i)
        remap[(size_t) i] = juce::jlimit(0, 11, pitchClassRemap[(size_t) i]);
    transpose = juce::jlimit(-24, 24, semitoneTranspose);
}

int MidiScaleModule::applyRemapAndTranspose (int noteNumber) const noexcept
{
    const int safeNote = juce::jlimit(0, 127, noteNumber);
    const int pc = safeNote % 12;
    const int mappedPc = remap[(size_t) pc];
    return juce::jlimit(0, 127, safeNote - pc + mappedPc + transpose);
}

void MidiScaleModule::process (juce::MidiBuffer& midi)
{
    juce::MidiBuffer processed;
    process(midi, processed);
    midi.swapWith(processed);
}

void MidiScaleModule::process (const juce::MidiBuffer& input, juce::MidiBuffer& output)
{
    output.clear();

    for (const auto metadata : input)
    {
        auto message = metadata.getMessage();
        const int samplePosition = metadata.samplePosition;

        if (message.isNoteOn(false))
        {
            const int originalNote = message.getNoteNumber();
            const int mappedNote = enabled ? applyRemapAndTranspose(mapNoteToScale(originalNote, root, scale, correction, custom))
                                           : originalNote;

            if (enabled)
                pushActiveMapping(message.getChannel(), originalNote, mappedNote);

            message.setNoteNumber(mappedNote);
            output.addEvent(message, samplePosition);
        }
        else if (message.isNoteOff(true))
        {
            const int originalNote = message.getNoteNumber();
            const int mappedNote = popActiveMapping(message.getChannel(), originalNote);
            message.setNoteNumber(mappedNote);
            output.addEvent(message, samplePosition);
        }
        else
        {
            if (message.isAllNotesOff() || message.isAllSoundOff())
                clearChannel(message.getChannel());

            output.addEvent(message, samplePosition);
        }
    }
}
