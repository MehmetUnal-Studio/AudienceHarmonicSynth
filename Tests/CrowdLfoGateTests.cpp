#include "../Source/CrowdLfoGate.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <type_traits>

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

    bool near (double a, double b, double tolerance = 1.0e-12)
    {
        return std::abs(a - b) <= tolerance;
    }

    CrowdLfoGate::Config squareHz (double rateHz = 1.0)
    {
        CrowdLfoGate::Config config;
        config.enabled = true;
        config.waveform = CrowdLfoGate::Waveform::Square;
        config.rateMode = CrowdLfoGate::RateMode::Hertz;
        config.rateHz = rateHz;
        return config;
    }

    CrowdLfoGate::ClockFrame frame (double sampleRate, int numSamples,
                                     double monotonicSeconds = 0.0)
    {
        CrowdLfoGate::ClockFrame clock;
        clock.sampleRate = sampleRate;
        clock.numSamples = numSamples;
        clock.monotonicSeconds = monotonicSeconds;
        return clock;
    }

    struct AbsoluteTransition
    {
        CrowdLfoGate::Transition::Type type = CrowdLfoGate::Transition::Type::Open;
        int offset = 0;
    };
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
    static_assert(CrowdLfoGate::kMaxTransitionsPerBlock == 64,
                  "transition capacity changed");
    static_assert(std::is_nothrow_default_constructible<CrowdLfoGate>::value,
                  "LFO gate construction must stay noexcept");
    static_assert(std::is_trivially_copyable<CrowdLfoGate::Config>::value,
                  "configuration must remain POD-like");
    static_assert(std::is_trivially_copyable<CrowdLfoGate::ClockFrame>::value,
                  "clock frame must remain POD-like");

    expect(near(CrowdLfoGate::waveformValue(CrowdLfoGate::Waveform::Sine, 0.0), 0.5)
               && near(CrowdLfoGate::waveformValue(CrowdLfoGate::Waveform::Triangle, 0.0), 0.0)
               && near(CrowdLfoGate::waveformValue(CrowdLfoGate::Waveform::Triangle, 0.5), 1.0)
               && near(CrowdLfoGate::waveformValue(CrowdLfoGate::Waveform::Square, 0.49), 1.0)
               && near(CrowdLfoGate::waveformValue(CrowdLfoGate::Waveform::Square, 0.5), 0.0)
               && near(CrowdLfoGate::waveformValue(CrowdLfoGate::Waveform::SawUp, 0.25), 0.25)
               && near(CrowdLfoGate::waveformValue(CrowdLfoGate::Waveform::SawDown, 0.25), 0.75),
           "all waveform phase conventions are exact");
    expect(std::isfinite(CrowdLfoGate::waveformValue(CrowdLfoGate::Waveform::Sine,
                                                     std::numeric_limits<double>::quiet_NaN()))
               && std::isfinite(CrowdLfoGate::waveformValue(CrowdLfoGate::Waveform::SawUp,
                                                            std::numeric_limits<double>::infinity())),
           "waveforms reject NaN and infinity without propagation");

    // A 1 Hz square sampled at 8 Hz closes at sample four. The next block's
    // absolute process-wide clock lands exactly on the next cycle boundary.
    {
        CrowdLfoGate gate;
        const auto config = squareHz();
        CrowdLfoGate::OutputBlock output;
        gate.process(config, frame(8.0, 8, 0.0), output);
        expect(output.count == 1
                   && output.transitions[0].type == CrowdLfoGate::Transition::Type::Close
                   && output.transitions[0].sampleOffset == 4
                   && ! output.gateOpen,
               "Hz square closes at the exact sample");

        gate.process(config, frame(8.0, 4, 1.0), output);
        expect(output.count == 1
                   && output.transitions[0].type == CrowdLfoGate::Transition::Type::Open
                   && output.transitions[0].sampleOffset == 0
                   && output.gateOpen,
               "Hz square follows the absolute monotonic clock");
    }

    // Splitting blocks cannot move or reorder absolute-clock transitions.
    {
        constexpr int totalSamples = 96;
        constexpr double sampleRate = 32.0;
        const auto config = squareHz(2.0);
        CrowdLfoGate oneBlock;
        CrowdLfoGate::OutputBlock output;
        std::array<AbsoluteTransition, 32> expected {};
        int expectedCount = 0;
        oneBlock.process(config, frame(sampleRate, totalSamples, 0.0), output);
        for (int i = 0; i < output.count; ++i)
            expected[static_cast<std::size_t>(expectedCount++)]
                = { output.transitions[static_cast<std::size_t>(i)].type,
                    output.transitions[static_cast<std::size_t>(i)].sampleOffset };

        CrowdLfoGate split;
        const std::array<int, 8> sizes {{ 3, 5, 1, 7, 9, 11, 17, 43 }};
        std::array<AbsoluteTransition, 32> actual {};
        int actualCount = 0;
        int blockStart = 0;
        for (int size : sizes)
        {
            split.process(config,
                          frame(sampleRate, size,
                                static_cast<double>(blockStart) / sampleRate),
                          output);
            for (int i = 0; i < output.count; ++i)
                actual[static_cast<std::size_t>(actualCount++)]
                    = { output.transitions[static_cast<std::size_t>(i)].type,
                        blockStart + output.transitions[static_cast<std::size_t>(i)].sampleOffset };
            blockStart += size;
        }

        bool identical = expectedCount == actualCount;
        for (int i = 0; identical && i < expectedCount; ++i)
            identical = expected[static_cast<std::size_t>(i)].type
                            == actual[static_cast<std::size_t>(i)].type
                     && expected[static_cast<std::size_t>(i)].offset
                            == actual[static_cast<std::size_t>(i)].offset;
        expect(identical && blockStart == totalSamples,
               "absolute fallback LFO is invariant to block partitioning");
    }

    // One quarter-note cycle at 120 BPM is four samples at this deliberately
    // tiny test rate: close=2, open=4, close=6.
    {
        CrowdLfoGate gate;
        auto config = squareHz();
        config.rateMode = CrowdLfoGate::RateMode::Sync;
        config.syncDivision = CrowdLfoGate::SyncDivision::Quarter;
        auto clock = frame(8.0, 8);
        clock.hostValid = true;
        clock.isPlaying = true;
        clock.bpm = 120.0;
        clock.ppqPosition = 0.0;

        CrowdLfoGate::OutputBlock output;
        gate.process(config, clock, output);
        expect(output.hostLocked && near(output.effectiveRateHz, 2.0)
                   && output.count == 3
                   && output.transitions[0].type == CrowdLfoGate::Transition::Type::Close
                   && output.transitions[0].sampleOffset == 2
                   && output.transitions[1].type == CrowdLfoGate::Transition::Type::Open
                   && output.transitions[1].sampleOffset == 4
                   && output.transitions[2].type == CrowdLfoGate::Transition::Type::Close
                   && output.transitions[2].sampleOffset == 6,
               "host PPQ sync is sample accurate at 120 BPM");
    }

    // Tempo changes use the new host slope from the supplied PPQ start; a seek
    // or loop reanchors immediately instead of dragging prior instance state.
    {
        CrowdLfoGate gate;
        auto config = squareHz();
        config.rateMode = CrowdLfoGate::RateMode::Sync;
        config.syncDivision = CrowdLfoGate::SyncDivision::Quarter;
        auto clock = frame(8.0, 2);
        clock.hostValid = true;
        clock.isPlaying = true;
        clock.bpm = 120.0;
        clock.ppqPosition = 0.0;
        CrowdLfoGate::OutputBlock output;
        gate.process(config, clock, output);

        clock.ppqPosition = 0.5;
        clock.bpm = 60.0;
        clock.numSamples = 4;
        gate.process(config, clock, output);
        const bool tempoChangeClosedAtStart = output.count >= 1
            && output.transitions[0].type == CrowdLfoGate::Transition::Type::Close
            && output.transitions[0].sampleOffset == 0;

        clock.ppqPosition = 0.0;
        clock.bpm = 120.0;
        clock.numSamples = 1;
        gate.process(config, clock, output);
        expect(tempoChangeClosedAtStart && output.count == 1
                   && output.transitions[0].type == CrowdLfoGate::Transition::Type::Open
                   && output.transitions[0].sampleOffset == 0,
               "tempo change and host seek are resolved from exact PPQ");
    }

    // Invalid/stopped host Sync derives absolute phase from the shared monotonic
    // clock and fallback tempo. Two independently-created gates therefore agree.
    {
        CrowdLfoGate a, b;
        auto config = squareHz();
        config.rateMode = CrowdLfoGate::RateMode::Sync;
        config.syncDivision = CrowdLfoGate::SyncDivision::Quarter;
        auto clock = frame(8.0, 4, 1.0);
        clock.hostValid = false;
        clock.isPlaying = false;
        clock.fallbackBpm = 120.0;
        CrowdLfoGate::OutputBlock outA, outB;
        a.process(config, clock, outA);
        b.process(config, clock, outB);
        bool identical = outA.count == outB.count
                      && outA.gateOpen == outB.gateOpen
                      && near(outA.phaseAtStart, outB.phaseAtStart);
        for (int i = 0; identical && i < outA.count; ++i)
            identical = outA.transitions[static_cast<std::size_t>(i)].type
                            == outB.transitions[static_cast<std::size_t>(i)].type
                     && outA.transitions[static_cast<std::size_t>(i)].sampleOffset
                            == outB.transitions[static_cast<std::size_t>(i)].sampleOffset;
        expect(identical && ! outA.hostLocked && near(outA.effectiveRateHz, 2.0),
               "fallback phase aligns instances through process-wide monotonic time");
    }

    // Disabling the LFO is a true always-open bypass, including a zero-sample
    // automation boundary after the previous active block closed the gate.
    {
        CrowdLfoGate gate;
        auto config = squareHz();
        CrowdLfoGate::OutputBlock output;
        gate.process(config, frame(8.0, 1, 0.5), output);
        const bool initiallyClosed = output.count == 1 && ! output.gateOpen;
        config.enabled = false;
        gate.process(config, frame(8.0, 0, 0.5), output);
        expect(initiallyClosed && ! output.active && output.gateOpen
                   && output.count == 1
                   && output.transitions[0].type == CrowdLfoGate::Transition::Type::Open
                   && output.transitions[0].sampleOffset == 0
                   && near(output.valueAtStart, 1.0),
               "disabled LFO opens immediately with zero-sample-safe bypass semantics");
    }

    // A one-sample call can transition only at zero; hostile clock scalars remain
    // finite and cannot create negative offsets.
    {
        CrowdLfoGate gate;
        const auto config = squareHz();
        auto clock = frame(std::numeric_limits<double>::quiet_NaN(), 1,
                           std::numeric_limits<double>::infinity());
        CrowdLfoGate::OutputBlock output;
        gate.process(config, clock, output);
        bool offsetsSafe = true;
        for (int i = 0; i < output.count; ++i)
            offsetsSafe = offsetsSafe
                       && output.transitions[static_cast<std::size_t>(i)].sampleOffset == 0;
        expect(offsetsSafe && std::isfinite(output.phaseAtStart)
                   && std::isfinite(output.phaseAtEnd)
                   && std::isfinite(output.valueAtEnd),
               "zero/one-sample hostile clocks remain finite and offset-safe");
    }

    // Waveform/config automation emits a normal edge at sample zero; it never
    // fabricates a safety reset.
    {
        CrowdLfoGate gate;
        auto config = squareHz();
        CrowdLfoGate::OutputBlock output;
        gate.process(config, frame(8.0, 1, 0.0), output);
        config.waveform = CrowdLfoGate::Waveform::SawUp;
        gate.process(config, frame(8.0, 1, 0.0), output);
        expect(output.count == 1 && ! output.gateOpen
                   && output.transitions[0].type == CrowdLfoGate::Transition::Type::Close
                   && output.transitions[0].sampleOffset == 0
                   && ! output.resetRequested,
               "configuration automation produces an ordinary gate edge");
    }

    // Reset restores the safe-open gate without retaining instance-local phase;
    // the next absolute clock frame deterministically chooses its state.
    {
        CrowdLfoGate gate;
        const auto config = squareHz();
        CrowdLfoGate::OutputBlock output;
        gate.process(config, frame(8.0, 1, 0.5), output);
        gate.reset();
        gate.process(config, frame(8.0, 1, 0.5), output);
        expect(output.count == 1
                   && output.transitions[0].type == CrowdLfoGate::Transition::Type::Close
                   && output.transitions[0].sampleOffset == 0,
               "reset restores safe-open state deterministically");
    }

    // Hostile enum/scalar values are sanitised before any floating-point path.
    {
        CrowdLfoGate::Config hostile;
        hostile.enabled = true;
        hostile.waveform = static_cast<CrowdLfoGate::Waveform>(255);
        hostile.rateMode = static_cast<CrowdLfoGate::RateMode>(255);
        hostile.syncDivision = static_cast<CrowdLfoGate::SyncDivision>(255);
        hostile.rateHz = std::numeric_limits<double>::quiet_NaN();
        const auto clean = CrowdLfoGate::sanitiseConfig(hostile);
        expect(clean.waveform == CrowdLfoGate::Waveform::Sine
                   && clean.rateMode == CrowdLfoGate::RateMode::Hertz
                   && clean.syncDivision == CrowdLfoGate::SyncDivision::Quarter
                   && near(clean.rateHz, 1.0),
               "configuration sanitisation is finite and bounded");
    }

    // An intentionally hostile high-transition block fails closed as one safety
    // reset request; no partial Open/Close prefix is exposed to MIDI routing.
    {
        CrowdLfoGate gate;
        const auto config = squareHz(20.0);
        CrowdLfoGate::OutputBlock output;
        gate.process(config, frame(100.0, 400, 0.0), output);
        expect(output.resetRequested && output.overflowed && output.count == 0,
               "transition overflow requests reset without partial lifecycle output");
    }

    // The process path must remain allocation-free.
    {
        CrowdLfoGate gate;
        const auto config = squareHz(20.0);
        CrowdLfoGate::OutputBlock output;
        watchedAllocations = 0;
        watchAllocations = true;
        gate.process(config, frame(48000.0, 4096, 10.0), output);
        watchAllocations = false;
        expect(watchedAllocations == 0,
               "process performs no heap allocation");
    }

    std::cout << (failures == 0 ? "All CrowdLfoGate tests passed.\n"
                                : "CrowdLfoGate test failures detected.\n");
    return failures == 0 ? 0 : 1;
}
