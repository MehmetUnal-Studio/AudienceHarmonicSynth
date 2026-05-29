#include "OscBridge.h"
#include <algorithm>
#include <array>
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

    void addClient (OscBridge& bridge)
    {
        for (auto& client : clients)
            if (client.load(std::memory_order_acquire) == &bridge)
                return;

        for (auto& client : clients)
        {
            OscBridge* empty = nullptr;
            if (client.compare_exchange_strong(empty, &bridge, std::memory_order_release, std::memory_order_relaxed))
                return;
        }
    }

    void removeClient (OscBridge& bridge)
    {
        for (auto& client : clients)
        {
            OscBridge* expected = &bridge;
            client.compare_exchange_strong(expected, nullptr, std::memory_order_release, std::memory_order_relaxed);
        }
    }

private:
    void oscMessageReceived (const juce::OSCMessage& msg) override
    {
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
    std::array<std::atomic<OscBridge*>, 16> clients {};
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
    const char* p = addr.toRawUTF8();

    auto lower = [] (char c) noexcept -> char
    {
        return (c >= 'A' && c <= 'Z') ? (char) (c + ('a' - 'A')) : c;
    };

    auto matches = [lower] (const char* text, const char* token) noexcept
    {
        while (*text != 0 && *token != 0)
        {
            if (lower(*text++) != lower(*token++))
                return false;
        }

        return *text == 0 && *token == 0;
    };

    if (p == nullptr || p[0] != '/' || lower(p[1]) != 'c' || lower(p[2]) != 's' || p[3] != '/')
        return;
    p += 4;

    char rowChar = *p;
    if (rowChar >= 'a' && rowChar <= 'z')
        rowChar = (char) (rowChar - ('a' - 'A'));
    if (rowChar < 'A' || rowChar > 'Z') return;
    const int row = (int)(rowChar - 'A');

    while (*p != 0 && *p != '/')
        ++p;
    if (*p != '/') return;
    ++p;

    bool hasCol = false;
    int col = 0;
    while (*p >= '0' && *p <= '9')
    {
        hasCol = true;
        col = col * 10 + (*p - '0');
        ++p;
    }

    if (! hasCol || *p != '/') return;
    if (col < 0 || col >= SeatEventSink::MAX_COLS) return;
    ++p;

    while (*p != 0 && *p != '/')
        ++p;
    if (*p != '/') return;
    const char* param = p + 1;

    if (msg.size() == 0)
    {
        // /off with no args
        if (matches(param, "off"))
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

    if (matches(param, "v"))
    {
        float y = firstAsFloat();
        y = juce::jlimit(0.0f, 1.0f, y);
        target.setY(row, col, y);
    }
    else if (matches(param, "line"))
    {
        const float lineVal = firstAsFloat();   // expected 0..127
        const float xNorm   = juce::jlimit(0.0f, 1.0f, lineVal / 127.0f);
        target.setX(row, col, xNorm);
    }
    else if (matches(param, "on"))
    {
        target.setOn(row, col, firstAsInt() != 0);
    }
    else if (matches(param, "off"))
    {
        target.setOn(row, col, false);
    }
}
