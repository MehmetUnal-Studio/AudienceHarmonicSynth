#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "OscFingerRouter.h"
#include "SeatEventSink.h"

class SourceQualityController;

// MIDI-only audience state shared by the OSC/control and UI threads.
//
// The incoming OSC row/zone is intentionally not part of source identity: a
// plugin instance already represents one zone/UDP port, so sourceId alone owns
// the MIDI channel and all of its finger lifecycles.
class MidiAudienceModel final : public SeatEventSink
{
public:
    static constexpr int MAX_SOURCES = OscFingerRouter::MAX_SOURCES;
    static constexpr int MAX_FINGERS = OscFingerRouter::MAX_FINGERS;
    static constexpr std::uint32_t liveTouchTimeoutMs = 3000;
    static constexpr std::uint32_t recentSourceWindowMs = 8000;
    using MonotonicClock = std::uint32_t (*)() noexcept;

    struct SourceSnapshot
    {
        int sourceId = -1;
        float x = 0.0f;
        float y = 0.0f;
        std::uint16_t activeFingerMask = 0;
        int activeFingerCount = 0;
        int midiChannel = 0;
        bool active = false;
    };

    struct FingerSnapshot
    {
        float x = 0.0f;
        float y = 0.0f;
        bool active = false;
    };

    explicit MidiAudienceModel (OscFingerRouter& destination,
                                MonotonicClock clock = nullptr,
                                SourceQualityController* quality = nullptr) noexcept;

    void setX  (int row, int sourceId, float xNorm) noexcept override;
    void setY  (int row, int sourceId, float yNorm) noexcept override;
    void setOn (int row, int sourceId, bool on) noexcept override;

    void setFingerX  (int row, int sourceId, int finger, float xNorm) noexcept override;
    void setFingerY  (int row, int sourceId, int finger, float yNorm) noexcept override;
    void setFingerOn (int row, int sourceId, int finger, bool on) noexcept override;
    void setLiveFingerX  (int row, int sourceId, int finger, float xNorm) noexcept override;
    void setLiveFingerY  (int row, int sourceId, int finger, float yNorm) noexcept override;
    void setLiveFingerOn (int row, int sourceId, int finger, bool on) noexcept override;

    // Message/control-thread watchdog. Live OSC On arms a touch; U/V/Line only
    // refresh an already-armed touch. Stationary simulator voices never arm and
    // therefore never expire. Returns the number of ordered synthetic Cancel
    // events published during this bounded pass.
    int expireStaleLiveTouches (
        std::uint32_t timeoutMs = liveTouchTimeoutMs) noexcept;

    // Clears UI/control state immediately. The queued MIDI branch is reset by
    // its consumer so stale note events cannot survive this operation.
    void clear() noexcept;

    SourceSnapshot getSourceSnapshot (int sourceId) const noexcept;
    FingerSnapshot getFingerSnapshot (int sourceId, int finger) const noexcept;
    int getActiveSourceCount() const noexcept;
    int getActiveFingerCount() const noexcept;
    int getLastActiveSourceId() const noexcept;

    // Message/control-thread-only runtime admission limit for dense, zone-local
    // source IDs. Physical storage remains fixed at MAX_SOURCES; changing this
    // value never allocates or remaps an already-admitted source. Shrinking
    // takes the producer lock, retires upper sources in source/finger order and
    // returns the number of active fingers released. Never call from audio.
    int setSourceCapacity (int newCapacity) noexcept;
    int getSourceCapacity() const noexcept
    {
        return sourceCapacity.load(std::memory_order_acquire);
    }
    std::uint32_t getCapacityDroppedEventCount() const noexcept
    {
        return capacityDroppedEvents.load(std::memory_order_relaxed);
    }

    // Message/control-thread crowd telemetry. A live source remains recent for
    // a short window after release so brief, asynchronous taps still represent
    // the audience size seen by the Adaptive Crowd Governor. The processor
    // combines this window with Simulator::getSimSeatCount(), while current
    // active sources remain available through getActiveSourceCount().
    int getRecentLiveSourceCount (
        std::uint32_t windowMs = recentSourceWindowMs) const noexcept;

    // Flow forwards every motion packet for the legacy immediate response.
    // Timed modes disable forwarding and sample the canonical latest-value
    // ledger instead, preventing high-rate U/V traffic from filling the FIFO.
    void setMotionEventForwardingEnabled (bool enabled) noexcept;
    bool isMotionEventForwardingEnabled() const noexcept
    {
        return forwardMotionEvents.load(std::memory_order_acquire);
    }

    // Source IDs are base-1 for channel assignment. Source 0 is accepted by
    // the OSC protocol and wraps backwards to channel 16. Invalid IDs return 0.
    static int midiChannelForSourceId (int sourceId) noexcept;

private:
    struct SourceState
    {
        std::atomic<float> x { 0.0f };
        std::atomic<float> y { 0.0f };
        std::array<std::atomic<float>, MAX_FINGERS> fingerX;
        std::array<std::atomic<float>, MAX_FINGERS> fingerY;
        std::array<std::atomic<std::uint32_t>, MAX_FINGERS> liveActivityMs;
        std::atomic<std::uint32_t> lastLiveSourceActivityMs { 0 };
        std::atomic<bool> hasLiveSourceActivity { false };
        std::atomic<std::uint16_t> activeFingerMask { 0 };
        std::atomic<std::uint16_t> liveTrackedFingerMask { 0 };
    };

    static bool validSource (int sourceId) noexcept;
    static bool validFinger (int finger) noexcept;
    static float clampNormalized (float value) noexcept;
    static int countSetBits (std::uint16_t bits) noexcept;
    static std::uint32_t systemMonotonicMilliseconds() noexcept;
    static void incrementSaturating (
        std::atomic<std::uint32_t>& counter) noexcept;
    bool isSourceAdmittedLocked (int sourceId) const noexcept;
    int retireSourceLocked (int sourceId) noexcept;
    void setFingerXLocked (int sourceId, int finger, float value) noexcept;
    void setFingerYLocked (int sourceId, int finger, float value) noexcept;
    void setFingerOnLocked (int sourceId, int finger, bool on) noexcept;

    OscFingerRouter& router;
    MonotonicClock monotonicClock;
    SourceQualityController* qualityController = nullptr;
    // Live OSC and the message-thread simulator may publish concurrently.
    // Serialise each canonical-ledger mutation with its matching FIFO event so
    // those two representations always have the same total order. The audio
    // thread never takes this lock; it only reads the atomics below.
    juce::SpinLock producerLock;
    std::array<SourceState, MAX_SOURCES> sources;
    std::atomic<int> activeSourceCount { 0 };
    std::atomic<int> activeFingerCount { 0 };
    std::atomic<int> lastActiveSourceId { -1 };
    std::atomic<bool> forwardMotionEvents { true };
    // Existing callers retain the historical 256-source behaviour until the
    // owning processor applies its fresh/restored 64/128/256 setting.
    std::atomic<int> sourceCapacity { MAX_SOURCES };
    std::atomic<std::uint32_t> capacityDroppedEvents { 0 };
};
