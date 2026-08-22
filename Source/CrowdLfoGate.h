#pragma once

#include <array>
#include <cstdint>

// Realtime-safe binary LFO gate for the OSC audience stream. The class only
// decides when the gate changes state; the processor owns the authoritative
// held-touch ledger and translates Close into bounded releases and Open into a
// Time Field re-pend/rehydration policy.
//
// Threading contract: construct, reset and process on the audio thread (or while
// audio is stopped). process() performs no allocation, locking, logging or I/O.
class CrowdLfoGate final
{
public:
    static constexpr int kMaxTransitionsPerBlock = 64;
    static constexpr double kGateThreshold = 0.5;

    enum class Waveform : std::uint8_t
    {
        Sine = 0,
        Triangle,
        Square,
        SawUp,
        SawDown
    };

    enum class RateMode : std::uint8_t
    {
        Hertz = 0,
        Sync
    };

    // Values describe the duration of one complete LFO cycle.
    enum class SyncDivision : std::uint8_t
    {
        EightBars = 0,
        FourBars,
        TwoBars,
        OneBar,
        Half,
        Quarter,
        Eighth,
        Sixteenth,
        ThirtySecond,
        SixtyFourth
    };

    // Deliberately narrow v1 state: this is a lifecycle gate, not a general
    // modulation source. Disabled is always-open bypass; threshold and phase
    // convention are fixed so state recall cannot create ambiguous note gates.
    struct Config
    {
        bool enabled = false;
        Waveform waveform = Waveform::Sine;
        RateMode rateMode = RateMode::Hertz;
        double rateHz = 1.0;
        SyncDivision syncDivision = SyncDivision::Quarter;
    };

    // Values describe the start of the current audio block. Sync uses host PPQ
    // only while a valid host is playing. All other cases derive absolute phase
    // from one process-wide monotonicSeconds clock, aligning multiple plug-in
    // instances regardless of when they were created. fallbackBpm is used only
    // by Sync when host PPQ is unavailable.
    struct ClockFrame
    {
        double sampleRate = 44100.0;
        int numSamples = 0;
        bool hostValid = false;
        bool isPlaying = false;
        double bpm = 120.0;
        double ppqPosition = 0.0;
        double monotonicSeconds = 0.0;
        double fallbackBpm = 120.0;
    };

    struct Transition
    {
        enum class Type : std::uint8_t { Open, Close };

        Type type = Type::Open;
        int sampleOffset = 0;
    };

    struct OutputBlock
    {
        std::array<Transition, kMaxTransitionsPerBlock> transitions {};
        int count = 0;
        bool gateOpen = true;
        bool active = false;
        bool hostLocked = false;
        bool resetRequested = false;
        bool overflowed = false;
        double effectiveRateHz = 0.0;
        double phaseAtStart = 0.0;
        double phaseAtEnd = 0.0; // exclusive block end
        double valueAtStart = 1.0;
        double valueAtEnd = 1.0; // value at the exclusive block end
    };

    CrowdLfoGate() noexcept;

    CrowdLfoGate (const CrowdLfoGate&) = delete;
    CrowdLfoGate& operator= (const CrowdLfoGate&) = delete;

    static Config sanitiseConfig (const Config&) noexcept;
    static double divisionQuarterNotes (SyncDivision) noexcept;

    // Returns the unipolar [0, 1] signal used by the fixed binary threshold.
    // Non-finite phases are mapped to phase zero.
    static double waveformValue (Waveform, double phase) noexcept;

    // Emits only state transitions in ascending sample order. If more than the
    // fixed output capacity is required, the partial list is discarded and
    // resetRequested is set. The owner must perform one MIDI safety release and
    // then obey final gateOpen when deciding whether held voices may re-pend.
    void process (const Config&, const ClockFrame&, OutputBlock&) noexcept;

    // Restores the safe-open baseline. reset() emits no event; the owner remains
    // responsible for any MIDI safety release already owed.
    void reset() noexcept;

    bool isGateOpen() const noexcept { return gateOpen_; }

private:
    static double positiveFraction (double) noexcept;
    static bool validHostClock (const ClockFrame&) noexcept;
    static bool gateStateAtPhase (Waveform, double) noexcept;

    void setGate (bool shouldOpen, int sampleOffset, OutputBlock&) noexcept;

    bool gateOpen_ = true;
};
