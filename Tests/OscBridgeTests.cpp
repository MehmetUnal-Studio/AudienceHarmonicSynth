#include "../Source/OscBridge.h"

#include <atomic>
#include <iostream>

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

    bridgeA.stop();
    bridgeB.stop();

    std::cout << "\nSummary: " << (failed == 0 ? "ok" : "failed") << "\n";
    return failed == 0 ? 0 : 1;
}
