#include "OscBridge.h"
#include "OscWireFormat.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>

struct OscBridge::SharedPort final
    : private juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>
{
    explicit SharedPort (int portToUse) : port(portToUse), receiver("AudienceHarmonicSynthOSC")
    {
        for (auto& client : clients)
            client.store(nullptr, std::memory_order_relaxed);
    }

    ~SharedPort() override
    {
        receiver.removeListener(this);
        receiver.disconnect();
    }

    bool connect()
    {
        if (! receiver.connect(port))
            return false;

        receiver.addListener(this);
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
        for (const auto& el : bundle)
        {
            if (el.isMessage())      oscMessageReceived(el.getMessage());
            else if (el.isBundle())  oscBundleReceived(el.getBundle());
        }
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
}

OscBridge::OscBridge (SeatEventSink& t) : target(t) {}

OscBridge::~OscBridge() { stop(); }

bool OscBridge::start (int port)
{
    stop();
    validMessageCount.store(0, std::memory_order_relaxed);
    observedZoneMask.store(0, std::memory_order_relaxed);
    lastValidMessageMs.store(0, std::memory_order_relaxed);

    std::shared_ptr<SharedPort> portHandle;
    {
        const juce::ScopedLock lock(sharedPortsLock);

        if (auto existing = sharedPorts[port].lock())
        {
            portHandle = existing;
        }
        else
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
        if (sharedPort != nullptr)
            sharedPort->removeClient(*this);

        sharedPort.reset();
        running = false;
        receiving = false;
        currentPort = 0;
        statusString = "Stopped";
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
    // This installation intentionally treats each phone as one voice. Reject
    // secondary fingers before telemetry or audience state is touched, so
    // finger1..finger9 cannot inflate the visible crowd or create MIDI owners.
    if (! parsed.valid || parsed.finger != 0)
        return;

    const int   row   = parsed.row;
    const int   col   = parsed.col;
    const int   finger = parsed.finger;
    const auto  param = osc_wire::classifyParam(parsed.param);

    if (msg.size() == 0)
    {
        // /off with no args
        if (param == osc_wire::Param::Off)
        {
            target.setFingerOn(row, col, finger, false);
            observedZoneMask.fetch_or(1u << (uint32_t) row, std::memory_order_relaxed);
            validMessageCount.fetch_add(1, std::memory_order_relaxed);
            lastValidMessageMs.store(juce::Time::getMillisecondCounter(), std::memory_order_release);
        }
        return;
    }

    float firstValue = 0.0f;
    const auto readFiniteNumeric = [&]() -> bool
    {
        if (msg[0].isFloat32())
            firstValue = msg[0].getFloat32();
        else if (msg[0].isInt32())
            firstValue = (float) msg[0].getInt32();
        else
            return false;

        return std::isfinite(firstValue);
    };

    // Every parameter carrying an argument accepts OSC int32/float32 only.
    // This also keeps NaN/Inf from entering the routing and MIDI state.
    if (! readFiniteNumeric())
        return;

    bool handled = true;
    switch (param)
    {
        case osc_wire::Param::U:
        {
            const float x = juce::jlimit(0.0f, 1.0f, firstValue);
            target.setFingerX(row, col, finger, x);
            break;
        }
        case osc_wire::Param::V:
        {
            const float y = juce::jlimit(0.0f, 1.0f, firstValue);
            target.setFingerY(row, col, finger, y);
            break;
        }
        case osc_wire::Param::Line:
        {
            const float xNorm = juce::jlimit(0.0f, 1.0f, firstValue / 127.0f);
            target.setFingerX(row, col, finger, xNorm);
            break;
        }
        case osc_wire::Param::On:
            target.setFingerOn(row, col, finger, firstValue != 0.0f);
            break;
        case osc_wire::Param::Off:
            target.setFingerOn(row, col, finger, false);
            break;
        case osc_wire::Param::None:
        default:
            handled = false;
            break;
    }

    if (handled)
    {
        observedZoneMask.fetch_or(1u << (uint32_t) row, std::memory_order_relaxed);
        validMessageCount.fetch_add(1, std::memory_order_relaxed);
        lastValidMessageMs.store(juce::Time::getMillisecondCounter(), std::memory_order_release);
    }
}
