#include "AdaptiveCrowdGovernor.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double kResponseToNinetyFivePercent = 3.0;
    constexpr double kMaxUpdateDeltaSeconds = 1.0;

    double finiteOr (double value, double fallback) noexcept
    {
        return std::isfinite(value) ? value : fallback;
    }

    double clampDouble (double value, double minimum, double maximum,
                        double fallback) noexcept
    {
        return std::max(minimum, std::min(maximum, finiteOr(value, fallback)));
    }
}
AdaptiveCrowdGovernor::AdaptiveCrowdGovernor() noexcept
{
    reset();
}

AdaptiveCrowdGovernor::Config AdaptiveCrowdGovernor::sanitiseConfig (
    const Config& requested) noexcept
{
    Config result;
    result.voiceLimit = std::max(1, std::min(16, requested.voiceLimit));
    result.riseSeconds = clampDouble(requested.riseSeconds, 0.05, 60.0, 0.5);
    result.fallSeconds = clampDouble(requested.fallSeconds, 0.05, 120.0, 6.0);
    result.promotionHoldSeconds = clampDouble(requested.promotionHoldSeconds,
                                               0.0, 30.0, 0.5);
    result.demotionHoldSeconds = clampDouble(requested.demotionHoldSeconds,
                                              0.0, 60.0, 4.0);
    result.demotionHysteresis = clampDouble(requested.demotionHysteresis,
                                             0.0, 0.49, 0.20);
    return result;
}

AdaptiveCrowdGovernor::Profile AdaptiveCrowdGovernor::profileForDensity (
    int density, int voiceLimit) noexcept
{
    density = clampDensity(density);
    voiceLimit = std::max(1, std::min(16, voiceLimit));

    Profile result;
    if (density <= 8)
        result = { 0, 4, 1, 8 };
    else if (density <= 24)
        result = { 1, 4, 2, 10 };
    else if (density <= 64)
        result = { 2, 3, 4, 12 };
    else if (density <= 128)
        result = { 3, 2, 8, 14 };
    else
        result = { 4, 2, 16, 16 };

    result.maxActive = std::min(result.maxActive, voiceLimit);
    return result;
}

AdaptiveCrowdGovernor::Output AdaptiveCrowdGovernor::update (
    const Config& requestedConfig, const Input& input) noexcept
{
    const auto config = sanitiseConfig(requestedConfig);
    const int held = clampDensity(input.heldSources);
    const int recent = clampDensity(input.recentUniqueSources);
    output_.observedDensity = std::max(held, recent);

    double elapsed = 0.0;
    if (std::isfinite(input.monotonicSeconds))
    {
        if (! clockInitialised_)
        {
            lastTimeSeconds_ = input.monotonicSeconds;
            clockInitialised_ = true;
        }
        else if (input.monotonicSeconds >= lastTimeSeconds_)
        {
            elapsed = std::min(kMaxUpdateDeltaSeconds,
                               input.monotonicSeconds - lastTimeSeconds_);
            lastTimeSeconds_ = input.monotonicSeconds;
        }
        else
        {
            // Treat a reset host clock as a new epoch without crediting the
            // discontinuity to either debounce timer.
            lastTimeSeconds_ = input.monotonicSeconds;
        }
    }

    if (! envelopeInitialised_)
    {
        output_.smoothedDensity = static_cast<double>(output_.observedDensity);
        envelopeInitialised_ = true;
    }
    else if (elapsed > 0.0)
    {
        const double target = static_cast<double>(output_.observedDensity);
        const double response = target >= output_.smoothedDensity
                              ? config.riseSeconds : config.fallSeconds;
        const double coefficient = responseCoefficient(elapsed, response);
        output_.smoothedDensity += coefficient * (target - output_.smoothedDensity);
        output_.smoothedDensity = std::max(0.0,
                                           std::min(static_cast<double>(kMaxCrowdSize),
                                                    output_.smoothedDensity));
    }

    const auto previous = output_;
    considerBandTransition(config, elapsed);
    applyCurrentProfile(config.voiceLimit);
    output_.changed = previous.band != output_.band
                   || previous.maxAttacksPerStep != output_.maxAttacksPerStep
                   || previous.spreadSlots != output_.spreadSlots
                   || previous.maxActive != output_.maxActive;
    return output_;
}

void AdaptiveCrowdGovernor::reset() noexcept
{
    output_ = {};
    const auto low = profileForDensity(0, 16);
    output_.band = low.band;
    output_.maxAttacksPerStep = low.maxAttacksPerStep;
    output_.spreadSlots = low.spreadSlots;
    output_.maxActive = low.maxActive;
    currentBand_ = 0;
    lastTimeSeconds_ = 0.0;
    clockInitialised_ = false;
    envelopeInitialised_ = false;
    clearCandidate();
}

int AdaptiveCrowdGovernor::clampDensity (int value) noexcept
{
    return std::max(0, std::min(kMaxCrowdSize, value));
}

double AdaptiveCrowdGovernor::responseCoefficient (
    double elapsedSeconds, double responseSeconds) noexcept
{
    if (! std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0)
        return 0.0;

    const double exponent = -kResponseToNinetyFivePercent
                          * elapsedSeconds / responseSeconds;
    return std::max(0.0, std::min(1.0, -std::expm1(exponent)));
}

int AdaptiveCrowdGovernor::lowerEdgeForBand (int band) noexcept
{
    switch (band)
    {
        case 1:  return 9;
        case 2:  return 25;
        case 3:  return 65;
        case 4:  return 129;
        default: return 0;
    }
}

bool AdaptiveCrowdGovernor::sameRecommendations (
    const Output& output, const Profile& profile) noexcept
{
    return output.band == profile.band
        && output.maxAttacksPerStep == profile.maxAttacksPerStep
        && output.spreadSlots == profile.spreadSlots
        && output.maxActive == profile.maxActive;
}

void AdaptiveCrowdGovernor::clearCandidate() noexcept
{
    candidateBand_ = -1;
    candidateElapsedSeconds_ = 0.0;
}

void AdaptiveCrowdGovernor::considerBandTransition (
    const Config& config, double elapsedSeconds) noexcept
{
    const int roundedDensity = clampDensity(
        static_cast<int>(std::floor(output_.smoothedDensity + 0.5)));
    const int tableBand = profileForDensity(roundedDensity, config.voiceLimit).band;

    int desiredBand = currentBand_;
    double requiredHold = 0.0;

    if (tableBand > currentBand_)
    {
        desiredBand = tableBand;
        requiredHold = config.promotionHoldSeconds;
    }
    else if (tableBand < currentBand_)
    {
        const double lowerEdge = static_cast<double>(lowerEdgeForBand(currentBand_));
        const double demotionThreshold = lowerEdge
                                       * (1.0 - config.demotionHysteresis);
        if (output_.smoothedDensity < demotionThreshold)
        {
            desiredBand = tableBand;
            requiredHold = config.demotionHoldSeconds;
        }
    }

    if (desiredBand == currentBand_)
    {
        clearCandidate();
        return;
    }

    if (candidateBand_ != desiredBand)
    {
        candidateBand_ = desiredBand;
        candidateElapsedSeconds_ = 0.0;
    }
    else
    {
        candidateElapsedSeconds_ = std::min(60.0,
                                             candidateElapsedSeconds_
                                               + std::max(0.0, elapsedSeconds));
    }

    if (requiredHold <= 0.0 || candidateElapsedSeconds_ + 1.0e-12 >= requiredHold)
    {
        currentBand_ = desiredBand;
        clearCandidate();
    }
}

void AdaptiveCrowdGovernor::applyCurrentProfile (int voiceLimit) noexcept
{
    // Use one representative density from the selected band so the stateless
    // table remains the single source of truth for all recommendations.
    const auto profile = profileForDensity(lowerEdgeForBand(currentBand_), voiceLimit);
    if (! sameRecommendations(output_, profile))
    {
        output_.band = profile.band;
        output_.maxAttacksPerStep = profile.maxAttacksPerStep;
        output_.spreadSlots = profile.spreadSlots;
        output_.maxActive = profile.maxActive;
    }
}
