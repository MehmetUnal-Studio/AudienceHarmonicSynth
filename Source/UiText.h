#pragma once

#include <juce_core/juce_core.h>

namespace UiText
{
    /** Returns the conventional note name for a MIDI note number, e.g. 60 -> "C4".

        Uses the scientific-pitch octave convention (MIDI 0 == "C-1"), matching the
        formula previously duplicated across the UI editors:
            names[((midi % 12) + 12) % 12] + (midi / 12 - 1)

        The output is byte-identical to those former copies for all MIDI values.
    */
    inline juce::String midiNoteName (int midi)
    {
        static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        return juce::String(names[((midi % 12) + 12) % 12]) + juce::String(midi / 12 - 1);
    }
}
