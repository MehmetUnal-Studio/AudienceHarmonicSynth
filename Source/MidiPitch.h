#pragma once

#include <cmath>
#include <juce_core/juce_core.h>

struct MidiPitch
{
    int noteNumber = 60;
    double centsOffsetFromNearestNote = 0.0;
    int pitchBend14Bit = 8192;
    double targetFrequencyHz = 261.6255653005986;
};

inline MidiPitch convertFrequencyToMidiPitch (double targetFrequencyHz,
                                              int pitchBendRangeSemitones) noexcept
{
    constexpr double minimumMidiFrequency = 8.175798915643707;   // note 0
    constexpr double maximumMidiFrequency = 12543.853951415975;  // note 127
    MidiPitch out;
    const double finitePositive = std::isfinite(targetFrequencyHz)
                               && targetFrequencyHz > 0.0
                                ? targetFrequencyHz : minimumMidiFrequency;
    // Extremely small positive/subnormal inputs used to underflow in
    // targetFrequencyHz / 440, producing log2(0) = -Inf followed by an
    // undefined floating-to-int conversion. The MIDI protocol cannot represent
    // anything outside note 0..127 anyway, so clamp before the logarithm.
    out.targetFrequencyHz = juce::jlimit(minimumMidiFrequency,
                                         maximumMidiFrequency,
                                         finitePositive);

    const int bendRange = juce::jmax(1, pitchBendRangeSemitones);
    const double midiFloat = 69.0 + 12.0 * std::log2(out.targetFrequencyHz / 440.0);
    const int nearest = juce::jlimit(0, 127, (int) std::round(midiFloat));

    out.noteNumber = nearest;
    out.centsOffsetFromNearestNote = (midiFloat - (double) nearest) * 100.0;

    const double bendNorm = juce::jlimit(-1.0, 1.0,
        out.centsOffsetFromNearestNote / ((double) bendRange * 100.0));

    out.pitchBend14Bit = juce::jlimit(0, 16383,
        8192 + (int) std::round(bendNorm * 8192.0));

    return out;
}
