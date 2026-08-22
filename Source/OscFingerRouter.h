#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <juce_core/juce_core.h>

// Fixed-capacity hand-off from OSC/control threads to the audio thread.
// Writers are serialised without involving the audio thread; the consumer never
// locks, allocates, or performs I/O.
class OscFingerRouter
{
public:
    static constexpr int MAX_SOURCES = 256;
    static constexpr int MAX_FINGERS = 10;
    static constexpr int MAX_VOICES = MAX_SOURCES * MAX_FINGERS;
    static constexpr int EVENT_QUEUE_SIZE = 8192;

    struct Event
    {
        enum Type : juce::uint8 { X, Y, On, Off };

        juce::uint8 type = X;
        juce::uint8 finger = 0;
        juce::uint16 sourceId = 0;
        float value = 0.0f;
    };

    void pushX (int sourceId, int finger, float value) noexcept;
    void pushY (int sourceId, int finger, float value) noexcept;
    void pushOn (int sourceId, int finger, bool on) noexcept;

    // Message/control-thread request. The audio thread gives this priority over
    // queued data, discards stale packets and emits a safety reset.
    void requestReset() noexcept { resetPending.store(true, std::memory_order_release); }
    bool takeResetRequest() noexcept { return resetPending.exchange(false, std::memory_order_acq_rel); }

    // Audio-thread only.
    int drain (Event* destination, int maxEvents) noexcept;
    void discardPendingEvents() noexcept;

    uint32_t getDroppedEventCount() const noexcept
    {
        return droppedEvents.load(std::memory_order_relaxed);
    }

private:
    void push (Event::Type type, int sourceId, int finger, float value) noexcept;

    juce::SpinLock producerLock;
    juce::AbstractFifo fifo { EVENT_QUEUE_SIZE };
    std::array<Event, EVENT_QUEUE_SIZE> events {};
    std::atomic<bool> resetPending { false };
    std::atomic<uint32_t> droppedEvents { 0 };
};
