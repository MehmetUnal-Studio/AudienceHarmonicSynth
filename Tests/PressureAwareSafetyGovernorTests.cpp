#include "../Source/PressureAwareSafetyGovernor.h"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace
{
    using Governor = PressureAwareSafetyGovernor;

    int failures = 0;
    bool watchAllocations = false;
    std::size_t watchedAllocations = 0;

    void expect (bool condition, const char* name)
    {
        std::cout << (condition ? "PASS  " : "FAIL  ") << name << '\n';
        if (! condition)
            ++failures;
    }

    bool has (const Governor::Output& output, Governor::ReasonBits reason)
    {
        return Governor::hasReason(output.reasonBits, reason);
    }

    bool approximately (double actual, double expected) noexcept
    {
        return std::abs(actual - expected) <= 1.0e-12;
    }

    Governor::Config quickRecoveryConfig()
    {
        Governor::Config config;
        config.recoveryHysteresis = 0.20;
        config.highRecoveryHoldSeconds = 0.50;
        config.criticalRecoveryHoldSeconds = 0.50;
        config.emergencyRecoveryHoldSeconds = 0.50;
        config.maxElapsedSeconds = 0.25;
        return config;
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
    static_assert(std::is_nothrow_default_constructible<Governor>::value,
                  "governor construction must remain noexcept");
    static_assert(std::is_trivially_copyable<Governor::Input>::value,
                  "input snapshots must remain fixed plain data");
    static_assert(std::is_trivially_copyable<Governor::Output>::value,
                  "output snapshots must remain fixed plain data");
    static_assert(noexcept(std::declval<Governor&>().update(
                      std::declval<const Governor::Config&>(),
                      std::declval<const Governor::Input&>())),
                  "governor update must remain noexcept");

    // Exact shedding policy, including a fail-closed invalid enum value.
    {
        const auto normal = Governor::profileForState(Governor::State::NORMAL);
        const auto high = Governor::profileForState(Governor::State::HIGH);
        const auto critical = Governor::profileForState(Governor::State::CRITICAL);
        const auto emergency = Governor::profileForState(Governor::State::EMERGENCY);
        const auto hostile = Governor::profileForState(
            static_cast<Governor::State>(255));
        expect(normal.motionUpdateDivisor == 1
                   && normal.maxAttacksCeiling == 16
                   && normal.maxActiveCeiling == 16 && normal.minSpread == 1
                   && normal.admitNewAttacks && normal.macrosEnabled
                   && high.motionUpdateDivisor == 2
                   && high.maxAttacksCeiling == 8
                   && high.maxActiveCeiling == 12 && high.minSpread == 2
                   && high.admitNewAttacks && high.macrosEnabled
                   && critical.motionUpdateDivisor == 4
                   && critical.maxAttacksCeiling == 2
                   && critical.maxActiveCeiling == 8 && critical.minSpread == 4
                   && critical.admitNewAttacks && ! critical.macrosEnabled
                   && emergency.motionUpdateDivisor == 8
                   && emergency.maxAttacksCeiling == 1
                   && emergency.maxActiveCeiling == 4 && emergency.minSpread == 8
                   && ! emergency.admitNewAttacks && ! emergency.macrosEnabled
                   && hostile.motionUpdateDivisor == emergency.motionUpdateDivisor
                   && ! hostile.admitNewAttacks,
               "all states expose the exact monotonic shedding policy");
    }

    // Each observation has an independent telemetry reason and can promote in
    // the same call. The most severe simultaneous signal wins.
    {
        const struct SignalCase
        {
            Governor::Input input;
            Governor::ReasonBits reason;
            Governor::State state;
        } cases[] {
            { { 1500.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 },
              Governor::ReasonIngressRate, Governor::State::HIGH },
            { { 0.0, 0.80, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 },
              Governor::ReasonLifecycleQueue, Governor::State::CRITICAL },
            { { 0.0, 0.0, 64.0, 0.0, 0.0, 0.0, 0.0, 0.0 },
              Governor::ReasonMotionDrop, Governor::State::EMERGENCY },
            { { 0.0, 0.0, 0.0, 0.60, 0.0, 0.0, 0.0, 0.0 },
              Governor::ReasonTimeFieldPending, Governor::State::HIGH },
            { { 0.0, 0.0, 0.0, 0.0, 0.80, 0.0, 0.0, 0.0 },
              Governor::ReasonExternalFifo, Governor::State::CRITICAL },
            { { 0.0, 0.0, 0.0, 0.0, 0.0, 0.250, 0.0, 0.0 },
              Governor::ReasonExternalFifoAge, Governor::State::EMERGENCY },
            { { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.65, 0.0 },
              Governor::ReasonProcessDeadline, Governor::State::HIGH }
        };

        bool exact = true;
        for (const auto& signalCase : cases)
        {
            Governor governor;
            const auto output = governor.update({}, signalCase.input);
            exact = exact && output.state == signalCase.state
                    && has(output, signalCase.reason)
                    && ! has(output, Governor::ReasonInvalidInput)
                    && output.changed;
        }

        Governor governor;
        Governor::Input mixed;
        mixed.ingressEventsPerSecond = 1500.0;
        mixed.processDeadlineRatio = 0.95;
        const auto output = governor.update({}, mixed);
        expect(exact && output.state == Governor::State::EMERGENCY
                   && has(output, Governor::ReasonIngressRate)
                   && has(output, Governor::ReasonProcessDeadline),
               "signals promote immediately, retain reasons, and worst wins");
    }

    // Pending is work the Time Field must be allowed to drain. A valid full
    // queue therefore applies the strict CRITICAL ceilings without closing
    // attack admission; malformed pressure still fails closed below.
    {
        Governor governor;
        Governor::Input input;
        input.timeFieldPendingPressure = 1.0;
        const auto output = governor.update({}, input);
        expect(output.state == Governor::State::CRITICAL
                   && output.admitNewAttacks
                   && has(output, Governor::ReasonTimeFieldPending)
                   && ! has(output, Governor::ReasonInvalidInput),
               "valid full pending pressure remains drainable at CRITICAL");
    }

    // A different signal may close admission at EMERGENCY while Time Field work
    // is still queued. Since valid pending pressure is capped at CRITICAL, it
    // must not prevent the EMERGENCY -> CRITICAL recovery step that reopens the
    // queue's only drain path.
    {
        Governor governor;
        const auto config = quickRecoveryConfig();
        Governor::Input input;
        input.lifecycleQueuePressure = 0.95;
        input.timeFieldPendingPressure = 1.0;
        auto output = governor.update(config, input);
        bool recovered = output.state == Governor::State::EMERGENCY
                      && ! output.admitNewAttacks
                      && has(output, Governor::ReasonLifecycleQueue)
                      && has(output, Governor::ReasonTimeFieldPending);

        input.lifecycleQueuePressure = 0.0;
        input.monotonicSeconds = 0.10;
        output = governor.update(config, input); // Starts the recovery hold.
        recovered = recovered && output.state == Governor::State::EMERGENCY;
        input.monotonicSeconds = 0.35;
        output = governor.update(config, input);
        recovered = recovered && output.state == Governor::State::EMERGENCY;
        input.monotonicSeconds = 0.60;
        output = governor.update(config, input);

        expect(recovered && output.state == Governor::State::CRITICAL
                   && output.admitNewAttacks
                   && has(output, Governor::ReasonTimeFieldPending)
                   && has(output, Governor::ReasonRecoveryHeld)
                   && ! has(output, Governor::ReasonLifecycleQueue),
               "capped pending pressure cannot latch an unrelated EMERGENCY");
    }

    // HIGH does not recover while merely below the entry threshold; it must be
    // below the 20% hysteresis boundary for the complete hold.
    {
        Governor governor;
        const auto config = quickRecoveryConfig();
        Governor::Input input;
        input.lifecycleQueuePressure = 0.60;
        auto output = governor.update(config, input);
        bool correct = output.state == Governor::State::HIGH;

        input.monotonicSeconds = 10.0;
        input.lifecycleQueuePressure = 0.50; // Above HIGH exit: 0.60 * 0.80.
        output = governor.update(config, input);
        correct = correct && output.state == Governor::State::HIGH
                  && has(output, Governor::ReasonRecoveryHeld);

        input.monotonicSeconds = 20.0;
        input.lifecycleQueuePressure = 0.47;
        output = governor.update(config, input); // Starts, but earns no history.
        correct = correct && output.state == Governor::State::HIGH;
        input.monotonicSeconds = 20.25;
        output = governor.update(config, input);
        correct = correct && output.state == Governor::State::HIGH;
        input.monotonicSeconds = 20.50;
        output = governor.update(config, input);
        expect(correct && output.state == Governor::State::NORMAL
                   && has(output, Governor::ReasonRecoveryHeld),
               "recovery requires hysteresis plus the complete hold");
    }

    // Even a completely safe snapshot may only demote one state for each hold.
    {
        Governor governor;
        const auto config = quickRecoveryConfig();
        Governor::Input input;
        input.processDeadlineRatio = 0.95;
        auto output = governor.update(config, input);
        bool stepped = output.state == Governor::State::EMERGENCY;

        input.processDeadlineRatio = 0.0;
        input.monotonicSeconds = 0.10;
        governor.update(config, input);
        input.monotonicSeconds = 0.35;
        governor.update(config, input);
        input.monotonicSeconds = 0.60;
        output = governor.update(config, input);
        stepped = stepped && output.state == Governor::State::CRITICAL;

        input.monotonicSeconds = 0.85;
        output = governor.update(config, input);
        stepped = stepped && output.state == Governor::State::CRITICAL;
        input.monotonicSeconds = 1.10;
        governor.update(config, input);
        input.monotonicSeconds = 1.35;
        output = governor.update(config, input);
        expect(stepped && output.state == Governor::State::HIGH,
               "recovery descends at most one state per completed hold");
    }

    // A renewed load interrupts recovery and escalates without a promotion hold.
    {
        Governor governor;
        const auto config = quickRecoveryConfig();
        Governor::Input input;
        input.lifecycleQueuePressure = 0.60;
        governor.update(config, input);
        input.lifecycleQueuePressure = 0.0;
        input.monotonicSeconds = 0.25;
        governor.update(config, input);
        input.monotonicSeconds = 0.40;
        governor.update(config, input);
        input.externalFifoPressure = 0.95;
        input.monotonicSeconds = 0.41;
        const auto output = governor.update(config, input);
        expect(output.state == Governor::State::EMERGENCY
                   && output.changed
                   && has(output, Governor::ReasonExternalFifo),
               "renewed pressure cancels recovery and escalates immediately");
    }

    // changed includes telemetry-reason changes, but is quiet for an identical
    // stable payload and for the initial all-clear baseline.
    {
        Governor governor;
        Governor::Input input;
        auto output = governor.update({}, input);
        const bool initialQuiet = ! output.changed;
        input.ingressEventsPerSecond = 1500.0;
        input.monotonicSeconds = 0.1;
        output = governor.update({}, input);
        const bool promoted = output.changed;
        input.monotonicSeconds = 0.2;
        output = governor.update({}, input);
        const bool stableQuiet = ! output.changed;
        input.ingressEventsPerSecond = 0.0;
        input.lifecycleQueuePressure = 0.60;
        input.monotonicSeconds = 0.3;
        output = governor.update({}, input);
        expect(initialQuiet && promoted && stableQuiet && output.changed
                   && output.state == Governor::State::HIGH
                   && has(output, Governor::ReasonLifecycleQueue),
               "changed reports state, control, and telemetry payload changes");
    }

    // Non-finite, negative and impossible bounded inputs fail closed. The
    // signal bit remains present beside the invalid-input reason.
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        Governor::Input cases[7];
        cases[0].ingressEventsPerSecond = nan;
        cases[1].lifecycleQueuePressure = -0.01;
        cases[2].motionDropDelta = inf;
        cases[3].timeFieldPendingPressure = 1.01;
        cases[4].externalFifoPressure = nan;
        cases[5].externalFifoOldestAgeSeconds = -1.0;
        cases[6].processDeadlineRatio = inf;
        const Governor::ReasonBits reasons[] {
            Governor::ReasonIngressRate, Governor::ReasonLifecycleQueue,
            Governor::ReasonMotionDrop, Governor::ReasonTimeFieldPending,
            Governor::ReasonExternalFifo, Governor::ReasonExternalFifoAge,
            Governor::ReasonProcessDeadline
        };

        bool failClosed = true;
        for (std::size_t index = 0; index < 7; ++index)
        {
            Governor governor;
            const auto output = governor.update({}, cases[index]);
            failClosed = failClosed
                      && output.state == Governor::State::EMERGENCY
                      && ! output.admitNewAttacks && ! output.macrosEnabled
                      && has(output, Governor::ReasonInvalidInput)
                      && has(output, reasons[index]);
        }
        expect(failClosed, "hostile signal values fail closed with exact reasons");
    }

    // Invalid or backward clocks cannot earn recovery and fail closed with a
    // dedicated clock reason. One huge valid jump is capped.
    {
        Governor governor;
        auto config = quickRecoveryConfig();
        config.emergencyRecoveryHoldSeconds = 1.0;
        Governor::Input input;
        input.monotonicSeconds = std::numeric_limits<double>::quiet_NaN();
        auto output = governor.update(config, input);
        bool safe = output.state == Governor::State::EMERGENCY
                 && has(output, Governor::ReasonInvalidClock)
                 && has(output, Governor::ReasonInvalidInput);

        input.monotonicSeconds = 1.0;
        governor.update(config, input); // Establishes a valid clock and candidate.
        input.monotonicSeconds = 1000.0;
        output = governor.update(config, input); // Credits only 0.25 s.
        safe = safe && output.state == Governor::State::EMERGENCY;
        input.monotonicSeconds = 0.5;
        output = governor.update(config, input);
        expect(safe && output.state == Governor::State::EMERGENCY
                   && has(output, Governor::ReasonInvalidClock),
               "clock faults fail closed and time jumps cannot bypass holds");
    }

    // Invalid threshold order/non-finite scalars fall back to finite, ordered
    // defaults instead of allowing protection to be disabled.
    {
        Governor::Config hostile;
        hostile.lifecycleQueuePressure = { 0.9, 0.2, 0.1 };
        hostile.processDeadlineRatio = {
            std::numeric_limits<double>::quiet_NaN(), 1.0, 2.0 };
        hostile.recoveryHysteresis = std::numeric_limits<double>::infinity();
        hostile.highRecoveryHoldSeconds = -1.0;
        hostile.maxElapsedSeconds = 100.0;
        const auto clean = Governor::sanitiseConfig(hostile);
        expect(approximately(clean.lifecycleQueuePressure.high, 0.60)
                   && approximately(clean.lifecycleQueuePressure.critical, 0.80)
                   && approximately(clean.lifecycleQueuePressure.emergency, 0.95)
                   && approximately(clean.processDeadlineRatio.high, 0.65)
                   && approximately(clean.recoveryHysteresis, 0.15)
                   && approximately(clean.highRecoveryHoldSeconds, 2.0)
                   && approximately(clean.maxElapsedSeconds, 0.25),
               "hostile configuration restores bounded safe defaults");
    }

    // Reset removes clock/recovery history and restores the unthrottled profile.
    {
        Governor governor;
        Governor::Input input;
        input.processDeadlineRatio = 1.0;
        governor.update({}, input);
        governor.reset();
        const auto output = governor.getOutput();
        expect(output.state == Governor::State::NORMAL
                   && output.motionUpdateDivisor == 1
                   && output.maxAttacksCeiling == 16
                   && output.maxActiveCeiling == 16 && output.minSpread == 1
                   && output.admitNewAttacks && output.macrosEnabled
                   && output.reasonBits == Governor::ReasonNone && ! output.changed,
               "reset restores the complete NORMAL baseline");
    }

    // The complete update path performs no heap allocation.
    {
        Governor governor;
        const Governor::Config config;
        Governor::Input input;
        input.lifecycleQueuePressure = 0.80;
        watchedAllocations = 0;
        watchAllocations = true;
        const auto output = governor.update(config, input);
        watchAllocations = false;
        expect(watchedAllocations == 0
                   && output.state == Governor::State::CRITICAL,
               "update performs no heap allocation");
    }

    if (failures != 0)
        std::cerr << failures << " PressureAwareSafetyGovernor test(s) failed\n";
    return failures == 0 ? 0 : 1;
}
