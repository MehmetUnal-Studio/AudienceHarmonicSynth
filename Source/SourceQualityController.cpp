#include "SourceQualityController.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    constexpr std::uint32_t halfClockRange = 0x80000000u;

    double finiteOr (double value, double fallback) noexcept
    {
        return std::isfinite(value) ? value : fallback;
    }
}

SourceQualityController::SourceQualityController() noexcept
{
    output_.state = State::BYPASS;
    output_.admissionOpen = true;
}

void SourceQualityController::observeU (int sourceId, int finger,
                                        std::uint32_t nowMs) noexcept
{
    observeMotion(sourceId, finger, nowMs, true);
}

void SourceQualityController::observeV (int sourceId, int finger,
                                        std::uint32_t nowMs) noexcept
{
    observeMotion(sourceId, finger, nowMs, false);
}

void SourceQualityController::observeMotion (int sourceId, int finger,
                                             std::uint32_t nowMs,
                                             bool isU) noexcept
{
    if (! validIdentity(sourceId, finger))
        return;

    auto& source = sources_[(size_t) sourceId];
    incrementSaturating(isU ? source.uCount : source.vCount);
    (isU ? source.lastUMs : source.lastVMs)
        .store(nowMs, std::memory_order_release);
    source.lastEventMs.store(nowMs, std::memory_order_release);
}

void SourceQualityController::observeOn (int sourceId, int finger,
                                         std::uint32_t nowMs) noexcept
{
    if (! validIdentity(sourceId, finger))
        return;

    auto& source = sources_[(size_t) sourceId];
    const auto bit = static_cast<std::uint16_t>(
        1u << static_cast<unsigned int>(finger));
    const auto previous = source.activeFingerMask.fetch_or(
        bit, std::memory_order_acq_rel);
    incrementSaturating(source.onCount);
    if ((previous & bit) != 0)
        incrementSaturating(source.duplicateOnCount);
    source.lastEventMs.store(nowMs, std::memory_order_release);
}

void SourceQualityController::observeOff (int sourceId, int finger,
                                          std::uint32_t nowMs) noexcept
{
    if (! validIdentity(sourceId, finger))
        return;

    auto& source = sources_[(size_t) sourceId];
    const auto bit = static_cast<std::uint16_t>(
        1u << static_cast<unsigned int>(finger));
    const auto previous = source.activeFingerMask.fetch_and(
        static_cast<std::uint16_t>(~bit), std::memory_order_acq_rel);
    incrementSaturating(source.offCount);
    if ((previous & bit) == 0)
        incrementSaturating(source.orphanOffCount);
    source.lastEventMs.store(nowMs, std::memory_order_release);
}

void SourceQualityController::observeWatchdogCancel (
    int sourceId, int finger, std::uint32_t nowMs) noexcept
{
    if (! validIdentity(sourceId, finger))
        return;

    auto& source = sources_[(size_t) sourceId];
    const auto bit = static_cast<std::uint16_t>(
        1u << static_cast<unsigned int>(finger));
    source.activeFingerMask.fetch_and(
        static_cast<std::uint16_t>(~bit), std::memory_order_acq_rel);
    incrementSaturating(source.watchdogCancelCount);
    source.lastEventMs.store(nowMs, std::memory_order_release);
}

void SourceQualityController::clearLiveState() noexcept
{
    clearLiveStateForSources(0, MAX_SOURCES);
}

void SourceQualityController::clearLiveStateForSources (
    int firstSource, int pastLastSource) noexcept
{
    firstSource = std::max(0, std::min(MAX_SOURCES, firstSource));
    pastLastSource = std::max(firstSource,
                              std::min(MAX_SOURCES, pastLastSource));
    for (int sourceId = firstSource; sourceId < pastLastSource; ++sourceId)
    {
        auto& source = sources_[(size_t) sourceId];
        source.activeFingerMask.store(0, std::memory_order_release);
        source.lastUMs.store(0, std::memory_order_release);
        source.lastVMs.store(0, std::memory_order_release);
        source.lastEventMs.store(0, std::memory_order_release);
    }
}

void SourceQualityController::arm (std::uint32_t nowMs,
                                   const ExternalCounters& counters) noexcept
{
    armed_ = true;
    readyLatched_ = false;
    hardFaultLatched_ = false;
    captureEpochBaselines(nowMs, counters);
    output_ = {};
    output_.state = State::WARMING;
    output_.armed = true;
    output_.admissionOpen = false;
}

void SourceQualityController::disarm() noexcept
{
    armed_ = false;
    readyLatched_ = false;
    hardFaultLatched_ = false;
    readinessCandidateActive_ = false;
    rateClockInitialised_ = false;
    output_ = {};
    output_.state = State::BYPASS;
    output_.admissionOpen = true;
}

void SourceQualityController::restartEpoch (
    std::uint32_t nowMs, const ExternalCounters& counters) noexcept
{
    if (armed_)
        arm(nowMs, counters);
    else
        disarm();
}

SourceQualityController::Config SourceQualityController::sanitiseConfig (
    const Config& input) noexcept
{
    Config result = input;
    result.expectedSources = std::max(1, std::min(MAX_SOURCES,
                                                  input.expectedSources));
    result.activeHeartbeatMaxAgeMs = std::max(
        std::uint32_t { 100 }, std::min(std::uint32_t { 3000 },
                                        input.activeHeartbeatMaxAgeMs));
    result.readinessHoldMs = std::min(std::uint32_t { 30000 },
                                      input.readinessHoldMs);
    result.rateSampleWindowMs = std::max(
        std::uint32_t { 100 }, std::min(std::uint32_t { 5000 },
                                        input.rateSampleWindowMs));
    result.maxMotionEventsPerSecond = std::max(
        1.0, finiteOr(input.maxMotionEventsPerSecond, 50.0));
    result.maxTotalMotionEventsPerSecond = std::max(
        result.maxMotionEventsPerSecond,
        finiteOr(input.maxTotalMotionEventsPerSecond, 1200.0));
    result.maxTopTalkerShare = std::max(
        0.05, std::min(1.0, finiteOr(input.maxTopTalkerShare, 0.35)));
    return result;
}

SourceQualityController::Output SourceQualityController::update (
    const Config& requestedConfig, const ExternalCounters& counters,
    std::uint32_t nowMs) noexcept
{
    const auto config = sanitiseConfig(requestedConfig);
    if (! armed_)
    {
        output_.expectedSources = config.expectedSources;
        output_.state = State::BYPASS;
        output_.reasonBits = ReasonNone;
        output_.armed = false;
        output_.readyLatched = false;
        output_.admissionOpen = true;
        output_.serverRosterKnown = false;
        return output_;
    }

    Output next;
    next.expectedSources = config.expectedSources;
    next.armed = true;
    next.simulatorActive = config.simulatorActive;
    next.serverRosterKnown = false;

    const bool rateElapsedValid = ! rateClockInitialised_
        || elapsedIsValid(nowMs, lastRateSampleMs_);
    if (! rateElapsedValid)
    {
        next.reasonBits |= ReasonInvalidClock;
        rateClockInitialised_ = false;
        readinessCandidateActive_ = false;
        for (auto& rate : motionRate_)
            rate = 0.0;
    }

    const auto rateElapsedMs = rateClockInitialised_
        ? elapsedSince(nowMs, lastRateSampleMs_) : 0u;
    const bool sampleRates = ! rateClockInitialised_
                          || rateElapsedMs >= config.rateSampleWindowMs;

    std::uint64_t duplicateOnTotal = 0;
    std::uint64_t orphanOffTotal = 0;
    std::uint64_t cancelTotal = 0;
    double totalRate = 0.0;
    double maxRate = 0.0;

    for (int sourceId = 0; sourceId < config.expectedSources; ++sourceId)
    {
        const auto current = readCounters(sourceId);
        const auto& baseline = epochBaseline_[(size_t) sourceId];
        const auto uDelta = deltaSince(current.u, baseline.u);
        const auto vDelta = deltaSince(current.v, baseline.v);
        const auto onDelta = deltaSince(current.on, baseline.on);
        const auto offDelta = deltaSince(current.off, baseline.off);
        const auto duplicateDelta = deltaSince(current.duplicateOn,
                                               baseline.duplicateOn);
        const auto orphanDelta = deltaSince(current.orphanOff,
                                             baseline.orphanOff);
        const auto sourceCancelDelta = deltaSince(current.cancel,
                                                   baseline.cancel);

        const bool observed = uDelta != 0u || vDelta != 0u || onDelta != 0u
                           || offDelta != 0u || duplicateDelta != 0u
                           || orphanDelta != 0u || sourceCancelDelta != 0u;
        if (observed)
            ++next.observedSources;
        if (uDelta != 0u && vDelta != 0u && onDelta != 0u)
            ++next.qualifiedSources;

        duplicateOnTotal += duplicateDelta;
        orphanOffTotal += orphanDelta;
        cancelTotal += sourceCancelDelta;

        const auto activeMask = sources_[(size_t) sourceId]
                                    .activeFingerMask.load(
                                        std::memory_order_acquire);
        if (activeMask != 0)
        {
            ++next.activeSources;
            const auto lastU = sources_[(size_t) sourceId]
                                   .lastUMs.load(std::memory_order_acquire);
            const auto lastV = sources_[(size_t) sourceId]
                                   .lastVMs.load(std::memory_order_acquire);
            const auto uAge = elapsedSince(nowMs, lastU);
            const auto vAge = elapsedSince(nowMs, lastV);
            const auto age = std::max(uAge, vAge);
            next.maxActiveHeartbeatAgeMs = std::max(
                next.maxActiveHeartbeatAgeMs, age);
            const bool hasBothAxes = current.u != 0u && current.v != 0u;
            if (! hasBothAxes || ! elapsedIsValid(nowMs, lastU)
                || ! elapsedIsValid(nowMs, lastV)
                || uAge > config.activeHeartbeatMaxAgeMs
                || vAge > config.activeHeartbeatMaxAgeMs)
                ++next.staleActiveSources;
        }

        if (sampleRates)
        {
            const auto previous = lastRateCounters_[(size_t) sourceId];
            const std::uint64_t motionDelta =
                static_cast<std::uint64_t>(deltaSince(current.u, previous.u))
                + static_cast<std::uint64_t>(deltaSince(current.v, previous.v));
            const double seconds = rateClockInitialised_
                ? std::max(0.001, (double) rateElapsedMs * 0.001)
                : std::max(0.001, (double) config.rateSampleWindowMs * 0.001);
            const double instant = static_cast<double>(motionDelta) / seconds;
            const double previousRate = motionRate_[(size_t) sourceId];
            motionRate_[(size_t) sourceId] = rateClockInitialised_
                ? previousRate * 0.65 + instant * 0.35 : instant;
            lastRateCounters_[(size_t) sourceId] = current;
        }

        const double rate = std::max(0.0, motionRate_[(size_t) sourceId]);
        totalRate += rate;
        maxRate = std::max(maxRate, rate);
        if (rate > config.maxMotionEventsPerSecond)
            ++next.hotSources;
    }

    if (sampleRates)
    {
        lastRateSampleMs_ = nowMs;
        rateClockInitialised_ = true;
    }

    const auto clampCounter = [] (std::uint64_t value) noexcept
    {
        return static_cast<std::uint32_t>(std::min<std::uint64_t>(
            value, std::numeric_limits<std::uint32_t>::max()));
    };
    next.duplicateOnCount = clampCounter(duplicateOnTotal);
    next.orphanOffCount = clampCounter(orphanOffTotal);
    next.watchdogCancelCount = clampCounter(cancelTotal);
    next.capacityDropCount = deltaSince(counters.capacityDroppedEvents,
                                        externalBaseline_.capacityDroppedEvents);
    next.motionDropCount = deltaSince(counters.motionDroppedEvents,
                                      externalBaseline_.motionDroppedEvents);
    next.lifecycleDropCount = deltaSince(
        counters.lifecycleDroppedEvents,
        externalBaseline_.lifecycleDroppedEvents);
    next.totalMotionEventsPerSecond = totalRate;
    next.maxSourceMotionEventsPerSecond = maxRate;
    next.topTalkerShare = totalRate > 0.0 ? maxRate / totalRate : 0.0;
    next.aggregateRateHigh = totalRate
                          > config.maxTotalMotionEventsPerSecond;

    if (next.observedSources < config.expectedSources)
        next.reasonBits |= ReasonCoverageMissing;
    if (next.qualifiedSources < config.expectedSources)
        next.reasonBits |= ReasonSignalIncomplete;
    if (! readyLatched_ && config.requireAllSourcesActiveForReady
        && next.activeSources < config.expectedSources)
        next.reasonBits |= ReasonActiveCoverageMissing;
    if (next.staleActiveSources > 0)
        next.reasonBits |= ReasonHeartbeatStale;
    if (next.hotSources > 0
        || (next.activeSources >= 8
            && next.topTalkerShare > config.maxTopTalkerShare))
        next.reasonBits |= ReasonHotSource;
    if (next.aggregateRateHigh)
        next.reasonBits |= ReasonAggregateRate;
    if (next.simulatorActive)
        next.reasonBits |= ReasonSimulatorActive;
    if (duplicateOnTotal != 0u || orphanOffTotal != 0u || cancelTotal != 0u)
        next.reasonBits |= ReasonLifecycleAnomaly;
    if (next.capacityDropCount != 0u)
        next.reasonBits |= ReasonCapacityDrop;
    if (next.lifecycleDropCount != 0u)
        next.reasonBits |= ReasonQueueDrop;
    if (next.motionDropCount != 0u)
        next.reasonBits |= ReasonMotionDrop;

    if (next.capacityDropCount != 0u || next.lifecycleDropCount != 0u)
        hardFaultLatched_ = true;

    constexpr std::uint32_t candidateBlockingReasons =
        ReasonCoverageMissing | ReasonSignalIncomplete
        | ReasonHeartbeatStale | ReasonHotSource
        | ReasonLifecycleAnomaly | ReasonCapacityDrop | ReasonQueueDrop
        | ReasonInvalidClock | ReasonActiveCoverageMissing
        | ReasonAggregateRate | ReasonSimulatorActive | ReasonMotionDrop;
    const bool qualityCandidate = next.observedSources == config.expectedSources
                               && next.qualifiedSources == config.expectedSources
                               && ! hardFaultLatched_
                               && (next.reasonBits
                                   & candidateBlockingReasons) == 0u;
    if (! readyLatched_)
    {
        if (qualityCandidate)
        {
            if (! readinessCandidateActive_)
            {
                readinessCandidateActive_ = true;
                readinessCandidateSinceMs_ = nowMs;
            }

            const auto heldMs = elapsedIsValid(nowMs, readinessCandidateSinceMs_)
                ? elapsedSince(nowMs, readinessCandidateSinceMs_) : 0u;
            next.readinessHoldProgress = config.readinessHoldMs == 0u
                ? 1.0
                : std::min(1.0, (double) heldMs
                                  / (double) config.readinessHoldMs);
            if (heldMs >= config.readinessHoldMs)
                readyLatched_ = true;
            else
                next.reasonBits |= ReasonRecoveryHeld;
        }
        else
        {
            readinessCandidateActive_ = false;
            next.readinessHoldProgress = 0.0;
        }
    }
    else
    {
        next.readinessHoldProgress = 1.0;
    }

    next.readyLatched = readyLatched_;
    next.admissionOpen = readyLatched_ && ! hardFaultLatched_;
    constexpr std::uint32_t softDegradationReasons =
        ReasonHeartbeatStale | ReasonHotSource | ReasonLifecycleAnomaly
        | ReasonInvalidClock | ReasonAggregateRate
        | ReasonSimulatorActive | ReasonMotionDrop;
    const bool currentSoftDegradation =
        (next.reasonBits & softDegradationReasons) != 0u;
    next.state = hardFaultLatched_ || (readyLatched_ && currentSoftDegradation)
               ? State::DEGRADED
               : readyLatched_ ? State::READY : State::WARMING;

    output_ = next;
    return output_;
}

bool SourceQualityController::validIdentity (int sourceId, int finger) noexcept
{
    return sourceId >= 0 && sourceId < MAX_SOURCES
        && finger >= 0 && finger < MAX_FINGERS;
}

void SourceQualityController::incrementSaturating (
    std::atomic<std::uint32_t>& counter) noexcept
{
    auto current = counter.load(std::memory_order_relaxed);
    while (current != std::numeric_limits<std::uint32_t>::max())
    {
        if (counter.compare_exchange_weak(current, current + 1,
                                          std::memory_order_relaxed,
                                          std::memory_order_relaxed))
            return;
    }
}

std::uint32_t SourceQualityController::deltaSince (
    std::uint32_t current, std::uint32_t baseline) noexcept
{
    return current >= baseline ? current - baseline : 0u;
}

bool SourceQualityController::elapsedIsValid (
    std::uint32_t now, std::uint32_t before) noexcept
{
    return static_cast<std::uint32_t>(now - before) < halfClockRange;
}

std::uint32_t SourceQualityController::elapsedSince (
    std::uint32_t now, std::uint32_t before) noexcept
{
    const auto elapsed = static_cast<std::uint32_t>(now - before);
    return elapsed < halfClockRange ? elapsed
                                    : std::numeric_limits<std::uint32_t>::max();
}

SourceQualityController::CounterSnapshot
SourceQualityController::readCounters (int sourceId) const noexcept
{
    CounterSnapshot result;
    if (sourceId < 0 || sourceId >= MAX_SOURCES)
        return result;

    const auto& source = sources_[(size_t) sourceId];
    result.u = source.uCount.load(std::memory_order_acquire);
    result.v = source.vCount.load(std::memory_order_acquire);
    result.on = source.onCount.load(std::memory_order_acquire);
    result.off = source.offCount.load(std::memory_order_acquire);
    result.duplicateOn = source.duplicateOnCount.load(std::memory_order_acquire);
    result.orphanOff = source.orphanOffCount.load(std::memory_order_acquire);
    result.cancel = source.watchdogCancelCount.load(std::memory_order_acquire);
    return result;
}

void SourceQualityController::captureEpochBaselines (
    std::uint32_t nowMs, const ExternalCounters& counters) noexcept
{
    for (int sourceId = 0; sourceId < MAX_SOURCES; ++sourceId)
    {
        const auto current = readCounters(sourceId);
        epochBaseline_[(size_t) sourceId] = current;
        lastRateCounters_[(size_t) sourceId] = current;
        motionRate_[(size_t) sourceId] = 0.0;
    }
    externalBaseline_ = counters;
    lastRateSampleMs_ = nowMs;
    readinessCandidateSinceMs_ = nowMs;
    rateClockInitialised_ = true;
    readinessCandidateActive_ = false;
}
