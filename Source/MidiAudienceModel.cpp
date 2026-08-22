#include "MidiAudienceModel.h"

#include <cmath>

static_assert (MidiAudienceModel::MAX_SOURCES == SeatEventSink::MAX_OSC_SOURCES,
               "The audience model and OSC parser must share one source range");

MidiAudienceModel::MidiAudienceModel (OscFingerRouter& destination,
                                      MonotonicClock clock) noexcept
    : router(destination),
      monotonicClock(clock != nullptr ? clock
                                      : &MidiAudienceModel::systemMonotonicMilliseconds)
{
    for (auto& source : sources)
    {
        for (auto& value : source.fingerX)
            value.store(0.0f, std::memory_order_relaxed);
        for (auto& value : source.fingerY)
            value.store(0.0f, std::memory_order_relaxed);
        for (auto& value : source.liveActivityMs)
            value.store(0, std::memory_order_relaxed);
    }
}

void MidiAudienceModel::setX (int row, int sourceId, float xNorm) noexcept
{
    setFingerX(row, sourceId, 0, xNorm);
}

void MidiAudienceModel::setY (int row, int sourceId, float yNorm) noexcept
{
    setFingerY(row, sourceId, 0, yNorm);
}

void MidiAudienceModel::setOn (int row, int sourceId, bool on) noexcept
{
    setFingerOn(row, sourceId, 0, on);
}

void MidiAudienceModel::setFingerX (int, int sourceId, int finger, float xNorm) noexcept
{
    if (! validSource(sourceId) || ! validFinger(finger) || ! std::isfinite(xNorm))
        return;

    const juce::SpinLock::ScopedLockType lock(producerLock);
    setFingerXLocked(sourceId, finger, clampNormalized(xNorm));
}

void MidiAudienceModel::setFingerY (int, int sourceId, int finger, float yNorm) noexcept
{
    if (! validSource(sourceId) || ! validFinger(finger) || ! std::isfinite(yNorm))
        return;

    const juce::SpinLock::ScopedLockType lock(producerLock);
    setFingerYLocked(sourceId, finger, clampNormalized(yNorm));
}

void MidiAudienceModel::setFingerOn (int, int sourceId, int finger, bool on) noexcept
{
    if (! validSource(sourceId) || ! validFinger(finger))
        return;

    const juce::SpinLock::ScopedLockType lock(producerLock);
    setFingerOnLocked(sourceId, finger, on);
}

void MidiAudienceModel::setLiveFingerX (
    int, int sourceId, int finger, float xNorm) noexcept
{
    if (! validSource(sourceId) || ! validFinger(finger) || ! std::isfinite(xNorm))
        return;

    const auto bit = static_cast<std::uint16_t>(
        1u << static_cast<unsigned int>(finger));
    const juce::SpinLock::ScopedLockType lock(producerLock);
    auto& state = sources[(size_t) sourceId];
    const auto now = monotonicClock();
    state.lastLiveSourceActivityMs.store(now, std::memory_order_release);
    state.hasLiveSourceActivity.store(true, std::memory_order_release);
    if ((state.liveTrackedFingerMask.load(std::memory_order_acquire) & bit) != 0)
        state.liveActivityMs[(size_t) finger].store(now,
                                                     std::memory_order_release);
    setFingerXLocked(sourceId, finger, clampNormalized(xNorm));
}

void MidiAudienceModel::setLiveFingerY (
    int, int sourceId, int finger, float yNorm) noexcept
{
    if (! validSource(sourceId) || ! validFinger(finger) || ! std::isfinite(yNorm))
        return;

    const auto bit = static_cast<std::uint16_t>(
        1u << static_cast<unsigned int>(finger));
    const juce::SpinLock::ScopedLockType lock(producerLock);
    auto& state = sources[(size_t) sourceId];
    const auto now = monotonicClock();
    state.lastLiveSourceActivityMs.store(now, std::memory_order_release);
    state.hasLiveSourceActivity.store(true, std::memory_order_release);
    if ((state.liveTrackedFingerMask.load(std::memory_order_acquire) & bit) != 0)
        state.liveActivityMs[(size_t) finger].store(now,
                                                     std::memory_order_release);
    setFingerYLocked(sourceId, finger, clampNormalized(yNorm));
}

void MidiAudienceModel::setLiveFingerOn (
    int, int sourceId, int finger, bool on) noexcept
{
    if (! validSource(sourceId) || ! validFinger(finger))
        return;

    const auto bit = static_cast<std::uint16_t>(
        1u << static_cast<unsigned int>(finger));
    const juce::SpinLock::ScopedLockType lock(producerLock);
    auto& state = sources[(size_t) sourceId];
    const auto now = monotonicClock();
    state.lastLiveSourceActivityMs.store(now, std::memory_order_release);
    state.hasLiveSourceActivity.store(true, std::memory_order_release);

    if (on)
    {
        state.liveActivityMs[(size_t) finger].store(now,
                                                     std::memory_order_release);
        state.liveTrackedFingerMask.fetch_or(bit, std::memory_order_acq_rel);
    }

    // Tracking and canonical lifecycle publication share one critical section.
    // Panic, simulator reuse, and expiry therefore cannot interleave between a
    // live heartbeat and its matching state mutation/FIFO event.
    setFingerOnLocked(sourceId, finger, on);
}

void MidiAudienceModel::setFingerXLocked (
    int sourceId, int finger, float value) noexcept
{
    auto& source = sources[(size_t) sourceId];
    source.fingerX[(size_t) finger].store(value, std::memory_order_release);
    source.x.store(value, std::memory_order_release);
    if (forwardMotionEvents.load(std::memory_order_acquire))
        router.pushX(sourceId, finger, value);
}

void MidiAudienceModel::setFingerYLocked (
    int sourceId, int finger, float value) noexcept
{
    auto& source = sources[(size_t) sourceId];
    source.fingerY[(size_t) finger].store(value, std::memory_order_release);
    source.y.store(value, std::memory_order_release);
    if (forwardMotionEvents.load(std::memory_order_acquire))
        router.pushY(sourceId, finger, value);
}

void MidiAudienceModel::setFingerOnLocked (
    int sourceId, int finger, bool on) noexcept
{
    auto& state = sources[(size_t) sourceId];
    const auto bit = static_cast<std::uint16_t>(
        1u << static_cast<unsigned int>(finger));

    if (on)
    {
        const auto previous = state.activeFingerMask.fetch_or(bit,
                                                               std::memory_order_acq_rel);
        if ((previous & bit) == 0)
        {
            activeFingerCount.fetch_add(1, std::memory_order_relaxed);
            if (previous == 0)
                activeSourceCount.fetch_add(1, std::memory_order_relaxed);
        }

        lastActiveSourceId.store(sourceId, std::memory_order_release);
    }
    else
    {
        // Any explicit release, including one produced by the simulator,
        // removes live-network ownership. A later stationary simulator On with
        // the same source id must not inherit an old OSC watchdog deadline.
        state.liveTrackedFingerMask.fetch_and(
            static_cast<std::uint16_t>(~bit), std::memory_order_acq_rel);
        state.liveActivityMs[(size_t) finger].store(0, std::memory_order_release);
        const auto inverse = static_cast<std::uint16_t>(~bit);
        const auto previous = state.activeFingerMask.fetch_and(inverse,
                                                                std::memory_order_acq_rel);
        if ((previous & bit) != 0)
        {
            activeFingerCount.fetch_sub(1, std::memory_order_relaxed);
            if (previous == bit)
                activeSourceCount.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    // Lifecycle duplicates are forwarded intentionally. The downstream MIDI
    // state machine owns retrigger/note-off idempotence and must observe the
    // OSC stream exactly as received.
    router.pushOn(sourceId, finger, on);
}

int MidiAudienceModel::expireStaleLiveTouches (std::uint32_t timeoutMs) noexcept
{
    if (timeoutMs == 0)
        return 0;

    // Modular unsigned subtraction is wrap-safe when the timeout is below half
    // the uint32 range. Clamp hostile callers to preserve that ordering rule.
    timeoutMs = juce::jmin(timeoutMs, std::uint32_t { 0x7fffffffu });
    const std::uint32_t now = monotonicClock();
    int expiredTotal = 0;

    const juce::SpinLock::ScopedLockType lock(producerLock);
    for (int sourceId = 0; sourceId < MAX_SOURCES; ++sourceId)
    {
        auto& state = sources[(size_t) sourceId];
        const auto active = state.activeFingerMask.load(std::memory_order_acquire);
        const auto tracked = state.liveTrackedFingerMask.load(std::memory_order_acquire);
        const auto candidates = static_cast<std::uint16_t>(active & tracked);
        if (candidates == 0)
            continue;

        std::uint16_t expiredMask = 0;
        for (int finger = 0; finger < MAX_FINGERS; ++finger)
        {
            const auto bit = static_cast<std::uint16_t>(
                1u << static_cast<unsigned int>(finger));
            if ((candidates & bit) == 0)
                continue;

            const auto last = state.liveActivityMs[(size_t) finger].load(
                std::memory_order_acquire);
            if (static_cast<std::uint32_t>(now - last) >= timeoutMs)
                expiredMask = static_cast<std::uint16_t>(expiredMask | bit);
        }

        if (expiredMask == 0)
            continue;

        const auto remaining = static_cast<std::uint16_t>(active & ~expiredMask);
        state.activeFingerMask.store(remaining, std::memory_order_release);
        state.liveTrackedFingerMask.store(
            static_cast<std::uint16_t>(tracked & ~expiredMask),
            std::memory_order_release);

        const int expiredHere = countSetBits(expiredMask);
        expiredTotal += expiredHere;
        activeFingerCount.store(
            juce::jmax(0, activeFingerCount.load(std::memory_order_relaxed)
                             - expiredHere),
            std::memory_order_release);
        if (active != 0 && remaining == 0)
            activeSourceCount.store(
                juce::jmax(0, activeSourceCount.load(std::memory_order_relaxed) - 1),
                std::memory_order_release);

        // Publish under the same producer lock as the canonical mutation. This
        // keeps expiry Off and any concurrent fresh On in one total FIFO order.
        for (int finger = 0; finger < MAX_FINGERS; ++finger)
        {
            const auto bit = static_cast<std::uint16_t>(
                1u << static_cast<unsigned int>(finger));
            if ((expiredMask & bit) == 0)
                continue;

            state.liveActivityMs[(size_t) finger].store(0,
                                                         std::memory_order_release);
            router.pushOn(sourceId, finger, false);
        }
    }

    return expiredTotal;
}

void MidiAudienceModel::clear() noexcept
{
    const juce::SpinLock::ScopedLockType lock(producerLock);
    for (auto& source : sources)
    {
        source.x.store(0.0f, std::memory_order_relaxed);
        source.y.store(0.0f, std::memory_order_relaxed);
        for (auto& value : source.fingerX)
            value.store(0.0f, std::memory_order_relaxed);
        for (auto& value : source.fingerY)
            value.store(0.0f, std::memory_order_relaxed);
        for (auto& value : source.liveActivityMs)
            value.store(0, std::memory_order_relaxed);
        source.lastLiveSourceActivityMs.store(0, std::memory_order_relaxed);
        source.hasLiveSourceActivity.store(false, std::memory_order_release);
        source.activeFingerMask.store(0, std::memory_order_release);
        source.liveTrackedFingerMask.store(0, std::memory_order_release);
    }

    activeSourceCount.store(0, std::memory_order_release);
    activeFingerCount.store(0, std::memory_order_release);
    lastActiveSourceId.store(-1, std::memory_order_release);
    router.requestReset();
}

MidiAudienceModel::SourceSnapshot MidiAudienceModel::getSourceSnapshot (int sourceId) const noexcept
{
    SourceSnapshot result;
    result.sourceId = sourceId;
    result.midiChannel = midiChannelForSourceId(sourceId);

    if (! validSource(sourceId))
        return result;

    const auto& state = sources[(size_t) sourceId];
    result.x = state.x.load(std::memory_order_acquire);
    result.y = state.y.load(std::memory_order_acquire);
    result.activeFingerMask = state.activeFingerMask.load(std::memory_order_acquire);
    result.activeFingerCount = countSetBits(result.activeFingerMask);
    result.active = result.activeFingerMask != 0;
    return result;
}

MidiAudienceModel::FingerSnapshot MidiAudienceModel::getFingerSnapshot (int sourceId,
                                                                         int finger) const noexcept
{
    FingerSnapshot result;
    if (! validSource(sourceId) || ! validFinger(finger))
        return result;

    const auto& state = sources[(size_t) sourceId];
    result.x = state.fingerX[(size_t) finger].load(std::memory_order_acquire);
    result.y = state.fingerY[(size_t) finger].load(std::memory_order_acquire);
    const auto mask = state.activeFingerMask.load(std::memory_order_acquire);
    result.active = (mask & static_cast<std::uint16_t>(1u << static_cast<unsigned int>(finger))) != 0;
    return result;
}

int MidiAudienceModel::getActiveSourceCount() const noexcept
{
    return activeSourceCount.load(std::memory_order_acquire);
}

int MidiAudienceModel::getActiveFingerCount() const noexcept
{
    return activeFingerCount.load(std::memory_order_acquire);
}

int MidiAudienceModel::getLastActiveSourceId() const noexcept
{
    return lastActiveSourceId.load(std::memory_order_acquire);
}

int MidiAudienceModel::getRecentLiveSourceCount (std::uint32_t windowMs) const noexcept
{
    if (windowMs == 0)
        return 0;

    windowMs = juce::jmin(windowMs, std::uint32_t { 0x7fffffffu });
    const auto now = monotonicClock();
    int count = 0;
    for (const auto& source : sources)
    {
        if (! source.hasLiveSourceActivity.load(std::memory_order_acquire))
            continue;

        const auto last = source.lastLiveSourceActivityMs.load(
            std::memory_order_acquire);
        if (static_cast<std::uint32_t>(now - last) < windowMs)
            ++count;
    }
    return count;
}

void MidiAudienceModel::setMotionEventForwardingEnabled (bool enabled) noexcept
{
    if (forwardMotionEvents.load(std::memory_order_acquire) == enabled)
        return;

    if (forwardMotionEvents.exchange(enabled, std::memory_order_acq_rel) != enabled)
        router.requestReset();
}

int MidiAudienceModel::midiChannelForSourceId (int sourceId) noexcept
{
    if (! validSource(sourceId))
        return 0;

    constexpr int channelCount = 16;
    const int remainder = sourceId % channelCount;
    return remainder <= 0 ? remainder + channelCount : remainder;
}

bool MidiAudienceModel::validSource (int sourceId) noexcept
{
    return sourceId >= 0 && sourceId < MAX_SOURCES;
}

bool MidiAudienceModel::validFinger (int finger) noexcept
{
    return finger >= 0 && finger < MAX_FINGERS;
}

float MidiAudienceModel::clampNormalized (float value) noexcept
{
    if (value < 0.0f)
        return 0.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
}

int MidiAudienceModel::countSetBits (std::uint16_t bits) noexcept
{
    int count = 0;
    while (bits != 0)
    {
        bits = static_cast<std::uint16_t>(bits & static_cast<std::uint16_t>(bits - 1));
        ++count;
    }
    return count;
}

std::uint32_t MidiAudienceModel::systemMonotonicMilliseconds() noexcept
{
    return juce::Time::getMillisecondCounter();
}
