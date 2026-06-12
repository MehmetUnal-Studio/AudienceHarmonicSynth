#pragma once

#include <memory>
#include <juce_core/juce_core.h>
#include <juce_osc/juce_osc.h>
#include "SeatEventSink.h"

/*
    OscBridge

    Owns a juce::OSCReceiver bound to a UDP port. Parses incoming
    /cs/<row>/<col>/finger<n>/{on|line|v} messages and pokes the
    seat target's atomic/event targets.

    Listener uses JUCE's realtime OSC receiver callback so standalone tests
    and hosts do not need a message-pump dependency. The callback is not the
    audio thread; keep fan-out lock-free and parsing allocation-light.
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
    int  getCurrentPort() const noexcept { return currentPort; }

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
    juce::String statusString;
};
