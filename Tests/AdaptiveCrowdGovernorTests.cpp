#include "../Source/AdaptiveCrowdGovernor.h"

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
    int failures = 0;
    bool watchAllocations = false;
    std::size_t watchedAllocations = 0;

    void expect (bool condition, const char* name)
    {
        std::cout << (condition ? "PASS  " : "FAIL  ") << name << '\n';
        if (! condition)
            ++failures;
    }

    AdaptiveCrowdGovernor::Output tick (AdaptiveCrowdGovernor& governor,
                                         double time, int held, int recent,
                                         int voiceLimit = 16)
    {
        AdaptiveCrowdGovernor::Config config;
        config.voiceLimit = voiceLimit;
        return governor.update(config, { held, recent, time });
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
    using Governor = AdaptiveCrowdGovernor;
    static_assert(std::is_nothrow_default_constructible<Governor>::value,
                  "governor construction must stay realtime-safe");
    static_assert(noexcept(std::declval<Governor&>().update(
                      std::declval<const Governor::Config&>(),
                      std::declval<const Governor::Input&>())),
                  "governor update must remain noexcept");

    // Exact, deterministic table edges.
    {
        const struct Expected { int density, band, attacks, spread, active; } cases[] {
            { -100, 0, 4, 1, 8 }, { 8, 0, 4, 1, 8 },
            { 9, 1, 4, 2, 10 },   { 24, 1, 4, 2, 10 },
            { 25, 2, 3, 4, 12 },  { 64, 2, 3, 4, 12 },
            { 65, 3, 2, 8, 14 },  { 128, 3, 2, 8, 14 },
            { 129, 4, 2, 16, 16 }, { 9999, 4, 2, 16, 16 }
        };
        bool exact = true;
        for (const auto& expected : cases)
        {
            const auto profile = Governor::profileForDensity(expected.density, 16);
            exact = exact && profile.band == expected.band
                    && profile.maxAttacksPerStep == expected.attacks
                    && profile.spreadSlots == expected.spread
                    && profile.maxActive == expected.active;
        }
        expect(exact, "all five governor bands have exact inclusive boundaries");
    }

    // The larger of held and recent-eight-second unique participants drives the
    // envelope; promotion remains debounced for exactly half a second.
    {
        Governor governor;
        auto output = tick(governor, 0.0, 12, 65);
        const bool usesMaximum = output.observedDensity == 65
                              && output.band == 0;
        output = tick(governor, 0.49, 12, 65);
        const bool heldBeforeBoundary = output.band == 0;
        output = tick(governor, 0.50, 12, 65);
        expect(usesMaximum && heldBeforeBoundary && output.band == 3
                   && output.maxAttacksPerStep == 2
                   && output.spreadSlots == 8 && output.maxActive == 14,
               "density=max(held,recent) and promotion hold is deterministic");
    }

    // A participant count hovering just below a boundary remains in the higher
    // band. It must cross the 20% lower hysteresis edge and remain there for the
    // four-second demotion hold.
    {
        Governor governor;
        tick(governor, 0.0, 9, 0);
        auto output = tick(governor, 0.5, 9, 0);
        bool stable = output.band == 1;
        for (int step = 1; step <= 100; ++step)
        {
            output = tick(governor, 0.5 + step * 0.1, 8, 0);
            stable = stable && output.band == 1;
        }

        const double fallingStart = 10.5;
        bool noEarlyDemotion = true;
        for (int step = 1; step <= 39; ++step)
        {
            output = tick(governor, fallingStart + step * 0.1, 0, 0);
            noEarlyDemotion = noEarlyDemotion && output.band == 1;
        }
        for (int step = 40; step <= 60; ++step)
            output = tick(governor, fallingStart + step * 0.1, 0, 0);

        expect(stable && noEarlyDemotion && output.band == 0,
               "20 percent hysteresis and four-second demotion suppress chatter");
    }

    // The response constants are calibrated as time-to-about-95%, independent
    // of a 10 Hz update cadence.
    {
        Governor governor;
        tick(governor, 0.0, 0, 0);
        auto output = tick(governor, 0.5, 100, 0);
        const bool fastRise = output.smoothedDensity > 94.0
                           && output.smoothedDensity < 96.0;
        for (int step = 1; step <= 60; ++step)
            output = tick(governor, 0.5 + step * 0.1, 0, 0);
        const bool slowFall = output.smoothedDensity > 4.0
                           && output.smoothedDensity < 6.0;
        expect(fastRise && slowFall,
               "density envelope rises in 0.5 s and falls in 6 s at 10 Hz");
    }

    // MPE gives the governor its member-channel capacity. The attack/spread
    // profile remains musical while active voices cannot exceed that capacity.
    {
        const auto mpe = Governor::profileForDensity(256, 7);
        const auto hostileLow = Governor::profileForDensity(256, -99);
        const auto hostileHigh = Governor::profileForDensity(256, 999);
        expect(mpe.band == 4 && mpe.maxAttacksPerStep == 2
                   && mpe.spreadSlots == 16 && mpe.maxActive == 7
                   && hostileLow.maxActive == 1 && hostileHigh.maxActive == 16,
               "MPE voice capacity and hostile limits are clamped safely");
    }

    // Reset must discard clock, candidate and envelope history.
    {
        Governor governor;
        tick(governor, 0.0, 256, 256);
        tick(governor, 0.5, 256, 256);
        governor.reset();
        const auto reset = governor.getOutput();
        const auto restarted = tick(governor, -500.0, 0, 0);
        expect(reset.band == 0 && reset.observedDensity == 0
                   && reset.smoothedDensity == 0.0
                   && reset.maxAttacksPerStep == 4 && reset.spreadSlots == 1
                   && reset.maxActive == 8 && ! reset.changed
                   && restarted.band == 0 && restarted.smoothedDensity == 0.0,
               "reset restores the complete low-density baseline");
    }

    // Non-finite/backward clocks advance no hold time. Counts and config values
    // are clamped, and even a huge finite jump is worth at most one second.
    {
        Governor governor;
        Governor::Config hostile;
        hostile.voiceLimit = std::numeric_limits<int>::max();
        hostile.riseSeconds = std::numeric_limits<double>::quiet_NaN();
        hostile.fallSeconds = std::numeric_limits<double>::infinity();
        hostile.promotionHoldSeconds = 4.0;
        hostile.demotionHoldSeconds = -1.0;
        hostile.demotionHysteresis = -100.0;

        auto output = governor.update(hostile,
            { std::numeric_limits<int>::min(), std::numeric_limits<int>::max(),
              std::numeric_limits<double>::quiet_NaN() });
        bool safe = output.observedDensity == 256
                 && std::isfinite(output.smoothedDensity) && output.band == 0;
        output = governor.update(hostile,
            { 256, 256, std::numeric_limits<double>::infinity() });
        safe = safe && output.band == 0;
        output = governor.update(hostile, { 256, 256, 0.0 });
        output = governor.update(hostile, { 256, 256, 1.0e300 });
        safe = safe && output.band == 0;
        output = governor.update(hostile, { 256, 256, -1.0e300 });
        safe = safe && output.band == 0 && std::isfinite(output.smoothedDensity)
             && output.maxActive >= 1 && output.maxActive <= 16;
        expect(safe, "hostile counts, config and clocks fail bounded and finite");
    }

    // The complete process path remains allocation-free.
    {
        Governor governor;
        Governor::Config config;
        const Governor::Input input { 128, 256, 0.1 };
        watchedAllocations = 0;
        watchAllocations = true;
        const auto output = governor.update(config, input);
        watchAllocations = false;
        expect(watchedAllocations == 0 && output.observedDensity == 256,
               "update performs no heap allocation");
    }

    if (failures != 0)
        std::cerr << failures << " AdaptiveCrowdGovernor test(s) failed\n";
    return failures == 0 ? 0 : 1;
}
