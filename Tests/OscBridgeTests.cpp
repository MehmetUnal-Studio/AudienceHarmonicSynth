#include "../Source/OscBridge.h"
#include "../Source/OscWireFormat.h"
#include "../Source/SeatEventSink.h"

#include <array>
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>

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

        void setFingerX (int row, int sourceId, int finger, float xNorm) override
        {
            lastFinger.store(finger);
            setX(row, sourceId, xNorm);
        }

        void setFingerY (int row, int sourceId, int finger, float yNorm) override
        {
            lastFinger.store(finger);
            setY(row, sourceId, yNorm);
        }

        void setFingerOn (int row, int sourceId, int finger, bool on) override
        {
            lastFinger.store(finger);
            setOn(row, sourceId, on);
        }

        std::atomic<int> lastRow { -1 };
        std::atomic<int> lastCol { -1 };
        std::atomic<int> lastFinger { -1 };
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

    bool waitForCount (const std::atomic<int>& count, int target)
    {
        for (int i = 0; i < 200; ++i)
        {
            if (count.load() >= target)
                return true;
            juce::Thread::sleep(5);
        }
        return count.load() >= target;
    }

    bool waitForValidMessageCount (const OscBridge& bridge, uint32_t target)
    {
        for (int i = 0; i < 200; ++i)
        {
            if (bridge.getValidMessageCount() >= target)
                return true;
            juce::Thread::sleep(5);
        }
        return bridge.getValidMessageCount() >= target;
    }

    bool nearlyEqual (float a, float b) noexcept
    {
        return std::abs(a - b) < 1.0e-5f;
    }
}

int main()
{
    int failed = 0;

    // Address parsing preserves source and finger identity. The audio grid is
    // still 100 columns wide, while OSC accepts participant ids 0..255.
    {
        const auto parsed = osc_wire::parseAddress(
            "/cs/A/255/finger9/u", SeatEventSink::MAX_OSC_SOURCES);
        expect(parsed.valid && parsed.row == 0 && parsed.col == 255
                   && parsed.finger == 9
                   && osc_wire::classifyParam(parsed.param) == osc_wire::Param::U,
               "source 255, finger9 and /u parse successfully", failed);

        expect(! osc_wire::parseAddress(
                    "/cs/A/256/finger0/u", SeatEventSink::MAX_OSC_SOURCES).valid,
               "source 256 is outside the OSC participant range", failed);

        expect(osc_wire::parseAddress(
                   "/cs/Z/0/finger0/on", SeatEventSink::MAX_OSC_SOURCES).valid,
               "finger0 is valid", failed);
        expect(osc_wire::parseAddress(
                   "/cs/Z/0/finger9/off", SeatEventSink::MAX_OSC_SOURCES).valid,
               "finger9 is valid", failed);

        constexpr const char* invalidFingers[] = {
            "/cs/A/1/finger/u",
            "/cs/A/1/finger10/u",
            "/cs/A/1/finger01/u",
            "/cs/A/1/fingerx/u",
            "/cs/A/1/Finger1/u",
            "/cs/A/1/thumb1/u"
        };
        bool allRejected = true;
        for (const auto* address : invalidFingers)
            allRejected = allRejected
                && ! osc_wire::parseAddress(address, SeatEventSink::MAX_OSC_SOURCES).valid;
        expect(allRejected, "only exact finger0..finger9 segments are accepted", failed);
    }

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

    expect(started && bridgeA.isRunning() && bridgeB.isRunning()
               && bridgeA.isReceiving() && bridgeB.isReceiving(),
           "two shared-port OscBridge instances are running and receiving", failed);

    juce::OSCSender sender;
    const bool connected = sender.connect("127.0.0.1", port);
    expect(connected, "OSC sender connects to shared test port", failed);

    if (connected)
    {
        const int aOn = sinkA.onCount.load();
        const int aX = sinkA.xCount.load();
        const int aY = sinkA.yCount.load();
        const int bOn = sinkB.onCount.load();
        const int bX = sinkB.xCount.load();
        const int bY = sinkB.yCount.load();
        const uint32_t aValid = bridgeA.getValidMessageCount();
        const uint32_t bValid = bridgeB.getValidMessageCount();

        // Secondary-finger traffic may exist on the live wire, but this
        // single-finger receiver must ignore it before sink state and valid
        // message telemetry. The following finger0 triplet is an ordered UDP
        // barrier and the only traffic expected to land.
        juce::OSCBundle startBundle;
        startBundle.addElement(juce::OSCBundle::Element(
            juce::OSCMessage("/cs/A/0/finger1/on", 1)));
        startBundle.addElement(juce::OSCBundle::Element(
            juce::OSCMessage("/cs/A/0/finger1/u", 0.9f)));
        startBundle.addElement(juce::OSCBundle::Element(
            juce::OSCMessage("/cs/A/0/finger1/v", 0.1f)));
        startBundle.addElement(juce::OSCBundle::Element(
            juce::OSCMessage("/cs/A/0/finger0/u", 0.25f)));
        startBundle.addElement(juce::OSCBundle::Element(
            juce::OSCMessage("/cs/A/0/finger0/v", 0.5f)));
        startBundle.addElement(juce::OSCBundle::Element(
            juce::OSCMessage("/cs/A/0/finger0/on", 1)));
        sender.send(startBundle);

        const bool fannedOut = waitForCount(sinkA.onCount, aOn + 1)
            && waitForCount(sinkA.xCount, aX + 1)
            && waitForCount(sinkA.yCount, aY + 1)
            && waitForCount(sinkB.onCount, bOn + 1)
            && waitForCount(sinkB.xCount, bX + 1)
            && waitForCount(sinkB.yCount, bY + 1);
        const bool telemetryFannedOut = waitForValidMessageCount(bridgeA, aValid + 3)
            && waitForValidMessageCount(bridgeB, bValid + 3);

        expect(fannedOut && telemetryFannedOut
                   && sinkA.onCount.load() == aOn + 1
                   && sinkA.xCount.load() == aX + 1
                   && sinkA.yCount.load() == aY + 1
                   && sinkB.onCount.load() == bOn + 1
                   && sinkB.xCount.load() == bX + 1
                   && sinkB.yCount.load() == bY + 1
                   && bridgeA.getValidMessageCount() == aValid + 3
                   && bridgeB.getValidMessageCount() == bValid + 3
                   && sinkA.active.load() && sinkB.active.load()
                   && sinkA.lastRow.load() == 0 && sinkB.lastRow.load() == 0
                   && sinkA.lastCol.load() == 0 && sinkB.lastCol.load() == 0
                   && sinkA.lastFinger.load() == 0 && sinkB.lastFinger.load() == 0,
               "finger0 fans out while secondary fingers stay invisible to state and telemetry", failed);

        expect((bridgeA.getObservedZoneMask() & 1u) != 0
                   && (bridgeB.getObservedZoneMask() & 1u) != 0,
               "valid zone A messages set the zone-A telemetry bit on both clients", failed);

        // Production release is /on 0 in its own immediate bundle. Legacy
        // /off remains covered below only as a backwards-compatible input.
        {
            const int aTarget = sinkA.onCount.load() + 1;
            const int bTarget = sinkB.onCount.load() + 1;
            juce::OSCBundle releaseBundle;
            releaseBundle.addElement(juce::OSCBundle::Element(
                juce::OSCMessage("/cs/A/0/finger0/on", 0)));
            sender.send(releaseBundle);
            const bool released = waitForCount(sinkA.onCount, aTarget)
                && waitForCount(sinkB.onCount, bTarget);
            expect(released && ! sinkA.active.load() && ! sinkB.active.load(),
                   "production /on 0 bundle releases finger0", failed);
        }

        // A known-good packet sent after a packet under test acts as an ordered
        // localhost UDP barrier. It lets the drop tests remain deterministic.
        auto sendXBarrier = [&]()
        {
            const int target = sinkA.xCount.load() + 1;
            sender.send("/cs/Z/99/finger0/line", 0.0f);
            return waitForCount(sinkA.xCount, target);
        };

        // /u is already normalised and the complete participant range routes.
        {
            const int x0 = sinkA.xCount.load();
            const uint32_t aBefore = bridgeA.getValidMessageCount();
            const uint32_t bBefore = bridgeB.getValidMessageCount();
            sender.send("/cs/B/255/finger0/u", 0.25f);
            const bool landed = waitForCount(sinkA.xCount, x0 + 1)
                && waitForValidMessageCount(bridgeA, aBefore + 1)
                && waitForValidMessageCount(bridgeB, bBefore + 1);
            constexpr uint32_t zonesAB = (1u << 0) | (1u << 1);
            const bool telemetryOk = (bridgeA.getObservedZoneMask() & zonesAB) == zonesAB
                && (bridgeB.getObservedZoneMask() & zonesAB) == zonesAB
                && bridgeA.getLastValidMessageAgeMs() < 5000
                && bridgeB.getLastValidMessageAgeMs() < 5000;
            expect(landed && telemetryOk
                       && sinkA.lastRow.load() == 1 && sinkA.lastCol.load() == 255
                       && sinkA.lastFinger.load() == 0
                       && nearlyEqual(sinkA.lastX.load(), 0.25f),
                   "/u routes source 255/finger0 and records zones A/B with a recent age", failed);
        }

        // The second, valid packet is an ordered localhost barrier. Exactly one
        // telemetry increment proves the malformed packet before it was ignored.
        {
            const uint32_t aBefore = bridgeA.getValidMessageCount();
            const uint32_t bBefore = bridgeB.getValidMessageCount();
            sender.send("/not-cs/A/1/finger0/u", 0.5f);
            sender.send("/cs/A/1/finger0/u", 0.5f);
            const bool barrierLanded = waitForValidMessageCount(bridgeA, aBefore + 1)
                && waitForValidMessageCount(bridgeB, bBefore + 1);
            expect(barrierLanded
                       && bridgeA.getValidMessageCount() == aBefore + 1
                       && bridgeB.getValidMessageCount() == bBefore + 1,
                   "invalid OSC addresses do not increment valid-message telemetry", failed);
        }

        // Source 256 is rejected, while a following valid barrier is delivered.
        {
            const int x0 = sinkA.xCount.load();
            sender.send("/cs/A/256/finger0/u", 0.5f);
            const bool barrierLanded = sendXBarrier();
            expect(barrierLanded && sinkA.xCount.load() == x0 + 1
                       && sinkA.lastRow.load() == 25 && sinkA.lastCol.load() == 99,
                   "source 256 is dropped without a sink call", failed);
        }

        // Secondary fingers neither reach the sink nor create zone/valid
        // telemetry. A following finger0 packet is the ordered UDP barrier.
        {
            const int x0 = sinkA.xCount.load();
            const int on0 = sinkA.onCount.load();
            const uint32_t aBefore = bridgeA.getValidMessageCount();
            const uint32_t bBefore = bridgeB.getValidMessageCount();
            sender.send("/cs/C/7/finger1/u", 0.1f);
            sender.send("/cs/C/7/finger9/on", 1);
            sender.send("/cs/Z/99/finger0/u", 0.0f);
            const bool barrierLanded = waitForValidMessageCount(bridgeA, aBefore + 1)
                && waitForValidMessageCount(bridgeB, bBefore + 1);
            expect(barrierLanded
                       && bridgeA.getValidMessageCount() == aBefore + 1
                       && bridgeB.getValidMessageCount() == bBefore + 1
                       && sinkA.xCount.load() == x0 + 1
                       && sinkA.onCount.load() == on0
                       && sinkA.lastFinger.load() == 0
                       && (bridgeA.getObservedZoneMask() & (1u << 2)) == 0
                       && (bridgeB.getObservedZoneMask() & (1u << 2)) == 0,
                   "finger1..finger9 do not create sink state, crowd counts or zone telemetry", failed);
        }

        // Legacy /line remains 0..127, while /u and /v clamp direct values.
        {
            int target = sinkA.xCount.load() + 1;
            sender.send("/cs/D/3/finger0/line", 64.0f);
            const bool lineLanded = waitForCount(sinkA.xCount, target);
            const bool lineScaled = nearlyEqual(sinkA.lastX.load(), 64.0f / 127.0f);

            target = sinkA.xCount.load() + 1;
            sender.send("/cs/D/3/finger0/u", 1.5f);
            const bool uLanded = waitForCount(sinkA.xCount, target);

            const int yTarget = sinkA.yCount.load() + 1;
            sender.send("/cs/D/3/finger0/v", -0.5f);
            const bool vLanded = waitForCount(sinkA.yCount, yTarget);

            expect(lineLanded && lineScaled && uLanded && vLanded
                       && nearlyEqual(sinkA.lastX.load(), 1.0f)
                       && nearlyEqual(sinkA.lastY.load(), 0.0f),
                   "/line scales 0..127 and /u,/v clamp finite values to 0..1", failed);
        }

        // Numeric int32/float32 values are accepted for on/off. In particular,
        // a non-zero fractional float is true rather than being truncated.
        {
            int target = sinkA.onCount.load() + 1;
            sender.send("/cs/E/4/finger0/on", 0.5f);
            const bool floatOn = waitForCount(sinkA.onCount, target) && sinkA.active.load();

            target = sinkA.onCount.load() + 1;
            sender.send("/cs/E/4/finger0/off", 123);
            const bool numericOff = waitForCount(sinkA.onCount, target) && ! sinkA.active.load();

            target = sinkA.onCount.load() + 1;
            sender.send("/cs/E/4/finger0/on", 1);
            const bool primed = waitForCount(sinkA.onCount, target) && sinkA.active.load();

            target = sinkA.onCount.load() + 1;
            sender.send("/cs/E/4/finger0/off");
            const bool emptyOff = waitForCount(sinkA.onCount, target) && ! sinkA.active.load();

            expect(floatOn && numericOff && primed && emptyOff,
                   "on accepts numeric int/float and off accepts numeric or no argument", failed);
        }

        // Non-numeric and non-finite values must not mutate any sink state.
        {
            const int x0 = sinkA.xCount.load();
            sender.send("/cs/F/5/finger0/u", juce::String("bad"));
            const bool barrierLanded = sendXBarrier();
            expect(barrierLanded && sinkA.xCount.load() == x0 + 1,
                   "non-numeric /u is dropped", failed);
        }

        {
            const int x0 = sinkA.xCount.load();
            sender.send("/cs/F/5/finger0/u", std::numeric_limits<float>::quiet_NaN());
            const bool barrierLanded = sendXBarrier();
            expect(barrierLanded && sinkA.xCount.load() == x0 + 1,
                   "NaN /u is dropped", failed);
        }

        {
            const int y0 = sinkA.yCount.load();
            sender.send("/cs/F/5/finger0/v", std::numeric_limits<float>::infinity());
            const bool barrierLanded = sendXBarrier();
            expect(barrierLanded && sinkA.yCount.load() == y0,
                   "infinite /v is dropped", failed);
        }

        {
            const int on0 = sinkA.onCount.load();
            sender.send("/cs/F/5/finger0/on", juce::String("yes"));
            const bool barrierLanded = sendXBarrier();
            expect(barrierLanded && sinkA.onCount.load() == on0,
                   "non-numeric /on is dropped", failed);
        }

        {
            const int on0 = sinkA.onCount.load();
            sender.send("/cs/F/5/finger0/off", juce::String("ignored"));
            const bool barrierLanded = sendXBarrier();
            expect(barrierLanded && sinkA.onCount.load() == on0,
                   "non-numeric /off is dropped", failed);
        }
    }

    bridgeA.stop();
    bridgeB.stop();

    // A bound shared port can host MAX_SHARED_CLIENTS registered receivers.
    // One additional instance still owns a running shared-port handle, but it
    // must report that it is not receiving and surface the PORT FULL condition.
    {
        constexpr size_t bridgeCount = (size_t) OscBridge::MAX_SHARED_CLIENTS + 1;
        std::array<CountingSink, bridgeCount> sinks;
        std::array<std::unique_ptr<OscBridge>, bridgeCount> bridges;
        for (size_t i = 0; i < bridgeCount; ++i)
            bridges[i] = std::make_unique<OscBridge>(sinks[i]);

        int overflowPort = 62120;
        bool overflowStarted = false;
        for (; overflowPort < 62220; ++overflowPort)
        {
            if (! bridges[0]->start(overflowPort))
                continue;

            overflowStarted = true;
            for (size_t i = 1; i < bridgeCount; ++i)
                overflowStarted = bridges[i]->start(overflowPort) && overflowStarted;
            break;
        }

        bool registeredClientsReceiving = overflowStarted;
        for (size_t i = 0; i < bridgeCount - 1; ++i)
            registeredClientsReceiving = registeredClientsReceiving
                && bridges[i]->isRunning() && bridges[i]->isReceiving();

        const auto& overflowClient = *bridges.back();
        expect(registeredClientsReceiving,
               "the shared port registers and receives on all available client slots", failed);
        expect(overflowStarted && overflowClient.isRunning()
                   && ! overflowClient.isReceiving()
                   && overflowClient.oscStatus().contains("PORT FULL"),
               "the client beyond MAX_SHARED_CLIENTS is running but not receiving and reports PORT FULL", failed);

        // Once a registered instance leaves, a previously-full instance can
        // retry the same port and claim the released slot. This mirrors the UI
        // Apply action after a PORT FULL warning.
        bridges[0]->stop();
        const bool recovered = bridges.back()->start(overflowPort);
        expect(recovered && bridges.back()->isRunning() && bridges.back()->isReceiving()
                   && bridges.back()->oscStatus().contains("Listening"),
               "a PORT FULL client can retry the same UDP port after a slot is released", failed);

        for (auto& bridge : bridges)
            bridge->stop();
    }

    std::cout << "\nSummary: " << (failed == 0 ? "ok" : "failed") << "\n";
    return failed == 0 ? 0 : 1;
}
