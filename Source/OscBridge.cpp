#include "OscBridge.h"
#include <algorithm>
#include <map>
#include <vector>

struct OscBridge::SharedPort final
    : private juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>
{
    explicit SharedPort (int portToUse) : port(portToUse), receiver("AudienceHarmonicSynthOSC") {}

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

    void addClient (OscBridge& bridge)
    {
        const juce::ScopedLock lock(clientsLock);
        for (auto* existing : clients)
            if (existing == &bridge)
                return;

        clients.push_back(&bridge);
    }

    void removeClient (OscBridge& bridge)
    {
        const juce::ScopedLock lock(clientsLock);
        clients.erase(std::remove(clients.begin(), clients.end(), &bridge), clients.end());
    }

private:
    void oscMessageReceived (const juce::OSCMessage& msg) override
    {
        const juce::ScopedLock lock(clientsLock);
        for (auto* client : clients)
            if (client != nullptr)
                client->oscMessageReceived(msg);
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
    std::vector<OscBridge*> clients;
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
                currentPort = 0;
                return false;
            }

            sharedPorts[port] = created;
            portHandle = std::move(created);
        }
    }

    portHandle->addClient(*this);
    sharedPort = std::move(portHandle);
    currentPort = port;
    running     = true;
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
        currentPort = 0;
    }
}

void OscBridge::oscMessageReceived (const juce::OSCMessage& msg)
{
    const auto addr = msg.getAddressPattern().toString();
    if (! addr.startsWithIgnoreCase("/cs/"))
        return;

    // Tokenize the path: /cs/<row>/<col>/finger<n>/<param>
    juce::StringArray parts;
    parts.addTokens(addr.substring(1), "/", "");

    if (parts.size() < 5) return;
    if (parts[0] != "cs")  return;

    const auto rowStr = parts[1].toUpperCase();
    if (rowStr.isEmpty()) return;

    const auto rowChar = (juce::juce_wchar) rowStr[0];
    if (rowChar < 'A' || rowChar > 'Z') return;
    const int row = (int)(rowChar - 'A');

    if (parts[2].isEmpty() || ! parts[2].containsOnly("0123456789")) return;
    const int col = parts[2].getIntValue();
    if (col < 0 || col >= SeatEventSink::MAX_COLS) return;

    const auto param = parts[4].toLowerCase();

    if (msg.size() == 0)
    {
        // /off with no args
        if (param == "off")
            target.setOn(row, col, false);
        return;
    }

    auto firstAsFloat = [&]() -> float
    {
        if (msg[0].isFloat32()) return msg[0].getFloat32();
        if (msg[0].isInt32())   return (float) msg[0].getInt32();
        return 0.0f;
    };
    auto firstAsInt = [&]() -> int
    {
        if (msg[0].isInt32())   return msg[0].getInt32();
        if (msg[0].isFloat32()) return (int) msg[0].getFloat32();
        return 0;
    };

    if (param == "v")
    {
        float y = firstAsFloat();
        y = juce::jlimit(0.0f, 1.0f, y);
        target.setY(row, col, y);
    }
    else if (param == "line")
    {
        const float lineVal = firstAsFloat();   // expected 0..127
        const float xNorm   = juce::jlimit(0.0f, 1.0f, lineVal / 127.0f);
        target.setX(row, col, xNorm);
    }
    else if (param == "on")
    {
        target.setOn(row, col, firstAsInt() != 0);
    }
    else if (param == "off")
    {
        target.setOn(row, col, false);
    }
}
