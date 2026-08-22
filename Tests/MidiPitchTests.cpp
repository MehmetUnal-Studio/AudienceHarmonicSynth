#include "../Source/MidiPitch.h"

#include <cmath>
#include <limits>
#include <iostream>

namespace
{
    void expect (bool ok, const char* name, int& failed)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << name << "\n";
        if (! ok)
            ++failed;
    }

    bool near (double a, double b, double tolerance)
    {
        return std::abs(a - b) <= tolerance;
    }
}

int main()
{
    int failed = 0;

    {
        const auto pitch = convertFrequencyToMidiPitch (440.0, 48);
        expect (pitch.noteNumber == 69
             && near (pitch.centsOffsetFromNearestNote, 0.0, 0.000001)
             && pitch.pitchBend14Bit == 8192,
             "A4 maps to MIDI note 69 with centered pitch bend",
             failed);
    }

    {
        const auto c1Hz = 32.70319566257483;
        const auto pitch = convertFrequencyToMidiPitch (c1Hz, 48);
        expect (pitch.noteNumber == 24
             && near (pitch.centsOffsetFromNearestNote, 0.0, 0.000001)
             && pitch.pitchBend14Bit == 8192,
             "C1 maps to MIDI note 24 with centered bend",
             failed);
    }

    {
        const auto c1Hz = 32.70319566257483;
        const auto fluorineLikeDegreeHz = c1Hz * std::pow (2.0, 80.4 / 1200.0);
        const auto pitch = convertFrequencyToMidiPitch (fluorineLikeDegreeHz, 48);
        expect (pitch.noteNumber == 25
             && near (pitch.centsOffsetFromNearestNote, -19.6, 0.05)
             && pitch.pitchBend14Bit < 8192,
             "microtonal pitch keeps cents as per-note pitch bend",
             failed);
    }

    {
        const auto pitch = convertFrequencyToMidiPitch (1.0, 48);
        expect (pitch.noteNumber == 0
             && pitch.pitchBend14Bit >= 0
             && pitch.pitchBend14Bit <= 16383,
             "sub-MIDI frequencies clamp safely into valid MIDI/bend range",
             failed);
    }

    {
        const auto pitch = convertFrequencyToMidiPitch (20000.0, 48);
        expect (pitch.noteNumber == 127
             && pitch.pitchBend14Bit >= 0
             && pitch.pitchBend14Bit <= 16383,
             "very high frequencies clamp safely into valid MIDI/bend range",
             failed);
    }

    {
        const auto nanPitch = convertFrequencyToMidiPitch(
            std::numeric_limits<double>::quiet_NaN(), 48);
        const auto infPitch = convertFrequencyToMidiPitch(
            std::numeric_limits<double>::infinity(), 48);
        expect (nanPitch.noteNumber == 0 && infPitch.noteNumber == 0
             && std::isfinite(nanPitch.targetFrequencyHz)
             && std::isfinite(infPitch.targetFrequencyHz),
             "non-finite frequencies fall back to a finite valid MIDI pitch",
             failed);
    }

    std::cout << "\nSummary: " << (failed == 0 ? "ok" : "failed") << "\n";
    return failed == 0 ? 0 : 1;
}
