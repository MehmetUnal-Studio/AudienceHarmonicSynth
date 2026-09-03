#pragma once

#include <array>
#include <cstdint>
#include <limits>

// Pure C++ realtime scheduler for OSC finger lifecycles. The owning processor
// keeps the current x/y values in its canonical finger ledger; SampleMotion and
// Attack outputs are requests to sample that ledger at the event's timestamp.
//
// Threading contract: construct, reset, rehydrate and process on the audio
// thread (or while audio is stopped). process() performs no allocation, locking,
// logging, I/O or calls outside this class.
class CrowdTimeField final
{
public:
    static constexpr int kMaxSources = 256;
    static constexpr int kFingersPerSource = 10;
    static constexpr int kMaxVoices = kMaxSources * kFingersPerSource;
    static constexpr int kMaxOutputEvents = 64;
    static constexpr int kMaxInputEventsPerBlock = kMaxVoices;

    enum class Mode : std::uint8_t
    {
        Flow = 0,
        Grid,
        Ensemble
    };

    enum class ClockSource : std::uint8_t
    {
        Host = 0,
        Internal
    };

    enum class Division : std::uint8_t
    {
        Quarter = 0,
        Eighth,
        Sixteenth,
        ThirtySecond
    };

    struct Config
    {
        Mode mode = Mode::Flow;
        ClockSource clockSource = ClockSource::Host;
        Division division = Division::Sixteenth;
        double internalBpm = 120.0;
        int maxAttacksPerStep = 4;
        int maxActive = 16;
        double gatePercent = 70.0;
        int spreadSlots = 4;
        std::uint32_t laneSeed = 0;
        // Soft safety policy. Closed admission retains/cancels pending intent
        // and always processes releases; reopening never causes a reset.
        bool attackAdmissionOpen = true;
        // Flow normally retains its direct, low-latency behaviour. These two
        // independent soft ceilings are used only by the pressure governor so
        // an overloaded venue cannot release an unbounded attack burst when
        // admission reopens. They never form part of the clock domain.
        int flowMaxAttacksPerBlock = kMaxVoices;
        int flowMaxActive = kMaxVoices;
    };

    // All positions describe the start of the current audio block. ppqPosition
    // is in quarter-note beats. monotonicSeconds must come from one process-wide
    // monotonic timebase so multiple instances share the internal/fallback grid.
    struct ClockFrame
    {
        double sampleRate = 44100.0;
        int numSamples = 0;
        bool hostPositionAvailable = false;
        bool hostBpmValid = false;
        bool hostValid = false;
        bool isPlaying = false;
        double bpm = 120.0;
        double ppqPosition = 0.0;
        double monotonicSeconds = 0.0;
    };

    struct InputEvent
    {
        enum class Type : std::uint8_t { On, Off, Cancel };

        Type type = Type::On;
        int voiceId = -1;
        int sourceId = -1;
        int sampleOffset = 0;
    };

    struct OutputEvent
    {
        enum class Type : std::uint8_t { Attack, Release, Cancel, SampleMotion };

        Type type = Type::Attack;
        int voiceId = -1;
        int sourceId = -1;
        int sampleOffset = 0;
    };

    // A canonical held snapshot contains one entry per held source/finger. It is
    // deliberately position-free: the processor already owns the latest x/y.
    struct HeldVoice
    {
        int voiceId = -1;
        int sourceId = -1;
    };

    struct OutputBlock
    {
        std::array<OutputEvent, kMaxOutputEvents> events {};
        int count = 0;

        // A true resetRequested means events is empty. The caller should issue
        // its bounded MIDI safety reset, then call rehydrate() from the canonical
        // held ledger. overflowed distinguishes a bounded-work, malformed-clock
        // or mandatory-event failure from an ordinary clock/config-domain reset.
        bool resetRequested = false;
        bool overflowed = false;
        bool clockLocked = false;
        bool running = false;
        double effectiveBpm = 120.0;
        int pendingCount = 0;
        int activeCount = 0;
        std::uint32_t mergedCount = 0;
        std::uint32_t droppedMotionCount = 0;
    };

    CrowdTimeField() noexcept;

    CrowdTimeField (const CrowdTimeField&) = delete;
    CrowdTimeField& operator= (const CrowdTimeField&) = delete;

    static constexpr int voiceIdFor (int sourceId, int finger) noexcept
    {
        if (sourceId < 0 || sourceId >= kMaxSources
            || finger < 0 || finger >= kFingersPerSource)
            return -1;
        return sourceId * kFingersPerSource + finger;
    }

    static constexpr int sourceIdForVoice (int voiceId) noexcept
    {
        return voiceId >= 0 && voiceId < kMaxVoices
             ? voiceId / kFingersPerSource : -1;
    }

    static Config sanitiseConfig (const Config&) noexcept;
    static double divisionQuarterNotes (Division) noexcept;

    // Inputs must be in nondecreasing sampleOffset order (FIFO order for equal
    // offsets), with at most kMaxInputEventsPerBlock items. Invalid identities
    // are ignored; offsets are clamped to the block. Oversized/out-of-order valid
    // input fails closed with resetRequested. The returned event list is
    // chronological and stable at equal offsets.
    void process (const Config&, const ClockFrame&,
                  const InputEvent* inputs, int inputCount,
                  OutputBlock&) noexcept;

    // Clears both the scheduling domain and all voices/statistics. This never
    // emits releases; the owner is responsible for its MIDI safety reset.
    void reset() noexcept;

    // Replaces voice state with the authoritative held set while retaining the
    // clock/config domain accepted by the last process() call. Held Flow voices
    // retrigger in bounded batches; Grid/Ensemble voices enter their next grid.
    int rehydrate (const HeldVoice* held, int count) noexcept;

    // Releases Grid's admission ownership after the Notes Only renderer has
    // emitted the final fixed-duration Note Off for this semantic voice. The
    // gesture's held state is deliberately retained: Grid remains one-shot and
    // a fresh attack still requires the normal Off -> On lifecycle.
    // Audio-thread only; emits no MIDI and allocates nothing.
    bool expireAudibleVoice (int voiceId) noexcept;

    int getPendingCount() const noexcept { return pendingCount_; }
    int getActiveCount() const noexcept { return activeCount_; }
    std::uint32_t getMergedCount() const noexcept { return mergedCount_; }

private:
    struct VoiceState
    {
        bool held = false;
        bool sounding = false;
        bool pending = false;
        bool releaseReservedThisBlock = false;
        int sourceId = -1;
        double pendingSinceBeat = 0.0;
        double pendingDeadlineBeat = std::numeric_limits<double>::infinity();
        double gateEndBeat = std::numeric_limits<double>::infinity();
        std::int64_t lastAttackStep = std::numeric_limits<std::int64_t>::min();
    };

    struct ResolvedClock
    {
        double sampleRate = 44100.0;
        int numSamples = 0;
        double beatStart = 0.0;
        double beatEnd = 0.0;
        double bpm = 120.0;
        double monotonicSeconds = 0.0;
        bool hostPrimary = false;
        bool clockLocked = false;
        bool running = true;
        bool validDomain = true;
    };

    struct EventCollector;

    static bool validIdentity (int voiceId, int sourceId) noexcept;
    static int clampedOffset (int sampleOffset, int numSamples) noexcept;
    static ResolvedClock resolveClock (const Config&, const ClockFrame&) noexcept;
    static bool domainConfigsEqual (const Config&, const Config&) noexcept;

    void clearVoiceState() noexcept;
    void adoptDomain (const Config&, const ResolvedClock&) noexcept;
    bool domainChanged (const Config&, const ResolvedClock&) const noexcept;
    void applySoftPolicy (const Config&, const ResolvedClock&) noexcept;
    void updateClockHistory (const ResolvedClock&) noexcept;

    void processFlow (const Config&, const ResolvedClock&, const InputEvent*, int,
                      EventCollector&) noexcept;
    void processTimed (const Config&, const ResolvedClock&,
                       const InputEvent*, int, EventCollector&) noexcept;
    void handleTimedInput (const Config&, const InputEvent&, int,
                           double, EventCollector&) noexcept;
    void processGateReleases (const Config&, double, int,
                              EventCollector&) noexcept;
    void processPendingExpiry (double) noexcept;
    void processGridTick (const Config&, std::int64_t, double, int,
                          EventCollector&) noexcept;
    void startAttack (const Config&, int, std::int64_t, double, int,
                      EventCollector&) noexcept;
    void stopVoice (int, int, EventCollector&) noexcept;
    void cancelVoice (int, int, EventCollector&) noexcept;

    void setPending (int voiceId, double sinceBeat, double lifetimeBeats) noexcept;
    void clearPending (int voiceId) noexcept;
    void addTimedActive (int voiceId) noexcept;
    void removeTimedActive (int voiceId) noexcept;
    void refreshNextGateEnd() noexcept;
    void refreshNextPendingExpiry() noexcept;
    void incrementMerged() noexcept;

    std::array<VoiceState, kMaxVoices> voices_ {};
    Config config_ {};
    bool domainInitialised_ = false;
    bool lastHostPrimary_ = false;
    bool lastRunning_ = true;
    double lastSampleRate_ = 44100.0;
    double lastBpm_ = 120.0;
    double lastBeatStart_ = 0.0;
    double lastBeatEnd_ = 0.0;
    double lastMonotonicSeconds_ = 0.0;
    int lastNumSamples_ = 0;
    std::int64_t nextStepIndex_ = 0;
    int fairCursor_ = -1;
    std::array<int, 16> timedActiveVoiceIds_ {};
    int timedActiveCount_ = 0;
    int pendingCount_ = 0;
    int activeCount_ = 0;
    double nextGateEndBeat_ = std::numeric_limits<double>::infinity();
    double nextPendingExpiryBeat_ = std::numeric_limits<double>::infinity();
    bool pendingAnchorDeferred_ = false;
    std::uint32_t mergedCount_ = 0;
    std::uint32_t droppedMotionCount_ = 0;
};
