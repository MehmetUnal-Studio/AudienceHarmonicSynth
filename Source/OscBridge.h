#pragma once

#include <memory>
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

    explicit OscBridge (SeatEventSink& target);
    ~OscBridge();

    bool start (int port);
    void stop();

    bool isRunning()    const noexcept { return running; }
    int  getCurrentPort() const noexcept { return currentPort; }

private:
    void oscMessageReceived (const juce::OSCMessage& msg);

    SeatEventSink&    target;
    std::shared_ptr<SharedPort> sharedPort;
    int  currentPort = 0;
    bool running     = false;
};
