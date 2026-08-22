#include "OscFingerRouter.h"

#include <cmath>
#include <limits>

OscFingerRouter::OscFingerRouter() noexcept
{
    consumerEpochs.fill(1);
}

void OscFingerRouter::pushX (int sourceId, int finger, float value) noexcept
{
    pushMotion(Event::X, sourceId, finger, value);
}

void OscFingerRouter::pushY (int sourceId, int finger, float value) noexcept
{
    pushMotion(Event::Y, sourceId, finger, value);
}

void OscFingerRouter::pushOn (int sourceId, int finger, bool on) noexcept
{
    pushLifecycle(sourceId, finger, on);
}

int OscFingerRouter::voiceIndex (int sourceId, int finger) noexcept
{
    return sourceId * MAX_FINGERS + finger;
}

uint32_t OscFingerRouter::nextEpoch (uint32_t current) noexcept
{
    ++current;
    return current == 0 ? 1 : current;
}

void OscFingerRouter::incrementSaturating (std::atomic<uint32_t>& counter,
                                           uint32_t amount) noexcept
{
    auto current = counter.load(std::memory_order_relaxed);
    while (current != std::numeric_limits<uint32_t>::max())
    {
        const auto remaining = std::numeric_limits<uint32_t>::max() - current;
        const auto desired = current + juce::jmin(amount, remaining);
        if (counter.compare_exchange_weak(current, desired,
                                          std::memory_order_relaxed,
                                          std::memory_order_relaxed))
            return;
    }
}

void OscFingerRouter::updateHighWater (std::atomic<uint32_t>& highWater,
                                        int depth) noexcept
{
    const auto bounded = (uint32_t) juce::jlimit(0, EVENT_QUEUE_SIZE, depth);
    auto current = highWater.load(std::memory_order_relaxed);
    while (bounded > current
           && ! highWater.compare_exchange_weak(current, bounded,
                                                std::memory_order_relaxed,
                                                std::memory_order_relaxed))
    {
    }
}

void OscFingerRouter::pushMotion (Event::Type type, int sourceId, int finger,
                                  float value) noexcept
{
    if (sourceId < 0 || sourceId >= MAX_SOURCES
        || finger < 0 || finger >= MAX_FINGERS
        || (type != Event::X && type != Event::Y)
        || ! std::isfinite(value))
        return;

    const juce::SpinLock::ScopedLockType lock(producerLock);
    const auto clamped = juce::jlimit(0.0f, 1.0f, value);
    auto& state = motionStates[(size_t) voiceIndex(sourceId, finger)];
    auto& latest = type == Event::X ? state.latestX : state.latestY;
    auto& queuedEpoch = type == Event::X ? state.queuedXEpoch : state.queuedYEpoch;

    // Publish the value before inspecting the dirty token. If the consumer
    // clears the token concurrently, either it observes this value or this
    // producer creates a new marker; the latest update cannot disappear.
    latest.store(clamped, std::memory_order_release);
    if (type == Event::X)
        state.xDirtySinceLifecycle = true;
    else
        state.yDirtySinceLifecycle = true;

    const auto sample = motionIngressCounter.fetch_add(1, std::memory_order_relaxed);
    const int divisor = motionUpdateDivisor.load(std::memory_order_relaxed);
    if (divisor > 1 && sample % (uint32_t) divisor != 0u)
    {
        incrementSaturating(coalescedMotionEvents);
        return;
    }

    const auto epoch = state.producerEpoch;
    const auto previousEpoch = queuedEpoch.exchange(epoch, std::memory_order_acq_rel);
    if (previousEpoch == epoch)
    {
        incrementSaturating(coalescedMotionEvents);
        return;
    }

    if (! enqueueMotionMarker(type, sourceId, finger, epoch))
    {
        auto expected = epoch;
        queuedEpoch.compare_exchange_strong(expected, 0,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire);
        incrementSaturating(droppedEvents);
    }
}

void OscFingerRouter::pushLifecycle (int sourceId, int finger, bool on) noexcept
{
    if (sourceId < 0 || sourceId >= MAX_SOURCES
        || finger < 0 || finger >= MAX_FINGERS)
        return;

    const juce::SpinLock::ScopedLockType lock(producerLock);
    auto& state = motionStates[(size_t) voiceIndex(sourceId, finger)];
    const auto epoch = nextEpoch(state.producerEpoch);
    state.producerEpoch = epoch;
    state.publishedEpoch.store(epoch, std::memory_order_release);

    std::array<QueuedEvent, 3> group {};
    int count = 0;

    // Any U/V received since the preceding lifecycle boundary is copied into
    // the priority queue immediately before On. This preserves the production
    // bundle contract (U, V, On) even when the ordinary motion markers are
    // heavily coalesced or waiting behind a flood.
    if (on && state.xDirtySinceLifecycle)
        group[(size_t) count++] = { { Event::X, (juce::uint8) finger,
                                      (juce::uint16) sourceId,
                                      state.latestX.load(std::memory_order_acquire) },
                                    epoch };
    if (on && state.yDirtySinceLifecycle)
        group[(size_t) count++] = { { Event::Y, (juce::uint8) finger,
                                      (juce::uint16) sourceId,
                                      state.latestY.load(std::memory_order_acquire) },
                                    epoch };

    group[(size_t) count++] = { { (juce::uint8) (on ? Event::On : Event::Off),
                                  (juce::uint8) finger,
                                  (juce::uint16) sourceId,
                                  on ? 1.0f : 0.0f },
                                epoch };

    if (! enqueueLifecycleGroup(group.data(), count))
    {
        incrementSaturating(droppedEvents, (uint32_t) count);
        // The canonical MidiAudienceModel is updated before it calls us. A
        // reset therefore recovers the exact held/released state without
        // allowing a dropped lifecycle to leave a stuck note.
        resetPending.store(true, std::memory_order_release);
    }

    if (on)
    {
        state.xDirtySinceLifecycle = false;
        state.yDirtySinceLifecycle = false;
    }
}

bool OscFingerRouter::enqueueLifecycleGroup (const QueuedEvent* group, int count) noexcept
{
    if (group == nullptr || count <= 0 || lifecycleFifo.getFreeSpace() < count)
        return false;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    lifecycleFifo.prepareToWrite(count, start1, size1, start2, size2);
    if (size1 + size2 != count)
        return false;

    int copied = 0;
    for (int i = 0; i < size1; ++i)
        lifecycleEvents[(size_t) (start1 + i)] = group[(size_t) copied++];
    for (int i = 0; i < size2; ++i)
        lifecycleEvents[(size_t) (start2 + i)] = group[(size_t) copied++];

    lifecycleFifo.finishedWrite(count);
    updateHighWater(lifecycleHighWater, lifecycleFifo.getNumReady());
    return true;
}

bool OscFingerRouter::enqueueMotionMarker (Event::Type type, int sourceId, int finger,
                                           uint32_t epoch) noexcept
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    motionFifo.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 + size2 != 1)
        return false;

    const int slot = size1 > 0 ? start1 : start2;
    motionEvents[(size_t) slot] = { { (juce::uint8) type,
                                      (juce::uint8) finger,
                                      (juce::uint16) sourceId,
                                      0.0f },
                                    epoch };
    motionFifo.finishedWrite(1);
    updateHighWater(motionHighWater, motionFifo.getNumReady());
    return true;
}

int OscFingerRouter::drain (Event* destination, int maxEvents) noexcept
{
    if (destination == nullptr || maxEvents <= 0)
        return 0;

    int written = 0;

    // Lifecycle first: note release can never sit behind a U/V flood.
    {
        const int available = juce::jmin(maxEvents, lifecycleFifo.getNumReady());
        if (available > 0)
        {
            int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
            lifecycleFifo.prepareToRead(available, start1, size1, start2, size2);

            auto copyLifecycle = [&] (const QueuedEvent& queued) noexcept
            {
                const auto type = (Event::Type) queued.event.type;
                if (type == Event::On || type == Event::Off)
                {
                    const int voice = voiceIndex((int) queued.event.sourceId,
                                                 (int) queued.event.finger);
                    consumerEpochs[(size_t) voice] = queued.epoch;
                }
                destination[written++] = queued.event;
            };

            for (int i = 0; i < size1; ++i)
                copyLifecycle(lifecycleEvents[(size_t) (start1 + i)]);
            for (int i = 0; i < size2; ++i)
                copyLifecycle(lifecycleEvents[(size_t) (start2 + i)]);

            lifecycleFifo.finishedRead(size1 + size2);
        }
    }

    // Inspect at most the remaining public event budget. Stale markers are
    // consumed without turning this bounded audio-thread operation into a scan
    // over an attacker-controlled backlog.
    const int markerBudget = maxEvents - written;
    const int markersAvailable = juce::jmin(markerBudget, motionFifo.getNumReady());
    if (markersAvailable > 0)
    {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        motionFifo.prepareToRead(markersAvailable, start1, size1, start2, size2);

        auto consumeMotion = [&] (const QueuedEvent& marker) noexcept
        {
            const auto type = (Event::Type) marker.event.type;
            const int sourceId = (int) marker.event.sourceId;
            const int finger = (int) marker.event.finger;
            if ((type != Event::X && type != Event::Y)
                || sourceId < 0 || sourceId >= MAX_SOURCES
                || finger < 0 || finger >= MAX_FINGERS)
                return;

            const int voice = voiceIndex(sourceId, finger);
            auto& state = motionStates[(size_t) voice];
            auto& queuedEpoch = type == Event::X ? state.queuedXEpoch : state.queuedYEpoch;
            auto expected = marker.epoch;
            if (! queuedEpoch.compare_exchange_strong(expected, 0,
                                                       std::memory_order_acq_rel,
                                                       std::memory_order_acquire))
                return; // superseded by a marker from a newer lifecycle epoch

            if (consumerEpochs[(size_t) voice] != marker.epoch)
            {
                // A lifecycle can be published concurrently just after the
                // priority queue was inspected. Ask the next callback to
                // rehydrate rather than applying post-lifecycle motion early.
                if (state.publishedEpoch.load(std::memory_order_acquire) == marker.epoch)
                    resetPending.store(true, std::memory_order_release);
                return;
            }

            const float value = (type == Event::X ? state.latestX : state.latestY)
                                    .load(std::memory_order_acquire);
            if (! std::isfinite(value))
                return;

            destination[written++] = { (juce::uint8) type,
                                       (juce::uint8) finger,
                                       (juce::uint16) sourceId,
                                       juce::jlimit(0.0f, 1.0f, value) };
        };

        for (int i = 0; i < size1; ++i)
            consumeMotion(motionEvents[(size_t) (start1 + i)]);
        for (int i = 0; i < size2; ++i)
            consumeMotion(motionEvents[(size_t) (start2 + i)]);

        motionFifo.finishedRead(size1 + size2);
    }

    return written;
}

void OscFingerRouter::discardPendingEvents() noexcept
{
    {
        const int available = lifecycleFifo.getNumReady();
        if (available > 0)
        {
            int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
            lifecycleFifo.prepareToRead(available, start1, size1, start2, size2);
            lifecycleFifo.finishedRead(size1 + size2);
        }
    }

    {
        const int available = motionFifo.getNumReady();
        if (available > 0)
        {
            int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
            motionFifo.prepareToRead(available, start1, size1, start2, size2);

            auto clearMarker = [&] (const QueuedEvent& marker) noexcept
            {
                const auto type = (Event::Type) marker.event.type;
                const int sourceId = (int) marker.event.sourceId;
                const int finger = (int) marker.event.finger;
                if ((type != Event::X && type != Event::Y)
                    || sourceId < 0 || sourceId >= MAX_SOURCES
                    || finger < 0 || finger >= MAX_FINGERS)
                    return;

                auto& state = motionStates[(size_t) voiceIndex(sourceId, finger)];
                auto& queuedEpoch = type == Event::X ? state.queuedXEpoch : state.queuedYEpoch;
                auto expected = marker.epoch;
                queuedEpoch.compare_exchange_strong(expected, 0,
                                                     std::memory_order_acq_rel,
                                                     std::memory_order_acquire);
            };

            for (int i = 0; i < size1; ++i)
                clearMarker(motionEvents[(size_t) (start1 + i)]);
            for (int i = 0; i < size2; ++i)
                clearMarker(motionEvents[(size_t) (start2 + i)]);

            motionFifo.finishedRead(size1 + size2);
        }
    }

    // The owning processor rehydrates canonical values immediately after this
    // call. Align epoch filtering with the same latest producer boundary so
    // subsequent held-touch motion remains admissible without another On.
    for (size_t voice = 0; voice < motionStates.size(); ++voice)
        consumerEpochs[voice] = motionStates[voice].publishedEpoch.load(std::memory_order_acquire);
}
