#include "PressureAwareSafetyGovernor.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double kMaxIngressEventsPerSecond = 1.0e9;
    constexpr double kMaxMotionDropDelta = 1.0e9;
    constexpr double kMaxFifoAgeSeconds = 86400.0;
    constexpr double kMaxDeadlineRatio = 1000.0;
    constexpr double kMaxMonotonicSeconds = 1.0e12;

    bool isFiniteInRange (double value, double maximum) noexcept
    {
        return std::isfinite(value) && value >= 0.0 && value <= maximum;
    }

    bool validTripPoints (const PressureAwareSafetyGovernor::TripPoints& points,
                          double maximum) noexcept
    {
        return std::isfinite(points.high)
            && std::isfinite(points.critical)
            && std::isfinite(points.emergency)
            && points.high > 0.0
            && points.high < points.critical
            && points.critical < points.emergency
            && points.emergency <= maximum;
    }

    PressureAwareSafetyGovernor::TripPoints sanitiseTripPoints (
        const PressureAwareSafetyGovernor::TripPoints& requested,
        const PressureAwareSafetyGovernor::TripPoints& fallback,
        double maximum) noexcept
    {
        return validTripPoints(requested, maximum) ? requested : fallback;
    }

    double sanitiseScalar (double value, double minimum, double maximum,
                           double fallback) noexcept
    {
        if (! std::isfinite(value) || value < minimum || value > maximum)
            return fallback;
        return value;
    }

    int stateRank (PressureAwareSafetyGovernor::State state) noexcept
    {
        return static_cast<int>(state);
    }

    void assessSignal (double value,
                       double validMaximum,
                       const PressureAwareSafetyGovernor::TripPoints& points,
                       PressureAwareSafetyGovernor::ReasonBits reason,
                       PressureAwareSafetyGovernor::State& requestedState,
                       std::uint32_t& reasons) noexcept
    {
        using Governor = PressureAwareSafetyGovernor;
        const auto reasonMask = static_cast<std::uint32_t>(reason);

        if (! isFiniteInRange(value, validMaximum))
        {
            reasons |= reasonMask
                     | static_cast<std::uint32_t>(Governor::ReasonInvalidInput);
            requestedState = Governor::State::EMERGENCY;
            return;
        }

        Governor::State severity = Governor::State::NORMAL;
        if (value >= points.emergency)
            severity = Governor::State::EMERGENCY;
        else if (value >= points.critical)
            severity = Governor::State::CRITICAL;
        else if (value >= points.high)
            severity = Governor::State::HIGH;

        if (severity != Governor::State::NORMAL)
            reasons |= reasonMask;
        if (stateRank(severity) > stateRank(requestedState))
            requestedState = severity;
    }
}

PressureAwareSafetyGovernor::PressureAwareSafetyGovernor() noexcept
{
    reset();
}

PressureAwareSafetyGovernor::Config PressureAwareSafetyGovernor::sanitiseConfig (
    const Config& requested) noexcept
{
    const Config defaults;
    Config result;
    result.ingressEventsPerSecond = sanitiseTripPoints(
        requested.ingressEventsPerSecond, defaults.ingressEventsPerSecond,
        kMaxIngressEventsPerSecond);
    result.lifecycleQueuePressure = sanitiseTripPoints(
        requested.lifecycleQueuePressure, defaults.lifecycleQueuePressure, 1.0);
    result.motionDropDelta = sanitiseTripPoints(
        requested.motionDropDelta, defaults.motionDropDelta, kMaxMotionDropDelta);
    result.timeFieldPendingPressure = sanitiseTripPoints(
        requested.timeFieldPendingPressure, defaults.timeFieldPendingPressure, 1.0);
    result.externalFifoPressure = sanitiseTripPoints(
        requested.externalFifoPressure, defaults.externalFifoPressure, 1.0);
    result.externalFifoOldestAgeSeconds = sanitiseTripPoints(
        requested.externalFifoOldestAgeSeconds,
        defaults.externalFifoOldestAgeSeconds, kMaxFifoAgeSeconds);
    result.processDeadlineRatio = sanitiseTripPoints(
        requested.processDeadlineRatio, defaults.processDeadlineRatio,
        kMaxDeadlineRatio);
    result.recoveryHysteresis = sanitiseScalar(
        requested.recoveryHysteresis, 0.0, 0.49,
        defaults.recoveryHysteresis);
    result.highRecoveryHoldSeconds = sanitiseScalar(
        requested.highRecoveryHoldSeconds, 0.0, 60.0,
        defaults.highRecoveryHoldSeconds);
    result.criticalRecoveryHoldSeconds = sanitiseScalar(
        requested.criticalRecoveryHoldSeconds, 0.0, 60.0,
        defaults.criticalRecoveryHoldSeconds);
    result.emergencyRecoveryHoldSeconds = sanitiseScalar(
        requested.emergencyRecoveryHoldSeconds, 0.0, 60.0,
        defaults.emergencyRecoveryHoldSeconds);
    result.maxElapsedSeconds = sanitiseScalar(
        requested.maxElapsedSeconds, 0.001, 1.0,
        defaults.maxElapsedSeconds);
    return result;
}

PressureAwareSafetyGovernor::Profile PressureAwareSafetyGovernor::profileForState (
    State state) noexcept
{
    switch (state)
    {
        case State::NORMAL:    return { 1, 16, 16, 1, true, true };
        case State::HIGH:      return { 2, 8, 12, 2, true, true };
        case State::CRITICAL:  return { 4, 2, 8, 4, true, false };
        case State::EMERGENCY: return { 8, 1, 4, 8, false, false };
        default:               return { 8, 1, 4, 8, false, false };
    }
}

PressureAwareSafetyGovernor::Output PressureAwareSafetyGovernor::update (
    const Config& requestedConfig, const Input& input) noexcept
{
    const auto config = sanitiseConfig(requestedConfig);
    const auto previous = output_;
    State requestedState = State::NORMAL;
    std::uint32_t reasons = ReasonNone;

    assessSignal(input.ingressEventsPerSecond, kMaxIngressEventsPerSecond,
                 config.ingressEventsPerSecond, ReasonIngressRate,
                 requestedState, reasons);
    assessSignal(input.lifecycleQueuePressure, 1.0,
                 config.lifecycleQueuePressure, ReasonLifecycleQueue,
                 requestedState, reasons);
    assessSignal(input.motionDropDelta, kMaxMotionDropDelta,
                 config.motionDropDelta, ReasonMotionDrop,
                 requestedState, reasons);
    assessSignal(input.timeFieldPendingPressure, 1.0,
                 config.timeFieldPendingPressure, ReasonTimeFieldPending,
                 requestedState, reasons);
    assessSignal(input.externalFifoPressure, 1.0,
                 config.externalFifoPressure, ReasonExternalFifo,
                 requestedState, reasons);
    assessSignal(input.externalFifoOldestAgeSeconds, kMaxFifoAgeSeconds,
                 config.externalFifoOldestAgeSeconds, ReasonExternalFifoAge,
                 requestedState, reasons);
    assessSignal(input.processDeadlineRatio, kMaxDeadlineRatio,
                 config.processDeadlineRatio, ReasonProcessDeadline,
                 requestedState, reasons);

    double elapsedSeconds = 0.0;
    const bool finiteClock = isFiniteInRange(input.monotonicSeconds,
                                              kMaxMonotonicSeconds);
    bool clockValid = finiteClock;
    if (finiteClock)
    {
        if (! clockInitialised_)
        {
            lastTimeSeconds_ = input.monotonicSeconds;
            clockInitialised_ = true;
        }
        else if (input.monotonicSeconds >= lastTimeSeconds_)
        {
            elapsedSeconds = std::min(config.maxElapsedSeconds,
                                      input.monotonicSeconds - lastTimeSeconds_);
            lastTimeSeconds_ = input.monotonicSeconds;
        }
        else
        {
            // Adopt the new epoch, but never let a discontinuity earn recovery.
            lastTimeSeconds_ = input.monotonicSeconds;
            clockValid = false;
        }
    }

    if (! clockValid)
    {
        reasons |= static_cast<std::uint32_t>(ReasonInvalidInput)
                 | static_cast<std::uint32_t>(ReasonInvalidClock);
        requestedState = State::EMERGENCY;
        elapsedSeconds = 0.0;
    }

    if (stateRank(requestedState) > stateRank(state_))
    {
        state_ = requestedState;
        clearRecoveryCandidate();
    }
    else if (stateRank(requestedState) < stateRank(state_))
    {
        reasons |= static_cast<std::uint32_t>(ReasonRecoveryHeld);
        if (! allSignalsBelowRecoveryBoundary(config, input) || ! clockValid)
        {
            clearRecoveryCandidate();
        }
        else
        {
            if (! recoveryCandidateActive_ || recoveryCandidateState_ != state_)
            {
                recoveryCandidateActive_ = true;
                recoveryCandidateState_ = state_;
                recoveryElapsedSeconds_ = 0.0;
            }
            else
            {
                recoveryElapsedSeconds_ = std::min(
                    60.0, recoveryElapsedSeconds_ + elapsedSeconds);
            }

            if (recoveryElapsedSeconds_ + 1.0e-12
                >= recoveryHoldForState(config, state_))
            {
                state_ = lowerState(state_);
                clearRecoveryCandidate();
            }
        }
    }
    else
    {
        clearRecoveryCandidate();
    }

    output_.state = state_;
    output_.reasonBits = reasons;
    applyProfile();
    output_.changed = ! sameOutputPayload(previous, output_);
    return output_;
}

void PressureAwareSafetyGovernor::reset() noexcept
{
    state_ = State::NORMAL;
    output_ = {};
    output_.state = state_;
    applyProfile();
    output_.reasonBits = ReasonNone;
    output_.changed = false;
    lastTimeSeconds_ = 0.0;
    clockInitialised_ = false;
    clearRecoveryCandidate();
}

PressureAwareSafetyGovernor::State PressureAwareSafetyGovernor::lowerState (
    State state) noexcept
{
    switch (state)
    {
        case State::EMERGENCY: return State::CRITICAL;
        case State::CRITICAL:  return State::HIGH;
        case State::HIGH:      return State::NORMAL;
        case State::NORMAL:    return State::NORMAL;
        default:               return State::EMERGENCY;
    }
}

double PressureAwareSafetyGovernor::thresholdForState (
    const TripPoints& points, State state) noexcept
{
    switch (state)
    {
        case State::HIGH:      return points.high;
        case State::CRITICAL:  return points.critical;
        case State::EMERGENCY: return points.emergency;
        case State::NORMAL:    return 0.0;
        default:               return points.emergency;
    }
}

double PressureAwareSafetyGovernor::recoveryHoldForState (
    const Config& config, State state) noexcept
{
    switch (state)
    {
        case State::HIGH:      return config.highRecoveryHoldSeconds;
        case State::CRITICAL:  return config.criticalRecoveryHoldSeconds;
        case State::EMERGENCY: return config.emergencyRecoveryHoldSeconds;
        case State::NORMAL:    return 0.0;
        default:               return config.emergencyRecoveryHoldSeconds;
    }
}

bool PressureAwareSafetyGovernor::sameOutputPayload (
    const Output& first, const Output& second) noexcept
{
    return first.state == second.state
        && first.motionUpdateDivisor == second.motionUpdateDivisor
        && first.maxAttacksCeiling == second.maxAttacksCeiling
        && first.maxActiveCeiling == second.maxActiveCeiling
        && first.minSpread == second.minSpread
        && first.admitNewAttacks == second.admitNewAttacks
        && first.macrosEnabled == second.macrosEnabled
        && first.reasonBits == second.reasonBits;
}

bool PressureAwareSafetyGovernor::allSignalsBelowRecoveryBoundary (
    const Config& config, const Input& input) const noexcept
{
    const double multiplier = 1.0 - config.recoveryHysteresis;
    const auto below = [this, multiplier] (double value,
                                            const TripPoints& points) noexcept
    {
        return value < thresholdForState(points, state_) * multiplier;
    };

    return below(input.ingressEventsPerSecond, config.ingressEventsPerSecond)
        && below(input.lifecycleQueuePressure, config.lifecycleQueuePressure)
        && below(input.motionDropDelta, config.motionDropDelta)
        && below(input.timeFieldPendingPressure, config.timeFieldPendingPressure)
        && below(input.externalFifoPressure, config.externalFifoPressure)
        && below(input.externalFifoOldestAgeSeconds,
                 config.externalFifoOldestAgeSeconds)
        && below(input.processDeadlineRatio, config.processDeadlineRatio);
}

void PressureAwareSafetyGovernor::clearRecoveryCandidate() noexcept
{
    recoveryCandidateState_ = State::NORMAL;
    recoveryElapsedSeconds_ = 0.0;
    recoveryCandidateActive_ = false;
}

void PressureAwareSafetyGovernor::applyProfile() noexcept
{
    const auto profile = profileForState(state_);
    output_.motionUpdateDivisor = profile.motionUpdateDivisor;
    output_.maxAttacksCeiling = profile.maxAttacksCeiling;
    output_.maxActiveCeiling = profile.maxActiveCeiling;
    output_.minSpread = profile.minSpread;
    output_.admitNewAttacks = profile.admitNewAttacks;
    output_.macrosEnabled = profile.macrosEnabled;
}
