#include "../Source/OscBridge.h"
#include "../Source/SeatEventSink.h"

#include <atomic>
#include <iostream>
#include <utility>

namespace
{
    struct CountingSink : SeatEventSink
    {
        void setX (int row, int col, float xNorm) override
        {
            lastRow.store(row);
            lastCol.store(col);
            lastX.store(xNorm);
            xCount.fetch_add(1);
        }

        void setY (int row, int col, float yNorm) override
        {
            lastRow.store(row);
            lastCol.store(col);
            lastY.store(yNorm);
            yCount.fetch_add(1);
        }

        void setOn (int row, int col, bool on) override
        {
            lastRow.store(row);
            lastCol.store(col);
            active.store(on);
            onCount.fetch_add(1);
        }

        std::atomic<int> lastRow { -1 };
        std::atomic<int> lastCol { -1 };
        std::atomic<int> xCount { 0 };
        std::atomic<int> yCount { 0 };
        std::atomic<int> onCount { 0 };
        std::atomic<float> lastX { 0.0f };
        std::atomic<float> lastY { 0.0f };
        std::atomic<bool> active { false };
    };

    void expect (bool ok, const char* name, int& failed)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << name << "\n";
        if (! ok)
            ++failed;
    }
}

int main()
{
    int failed = 0;
    CountingSink sinkA, sinkB;
    OscBridge bridgeA(sinkA), bridgeB(sinkB);

    int port = 62060;
    bool started = false;
    for (; port < 62120; ++port)
    {
        if (bridgeA.start(port))
        {
            started = bridgeB.start(port);
            break;
        }
    }

    expect(started && bridgeA.isRunning() && bridgeB.isRunning(),
           "two OscBridge instances can share one UDP port inside the host process", failed);

    juce::OSCSender sender;
    const bool connected = sender.connect("127.0.0.1", port);
    expect(connected, "OSC sender connects to shared test port", failed);

    if (connected)
    {
        sender.send("/cs/A/0/finger1/on", 1);
        sender.send("/cs/A/0/finger1/line", 64.0f);
        sender.send("/cs/A/0/finger1/v", 0.5f);

        for (int i = 0; i < 100; ++i)
        {
            if (sinkA.onCount.load() > 0 && sinkB.onCount.load() > 0
                && sinkA.xCount.load() > 0 && sinkB.xCount.load() > 0
                && sinkA.yCount.load() > 0 && sinkB.yCount.load() > 0)
                break;

            juce::Thread::sleep(10);
        }

        expect(sinkA.active.load() && sinkB.active.load()
            && sinkA.lastRow.load() == 0 && sinkB.lastRow.load() == 0
            && sinkA.lastCol.load() == 0 && sinkB.lastCol.load() == 0,
            "shared UDP packet is fanned out to both plugin sinks", failed);
    }

    // ---------------------------------------------------------------------
    // B27: OSC wire-format edge cases.
    //
    // These lock the *actual* routing behaviour of OscWireFormat::parseAddress
    // + OscBridge::oscMessageReceived (read off the source, not assumed):
    //   - The address grammar is "/cs/<row>/<col>/finger<n>/<param>".
    //   - <row> is a single A..Z letter (case-insensitive); any trailing chars
    //     in that segment are ignored. Outside A..Z -> dropped.
    //   - <col> is decimal digits, must be in [0, SeatEventSink::MAX_COLS) and
    //     be followed by '/'. col >= MAX_COLS -> dropped.
    //   - finger<n> is OPAQUE: its bytes are skipped entirely, so finger1 and
    //     finger2 collapse to the same seat.
    //   - A bad prefix or a missing trailing param segment -> dropped.
    //   - For a zero-argument message, ONLY ".../off" fires (setOn(row,col,false));
    //     a zero-arg on/v/line is a no-op (see the msg.size()==0 branch).
    //
    // Observation model (matches the harness above): the sink only records
    // setX (from "line"), setY (from "v") and setOn (from "on"/"off"). UDP from
    // localhost is delivered in order, so to prove a message was *dropped* we
    // send it, then send a known-good "barrier" message to a distinct seat and
    // wait for that barrier to land. If the barrier's effect is the ONLY change
    // observed, the message under test produced no sink call.
    if (connected)
    {
        // The barrier targets a valid-but-distinct seat: row Z (=25), col 99
        // (< MAX_COLS). Each barrier is a single "line" message => one setX.
        constexpr int kBarrierRow = 25;   // 'Z'
        constexpr int kBarrierCol = 99;   // < MAX_COLS (100)

        // Wait (bounded) until xCount reaches target; returns reached-or-not.
        auto waitForX = [&] (int target) -> bool
        {
            for (int i = 0; i < 200; ++i)
            {
                if (sinkA.xCount.load() >= target)
                    return true;
                juce::Thread::sleep(5);
            }
            return sinkA.xCount.load() >= target;
        };

        // Fire `addressUnderTest` (optionally with an int arg), then a barrier
        // "/cs/Z/99/finger1/line" and block until the barrier's setX lands.
        // Asserts the barrier itself routed to (kBarrierRow,kBarrierCol). The
        // caller then inspects the count deltas to decide drop vs. delivery.
        auto sendThenBarrier = [&] (auto&&... sendArgs)
        {
            sender.send(std::forward<decltype(sendArgs)>(sendArgs)...);
            const int targetX = sinkA.xCount.load() + 1;
            sender.send("/cs/Z/99/finger1/line", 0.0f);
            const bool landed = waitForX(targetX);
            return landed;
        };

        // --- col >= MAX_COLS is dropped (no sink call) -------------------
        {
            const int x0 = sinkA.xCount.load();
            const int y0 = sinkA.yCount.load();
            const int o0 = sinkA.onCount.load();
            // col 100 == MAX_COLS, out of [0,100) -> parseAddress bails.
            const bool barrierLanded = sendThenBarrier("/cs/A/100/finger1/line", 64.0f);
            // Only the barrier's single setX should have happened; on/v untouched,
            // and the last seat seen must be the barrier (not col 100).
            const bool dropped = barrierLanded
                && sinkA.xCount.load() == x0 + 1
                && sinkA.yCount.load() == y0
                && sinkA.onCount.load() == o0
                && sinkA.lastRow.load() == kBarrierRow
                && sinkA.lastCol.load() == kBarrierCol;
            expect(dropped, "col >= MAX_COLS (100) is dropped: no sink call", failed);
        }

        // --- col within range still routes (control for the bound) -------
        {
            const int x0 = sinkA.xCount.load();
            sender.send("/cs/A/99/finger1/line", 127.0f); // col 99 < MAX_COLS
            const bool landed = waitForX(x0 + 1);
            expect(landed && sinkA.lastRow.load() == 0 && sinkA.lastCol.load() == 99,
                   "col == MAX_COLS-1 (99) is accepted and routes to that seat", failed);
        }

        // --- zero-arg ".../off" still triggers a note-off ----------------
        {
            // Prime the seat to "on" so a subsequent off is observable.
            sender.send("/cs/A/5/finger1/on", 1);
            const int onAfterPrime = sinkA.onCount.load() + 1;
            for (int i = 0; i < 200 && sinkA.onCount.load() < onAfterPrime; ++i)
                juce::Thread::sleep(5);

            const int o0 = sinkA.onCount.load();
            // Zero-argument /off: juce::OSCSender::send with no value sends an
            // empty-argument message -> msg.size()==0 -> Param::Off branch.
            sender.send("/cs/A/5/finger1/off");
            for (int i = 0; i < 200 && sinkA.onCount.load() < o0 + 1; ++i)
                juce::Thread::sleep(5);

            const bool offFired = sinkA.onCount.load() == o0 + 1
                && sinkA.active.load() == false
                && sinkA.lastRow.load() == 0
                && sinkA.lastCol.load() == 5;
            expect(offFired, "zero-arg '/cs/<row>/<col>/finger<n>/off' triggers a note-off", failed);
        }

        // --- finger1 vs finger2 collapse to the same seat ----------------
        {
            // The finger<n> segment is opaque; only row/col/param matter. Drive
            // the same seat through two different finger ids and confirm both
            // land on (row 1, col 7).
            sender.send("/cs/B/7/finger1/line", 0.0f);
            int target = sinkA.xCount.load() + 1;
            const bool f1 = waitForX(target);
            const int rowF1 = sinkA.lastRow.load();
            const int colF1 = sinkA.lastCol.load();

            sender.send("/cs/B/7/finger2/line", 127.0f);
            target = sinkA.xCount.load() + 1;
            const bool f2 = waitForX(target);
            const int rowF2 = sinkA.lastRow.load();
            const int colF2 = sinkA.lastCol.load();

            expect(f1 && f2
                   && rowF1 == 1 && colF1 == 7
                   && rowF2 == 1 && colF2 == 7,
                   "finger1 and finger2 collapse to the same seat (finger<n> is opaque)", failed);
        }

        // --- invalid row (non-letter / outside A..Z) is dropped ----------
        {
            const int x0 = sinkA.xCount.load();
            const int y0 = sinkA.yCount.load();
            const int o0 = sinkA.onCount.load();
            // '1' is not in A..Z -> parseAddress bails at the row check.
            const bool barrierLanded = sendThenBarrier("/cs/1/3/finger1/line", 64.0f);
            const bool dropped = barrierLanded
                && sinkA.xCount.load() == x0 + 1   // only the barrier
                && sinkA.yCount.load() == y0
                && sinkA.onCount.load() == o0
                && sinkA.lastRow.load() == kBarrierRow
                && sinkA.lastCol.load() == kBarrierCol;
            expect(dropped, "invalid row (non-letter '1') is dropped: no sink call", failed);
        }

        // --- malformed prefix is dropped ---------------------------------
        {
            const int x0 = sinkA.xCount.load();
            const int y0 = sinkA.yCount.load();
            const int o0 = sinkA.onCount.load();
            // Wrong prefix: "/xx/..." fails the '/cs/' check.
            const bool barrierLanded = sendThenBarrier("/xx/A/3/finger1/line", 64.0f);
            const bool dropped = barrierLanded
                && sinkA.xCount.load() == x0 + 1
                && sinkA.yCount.load() == y0
                && sinkA.onCount.load() == o0
                && sinkA.lastRow.load() == kBarrierRow
                && sinkA.lastCol.load() == kBarrierCol;
            expect(dropped, "malformed prefix '/xx/...' is dropped: no sink call", failed);
        }

        // --- missing param segment is dropped ----------------------------
        {
            const int x0 = sinkA.xCount.load();
            const int y0 = sinkA.yCount.load();
            const int o0 = sinkA.onCount.load();
            // No trailing '/<param>' after finger<n>: parseAddress requires the
            // finger segment to be followed by '/', so this never reaches a sink.
            const bool barrierLanded = sendThenBarrier("/cs/A/3/finger1", 64.0f);
            const bool dropped = barrierLanded
                && sinkA.xCount.load() == x0 + 1
                && sinkA.yCount.load() == y0
                && sinkA.onCount.load() == o0
                && sinkA.lastRow.load() == kBarrierRow
                && sinkA.lastCol.load() == kBarrierCol;
            expect(dropped, "missing trailing param segment is dropped: no sink call", failed);
        }

        // --- unknown trailing param is dropped (Param::None no-op) -------
        {
            const int x0 = sinkA.xCount.load();
            const int y0 = sinkA.yCount.load();
            const int o0 = sinkA.onCount.load();
            // A well-formed address with an unrecognised param ("bogus") parses
            // (valid==true) but classifyParam -> None, so no sink call fires.
            const bool barrierLanded = sendThenBarrier("/cs/A/3/finger1/bogus", 64.0f);
            const bool dropped = barrierLanded
                && sinkA.xCount.load() == x0 + 1
                && sinkA.yCount.load() == y0
                && sinkA.onCount.load() == o0
                && sinkA.lastRow.load() == kBarrierRow
                && sinkA.lastCol.load() == kBarrierCol;
            expect(dropped, "unknown trailing param ('bogus') is dropped: no sink call", failed);
        }

        // --- case-insensitive prefix + row letter are accepted -----------
        {
            // '/cs/' is matched case-insensitively on c/s, and the row letter is
            // upper-cased before the A..Z check; '/CS/c/...' must route to row 2.
            const int x0 = sinkA.xCount.load();
            sender.send("/CS/c/8/finger1/line", 0.0f);
            const bool landed = waitForX(x0 + 1);
            expect(landed && sinkA.lastRow.load() == 2 && sinkA.lastCol.load() == 8,
                   "case-insensitive prefix and lowercase row letter route correctly", failed);
        }

        // --- 'on' with explicit 0 arg routes to a note-off ---------------
        {
            // Prime on, then send on with value 0 -> firstAsInt()==0 -> setOn(false).
            sender.send("/cs/D/2/finger1/on", 1);
            int target = sinkA.onCount.load() + 1;
            for (int i = 0; i < 200 && sinkA.onCount.load() < target; ++i)
                juce::Thread::sleep(5);
            const bool wasOn = sinkA.active.load();

            sender.send("/cs/D/2/finger1/on", 0);
            target = sinkA.onCount.load() + 1;
            for (int i = 0; i < 200 && sinkA.onCount.load() < target; ++i)
                juce::Thread::sleep(5);

            expect(wasOn && sinkA.active.load() == false
                   && sinkA.lastRow.load() == 3 && sinkA.lastCol.load() == 2,
                   "'on' with arg 0 routes to a note-off on the same seat", failed);
        }
    }

    bridgeA.stop();
    bridgeB.stop();

    std::cout << "\nSummary: " << (failed == 0 ? "ok" : "failed") << "\n";
    return failed == 0 ? 0 : 1;
}
