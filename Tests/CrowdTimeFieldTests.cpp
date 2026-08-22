#include "../Source/CrowdTimeField.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
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

    CrowdTimeField::Config configFor (CrowdTimeField::Mode mode,
                                      CrowdTimeField::Division division
                                          = CrowdTimeField::Division::Sixteenth)
    {
        CrowdTimeField::Config config;
        config.mode = mode;
        config.clockSource = CrowdTimeField::ClockSource::Host;
        config.division = division;
        config.internalBpm = 120.0;
        config.maxAttacksPerStep = 16;
        config.maxActive = 16;
        config.gatePercent = 50.0;
        config.spreadSlots = 1;
        return config;
    }

    CrowdTimeField::ClockFrame hostFrame (double sampleRate, int numSamples,
                                           double ppq = 0.0,
                                           double bpm = 120.0)
    {
        CrowdTimeField::ClockFrame frame;
        frame.sampleRate = sampleRate;
        frame.numSamples = numSamples;
        frame.hostValid = true;
        frame.isPlaying = true;
        frame.bpm = bpm;
        frame.ppqPosition = ppq;
        frame.monotonicSeconds = ppq * 60.0 / bpm;
        return frame;
    }

    CrowdTimeField::ClockFrame nextFrame (CrowdTimeField::ClockFrame frame,
                                           int nextNumSamples)
    {
        const double seconds = static_cast<double>(frame.numSamples) / frame.sampleRate;
        if (frame.isPlaying)
            frame.ppqPosition += seconds * frame.bpm / 60.0;
        frame.monotonicSeconds += seconds;
        frame.numSamples = nextNumSamples;
        return frame;
    }

    CrowdTimeField::InputEvent input (CrowdTimeField::InputEvent::Type type,
                                      int sourceId, int finger, int offset)
    {
        return { type, CrowdTimeField::voiceIdFor(sourceId, finger),
                 sourceId, offset };
    }

    int countType (const CrowdTimeField::OutputBlock& output,
                   CrowdTimeField::OutputEvent::Type type)
    {
        int count = 0;
        for (int i = 0; i < output.count; ++i)
            count += output.events[static_cast<std::size_t>(i)].type == type ? 1 : 0;
        return count;
    }

    int firstOffset (const CrowdTimeField::OutputBlock& output,
                     CrowdTimeField::OutputEvent::Type type,
                     int sourceId = -1)
    {
        for (int i = 0; i < output.count; ++i)
        {
            const auto& event = output.events[static_cast<std::size_t>(i)];
            if (event.type == type && (sourceId < 0 || event.sourceId == sourceId))
                return event.sampleOffset;
        }
        return -1;
    }

    bool allOffsetsValid (const CrowdTimeField::OutputBlock& output, int numSamples)
    {
        const int maximum = std::max(0, numSamples - 1);
        for (int i = 0; i < output.count; ++i)
        {
            const int offset = output.events[static_cast<std::size_t>(i)].sampleOffset;
            if (offset < 0 || offset > maximum)
                return false;
        }
        return true;
    }
}

// The process-path allocation test enables this counter only around process().
// Test reporting and containers are deliberately outside the watched region.
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
    static_assert(CrowdTimeField::kMaxSources == 256, "source capacity changed");
    static_assert(CrowdTimeField::kMaxVoices == 2560, "finger capacity changed");
    static_assert(CrowdTimeField::kMaxOutputEvents == 64, "block contract changed");
    static_assert(std::is_nothrow_default_constructible<CrowdTimeField>::value,
                  "realtime core construction must stay noexcept");

    expect(std::abs(CrowdTimeField::divisionQuarterNotes(
                        CrowdTimeField::Division::Sixteenth)
                    * 60.0 / 120.0 - 0.125) < 1.0e-12,
           "120 BPM 1/16 is exactly 125 ms");

    // Flow is the identity lifecycle path: no quantisation and no synthesized
    // motion events. Hostile offsets are clamped without changing FIFO order.
    {
        CrowdTimeField field;
        const auto config = configFor(CrowdTimeField::Mode::Flow);
        const auto frame = hostFrame(48000.0, 64);
        const CrowdTimeField::InputEvent events[] {
            input(CrowdTimeField::InputEvent::Type::On, 2, 3, -50),
            input(CrowdTimeField::InputEvent::Type::Off, 2, 3, 999)
        };
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, events, 2, output);

        expect(! output.resetRequested && output.count == 2
                   && output.events[0].type == CrowdTimeField::OutputEvent::Type::Attack
                   && output.events[0].sampleOffset == 0
                   && output.events[1].type == CrowdTimeField::OutputEvent::Type::Release
                   && output.events[1].sampleOffset == 63,
               "Flow passes ordered On/Off immediately with exact clamping");
        expect(countType(output, CrowdTimeField::OutputEvent::Type::SampleMotion) == 0
                   && output.activeCount == 0,
               "Flow leaves X/Y on the processor path and cannot leave this lifecycle stuck");
    }
    {
        CrowdTimeField field;
        const auto config = configFor(CrowdTimeField::Mode::Flow);
        const auto frame = hostFrame(48000.0, 64);
        const CrowdTimeField::InputEvent events[] {
            input(CrowdTimeField::InputEvent::Type::On, 0, 0, 10),
            { CrowdTimeField::InputEvent::Type::Off, -1, -1, 0 },
            { static_cast<CrowdTimeField::InputEvent::Type>(255),
              CrowdTimeField::voiceIdFor(1, 0), 1, 0 },
            input(CrowdTimeField::InputEvent::Type::Off, 0, 0, 20)
        };
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, events, 4, output);
        expect(! output.resetRequested && output.count == 2
                   && output.events[0].sampleOffset == 10
                   && output.events[1].sampleOffset == 20,
               "invalid identity/type is ignored before valid FIFO-order checks");
    }

    // Zero and one-sample host blocks must never produce a negative timestamp or
    // divide by zero. A zero-sample Flow block still consumes lifecycle state.
    {
        CrowdTimeField field;
        const auto config = configFor(CrowdTimeField::Mode::Flow);
        const auto frame = hostFrame(48000.0, 0);
        const CrowdTimeField::InputEvent events[] {
            input(CrowdTimeField::InputEvent::Type::On, 0, 0, 8),
            input(CrowdTimeField::InputEvent::Type::Off, 0, 0, 8)
        };
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, events, 2, output);
        expect(output.count == 2 && allOffsetsValid(output, 0)
                   && output.activeCount == 0,
               "zero-sample block is finite, clamped and lifecycle-safe");
    }
    {
        CrowdTimeField field;
        const auto config = configFor(CrowdTimeField::Mode::Grid);
        const auto frame = hostFrame(48000.0, 1);
        const auto event = input(CrowdTimeField::InputEvent::Type::On, 0, 0, 0);
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, &event, 1, output);
        expect(countType(output, CrowdTimeField::OutputEvent::Type::Attack) == 1
                   && firstOffset(output, CrowdTimeField::OutputEvent::Type::Attack) == 0
                   && allOffsetsValid(output, 1),
               "one-sample Grid block accepts an exact boundary at offset zero");
    }
    {
        CrowdTimeField field;
        const auto config = configFor(CrowdTimeField::Mode::Grid);
        const auto frame = hostFrame(48000.0, 1);
        std::array<CrowdTimeField::InputEvent, 16> events {};
        for (int source = 0; source < 16; ++source)
            events[static_cast<std::size_t>(source)]
                = input(CrowdTimeField::InputEvent::Type::On, source, 0, 0);
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, events.data(), static_cast<int>(events.size()), output);
        expect(countType(output, CrowdTimeField::OutputEvent::Type::Attack) == 16
                   && output.pendingCount == 0,
               "one-sample boundary preserves configured attack density without buffer-size latency");
    }

    // Every sample rate/division combination lands on the mathematically nearest
    // sample. The On is one sample after beat zero so the next boundary is used.
    {
        const std::array<double, 3> sampleRates {{ 44100.0, 48000.0, 96000.0 }};
        const std::array<CrowdTimeField::Division, 4> divisions {{
            CrowdTimeField::Division::Quarter,
            CrowdTimeField::Division::Eighth,
            CrowdTimeField::Division::Sixteenth,
            CrowdTimeField::Division::ThirtySecond
        }};
        bool allExact = true;
        for (double sampleRate : sampleRates)
        {
            for (auto division : divisions)
            {
                const double boundarySamples = sampleRate * 60.0 / 120.0
                                             * CrowdTimeField::divisionQuarterNotes(division);
                const int expected = static_cast<int>(std::llround(boundarySamples));
                CrowdTimeField field;
                const auto config = configFor(CrowdTimeField::Mode::Grid, division);
                const auto frame = hostFrame(sampleRate, expected + 2);
                const auto event = input(CrowdTimeField::InputEvent::Type::On, 0, 0, 1);
                CrowdTimeField::OutputBlock output;
                field.process(config, frame, &event, 1, output);
                allExact = allExact
                        && firstOffset(output, CrowdTimeField::OutputEvent::Type::Attack)
                             == expected;
            }
        }
        expect(allExact, "quarter through 1/32 boundaries are exact at 44.1/48/96 kHz");
    }

    // A boundary exactly at the half-open block end belongs to the next block.
    {
        CrowdTimeField field;
        const auto config = configFor(CrowdTimeField::Mode::Grid);
        auto frame = hostFrame(48000.0, 6000);
        const auto event = input(CrowdTimeField::InputEvent::Type::On, 0, 0, 1);
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, &event, 1, output);
        const bool firstWasEmpty = countType(output, CrowdTimeField::OutputEvent::Type::Attack) == 0;

        frame = nextFrame(frame, 64);
        field.process(config, frame, nullptr, 0, output);
        expect(firstWasEmpty
                   && firstOffset(output, CrowdTimeField::OutputEvent::Type::Attack) == 0,
               "block-end boundary is emitted once at the next block's offset zero");
    }

    // Grid taps shorter than the quantisation wait are latched to the configured
    // minimum gate. A release after a sounding attack remains immediate.
    {
        CrowdTimeField field;
        auto config = configFor(CrowdTimeField::Mode::Grid);
        config.gatePercent = 50.0;
        const auto frame = hostFrame(1000.0, 300, 0.01);
        const CrowdTimeField::InputEvent events[] {
            input(CrowdTimeField::InputEvent::Type::On, 1, 0, 0),
            input(CrowdTimeField::InputEvent::Type::Off, 1, 0, 10)
        };
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, events, 2, output);
        expect(firstOffset(output, CrowdTimeField::OutputEvent::Type::Attack) == 120
                   && firstOffset(output, CrowdTimeField::OutputEvent::Type::Release) == 183
                   && output.activeCount == 0,
               "Grid short tap waits for the boundary then receives a 50% minimum gate");
    }
    {
        CrowdTimeField field;
        const auto config = configFor(CrowdTimeField::Mode::Grid);
        const auto frame = hostFrame(1000.0, 100);
        const CrowdTimeField::InputEvent events[] {
            input(CrowdTimeField::InputEvent::Type::On, 1, 0, 0),
            input(CrowdTimeField::InputEvent::Type::Off, 1, 0, 50)
        };
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, events, 2, output);
        expect(firstOffset(output, CrowdTimeField::OutputEvent::Type::Attack) == 0
                   && firstOffset(output, CrowdTimeField::OutputEvent::Type::Release) == 50,
               "Grid releases an already sounding held note immediately and safely");
    }

    // Timed held notes get one snapshot request per later grid boundary, but not
    // a redundant SampleMotion on the Attack boundary itself.
    {
        CrowdTimeField field;
        const auto config = configFor(CrowdTimeField::Mode::Grid);
        const auto frame = hostFrame(1000.0, 251);
        const auto event = input(CrowdTimeField::InputEvent::Type::On, 0, 0, 0);
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, &event, 1, output);
        expect(countType(output, CrowdTimeField::OutputEvent::Type::Attack) == 1
                   && countType(output, CrowdTimeField::OutputEvent::Type::SampleMotion) == 2
                   && output.events[1].sampleOffset == 125
                   && output.events[2].sampleOffset == 250,
               "timed held voice samples motion at later boundaries only");
    }

    // Fair selection rotates over held voices, and fixed gates requeue them.
    {
        CrowdTimeField field;
        auto config = configFor(CrowdTimeField::Mode::Ensemble);
        config.maxAttacksPerStep = 1;
        config.maxActive = 1;
        config.gatePercent = 50.0;
        config.spreadSlots = 1;
        const auto frame = hostFrame(1000.0, 501);
        const CrowdTimeField::InputEvent events[] {
            input(CrowdTimeField::InputEvent::Type::On, 0, 0, 0),
            input(CrowdTimeField::InputEvent::Type::On, 1, 0, 0),
            input(CrowdTimeField::InputEvent::Type::On, 2, 0, 0)
        };
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, events, 3, output);

        std::array<int, 5> sources {{ -1, -1, -1, -1, -1 }};
        int attackIndex = 0;
        for (int i = 0; i < output.count && attackIndex < 5; ++i)
            if (output.events[static_cast<std::size_t>(i)].type
                  == CrowdTimeField::OutputEvent::Type::Attack)
                sources[static_cast<std::size_t>(attackIndex++)]
                    = output.events[static_cast<std::size_t>(i)].sourceId;

        expect(attackIndex == 5
                   && sources == std::array<int, 5> {{ 0, 1, 2, 0, 1 }},
               "Ensemble selection fairly rotates and held voices requeue");
        expect(output.activeCount <= 1,
               "Ensemble never exceeds its configured active cap");
    }

    // A 100% gate releases before its same-boundary held retrigger. Stable event
    // order is essential because the consumer is a stateful MPE allocator.
    {
        CrowdTimeField field;
        auto config = configFor(CrowdTimeField::Mode::Ensemble);
        config.maxAttacksPerStep = 1;
        config.maxActive = 1;
        config.gatePercent = 100.0;
        const auto frame = hostFrame(1000.0, 251);
        const auto event = input(CrowdTimeField::InputEvent::Type::On, 0, 0, 0);
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, &event, 1, output);

        bool ordered = output.count == 5;
        const std::array<CrowdTimeField::OutputEvent::Type, 5> expected {{
            CrowdTimeField::OutputEvent::Type::Attack,
            CrowdTimeField::OutputEvent::Type::Release,
            CrowdTimeField::OutputEvent::Type::Attack,
            CrowdTimeField::OutputEvent::Type::Release,
            CrowdTimeField::OutputEvent::Type::Attack
        }};
        const std::array<int, 5> offsets {{ 0, 125, 125, 250, 250 }};
        for (int i = 0; i < output.count && i < 5; ++i)
            ordered = ordered
                   && output.events[static_cast<std::size_t>(i)].type
                        == expected[static_cast<std::size_t>(i)]
                   && output.events[static_cast<std::size_t>(i)].sampleOffset
                        == offsets[static_cast<std::size_t>(i)];
        expect(ordered, "held 100% Ensemble pulse releases before its same-tick retrigger");
    }

    // Spread assigns source N to lane N mod spread and distributes those lanes
    // across successive base-grid steps.
    {
        CrowdTimeField field;
        auto config = configFor(CrowdTimeField::Mode::Ensemble);
        config.spreadSlots = 4;
        config.gatePercent = 50.0;
        const auto frame = hostFrame(1000.0, 501);
        const CrowdTimeField::InputEvent events[] {
            input(CrowdTimeField::InputEvent::Type::On, 0, 0, 0),
            input(CrowdTimeField::InputEvent::Type::On, 1, 0, 0),
            input(CrowdTimeField::InputEvent::Type::On, 2, 0, 0),
            input(CrowdTimeField::InputEvent::Type::On, 3, 0, 0)
        };
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, events, 4, output);
        bool lanes = true;
        for (int source = 0; source < 4; ++source)
            lanes = lanes
                 && firstOffset(output, CrowdTimeField::OutputEvent::Type::Attack,
                                source) == source * 125;
        expect(lanes, "Ensemble source lanes deterministically spread over grid steps");
    }

    // A released tap receives at least one complete deterministic lane cycle
    // before expiry. This matters when a coarse division is paired with a wide
    // spread (for example, quarter notes over 16 steps = 16 beats).
    {
        const std::array<CrowdTimeField::Division, 4> divisions {{
            CrowdTimeField::Division::Quarter,
            CrowdTimeField::Division::Eighth,
            CrowdTimeField::Division::Sixteenth,
            CrowdTimeField::Division::ThirtySecond
        }};
        const std::array<int, 5> spreads {{ 1, 2, 4, 8, 16 }};
        bool everyLaneSurvived = true;
        for (auto division : divisions)
        {
            for (int spread : spreads)
            {
                CrowdTimeField field;
                auto config = configFor(CrowdTimeField::Mode::Ensemble, division);
                config.spreadSlots = spread;
                config.maxAttacksPerStep = 1;
                config.maxActive = 1;
                const double targetBeat = static_cast<double>(spread)
                                        * CrowdTimeField::divisionQuarterNotes(division);
                const int expectedOffset = static_cast<int>(std::llround(
                    (targetBeat - 0.01) * 500.0));
                const auto frame = hostFrame(1000.0, expectedOffset + 2, 0.01);
                const CrowdTimeField::InputEvent events[] {
                    input(CrowdTimeField::InputEvent::Type::On, 0, 0, 0),
                    input(CrowdTimeField::InputEvent::Type::Off, 0, 0, 1)
                };
                CrowdTimeField::OutputBlock output;
                field.process(config, frame, events, 2, output);
                everyLaneSurvived = everyLaneSurvived
                                 && ! output.resetRequested
                                 && output.mergedCount == 0
                                 && firstOffset(output,
                                      CrowdTimeField::OutputEvent::Type::Attack, 0)
                                      == expectedOffset;
            }
        }
        expect(everyLaneSurvived,
               "short taps survive one full lane cycle at every division/spread");
    }

    // A stable instance seed rotates all deterministic source lanes, preventing
    // otherwise-identical zones from stacking every attack on the same ticks.
    {
        auto configA = configFor(CrowdTimeField::Mode::Ensemble);
        configA.spreadSlots = 16;
        configA.laneSeed = 9000;
        auto configB = configA;
        configB.laneSeed = 9001;
        const auto frame = hostFrame(1000.0, 1800, 0.01);
        const CrowdTimeField::InputEvent events[] {
            input(CrowdTimeField::InputEvent::Type::On, 0, 0, 0),
            input(CrowdTimeField::InputEvent::Type::Off, 0, 0, 1)
        };
        CrowdTimeField a, b, sameAsA;
        CrowdTimeField::OutputBlock outA, outB, outSame;
        a.process(configA, frame, events, 2, outA);
        b.process(configB, frame, events, 2, outB);
        sameAsA.process(configA, frame, events, 2, outSame);
        const int offsetA = firstOffset(outA, CrowdTimeField::OutputEvent::Type::Attack);
        const int offsetB = firstOffset(outB, CrowdTimeField::OutputEvent::Type::Attack);
        const int offsetSame = firstOffset(outSame, CrowdTimeField::OutputEvent::Type::Attack);
        expect(offsetA >= 0 && offsetB >= 0 && offsetA != offsetB
                   && offsetA == offsetSame,
               "laneSeed decorrelates instances while remaining deterministic");
    }

    // A full 256-participant arrival is absorbed into fixed state. Per-step
    // onsets and active voices stay bounded; requests older than one beat merge.
    {
        CrowdTimeField field;
        auto config = configFor(CrowdTimeField::Mode::Ensemble);
        config.maxAttacksPerStep = 4;
        config.maxActive = 16;
        config.gatePercent = 100.0;
        config.spreadSlots = 1;
        const auto frame = hostFrame(1000.0, 501);
        std::array<CrowdTimeField::InputEvent, 256> events {};
        for (int source = 0; source < 256; ++source)
            events[static_cast<std::size_t>(source)]
                = input(CrowdTimeField::InputEvent::Type::On, source, 0, 0);

        CrowdTimeField::OutputBlock output;
        field.process(config, frame, events.data(), static_cast<int>(events.size()), output);

        bool perStepCap = true;
        for (int offset : { 0, 125, 250, 375, 500 })
        {
            int attacks = 0;
            for (int i = 0; i < output.count; ++i)
            {
                const auto& event = output.events[static_cast<std::size_t>(i)];
                if (event.type == CrowdTimeField::OutputEvent::Type::Attack
                    && event.sampleOffset == offset)
                    ++attacks;
            }
            perStepCap = perStepCap && attacks <= 4;
        }
        expect(! output.resetRequested && output.count <= 64
                   && perStepCap && output.activeCount <= 16,
               "256-source storm obeys output, per-step and active bounds");
        expect(output.pendingCount > 0 && output.mergedCount > 0,
               "storm backlog is retained fairly and one-beat expiries increment merged telemetry");
    }

    // Releases and attacks consume the fixed budget before refresh telemetry;
    // dropping motion is safe, dropping a lifecycle would not be.
    {
        CrowdTimeField field;
        auto config = configFor(CrowdTimeField::Mode::Grid);
        config.maxAttacksPerStep = 16;
        config.maxActive = 16;
        const auto frame = hostFrame(1000.0, 501);
        std::array<CrowdTimeField::InputEvent, 32> events {};
        for (int source = 0; source < 16; ++source)
        {
            events[static_cast<std::size_t>(source)]
                = input(CrowdTimeField::InputEvent::Type::On, source, 0, 0);
            events[static_cast<std::size_t>(source + 16)]
                = input(CrowdTimeField::InputEvent::Type::Off, source, 0, 400);
        }
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, events.data(), static_cast<int>(events.size()), output);
        expect(! output.resetRequested && output.count == 64
                   && countType(output, CrowdTimeField::OutputEvent::Type::Attack) == 16
                   && countType(output, CrowdTimeField::OutputEvent::Type::Release) == 16
                   && countType(output, CrowdTimeField::OutputEvent::Type::SampleMotion) == 32
                   && output.droppedMotionCount == 16,
               "64-event budget prioritises every Release and Attack over SampleMotion");
        expect(output.activeCount == 0,
               "budget pressure cannot leave a timed lifecycle stuck");
    }

    // Dense but valid offline blocks degrade pulse density instead of exceeding
    // the fixed contract and entering a reset/silence loop. Every admitted
    // attack reserves room for its mandatory release.
    {
        CrowdTimeField field;
        auto config = configFor(CrowdTimeField::Mode::Ensemble,
                                CrowdTimeField::Division::ThirtySecond);
        config.maxAttacksPerStep = 16;
        config.maxActive = 16;
        config.gatePercent = 100.0;
        config.spreadSlots = 1;
        auto frame = hostFrame(48000.0, 4096, 0.0, 240.0);
        std::array<CrowdTimeField::InputEvent, 16> events {};
        for (int source = 0; source < 16; ++source)
            events[static_cast<std::size_t>(source)]
                = input(CrowdTimeField::InputEvent::Type::On, source, 0, 0);

        CrowdTimeField::OutputBlock output;
        field.process(config, frame, events.data(), static_cast<int>(events.size()), output);
        std::array<bool, 16> sounding {};
        bool paired = true;
        for (int i = 0; i < output.count; ++i)
        {
            const auto& event = output.events[static_cast<std::size_t>(i)];
            if (event.sourceId < 0 || event.sourceId >= 16)
                continue;
            auto& isOn = sounding[static_cast<std::size_t>(event.sourceId)];
            if (event.type == CrowdTimeField::OutputEvent::Type::Attack)
            {
                paired = paired && ! isOn;
                isOn = true;
            }
            else if (event.type == CrowdTimeField::OutputEvent::Type::Release)
            {
                paired = paired && isOn;
                isOn = false;
            }
        }
        for (bool isOn : sounding)
            paired = paired && ! isOn;

        const bool firstBlockSafe = ! output.resetRequested && ! output.overflowed
                                 && output.count == 64
                                 && countType(output,
                                      CrowdTimeField::OutputEvent::Type::Attack) == 32
                                 && countType(output,
                                      CrowdTimeField::OutputEvent::Type::Release) == 32
                                 && paired && output.pendingCount == 16;

        frame = nextFrame(frame, 512);
        field.process(config, frame, nullptr, 0, output);
        expect(firstBlockSafe && ! output.resetRequested
                   && countType(output, CrowdTimeField::OutputEvent::Type::Attack) == 16
                   && output.activeCount == 16,
               "4096-sample dense pulses stay paired/capped and resume next block");
    }

    // Grid backlog still expires at one beat when the active cap prevents a tap
    // from being selected; Ensemble adds only the lane-cycle grace tested below.
    {
        CrowdTimeField field;
        auto config = configFor(CrowdTimeField::Mode::Grid);
        config.maxAttacksPerStep = 1;
        config.maxActive = 1;
        const auto frame = hostFrame(1000.0, 510, 0.01);
        const CrowdTimeField::InputEvent events[] {
            input(CrowdTimeField::InputEvent::Type::On, 0, 0, 0),
            input(CrowdTimeField::InputEvent::Type::On, 1, 0, 1),
            input(CrowdTimeField::InputEvent::Type::Off, 1, 0, 2)
        };
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, events, 3, output);
        expect(firstOffset(output, CrowdTimeField::OutputEvent::Type::Attack, 1) < 0
                   && output.pendingCount == 0 && output.mergedCount == 1,
               "capacity-blocked Grid short tap expires and merges at one beat");
    }

    // Mode, internal-tempo and PPQ discontinuities request an empty safety-reset
    // block. Host tempo automation stays in the continuous PPQ domain.
    {
        CrowdTimeField field;
        auto config = configFor(CrowdTimeField::Mode::Flow);
        auto frame = hostFrame(48000.0, 64);
        const auto event = input(CrowdTimeField::InputEvent::Type::On, 4, 0, 0);
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, &event, 1, output);

        frame = nextFrame(frame, 64);
        config.mode = CrowdTimeField::Mode::Grid;
        field.process(config, frame, nullptr, 0, output);
        expect(output.resetRequested && output.count == 0 && output.activeCount == 0,
               "mode change requests an empty safety reset");
    }
    {
        CrowdTimeField field;
        const auto config = configFor(CrowdTimeField::Mode::Grid);
        auto frame = hostFrame(48000.0, 64);
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, nullptr, 0, output);
        frame = nextFrame(frame, 64);
        frame.bpm = 121.0;
        field.process(config, frame, nullptr, 0, output);
        const bool changedWithoutReset = ! output.resetRequested
                                      && output.effectiveBpm == 121.0;
        frame = nextFrame(frame, 64);
        field.process(config, frame, nullptr, 0, output);
        expect(changedWithoutReset && ! output.resetRequested,
               "continuous host PPQ tolerates tempo automation without reset churn");
    }
    {
        CrowdTimeField field;
        auto config = configFor(CrowdTimeField::Mode::Grid);
        auto frame = hostFrame(48000.0, 64);
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, nullptr, 0, output);
        frame = nextFrame(frame, 64);
        config.internalBpm = 137.0;
        field.process(config, frame, nullptr, 0, output);
        expect(! output.resetRequested && output.effectiveBpm == 120.0,
               "hidden internal BPM automation does not reset a playing Host domain");
    }
    {
        CrowdTimeField field;
        auto config = configFor(CrowdTimeField::Mode::Grid);
        config.clockSource = CrowdTimeField::ClockSource::Internal;
        auto frame = hostFrame(48000.0, 64);
        frame.monotonicSeconds = 1.0;
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, nullptr, 0, output);
        const bool internalLocked = output.clockLocked;
        frame = nextFrame(frame, 64);
        config.internalBpm = 121.0;
        field.process(config, frame, nullptr, 0, output);
        expect(internalLocked && output.resetRequested && output.count == 0,
               "internal clock is locked and an internal BPM change resets its domain");
    }
    {
        CrowdTimeField field;
        auto config = configFor(CrowdTimeField::Mode::Grid);
        auto frame = hostFrame(48000.0, 64);
        frame.isPlaying = false;
        frame.monotonicSeconds = 1.0;
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, nullptr, 0, output);
        frame = nextFrame(frame, 64);
        config.internalBpm = 121.0;
        field.process(config, frame, nullptr, 0, output);
        expect(output.resetRequested,
               "Host fallback resets when its active internal BPM changes");
    }
    {
        CrowdTimeField field;
        const auto config = configFor(CrowdTimeField::Mode::Grid);
        auto frame = hostFrame(48000.0, 64);
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, nullptr, 0, output);
        frame = nextFrame(frame, 64);
        frame.ppqPosition += 8.0;
        field.process(config, frame, nullptr, 0, output);
        expect(output.resetRequested && output.count == 0,
               "host PPQ seek requests a clock-domain reset");
    }

    // Rehydration is authoritative replacement. Flow retriggers in a bounded
    // batch and a later canonical Off closes that voice normally.
    {
        CrowdTimeField field;
        const auto config = configFor(CrowdTimeField::Mode::Flow);
        auto frame = hostFrame(48000.0, 64);
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, nullptr, 0, output);
        frame = nextFrame(frame, 64);
        frame.ppqPosition += 4.0;
        field.process(config, frame, nullptr, 0, output);

        const CrowdTimeField::HeldVoice held {
            CrowdTimeField::voiceIdFor(7, 2), 7
        };
        const bool accepted = field.rehydrate(&held, 1) == 1;
        frame = nextFrame(frame, 64);
        field.process(config, frame, nullptr, 0, output);
        const bool attacked = countType(output, CrowdTimeField::OutputEvent::Type::Attack) == 1
                           && output.activeCount == 1;

        frame = nextFrame(frame, 64);
        const auto off = input(CrowdTimeField::InputEvent::Type::Off, 7, 2, 5);
        field.process(config, frame, &off, 1, output);
        expect(accepted && attacked
                   && firstOffset(output, CrowdTimeField::OutputEvent::Type::Release) == 5
                   && output.activeCount == 0,
               "canonical rehydrate retriggers, then Off closes without a stuck lifecycle");
    }

    // Host PPQ is primary when valid; otherwise two instances sharing the same
    // monotonic timestamp and internal BPM derive the same fallback grid.
    {
        auto config = configFor(CrowdTimeField::Mode::Grid);
        config.clockSource = CrowdTimeField::ClockSource::Host;
        config.internalBpm = 120.0;
        auto frame = hostFrame(1000.0, 130, 0.01);
        frame.hostValid = false;
        frame.monotonicSeconds = 0.005; // beat 0.01, next 1/16 is 120 samples away
        const auto event = input(CrowdTimeField::InputEvent::Type::On, 0, 0, 0);
        CrowdTimeField a;
        CrowdTimeField b;
        CrowdTimeField::OutputBlock outA, outB;
        a.process(config, frame, &event, 1, outA);
        b.process(config, frame, &event, 1, outB);
        expect(! outA.clockLocked && ! outB.clockLocked
                   && firstOffset(outA, CrowdTimeField::OutputEvent::Type::Attack)
                        == firstOffset(outB, CrowdTimeField::OutputEvent::Type::Attack)
                   && firstOffset(outA, CrowdTimeField::OutputEvent::Type::Attack) == 120,
               "invalid host falls back to a common monotonic-seconds grid");
    }

    // A selected Host clock follows the common monotonic grid while transport is
    // stopped. Starting transport changes to PPQ once, requesting one rehydrate;
    // the following continuous PPQ block does not reset again.
    {
        auto config = configFor(CrowdTimeField::Mode::Grid);
        config.clockSource = CrowdTimeField::ClockSource::Host;
        auto frame = hostFrame(1000.0, 130, 8.0);
        frame.isPlaying = false;
        frame.monotonicSeconds = 0.005;
        const auto event = input(CrowdTimeField::InputEvent::Type::On, 0, 0, 0);
        CrowdTimeField field;
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, &event, 1, output);
        const bool stoppedFallback = ! output.resetRequested && ! output.clockLocked
                                  && firstOffset(output,
                                      CrowdTimeField::OutputEvent::Type::Attack) == 120;

        frame = nextFrame(frame, 64);
        frame.isPlaying = true;
        frame.ppqPosition = 2.0;
        field.process(config, frame, nullptr, 0, output);
        const bool startReset = output.resetRequested && output.count == 0;

        frame = nextFrame(frame, 64);
        field.process(config, frame, nullptr, 0, output);
        expect(stoppedFallback && startReset && ! output.resetRequested
                   && output.clockLocked,
               "stopped Host uses fallback, then transport start resets exactly once");
    }

    // Rehydrate can safely precede the first clock frame. Its pending age is
    // anchored when the domain arrives instead of replaying years of expiries.
    {
        CrowdTimeField field;
        const CrowdTimeField::HeldVoice held {
            CrowdTimeField::voiceIdFor(255, 9), 255
        };
        const bool accepted = field.rehydrate(&held, 1) == 1;
        const auto config = configFor(CrowdTimeField::Mode::Grid);
        const auto frame = hostFrame(48000.0, 64, 100000.0);
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, nullptr, 0, output);
        expect(accepted && ! output.resetRequested && output.mergedCount == 0
                   && firstOffset(output, CrowdTimeField::OutputEvent::Type::Attack) == 0
                   && output.events[0].voiceId == CrowdTimeField::kMaxVoices - 1,
               "pre-domain rehydrate anchors once and accepts source 255 finger 9");
    }

    // An unusable Host PPQ falls back to the shared monotonic clock. If both
    // domains are numerically unrepresentable, timed scheduling fails closed
    // every block and recovers from a canonical held snapshot once valid again.
    {
        CrowdTimeField field;
        const auto config = configFor(CrowdTimeField::Mode::Grid);
        auto frame = hostFrame(48000.0, 64, 1.0e300);
        frame.monotonicSeconds = 0.0;
        const auto event = input(CrowdTimeField::InputEvent::Type::On, 0, 0, 0);
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, &event, 1, output);
        const bool usedFallback = ! output.resetRequested && ! output.clockLocked
                               && firstOffset(output,
                                   CrowdTimeField::OutputEvent::Type::Attack) == 0;

        field.reset();
        frame.monotonicSeconds = 1.0e300;
        field.process(config, frame, &event, 1, output);
        const bool firstFailure = output.resetRequested && output.overflowed
                               && output.count == 0 && output.pendingCount == 0
                               && output.activeCount == 0;
        const CrowdTimeField::HeldVoice held { 0, 0 };
        const bool firstRehydrate = field.rehydrate(&held, 1) == 1;
        frame = nextFrame(frame, 64);
        field.process(config, frame, nullptr, 0, output);
        const bool repeatedFailure = output.resetRequested && output.overflowed
                                  && output.count == 0 && output.pendingCount == 0
                                  && output.activeCount == 0;

        const bool secondRehydrate = field.rehydrate(&held, 1) == 1;
        frame.ppqPosition = 0.0;
        frame.monotonicSeconds = 0.0;
        field.process(config, frame, nullptr, 0, output);
        expect(usedFallback && firstFailure && firstRehydrate && repeatedFailure
                   && secondRehydrate && ! output.resetRequested
                   && firstOffset(output,
                       CrowdTimeField::OutputEvent::Type::Attack) == 0,
               "extreme clocks fail closed without stuck state and recover");
    }

    // The public pointer/count API fails closed before dereferencing an input
    // count beyond its fixed work contract.
    {
        CrowdTimeField field;
        const auto config = configFor(CrowdTimeField::Mode::Grid);
        const auto frame = hostFrame(48000.0, 64);
        const auto oneEvent = input(CrowdTimeField::InputEvent::Type::On, 0, 0, 0);
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, &oneEvent,
                      CrowdTimeField::kMaxInputEventsPerBlock + 1, output);
        expect(output.resetRequested && output.overflowed && output.count == 0,
               "oversized input count is rejected before unbounded work or access");

        field.process(config, nextFrame(frame, 64), nullptr, 1, output);
        expect(output.resetRequested && output.overflowed && output.count == 0,
               "null input with a positive count fails closed before access");

        expect(CrowdTimeField::sourceIdForVoice(-1) == -1
                   && CrowdTimeField::sourceIdForVoice(
                       CrowdTimeField::kMaxVoices) == -1,
               "invalid voice ids never alias a valid source");
    }

    // A Time Gate falling edge releases every sounding timed voice at the
    // exact edge, retains only genuinely held intent, and never manufactures a
    // ghost attack after that source releases while the gate is closed.
    {
        CrowdTimeField field;
        const auto config = configFor(CrowdTimeField::Mode::Grid);
        auto frame = hostFrame(48000.0, 512);
        const auto on = input(CrowdTimeField::InputEvent::Type::On, 7, 0, 0);
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, &on, 1, output);
        const bool started = countType(output, CrowdTimeField::OutputEvent::Type::Attack) == 1
                          && output.activeCount == 1;

        frame = nextFrame(frame, 512);
        const CrowdTimeField::InputEvent close {
            CrowdTimeField::InputEvent::Type::GateClose, -1, -1, 37
        };
        field.process(config, frame, &close, 1, output);
        const bool closed = firstOffset(output,
                                CrowdTimeField::OutputEvent::Type::Release, 7) == 37
                         && output.activeCount == 0 && output.pendingCount == 1
                         && ! field.isExternalGateOpen();

        frame = nextFrame(frame, 512);
        const auto off = input(CrowdTimeField::InputEvent::Type::Off, 7, 0, 0);
        field.process(config, frame, &off, 1, output);
        const bool cancelled = output.pendingCount == 0 && output.activeCount == 0
                            && output.count == 0;

        frame = nextFrame(frame, 8192);
        const CrowdTimeField::InputEvent open {
            CrowdTimeField::InputEvent::Type::GateOpen, -1, -1, 0
        };
        field.process(config, frame, &open, 1, output);
        expect(started && closed && cancelled
                   && field.isExternalGateOpen()
                   && countType(output, CrowdTimeField::OutputEvent::Type::Attack) == 0,
               "Time Gate close releases exactly and closed-window Off prevents ghost attack");
    }

    // Reopening does not create an unbounded edge burst. A held source returns
    // through the next normal grid tick and retains the scheduler's limits.
    {
        CrowdTimeField field;
        const auto config = configFor(CrowdTimeField::Mode::Grid);
        auto frame = hostFrame(48000.0, 512);
        const auto on = input(CrowdTimeField::InputEvent::Type::On, 9, 0, 0);
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, &on, 1, output);

        frame = nextFrame(frame, 6000);
        const CrowdTimeField::InputEvent close {
            CrowdTimeField::InputEvent::Type::GateClose, -1, -1, 12
        };
        field.process(config, frame, &close, 1, output);
        const bool heldPending = output.activeCount == 0 && output.pendingCount == 1;

        frame = nextFrame(frame, 6000);
        const CrowdTimeField::InputEvent open {
            CrowdTimeField::InputEvent::Type::GateOpen, -1, -1, 0
        };
        field.process(config, frame, &open, 1, output);
        expect(heldPending && field.isExternalGateOpen()
                   && countType(output, CrowdTimeField::OutputEvent::Type::Attack) == 1
                   && output.activeCount == 1 && output.pendingCount == 0,
               "Time Gate reopen admits held intent on the next normal grid tick");
    }

    // Global gate edges share the scheduler timeline with participant input.
    // At an identical offset, all input is consumed FIFO before the grid tick:
    // Close must suppress that tick, while Open must admit it deterministically.
    {
        CrowdTimeField field;
        const auto config = configFor(CrowdTimeField::Mode::Grid);
        auto frame = hostFrame(48000.0, 6000);
        const CrowdTimeField::InputEvent closeAtTick[] {
            input(CrowdTimeField::InputEvent::Type::On, 11, 0, 0),
            { CrowdTimeField::InputEvent::Type::GateClose, -1, -1, 0 }
        };
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, closeAtTick, 2, output);
        const bool closeWon = output.count == 0 && output.pendingCount == 1
                           && output.activeCount == 0
                           && ! field.isExternalGateOpen();

        frame = nextFrame(frame, 64); // exact next sixteenth-note boundary
        const CrowdTimeField::InputEvent openAtTick {
            CrowdTimeField::InputEvent::Type::GateOpen, -1, -1, 0
        };
        field.process(config, frame, &openAtTick, 1, output);
        expect(closeWon && output.activeCount == 1 && output.pendingCount == 0
                   && firstOffset(output,
                       CrowdTimeField::OutputEvent::Type::Attack, 11) == 0,
               "same-sample Time Gate edges are ordered before the grid tick");
    }

    // A closed rehydration snapshot remains silent and a real Off cancels its
    // pending re-arm before the first clock frame.
    {
        CrowdTimeField field;
        const CrowdTimeField::HeldVoice held {
            CrowdTimeField::voiceIdFor(3, 0), 3
        };
        const bool accepted = field.rehydrate(&held, 1, false) == 1;
        const auto config = configFor(CrowdTimeField::Mode::Ensemble);
        auto frame = hostFrame(48000.0, 512);
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, nullptr, 0, output);
        const bool stayedClosed = output.count == 0 && output.pendingCount == 1
                               && ! field.isExternalGateOpen();
        frame = nextFrame(frame, 512);
        const auto off = input(CrowdTimeField::InputEvent::Type::Off, 3, 0, 0);
        field.process(config, frame, &off, 1, output);
        expect(accepted && stayedClosed && output.pendingCount == 0
                   && output.activeCount == 0 && output.count == 0,
               "closed Time Gate rehydrate stays silent and Off cancels re-arm");
    }

    // Performance gate: empty and 16-active Grid states process realistic runs
    // of one-sample callbacks without any per-callback 2,560-voice scan.
    {
        constexpr int iterations = 100000;
        const auto config = configFor(CrowdTimeField::Mode::Grid);
        CrowdTimeField::OutputBlock output;

        CrowdTimeField empty;
        auto emptyFrame = hostFrame(48000.0, 64, 0.001);
        empty.process(config, emptyFrame, nullptr, 0, output);
        emptyFrame = nextFrame(emptyFrame, 1);

        CrowdTimeField active;
        auto activeFrame = hostFrame(48000.0, 64);
        std::array<CrowdTimeField::InputEvent, 16> held {};
        for (int source = 0; source < 16; ++source)
            held[static_cast<std::size_t>(source)]
                = input(CrowdTimeField::InputEvent::Type::On, source, 0, 0);
        active.process(config, activeFrame, held.data(), static_cast<int>(held.size()), output);
        activeFrame = nextFrame(activeFrame, 1);

        const auto started = std::chrono::steady_clock::now();
        bool stayedStable = true;
        for (int i = 0; i < iterations; ++i)
        {
            empty.process(config, emptyFrame, nullptr, 0, output);
            stayedStable = stayedStable && ! output.resetRequested;
            emptyFrame = nextFrame(emptyFrame, 1);
        }
        for (int i = 0; i < iterations; ++i)
        {
            active.process(config, activeFrame, nullptr, 0, output);
            stayedStable = stayedStable && ! output.resetRequested
                         && output.activeCount == 16;
            activeFrame = nextFrame(activeFrame, 1);
        }
        const double seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started).count();
        expect(stayedStable && seconds < 2.5,
               "block-1 empty/16-active hot path avoids full-ledger callback scans");
    }

    // Process is mechanically watched for operator-new traffic as a regression
    // check in addition to the fixed-array API and noexcept contract.
    {
        CrowdTimeField field;
        const auto config = configFor(CrowdTimeField::Mode::Ensemble);
        const auto frame = hostFrame(48000.0, 64);
        const auto event = input(CrowdTimeField::InputEvent::Type::On, 0, 0, 0);
        CrowdTimeField::OutputBlock output;
        watchedAllocations = 0;
        watchAllocations = true;
        field.process(config, frame, &event, 1, output);
        watchAllocations = false;
        expect(watchedAllocations == 0,
               "process performs no heap allocations");
    }

    // A Flow burst larger than the mandatory output contract explicitly asks
    // for safety reset instead of silently dropping a lifecycle transition.
    {
        CrowdTimeField field;
        const auto config = configFor(CrowdTimeField::Mode::Flow);
        const auto frame = hostFrame(48000.0, 64);
        std::array<CrowdTimeField::InputEvent, 65> events {};
        for (int source = 0; source < 65; ++source)
            events[static_cast<std::size_t>(source)]
                = input(CrowdTimeField::InputEvent::Type::On, source, 0, 0);
        CrowdTimeField::OutputBlock output;
        field.process(config, frame, events.data(), static_cast<int>(events.size()), output);
        expect(output.resetRequested && output.overflowed && output.count == 0
                   && output.activeCount == 0,
               "mandatory overflow fails closed with reset and no stuck internal voices");
    }

    return failures == 0 ? 0 : 1;
}
