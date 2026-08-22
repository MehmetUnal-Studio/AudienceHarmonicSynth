#include "OscBridge.h"
#include "OscWireFormat.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>

struct OscBridge::SharedPort final
    : private juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>
{
    explicit SharedPort (int portToUse) : port(portToUse), receiver("AudienceHarmonicSynthOSC")
    {
        for (auto& client : clients)
            client.store(nullptr, std::memory_order_relaxed);

        // JUCE rejects malformed/truncated OSC before listener dispatch. Fan
        // that fact out as telemetry without inspecting or retaining attacker
        // controlled payload bytes.
        receiver.registerFormatErrorHandler(
            [this] (const char*, int) { oscFormatErrorReceived(); });
        // JUCE's realtime listener list is not internally synchronised. Install
        // the listener before connect() starts its network thread.
        receiver.addListener(this);
    }

    ~SharedPort() override
    {
        // Stop and join the network thread before mutating JUCE's unsynchronised
        // listener list. Reversing this order is a real add/remove-vs-callback
        // data race under shared-port churn.
        receiver.disconnect();
        receiver.removeListener(this);
    }

    bool connect()
    {
        if (! receiver.connect(port))
            return false;
        return true;
    }

    // Returns true if the bridge is registered as a client (either it was
    // already present or a free slot accepted it), false if the cap was
    // reached and the client could not be added (B25). The connection /
    // slot-claim logic itself is unchanged; we only report the outcome.
    bool addClient (OscBridge& bridge)
    {
        const juce::ScopedLock lock(clientsLock);

        for (auto& client : clients)
            if (client.load(std::memory_order_acquire) == &bridge)
                return true;

        for (auto& client : clients)
        {
            OscBridge* empty = nullptr;
            if (client.compare_exchange_strong(empty, &bridge, std::memory_order_release, std::memory_order_relaxed))
                return true;
        }

        return false; // all slots full -> client silently dropped previously
    }

    void removeClient (OscBridge& bridge)
    {
        // The callback holds the same network-thread lock while dereferencing a
        // client. Waiting here guarantees that stop()/the OscBridge destructor
        // cannot return while a callback still owns a raw pointer to the bridge.
        const juce::ScopedLock lock(clientsLock);

        for (auto& client : clients)
        {
            OscBridge* expected = &bridge;
            client.compare_exchange_strong(expected, nullptr, std::memory_order_release, std::memory_order_relaxed);
        }
    }

private:
    void oscMessageReceived (const juce::OSCMessage& msg) override
    {
        const juce::ScopedLock lock(clientsLock);

        for (auto& entry : clients)
        {
            auto* client = entry.load(std::memory_order_acquire);
            if (client != nullptr)
                client->oscMessageReceived(msg);
        }
    }

    void oscBundleReceived (const juce::OSCBundle& bundle) override
    {
        deliverBundle(bundle, 0);
    }

    void deliverBundle (const juce::OSCBundle& bundle, int depth)
    {
        // The production sender uses immediate bundles. Executing a dated
        // bundle at arrival time would be musically false, while scheduling it
        // against wall time would conflict with the host-synchronised Time
        // Field. Fail closed instead, and apply the rule at every nested level.
        if (depth >= OscBridge::MAX_BUNDLE_DEPTH
            || ! bundle.getTimeTag().isImmediately())
            return;

        for (const auto& el : bundle)
        {
            if (el.isMessage())      oscMessageReceived(el.getMessage());
            else if (el.isBundle())  deliverBundle(el.getBundle(), depth + 1);
        }
    }

    void oscFormatErrorReceived()
    {
        const juce::ScopedLock lock(clientsLock);
        for (auto& entry : clients)
            if (auto* client = entry.load(std::memory_order_acquire))
                client->recordMalformedDatagram();
    }

    int port = 0;
    juce::OSCReceiver receiver;
    juce::CriticalSection clientsLock;
    std::array<std::atomic<OscBridge*>, (std::size_t) OscBridge::MAX_SHARED_CLIENTS> clients {};
};

namespace
{
    juce::CriticalSection sharedPortsLock;
    std::map<int, std::weak_ptr<OscBridge::SharedPort>> sharedPorts;

    void incrementSaturating (std::atomic<uint32_t>& counter) noexcept
    {
        auto current = counter.load(std::memory_order_relaxed);
        while (current != std::numeric_limits<uint32_t>::max()
               && ! counter.compare_exchange_weak(current, current + 1,
                                                   std::memory_order_relaxed,
                                                   std::memory_order_relaxed))
        {
        }
    }
}

OscBridge::OscBridge (SeatEventSink& t, FingerPolicy policy)
    : target(t), fingerPolicy(policy) {}

OscBridge::~OscBridge() { stop(); }

bool OscBridge::start (int port)
{
    stop();
    validMessageCount.store(0, std::memory_order_relaxed);
    observedZoneMask.store(0, std::memory_order_relaxed);
    lastValidMessageMs.store(0, std::memory_order_relaxed);
    malformedDatagramCount.store(0, std::memory_order_relaxed);
    hasReceivedValidMessage.store(false, std::memory_order_release);

    if (port < 1 || port > 65535)
    {
        running = false;
        receiving = false;
        currentPort = 0;
        statusString = "Invalid UDP port " + juce::String(port) + " (expected 1..65535)";
        return false;
    }

    std::shared_ptr<SharedPort> portHandle;
    {
        const juce::ScopedLock lock(sharedPortsLock);

        const auto existingEntry = sharedPorts.find(port);
        if (existingEntry != sharedPorts.end())
        {
            portHandle = existingEntry->second.lock();
            if (portHandle == nullptr)
                sharedPorts.erase(existingEntry);
        }

        if (portHandle == nullptr)
        {
            auto created = std::make_shared<SharedPort>(port);
            if (! created->connect())
            {
                sharedPorts.erase(port);
                running = false;
                receiving = false;
                currentPort = 0;
                statusString = "FAILED to bind UDP " + juce::String(port) + " - port busy?";
                return false;
            }

            sharedPorts[port] = created;
            portHandle = std::move(created);
        }
    }

    // Connection logic is unchanged; we only observe whether this bridge got a
    // fan-out slot. When the shared port is already full the client is dropped
    // exactly as before, but we now surface it instead of failing silently (B25).
    const bool registered = portHandle->addClient(*this);
    sharedPort = std::move(portHandle);
    currentPort = port;
    running     = true;
    receiving   = registered;

    if (registered)
        statusString = "Listening on UDP " + juce::String(port);
    else
        statusString = "UDP " + juce::String(port) + " PORT FULL - max "
                     + juce::String(MAX_SHARED_CLIENTS)
                     + " clients reached, this instance is not receiving OSC";

    return true;
}

void OscBridge::stop()
{
    if (running)
    {
        const int stoppedPort = currentPort;
        if (sharedPort != nullptr)
            sharedPort->removeClient(*this);

        sharedPort.reset();
        running = false;
        receiving = false;
        currentPort = 0;
        statusString = "Stopped";

        // Do not retain one expired weak-map node for every port ever tried.
        const juce::ScopedLock lock(sharedPortsLock);
        const auto entry = sharedPorts.find(stoppedPort);
        if (entry != sharedPorts.end() && entry->second.expired())
            sharedPorts.erase(entry);
    }
}

void OscBridge::oscMessageReceived (const juce::OSCMessage& msg)
{
    // Parse the address straight off the OSCAddressPattern's raw UTF-8 storage
    // (B26): toString() hands back the pattern's cached, already-UTF-8 string as
    // a copy-on-write reference (a ref-count bump, no character-data copy), and
    // toRawUTF8() then returns its internal buffer with no allocation (JUCE
    // strings are UTF-8 on this platform). We bind that string to a const ref so
    // its buffer stays alive while we parse off the raw pointer; no per-message
    // juce::String of our own is constructed. The wire-format grammar + matching
    // lives in the shared, documented header (B16).
    const juce::String& addr = msg.getAddressPattern().toString();
    const char* raw = addr.toRawUTF8();

    const auto parsed = osc_wire::parseAddress(raw, SeatEventSink::MAX_OSC_SOURCES);
    // A finger0-only client treats each phone as one voice. Reject secondary
    // fingers before telemetry or audience state is touched, so they cannot
    // inflate the visible crowd or create MIDI owners for that client.
    if (! parsed.valid
        || (fingerPolicy == FingerPolicy::finger0Only && parsed.finger != 0))
        return;

    const int   row   = parsed.row;
    const int   col   = parsed.col;
    const int   finger = parsed.finger;
    const auto  param = osc_wire::classifyParam(parsed.param);

    if (param == osc_wire::Param::None)
        return;

    if (param == osc_wire::Param::Off && msg.size() == 0)
    {
        target.setLiveFingerOn(row, col, finger, false);
        recordAcceptedMessage(row);
        return;
    }

    // U/V/On/Line require exactly one argument. Legacy Off accepts either no
    // argument (above) or exactly one finite numeric value. Extra OSC arguments
    // are rejected so malformed producer payloads cannot be mistaken as valid
    // traffic in telemetry.
    if (msg.size() != 1)
        return;

    float firstValue = 0.0f;
    if (msg[0].isFloat32())
        firstValue = msg[0].getFloat32();
    else if (msg[0].isInt32())
        firstValue = (float) msg[0].getInt32();
    else
        return;

    // Every parameter carrying an argument accepts OSC int32/float32 only.
    // This also keeps NaN/Inf from entering the routing and MIDI state.
    if (! std::isfinite(firstValue))
        return;

    switch (param)
    {
        case osc_wire::Param::U:
        {
            const float x = juce::jlimit(0.0f, 1.0f, firstValue);
            target.setLiveFingerX(row, col, finger, x);
            break;
        }
        case osc_wire::Param::V:
        {
            const float y = juce::jlimit(0.0f, 1.0f, firstValue);
            target.setLiveFingerY(row, col, finger, y);
            break;
        }
        case osc_wire::Param::Line:
        {
            const float xNorm = juce::jlimit(0.0f, 1.0f, firstValue / 127.0f);
            target.setLiveFingerX(row, col, finger, xNorm);
            break;
        }
        case osc_wire::Param::On:
        {
            const bool on = firstValue != 0.0f;
            target.setLiveFingerOn(row, col, finger, on);
            break;
        }
        case osc_wire::Param::Off:
            target.setLiveFingerOn(row, col, finger, false);
            break;
        case osc_wire::Param::None:
        default:
            return;
    }

    recordAcceptedMessage(row);
}

void OscBridge::recordAcceptedMessage (int row) noexcept
{
    observedZoneMask.fetch_or(1u << (uint32_t) row, std::memory_order_relaxed);
    incrementSaturating(validMessageCount);
    lastValidMessageMs.store(juce::Time::getMillisecondCounter(), std::memory_order_release);
    hasReceivedValidMessage.store(true, std::memory_order_release);
}

void OscBridge::recordMalformedDatagram() noexcept
{
    incrementSaturating(malformedDatagramCount);
}
