#include "CrowdExpressionMacros.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double kDefaultRateHz = 10.0;
    constexpr double kMinimumRateHz = 5.0;
    constexpr double kMaximumRateHz = 30.0;
    constexpr double kMotionEmaSeconds = 0.30;
    constexpr double kMaximumElapsedSeconds = 1.0;
    constexpr double kInverseMaximumDistance = 0.70710678118654752440;
    constexpr double kScheduleEpsilonSeconds = 1.0e-12;

    double finiteOr (double value, double fallback) noexcept
    {
        return std::isfinite(value) ? value : fallback;
    }
}

CrowdExpressionMacros::CrowdExpressionMacros() noexcept
{
    reset();
}

CrowdExpressionMacros::Config CrowdExpressionMacros::sanitiseConfig (
    const Config& requested) noexcept
{
    Config result;
    result.enabled = requested.enabled;
    result.rateHz = std::max(kMinimumRateHz,
                             std::min(kMaximumRateHz,
                                      finiteOr(requested.rateHz,
                                               kDefaultRateHz)));
    return result;
}

CrowdExpressionMacros::Output CrowdExpressionMacros::update (
    const Config& requestedConfig, const Input& input) noexcept
{
    const auto config = sanitiseConfig(requestedConfig);
    const bool finiteClock = std::isfinite(input.monotonicSeconds);
    bool clockRebased = false;
    double elapsedSeconds = 0.0;

    if (finiteClock)
    {
        if (! clockInitialised_)
        {
            lastUpdateTimeSeconds_ = input.monotonicSeconds;
            clockInitialised_ = true;
        }
        else if (input.monotonicSeconds >= lastUpdateTimeSeconds_)
        {
            elapsedSeconds = std::min(kMaximumElapsedSeconds,
                                      input.monotonicSeconds
                                        - lastUpdateTimeSeconds_);
            lastUpdateTimeSeconds_ = input.monotonicSeconds;
        }
        else
        {
            // A host transport/clock reset must not create a catch-up burst.
            lastUpdateTimeSeconds_ = input.monotonicSeconds;
            lastEmitTimeSeconds_ = input.monotonicSeconds;
            clockRebased = true;
        }
    }

    int activeCount = 0;
    int validPositionCount = 0;
    int motionPairCount = 0;
    double sumX = 0.0;
    double sumY = 0.0;
    double sumMotion = 0.0;

    for (std::size_t index = 0; index < kMaxSources; ++index)
    {
        const auto& source = input.sources[index];
        if (! source.active)
        {
            previousPositionValid_[index] = 0u;
            continue;
        }

        ++activeCount;
        const bool finitePosition = std::isfinite(source.x)
                                 && std::isfinite(source.y);
        if (! finitePosition)
        {
            // Do not let one hostile coordinate poison the crowd aggregate or
            // bridge an invalid sample into a later false motion spike.
            previousPositionValid_[index] = 0u;
            continue;
        }

        const double x = clampUnit(static_cast<double>(source.x));
        const double y = clampUnit(static_cast<double>(source.y));
        sumX += x;
        sumY += y;
        ++validPositionCount;

        if (previousPositionValid_[index] != 0u)
        {
            const double deltaX = x - static_cast<double>(previousX_[index]);
            const double deltaY = y - static_cast<double>(previousY_[index]);
            const double distance = std::sqrt(deltaX * deltaX
                                              + deltaY * deltaY);
            sumMotion += clampUnit(distance * kInverseMaximumDistance);
            ++motionPairCount;
        }

        previousX_[index] = static_cast<float>(x);
        previousY_[index] = static_cast<float>(y);
        previousPositionValid_[index] = 1u;
    }

    output_.activeSources = static_cast<std::uint16_t>(activeCount);
    output_.validPositionSources = static_cast<std::uint16_t>(validPositionCount);
    output_.density = static_cast<double>(activeCount)
                    / static_cast<double>(kMaxSources);
    output_.centroidX = validPositionCount > 0
                      ? clampUnit(sumX / static_cast<double>(validPositionCount))
                      : 0.5;
    output_.centroidY = validPositionCount > 0
                      ? clampUnit(sumY / static_cast<double>(validPositionCount))
                      : 0.5;

    const double instantaneousMotion = motionPairCount > 0
                                     ? clampUnit(sumMotion
                                         / static_cast<double>(motionPairCount))
                                     : 0.0;
    if (! motionInitialised_)
    {
        motionEma_ = instantaneousMotion;
        motionInitialised_ = true;
    }
    else if (elapsedSeconds > 0.0)
    {
        const double coefficient = -std::expm1(-elapsedSeconds
                                                / kMotionEmaSeconds);
        motionEma_ += coefficient * (instantaneousMotion - motionEma_);
        motionEma_ = clampUnit(motionEma_);
    }
    output_.motion = motionEma_;

    const MacroValues currentValues {
        toMidi7Bit(output_.density),
        toMidi7Bit(output_.centroidX),
        toMidi7Bit(output_.centroidY),
        toMidi7Bit(output_.motion)
    };
    output_.values = currentValues;
    output_.emitDue = false;
    output_.changedMask = ChangedNone;
    output_.clockValid = finiteClock;

    if (! config.enabled)
    {
        emissionEnabledLastUpdate_ = false;
        return output_;
    }

    if (! finiteClock)
        return output_;

    bool forceCompleteEmission = false;
    if (! emissionEnabledLastUpdate_)
    {
        emissionEnabledLastUpdate_ = true;
        output_.emitDue = true;
        forceCompleteEmission = true;
    }
    else if (! clockRebased)
    {
        const double periodSeconds = 1.0 / config.rateHz;
        output_.emitDue = ! hasEmitted_
                       || input.monotonicSeconds - lastEmitTimeSeconds_
                            + kScheduleEpsilonSeconds >= periodSeconds;
    }

    if (output_.emitDue)
    {
        output_.changedMask = forceCompleteEmission || ! hasEmitted_
                            ? ChangedAll
                            : changedBits(currentValues, lastEmittedValues_);
        lastEmittedValues_ = currentValues;
        lastEmitTimeSeconds_ = input.monotonicSeconds;
        hasEmitted_ = true;
    }

    return output_;
}

void CrowdExpressionMacros::reset() noexcept
{
    previousX_.fill(0.0f);
    previousY_.fill(0.0f);
    previousPositionValid_.fill(0u);
    output_ = {};
    output_.values.centroidX = 64;
    output_.values.centroidY = 64;
    output_.centroidX = 0.5;
    output_.centroidY = 0.5;
    output_.clockValid = true;
    lastEmittedValues_ = {};
    motionEma_ = 0.0;
    lastUpdateTimeSeconds_ = 0.0;
    lastEmitTimeSeconds_ = 0.0;
    clockInitialised_ = false;
    motionInitialised_ = false;
    emissionEnabledLastUpdate_ = false;
    hasEmitted_ = false;
}

double CrowdExpressionMacros::clampUnit (double value) noexcept
{
    return std::max(0.0, std::min(1.0, finiteOr(value, 0.0)));
}

std::uint8_t CrowdExpressionMacros::toMidi7Bit (double value) noexcept
{
    const double scaled = std::floor(clampUnit(value) * 127.0 + 0.5);
    return static_cast<std::uint8_t>(scaled);
}

bool CrowdExpressionMacros::sameValues (const MacroValues& first,
                                         const MacroValues& second) noexcept
{
    return first.density == second.density
        && first.centroidX == second.centroidX
        && first.centroidY == second.centroidY
        && first.motion == second.motion;
}

std::uint8_t CrowdExpressionMacros::changedBits (
    const MacroValues& current, const MacroValues& previous) noexcept
{
    if (sameValues(current, previous))
        return ChangedNone;

    std::uint8_t result = ChangedNone;
    if (current.density != previous.density)
        result = static_cast<std::uint8_t>(result | ChangedDensity);
    if (current.centroidX != previous.centroidX)
        result = static_cast<std::uint8_t>(result | ChangedCentroidX);
    if (current.centroidY != previous.centroidY)
        result = static_cast<std::uint8_t>(result | ChangedCentroidY);
    if (current.motion != previous.motion)
        result = static_cast<std::uint8_t>(result | ChangedMotion);
    return result;
}
