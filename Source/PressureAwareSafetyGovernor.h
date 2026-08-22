#pragma once

#include <cstdint>

// Allocation-free overload governor for the realtime/control boundary.
//
// The class owns no queues, clocks or telemetry sinks. Its owner supplies one
// immutable pressure snapshot per tick and publishes the returned plain-data
// Output as appropriate. A single owner must call update/reset; concurrent
// readers should consume a separately published snapshot.
class PressureAwareSafetyGovernor final
{
public:
    enum class State : std::uint8_t
    {
        NORMAL = 0,
        HIGH,
        CRITICAL,
        EMERGENCY
    };

    enum ReasonBits : std::uint32_t
    {
        ReasonNone = 0u,
        ReasonIngressRate = 1u << 0,
        ReasonLifecycleQueue = 1u << 1,
        ReasonMotionDrop = 1u << 2,
        ReasonTimeFieldPending = 1u << 3,
        ReasonExternalFifo = 1u << 4,
        ReasonExternalFifoAge = 1u << 5,
        ReasonProcessDeadline = 1u << 6,
        ReasonInvalidInput = 1u << 7,
        ReasonInvalidClock = 1u << 8,
        ReasonRecoveryHeld = 1u << 9
    };

    struct TripPoints
    {
        double high = 0.0;
        double critical = 0.0;
        double emergency = 0.0;
    };

    struct Config
    {
        TripPoints ingressEventsPerSecond { 1500.0, 4000.0, 8000.0 };
        TripPoints lifecycleQueuePressure { 0.60, 0.80, 0.95 };
        TripPoints motionDropDelta { 4.0, 16.0, 64.0 };
        TripPoints timeFieldPendingPressure { 0.60, 0.80, 0.95 };
        TripPoints externalFifoPressure { 0.60, 0.80, 0.95 };
        TripPoints externalFifoOldestAgeSeconds { 0.025, 0.075, 0.250 };
        TripPoints processDeadlineRatio { 0.65, 0.82, 0.95 };

        // Recovery must fall this fraction below the current state's entry
        // threshold, then remain there for the corresponding hold. Demotion is
        // deliberately limited to one state per completed hold.
        double recoveryHysteresis = 0.15;
        double highRecoveryHoldSeconds = 2.0;
        double criticalRecoveryHoldSeconds = 3.0;
        double emergencyRecoveryHoldSeconds = 5.0;

        // Bounds credit from a single update so a host resume cannot instantly
        // consume a recovery hold.
        double maxElapsedSeconds = 0.25;
    };

    struct Input
    {
        double ingressEventsPerSecond = 0.0;
        double lifecycleQueuePressure = 0.0;
        double motionDropDelta = 0.0;
        double timeFieldPendingPressure = 0.0;
        double externalFifoPressure = 0.0;
        double externalFifoOldestAgeSeconds = 0.0;
        double processDeadlineRatio = 0.0;
        double monotonicSeconds = 0.0;
    };

    struct Profile
    {
        int motionUpdateDivisor = 1;
        int maxAttacksCeiling = 16;
        int maxActiveCeiling = 16;
        int minSpread = 1;
        bool admitNewAttacks = true;
        bool macrosEnabled = true;
    };

    struct Output
    {
        State state = State::NORMAL;
        int motionUpdateDivisor = 1;
        int maxAttacksCeiling = 16;
        int maxActiveCeiling = 16;
        int minSpread = 1;
        bool admitNewAttacks = true;
        bool macrosEnabled = true;

        // Reasons describe the current sample. ReasonRecoveryHeld is added when
        // hysteresis or a hold intentionally keeps a stricter state active.
        std::uint32_t reasonBits = ReasonNone;

        // True when state, controls, or reasonBits differ from the last output.
        bool changed = false;
    };

    PressureAwareSafetyGovernor() noexcept;

    PressureAwareSafetyGovernor (const PressureAwareSafetyGovernor&) = delete;
    PressureAwareSafetyGovernor& operator= (const PressureAwareSafetyGovernor&) = delete;

    static Config sanitiseConfig (const Config&) noexcept;
    static Profile profileForState (State) noexcept;

    // Pressure fields are non-negative. Queue/pending/FIFO pressures are
    // normalised to 0..1; deadline ratio is process-time/deadline; FIFO age is
    // seconds. Any NaN, infinity, negative or impossible bound fails closed to
    // EMERGENCY and identifies the offending signal in reasonBits.
    Output update (const Config&, const Input&) noexcept;

    void reset() noexcept;
    Output getOutput() const noexcept { return output_; }

    static constexpr bool hasReason (std::uint32_t reasons,
                                     ReasonBits reason) noexcept
    {
        return (reasons & static_cast<std::uint32_t>(reason)) != 0u;
    }

private:
    static State lowerState (State) noexcept;
    static double thresholdForState (const TripPoints&, State) noexcept;
    static double recoveryHoldForState (const Config&, State) noexcept;
    static bool sameOutputPayload (const Output&, const Output&) noexcept;

    bool allSignalsBelowRecoveryBoundary (const Config&, const Input&) const noexcept;
    void clearRecoveryCandidate() noexcept;
    void applyProfile() noexcept;

    Output output_ {};
    State state_ = State::NORMAL;
    State recoveryCandidateState_ = State::NORMAL;
    double recoveryElapsedSeconds_ = 0.0;
    double lastTimeSeconds_ = 0.0;
    bool recoveryCandidateActive_ = false;
    bool clockInitialised_ = false;
};
