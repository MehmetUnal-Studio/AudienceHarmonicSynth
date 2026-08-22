#pragma once

#include <atomic>
#include <limits>
#include <memory>
#include <juce_core/juce_core.h>
#include <juce_osc/juce_osc.h>
#include "SeatEventSink.h"

/*
    OscBridge

    Owns a juce::OSCReceiver bound to a UDP port. Parses incoming
    /cs/<row>/<source>/finger<n>/{on|off|u|v|line} messages and pokes the
    seat target's atomic/event targets.

    Listener uses JUCE's realtime OSC receiver callback so standalone tests
    and hosts do not need a message-pump dependency. The callback is not the
    audio thread; fan-out is allocation-light and client removal is serialised
    with callbacks so stop()/destruction cannot race an in-flight delivery.
*/
class OscBridge
{
public:
    struct SharedPort;

    // Maximum number of in-process OscBridge clients that may share one UDP
    // port. Surfaced so callers/tests can reason about the cap (B25).
    static constexpr int MAX_SHARED_CLIENTS = 16;

    explicit OscBridge (SeatEventSink& target);
    ~OscBridge();

    bool start (int port);
    void stop();

    bool isRunning()    const noexcept { return running; }
    bool isReceiving()  const noexcept { return receiving; }
    int  getCurrentPort() const noexcept { return currentPort; }

    uint32_t getValidMessageCount() const noexcept
    {
        return validMessageCount.load(std::memory_order_relaxed);
    }

    uint32_t getObservedZoneMask() const noexcept
    {
        return observedZoneMask.load(std::memory_order_relaxed);
    }

    uint32_t getLastValidMessageAgeMs() const noexcept
    {
        const auto last = lastValidMessageMs.load(std::memory_order_acquire);
        return last == 0 ? std::numeric_limits<uint32_t>::max()
                         : juce::Time::getMillisecondCounter() - last;
    }

    // Human-readable status for the UI / debug panel. Mirrors the bool
    // returned by start(), and additionally surfaces the "port full"
    // condition when the shared-port client cap is reached (B25) instead of
    // the client being silently dropped.
    const juce::String& oscStatus() const noexcept { return statusString; }

private:
    void oscMessageReceived (const juce::OSCMessage& msg);

    SeatEventSink&    target;
    std::shared_ptr<SharedPort> sharedPort;
    int  currentPort = 0;
    bool running     = false;
    bool receiving   = false;
    juce::String statusString;
    std::atomic<uint32_t> validMessageCount { 0 };
    std::atomic<uint32_t> observedZoneMask { 0 };
    std::atomic<uint32_t> lastValidMessageMs { 0 };
};
