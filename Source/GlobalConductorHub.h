#pragma once

#include <array>
#include <atomic>
#include <cstdint>

// Process-local coordination core for at most sixteen Cosmic Microwave plugin
// instances. It stores scalar publications only: no instance pointers,
// callbacks, strings or dynamically-sized containers ever cross a lifetime or
// realtime boundary.
//
// Thread contract:
// - registerInstance()/unregisterInstance() are message-thread operations.
// - publishZoneDensity(), publishLeaderBudgets(), allocationTick() and
//   readAudioSnapshot() are bounded, allocation-free and safe from an audio
//   callback.
// - readers never wait for a writer. A publication in flight, stale heartbeat
//   or stale handle returns the caller's local fallback policy immediately.
class GlobalConductorHub final
{
public:
    static constexpr int kMaxInstances = 16;
    static constexpr int kMaxZoneDensity = 256;
    static constexpr int kMaxQuotaPerZone = 16;
    static constexpr int kMaxGlobalBudget = kMaxInstances * kMaxQuotaPerZone;
    static constexpr std::uint32_t kAllocationPeriodMs = 100; // 10 Hz
    static constexpr std::uint32_t kHeartbeatTimeoutMs = 1500;
    static constexpr std::uint8_t kInvalidSlot = 0xff;

    enum class Role : std::uint8_t
    {
        Off = 0,
        Follower,
        Leader
    };

    struct Registration
    {
        int udpPort = 0;
        std::uint32_t zoneKey = 0;
        Role role = Role::Off;
        // Instances coordinate only with registrations carrying the exact same
        // opaque group key. Zero is a valid default group.
        std::uint32_t groupKey = 0;
    };

    struct Handle
    {
        std::uint8_t slot = kInvalidSlot;
        std::uint32_t generation = 0;

        bool isValid() const noexcept
        {
            return slot < kMaxInstances && generation != 0;
        }

        bool operator== (const Handle& other) const noexcept
        {
            return slot == other.slot && generation == other.generation;
        }

        bool operator!= (const Handle& other) const noexcept
        {
            return ! (*this == other);
        }
    };

    enum class RegistrationStatus : std::uint8_t
    {
        Registered = 0,
        InvalidPort,
        DuplicatePort,
        CapacityFull
    };

    struct RegistrationResult
    {
        RegistrationStatus status = RegistrationStatus::InvalidPort;
        Handle handle {};
        Handle conflictingHandle {};
    };

    struct LocalPolicy
    {
        int attackQuota = 4;
        int voiceQuota = 8;
    };

    enum class SnapshotSource : std::uint8_t
    {
        // No coherent, current leader allocation is available. Quotas are the
        // sanitized LocalPolicy supplied by this plugin instance.
        LocalFallback = 0,

        // The instance explicitly opted out. It still receives LocalPolicy, but
        // Bypassed distinguishes this intentional state from a failed leader.
        Bypassed,

        // Quotas came from one coherent 10 Hz global allocation publication.
        Global
    };

    struct AudioSnapshot
    {
        SnapshotSource source = SnapshotSource::LocalFallback;
        Role role = Role::Off;
        int attackQuota = 4;
        int voiceQuota = 8;
        int globalAttackBudget = 0;
        int globalVoiceBudget = 0;
        int zoneDensity = 0;
        int activeZoneCount = 0;
        int leaderUdpPort = 0;
        std::uint32_t leaderZoneKey = 0;
        std::uint32_t allocationEpoch = 0;
        bool electedLeader = false;
    };

    enum class TickResult : std::uint8_t
    {
        Published = 0,
        TooSoon,
        Busy,
        NotLeader,
        StaleLeaderHeartbeat
    };

    GlobalConductorHub() noexcept;

    GlobalConductorHub (const GlobalConductorHub&) = delete;
    GlobalConductorHub& operator= (const GlobalConductorHub&) = delete;

    // One non-allocating hub shared by every instance loaded from this plugin
    // module. It owns no plugin objects and is safe to outlive every instance.
    static GlobalConductorHub& shared() noexcept;

    // Message-thread only. Ports outside 1..65535 are rejected rather than
    // clamped so hostile values cannot alias a real endpoint. Invalid Role enum
    // values are sanitized to Off. zoneKey and groupKey are opaque,
    // allocation-free IDs; zero is valid for both.
    RegistrationResult registerInstance (const Registration&) noexcept;
    bool unregisterInstance (Handle) noexcept;

    // Audio/control-thread publications. Density is clamped to 0..256. Only the
    // deterministically elected Leader in the caller's group (lowest UDP port,
    // then lowest slot) may publish global budgets; budgets are clamped to
    // 0..256.
    bool publishZoneDensity (Handle, int density,
                             std::uint32_t nowMilliseconds) noexcept;
    bool publishLeaderBudgets (Handle, int globalAttackBudget,
                               int globalVoiceBudget,
                               std::uint32_t nowMilliseconds) noexcept;

    // The elected Leader calls this at callback/control cadence. At most one
    // allocation is accepted per 100 ms. Scarce budgets rotate over live zones;
    // remaining capacity is density-weighted with deterministic tie-breaking.
    TickResult allocationTick (Handle leader,
                               std::uint32_t nowMilliseconds) noexcept;

    // Bounded atomic audio read. No retries can spin indefinitely. Global data
    // expires 1500 ms after either the leader heartbeat or allocation tick;
    // handle/leader changes invalidate it immediately.
    AudioSnapshot readAudioSnapshot (Handle, std::uint32_t nowMilliseconds,
                                     LocalPolicy) const noexcept;

    bool isRegistered (Handle) const noexcept;
    Handle electedLeader() const noexcept;

    // Helper for callers that own a floating monotonic clock. Non-finite or
    // negative values sanitize to zero; finite values wrap modulo uint32 ms.
    static std::uint32_t millisecondsFromSeconds (double) noexcept;

private:
    enum : std::uint32_t { emptySlot = 0, activeSlot = 1 };

    struct Slot
    {
        std::atomic<std::uint32_t> state { emptySlot };
        std::atomic<std::uint32_t> generation { 1 };
        std::atomic<int> udpPort { 0 };
        std::atomic<std::uint32_t> zoneKey { 0 };
        std::atomic<std::uint32_t> groupKey { 0 };
        std::atomic<std::uint32_t> role { 0 };

        std::atomic_flag densityWriter = ATOMIC_FLAG_INIT;
        std::atomic<std::uint32_t> densitySequence { 0 };
        std::atomic<std::uint32_t> densityGeneration { 0 };
        std::atomic<std::uint32_t> densityTimestampMs { 0 };
        std::atomic<int> density { 0 };

        std::atomic_flag leaderWriter = ATOMIC_FLAG_INIT;
        std::atomic<std::uint32_t> leaderSequence { 0 };
        std::atomic<std::uint32_t> leaderGeneration { 0 };
        std::atomic<std::uint32_t> leaderTimestampMs { 0 };
        std::atomic<int> globalAttackBudget { 0 };
        std::atomic<int> globalVoiceBudget { 0 };

        std::atomic<std::uint32_t> quotaSequence { 0 };
        std::atomic<std::uint32_t> quotaRegistrationGeneration { 0 };
        std::atomic<std::uint32_t> quotaLeaderGeneration { 0 };
        std::atomic<std::uint32_t> quotaAllocationTimestampMs { 0 };
        std::atomic<std::uint32_t> quotaLeaderTimestampMs { 0 };
        std::atomic<std::uint32_t> quotaEpoch { 0 };
        std::atomic<std::uint32_t> quotaGroupKey { 0 };
        std::atomic<int> quotaValid { 0 };
        std::atomic<int> attackQuota { 0 };
        std::atomic<int> voiceQuota { 0 };
        std::atomic<int> quotaGlobalAttackBudget { 0 };
        std::atomic<int> quotaGlobalVoiceBudget { 0 };
        std::atomic<int> quotaZoneDensity { 0 };
        std::atomic<int> quotaActiveZoneCount { 0 };
        std::atomic<int> quotaLeaderSlot { -1 };
        std::atomic<int> quotaLeaderUdpPort { 0 };
        std::atomic<std::uint32_t> quotaLeaderZoneKey { 0 };
    };

    struct DensityPublication
    {
        std::uint32_t generation = 0;
        std::uint32_t timestampMs = 0;
        int density = 0;
    };

    struct LeaderPublication
    {
        std::uint32_t generation = 0;
        std::uint32_t timestampMs = 0;
        int attackBudget = 0;
        int voiceBudget = 0;
    };

    struct QuotaPublication
    {
        std::uint32_t registrationGeneration = 0;
        std::uint32_t leaderGeneration = 0;
        std::uint32_t allocationTimestampMs = 0;
        std::uint32_t leaderTimestampMs = 0;
        std::uint32_t epoch = 0;
        std::uint32_t groupKey = 0;
        int valid = 0;
        int attackQuota = 0;
        int voiceQuota = 0;
        int globalAttackBudget = 0;
        int globalVoiceBudget = 0;
        int zoneDensity = 0;
        int activeZoneCount = 0;
        int leaderSlot = -1;
        int leaderUdpPort = 0;
        std::uint32_t leaderZoneKey = 0;
    };

    struct Candidate
    {
        int slot = -1;
        std::uint32_t generation = 0;
        int density = 0;
    };

    // allocationWriter_ serializes access. A separate clock/cursor per elected
    // leader slot lets independent groups publish on the same 10 Hz boundary.
    struct AllocationState
    {
        std::uint32_t leaderGeneration = 0;
        std::uint32_t lastAllocationMs = 0;
        std::uint32_t epoch = 0;
        int fairnessCursor = 0;
        bool clockInitialised = false;
    };

    static Role sanitiseRole (Role) noexcept;
    static int clamp (int value, int low, int high) noexcept;
    static bool isFresh (std::uint32_t now, std::uint32_t timestamp,
                         std::uint32_t timeout) noexcept;
    static std::uint32_t nextGeneration (std::uint32_t) noexcept;
    static int cyclicRank (int slot, int cursor) noexcept;

    void lockAdministration() noexcept;
    void unlockAdministration() noexcept;
    Handle electedLeaderForGroup (std::uint32_t groupKey) const noexcept;
    bool readDensity (const Slot&, DensityPublication&) const noexcept;
    bool readLeaderPublication (const Slot&, LeaderPublication&) const noexcept;
    bool readQuota (const Slot&, QuotaPublication&) const noexcept;
    void publishQuota (Slot&, const QuotaPublication&) noexcept;
    void allocateBudget (const std::array<Candidate, kMaxInstances>&,
                         int candidateCount, int budget, int cursor,
                         std::array<int, kMaxInstances>& quotas) const noexcept;

    std::array<Slot, kMaxInstances> slots_ {};
    std::array<AllocationState, kMaxInstances> allocationStates_ {};
    std::atomic_flag administrationLock_ = ATOMIC_FLAG_INIT;
    std::atomic_flag allocationWriter_ = ATOMIC_FLAG_INIT;
};
