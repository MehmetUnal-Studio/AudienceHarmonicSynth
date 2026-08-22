#include "OscFingerRouter.h"

#include <cmath>

void OscFingerRouter::pushX (int sourceId, int finger, float value) noexcept
{
    push(Event::X, sourceId, finger, value);
}

void OscFingerRouter::pushY (int sourceId, int finger, float value) noexcept
{
    push(Event::Y, sourceId, finger, value);
}

void OscFingerRouter::pushOn (int sourceId, int finger, bool on) noexcept
{
    push(on ? Event::On : Event::Off, sourceId, finger, on ? 1.0f : 0.0f);
}

void OscFingerRouter::push (Event::Type type, int sourceId, int finger, float value) noexcept
{
    if (sourceId < 0 || sourceId >= MAX_SOURCES
        || finger < 0 || finger >= MAX_FINGERS
        || ! std::isfinite(value))
        return;

    const juce::SpinLock::ScopedLockType lock(producerLock);
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 <= 0 && size2 <= 0)
    {
        droppedEvents.fetch_add(1, std::memory_order_relaxed);
        resetPending.store(true, std::memory_order_release);
        return;
    }

    const int slot = size1 > 0 ? start1 : start2;
    events[(size_t) slot] = { (juce::uint8) type,
                             (juce::uint8) finger,
                             (juce::uint16) sourceId,
                             juce::jlimit(0.0f, 1.0f, value) };
    fifo.finishedWrite(1);
}

int OscFingerRouter::drain (Event* destination, int maxEvents) noexcept
{
    if (destination == nullptr || maxEvents <= 0)
        return 0;

    const int available = juce::jmin(maxEvents, fifo.getNumReady());
    if (available <= 0)
        return 0;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToRead(available, start1, size1, start2, size2);

    int written = 0;
    for (int i = 0; i < size1; ++i)
        destination[written++] = events[(size_t) (start1 + i)];
    for (int i = 0; i < size2; ++i)
        destination[written++] = events[(size_t) (start2 + i)];

    fifo.finishedRead(size1 + size2);
    return written;
}

void OscFingerRouter::discardPendingEvents() noexcept
{
    const int available = fifo.getNumReady();
    if (available <= 0)
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToRead(available, start1, size1, start2, size2);
    fifo.finishedRead(size1 + size2);
}
