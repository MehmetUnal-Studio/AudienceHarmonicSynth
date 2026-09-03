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
        enum Type : juce::uint8 { X, Y, On, Off, Cancel };

        juce::uint8 type = X;
        juce::uint8 finger = 0;
        juce::uint16 sourceId = 0;
        float value = 0.0f;
    };

    OscFingerRouter() noexcept;

    void pushX (int sourceId, int finger, float value) noexcept;
    void pushY (int sourceId, int finger, float value) noexcept;
    void pushOn (int sourceId, int finger, bool on) noexcept;
    // Watchdog/disconnect release. Unlike an ordinary musical Off, this tells
    // the downstream duration scheduler to cancel only this semantic voice's
    // outstanding tails immediately.
    void pushCancel (int sourceId, int finger) noexcept;

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

    uint32_t getDroppedMotionEventCount() const noexcept
    {
        return droppedMotionEvents.load(std::memory_order_relaxed);
    }

    uint32_t getDroppedLifecycleEventCount() const noexcept
    {
        return droppedLifecycleEvents.load(std::memory_order_relaxed);
    }

    uint32_t getCoalescedMotionEventCount() const noexcept
    {
        return coalescedMotionEvents.load(std::memory_order_relaxed);
    }

    int getLifecycleQueueDepth() const noexcept { return lifecycleFifo.getNumReady(); }
    int getMotionQueueDepth() const noexcept { return motionFifo.getNumReady(); }
    uint32_t getLifecycleHighWater() const noexcept
    {
        return lifecycleHighWater.load(std::memory_order_relaxed);
    }
    uint32_t getMotionHighWater() const noexcept
    {
        return motionHighWater.load(std::memory_order_relaxed);
    }

    void setMotionUpdateDivisor (int divisor) noexcept
    {
        motionUpdateDivisor.store(juce::jlimit(1, 8, divisor),
                                  std::memory_order_relaxed);
    }

private:
    struct QueuedEvent
    {
        Event event;
        uint32_t epoch = 1;
    };

    struct MotionState
    {
        std::atomic<float> latestX { 0.0f };
        std::atomic<float> latestY { 0.0f };
        std::atomic<uint32_t> queuedXEpoch { 0 };
        std::atomic<uint32_t> queuedYEpoch { 0 };
        std::atomic<uint32_t> publishedEpoch { 1 };

        // Producer-thread state. All writers are serialised by producerLock;
        // the audio consumer never reads these members.
        uint32_t producerEpoch = 1;
        bool xDirtySinceLifecycle = false;
        bool yDirtySinceLifecycle = false;
    };

    static int voiceIndex (int sourceId, int finger) noexcept;
    static uint32_t nextEpoch (uint32_t current) noexcept;
    static void incrementSaturating (std::atomic<uint32_t>& counter,
                                     uint32_t amount = 1) noexcept;
    static void updateHighWater (std::atomic<uint32_t>& highWater,
                                 int depth) noexcept;

    void pushMotion (Event::Type type, int sourceId, int finger, float value) noexcept;
    void pushLifecycle (int sourceId, int finger,
                        Event::Type lifecycleType) noexcept;
    bool enqueueLifecycleGroup (const QueuedEvent* group, int count) noexcept;
    bool enqueueMotionMarker (Event::Type type, int sourceId, int finger,
                              uint32_t epoch) noexcept;

    juce::SpinLock producerLock;

    // Lifecycle traffic has its own queue and is always drained before motion.
    // This prevents a crowd-sized U/V flood from starving note-off messages.
    juce::AbstractFifo lifecycleFifo { EVENT_QUEUE_SIZE };
    std::array<QueuedEvent, EVENT_QUEUE_SIZE> lifecycleEvents {};

    // Motion is represented by one latest-value marker per source/finger/axis
    // and lifecycle epoch. Repeated packets update atomics rather than growing
    // this queue without bound.
    juce::AbstractFifo motionFifo { EVENT_QUEUE_SIZE };
    std::array<QueuedEvent, EVENT_QUEUE_SIZE> motionEvents {};
    std::array<MotionState, MAX_VOICES> motionStates {};

    // Audio-thread-only epoch view used to reject stale motion markers which
    // belonged to a touch before its most recent On/Off boundary.
    std::array<uint32_t, MAX_VOICES> consumerEpochs {};

    std::atomic<bool> resetPending { false };
    std::atomic<uint32_t> droppedEvents { 0 };
    std::atomic<uint32_t> droppedMotionEvents { 0 };
    std::atomic<uint32_t> droppedLifecycleEvents { 0 };
    std::atomic<uint32_t> coalescedMotionEvents { 0 };
    std::atomic<uint32_t> lifecycleHighWater { 0 };
    std::atomic<uint32_t> motionHighWater { 0 };
    std::atomic<int> motionUpdateDivisor { 1 };
    std::atomic<uint32_t> motionIngressCounter { 0 };
};
