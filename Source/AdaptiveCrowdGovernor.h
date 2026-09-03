#pragma once

// Allocation-free crowd-density controller for the Time Field scheduler.
//
// The owner supplies two already bounded observations: the number of sources
// currently held and the number of unique sources seen in its rolling eight
// second window. The governor deliberately owns no participant ledger and
// performs no I/O, allocation, locking or JUCE calls.
class AdaptiveCrowdGovernor final
{
public:
    static constexpr int kMaxCrowdSize = 256;

    struct Config
    {
        // Notes Only supplies the full 16 MIDI-channel capacity, so the
        // recommendation can never exceed its real voice pool.
        int voiceLimit = 16;

        // Rise/fall are approximately the time to traverse 95% of a density
        // step. Promotion and demotion holds debounce the resulting band.
        double riseSeconds = 0.5;
        double fallSeconds = 6.0;
        double promotionHoldSeconds = 0.5;
        double demotionHoldSeconds = 4.0;
        double demotionHysteresis = 0.20;
    };

    struct Input
    {
        int heldSources = 0;
        int recentUniqueSources = 0;
        double monotonicSeconds = 0.0;
    };

    struct Profile
    {
        int band = 0;
        int maxAttacksPerStep = 4;
        int spreadSlots = 1;
        int maxActive = 8;
    };

    struct Output
    {
        int observedDensity = 0;
        double smoothedDensity = 0.0;
        int band = 0;
        int maxAttacksPerStep = 4;
        int spreadSlots = 1;
        int maxActive = 8;

        // True only when a recommended scheduler control or band changed.
        bool changed = false;
    };

    AdaptiveCrowdGovernor() noexcept;

    AdaptiveCrowdGovernor (const AdaptiveCrowdGovernor&) = delete;
    AdaptiveCrowdGovernor& operator= (const AdaptiveCrowdGovernor&) = delete;

    static Config sanitiseConfig (const Config&) noexcept;

    // Stateless reference table. Density is clamped to 0..256 and voiceLimit
    // to 1..16, making this safe for hostile control/state input.
    static Profile profileForDensity (int density, int voiceLimit) noexcept;

    // Intended for a 10 Hz control tick, but accepts any finite monotonic clock.
    // A backward/non-finite timestamp advances no timers. A single large time
    // jump is bounded so host resume cannot bypass the promotion/demotion holds.
    Output update (const Config&, const Input&) noexcept;

    // Returns to the low-density profile and forgets all clock/envelope history.
    void reset() noexcept;

    Output getOutput() const noexcept { return output_; }

private:
    static int clampDensity (int value) noexcept;
    static double responseCoefficient (double elapsedSeconds,
                                       double responseSeconds) noexcept;
    static int lowerEdgeForBand (int band) noexcept;
    static bool sameRecommendations (const Output&, const Profile&) noexcept;

    void clearCandidate() noexcept;
    void considerBandTransition (const Config&, double elapsedSeconds) noexcept;
    void applyCurrentProfile (int voiceLimit) noexcept;

    Output output_ {};
    double lastTimeSeconds_ = 0.0;
    double candidateElapsedSeconds_ = 0.0;
    int currentBand_ = 0;
    int candidateBand_ = -1;
    bool clockInitialised_ = false;
    bool envelopeInitialised_ = false;
};
