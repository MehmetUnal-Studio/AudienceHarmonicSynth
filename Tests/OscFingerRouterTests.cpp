#include "../Source/OscFingerRouter.h"

#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>

namespace
{
    int failed = 0;

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
    std::array<OscFingerRouter::Event, 64> events {};

    router.pushX(1, 0, 0.25f);
    router.pushY(1, 0, 0.75f);
    router.pushOn(1, 0, true);
    router.pushOn(1, 0, false);
    const int count = router.drain(events.data(), (int) events.size());

    expect(count == 4, "ordered u/v/on/off events cross the fixed FIFO");
    expect(events[0].type == OscFingerRouter::Event::X
        && events[0].sourceId == 1 && events[0].finger == 0
        && events[0].value == 0.25f,
        "u preserves source, finger and value");
    expect(events[1].type == OscFingerRouter::Event::Y
        && events[1].sourceId == 1 && events[1].finger == 0
        && events[1].value == 0.75f,
        "v preserves source, finger and value");
    expect(events[2].type == OscFingerRouter::Event::On
        && events[3].type == OscFingerRouter::Event::Off,
        "on and off remain distinct lifecycle events");

    router.pushOn(255, 9, true);
    expect(router.drain(events.data(), 1) == 1
        && events[0].sourceId == 255 && events[0].finger == 9,
        "maximum source/finger voice is accepted");

    router.pushOn(256, 0, true);
    router.pushOn(1, 10, true);
    expect(router.drain(events.data(), (int) events.size()) == 0,
        "out-of-range source and finger are ignored");

    router.pushOn(17, 2, true);
    router.requestReset();
    expect(router.takeResetRequest(), "reset request is atomically published to audio thread");
    router.discardPendingEvents();
    expect(router.drain(events.data(), (int) events.size()) == 0,
        "reset discards stale queued finger events");

    for (int source = 0; source < 130; ++source)
        router.pushOn(source, 0, true);
    const int firstBatch = router.drain(events.data(), 64);
    const bool firstOrdered = firstBatch == 64
                           && events.front().sourceId == 0
                           && events[63].sourceId == 63;
    const int secondBatch = router.drain(events.data(), 64);
    const bool secondOrdered = secondBatch == 64
                            && events.front().sourceId == 64
                            && events[63].sourceId == 127;
    const int finalBatch = router.drain(events.data(), 64);
    expect(firstOrdered && secondOrdered && finalBatch == 2
               && events[0].sourceId == 128 && events[1].sourceId == 129,
           "bounded 64-event callbacks preserve FIFO lifecycle order across blocks");

    {
        OscFingerRouter numericRouter;
        numericRouter.pushX(3, 0, std::numeric_limits<float>::quiet_NaN());
        numericRouter.pushY(3, 0, std::numeric_limits<float>::infinity());
        numericRouter.pushX(3, 0, -100.0f);
        numericRouter.pushY(3, 0, 100.0f);
        const int numericCount = numericRouter.drain(events.data(), (int) events.size());
        expect(numericCount == 2
                   && events[0].type == OscFingerRouter::Event::X
                   && nearlyEqual(events[0].value, 0.0f)
                   && events[1].type == OscFingerRouter::Event::Y
                   && nearlyEqual(events[1].value, 1.0f),
               "non-finite motion is rejected and finite motion is clamped before publication");
    }

    // Reproduce the production start contract under a much larger pre-On
    // burst than one phone can generate. Only the latest U/V survive, but they
    // remain immediately before On in the priority lifecycle queue.
    {
        OscFingerRouter floodRouter;
        for (int i = 0; i < 100000; ++i)
        {
            floodRouter.pushX(42, 0, (float) (i % 101) / 100.0f);
            floodRouter.pushY(42, 0, (float) ((100 - i) % 101) / 100.0f);
        }
        floodRouter.pushX(42, 0, 0.875f);
        floodRouter.pushY(42, 0, 0.125f);
        floodRouter.pushOn(42, 0, true);

        const int floodCount = floodRouter.drain(events.data(), (int) events.size());
        expect(floodCount == 3
                   && events[0].type == OscFingerRouter::Event::X
                   && events[1].type == OscFingerRouter::Event::Y
                   && events[2].type == OscFingerRouter::Event::On
                   && nearlyEqual(events[0].value, 0.875f)
                   && nearlyEqual(events[1].value, 0.125f)
                   && floodRouter.getDroppedEventCount() == 0
                   && floodRouter.getCoalescedMotionEventCount() >= 199998
                   && ! floodRouter.takeResetRequest(),
               "latest-value coalescing bounds a 200k motion burst and preserves U/V before On");
    }

    // A release overtakes motion from its preceding touch epoch. Stale markers
    // are consumed internally and can neither starve nor follow the Off.
    {
        OscFingerRouter releaseRouter;
        releaseRouter.pushOn(7, 0, true);
        for (int i = 0; i < 100000; ++i)
        {
            releaseRouter.pushX(7, 0, (float) (i & 1));
            releaseRouter.pushY(7, 0, (float) ((i + 1) & 1));
        }
        releaseRouter.pushOn(7, 0, false);

        const int releaseCount = releaseRouter.drain(events.data(), (int) events.size());
        expect(releaseCount == 2
                   && events[0].type == OscFingerRouter::Event::On
                   && events[1].type == OscFingerRouter::Event::Off
                   && releaseRouter.getDroppedEventCount() == 0
                   && ! releaseRouter.takeResetRequest(),
               "Off has strict priority over a 200k active-motion flood");
    }

    // While a touch stays active, one marker per axis carries the most recent
    // value instead of replaying every network sample on the audio thread.
    {
        OscFingerRouter latestRouter;
        latestRouter.pushOn(11, 0, true);
        expect(latestRouter.drain(events.data(), (int) events.size()) == 1
                   && events[0].type == OscFingerRouter::Event::On,
               "active-motion test establishes its lifecycle epoch");

        for (int i = 0; i < 50000; ++i)
        {
            latestRouter.pushX(11, 0, 0.1f);
            latestRouter.pushY(11, 0, 0.9f);
        }
        latestRouter.pushX(11, 0, 0.33f);
        latestRouter.pushY(11, 0, 0.66f);

        const int latestCount = latestRouter.drain(events.data(), (int) events.size());
        expect(latestCount == 2
                   && events[0].type == OscFingerRouter::Event::X
                   && events[1].type == OscFingerRouter::Event::Y
                   && nearlyEqual(events[0].value, 0.33f)
                   && nearlyEqual(events[1].value, 0.66f),
               "an active touch drains exactly its latest X/Y values");
    }

    // Off -> fresh U/V -> On must not resurrect an older motion marker from
    // the previous touch epoch.
    {
        OscFingerRouter epochRouter;
        epochRouter.pushX(5, 0, 0.1f);
        epochRouter.pushY(5, 0, 0.2f);
        epochRouter.pushOn(5, 0, true);
        expect(epochRouter.drain(events.data(), (int) events.size()) == 3,
               "epoch restart test establishes its first touch");

        epochRouter.pushX(5, 0, 0.9f);
        epochRouter.pushOn(5, 0, false);
        epochRouter.pushY(5, 0, 0.8f);
        epochRouter.pushOn(5, 0, true);
        const int epochCount = epochRouter.drain(events.data(), (int) events.size());
        expect(epochCount == 4
                   && events[0].type == OscFingerRouter::Event::Off
                   && events[1].type == OscFingerRouter::Event::X
                   && events[2].type == OscFingerRouter::Event::Y
                   && events[3].type == OscFingerRouter::Event::On
                   && nearlyEqual(events[1].value, 0.9f)
                   && nearlyEqual(events[2].value, 0.8f),
               "Off and restarted U/V/On remain ordered across motion epochs");
    }

    // Aggregate worst-case documented load: 256 live finger0 sources at 60 Hz
    // produce 30,720 U/V calls in one synthetic second. Coalescing leaves only
    // two markers per source and never raises the safety reset.
    {
        OscFingerRouter crowdRouter;
        for (int source = 0; source < OscFingerRouter::MAX_SOURCES; ++source)
            crowdRouter.pushOn(source, 0, true);

        int lifecycleCount = 0;
        for (;;)
        {
            const int drained = crowdRouter.drain(events.data(), (int) events.size());
            if (drained == 0)
                break;
            lifecycleCount += drained;
        }

        for (int tick = 0; tick < 60; ++tick)
            for (int source = 0; source < OscFingerRouter::MAX_SOURCES; ++source)
            {
                crowdRouter.pushX(source, 0, (float) tick / 59.0f);
                crowdRouter.pushY(source, 0, (float) (59 - tick) / 59.0f);
            }

        int motionCount = 0;
        bool allFiniteAndBounded = true;
        for (;;)
        {
            const int drained = crowdRouter.drain(events.data(), (int) events.size());
            if (drained == 0)
                break;
            motionCount += drained;
            for (int i = 0; i < drained; ++i)
                allFiniteAndBounded = allFiniteAndBounded
                    && std::isfinite(events[(size_t) i].value)
                    && events[(size_t) i].value >= 0.0f
                    && events[(size_t) i].value <= 1.0f;
        }

        expect(lifecycleCount == OscFingerRouter::MAX_SOURCES
                   && motionCount == OscFingerRouter::MAX_SOURCES * 2
                   && allFiniteAndBounded
                   && crowdRouter.getDroppedEventCount() == 0
                   && ! crowdRouter.takeResetRequest(),
               "256-source 60 Hz crowd load is bounded to 512 latest-value motion events");
    }

    // Lifecycle overflow still fails safe: unlike motion coalescing, an On/Off
    // transition is never silently discarded without requesting canonical
    // rehydration on the audio thread.
    {
        OscFingerRouter overflowRouter;
        for (int i = 0; i < OscFingerRouter::EVENT_QUEUE_SIZE + 32; ++i)
            overflowRouter.pushOn(1, 0, (i & 1) != 0);

        expect(overflowRouter.getDroppedEventCount() > 0
                   && overflowRouter.takeResetRequest(),
               "lifecycle queue overflow requests a safety reset instead of risking a stuck note");
        overflowRouter.discardPendingEvents();
    }

    // Producer serialization and audio draining are exercised concurrently;
    // this is also used by the sanitizer/TSan validation run.
    {
        OscFingerRouter concurrentRouter;
        std::atomic<bool> producerDone { false };
        std::thread producer([&]
        {
            concurrentRouter.pushX(88, 0, 0.2f);
            concurrentRouter.pushY(88, 0, 0.8f);
            concurrentRouter.pushOn(88, 0, true);
            for (int i = 0; i < 100000; ++i)
            {
                concurrentRouter.pushX(88, 0, (float) (i % 100) / 99.0f);
                concurrentRouter.pushY(88, 0, (float) ((99 - i) % 100) / 99.0f);
            }
            concurrentRouter.pushOn(88, 0, false);
            producerDone.store(true, std::memory_order_release);
        });

        bool sawOn = false;
        bool sawOff = false;
        bool active = false;
        int emptyPasses = 0;
        while (! producerDone.load(std::memory_order_acquire) || emptyPasses < 4)
        {
            const int drained = concurrentRouter.drain(events.data(), (int) events.size());
            emptyPasses = drained == 0 ? emptyPasses + 1 : 0;
            for (int i = 0; i < drained; ++i)
            {
                if (events[(size_t) i].type == OscFingerRouter::Event::On)
                    sawOn = active = true;
                else if (events[(size_t) i].type == OscFingerRouter::Event::Off)
                    sawOff = true, active = false;
            }
            if (drained == 0)
                std::this_thread::yield();
        }
        producer.join();

        // Catch any event published between the final empty pass and join.
        for (;;)
        {
            const int drained = concurrentRouter.drain(events.data(), (int) events.size());
            if (drained == 0)
                break;
            for (int i = 0; i < drained; ++i)
            {
                if (events[(size_t) i].type == OscFingerRouter::Event::On)
                    sawOn = active = true;
                else if (events[(size_t) i].type == OscFingerRouter::Event::Off)
                    sawOff = true, active = false;
            }
        }

        expect(sawOn && sawOff && ! active
                   && concurrentRouter.getDroppedEventCount() == 0,
               "concurrent 200k motion publication preserves the final Off without overflow");
    }

    return failed == 0 ? 0 : 1;
}
