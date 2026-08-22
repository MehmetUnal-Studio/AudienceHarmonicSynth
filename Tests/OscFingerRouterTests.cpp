#include "../Source/OscFingerRouter.h"

#include <iostream>

namespace
{
    int failed = 0;

    void expect (bool condition, const char* name)
    {
        std::cout << (condition ? "PASS  " : "FAIL  ") << name << "\n";
        if (! condition)
            ++failed;
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

    return failed == 0 ? 0 : 1;
}
