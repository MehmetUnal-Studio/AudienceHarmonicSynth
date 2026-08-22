#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "OscFingerRouter.h"
#include "SeatEventSink.h"

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

    explicit MidiAudienceModel (OscFingerRouter& destination) noexcept;

    void setX  (int row, int sourceId, float xNorm) noexcept override;
    void setY  (int row, int sourceId, float yNorm) noexcept override;
    void setOn (int row, int sourceId, bool on) noexcept override;

    void setFingerX  (int row, int sourceId, int finger, float xNorm) noexcept override;
    void setFingerY  (int row, int sourceId, int finger, float yNorm) noexcept override;
    void setFingerOn (int row, int sourceId, int finger, bool on) noexcept override;

    // Clears UI/control state immediately. The queued MIDI branch is reset by
    // its consumer so stale note events cannot survive this operation.
    void clear() noexcept;

    SourceSnapshot getSourceSnapshot (int sourceId) const noexcept;
    FingerSnapshot getFingerSnapshot (int sourceId, int finger) const noexcept;
    int getActiveSourceCount() const noexcept;
    int getActiveFingerCount() const noexcept;
    int getLastActiveSourceId() const noexcept;

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
        std::atomic<std::uint16_t> activeFingerMask { 0 };
    };

    static bool validSource (int sourceId) noexcept;
    static bool validFinger (int finger) noexcept;
    static float clampNormalized (float value) noexcept;
    static int countSetBits (std::uint16_t bits) noexcept;

    OscFingerRouter& router;
    // Live OSC and the message-thread simulator may publish concurrently.
    // Serialise each canonical-ledger mutation with its matching FIFO event so
    // those two representations always have the same total order. The audio
    // thread never takes this lock; it only reads the atomics below.
    juce::SpinLock producerLock;
    std::array<SourceState, MAX_SOURCES> sources;
    std::atomic<int> activeSourceCount { 0 };
    std::atomic<int> activeFingerCount { 0 };
    std::atomic<int> lastActiveSourceId { -1 };
};
