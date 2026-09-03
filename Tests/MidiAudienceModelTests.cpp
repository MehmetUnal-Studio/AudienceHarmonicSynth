#include "../Source/MidiAudienceModel.h"
#include "../Source/SourceQualityController.h"

#include <array>
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>

namespace
{
    int failed = 0;
    std::atomic<std::uint32_t> fakeNowMs { 100 };

    std::uint32_t fakeMonotonicClock() noexcept
    {
        return fakeNowMs.load(std::memory_order_relaxed);
    }

    void expect (bool condition, const char* name)
    {
        std::cout << (condition ? "PASS  " : "FAIL  ") << name << "\n";
        if (! condition)
            ++failed;
    }

    bool nearlyEqual (float a, float b) noexcept
    {
        return std::abs(a - b) < 1.0e-6f;
    }
}

int main()
{
    OscFingerRouter router;
    MidiAudienceModel model(router, &fakeMonotonicClock);
    std::array<OscFingerRouter::Event, 32> events {};

    model.setFingerX(0, 17, 2, 0.25f);
    model.setFingerY(25, 17, 2, 0.75f);
    model.setFingerOn(0, 17, 2, true);
    model.setFingerOn(25, 17, 7, true);

    auto source17 = model.getSourceSnapshot(17);
    expect(source17.active
               && source17.activeFingerMask == ((1u << 2) | (1u << 7))
               && source17.activeFingerCount == 2
               && model.getActiveSourceCount() == 1
               && model.getActiveFingerCount() == 2,
           "two fingers aggregate into one active source without losing their mask");
    expect(nearlyEqual(source17.x, 0.25f) && nearlyEqual(source17.y, 0.75f)
               && model.getLastActiveSourceId() == 17,
           "source snapshot exposes its latest normalized position and activity");
    const auto finger2 = model.getFingerSnapshot(17, 2);
    const auto finger7 = model.getFingerSnapshot(17, 7);
    expect(finger2.active && finger7.active
               && nearlyEqual(finger2.x, 0.25f) && nearlyEqual(finger2.y, 0.75f)
               && nearlyEqual(finger7.x, 0.0f) && nearlyEqual(finger7.y, 0.0f),
           "canonical ledger preserves each finger position independently for reset recovery");

    const int initialCount = router.drain(events.data(), (int) events.size());
    expect(initialCount == 4
               && events[0].sourceId == 17 && events[0].finger == 2
               && events[1].sourceId == 17 && events[1].finger == 2
               && events[2].sourceId == 17 && events[2].finger == 2
               && events[3].sourceId == 17 && events[3].finger == 7,
           "row is ignored while source and finger identity reach the MIDI router");

    const int source17ChannelAfterTwoFingers = source17.midiChannel;
    expect(source17ChannelAfterTwoFingers == 1
               && source17ChannelAfterTwoFingers
                    == MidiAudienceModel::midiChannelForSourceId(17),
           "every finger of one source shares one MIDI channel");
    expect(MidiAudienceModel::midiChannelForSourceId(0) == 16
               && MidiAudienceModel::midiChannelForSourceId(1) == 1
               && MidiAudienceModel::midiChannelForSourceId(16) == 16
               && MidiAudienceModel::midiChannelForSourceId(17) == 1
               && MidiAudienceModel::midiChannelForSourceId(255) == 15,
           "base-1 source IDs wrap deterministically across MIDI channels 1-16");
    expect(MidiAudienceModel::midiChannelForSourceId(-1) == 0
               && MidiAudienceModel::midiChannelForSourceId(256) == 0
               && model.getSourceSnapshot(-1).midiChannel == 0,
           "invalid source IDs do not masquerade as routable MIDI channels");

    {
        bool balanced = true;
        for (const int capacity : { 64, 128, 256 })
        {
            std::array<int, 16> channelCounts {};
            for (int sourceId = 0; sourceId < capacity; ++sourceId)
            {
                const int channel = MidiAudienceModel::midiChannelForSourceId(sourceId);
                balanced = balanced && channel >= 1 && channel <= 16;
                if (channel >= 1 && channel <= 16)
                    ++channelCounts[(size_t) (channel - 1)];
            }

            for (const int count : channelCounts)
                balanced = balanced && count == capacity / 16;
        }

        expect(balanced
                   && MidiAudienceModel::midiChannelForSourceId(0) == 16,
               "64/128/256 dense domains balance at 4/8/16 sources per channel with source 0 on channel 16");
    }

    // Capacity rejects only syntactically valid events above the dense domain.
    // Malformed protocol identities and non-finite values remain ordinary
    // validation failures and do not pollute overload telemetry.
    {
        OscFingerRouter capacityRouter;
        MidiAudienceModel capacityModel(capacityRouter, &fakeMonotonicClock);
        expect(capacityModel.getSourceCapacity() == MidiAudienceModel::MAX_SOURCES
                   && capacityModel.setSourceCapacity(64) == 0,
               "existing model callers retain 256 until a runtime capacity is applied");

        capacityModel.setFingerX(0, 63, 0, 0.25f);
        capacityModel.setFingerY(0, 63, 0, 0.75f);
        capacityModel.setFingerOn(0, 63, 0, true);
        capacityModel.setFingerX(0, 64, 0, 0.25f);
        capacityModel.setFingerY(0, 64, 0, 0.75f);
        capacityModel.setFingerOn(0, 64, 0, true);
        capacityModel.setFingerOn(0, 64, 0, false);
        capacityModel.setLiveFingerX(0, 64, 0, 0.5f);
        capacityModel.setFingerOn(0, 256, 0, true);
        capacityModel.setFingerX(0, 64, 0,
            std::numeric_limits<float>::quiet_NaN());

        std::array<OscFingerRouter::Event, 8> capacityEvents {};
        const int capacityEventCount = capacityRouter.drain(
            capacityEvents.data(), (int) capacityEvents.size());
        expect(capacityEventCount == 3
                   && capacityModel.getFingerSnapshot(63, 0).active
                   && ! capacityModel.getFingerSnapshot(64, 0).active
                   && capacityModel.getActiveSourceCount() == 1
                   && capacityModel.getRecentLiveSourceCount() == 0
                   && capacityModel.getCapacityDroppedEventCount() == 5,
               "capacity admits 0..63 and counts bounded valid-event rejection separately");
    }

    // Shrink cleanup is source/finger ordered, preserves lower identities and
    // clears upper live provenance as well as canonical positions.
    {
        OscFingerRouter shrinkRouter;
        MidiAudienceModel shrinkModel(shrinkRouter, &fakeMonotonicClock);
        fakeNowMs.store(100, std::memory_order_relaxed);
        shrinkModel.setFingerOn(0, 0, 0, true);
        shrinkModel.setFingerOn(0, 63, 0, true);
        shrinkModel.setFingerX(0, 64, 2, 0.7f);
        shrinkModel.setFingerOn(0, 64, 0, true);
        shrinkModel.setFingerOn(0, 64, 2, true);
        shrinkModel.setLiveFingerOn(0, 127, 1, true);
        shrinkModel.setFingerOn(0, 255, 9, true);
        shrinkRouter.discardPendingEvents();

        const int released = shrinkModel.setSourceCapacity(64);
        std::array<OscFingerRouter::Event, 8> shrinkEvents {};
        const int shrinkEventCount = shrinkRouter.drain(
            shrinkEvents.data(), (int) shrinkEvents.size());
        const bool ordered = shrinkEventCount == 4
            && shrinkEvents[0].type == OscFingerRouter::Event::Off
            && shrinkEvents[0].sourceId == 64 && shrinkEvents[0].finger == 0
            && shrinkEvents[1].sourceId == 64 && shrinkEvents[1].finger == 2
            && shrinkEvents[2].sourceId == 127 && shrinkEvents[2].finger == 1
            && shrinkEvents[3].sourceId == 255 && shrinkEvents[3].finger == 9;
        expect(released == 4 && ordered
                   && shrinkModel.getSourceCapacity() == 64
                   && shrinkModel.getActiveSourceCount() == 2
                   && shrinkModel.getActiveFingerCount() == 2
                   && shrinkModel.getFingerSnapshot(0, 0).active
                   && shrinkModel.getFingerSnapshot(63, 0).active
                   && ! shrinkModel.getFingerSnapshot(64, 0).active
                   && nearlyEqual(shrinkModel.getFingerSnapshot(64, 2).x, 0.0f)
                   && shrinkModel.getRecentLiveSourceCount() == 0,
               "runtime shrink deterministically releases and erases only retired sources");

        const auto dropsBeforeExpansion =
            shrinkModel.getCapacityDroppedEventCount();
        shrinkModel.setFingerOn(0, 64, 0, true);
        const bool rejectedWhileSmall = ! shrinkModel.getFingerSnapshot(64, 0).active
            && shrinkModel.getCapacityDroppedEventCount()
                   == dropsBeforeExpansion + 1;
        const int releasedOnExpansion = shrinkModel.setSourceCapacity(128);
        shrinkModel.setFingerOn(0, 64, 0, true);
        expect(rejectedWhileSmall && releasedOnExpansion == 0
                   && shrinkModel.getFingerSnapshot(64, 0).active
                   && shrinkModel.getFingerSnapshot(0, 0).active
                   && MidiAudienceModel::midiChannelForSourceId(0) == 16,
               "capacity expansion preserves lower sources and admits new IDs without remapping");
    }

    model.setFingerOn(8, 17, 2, false);
    source17 = model.getSourceSnapshot(17);
    expect(source17.active && source17.activeFingerCount == 1
               && model.getActiveSourceCount() == 1
               && model.getActiveFingerCount() == 1,
           "releasing one finger keeps its source active until the final finger ends");
    model.setFingerOn(-99, 17, 7, false);
    source17 = model.getSourceSnapshot(17);
    expect(! source17.active && model.getActiveSourceCount() == 0
               && model.getActiveFingerCount() == 0,
           "the final finger release clears the aggregate source independent of row");
    router.discardPendingEvents();

    model.setFingerOn(0, 0, 0, true);
    model.setFingerOn(0, 255, 9, true);
    model.setFingerOn(0, -1, 0, true);
    model.setFingerOn(0, 256, 0, true);
    model.setFingerOn(0, 1, -1, true);
    model.setFingerOn(0, 1, 10, true);
    const int boundaryCount = router.drain(events.data(), (int) events.size());
    expect(boundaryCount == 2
               && events[0].sourceId == 0 && events[0].finger == 0
               && events[1].sourceId == 255 && events[1].finger == 9,
           "source 0/255 and finger 0/9 are accepted while out-of-range identities are ignored");
    expect(model.getActiveSourceCount() == 2 && model.getActiveFingerCount() == 2
               && model.getSourceSnapshot(0).midiChannel == 16
               && model.getSourceSnapshot(255).midiChannel == 15,
           "boundary sources are represented and counted correctly");

    model.setX(12345, 1, -0.5f);
    model.setY(-12345, 1, 1.5f);
    model.setOn(12345, 1, true);
    auto source1 = model.getSourceSnapshot(1);
    expect(source1.active && source1.activeFingerCount == 1
               && nearlyEqual(source1.x, 0.0f) && nearlyEqual(source1.y, 1.0f),
           "legacy seat callbacks target finger0, ignore row and clamp normalized values");

    model.clear();
    source1 = model.getSourceSnapshot(1);
    expect(model.getActiveSourceCount() == 0 && model.getActiveFingerCount() == 0
               && model.getLastActiveSourceId() == -1
               && ! source1.active && source1.activeFingerMask == 0
               && nearlyEqual(source1.x, 0.0f) && nearlyEqual(source1.y, 0.0f),
           "clear resets all source snapshots and aggregate telemetry");
    // Reproduce the control/audio race: clear publishes a reset, then a fresh
    // touch arrives before the audio consumer handles it. Even if queued events
    // are discarded at the reset boundary, the canonical finger ledger must
    // retain the new touch so the processor can rehydrate it.
    model.setFingerX(0, 9, 4, 0.33f);
    model.setFingerY(0, 9, 4, 0.66f);
    model.setFingerOn(0, 9, 4, true);
    expect(router.takeResetRequest(),
           "clear requests a downstream MIDI safety reset");
    router.discardPendingEvents();
    const auto recoveredFinger = model.getFingerSnapshot(9, 4);
    expect(recoveredFinger.active
               && nearlyEqual(recoveredFinger.x, 0.33f)
               && nearlyEqual(recoveredFinger.y, 0.66f),
           "post-reset touch survives queue discard in the canonical finger ledger");

    {
        OscFingerRouter concurrentRouter;
        MidiAudienceModel concurrentModel(concurrentRouter);
        std::atomic<bool> start { false };

        auto publish = [&] (bool phase)
        {
            while (! start.load(std::memory_order_acquire))
                std::this_thread::yield();

            for (int i = 0; i < 200; ++i)
                concurrentModel.setFingerOn(0, 42, 3, ((i & 1) != 0) == phase);
        };

        std::thread first(publish, false);
        std::thread second(publish, true);
        start.store(true, std::memory_order_release);
        first.join();
        second.join();

        std::array<OscFingerRouter::Event, 512> concurrentEvents {};
        const int count = concurrentRouter.drain(concurrentEvents.data(),
                                                 (int) concurrentEvents.size());
        bool fifoActive = false;
        for (int i = 0; i < count; ++i)
            if (concurrentEvents[(size_t) i].type == OscFingerRouter::Event::On
                || concurrentEvents[(size_t) i].type == OscFingerRouter::Event::Off)
                fifoActive = concurrentEvents[(size_t) i].type == OscFingerRouter::Event::On;

        expect(count == 400
                   && fifoActive == concurrentModel.getFingerSnapshot(42, 3).active,
               "concurrent OSC/simulator publishers keep FIFO order equal to canonical state");
    }

    // A stationary simulator uses only setFinger* and never the live-network
    // hook, so it must remain held indefinitely.
    {
        OscFingerRouter watchdogRouter;
        MidiAudienceModel watchdogModel(watchdogRouter, &fakeMonotonicClock);
        fakeNowMs.store(100, std::memory_order_relaxed);
        watchdogModel.setFingerOn(0, 5, 0, true);
        watchdogRouter.discardPendingEvents();
        fakeNowMs.store(100000, std::memory_order_relaxed);
        expect(watchdogModel.expireStaleLiveTouches() == 0
                   && watchdogModel.getFingerSnapshot(5, 0).active,
               "stationary simulator touch is not enrolled in the live OSC watchdog");
    }

    // Production readiness observes only accepted live OSC APIs. Simulator
    // state may exercise the musical engine but cannot certify a venue signal
    // path, and a source outside the selected dense capacity becomes a hard
    // capacity incident rather than false coverage.
    {
        OscFingerRouter qualityRouter;
        SourceQualityController qualityController;
        MidiAudienceModel qualityModel(qualityRouter, &fakeMonotonicClock,
                                       &qualityController);
        SourceQualityController::Config config;
        config.expectedSources = 1;
        config.readinessHoldMs = 0;
        fakeNowMs.store(100, std::memory_order_relaxed);
        qualityController.arm(100u, {});

        qualityModel.setFingerX(0, 0, 0, 0.2f);
        qualityModel.setFingerY(0, 0, 0, 0.8f);
        qualityModel.setFingerOn(0, 0, 0, true);
        qualityModel.setFingerOn(0, 0, 0, false);
        auto output = qualityController.update(config, {}, 200u);
        const bool simulatorExcluded = output.observedSources == 0
                                    && output.qualifiedSources == 0
                                    && ! output.admissionOpen;

        fakeNowMs.store(300, std::memory_order_relaxed);
        qualityModel.setLiveFingerX(0, 0, 0, 0.2f);
        qualityModel.setLiveFingerY(0, 0, 0, 0.8f);
        qualityModel.setLiveFingerOn(0, 0, 0, true);
        output = qualityController.update(config, {}, 300u);
        const bool liveQualified = output.observedSources == 1
                                && output.qualifiedSources == 1
                                && output.activeSources == 1
                                && output.state
                                    == SourceQualityController::State::READY
                                && output.admissionOpen;

        qualityModel.setLiveFingerOn(0, 0, 0, false);

        qualityModel.setSourceCapacity(64);
        SourceQualityController::ExternalCounters baseline;
        baseline.capacityDroppedEvents =
            qualityModel.getCapacityDroppedEventCount();
        qualityController.arm(400u, baseline);
        fakeNowMs.store(500, std::memory_order_relaxed);
        qualityModel.setLiveFingerX(0, 64, 0, 0.5f);
        SourceQualityController::ExternalCounters afterDrop;
        afterDrop.capacityDroppedEvents =
            qualityModel.getCapacityDroppedEventCount();
        config.expectedSources = 64;
        output = qualityController.update(config, afterDrop, 500u);
        expect(simulatorExcluded && liveQualified
                   && output.capacityDropCount == 1u
                   && SourceQualityController::hasReason(
                       output.reasonBits,
                       SourceQualityController::ReasonCapacityDrop)
                   && ! output.admissionOpen,
               "Ready Gate excludes simulator traffic and rejects live source 64 at capacity 64");
    }

    // Retiring a capacity range must also erase the controller's live masks.
    // Otherwise shrink -> expand can resurrect a stale source or classify its
    // next legitimate On as a duplicate.
    {
        OscFingerRouter rangeRouter;
        SourceQualityController rangeController;
        MidiAudienceModel rangeModel(rangeRouter, &fakeMonotonicClock,
                                     &rangeController);
        rangeModel.setSourceCapacity(128);
        SourceQualityController::Config config;
        config.expectedSources = 128;
        config.readinessHoldMs = 0;
        config.requireAllSourcesActiveForReady = false;

        fakeNowMs.store(100, std::memory_order_relaxed);
        rangeController.arm(100u, {});
        rangeModel.setLiveFingerX(0, 100, 0, 0.25f);
        rangeModel.setLiveFingerY(0, 100, 0, 0.75f);
        rangeModel.setLiveFingerOn(0, 100, 0, true);
        auto output = rangeController.update(config, {}, 100u);
        const bool upperWasActive = output.activeSources == 1
                                 && output.duplicateOnCount == 0u;

        rangeRouter.discardPendingEvents();
        const int released = rangeModel.setSourceCapacity(64);
        rangeRouter.discardPendingEvents();
        const int releasedOnExpansion = rangeModel.setSourceCapacity(128);
        output = rangeController.update(config, {}, 200u);
        const bool didNotResurrect = released == 1
                                  && releasedOnExpansion == 0
                                  && output.activeSources == 0
                                  && output.duplicateOnCount == 0u;

        fakeNowMs.store(300, std::memory_order_relaxed);
        rangeModel.setLiveFingerX(0, 100, 0, 0.4f);
        rangeModel.setLiveFingerY(0, 100, 0, 0.6f);
        rangeModel.setLiveFingerOn(0, 100, 0, true);
        output = rangeController.update(config, {}, 300u);
        expect(upperWasActive && didNotResurrect
                   && output.activeSources == 1
                   && output.duplicateOnCount == 0u,
               "capacity shrink/expand clears retired Ready Gate live ownership");
    }

    // Governor participation uses a rolling unique-source window rather than
    // only the instantaneous held count. Any valid live packet refreshes that
    // source, releases remain visible for eight seconds, and simulator-only
    // sources do not masquerade as network audience history.
    {
        OscFingerRouter recentRouter;
        MidiAudienceModel recentModel(recentRouter, &fakeMonotonicClock);
        fakeNowMs.store(100, std::memory_order_relaxed);
        recentModel.setLiveFingerX(0, 3, 0, 0.2f);
        recentModel.setLiveFingerOn(0, 7, 0, true);
        recentModel.setLiveFingerOn(0, 7, 0, false);
        recentModel.setFingerOn(0, 9, 0, true);

        const bool initial = recentModel.getRecentLiveSourceCount() == 2
                          && recentModel.getActiveSourceCount() == 1;
        fakeNowMs.store(8099, std::memory_order_relaxed);
        const bool beforeBoundary = recentModel.getRecentLiveSourceCount() == 2;
        fakeNowMs.store(8100, std::memory_order_relaxed);
        const bool atBoundary = recentModel.getRecentLiveSourceCount() == 0;
        recentModel.clear();
        expect(initial && beforeBoundary && atBoundary
                   && recentModel.getRecentLiveSourceCount() == 0,
               "recent live-source window is unique, exact and simulator-independent");
    }

    // Live OSC starts tracking explicitly. Refresh packets preserve the touch,
    // the exact 3 s boundary expires it, and the synthetic Cancel is ordered.
    {
        OscFingerRouter watchdogRouter;
        MidiAudienceModel watchdogModel(watchdogRouter, &fakeMonotonicClock);
        fakeNowMs.store(100, std::memory_order_relaxed);
        watchdogModel.setLiveFingerOn(0, 7, 0, true);
        watchdogRouter.discardPendingEvents();

        for (const std::uint32_t tick : { 1100u, 2100u, 3100u })
        {
            fakeNowMs.store(tick, std::memory_order_relaxed);
            watchdogModel.setLiveFingerX(0, 7, 0, 0.5f);
        }
        watchdogRouter.discardPendingEvents();

        fakeNowMs.store(6099, std::memory_order_relaxed);
        const bool beforeBoundary = watchdogModel.expireStaleLiveTouches() == 0
                                 && watchdogModel.getFingerSnapshot(7, 0).active;
        fakeNowMs.store(6100, std::memory_order_relaxed);
        const bool atBoundary = watchdogModel.expireStaleLiveTouches() == 1
                             && ! watchdogModel.getFingerSnapshot(7, 0).active;
        std::array<OscFingerRouter::Event, 4> expiryEvents {};
        const int expiryCount = watchdogRouter.drain(expiryEvents.data(),
                                                      (int) expiryEvents.size());
        expect(beforeBoundary && atBoundary && expiryCount == 1
                   && expiryEvents[0].type == OscFingerRouter::Event::Cancel
                   && expiryEvents[0].sourceId == 7 && expiryEvents[0].finger == 0,
               "1 Hz live heartbeat expires once at the exact 3 s boundary with ordered Cancel");

        // Refresh after expiry updates position only. It cannot reactivate or
        // re-enrol the voice; a new explicit live On is required.
        fakeNowMs.store(6200, std::memory_order_relaxed);
        watchdogModel.setLiveFingerX(0, 7, 0, 0.9f);
        fakeNowMs.store(20000, std::memory_order_relaxed);
        const bool strayStayedOff = watchdogModel.expireStaleLiveTouches() == 0
                                 && ! watchdogModel.getFingerSnapshot(7, 0).active;
        watchdogModel.setLiveFingerOn(0, 7, 0, true);
        expect(strayStayedOff && watchdogModel.getFingerSnapshot(7, 0).active,
               "stray U/V never re-arms an expired touch; explicit live On does");
    }

    // uint32 millisecond rollover remains ordered by modular subtraction.
    {
        OscFingerRouter wrapRouter;
        MidiAudienceModel wrapModel(wrapRouter, &fakeMonotonicClock);
        constexpr std::uint32_t start = 0xffffffffu - 1000u;
        fakeNowMs.store(start, std::memory_order_relaxed);
        wrapModel.setLiveFingerOn(0, 8, 0, true);
        wrapRouter.discardPendingEvents();
        fakeNowMs.store(start + 2999u, std::memory_order_relaxed);
        const bool heldAcrossWrap = wrapModel.expireStaleLiveTouches() == 0;
        fakeNowMs.store(start + 3000u, std::memory_order_relaxed);
        expect(heldAcrossWrap && wrapModel.expireStaleLiveTouches() == 1
                   && ! wrapModel.getFingerSnapshot(8, 0).active,
               "watchdog timeout remains exact across uint32 monotonic-clock wrap");
    }

    // Live tracking and the matching lifecycle update are one model operation.
    // Every possible interleaving with expiry ends with the fresh On as
    // canonical state and as the last FIFO lifecycle event.
    {
        OscFingerRouter raceRouter;
        MidiAudienceModel raceModel(raceRouter, &fakeMonotonicClock);
        fakeNowMs.store(100, std::memory_order_relaxed);
        raceModel.setLiveFingerOn(0, 9, 0, true);
        raceRouter.discardPendingEvents();
        fakeNowMs.store(3100, std::memory_order_relaxed);

        std::atomic<bool> startRace { false };
        std::thread expiry([&]
        {
            while (! startRace.load(std::memory_order_acquire))
                std::this_thread::yield();
            raceModel.expireStaleLiveTouches();
        });
        std::thread freshOn([&]
        {
            while (! startRace.load(std::memory_order_acquire))
                std::this_thread::yield();
            raceModel.setLiveFingerOn(0, 9, 0, true);
        });
        startRace.store(true, std::memory_order_release);
        expiry.join();
        freshOn.join();

        std::array<OscFingerRouter::Event, 8> raceEvents {};
        const int raceCount = raceRouter.drain(raceEvents.data(),
                                               (int) raceEvents.size());
        bool fifoActive = false;
        for (int i = 0; i < raceCount; ++i)
            if (raceEvents[(size_t) i].type == OscFingerRouter::Event::On
                || raceEvents[(size_t) i].type == OscFingerRouter::Event::Off
                || raceEvents[(size_t) i].type == OscFingerRouter::Event::Cancel)
                fifoActive = raceEvents[(size_t) i].type == OscFingerRouter::Event::On;
        expect(raceCount >= 1 && fifoActive
                   && raceModel.getFingerSnapshot(9, 0).active,
               "fresh live On and simultaneous expiry retain canonical/FIFO agreement");
    }

    // A simulator release can race the same source identity as a fresh phone
    // On. The live API must publish heartbeat ownership and the On under one
    // lock: whichever complete operation is last owns both canonical state and
    // FIFO order, and only that live-active result receives a later expiry.
    {
        OscFingerRouter collisionRaceRouter;
        MidiAudienceModel collisionRaceModel(collisionRaceRouter,
                                              &fakeMonotonicClock);
        fakeNowMs.store(3100, std::memory_order_relaxed);
        std::atomic<bool> startRace { false };
        std::thread simulatorOff([&]
        {
            while (! startRace.load(std::memory_order_acquire))
                std::this_thread::yield();
            collisionRaceModel.setFingerOn(0, 11, 0, false);
        });
        std::thread liveOn([&]
        {
            while (! startRace.load(std::memory_order_acquire))
                std::this_thread::yield();
            collisionRaceModel.setLiveFingerOn(0, 11, 0, true);
        });
        startRace.store(true, std::memory_order_release);
        simulatorOff.join();
        liveOn.join();

        std::array<OscFingerRouter::Event, 8> eventsAfterRace {};
        const int eventCount = collisionRaceRouter.drain(
            eventsAfterRace.data(), (int) eventsAfterRace.size());
        bool fifoActive = false;
        for (int index = 0; index < eventCount; ++index)
            if (eventsAfterRace[(size_t) index].type == OscFingerRouter::Event::On
                || eventsAfterRace[(size_t) index].type == OscFingerRouter::Event::Off)
                fifoActive = eventsAfterRace[(size_t) index].type
                          == OscFingerRouter::Event::On;

        const bool canonicalMatches = eventCount == 2
                                   && collisionRaceModel.getFingerSnapshot(11, 0).active
                                      == fifoActive;
        fakeNowMs.store(6100, std::memory_order_relaxed);
        const int expired = collisionRaceModel.expireStaleLiveTouches();
        expect(canonicalMatches && expired == (fifoActive ? 1 : 0)
                   && ! collisionRaceModel.getFingerSnapshot(11, 0).active,
               "simulator Off versus live On is atomic across heartbeat, canonical state and FIFO");
    }

    // Stop and clear both erase watchdog provenance. A later stationary
    // simulator source reusing that ID cannot inherit a stale live deadline.
    {
        OscFingerRouter collisionRouter;
        MidiAudienceModel collisionModel(collisionRouter, &fakeMonotonicClock);
        fakeNowMs.store(100, std::memory_order_relaxed);
        collisionModel.setLiveFingerOn(0, 10, 0, true);
        collisionModel.setLiveFingerOn(0, 10, 0, false);
        collisionModel.setFingerOn(0, 10, 0, true); // stationary simulator reuse
        collisionRouter.discardPendingEvents();
        fakeNowMs.store(100000, std::memory_order_relaxed);
        const bool explicitStopSafe = collisionModel.expireStaleLiveTouches() == 0
                                   && collisionModel.getFingerSnapshot(10, 0).active;

        collisionModel.clear();
        collisionRouter.takeResetRequest();
        collisionRouter.discardPendingEvents();
        collisionModel.setFingerOn(0, 10, 0, true);
        fakeNowMs.store(200000, std::memory_order_relaxed);
        expect(explicitStopSafe && collisionModel.expireStaleLiveTouches() == 0
                   && collisionModel.getFingerSnapshot(10, 0).active,
               "live Stop and Panic/clear erase tracking before stationary simulator ID reuse");
    }

    std::cout << "\nSummary: " << (failed == 0 ? "ok" : "failed") << "\n";
    return failed == 0 ? 0 : 1;
}
