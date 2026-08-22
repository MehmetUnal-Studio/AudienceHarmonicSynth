#include "MidiAudienceModel.h"

#include <cmath>

static_assert (MidiAudienceModel::MAX_SOURCES == SeatEventSink::MAX_OSC_SOURCES,
               "The audience model and OSC parser must share one source range");

MidiAudienceModel::MidiAudienceModel (OscFingerRouter& destination) noexcept
    : router(destination)
{
    for (auto& source : sources)
    {
        for (auto& value : source.fingerX)
            value.store(0.0f, std::memory_order_relaxed);
        for (auto& value : source.fingerY)
            value.store(0.0f, std::memory_order_relaxed);
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
    const float value = clampNormalized(xNorm);
    auto& source = sources[(size_t) sourceId];
    source.fingerX[(size_t) finger].store(value, std::memory_order_release);
    source.x.store(value, std::memory_order_release);
    if (forwardMotionEvents.load(std::memory_order_acquire))
        router.pushX(sourceId, finger, value);
}

void MidiAudienceModel::setFingerY (int, int sourceId, int finger, float yNorm) noexcept
{
    if (! validSource(sourceId) || ! validFinger(finger) || ! std::isfinite(yNorm))
        return;

    const juce::SpinLock::ScopedLockType lock(producerLock);
    const float value = clampNormalized(yNorm);
    auto& source = sources[(size_t) sourceId];
    source.fingerY[(size_t) finger].store(value, std::memory_order_release);
    source.y.store(value, std::memory_order_release);
    if (forwardMotionEvents.load(std::memory_order_acquire))
        router.pushY(sourceId, finger, value);
}

void MidiAudienceModel::setFingerOn (int, int sourceId, int finger, bool on) noexcept
{
    if (! validSource(sourceId) || ! validFinger(finger))
        return;

    const juce::SpinLock::ScopedLockType lock(producerLock);
    auto& state = sources[(size_t) sourceId];
    const auto bit = static_cast<std::uint16_t>(1u << static_cast<unsigned int>(finger));

    if (on)
    {
        const auto previous = state.activeFingerMask.fetch_or(bit, std::memory_order_acq_rel);
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
        const auto inverse = static_cast<std::uint16_t>(~bit);
        const auto previous = state.activeFingerMask.fetch_and(inverse, std::memory_order_acq_rel);
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
        source.activeFingerMask.store(0, std::memory_order_release);
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
