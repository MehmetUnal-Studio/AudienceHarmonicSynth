#pragma once

#include <array>
#include <atomic>
#include <cstdint>

// Fixed-capacity quality census for accepted live OSC sources.
//
// Producers only publish counters/timestamps into atomics. The processor's
// message timer owns arm/disarm/update and the resulting Output. The audio
// callback never scans sources; it consumes one separately-published boolean.
class SourceQualityController final
{
public:
    static constexpr int MAX_SOURCES = 256;
    static constexpr int MAX_FINGERS = 10;

    enum class State : std::uint8_t
    {
        BYPASS = 0,
        WARMING,
        READY,
        DEGRADED
    };

    enum ReasonBits : std::uint32_t
    {
        ReasonNone = 0u,
        ReasonCoverageMissing = 1u << 0,
        ReasonSignalIncomplete = 1u << 1,
        ReasonHeartbeatStale = 1u << 2,
        ReasonHotSource = 1u << 3,
        ReasonLifecycleAnomaly = 1u << 4,
        ReasonCapacityDrop = 1u << 5,
        ReasonQueueDrop = 1u << 6,
        ReasonRecoveryHeld = 1u << 7,
        ReasonInvalidClock = 1u << 8,
        ReasonActiveCoverageMissing = 1u << 9,
        ReasonAggregateRate = 1u << 10,
        ReasonSimulatorActive = 1u << 11,
        ReasonMotionDrop = 1u << 12
    };

    struct Config
    {
        int expectedSources = 64;
        std::uint32_t activeHeartbeatMaxAgeMs = 1200;
        std::uint32_t readinessHoldMs = 2000;
        std::uint32_t rateSampleWindowMs = 500;
        double maxMotionEventsPerSecond = 50.0;
        double maxTotalMotionEventsPerSecond = 1200.0;
        double maxTopTalkerShare = 0.35;
        bool requireAllSourcesActiveForReady = true;
        bool simulatorActive = false;
    };

    struct ExternalCounters
    {
        std::uint32_t capacityDroppedEvents = 0;
        std::uint32_t motionDroppedEvents = 0;
        std::uint32_t lifecycleDroppedEvents = 0;
    };

    struct Output
    {
        State state = State::BYPASS;
        std::uint32_t reasonBits = ReasonNone;
        int expectedSources = 64;
        int observedSources = 0;
        int qualifiedSources = 0;
        int activeSources = 0;
        int staleActiveSources = 0;
        int hotSources = 0;
        std::uint32_t maxActiveHeartbeatAgeMs = 0;
        std::uint32_t duplicateOnCount = 0;
        std::uint32_t orphanOffCount = 0;
        std::uint32_t watchdogCancelCount = 0;
        std::uint32_t capacityDropCount = 0;
        std::uint32_t motionDropCount = 0;
        std::uint32_t lifecycleDropCount = 0;
        double totalMotionEventsPerSecond = 0.0;
        double maxSourceMotionEventsPerSecond = 0.0;
        double topTalkerShare = 0.0;
        double readinessHoldProgress = 0.0;
        bool aggregateRateHigh = false;
        bool simulatorActive = false;
        bool armed = false;
        bool readyLatched = false;
        bool admissionOpen = true;
        bool serverRosterKnown = false;
    };

    SourceQualityController() noexcept;

    SourceQualityController (const SourceQualityController&) = delete;
    SourceQualityController& operator= (const SourceQualityController&) = delete;

    // Network/control producer path. Every call is bounded and allocation-free.
    // Only events already admitted by MidiAudienceModel reach these methods;
    // simulator traffic deliberately does not.
    void observeU (int sourceId, int finger, std::uint32_t nowMs) noexcept;
    void observeV (int sourceId, int finger, std::uint32_t nowMs) noexcept;
    void observeOn (int sourceId, int finger, std::uint32_t nowMs) noexcept;
    void observeOff (int sourceId, int finger, std::uint32_t nowMs) noexcept;
    void observeWatchdogCancel (int sourceId, int finger,
                                std::uint32_t nowMs) noexcept;

    // Clears transient live ownership without erasing cumulative counters.
    // This is called under MidiAudienceModel's producer lock during Panic.
    void clearLiveState() noexcept;
    void clearLiveStateForSources (int firstSource,
                                   int pastLastSource) noexcept;

    // Message-thread-only control. Arm captures a new incident/census epoch;
    // previous traffic cannot make a newly armed gate READY. Restart preserves
    // the operator's armed/bypass choice across route or capacity boundaries.
    void arm (std::uint32_t nowMs, const ExternalCounters&) noexcept;
    void disarm() noexcept;
    void restartEpoch (std::uint32_t nowMs,
                       const ExternalCounters&) noexcept;

    // Message-thread-only evaluation. A READY result is latched for the epoch.
    // Soft quality warnings after the latch report DEGRADED but do not mute a
    // running show; hard capacity/lifecycle drops close admission until re-armed.
    Output update (const Config&, const ExternalCounters&,
                   std::uint32_t nowMs) noexcept;

    Output getOutput() const noexcept { return output_; }
    bool isArmed() const noexcept { return armed_; }

    static Config sanitiseConfig (const Config&) noexcept;
    static constexpr bool hasReason (std::uint32_t reasons,
                                     ReasonBits reason) noexcept
    {
        return (reasons & static_cast<std::uint32_t>(reason)) != 0u;
    }

private:
    struct SourceLedger
    {
        std::atomic<std::uint32_t> uCount { 0 };
        std::atomic<std::uint32_t> vCount { 0 };
        std::atomic<std::uint32_t> onCount { 0 };
        std::atomic<std::uint32_t> offCount { 0 };
        std::atomic<std::uint32_t> duplicateOnCount { 0 };
        std::atomic<std::uint32_t> orphanOffCount { 0 };
        std::atomic<std::uint32_t> watchdogCancelCount { 0 };
        std::atomic<std::uint32_t> lastEventMs { 0 };
        std::atomic<std::uint32_t> lastUMs { 0 };
        std::atomic<std::uint32_t> lastVMs { 0 };
        std::atomic<std::uint16_t> activeFingerMask { 0 };
    };

    struct CounterSnapshot
    {
        std::uint32_t u = 0;
        std::uint32_t v = 0;
        std::uint32_t on = 0;
        std::uint32_t off = 0;
        std::uint32_t duplicateOn = 0;
        std::uint32_t orphanOff = 0;
        std::uint32_t cancel = 0;
    };

    static bool validIdentity (int sourceId, int finger) noexcept;
    static void incrementSaturating (
        std::atomic<std::uint32_t>& counter) noexcept;
    static std::uint32_t deltaSince (std::uint32_t current,
                                     std::uint32_t baseline) noexcept;
    static bool elapsedIsValid (std::uint32_t now,
                                std::uint32_t before) noexcept;
    static std::uint32_t elapsedSince (std::uint32_t now,
                                       std::uint32_t before) noexcept;
    CounterSnapshot readCounters (int sourceId) const noexcept;
    void captureEpochBaselines (std::uint32_t nowMs,
                                const ExternalCounters&) noexcept;
    void observeMotion (int sourceId, int finger, std::uint32_t nowMs,
                        bool isU) noexcept;

    std::array<SourceLedger, MAX_SOURCES> sources_ {};

    // Message-thread-owned evaluation state.
    std::array<CounterSnapshot, MAX_SOURCES> epochBaseline_ {};
    std::array<CounterSnapshot, MAX_SOURCES> lastRateCounters_ {};
    std::array<double, MAX_SOURCES> motionRate_ {};
    ExternalCounters externalBaseline_ {};
    Output output_ {};
    std::uint32_t lastRateSampleMs_ = 0;
    std::uint32_t readinessCandidateSinceMs_ = 0;
    bool rateClockInitialised_ = false;
    bool readinessCandidateActive_ = false;
    bool hardFaultLatched_ = false;
    bool armed_ = false;
    bool readyLatched_ = false;
};
