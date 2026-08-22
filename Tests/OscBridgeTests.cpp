#include "../Source/OscBridge.h"
#include "../Source/OscWireFormat.h"
#include "../Source/SeatEventSink.h"

#include <array>
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
    struct CountingSink : SeatEventSink
    {
        static constexpr int logCapacity = 4096;

        void record (int code) noexcept
        {
            const int slot = logCount.fetch_add(1, std::memory_order_acq_rel);
            if (slot >= 0 && slot < logCapacity)
                eventLog[(size_t) slot].store(code, std::memory_order_release);
        }

        void setX (int row, int col, float xNorm) override
        {
            record(1000 + col);
            lastRow.store(row);
            lastCol.store(col);
            lastX.store(xNorm);
            xCount.fetch_add(1);
        }

        void setY (int row, int col, float yNorm) override
        {
            record(2000 + col);
            lastRow.store(row);
            lastCol.store(col);
            lastY.store(yNorm);
            yCount.fetch_add(1);
        }

        void setOn (int row, int col, bool on) override
        {
            record((on ? 3000 : 4000) + col);
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

        void recordLiveActivity (int sourceId, int finger, int activity)
        {
            lastActivitySource.store(sourceId, std::memory_order_release);
            lastActivityFinger.store(finger, std::memory_order_release);
            lastActivity.store(activity, std::memory_order_release);
            if (activity == 0)
                activityStartCount.fetch_add(1, std::memory_order_relaxed);
            else if (activity == 1)
                activityRefreshCount.fetch_add(1, std::memory_order_relaxed);
            else
                activityStopCount.fetch_add(1, std::memory_order_relaxed);

            record(5000 + activity * 1000 + sourceId);
        }

        void setLiveFingerX (int row, int sourceId, int finger, float xNorm) override
        {
            recordLiveActivity(sourceId, finger, 1);
            setFingerX(row, sourceId, finger, xNorm);
        }

        void setLiveFingerY (int row, int sourceId, int finger, float yNorm) override
        {
            recordLiveActivity(sourceId, finger, 1);
            setFingerY(row, sourceId, finger, yNorm);
        }

        void setLiveFingerOn (int row, int sourceId, int finger, bool on) override
        {
            recordLiveActivity(sourceId, finger, on ? 0 : 2);
            setFingerOn(row, sourceId, finger, on);
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
        std::atomic<int> lastActivitySource { -1 };
        std::atomic<int> lastActivityFinger { -1 };
        std::atomic<int> lastActivity { -1 };
        std::atomic<int> activityStartCount { 0 };
        std::atomic<int> activityRefreshCount { 0 };
        std::atomic<int> activityStopCount { 0 };
        std::atomic<int> logCount { 0 };
        std::array<std::atomic<int>, logCapacity> eventLog {};
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

    bool waitForMalformedDatagramCount (const OscBridge& bridge, uint32_t target)
    {
        for (int i = 0; i < 200; ++i)
        {
            if (bridge.getMalformedDatagramCount() >= target)
                return true;
            juce::Thread::sleep(5);
        }
        return bridge.getMalformedDatagramCount() >= target;
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

        constexpr const char* invalidZones[] = {
            "/cs//1/finger0/u",
            "/cs/AA/1/finger0/u",
            "/cs/A1/1/finger0/u",
            "/cs/1/1/finger0/u",
            "/cs/[/1/finger0/u"
        };
        bool zonesExact = true;
        for (const auto* address : invalidZones)
            zonesExact = zonesExact
                && ! osc_wire::parseAddress(address, SeatEventSink::MAX_OSC_SOURCES).valid;
        expect(zonesExact
                   && osc_wire::parseAddress(
                       "/CS/a/1/finger0/U", SeatEventSink::MAX_OSC_SOURCES).valid,
               "zone is exactly one case-insensitive A-Z letter", failed);

        constexpr const char* invalidSources[] = {
            "/cs/A/-1/finger0/u",
            "/cs/A/+1/finger0/u",
            "/cs/A/1.0/finger0/u",
            "/cs/A/ 1/finger0/u",
            "/cs/A/256/finger0/u",
            "/cs/A/0001/finger0/u",
            "/cs/A/999999999999999999999999999/finger0/u"
        };
        bool sourcesExact = true;
        for (const auto* address : invalidSources)
            sourcesExact = sourcesExact
                && ! osc_wire::parseAddress(address, SeatEventSink::MAX_OSC_SOURCES).valid;
        const auto zeroPadded = osc_wire::parseAddress(
            "/cs/A/005/finger0/u", SeatEventSink::MAX_OSC_SOURCES);
        expect(sourcesExact && zeroPadded.valid && zeroPadded.col == 5,
               "source is bounded decimal 0..255 without signs, suffixes or oversized digit runs", failed);

        // Every logical prefix is passed from an exactly-sized buffer. Under
        // ASan this catches reads past the NUL terminator as well as ordinary
        // grammar mistakes in shortened finger tokens.
        const std::string canonical = "/cs/A/1/finger0/u";
        bool everyTruncationRejected = true;
        for (size_t length = 0; length < canonical.size(); ++length)
        {
            std::vector<char> prefix(canonical.begin(), canonical.begin() + (std::ptrdiff_t) length);
            prefix.push_back('\0');
            prefix.shrink_to_fit();
            everyTruncationRejected = everyTruncationRejected
                && ! osc_wire::parseAddress(prefix.data(), SeatEventSink::MAX_OSC_SOURCES).valid;
        }
        expect(everyTruncationRejected
                   && ! osc_wire::parseAddress(nullptr, SeatEventSink::MAX_OSC_SOURCES).valid
                   && ! osc_wire::parseAddress("", SeatEventSink::MAX_OSC_SOURCES).valid
                   && ! osc_wire::parseAddress("/", SeatEventSink::MAX_OSC_SOURCES).valid
                   && ! osc_wire::parseAddress("/c", SeatEventSink::MAX_OSC_SOURCES).valid
                   && ! osc_wire::parseAddress("/cs", SeatEventSink::MAX_OSC_SOURCES).valid
                   && ! osc_wire::parseAddress(canonical.c_str(), 0).valid
                   && ! osc_wire::parseAddress(canonical.c_str(), -1).valid,
               "all truncated address prefixes, null input and invalid capacities fail closed", failed);

        const auto extraSegment = osc_wire::parseAddress(
            "/cs/A/1/finger0/u/extra", SeatEventSink::MAX_OSC_SOURCES);
        const auto unknownParam = osc_wire::parseAddress(
            "/cs/A/1/finger0/unknown", SeatEventSink::MAX_OSC_SOURCES);
        expect(! extraSegment.valid && ! unknownParam.valid,
               "extra path segments and unknown parameters fail address parsing", failed);
    }

    {
        CountingSink invalidPortSink;
        OscBridge invalidPortBridge(invalidPortSink);
        expect(! invalidPortBridge.start(0)
                   && ! invalidPortBridge.start(-1)
                   && ! invalidPortBridge.start(65536)
                   && ! invalidPortBridge.isRunning()
                   && ! invalidPortBridge.isReceiving()
                   && invalidPortBridge.getCurrentPort() == 0
                   && invalidPortBridge.oscStatus().contains("Invalid UDP port"),
               "UDP port validation rejects values outside 1..65535 before binding", failed);
    }

    CountingSink sinkA, sinkB, allFingersSink;
    OscBridge bridgeA(sinkA, OscBridge::FingerPolicy::finger0Only);
    OscBridge bridgeB(sinkB, OscBridge::FingerPolicy::finger0Only);
    OscBridge allFingersBridge(allFingersSink);

    int port = 62060;
    bool started = false;
    for (; port < 62120; ++port)
    {
        if (bridgeA.start(port))
        {
            started = bridgeB.start(port) && allFingersBridge.start(port);
            break;
        }
    }

    expect(started && bridgeA.isRunning() && bridgeB.isRunning()
               && allFingersBridge.isRunning()
               && bridgeA.isReceiving() && bridgeB.isReceiving()
               && allFingersBridge.isReceiving(),
           "three shared-port OscBridge instances are running and receiving", failed);
    expect(bridgeA.getValidMessageCount() == 0
               && bridgeA.getMalformedDatagramCount() == 0
               && bridgeA.getObservedZoneMask() == 0
               && bridgeA.getLastValidMessageAgeMs() == std::numeric_limits<uint32_t>::max(),
           "fresh receiver telemetry distinguishes never-received from a millisecond-counter value of zero", failed);

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
        const uint32_t allFingersValid = allFingersBridge.getValidMessageCount();
        const int aActivityStart = sinkA.activityStartCount.load();
        const int aActivityRefresh = sinkA.activityRefreshCount.load();
        const int aLog = sinkA.logCount.load();

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
        const bool defaultPolicyPreserved = waitForValidMessageCount(
            allFingersBridge, allFingersValid + 6);

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
                   && sinkA.lastFinger.load() == 0 && sinkB.lastFinger.load() == 0
                   && sinkA.activityStartCount.load() == aActivityStart + 1
                   && sinkA.activityRefreshCount.load() == aActivityRefresh + 2,
               "finger0 fans out while secondary fingers stay invisible to state and telemetry", failed);

        expect(sinkA.logCount.load() >= aLog + 6
                   && sinkA.eventLog[(size_t) aLog + 0].load() == 6000
                   && sinkA.eventLog[(size_t) aLog + 1].load() == 1000
                   && sinkA.eventLog[(size_t) aLog + 2].load() == 6000
                   && sinkA.eventLog[(size_t) aLog + 3].load() == 2000
                   && sinkA.eventLog[(size_t) aLog + 4].load() == 5000
                   && sinkA.eventLog[(size_t) aLog + 5].load() == 3000,
               "validated Refresh/Refresh/Start heartbeats publish before U/V/On setters", failed);

        expect(defaultPolicyPreserved
                   && allFingersBridge.getValidMessageCount() == allFingersValid + 6
                   && allFingersSink.onCount.load() == 2
                   && allFingersSink.xCount.load() == 2
                   && allFingersSink.yCount.load() == 2,
               "default bridge policy preserves multi-finger auxiliary products", failed);

        expect((bridgeA.getObservedZoneMask() & 1u) != 0
                   && (bridgeB.getObservedZoneMask() & 1u) != 0,
               "valid zone A messages set the zone-A telemetry bit on both clients", failed);

        // Production release is /on 0 in its own immediate bundle. Legacy
        // /off remains covered below only as a backwards-compatible input.
        {
            const int aTarget = sinkA.onCount.load() + 1;
            const int bTarget = sinkB.onCount.load() + 1;
            const int stopBefore = sinkA.activityStopCount.load();
            const int logBefore = sinkA.logCount.load();
            juce::OSCBundle releaseBundle;
            releaseBundle.addElement(juce::OSCBundle::Element(
                juce::OSCMessage("/cs/A/0/finger0/on", 0)));
            sender.send(releaseBundle);
            const bool released = waitForCount(sinkA.onCount, aTarget)
                && waitForCount(sinkB.onCount, bTarget);
            expect(released && ! sinkA.active.load() && ! sinkB.active.load()
                       && sinkA.activityStopCount.load() == stopBefore + 1
                       && sinkA.logCount.load() >= logBefore + 2
                       && sinkA.eventLog[(size_t) logBefore].load() == 7000
                       && sinkA.eventLog[(size_t) logBefore + 1].load() == 4000,
                   "production /on 0 publishes Stop heartbeat before releasing finger0", failed);
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

        // Contract segments that happen to share a valid prefix must still be
        // rejected as complete addresses, and argument cardinality is exact.
        {
            const int x0 = sinkA.xCount.load();
            const int on0 = sinkA.onCount.load();
            const uint32_t validBefore = bridgeA.getValidMessageCount();
            const int activityBefore = sinkA.activityStartCount.load()
                                     + sinkA.activityRefreshCount.load()
                                     + sinkA.activityStopCount.load();

            juce::OSCMessage extraU("/cs/A/8/finger0/u");
            extraU.addFloat32(0.25f);
            extraU.addInt32(7);
            sender.send(extraU);

            juce::OSCMessage noArgOn("/cs/A/8/finger0/on");
            sender.send(noArgOn);

            juce::OSCMessage extraOff("/cs/A/8/finger0/off");
            extraOff.addInt32(0);
            extraOff.addInt32(1);
            sender.send(extraOff);

            sender.send("/cs/A1/8/finger0/u", 0.5f);
            sender.send("/cs/A/8/finger0/u/extra", 0.5f);
            sender.send("/cs/A/8/finger0/unknown", 0.5f);

            const bool barrierLanded = sendXBarrier();
            expect(barrierLanded
                       && sinkA.xCount.load() == x0 + 1
                       && sinkA.onCount.load() == on0
                       && bridgeA.getValidMessageCount() == validBefore + 1
                       && sinkA.activityStartCount.load()
                            + sinkA.activityRefreshCount.load()
                            + sinkA.activityStopCount.load() == activityBefore + 1,
                   "malformed segments, unknown controls and extra/missing arguments fail closed", failed);
        }

        // Recursive immediate bundles retain depth-first wire order. The live
        // heartbeat is deliberately part of that order and precedes each sink
        // mutation.
        {
            const uint32_t validBefore = bridgeA.getValidMessageCount();
            const int logBefore = sinkA.logCount.load();

            juce::OSCBundle inner;
            inner.addElement(juce::OSCBundle::Element(
                juce::OSCMessage("/cs/G/20/finger0/v", 0.7f)));
            inner.addElement(juce::OSCBundle::Element(
                juce::OSCMessage("/cs/G/20/finger0/on", 1)));

            juce::OSCBundle outer;
            outer.addElement(juce::OSCBundle::Element(
                juce::OSCMessage("/cs/G/20/finger0/u", 0.2f)));
            outer.addElement(juce::OSCBundle::Element(inner));
            outer.addElement(juce::OSCBundle::Element(
                juce::OSCMessage("/cs/G/20/finger0/on", 0)));
            sender.send(outer);

            const bool landed = waitForValidMessageCount(bridgeA, validBefore + 4);
            expect(landed && sinkA.logCount.load() >= logBefore + 8
                       && sinkA.eventLog[(size_t) logBefore + 0].load() == 6020
                       && sinkA.eventLog[(size_t) logBefore + 1].load() == 1020
                       && sinkA.eventLog[(size_t) logBefore + 2].load() == 6020
                       && sinkA.eventLog[(size_t) logBefore + 3].load() == 2020
                       && sinkA.eventLog[(size_t) logBefore + 4].load() == 5020
                       && sinkA.eventLog[(size_t) logBefore + 5].load() == 3020
                       && sinkA.eventLog[(size_t) logBefore + 6].load() == 7020
                       && sinkA.eventLog[(size_t) logBefore + 7].load() == 4020,
                   "nested immediate bundles preserve U/V/On/Off and heartbeat ordering", failed);
        }

        // Dated bundles cannot be made sample-accurate from this UDP callback;
        // production sends immediate tags, so future tags fail closed instead
        // of being executed too early.
        {
            const uint32_t validBefore = bridgeA.getValidMessageCount();
            const int onBefore = sinkA.onCount.load();
            juce::OSCBundle futureBundle(juce::OSCTimeTag(
                juce::Time::getCurrentTime() + juce::RelativeTime::seconds(60.0)));
            futureBundle.addElement(juce::OSCBundle::Element(
                juce::OSCMessage("/cs/H/21/finger0/on", 1)));
            sender.send(futureBundle);
            const bool barrierLanded = sendXBarrier();
            expect(barrierLanded && sinkA.onCount.load() == onBefore
                       && bridgeA.getValidMessageCount() == validBefore + 1,
                   "non-immediate top-level bundle is ignored rather than executed early", failed);
        }

        {
            const uint32_t validBefore = bridgeA.getValidMessageCount();
            const int onBefore = sinkA.onCount.load();
            juce::OSCBundle datedChild(juce::OSCTimeTag(
                juce::Time::getCurrentTime() + juce::RelativeTime::seconds(60.0)));
            datedChild.addElement(juce::OSCBundle::Element(
                juce::OSCMessage("/cs/H/22/finger0/on", 1)));

            juce::OSCBundle immediateParent;
            immediateParent.addElement(juce::OSCBundle::Element(datedChild));
            immediateParent.addElement(juce::OSCBundle::Element(
                juce::OSCMessage("/cs/H/22/finger0/u", 0.4f)));
            sender.send(immediateParent);

            const bool landed = waitForValidMessageCount(bridgeA, validBefore + 1);
            expect(landed && sinkA.onCount.load() == onBefore
                       && bridgeA.getValidMessageCount() == validBefore + 1,
                   "a dated nested subtree is ignored while immediate siblings continue", failed);
        }

        // The final supported nesting level is processed; one additional
        // wrapper is ignored without preventing a valid top-level sibling.
        {
            auto nest = [] (juce::OSCMessage message, int bundleLevels)
            {
                juce::OSCBundle nested;
                nested.addElement(juce::OSCBundle::Element(std::move(message)));
                for (int level = 1; level < bundleLevels; ++level)
                {
                    juce::OSCBundle wrapper;
                    wrapper.addElement(juce::OSCBundle::Element(nested));
                    nested = std::move(wrapper);
                }
                return nested;
            };

            const int onBefore = sinkA.onCount.load();
            const uint32_t atLimitBefore = bridgeA.getValidMessageCount();
            auto atLimit = nest(juce::OSCMessage("/cs/I/31/finger0/on", 1),
                                OscBridge::MAX_BUNDLE_DEPTH);
            sender.send(atLimit);
            const bool atLimitLanded = waitForValidMessageCount(bridgeA, atLimitBefore + 1);

            juce::OSCBundle beyondLimit;
            beyondLimit.addElement(juce::OSCBundle::Element(atLimit));
            beyondLimit.addElement(juce::OSCBundle::Element(
                juce::OSCMessage("/cs/I/32/finger0/u", 0.6f)));
            const uint32_t beyondBefore = bridgeA.getValidMessageCount();
            sender.send(beyondLimit);
            const bool siblingLanded = waitForValidMessageCount(bridgeA, beyondBefore + 1);

            expect(atLimitLanded && siblingLanded
                       && sinkA.onCount.load() == onBefore + 1
                       && bridgeA.getValidMessageCount() == beyondBefore + 1,
                   "bundle depth cap accepts level 32 and drops only the deeper subtree", failed);
        }

        // JUCE rejects malformed/truncated byte streams before constructing an
        // OSCMessage. SharedPort exposes this separately from valid-message
        // telemetry and remains healthy for the following packet.
        {
            juce::DatagramSocket rawSender(false);
            const uint32_t malformedBefore = bridgeA.getMalformedDatagramCount();
            const uint32_t validBefore = bridgeA.getValidMessageCount();
            const std::array<char, 4> garbage { 'N', 'O', 'P', 'E' };
            const std::array<char, 8> truncated { '/', 'c', 's', '/', 'A', '/', '1', '/' };
            const bool rawSent = rawSender.write("127.0.0.1", port,
                                                 garbage.data(), (int) garbage.size())
                                      == (int) garbage.size()
                              && rawSender.write("127.0.0.1", port,
                                                 truncated.data(), (int) truncated.size())
                                      == (int) truncated.size();
            const bool malformedCounted = waitForMalformedDatagramCount(
                bridgeA, malformedBefore + 2);
            const bool barrierLanded = sendXBarrier();
            expect(rawSent && malformedCounted && barrierLanded
                       && bridgeA.getMalformedDatagramCount() == malformedBefore + 2
                       && bridgeA.getValidMessageCount() == validBefore + 1,
                   "malformed and truncated datagrams are counted, dropped and do not poison the receiver", failed);
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
    allFingersBridge.stop();

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

    // Repeated client destruction while the shared receiver is under load must
    // not leave a callback holding a dangling OscBridge/SeatEventSink pointer.
    // Sanitizer and TSan runs exercise the same path more aggressively.
    {
        CountingSink stableSink;
        OscBridge stableBridge(stableSink, OscBridge::FingerPolicy::finger0Only);
        int stressPort = 62220;
        bool stressStarted = false;
        for (; stressPort < 62320; ++stressPort)
            if ((stressStarted = stableBridge.start(stressPort)))
                break;

        std::atomic<bool> floodConnected { false };
        std::thread flood([&]
        {
            juce::OSCSender floodSender;
            const bool ok = floodSender.connect("127.0.0.1", stressPort);
            floodConnected.store(ok, std::memory_order_release);
            if (! ok)
                return;

            for (int i = 0; i < 5000; ++i)
                floodSender.send("/cs/A/77/finger0/u", (float) (i % 101) / 100.0f);
        });

        bool everyTemporaryClientRegistered = stressStarted;
        for (int iteration = 0; iteration < 250; ++iteration)
        {
            CountingSink temporarySink;
            OscBridge temporaryBridge(temporarySink, OscBridge::FingerPolicy::finger0Only);
            everyTemporaryClientRegistered = temporaryBridge.start(stressPort)
                && temporaryBridge.isReceiving()
                && everyTemporaryClientRegistered;
            temporaryBridge.stop();
        }
        flood.join();

        const bool stableReceived = waitForValidMessageCount(stableBridge, 1);
        expect(stressStarted && floodConnected.load(std::memory_order_acquire)
                   && everyTemporaryClientRegistered && stableReceived
                   && stableBridge.isRunning() && stableBridge.isReceiving(),
               "shared-port callback lifetime survives repeated client stop/destruction under flood", failed);
        stableBridge.stop();
    }

    // Venue instances can require exclusive ownership of their zone port. A
    // conflicting client must fail visibly rather than silently sharing the
    // datagrams, and expected-zone filtering happens before state/telemetry.
    {
        CountingSink exclusiveSink, conflictSink;
        OscBridge exclusiveBridge(exclusiveSink, OscBridge::FingerPolicy::finger0Only);
        OscBridge conflictBridge(conflictSink, OscBridge::FingerPolicy::finger0Only);
        int venuePort = 62320;
        bool venueStarted = false;
        for (; venuePort < 62420; ++venuePort)
            if ((venueStarted = exclusiveBridge.start(
                    venuePort, OscBridge::PortPolicy::exclusive)))
                break;

        exclusiveBridge.setExpectedZone(0); // A
        const bool conflictReported = conflictBridge.start(venuePort)
                                   && conflictBridge.isRunning()
                                   && ! conflictBridge.isReceiving()
                                   && conflictBridge.oscStatus().contains("OWNERSHIP CONFLICT");

        juce::OSCSender venueSender;
        const bool venueConnected = venueSender.connect("127.0.0.1", venuePort);
        if (venueConnected)
        {
            venueSender.send("/cs/B/7/finger0/on", 1);
            venueSender.send("/cs/A/7/finger0/on", 1);
        }
        const bool expectedAccepted = waitForValidMessageCount(exclusiveBridge, 1);
        expect(venueStarted && venueConnected && conflictReported
                   && expectedAccepted
                   && exclusiveBridge.isExclusive()
                   && exclusiveBridge.getExpectedZone() == 0
                   && exclusiveBridge.getZoneMismatchCount() == 1
                   && exclusiveBridge.getValidMessageCount() == 1
                   && exclusiveSink.onCount.load() == 1
                   && exclusiveSink.lastRow.load() == 0,
               "exclusive port ownership and expected-zone filtering fail closed", failed);

        exclusiveBridge.stop();
        const bool sharedRecovered = conflictBridge.start(venuePort)
                                  && conflictBridge.isReceiving();
        OscBridge lateExclusive(exclusiveSink, OscBridge::FingerPolicy::finger0Only);
        const bool lateExclusiveRejected = lateExclusive.start(
                venuePort, OscBridge::PortPolicy::exclusive)
            && ! lateExclusive.isReceiving()
            && lateExclusive.oscStatus().contains("OWNERSHIP CONFLICT");
        expect(sharedRecovered && lateExclusiveRejected,
               "released exclusive ports recover and reject late exclusive claims while shared", failed);

        lateExclusive.stop();
        conflictBridge.stop();
    }

    std::cout << "\nSummary: " << (failed == 0 ? "ok" : "failed") << "\n";
    return failed == 0 ? 0 : 1;
}
