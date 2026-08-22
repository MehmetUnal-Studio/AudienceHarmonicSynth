#include "../Source/MidiAudienceModel.h"

#include <array>
#include <atomic>
#include <cmath>
#include <iostream>
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
    MidiAudienceModel model(router);
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

    std::cout << "\nSummary: " << (failed == 0 ? "ok" : "failed") << "\n";
    return failed == 0 ? 0 : 1;
}
