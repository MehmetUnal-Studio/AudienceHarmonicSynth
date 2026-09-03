#include "../Source/CrowdSimulatorModel.h"
#include "../Source/MidiAudienceModel.h"
#include "../Source/Simulator.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <new>
#include <numeric>
#include <utility>
#include <vector>

namespace AllocationProbe
{
    std::atomic<bool> enabled { false };
    std::atomic<std::size_t> count { 0 };
}

void* operator new (std::size_t size)
{
    if (AllocationProbe::enabled.load (std::memory_order_relaxed))
        AllocationProbe::count.fetch_add (1, std::memory_order_relaxed);
    if (void* memory = std::malloc (size))
        return memory;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)
{
    return ::operator new (size);
}

void operator delete (void* memory) noexcept { std::free (memory); }
void operator delete[] (void* memory) noexcept { std::free (memory); }
void operator delete (void* memory, std::size_t) noexcept { std::free (memory); }
void operator delete[] (void* memory, std::size_t) noexcept { std::free (memory); }

static_assert (std::is_nothrow_constructible<CrowdSimulatorModel, int,
                                              std::uint64_t>::value,
               "Crowd simulator construction must remain noexcept");
static_assert (noexcept (std::declval<CrowdSimulatorModel&>().advance (
                   true, std::declval<CrowdSimulatorModel::EventBuffer&>())),
               "Crowd simulator advance must remain noexcept");
static_assert (noexcept (std::declval<CrowdSimulatorModel&>().setSourceCapacity (
                   64, std::declval<CrowdSimulatorModel::EventBuffer&>())),
               "Crowd simulator capacity mutation must remain noexcept");
static_assert (std::is_trivially_copyable<CrowdSimulatorModel::Event>::value
                   && std::is_trivially_copyable<
                       CrowdSimulatorModel::ParticipantSnapshot>::value,
               "Crowd simulator transport and snapshot types must remain POD-like");

struct SimulatorTestAccess
{
    static void tick (Simulator& simulator) { simulator.timerCallback(); }
    static void stopTimer (Simulator& simulator) { simulator.stopTimer(); }
};

namespace
{
    int failures = 0;

    void expect (bool condition, const char* name)
    {
        std::cout << (condition ? "PASS  " : "FAIL  ") << name << '\n';
        if (! condition)
            ++failures;
    }

    class CapturingSink final : public SeatEventSink
    {
    public:
        enum class Type { x, y, on, off };
        struct Event { Type type; int sourceId; float value; };

        void setX (int, int sourceId, float value) override
        {
            validValues = validValues && std::isfinite (value)
                       && value >= 0.0f && value <= 1.0f;
            quantisedValues = quantisedValues
                && std::abs (value * 100.0f - std::round (value * 100.0f)) < 1.0e-4f;
            events.push_back ({ Type::x, sourceId, value });
            ++xCount;
        }

        void setY (int, int sourceId, float value) override
        {
            validValues = validValues && std::isfinite (value)
                       && value >= 0.0f && value <= 1.0f;
            quantisedValues = quantisedValues
                && std::abs (value * 100.0f - std::round (value * 100.0f)) < 1.0e-4f;
            events.push_back ({ Type::y, sourceId, value });
            ++yCount;
        }

        void setOn (int, int sourceId, bool on) override
        {
            if (sourceId < 0 || sourceId >= MAX_OSC_SOURCES)
            {
                validIds = false;
                return;
            }

            if (on)
            {
                const bool ordered = events.size() >= 2
                    && events[events.size() - 2].type == Type::x
                    && events[events.size() - 2].sourceId == sourceId
                    && events[events.size() - 1].type == Type::y
                    && events[events.size() - 1].sourceId == sourceId;
                orderedOnsets = orderedOnsets && ordered;
                duplicateLifecycle = duplicateLifecycle
                    || active[static_cast<std::size_t> (sourceId)];
                ++onCount[static_cast<std::size_t> (sourceId)];
                active[static_cast<std::size_t> (sourceId)] = true;
                events.push_back ({ Type::on, sourceId, 1.0f });
            }
            else
            {
                duplicateLifecycle = duplicateLifecycle
                    || ! active[static_cast<std::size_t> (sourceId)];
                ++offCount[static_cast<std::size_t> (sourceId)];
                active[static_cast<std::size_t> (sourceId)] = false;
                events.push_back ({ Type::off, sourceId, 0.0f });
            }
        }

        int activeCount() const
        {
            return static_cast<int> (std::count (active.begin(), active.end(), true));
        }

        int sourcesSeenOn() const
        {
            return static_cast<int> (std::count_if (onCount.begin(), onCount.end(),
                                                    [] (int value) { return value > 0; }));
        }

        int sourcesSeenOff() const
        {
            return static_cast<int> (std::count_if (offCount.begin(), offCount.end(),
                                                    [] (int value) { return value > 0; }));
        }

        int sourcesReactivated() const
        {
            return static_cast<int> (std::count_if (onCount.begin(), onCount.end(),
                                                    [] (int value) { return value > 1; }));
        }

        std::array<bool, MAX_OSC_SOURCES> active {};
        std::array<int, MAX_OSC_SOURCES> onCount {};
        std::array<int, MAX_OSC_SOURCES> offCount {};
        std::vector<Event> events;
        int xCount = 0;
        int yCount = 0;
        bool validIds = true;
        bool validValues = true;
        bool quantisedValues = true;
        bool orderedOnsets = true;
        bool duplicateLifecycle = false;
    };

    double quantile (std::vector<double> values, double fraction)
    {
        if (values.empty())
            return 0.0;
        std::sort (values.begin(), values.end());
        const auto index = static_cast<std::size_t> (
            fraction * static_cast<double> (values.size() - 1));
        return values[index];
    }

    bool sameSnapshot (const CrowdSimulatorModel::ParticipantSnapshot& left,
                       const CrowdSimulatorModel::ParticipantSnapshot& right)
    {
        return left.occupied == right.occupied
            && left.active == right.active
            && left.held == right.held
            && left.row == right.row
            && std::abs (left.x - right.x) < std::numeric_limits<float>::epsilon()
            && std::abs (left.y - right.y) < std::numeric_limits<float>::epsilon()
            && left.lifecycleRemainingMs == right.lifecycleRemainingMs;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    // +1 Held stays pressed indefinitely and remains the deterministic mapping
    // test. Motion is intentionally a separate switch.
    {
        CapturingSink sink;
        Simulator simulator (sink, 64);
        simulator.setSeed (1234);
        simulator.addRandomSeat();
        SimulatorTestAccess::stopTimer (simulator);

        for (int tick = 0; tick < 400; ++tick)
            SimulatorTestAccess::tick (simulator);

        expect (simulator.getSimSeatCount() == 1
                    && simulator.getHeldSeatCount() == 1
                    && simulator.getCrowdParticipantCount() == 0
                    && simulator.getActiveSimSeatCount() == 1
                    && sink.activeCount() == 1
                    && sink.sourcesSeenOff() == 0,
                "+1 Held remains active until explicit Remove or Clear");

        const int xBefore = sink.xCount;
        const int yBefore = sink.yCount;
        simulator.setRandomMovement (true);
        SimulatorTestAccess::stopTimer (simulator);
        SimulatorTestAccess::tick (simulator);
        SimulatorTestAccess::tick (simulator);
        expect (sink.xCount == xBefore + 1 && sink.yCount == yBefore + 1,
                "movement follows the measured 20 Hz logical coordinate cadence");
        expect (sink.validValues && sink.quantisedValues,
                "simulated U/V values stay finite, bounded and quantised to 0.01");
        simulator.clear();
        expect (sink.activeCount() == 0 && simulator.getSimSeatCount() == 0,
                "Clear releases the held mapping-test touch");
    }

    // +25 Crowd is a stable identity pool. Cards pulse through measured human
    // gestures, but source IDs do not disappear from represented population.
    {
        CapturingSink sink;
        Simulator simulator (sink, 64);
        simulator.setSeed (987654321);
        simulator.addCrowdParticipants (25);
        SimulatorTestAccess::stopTimer (simulator);

        expect (simulator.getSimSeatCount() == 25
                    && simulator.getCrowdParticipantCount() == 25
                    && simulator.getActiveCrowdParticipantCount() == 0
                    && sink.activeCount() == 0,
                "+25 Crowd creates an idle pool without a simultaneous attack burst");

        bool canonicalCountStayedExact = true;
        for (int tick = 0; tick < 1600; ++tick) // 40 s covers the 25 s maximum drag.
        {
            SimulatorTestAccess::tick (simulator);
            canonicalCountStayedExact = canonicalCountStayedExact
                && sink.activeCount() == simulator.getActiveSimSeatCount()
                && simulator.getActiveCrowdParticipantCount()
                     == simulator.getActiveSimSeatCount();
        }

        expect (canonicalCountStayedExact
                    && simulator.getCrowdParticipantCount() == 25,
                "crowd pool stays constant while active cards appear and disappear");
        expect (sink.validIds && sink.sourcesSeenOn() == 25
                    && sink.sourcesSeenOff() == 25
                    && sink.sourcesReactivated() == 25,
                "all 25 source-derived identities complete Off and reactivate");
        expect (sink.orderedOnsets && ! sink.duplicateLifecycle,
                "each lifecycle is exactly U, V, On ... Off with no duplicate edge");

        const int activeBeforeClear = simulator.getActiveSimSeatCount();
        const auto offBefore = sink.offCount;
        simulator.clear();
        int newlyReleased = 0;
        for (int sourceId = 0; sourceId < SeatEventSink::MAX_OSC_SOURCES; ++sourceId)
            newlyReleased += sink.offCount[static_cast<std::size_t> (sourceId)]
                           - offBefore[static_cast<std::size_t> (sourceId)];
        expect (newlyReleased == activeBeforeClear && sink.activeCount() == 0
                    && simulator.getSimSeatCount() == 0
                    && simulator.getActiveSimSeatCount() == 0,
                "Clear emits exactly one Off for each active simulated finger");
    }

    // Public helper counts are bounded before looping, including hostile input.
    {
        CapturingSink sink;
        Simulator simulator (sink, 32);
        simulator.addCrowdParticipants (std::numeric_limits<int>::max());
        SimulatorTestAccess::stopTimer (simulator);
        simulator.addRandomSeats (std::numeric_limits<int>::max());

        expect (simulator.getSimSeatCount() == 32
                    && simulator.getCrowdParticipantCount() == 32
                    && simulator.getHeldSeatCount() == 0,
                "participant additions clamp to configured fixed capacity");
        simulator.clear();
    }

    // Capacity mutation reuses fixed storage. Shrink emits one deterministic
    // Off per active removed identity and keeps all lower participant state.
    {
        CrowdSimulatorModel model (128, 7788);
        CrowdSimulatorModel::EventBuffer events;
        for (int sourceId = 0; sourceId < 64; ++sourceId)
            model.addCrowdParticipant (events);
        for (int sourceId = 64; sourceId < 128; ++sourceId)
            model.addHeldParticipant (events);

        const auto source0Before = model.getParticipantSnapshot (0);
        const auto source63Before = model.getParticipantSnapshot (63);
        AllocationProbe::count.store (0, std::memory_order_relaxed);
        AllocationProbe::enabled.store (true, std::memory_order_relaxed);
        const auto released = model.setSourceCapacity (64, events);
        AllocationProbe::enabled.store (false, std::memory_order_relaxed);

        bool ordered = released == 64;
        for (std::size_t index = 0; index < released; ++index)
            ordered = ordered
                && events[index].type == CrowdSimulatorModel::EventType::off
                && events[index].sourceId == 64 + static_cast<int> (index);

        expect(ordered && AllocationProbe::count.load (
                    std::memory_order_relaxed) == 0
                    && model.getSourceCapacity() == 64
                    && model.getPopulation() == 64
                    && model.getHeldPopulation() == 0
                    && model.getCrowdPopulation() == 64
                    && model.getActivePopulation() == 0
                    && model.getActiveCrowdPopulation() == 0
                    && sameSnapshot (source0Before,
                                     model.getParticipantSnapshot (0))
                    && sameSnapshot (source63Before,
                                     model.getParticipantSnapshot (63))
                    && ! model.getParticipantSnapshot (64).occupied,
               "model shrink is allocation-free, ordered and preserves lower participant state");

        const auto rejected = model.addHeldParticipant (events);
        const auto expansionReleases = model.setSourceCapacity (128, events);
        const auto addedAfterExpansion = model.addHeldParticipant (events);
        expect(rejected.sourceId == -1 && expansionReleases == 0
                    && addedAfterExpansion.sourceId == 64
                    && model.getPopulation() == 65
                    && model.getHeldPopulation() == 1
                    && model.getCrowdPopulation() == 64
                    && model.getActivePopulation() == 1
                    && sameSnapshot (source0Before,
                                     model.getParticipantSnapshot (0)),
               "expansion exposes empty upper slots without disturbing existing identities");
    }

    // The Simulator adapter dispatches shrink Offs synchronously. This lets an
    // owner narrow the simulator first, then close MidiAudienceModel admission,
    // without losing the cleanup messages at the new boundary.
    {
        OscFingerRouter router;
        MidiAudienceModel audience (router);
        Simulator simulator (audience, 128);
        simulator.addRandomSeats (128);
        SimulatorTestAccess::stopTimer (simulator);
        router.discardPendingEvents();

        const auto released = simulator.setSourceCapacity (64);
        std::array<OscFingerRouter::Event, 128> releaseEvents {};
        const int releaseCount = router.drain (
            releaseEvents.data(), (int) releaseEvents.size());
        bool ordered = released == 64 && releaseCount == 64;
        for (int index = 0; index < releaseCount; ++index)
            ordered = ordered
                && releaseEvents[(size_t) index].type
                       == OscFingerRouter::Event::Off
                && releaseEvents[(size_t) index].sourceId == 64 + index;

        const int audienceReleased = audience.setSourceCapacity (64);
        expect(ordered && audienceReleased == 0
                    && simulator.getSourceCapacity() == 64
                    && simulator.getSimSeatCount() == 64
                    && simulator.getHeldSeatCount() == 64
                    && audience.getSourceCapacity() == 64
                    && audience.getActiveSourceCount() == 64,
               "adapter shrink sends upper Offs before downstream admission closes");

        audience.setSourceCapacity (128);
        simulator.setSourceCapacity (128);
        simulator.addRandomSeats (64);
        expect(simulator.getSimSeatCount() == 128
                    && audience.getActiveSourceCount() == 128
                    && audience.getCapacityDroppedEventCount() == 0,
               "target-first expansion safely restores the full dense simulator range");
        simulator.clear();
    }

    // Governor-facing atomics and the canonical audience ledger agree on every
    // tick; simulator events never arm the live-OSC stale-touch watchdog.
    {
        OscFingerRouter router;
        MidiAudienceModel model (router);
        Simulator simulator (model, 32);
        simulator.addCrowdParticipants (25);
        SimulatorTestAccess::stopTimer (simulator);

        bool modelStayedCoherent = true;
        for (int tick = 0; tick < 1600; ++tick)
        {
            SimulatorTestAccess::tick (simulator);
            modelStayedCoherent = modelStayedCoherent
                && model.getActiveSourceCount() == simulator.getActiveSimSeatCount();
        }
        expect (modelStayedCoherent,
                "MidiAudienceModel active cards match simulator telemetry");

        simulator.clear();
        expect (model.getActiveSourceCount() == 0
                    && model.getActiveFingerCount() == 0,
                "simulator teardown leaves no held source or finger");
    }

    // Profile/seed are ephemeral simulator controls, not session parameters.
    {
        CapturingSink sink;
        Simulator simulator (sink, 16);
        expect (simulator.getProfile() == Simulator::Profile::human,
                "Human is the default simulator profile");
        simulator.setProfile (Simulator::Profile::dense);
        simulator.setSeed (UINT64_C (0x123456789abcdef0));
        expect (simulator.getProfile() == Simulator::Profile::dense
                    && simulator.getSeed() == UINT64_C (0x123456789abcdef0),
                "profile and global seed round-trip without APVTS state");
    }

    // The same seed is reproducible. A different seed changes behaviour.
    {
        CrowdSimulatorModel left (16, 424242);
        CrowdSimulatorModel right (16, 424242);
        CrowdSimulatorModel different (16, 424243);
        CrowdSimulatorModel::EventBuffer leftEvents, rightEvents, differentEvents;
        for (int i = 0; i < 16; ++i)
        {
            left.addCrowdParticipant (leftEvents);
            right.addCrowdParticipant (rightEvents);
            different.addCrowdParticipant (differentEvents);
        }

        bool identical = true;
        bool seedChangedTrace = false;
        for (int tick = 0; tick < 800; ++tick)
        {
            left.advance (true, leftEvents);
            right.advance (true, rightEvents);
            different.advance (true, differentEvents);
            for (int source = 0; source < 16; ++source)
            {
                const auto a = left.getParticipantSnapshot (source);
                identical = identical
                    && sameSnapshot (a, right.getParticipantSnapshot (source));
                seedChangedTrace = seedChangedTrace
                    || ! sameSnapshot (a, different.getParticipantSnapshot (source));
            }
        }
        expect (identical, "same seed reproduces every participant trace exactly");
        expect (seedChangedTrace, "changing the seed changes generated behaviour");
    }

    // Source-derived PRNG streams isolate participant physics/lifecycle from
    // population size. Aggregate pacing may downsample output at high N, but
    // it never feeds back into the first ten underlying traces.
    {
        CrowdSimulatorModel small (16, 20260823);
        CrowdSimulatorModel medium (100, 20260823);
        CrowdSimulatorModel large (256, 20260823);
        CrowdSimulatorModel::EventBuffer events;
        for (int i = 0; i < 16; ++i) small.addCrowdParticipant (events);
        for (int i = 0; i < 100; ++i) medium.addCrowdParticipant (events);
        for (int i = 0; i < 256; ++i) large.addCrowdParticipant (events);

        bool invariant = true;
        for (int tick = 0; tick < 1600; ++tick)
        {
            small.advance (true, events);
            medium.advance (true, events);
            large.advance (true, events);
            for (int source = 0; source < 10; ++source)
            {
                const auto reference = small.getParticipantSnapshot (source);
                invariant = invariant
                    && sameSnapshot (reference, medium.getParticipantSnapshot (source))
                    && sameSnapshot (reference, large.getParticipantSnapshot (source));
            }
        }
        expect (invariant,
                "first ten traces are invariant at populations 16, 100 and 256");
    }

    // Long-run motion checks use the same 50 ms cadence as the phone capture.
    // Tolerances intentionally cover natural seeds rather than one exact trace.
    {
        CrowdSimulatorModel model (1, UINT64_C (0x123456789));
        CrowdSimulatorModel::EventBuffer events;
        model.addHeldParticipant (events);
        std::vector<double> x, y;
        bool validAndQuantised = true;
        int edgeBandComponents = 0;
        int railComponents = 0;

        for (int tick = 0; tick < 40000; ++tick)
        {
            const auto count = model.advance (true, events);
            for (std::size_t i = 0; i + 1 < count; ++i)
            {
                if (events[i].type != CrowdSimulatorModel::EventType::x
                    || events[i + 1].type != CrowdSimulatorModel::EventType::y)
                    continue;
                const double xValue = events[i].value;
                const double yValue = events[i + 1].value;
                validAndQuantised = validAndQuantised
                    && std::isfinite (xValue) && std::isfinite (yValue)
                    && xValue >= 0.0 && xValue <= 1.0
                    && yValue >= 0.0 && yValue <= 1.0
                    && std::abs (xValue * 100.0 - std::round (xValue * 100.0)) < 1.0e-4
                    && std::abs (yValue * 100.0 - std::round (yValue * 100.0)) < 1.0e-4;
                edgeBandComponents += (xValue < 0.05 || xValue > 0.95) ? 1 : 0;
                edgeBandComponents += (yValue < 0.05 || yValue > 0.95) ? 1 : 0;
                railComponents += (xValue == 0.0 || xValue == 1.0) ? 1 : 0;
                railComponents += (yValue == 0.0 || yValue == 1.0) ? 1 : 0;
                x.push_back (xValue);
                y.push_back (yValue);
            }
        }

        std::vector<double> steps, speeds, deltaX, deltaY;
        int exactPauses = 0;
        for (std::size_t i = 1; i < x.size(); ++i)
        {
            const double dx = x[i] - x[i - 1];
            const double dy = y[i] - y[i - 1];
            const double step = std::hypot (dx, dy);
            deltaX.push_back (dx);
            deltaY.push_back (dy);
            steps.push_back (step);
            speeds.push_back (step / 0.05);
            exactPauses += step == 0.0 ? 1 : 0;
        }

        int decisiveTurns = 0;
        for (std::size_t i = 1; i < deltaX.size(); ++i)
        {
            const double previousLength = std::hypot (deltaX[i - 1], deltaY[i - 1]);
            const double currentLength = std::hypot (deltaX[i], deltaY[i]);
            if (previousLength > 0.001 && currentLength > 0.001
                && (deltaX[i - 1] * deltaX[i] + deltaY[i - 1] * deltaY[i]) < 0.0)
                ++decisiveTurns;
        }

        const double meanDx = std::accumulate (deltaX.begin(), deltaX.end(), 0.0)
                            / static_cast<double> (deltaX.size());
        double lagCovariance = 0.0;
        double lagVariance = 0.0;
        for (std::size_t i = 1; i < deltaX.size(); ++i)
        {
            lagCovariance += (deltaX[i - 1] - meanDx) * (deltaX[i] - meanDx);
            lagVariance += (deltaX[i - 1] - meanDx) * (deltaX[i - 1] - meanDx);
        }

        const double pauseFraction = static_cast<double> (exactPauses)
                                   / static_cast<double> (steps.size());
        const double turnFraction = static_cast<double> (decisiveTurns)
                                  / static_cast<double> (deltaX.size() - 1);
        const double lagOne = lagVariance > 0.0 ? lagCovariance / lagVariance : 0.0;
        const double edgeBandFraction = static_cast<double> (edgeBandComponents)
                                      / static_cast<double> (x.size() * 2);
        const double railFraction = static_cast<double> (railComponents)
                                  / static_cast<double> (x.size() * 2);

        expect (validAndQuantised && x.size() == 20000,
                "long-run movement remains finite/bounded at exactly 20 coordinate frames/s");
        expect (pauseFraction >= 0.05 && pauseFraction <= 0.13,
                "one permanently-held persona stays in its 5-13 percent pause envelope");
        expect (quantile (speeds, 0.5) >= 0.10 && quantile (speeds, 0.5) <= 2.50
                    && quantile (speeds, 0.95) >= 1.0
                    && quantile (speeds, 0.95) <= 6.0,
                "one held persona stays inside the Real10 per-source speed envelope");
        expect (turnFraction >= 0.07 && turnFraction <= 0.16
                    && lagOne >= 0.35 && lagOne <= 0.78,
                "paths combine persistent velocity with measured decisive turns");
        expect (edgeBandFraction <= 0.06 && railFraction < 0.005,
                "soft boundary keeps trajectories interior-biased without rail sticking");
    }

    // The model is fixed-capacity and its hot control-thread advance performs
    // no heap work. The allocation counter is enabled only around pure ticks.
    {
        CrowdSimulatorModel model (256, 99);
        CrowdSimulatorModel::EventBuffer events;
        for (int i = 0; i < 256; ++i)
            model.addHeldParticipant (events);
        AllocationProbe::count.store (0, std::memory_order_relaxed);
        AllocationProbe::enabled.store (true, std::memory_order_relaxed);
        for (int tick = 0; tick < 1000; ++tick)
            model.advance (true, events);
        AllocationProbe::enabled.store (false, std::memory_order_relaxed);
        expect (AllocationProbe::count.load (std::memory_order_relaxed) == 0,
                "fixed-capacity model advances 256 participants without heap allocation");
    }

    // Human lifecycle is the four-class mixture measured across ten real
    // phones. Observable duration bands intentionally test the capture-facing
    // result, including 25 ms timer quantisation, rather than private enums.
    {
        CrowdSimulatorModel model (64, 123);
        CrowdSimulatorModel::EventBuffer events;
        for (int i = 0; i < 64; ++i)
            model.addCrowdParticipant (events);

        std::array<int, CrowdSimulatorModel::maxParticipants> onTick;
        onTick.fill (-1);
        std::vector<double> durations;
        bool lifecycleOrdered = true;
        std::array<bool, CrowdSimulatorModel::maxParticipants> active {};

        for (int tick = 0; tick < 40000; ++tick)
        {
            const auto count = model.advance (false, events);
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto& event = events[i];
                if (event.type == CrowdSimulatorModel::EventType::on)
                {
                    lifecycleOrdered = lifecycleOrdered && i >= 2
                        && events[i - 2].type == CrowdSimulatorModel::EventType::x
                        && events[i - 1].type == CrowdSimulatorModel::EventType::y
                        && events[i - 2].sourceId == event.sourceId
                        && events[i - 1].sourceId == event.sourceId
                        && ! active[static_cast<std::size_t> (event.sourceId)];
                    active[static_cast<std::size_t> (event.sourceId)] = true;
                    onTick[static_cast<std::size_t> (event.sourceId)] = tick;
                }
                else if (event.type == CrowdSimulatorModel::EventType::off)
                {
                    const int started = onTick[static_cast<std::size_t> (event.sourceId)];
                    lifecycleOrdered = lifecycleOrdered && started >= 0
                        && active[static_cast<std::size_t> (event.sourceId)];
                    active[static_cast<std::size_t> (event.sourceId)] = false;
                    if (started >= 0)
                        durations.push_back ((tick - started)
                            * CrowdSimulatorModel::tickIntervalMs * 0.001);
                    onTick[static_cast<std::size_t> (event.sourceId)] = -1;
                }
            }
        }

        int under250Ms = 0, over3000Ms = 0;
        for (const double duration : durations)
        {
            if (duration < 0.250) ++under250Ms;
            if (duration > 3.0) ++over3000Ms;
        }
        const double count = static_cast<double> (durations.size());
        expect (lifecycleOrdered && durations.size() > 5000,
                "long-run lifecycle remains ordered with ample independent samples");
        expect (under250Ms / count >= 0.56 && under250Ms / count <= 0.66
                    && over3000Ms / count >= 0.025 && over3000Ms / count <= 0.070,
                "Human lifecycle matches Real10 short-touch and long-tail fractions");
        expect (quantile (durations, 0.5) >= 0.10
                    && quantile (durations, 0.5) <= 0.20
                    && quantile (durations, 0.95) >= 2.0
                    && quantile (durations, 0.95) <= 3.8
                    && *std::min_element (durations.begin(), durations.end()) >= 0.025
                    && *std::max_element (durations.begin(), durations.end()) <= 20.025,
                "Human gesture median, p95 and bounded extended tail match Real10");
    }

    // Eighty seconds with ten independent sources is the same validation
    // window used for the Real10 capture comparison. These are deliberately
    // broad source-bootstrap bands, not a seed-specific golden trace.
    {
        struct MotionState
        {
            bool active = false;
            bool haveCoordinate = false;
            bool changedDuringGesture = false;
            int onTick = -1;
            int coordinateTick = -1;
            int samePairRun = 0;
            int longestSamePairRun = 0;
            int horizontalSteps = 0;
            int verticalSteps = 0;
            double x = 0.0;
            double y = 0.0;
            std::vector<double> speeds;
        };

        CrowdSimulatorModel model (10, UINT64_C (20260824));
        CrowdSimulatorModel::EventBuffer events;
        for (int source = 0; source < 10; ++source)
            model.addCrowdParticipant (events);

        std::array<MotionState, 10> states;
        std::array<int, 4> cadenceCounts {};
        std::vector<double> steps;
        std::vector<double> speeds;
        int invalidCadence = 0;
        int exactPauses = 0;
        int microSteps = 0;
        int largeSteps = 0;
        int taps = 0;
        int unchangedTaps = 0;

        for (int tick = 0; tick < 3200; ++tick)
        {
            const auto count = model.advance (true, events);
            for (std::size_t i = 0; i < count;)
            {
                const auto& event = events[i];
                const int source = event.sourceId;
                if (source < 0 || source >= 10)
                {
                    ++invalidCadence;
                    ++i;
                    continue;
                }
                auto& state = states[static_cast<std::size_t> (source)];

                if (event.type == CrowdSimulatorModel::EventType::off)
                {
                    if (state.active && state.onTick >= 0
                        && (tick - state.onTick)
                           * CrowdSimulatorModel::tickIntervalMs <= 250)
                    {
                        ++taps;
                        if (! state.changedDuringGesture)
                            ++unchangedTaps;
                    }
                    state.active = false;
                    state.haveCoordinate = false;
                    state.onTick = -1;
                    state.samePairRun = 0;
                    ++i;
                    continue;
                }

                const bool coordinatePair = event.type
                        == CrowdSimulatorModel::EventType::x
                    && i + 1 < count
                    && events[i + 1].type == CrowdSimulatorModel::EventType::y
                    && events[i + 1].sourceId == source;
                if (! coordinatePair)
                {
                    ++i;
                    continue;
                }

                const double nextX = event.value;
                const double nextY = events[i + 1].value;
                const bool attack = i + 2 < count
                    && events[i + 2].type == CrowdSimulatorModel::EventType::on
                    && events[i + 2].sourceId == source;
                if (attack)
                {
                    state.active = true;
                    state.haveCoordinate = true;
                    state.changedDuringGesture = false;
                    state.onTick = tick;
                    state.coordinateTick = tick;
                    state.samePairRun = 0;
                    state.x = nextX;
                    state.y = nextY;
                    i += 3;
                    continue;
                }

                if (state.active && state.haveCoordinate)
                {
                    const int elapsedTicks = tick - state.coordinateTick;
                    if (elapsedTicks == 2) ++cadenceCounts[0];
                    else if (elapsedTicks == 4) ++cadenceCounts[1];
                    else if (elapsedTicks == 6) ++cadenceCounts[2];
                    else if (elapsedTicks == 8) ++cadenceCounts[3];
                    else ++invalidCadence;

                    const double dx = nextX - state.x;
                    const double dy = nextY - state.y;
                    const double step = std::hypot (dx, dy);
                    const double seconds = elapsedTicks
                        * CrowdSimulatorModel::tickIntervalMs * 0.001;
                    const double speed = seconds > 0.0 ? step / seconds : 0.0;
                    steps.push_back (step);
                    speeds.push_back (speed);
                    state.speeds.push_back (speed);
                    exactPauses += step < 1.0e-9 ? 1 : 0;
                    microSteps += step > 1.0e-9 && step <= 0.0200001 ? 1 : 0;
                    largeSteps += step >= 0.15 ? 1 : 0;
                    if (step < 1.0e-9)
                    {
                        ++state.samePairRun;
                        state.longestSamePairRun = std::max (
                            state.longestSamePairRun, state.samePairRun);
                    }
                    else
                    {
                        state.samePairRun = 0;
                        state.changedDuringGesture = true;
                        if (step > 0.0200001)
                        {
                            if (std::abs (dx) > std::abs (dy) * 1.5)
                                ++state.horizontalSteps;
                            else if (std::abs (dy) > std::abs (dx) * 1.5)
                                ++state.verticalSteps;
                        }
                    }
                    state.x = nextX;
                    state.y = nextY;
                    state.coordinateTick = tick;
                }
                i += 2;
            }
        }

        const double stepCount = static_cast<double> (steps.size());
        const int cadenceTotal = std::accumulate (cadenceCounts.begin(),
                                                  cadenceCounts.end(), 0);
        const double cadenceDenominator = static_cast<double> (cadenceTotal);
        expect (invalidCadence == 0 && cadenceTotal > 5000
                    && cadenceCounts[0] / cadenceDenominator >= 0.68
                    && cadenceCounts[0] / cadenceDenominator <= 0.78
                    && cadenceCounts[1] / cadenceDenominator >= 0.19
                    && cadenceCounts[1] / cadenceDenominator <= 0.30
                    && cadenceCounts[2] / cadenceDenominator >= 0.010
                    && cadenceCounts[2] / cadenceDenominator <= 0.040
                    && cadenceCounts[3] / cadenceDenominator <= 0.012,
                "Human cadence uses only 50/100/150/200 ms skips at Real10 weights");

        expect (steps.size() > 5000
                    && quantile (steps, 0.5) >= 0.022
                    && quantile (steps, 0.5) <= 0.070
                    && quantile (steps, 0.95) >= 0.17
                    && quantile (steps, 0.95) <= 0.33
                    && quantile (speeds, 0.5) >= 0.39
                    && quantile (speeds, 0.5) <= 1.10
                    && quantile (speeds, 0.95) >= 2.35
                    && quantile (speeds, 0.95) <= 4.80,
                "ten-source Human step and speed quantiles stay inside Real10 bootstrap bands");
        expect (exactPauses / stepCount >= 0.13
                    && exactPauses / stepCount <= 0.35
                    && microSteps / stepCount >= 0.06
                    && microSteps / stepCount <= 0.19
                    && largeSteps / stepCount >= 0.068
                    && largeSteps / stepCount <= 0.24,
                "Human pauses, microchanges and large moves match Real10 bands");

        int horizontalSources = 0;
        int verticalSources = 0;
        int longestPauseRun = 0;
        std::vector<double> sourceMedianSpeeds;
        for (auto& state : states)
        {
            if (state.horizontalSteps > state.verticalSteps * 5 / 4)
                ++horizontalSources;
            else if (state.verticalSteps > state.horizontalSteps * 5 / 4)
                ++verticalSources;
            longestPauseRun = std::max (longestPauseRun,
                                        state.longestSamePairRun);
            sourceMedianSpeeds.push_back (quantile (state.speeds, 0.5));
        }
        expect (horizontalSources >= 3 && verticalSources >= 2
                    && *std::min_element (sourceMedianSpeeds.begin(),
                                         sourceMedianSpeeds.end()) < 0.45
                    && *std::max_element (sourceMedianSpeeds.begin(),
                                         sourceMedianSpeeds.end()) > 1.0,
                "stable axis and motion personas create diverse independent users");
        expect (taps > 250
                    && unchangedTaps / static_cast<double> (taps) >= 0.68
                    && unchangedTaps / static_cast<double> (taps) <= 0.90
                    && longestPauseRun >= 8 && longestPauseRun <= 40,
                "Human taps are mostly static and pauses retain a bounded heavy tail");
    }

    // Aggregate pacing is independent from physics: 20 Hz remains logical at
    // low N, while high-N output is bounded and drops overdue frames instead of
    // creating a later catch-up burst.
    {
        const std::array<CrowdSimulatorModel::Profile, 3> profiles {
            CrowdSimulatorModel::Profile::human,
            CrowdSimulatorModel::Profile::dense,
            CrowdSimulatorModel::Profile::stress
        };
        bool ratesBounded = true;
        bool perTickBounded = true;
        CrowdSimulatorModel::EventBuffer events;

        for (const auto profile : profiles)
        {
            CrowdSimulatorModel model (256, 88);
            model.setProfile (profile);
            for (int i = 0; i < 256; ++i)
                model.addHeldParticipant (events);

            std::int64_t scalarMotionEvents = 0;
            for (int tick = 0; tick < 4000; ++tick)
            {
                const auto count = model.advance (true, events);
                int thisTick = 0;
                for (std::size_t i = 0; i < count; ++i)
                    if (events[i].type == CrowdSimulatorModel::EventType::x
                        || events[i].type == CrowdSimulatorModel::EventType::y)
                        ++thisTick;
                scalarMotionEvents += thisTick;
                const int fiftyMsAllowance =
                    CrowdSimulatorModel::scalarMotionEventLimitPerSecond (profile) / 20;
                perTickBounded = perTickBounded && thisTick <= fiftyMsAllowance;
            }

            const double seconds = 4000
                * CrowdSimulatorModel::tickIntervalMs * 0.001;
            const double measuredRate = static_cast<double> (scalarMotionEvents) / seconds;
            const double limit =
                CrowdSimulatorModel::scalarMotionEventLimitPerSecond (profile);
            ratesBounded = ratesBounded && measuredRate <= limit + 0.01
                         && measuredRate >= limit * 0.98;
        }

        expect (ratesBounded,
                "Human/Dense/Stress high-N motion rates are bounded at 1000/2000/4000 events/s");
        expect (perTickBounded,
                "pacing never emits more than one 50 ms allowance in a timer callback");
    }

    // Fixed storage reaches the complete protocol capacity and clears without
    // allocations, stale state or out-of-range snapshots.
    {
        CrowdSimulatorModel model (CrowdSimulatorModel::maxParticipants, 7);
        CrowdSimulatorModel::EventBuffer events;
        for (int i = 0; i < CrowdSimulatorModel::maxParticipants; ++i)
            model.addCrowdParticipant (events);
        const auto rejected = model.addCrowdParticipant (events);
        expect (model.getPopulation() == CrowdSimulatorModel::maxParticipants
                    && rejected.sourceId == -1,
                "fixed model reaches and safely rejects beyond 256 participants");
        model.clearSilently();
        expect (model.getPopulation() == 0 && model.getActivePopulation() == 0
                    && ! model.getParticipantSnapshot (0).occupied
                    && ! model.getParticipantSnapshot (256).occupied,
                "silent clear resets fixed model and invalid snapshots safely");
    }

    std::cout << "Summary: " << (failures == 0 ? "ok" : "FAILED") << '\n';
    return failures == 0 ? 0 : 1;
}
