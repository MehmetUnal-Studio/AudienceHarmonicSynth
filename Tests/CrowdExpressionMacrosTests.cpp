#include "../Source/CrowdExpressionMacros.h"

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
    using Macros = CrowdExpressionMacros;

    int failures = 0;
    bool watchAllocations = false;
    std::size_t watchedAllocations = 0;

    void expect (bool condition, const char* name)
    {
        std::cout << (condition ? "PASS  " : "FAIL  ") << name << '\n';
        if (! condition)
            ++failures;
    }

    bool finiteOutput (const Macros::Output& output)
    {
        return std::isfinite(output.density)
            && std::isfinite(output.centroidX)
            && std::isfinite(output.centroidY)
            && std::isfinite(output.motion)
            && output.density >= 0.0 && output.density <= 1.0
            && output.centroidX >= 0.0 && output.centroidX <= 1.0
            && output.centroidY >= 0.0 && output.centroidY <= 1.0
            && output.motion >= 0.0 && output.motion <= 1.0
            && output.values.density <= 127
            && output.values.centroidX <= 127
            && output.values.centroidY <= 127
            && output.values.motion <= 127;
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
    static_assert(Macros::kMaxSources == 256,
                  "the crowd snapshot contract is exactly 256 slots");
    static_assert(std::is_trivially_copyable<Macros::SourceSnapshot>::value,
                  "source snapshots must remain plain data");
    static_assert(std::is_trivially_copyable<Macros::Input>::value,
                  "input snapshots must remain fixed plain data");
    static_assert(std::is_trivially_copyable<Macros::Output>::value,
                  "outputs must remain fixed plain data");
    static_assert(std::is_nothrow_default_constructible<Macros>::value,
                  "construction must remain noexcept");
    static_assert(noexcept(std::declval<Macros&>().update(
                      std::declval<const Macros::Config&>(),
                      std::declval<const Macros::Input&>())),
                  "update must remain noexcept");

    // Empty input has a musically neutral centroid and emits a complete first
    // snapshot so external MIDI CC state never depends on stale host values.
    {
        Macros macros;
        Macros::Config config;
        config.enabled = true;
        Macros::Input input;
        const auto output = macros.update(config, input);
        expect(output.activeSources == 0
                   && output.validPositionSources == 0
                   && output.values.density == 0
                   && output.values.centroidX == 64
                   && output.values.centroidY == 64
                   && output.values.motion == 0
                   && output.emitDue
                   && output.changedMask == Macros::ChangedAll
                   && finiteOutput(output),
               "empty crowd is centred and first emission rehydrates all macros");
    }

    // Density counts active identities, while centroid respects finite positions
    // and clamps hostile but finite coordinates to the phone's unit plane.
    {
        Macros macros;
        Macros::Input input;
        for (std::size_t index = 0; index < 128; ++index)
        {
            input.sources[index].active = true;
            input.sources[index].x = index % 2 == 0 ? -100.0f : 100.0f;
            input.sources[index].y = 0.25f;
        }
        input.sources[0].x = std::numeric_limits<float>::quiet_NaN();
        input.sources[1].y = std::numeric_limits<float>::infinity();

        const auto output = macros.update({}, input);
        expect(output.activeSources == 128
                   && output.validPositionSources == 126
                   && output.values.density == 64
                   && output.values.centroidX >= 62
                   && output.values.centroidX <= 65
                   && output.values.centroidY == 32
                   && finiteOutput(output),
               "density, finite centroid filtering, and coordinate clamps are bounded");
    }

    // A stable participant's displacement drives a time-aware EMA. Holding the
    // next position decays it, while a newly activated identity creates no jump.
    {
        Macros macros;
        Macros::Input input;
        input.sources[5] = { true, 0.0f, 0.0f };
        macros.update({}, input);

        input.monotonicSeconds = 0.1;
        input.sources[5] = { true, 1.0f, 1.0f };
        auto output = macros.update({}, input);
        const double moving = output.motion;

        input.monotonicSeconds = 0.4;
        output = macros.update({}, input);
        const bool decayed = output.motion < moving;

        input.monotonicSeconds = 0.5;
        input.sources[5].active = false;
        input.sources[99] = { true, 1.0f, 0.0f };
        output = macros.update({}, input);
        expect(moving > 0.0 && moving < 1.0 && decayed
                   && output.motion < moving && finiteOutput(output),
               "motion EMA responds, decays, and ignores identity activation jumps");
    }

    // Rate ticks are bounded, do not catch up in bursts, and carry only macros
    // that changed since the previous emission.
    {
        Macros macros;
        Macros::Config config;
        config.enabled = true;
        config.rateHz = 10.0;
        Macros::Input input;
        input.sources[0] = { true, 0.25f, 0.75f };
        auto output = macros.update(config, input);
        bool schedule = output.emitDue
                     && output.changedMask == Macros::ChangedAll;

        input.monotonicSeconds = 0.099;
        output = macros.update(config, input);
        schedule = schedule && ! output.emitDue
                 && output.changedMask == Macros::ChangedNone;

        input.monotonicSeconds = 0.100;
        output = macros.update(config, input);
        schedule = schedule && output.emitDue
                 && output.changedMask == Macros::ChangedNone;

        input.monotonicSeconds = 1000.0;
        input.sources[0].x = 1.0f;
        output = macros.update(config, input);
        schedule = schedule && output.emitDue
                 && (output.changedMask & Macros::ChangedCentroidX) != 0u;

        input.monotonicSeconds = 1000.001;
        output = macros.update(config, input);
        expect(schedule && ! output.emitDue,
               "scheduler honours rate and emits once after a large clock jump");
    }

    // Safety Governor can close emission through Config::enabled while keeping
    // telemetry live. Re-opening publishes a complete downstream snapshot.
    {
        Macros macros;
        Macros::Config config;
        Macros::Input input;
        input.sources[0] = { true, 0.0f, 0.0f };
        auto output = macros.update(config, input);
        bool safeGate = ! output.emitDue && output.activeSources == 1;

        input.monotonicSeconds = 1.0;
        input.sources[1] = { true, 1.0f, 1.0f };
        output = macros.update(config, input);
        safeGate = safeGate && ! output.emitDue && output.activeSources == 2;

        config.enabled = true;
        input.monotonicSeconds = 1.01;
        output = macros.update(config, input);
        safeGate = safeGate && output.emitDue
                 && output.changedMask == Macros::ChangedAll;

        config.enabled = false;
        input.monotonicSeconds = 1.02;
        macros.update(config, input);
        config.enabled = true;
        input.monotonicSeconds = 1.03;
        output = macros.update(config, input);
        expect(safeGate && output.emitDue
                   && output.changedMask == Macros::ChangedAll,
               "external safety gate preserves telemetry and rehydrates on reopen");
    }

    // Backward and non-finite clocks cannot earn emission or EMA time. Invalid
    // coordinates remain isolated and every published number stays finite.
    {
        Macros macros;
        Macros::Config config;
        config.enabled = true;
        config.rateHz = std::numeric_limits<double>::quiet_NaN();
        Macros::Input input;
        input.monotonicSeconds = 10.0;
        input.sources[0] = { true,
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity() };
        auto output = macros.update(config, input);
        bool safe = output.emitDue && output.clockValid && finiteOutput(output);

        input.monotonicSeconds = 9.0;
        output = macros.update(config, input);
        safe = safe && ! output.emitDue && output.clockValid
             && finiteOutput(output);

        input.monotonicSeconds = std::numeric_limits<double>::infinity();
        output = macros.update(config, input);
        safe = safe && ! output.emitDue && ! output.clockValid
             && finiteOutput(output);

        input.monotonicSeconds = 9.1;
        output = macros.update(config, input);
        expect(safe && output.emitDue && finiteOutput(output),
               "hostile coordinates, rate, and clock values fail finite and bounded");
    }

    // Reset discards slot history, EMA, clock and emission state.
    {
        Macros macros;
        Macros::Config config;
        config.enabled = true;
        Macros::Input input;
        input.sources[0] = { true, 0.0f, 0.0f };
        macros.update(config, input);
        input.monotonicSeconds = 0.1;
        input.sources[0] = { true, 1.0f, 1.0f };
        macros.update(config, input);
        macros.reset();
        const auto reset = macros.getOutput();
        input.monotonicSeconds = -500.0;
        const auto restarted = macros.update(config, input);
        expect(reset.activeSources == 0 && reset.values.density == 0
                   && reset.values.centroidX == 64
                   && reset.values.centroidY == 64 && reset.values.motion == 0
                   && restarted.emitDue
                   && restarted.changedMask == Macros::ChangedAll
                   && restarted.values.motion == 0,
               "reset clears analysis, scheduler, and participant history");
    }

    // The complete 256-source analysis and scheduling path allocates nothing.
    {
        Macros macros;
        Macros::Config config;
        config.enabled = true;
        Macros::Input input;
        for (auto& source : input.sources)
            source = { true, 0.5f, 0.5f };

        watchedAllocations = 0;
        watchAllocations = true;
        const auto output = macros.update(config, input);
        watchAllocations = false;
        expect(watchedAllocations == 0 && output.activeSources == 256
                   && output.values.density == 127,
               "full 256-source update performs no heap allocation");
    }

    if (failures != 0)
        std::cerr << failures << " CrowdExpressionMacros test(s) failed\n";
    return failures == 0 ? 0 : 1;
}
