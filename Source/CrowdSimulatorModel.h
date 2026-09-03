#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// JUCE-free, allocation-free human touch model used by Simulator. It is
// intentionally a message/control-thread component; none of its methods are
// called from the audio callback. Every participant owns independent lifecycle
// and motion PRNG streams derived from (global seed, source ID), so growing a
// population cannot alter an existing participant's underlying behaviour.
class CrowdSimulatorModel
{
public:
    static constexpr int maxParticipants = 256;
    static constexpr int tickIntervalMs = 25;
    static constexpr int coordinateIntervalMs = 50;
    static constexpr std::size_t maxEventsPerAdvance =
        static_cast<std::size_t> (maxParticipants * 5);

    enum class Profile : std::uint8_t
    {
        human = 0,
        dense,
        stress
    };

    enum class EventType : std::uint8_t
    {
        x,
        y,
        on,
        off
    };

    struct Event
    {
        EventType type = EventType::x;
        int row = 0;
        int sourceId = 0;
        float value = 0.0f;
    };

    using EventBuffer = std::array<Event, maxEventsPerAdvance>;

    struct MutationResult
    {
        int sourceId = -1;
        std::size_t eventCount = 0;
    };

    struct ParticipantSnapshot
    {
        bool occupied = false;
        bool active = false;
        bool held = false;
        int row = 0;
        float x = 0.0f;
        float y = 0.0f;
        int lifecycleRemainingMs = 0;
    };

    explicit CrowdSimulatorModel (
        int sourceCapacity = maxParticipants,
        std::uint64_t seed = defaultSeed) noexcept;

    void setProfile (Profile newProfile) noexcept;
    Profile getProfile() const noexcept { return profile; }

    // Intended to be set before adding participants. Reseeding a populated
    // model preserves current positions/lifecycle but deterministically changes
    // future random choices; it never emits events or changes population.
    void setSeed (std::uint64_t newSeed) noexcept;
    std::uint64_t getSeed() const noexcept { return globalSeed; }

    MutationResult addHeldParticipant (EventBuffer& output) noexcept;
    MutationResult addCrowdParticipant (EventBuffer& output) noexcept;
    MutationResult removeOneParticipant (EventBuffer& output) noexcept;

    std::size_t advance (bool movementEnabled, EventBuffer& output) noexcept;
    std::size_t clear (EventBuffer& output) noexcept;
    void clearSilently() noexcept;

    // Message/control-thread-only dense source-ID domain mutation. It does not
    // reallocate storage. On a shrink, active removed participants emit
    // deterministic Off events before their slots are cleared. The caller can
    // dispatch those events while its downstream audience model still has the
    // old admission limit.
    std::size_t setSourceCapacity (int newCapacity,
                                   EventBuffer& output) noexcept;

    int getSourceCapacity() const noexcept { return sourceCapacity; }
    int getPopulation() const noexcept { return population; }
    int getHeldPopulation() const noexcept { return heldPopulation; }
    int getCrowdPopulation() const noexcept { return crowdPopulation; }
    int getActivePopulation() const noexcept { return activePopulation; }
    int getActiveCrowdPopulation() const noexcept { return activeCrowdPopulation; }
    ParticipantSnapshot getParticipantSnapshot (int sourceId) const noexcept;

    static const char* profileName (Profile value) noexcept;
    static int motionFrameLimitPerSecond (Profile value) noexcept;
    static int scalarMotionEventLimitPerSecond (Profile value) noexcept
    {
        return motionFrameLimitPerSecond (value) * 2;
    }

    static constexpr std::uint64_t defaultSeed = UINT64_C (0x43534d5741564531);

private:
    enum class GestureClass : std::uint8_t
    {
        tap,
        shortDrag,
        longDrag,
        extendedDrag
    };

    enum class MotionStyle : std::uint8_t
    {
        still,
        gentle,
        ordinary,
        expressive
    };

    enum class AxisPreference : std::uint8_t
    {
        horizontal,
        vertical,
        free
    };

    enum class LifecyclePersona : std::uint8_t
    {
        tapper,
        explorer,
        ordinary,
        intermittent
    };

    // Stable, source-derived traits are kept separate from the two evolving
    // lifecycle/motion PRNG streams. This avoids accidentally coupling a
    // person's gesture timing to their path whenever either model is tuned.
    struct LifecycleTraits
    {
        LifecyclePersona persona = LifecyclePersona::ordinary;
        double durationScale = 1.0;
        double idleScale = 1.0;
    };

    struct MotionTraits
    {
        MotionStyle baseStyle = MotionStyle::ordinary;
        AxisPreference axisPreference = AxisPreference::free;
        double homeX = 0.5;
        double homeY = 0.5;
        double radiusX = 0.30;
        double radiusY = 0.22;
        double speedScale = 1.0;
        double pauseScale = 1.0;
        double axisLockChance = 0.76;
        double homePull = 0.30;
    };

    struct Participant
    {
        bool occupied = false;
        bool active = false;
        bool held = false;
        bool paused = false;
        bool staticTap = false;
        bool axisLocked = false;
        int row = 0;
        int sourceId = 0;
        int lifecycleRemainingMs = 0;
        int physicsRemainingMs = coordinateIntervalMs;
        int motionRemainingMs = coordinateIntervalMs;
        int motionSegmentRemainingMs = 0;
        int pauseFramesRemaining = 0;
        int styleBoutRemainingMs = 0;
        GestureClass gestureClass = GestureClass::tap;
        MotionStyle motionStyle = MotionStyle::ordinary;
        LifecycleTraits lifecycleTraits {};
        MotionTraits motionTraits {};
        double x = 0.0;
        double y = 0.0;
        double emittedX = 0.0;
        double emittedY = 0.0;
        double speed = 0.0;
        double targetSpeed = 0.0;
        double gestureSpeedScale = 1.0;
        double heading = 0.0;
        double targetHeading = 0.0;
        std::uint64_t lifecycleRng = 1;
        std::uint64_t motionRng = 1;
        std::uint64_t cadenceRng = 1;
    };

    MutationResult addParticipant (bool held, EventBuffer& output) noexcept;
    void activate (Participant& participant, EventBuffer& output,
                   std::size_t& eventCount) noexcept;
    void deactivate (Participant& participant, EventBuffer& output,
                     std::size_t& eventCount) noexcept;
    void advanceMotion (Participant& participant) noexcept;
    void chooseMotionSegment (Participant& participant, bool initial) noexcept;
    void chooseMotionStyle (Participant& participant, bool initial) noexcept;
    int sampleMotionIntervalMs (Participant& participant) noexcept;
    int samplePauseRunFrames (Participant& participant) noexcept;
    GestureClass chooseGestureClass (Participant& participant) noexcept;
    int sampleGestureDurationMs (Participant& participant,
                                 GestureClass gesture) noexcept;
    int sampleIdleDurationMs (Participant& participant) noexcept;
    int sampleInitialDelayMs (Participant& participant) noexcept;
    int findFirstFreeSource() const noexcept;
    int chooseOccupiedSource() noexcept;
    void initialiseParticipantRng (Participant& participant) noexcept;
    void resetMotionBudget() noexcept;

    static std::uint64_t mix64 (std::uint64_t value) noexcept;
    static std::uint64_t nextRandom (std::uint64_t& state) noexcept;
    static double uniform01 (std::uint64_t& state) noexcept;
    static double normal01 (std::uint64_t& state) noexcept;
    static double boundedLogNormal (std::uint64_t& state, double median,
                                    double sigma, double minimum,
                                    double maximum) noexcept;
    static double wrapAngle (double radians) noexcept;
    static double quantizeCoordinate (double value) noexcept;
    static bool finiteUnit (double value) noexcept;
    static void appendEvent (EventBuffer& output, std::size_t& eventCount,
                             EventType type, const Participant& participant,
                             float value = 0.0f) noexcept;

    std::array<Participant, maxParticipants> participants {};
    int sourceCapacity = maxParticipants;
    int population = 0;
    int heldPopulation = 0;
    int crowdPopulation = 0;
    int activePopulation = 0;
    int activeCrowdPopulation = 0;
    int motionRoundRobinSource = 0;
    std::int64_t motionCreditMilliFrames = 0;
    std::uint64_t globalSeed = defaultSeed;
    std::uint64_t managementRng = 1;
    Profile profile = Profile::human;
};
