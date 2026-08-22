#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Fixed-cost crowd-expression analyser and MIDI-macro rate scheduler.
//
// Source slots are identity-bearing: callers keep the same participant in the
// same array index between updates. The class owns no threads, clocks, queues
// or MIDI destinations and performs no allocation, locking, I/O or JUCE work.
// A single owner calls update/reset and publishes the returned POD snapshot.
class CrowdExpressionMacros final
{
public:
    static constexpr std::size_t kMaxSources = 256;

    enum ChangedBits : std::uint8_t
    {
        ChangedNone = 0u,
        ChangedDensity = 1u << 0,
        ChangedCentroidX = 1u << 1,
        ChangedCentroidY = 1u << 2,
        ChangedMotion = 1u << 3,
        ChangedAll = ChangedDensity | ChangedCentroidX
                   | ChangedCentroidY | ChangedMotion
    };

    struct SourceSnapshot
    {
        bool active = false;
        float x = 0.0f;
        float y = 0.0f;
    };

    struct Input
    {
        std::array<SourceSnapshot, kMaxSources> sources {};
        double monotonicSeconds = 0.0;
    };

    struct Config
    {
        // Safety Governor policy is applied by the caller through this gate.
        // Telemetry continues to update while MIDI macro emission is disabled.
        bool enabled = false;
        double rateHz = 10.0;
    };

    struct MacroValues
    {
        std::uint8_t density = 0;
        std::uint8_t centroidX = 64;
        std::uint8_t centroidY = 64;
        std::uint8_t motion = 0;
    };

    struct Output
    {
        MacroValues values {};

        std::uint16_t activeSources = 0;
        std::uint16_t validPositionSources = 0;

        // Normalised, finite telemetry. Empty crowds are centred at (0.5, 0.5).
        double density = 0.0;
        double centroidX = 0.5;
        double centroidY = 0.5;
        double motion = 0.0;

        // emitDue marks a rate-scheduler tick. changedMask compares the current
        // values with the last emitted values. A first or re-enabled tick sets
        // all four bits so downstream CC state is always rehydrated.
        std::uint8_t changedMask = ChangedNone;
        bool emitDue = false;
        bool clockValid = true;
    };

    CrowdExpressionMacros() noexcept;

    CrowdExpressionMacros (const CrowdExpressionMacros&) = delete;
    CrowdExpressionMacros& operator= (const CrowdExpressionMacros&) = delete;

    static Config sanitiseConfig (const Config&) noexcept;

    // The scan cost is always exactly kMaxSources slots. Backward or non-finite
    // clocks earn no EMA/scheduler time; a backward finite clock starts a fresh
    // scheduling epoch without emitting a duplicate tick.
    Output update (const Config&, const Input&) noexcept;

    void reset() noexcept;
    Output getOutput() const noexcept { return output_; }

private:
    static double clampUnit (double value) noexcept;
    static std::uint8_t toMidi7Bit (double value) noexcept;
    static bool sameValues (const MacroValues&, const MacroValues&) noexcept;
    static std::uint8_t changedBits (const MacroValues&,
                                    const MacroValues&) noexcept;

    std::array<float, kMaxSources> previousX_ {};
    std::array<float, kMaxSources> previousY_ {};
    std::array<std::uint8_t, kMaxSources> previousPositionValid_ {};

    Output output_ {};
    MacroValues lastEmittedValues_ {};
    double motionEma_ = 0.0;
    double lastUpdateTimeSeconds_ = 0.0;
    double lastEmitTimeSeconds_ = 0.0;
    bool clockInitialised_ = false;
    bool motionInitialised_ = false;
    bool emissionEnabledLastUpdate_ = false;
    bool hasEmitted_ = false;
};
