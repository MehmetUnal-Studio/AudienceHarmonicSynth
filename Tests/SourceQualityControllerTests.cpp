#include "../Source/SourceQualityController.h"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace
{
    using Controller = SourceQualityController;

    int failures = 0;
    bool watchAllocations = false;
    std::size_t watchedAllocations = 0;

    void expect (bool condition, const char* name)
    {
        std::cout << (condition ? "PASS  " : "FAIL  ") << name << '\n';
        if (! condition)
            ++failures;
    }

    bool has (const Controller::Output& output,
              Controller::ReasonBits reason) noexcept
    {
        return Controller::hasReason(output.reasonBits, reason);
    }

    Controller::Config configFor (int expectedSources,
                                  std::uint32_t holdMs = 2000u) noexcept
    {
        Controller::Config config;
        config.expectedSources = expectedSources;
        config.readinessHoldMs = holdMs;
        return config;
    }

    void qualify (Controller& controller, int firstSource, int lastSource,
                  std::uint32_t nowMs, bool release = false) noexcept
    {
        for (int source = firstSource; source <= lastSource; ++source)
        {
            controller.observeU(source, 0, nowMs);
            controller.observeV(source, 0, nowMs);
            controller.observeOn(source, 0, nowMs);
            if (release)
                controller.observeOff(source, 0, nowMs);
        }
    }

    void heartbeat (Controller& controller, int firstSource, int lastSource,
                    std::uint32_t nowMs) noexcept
    {
        for (int source = firstSource; source <= lastSource; ++source)
        {
            controller.observeU(source, 0, nowMs);
            controller.observeV(source, 0, nowMs);
        }
    }
}

void* operator new (std::size_t size)
{
    if (watchAllocations)
        ++watchedAllocations;
    if (void* memory = std::malloc(size))
        return memory;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)
{
    if (watchAllocations)
        ++watchedAllocations;
    if (void* memory = std::malloc(size))
        return memory;
    throw std::bad_alloc();
}

void operator delete (void* memory) noexcept { std::free(memory); }
void operator delete[] (void* memory) noexcept { std::free(memory); }
void operator delete (void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[] (void* memory, std::size_t) noexcept { std::free(memory); }

int main()
{
    static_assert(std::is_nothrow_default_constructible<Controller>::value,
                  "quality controller construction must remain noexcept");
    static_assert(std::is_trivially_copyable<Controller::Output>::value,
                  "quality output must remain fixed plain data");
    static_assert(noexcept(std::declval<Controller&>().observeU(0, 0, 0u)),
                  "producer observation must remain noexcept");
    static_assert(noexcept(std::declval<Controller&>().update(
                      std::declval<const Controller::Config&>(),
                      std::declval<const Controller::ExternalCounters&>(), 0u)),
                  "quality evaluation must remain noexcept");

    // Existing projects remain audible until an operator explicitly arms the
    // check. Arming starts a fresh epoch and holds only new attacks.
    {
        Controller controller;
        auto output = controller.getOutput();
        const bool bypassSafe = output.state == Controller::State::BYPASS
                             && output.admissionOpen && ! output.armed;
        controller.arm(100u, {});
        output = controller.getOutput();
        expect(bypassSafe && output.state == Controller::State::WARMING
                   && output.armed && ! output.admissionOpen,
               "Ready Gate is explicit, runtime-only and fail-closed while warming");
    }

    // The production 64-source domain is exactly 0..63. Sixty-three complete
    // sources never pass; the final identity still needs the clean hold.
    {
        Controller controller;
        const auto config = configFor(64);
        controller.arm(100u, {});
        qualify(controller, 0, 62, 200u);
        auto output = controller.update(config, {}, 1000u);
        const bool missingHeld = output.state == Controller::State::WARMING
                              && output.observedSources == 63
                              && output.qualifiedSources == 63
                              && output.activeSources == 63
                              && ! output.admissionOpen
                              && has(output, Controller::ReasonCoverageMissing)
                              && has(output, Controller::ReasonSignalIncomplete)
                              && has(output, Controller::ReasonActiveCoverageMissing);

        qualify(controller, 63, 63, 1100u);
        output = controller.update(config, {}, 1100u);
        const bool finalStartsHold = output.state == Controller::State::WARMING
                                  && output.qualifiedSources == 64
                                  && output.activeSources == 64
                                  && ! output.admissionOpen;
        heartbeat(controller, 0, 63, 2100u);
        output = controller.update(config, {}, 3100u);
        expect(missingHeld && finalStartsHold
                   && output.state == Controller::State::READY
                   && output.readyLatched && output.admissionOpen
                   && output.observedSources == 64
                   && output.qualifiedSources == 64,
               "exact 0..63 U+V+On census needs 64 held sources plus a clean heartbeat hold");
    }

    // Seeing a complete U/V/On sequence once is not enough: every expected
    // source must still be held while the operator performs the preflight.
    {
        Controller controller;
        const auto config = configFor(2, 0u);
        controller.arm(100u, {});
        qualify(controller, 0, 0, 200u);
        qualify(controller, 1, 1, 200u, true);
        auto output = controller.update(config, {}, 200u);
        const bool releasedSourceBlocks = output.observedSources == 2
                                       && output.qualifiedSources == 2
                                       && output.activeSources == 1
                                       && output.state == Controller::State::WARMING
                                       && ! output.admissionOpen
                                       && has(output,
                                              Controller::ReasonActiveCoverageMissing);

        heartbeat(controller, 0, 1, 300u);
        controller.observeOn(1, 0, 300u);
        output = controller.update(config, {}, 300u);
        expect(releasedSourceBlocks
                   && output.activeSources == 2
                   && output.state == Controller::State::READY
                   && output.admissionOpen,
               "preflight requires every expected source to remain active");
    }

    // A common allocator error emits 1..64. Identity 64 is outside a 64-seat
    // capacity and must never compensate for the missing source 0.
    {
        Controller controller;
        const auto config = configFor(64);
        controller.arm(100u, {});
        qualify(controller, 1, 64, 200u);
        Controller::ExternalCounters counters;
        counters.capacityDroppedEvents = 4u;
        const auto output = controller.update(config, counters, 2500u);
        expect(output.observedSources == 63 && output.qualifiedSources == 63
                   && output.capacityDropCount == 4u
                   && output.state == Controller::State::DEGRADED
                   && ! output.admissionOpen
                   && has(output, Controller::ReasonCapacityDrop)
                   && has(output, Controller::ReasonCoverageMissing),
               "1..64 allocator drift cannot produce a false READY for domain 0..63");
    }

    // U and V are independent heartbeats. Repeating only one axis must not
    // hide a dead/stuck stream on the other axis.
    {
        Controller controller;
        const auto config = configFor(1, 0u);
        controller.arm(100u, {});
        qualify(controller, 0, 0, 200u);
        auto output = controller.update(config, {}, 200u);
        const bool becameReady = output.state == Controller::State::READY
                              && output.admissionOpen;

        controller.observeU(0, 0, 1000u);
        output = controller.update(config, {}, 1401u);
        expect(becameReady
                   && output.state == Controller::State::DEGRADED
                   && output.staleActiveSources == 1
                   && output.maxActiveHeartbeatAgeMs == 1201u
                   && output.admissionOpen
                   && has(output, Controller::ReasonHeartbeatStale),
               "fresh U cannot conceal a stale V heartbeat after READY");
    }

    // Held sources need continued U/V heartbeat. A post-READY soft incident is
    // reported without muting a running show and clears when health returns.
    {
        Controller controller;
        auto config = configFor(2);
        controller.arm(100u, {});
        qualify(controller, 0, 1, 200u, false);
        controller.update(config, {}, 200u);
        controller.observeU(0, 0, 1000u);
        controller.observeV(0, 0, 1000u);
        controller.observeU(1, 0, 1000u);
        controller.observeV(1, 0, 1000u);
        controller.observeU(0, 0, 2000u);
        controller.observeV(0, 0, 2000u);
        controller.observeU(1, 0, 2000u);
        controller.observeV(1, 0, 2000u);
        auto output = controller.update(config, {}, 2200u);
        const bool becameReady = output.state == Controller::State::READY
                              && output.admissionOpen;

        controller.observeU(0, 0, 3000u);
        controller.observeV(0, 0, 3000u);
        output = controller.update(config, {}, 3301u);
        const bool degradedSafely = output.state == Controller::State::DEGRADED
                                 && output.staleActiveSources == 1
                                 && output.admissionOpen
                                 && has(output, Controller::ReasonHeartbeatStale);

        controller.observeU(1, 0, 3400u);
        controller.observeV(1, 0, 3400u);
        output = controller.update(config, {}, 3400u);
        expect(becameReady && degradedSafely
                   && output.state == Controller::State::READY
                   && output.staleActiveSources == 0 && output.admissionOpen,
               "held-source heartbeat degradation is visible and release-safe after READY");
    }

    // Duplicate On, orphan Off and watchdog Cancel invalidate a warming epoch.
    // A fresh clean epoch can pass, while a later anomaly degrades telemetry
    // without cutting sound that is already running.
    {
        Controller controller;
        const auto config = configFor(1, 500u);
        controller.arm(100u, {});
        controller.observeU(0, 0, 200u);
        controller.observeV(0, 0, 200u);
        controller.observeOn(0, 0, 200u);
        controller.observeOn(0, 0, 201u);
        controller.observeOff(0, 0, 202u);
        controller.observeOff(0, 0, 203u);
        auto output = controller.update(config, {}, 1000u);
        const bool dirtyEpochHeld = output.state == Controller::State::WARMING
                                 && ! output.admissionOpen
                                 && output.duplicateOnCount == 1u
                                 && output.orphanOffCount == 1u
                                 && has(output, Controller::ReasonLifecycleAnomaly);

        controller.arm(1100u, {});
        qualify(controller, 0, 0, 1200u);
        controller.update(config, {}, 1200u);
        output = controller.update(config, {}, 1700u);
        const bool cleanEpochReady = output.state == Controller::State::READY
                                  && output.admissionOpen;
        controller.observeWatchdogCancel(0, 0, 1800u);
        output = controller.update(config, {}, 1800u);
        expect(dirtyEpochHeld && cleanEpochReady
                   && output.state == Controller::State::DEGRADED
                   && output.watchdogCancelCount == 1u
                   && output.admissionOpen,
               "lifecycle incidents block preflight but never hard-mute after READY");
    }

    // A coalescible motion-marker loss is visible but release-safe after READY.
    // A lifecycle loss is a hard fault because note ownership can no longer be
    // proven; that fault stays closed until a fresh baseline is armed.
    {
        Controller controller;
        const auto config = configFor(1, 0u);
        controller.arm(100u, {});
        qualify(controller, 0, 0, 200u);
        auto output = controller.update(config, {}, 200u);
        const bool initiallyReady = output.state == Controller::State::READY
                                 && output.admissionOpen;

        Controller::ExternalCounters dropped;
        dropped.motionDroppedEvents = 1u;
        output = controller.update(config, dropped, 300u);
        const bool motionDegradedOpen = output.state
                                            == Controller::State::DEGRADED
                                     && output.admissionOpen
                                     && output.motionDropCount == 1u
                                     && output.lifecycleDropCount == 0u
                                     && has(output, Controller::ReasonMotionDrop)
                                     && ! has(output, Controller::ReasonQueueDrop);

        dropped.lifecycleDroppedEvents = 1u;
        output = controller.update(config, dropped, 350u);
        const bool latchedClosed = output.state == Controller::State::DEGRADED
                                && ! output.admissionOpen
                                && output.motionDropCount == 1u
                                && output.lifecycleDropCount == 1u
                                && has(output, Controller::ReasonQueueDrop);

        controller.clearLiveState();
        controller.arm(400u, dropped);
        qualify(controller, 0, 0, 500u);
        output = controller.update(config, dropped, 500u);
        expect(initiallyReady && motionDegradedOpen && latchedClosed
                   && output.state == Controller::State::READY
                   && output.motionDropCount == 0u
                   && output.lifecycleDropCount == 0u
                   && output.admissionOpen,
               "motion drop stays soft while lifecycle drop latches closed until re-arm");
    }

    // Abnormally concentrated U/V traffic is diagnosed before it can be used
    // as a clean readiness sample.
    {
        Controller controller;
        auto config = configFor(1, 0u);
        config.rateSampleWindowMs = 100u;
        config.maxMotionEventsPerSecond = 10.0;
        controller.arm(100u, {});
        for (int index = 0; index < 20; ++index)
        {
            controller.observeU(0, 0, 200u);
            controller.observeV(0, 0, 200u);
        }
        controller.observeOn(0, 0, 200u);
        controller.observeOff(0, 0, 200u);
        const auto output = controller.update(config, {}, 200u);
        expect(output.hotSources == 1
                   && output.maxSourceMotionEventsPerSecond > 10.0
                   && output.state == Controller::State::WARMING
                   && ! output.admissionOpen
                   && has(output, Controller::ReasonHotSource),
               "per-source hot traffic is detected during readiness warmup");
    }

    // Concentration is an independent blocker. One source may dominate the
    // crowd even when every individual rate remains below its absolute limit.
    {
        Controller controller;
        auto config = configFor(8, 0u);
        config.rateSampleWindowMs = 100u;
        config.maxMotionEventsPerSecond = 1000.0;
        config.maxTotalMotionEventsPerSecond = 10000.0;
        config.maxTopTalkerShare = 0.35;
        controller.arm(100u, {});
        qualify(controller, 0, 7, 200u);
        for (int burst = 0; burst < 4; ++burst)
        {
            controller.observeU(0, 0, 200u);
            controller.observeV(0, 0, 200u);
        }

        const auto output = controller.update(config, {}, 200u);
        expect(output.activeSources == 8
                   && output.hotSources == 0
                   && output.topTalkerShare > 0.35
                   && output.maxSourceMotionEventsPerSecond < 1000.0
                   && output.state == Controller::State::WARMING
                   && ! output.admissionOpen
                   && has(output, Controller::ReasonHotSource),
               "top-talker share blocks READY without an individually hot source");
    }

    // A balanced crowd can overload the aggregate path without any single
    // participant crossing the per-source ceiling.
    {
        Controller controller;
        auto config = configFor(64, 0u);
        config.rateSampleWindowMs = 100u;
        config.maxMotionEventsPerSecond = 50.0;
        config.maxTotalMotionEventsPerSecond = 1200.0;
        controller.arm(100u, {});
        qualify(controller, 0, 63, 200u);
        for (int burst = 0; burst < 5; ++burst)
            heartbeat(controller, 0, 63, 200u);

        const auto output = controller.update(config, {}, 200u);
        expect(output.activeSources == 64
                   && output.hotSources == 0
                   && output.totalMotionEventsPerSecond > 1200.0
                   && output.aggregateRateHigh
                   && output.state == Controller::State::WARMING
                   && ! output.admissionOpen
                   && has(output, Controller::ReasonAggregateRate),
               "balanced aggregate traffic above 1200 events/s blocks READY");
    }

    // Simulator voices share the musical model and router, so preflight must
    // explicitly refuse certification until the simulator population is zero.
    {
        Controller controller;
        auto config = configFor(1, 0u);
        config.simulatorActive = true;
        controller.arm(100u, {});
        qualify(controller, 0, 0, 200u);
        auto output = controller.update(config, {}, 200u);
        const bool simulatorBlocked = output.simulatorActive
                                   && output.state == Controller::State::WARMING
                                   && ! output.admissionOpen
                                   && has(output,
                                          Controller::ReasonSimulatorActive);

        config.simulatorActive = false;
        output = controller.update(config, {}, 300u);
        expect(simulatorBlocked && ! output.simulatorActive
                   && output.state == Controller::State::READY
                   && output.admissionOpen,
               "active simulator blocks the signal census until cleared");
    }

    // A backward monotonic clock jump after READY is a soft telemetry fault:
    // it must be visible without cutting a running show.
    {
        Controller controller;
        const auto config = configFor(1, 0u);
        controller.arm(100u, {});
        qualify(controller, 0, 0, 200u);
        auto output = controller.update(config, {}, 200u);
        const bool becameReady = output.state == Controller::State::READY
                              && output.admissionOpen;

        output = controller.update(config, {}, 0u);
        expect(becameReady
                   && output.state == Controller::State::DEGRADED
                   && output.admissionOpen
                   && has(output, Controller::ReasonInvalidClock),
               "backward clock after READY degrades telemetry but leaves admission open");
    }

    // Millisecond wrap is valid modular time, not an invalid-clock incident.
    {
        Controller controller;
        const auto config = configFor(1);
        constexpr std::uint32_t start =
            std::numeric_limits<std::uint32_t>::max() - 1000u;
        controller.arm(start, {});
        qualify(controller, 0, 0, start + 100u);
        controller.update(config, {}, start + 100u);
        heartbeat(controller, 0, 0, start + 1100u);
        const auto output = controller.update(config, {}, start + 2100u);
        expect(output.state == Controller::State::READY
                   && output.admissionOpen
                   && ! has(output, Controller::ReasonInvalidClock),
               "clean hold and rate clock remain exact across uint32 wrap");
    }

    // Producer observations and the bounded 256-ledger scan remain free of
    // dynamic allocation, preserving the realtime handoff contract.
    {
        Controller controller;
        const auto config = configFor(64, 0u);
        controller.arm(100u, {});
        watchedAllocations = 0;
        watchAllocations = true;
        qualify(controller, 0, 63, 200u);
        const auto output = controller.update(config, {}, 200u);
        watchAllocations = false;
        expect(watchedAllocations == 0u && output.admissionOpen,
               "64-source observation and evaluation allocate no memory");
    }

    std::cout << "\nSummary: " << (failures == 0 ? "ok" : "failed") << '\n';
    return failures == 0 ? 0 : 1;
}
