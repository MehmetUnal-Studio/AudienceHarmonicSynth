#include "GlobalConductorHub.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    static_assert(std::atomic<int>::is_always_lock_free,
                  "GlobalConductorHub requires lock-free integer atomics");
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "GlobalConductorHub requires lock-free uint32 atomics");

    constexpr std::uint32_t kHalfTimestampRange = 0x80000000u;
}

GlobalConductorHub::GlobalConductorHub() noexcept = default;

GlobalConductorHub& GlobalConductorHub::shared() noexcept
{
    static GlobalConductorHub hub;
    return hub;
}

GlobalConductorHub::RegistrationResult GlobalConductorHub::registerInstance (
    const Registration& requested) noexcept
{
    RegistrationResult result;
    if (requested.udpPort < 1 || requested.udpPort > 65535)
    {
        result.status = RegistrationStatus::InvalidPort;
        return result;
    }

    lockAdministration();

    int empty = -1;
    for (int index = 0; index < kMaxInstances; ++index)
    {
        auto& slot = slots_[(std::size_t) index];
        if (slot.state.load(std::memory_order_acquire) != activeSlot)
        {
            if (empty < 0)
                empty = index;
            continue;
        }

        if (slot.udpPort.load(std::memory_order_relaxed) == requested.udpPort)
        {
            result.status = RegistrationStatus::DuplicatePort;
            result.conflictingHandle.slot = (std::uint8_t) index;
            result.conflictingHandle.generation =
                slot.generation.load(std::memory_order_relaxed);
            unlockAdministration();
            return result;
        }
    }

    if (empty < 0)
    {
        result.status = RegistrationStatus::CapacityFull;
        unlockAdministration();
        return result;
    }

    auto& slot = slots_[(std::size_t) empty];
    const auto generation = nextGeneration(
        slot.generation.load(std::memory_order_relaxed));
    slot.generation.store(generation, std::memory_order_relaxed);
    slot.udpPort.store(requested.udpPort, std::memory_order_relaxed);
    slot.zoneKey.store(requested.zoneKey, std::memory_order_relaxed);
    slot.groupKey.store(requested.groupKey, std::memory_order_relaxed);
    slot.role.store((std::uint32_t) sanitiseRole(requested.role),
                    std::memory_order_relaxed);

    // Generation tags, rather than destructive clearing, make a concurrent
    // stale publisher harmless even if it completes after this registration.
    slot.densityGeneration.store(0, std::memory_order_relaxed);
    slot.leaderGeneration.store(0, std::memory_order_relaxed);
    slot.quotaRegistrationGeneration.store(0, std::memory_order_relaxed);
    slot.quotaGroupKey.store(0, std::memory_order_relaxed);
    slot.quotaValid.store(0, std::memory_order_relaxed);
    slot.state.store(activeSlot, std::memory_order_release);

    result.status = RegistrationStatus::Registered;
    result.handle.slot = (std::uint8_t) empty;
    result.handle.generation = generation;
    unlockAdministration();
    return result;
}

bool GlobalConductorHub::unregisterInstance (Handle handle) noexcept
{
    if (! handle.isValid())
        return false;

    lockAdministration();
    auto& slot = slots_[(std::size_t) handle.slot];
    const bool matches = slot.state.load(std::memory_order_acquire) == activeSlot
                      && slot.generation.load(std::memory_order_relaxed)
                           == handle.generation;
    if (! matches)
    {
        unlockAdministration();
        return false;
    }

    // Empty is published first, so new audio reads reject this lifetime before
    // any scalar is recycled. In-flight readers only hold copied scalar values.
    slot.state.store(emptySlot, std::memory_order_release);
    slot.role.store((std::uint32_t) Role::Off, std::memory_order_relaxed);
    slot.densityGeneration.store(0, std::memory_order_release);
    slot.leaderGeneration.store(0, std::memory_order_release);
    slot.quotaRegistrationGeneration.store(0, std::memory_order_release);
    slot.quotaValid.store(0, std::memory_order_relaxed);
    slot.udpPort.store(0, std::memory_order_relaxed);
    slot.zoneKey.store(0, std::memory_order_relaxed);
    slot.groupKey.store(0, std::memory_order_relaxed);
    slot.quotaGroupKey.store(0, std::memory_order_relaxed);
    unlockAdministration();
    return true;
}

bool GlobalConductorHub::publishZoneDensity (Handle handle, int density,
                                              std::uint32_t now) noexcept
{
    if (! isRegistered(handle))
        return false;

    auto& slot = slots_[(std::size_t) handle.slot];
    if (sanitiseRole((Role) slot.role.load(std::memory_order_acquire)) == Role::Off)
        return false;
    if (slot.densityWriter.test_and_set(std::memory_order_acquire))
        return false;

    if (! isRegistered(handle)
        || sanitiseRole((Role) slot.role.load(std::memory_order_acquire)) == Role::Off)
    {
        slot.densityWriter.clear(std::memory_order_release);
        return false;
    }

    slot.densitySequence.fetch_add(1, std::memory_order_acq_rel);
    slot.density.store(clamp(density, 0, kMaxZoneDensity),
                       std::memory_order_relaxed);
    slot.densityTimestampMs.store(now, std::memory_order_relaxed);
    slot.densityGeneration.store(handle.generation, std::memory_order_relaxed);
    slot.densitySequence.fetch_add(1, std::memory_order_release);
    slot.densityWriter.clear(std::memory_order_release);
    return true;
}

bool GlobalConductorHub::publishLeaderBudgets (Handle handle,
                                                int attackBudget,
                                                int voiceBudget,
                                                std::uint32_t now) noexcept
{
    if (! isRegistered(handle))
        return false;

    auto& slot = slots_[(std::size_t) handle.slot];
    const auto group = slot.groupKey.load(std::memory_order_acquire);
    if (electedLeaderForGroup(group) != handle)
        return false;
    if (slot.leaderWriter.test_and_set(std::memory_order_acquire))
        return false;

    if (! isRegistered(handle)
        || slot.groupKey.load(std::memory_order_acquire) != group
        || electedLeaderForGroup(group) != handle)
    {
        slot.leaderWriter.clear(std::memory_order_release);
        return false;
    }

    slot.leaderSequence.fetch_add(1, std::memory_order_acq_rel);
    slot.globalAttackBudget.store(clamp(attackBudget, 0, kMaxGlobalBudget),
                                  std::memory_order_relaxed);
    slot.globalVoiceBudget.store(clamp(voiceBudget, 0, kMaxGlobalBudget),
                                 std::memory_order_relaxed);
    slot.leaderTimestampMs.store(now, std::memory_order_relaxed);
    slot.leaderGeneration.store(handle.generation, std::memory_order_relaxed);
    slot.leaderSequence.fetch_add(1, std::memory_order_release);
    slot.leaderWriter.clear(std::memory_order_release);
    return true;
}

GlobalConductorHub::TickResult GlobalConductorHub::allocationTick (
    Handle leader, std::uint32_t now) noexcept
{
    if (allocationWriter_.test_and_set(std::memory_order_acquire))
        return TickResult::Busy;

    const auto finish = [this] (TickResult result) noexcept
    {
        allocationWriter_.clear(std::memory_order_release);
        return result;
    };

    if (! isRegistered(leader))
        return finish(TickResult::NotLeader);

    const auto& leaderSlot = slots_[(std::size_t) leader.slot];
    const auto leaderGroup = leaderSlot.groupKey.load(std::memory_order_acquire);
    const auto elected = electedLeaderForGroup(leaderGroup);
    if (elected != leader)
        return finish(TickResult::NotLeader);

    LeaderPublication publication;
    if (! readLeaderPublication(leaderSlot, publication)
        || publication.generation != leader.generation
        || ! isFresh(now, publication.timestampMs, kHeartbeatTimeoutMs))
        return finish(TickResult::StaleLeaderHeartbeat);

    auto& allocationState = allocationStates_[(std::size_t) leader.slot];
    if (allocationState.leaderGeneration != leader.generation)
    {
        allocationState = {};
        allocationState.leaderGeneration = leader.generation;
    }

    if (allocationState.clockInitialised)
    {
        const std::uint32_t elapsed = now - allocationState.lastAllocationMs;
        const bool clockMovedBack = elapsed >= kHalfTimestampRange;
        if (! clockMovedBack && elapsed < kAllocationPeriodMs)
            return finish(TickResult::TooSoon);
    }

    std::array<Candidate, kMaxInstances> candidates {};
    std::array<bool, kMaxInstances> eligible {};
    std::array<std::uint32_t, kMaxInstances> candidateGenerationBySlot {};
    std::array<int, kMaxInstances> densityBySlot {};
    int candidateCount = 0;

    for (int index = 0; index < kMaxInstances; ++index)
    {
        const auto& slot = slots_[(std::size_t) index];
        if (slot.state.load(std::memory_order_acquire) != activeSlot
            || slot.groupKey.load(std::memory_order_relaxed) != leaderGroup
            || sanitiseRole((Role) slot.role.load(std::memory_order_acquire))
                 == Role::Off)
            continue;

        const auto generation = slot.generation.load(std::memory_order_relaxed);
        DensityPublication density;
        if (! readDensity(slot, density)
            || density.generation != generation
            || ! isFresh(now, density.timestampMs, kHeartbeatTimeoutMs))
            continue;

        Candidate candidate;
        candidate.slot = index;
        candidate.generation = generation;
        candidate.density = clamp(density.density, 0, kMaxZoneDensity);
        candidates[(std::size_t) candidateCount++] = candidate;
        eligible[(std::size_t) index] = true;
        candidateGenerationBySlot[(std::size_t) index] = generation;
        densityBySlot[(std::size_t) index] = candidate.density;
    }

    std::array<int, kMaxInstances> attackQuotas {};
    std::array<int, kMaxInstances> voiceQuotas {};
    const int attackBudget = clamp(publication.attackBudget, 0, kMaxGlobalBudget);
    const int voiceBudget = clamp(publication.voiceBudget, 0, kMaxGlobalBudget);
    allocateBudget(candidates, candidateCount, attackBudget,
                   allocationState.fairnessCursor,
                   attackQuotas);
    allocateBudget(candidates, candidateCount, voiceBudget,
                   allocationState.fairnessCursor,
                   voiceQuotas);

    allocationState.epoch = nextGeneration(allocationState.epoch);
    const int leaderPort = leaderSlot.udpPort.load(std::memory_order_relaxed);
    const auto leaderZone = leaderSlot.zoneKey.load(std::memory_order_relaxed);

    for (int index = 0; index < kMaxInstances; ++index)
    {
        auto& slot = slots_[(std::size_t) index];
        if (slot.state.load(std::memory_order_acquire) != activeSlot
            || slot.groupKey.load(std::memory_order_relaxed) != leaderGroup
            || sanitiseRole((Role) slot.role.load(std::memory_order_acquire))
                 == Role::Off)
            continue;

        QuotaPublication quota;
        quota.registrationGeneration =
            slot.generation.load(std::memory_order_relaxed);
        quota.leaderGeneration = leader.generation;
        quota.allocationTimestampMs = now;
        quota.leaderTimestampMs = publication.timestampMs;
        quota.epoch = allocationState.epoch;
        quota.groupKey = leaderGroup;
        // A slot may have been unregistered and reused after candidate capture.
        // Never stamp the old instance's quota/density with the new generation.
        quota.valid = eligible[(std::size_t) index]
                   && candidateGenerationBySlot[(std::size_t) index]
                        == quota.registrationGeneration ? 1 : 0;
        quota.attackQuota = attackQuotas[(std::size_t) index];
        quota.voiceQuota = voiceQuotas[(std::size_t) index];
        quota.globalAttackBudget = attackBudget;
        quota.globalVoiceBudget = voiceBudget;
        quota.zoneDensity = densityBySlot[(std::size_t) index];
        quota.activeZoneCount = candidateCount;
        quota.leaderSlot = (int) leader.slot;
        quota.leaderUdpPort = leaderPort;
        quota.leaderZoneKey = leaderZone;
        publishQuota(slot, quota);
    }

    allocationState.fairnessCursor =
        (allocationState.fairnessCursor + 1) % kMaxInstances;
    allocationState.lastAllocationMs = now;
    allocationState.clockInitialised = true;
    return finish(TickResult::Published);
}

GlobalConductorHub::AudioSnapshot GlobalConductorHub::readAudioSnapshot (
    Handle handle, std::uint32_t now, LocalPolicy local) const noexcept
{
    AudioSnapshot result;
    result.attackQuota = clamp(local.attackQuota, 0, kMaxQuotaPerZone);
    result.voiceQuota = clamp(local.voiceQuota, 0, kMaxQuotaPerZone);

    if (! isRegistered(handle))
        return result;

    const auto& slot = slots_[(std::size_t) handle.slot];
    result.role = sanitiseRole((Role) slot.role.load(std::memory_order_acquire));
    if (result.role == Role::Off)
    {
        result.source = SnapshotSource::Bypassed;
        return result;
    }

    const auto group = slot.groupKey.load(std::memory_order_acquire);
    const auto leader = electedLeaderForGroup(group);
    result.electedLeader = leader == handle;
    if (! leader.isValid())
        return result;

    QuotaPublication quota;
    if (! readQuota(slot, quota)
        || quota.valid == 0
        || quota.registrationGeneration != handle.generation
        || quota.groupKey != group
        || quota.leaderSlot != (int) leader.slot
        || quota.leaderGeneration != leader.generation
        || ! isFresh(now, quota.allocationTimestampMs, kHeartbeatTimeoutMs)
        || ! isFresh(now, quota.leaderTimestampMs, kHeartbeatTimeoutMs)
        || ! isRegistered(handle))
        return result;

    result.source = SnapshotSource::Global;
    result.attackQuota = clamp(quota.attackQuota, 0, kMaxQuotaPerZone);
    result.voiceQuota = clamp(quota.voiceQuota, 0, kMaxQuotaPerZone);
    result.globalAttackBudget = clamp(quota.globalAttackBudget, 0,
                                      kMaxGlobalBudget);
    result.globalVoiceBudget = clamp(quota.globalVoiceBudget, 0,
                                     kMaxGlobalBudget);
    result.zoneDensity = clamp(quota.zoneDensity, 0, kMaxZoneDensity);
    result.activeZoneCount = clamp(quota.activeZoneCount, 0, kMaxInstances);
    result.leaderUdpPort = clamp(quota.leaderUdpPort, 1, 65535);
    result.leaderZoneKey = quota.leaderZoneKey;
    result.allocationEpoch = quota.epoch;
    return result;
}

bool GlobalConductorHub::isRegistered (Handle handle) const noexcept
{
    if (! handle.isValid())
        return false;
    const auto& slot = slots_[(std::size_t) handle.slot];
    return slot.state.load(std::memory_order_acquire) == activeSlot
        && slot.generation.load(std::memory_order_acquire) == handle.generation;
}

GlobalConductorHub::Handle GlobalConductorHub::electedLeader() const noexcept
{
    Handle best;
    int bestPort = std::numeric_limits<int>::max();
    for (int index = 0; index < kMaxInstances; ++index)
    {
        const auto& slot = slots_[(std::size_t) index];
        if (slot.state.load(std::memory_order_acquire) != activeSlot
            || sanitiseRole((Role) slot.role.load(std::memory_order_acquire))
                 != Role::Leader)
            continue;

        const int port = slot.udpPort.load(std::memory_order_relaxed);
        if (port < bestPort || (port == bestPort && index < (int) best.slot))
        {
            bestPort = port;
            best.slot = (std::uint8_t) index;
            best.generation = slot.generation.load(std::memory_order_relaxed);
        }
    }

    if (! best.isValid())
        return {};
    const auto& selected = slots_[(std::size_t) best.slot];
    if (selected.state.load(std::memory_order_acquire) != activeSlot
        || selected.generation.load(std::memory_order_acquire) != best.generation
        || sanitiseRole((Role) selected.role.load(std::memory_order_acquire))
             != Role::Leader)
        return {};
    return best;
}

GlobalConductorHub::Handle GlobalConductorHub::electedLeaderForGroup (
    std::uint32_t groupKey) const noexcept
{
    Handle best;
    int bestPort = std::numeric_limits<int>::max();
    for (int index = 0; index < kMaxInstances; ++index)
    {
        const auto& slot = slots_[(std::size_t) index];
        if (slot.state.load(std::memory_order_acquire) != activeSlot
            || slot.groupKey.load(std::memory_order_relaxed) != groupKey
            || sanitiseRole((Role) slot.role.load(std::memory_order_acquire))
                 != Role::Leader)
            continue;

        const int port = slot.udpPort.load(std::memory_order_relaxed);
        if (port < bestPort || (port == bestPort && index < (int) best.slot))
        {
            bestPort = port;
            best.slot = (std::uint8_t) index;
            best.generation = slot.generation.load(std::memory_order_relaxed);
        }
    }

    if (! best.isValid())
        return {};
    const auto& selected = slots_[(std::size_t) best.slot];
    if (selected.state.load(std::memory_order_acquire) != activeSlot
        || selected.generation.load(std::memory_order_acquire) != best.generation
        || selected.groupKey.load(std::memory_order_relaxed) != groupKey
        || sanitiseRole((Role) selected.role.load(std::memory_order_acquire))
             != Role::Leader)
        return {};
    return best;
}

std::uint32_t GlobalConductorHub::millisecondsFromSeconds (double seconds) noexcept
{
    if (! std::isfinite(seconds) || seconds < 0.0)
        return 0;
    constexpr double wrapSeconds = 4294967.296; // 2^32 milliseconds
    const double wrapped = std::fmod(seconds, wrapSeconds);
    return (std::uint32_t) std::floor(wrapped * 1000.0);
}

GlobalConductorHub::Role GlobalConductorHub::sanitiseRole (Role role) noexcept
{
    switch (role)
    {
        case Role::Off:
        case Role::Follower:
        case Role::Leader:
            return role;
    }
    return Role::Off;
}

int GlobalConductorHub::clamp (int value, int low, int high) noexcept
{
    return std::max(low, std::min(high, value));
}

bool GlobalConductorHub::isFresh (std::uint32_t now,
                                  std::uint32_t timestamp,
                                  std::uint32_t timeout) noexcept
{
    return now - timestamp <= timeout;
}

std::uint32_t GlobalConductorHub::nextGeneration (std::uint32_t value) noexcept
{
    ++value;
    return value == 0 ? 1 : value;
}

int GlobalConductorHub::cyclicRank (int slot, int cursor) noexcept
{
    return (slot - cursor + kMaxInstances) % kMaxInstances;
}

void GlobalConductorHub::lockAdministration() noexcept
{
    while (administrationLock_.test_and_set(std::memory_order_acquire)) {}
}

void GlobalConductorHub::unlockAdministration() noexcept
{
    administrationLock_.clear(std::memory_order_release);
}

bool GlobalConductorHub::readDensity (const Slot& slot,
                                      DensityPublication& output) const noexcept
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const auto before = slot.densitySequence.load(std::memory_order_acquire);
        if ((before & 1u) != 0)
            continue;
        DensityPublication candidate;
        candidate.generation = slot.densityGeneration.load(std::memory_order_relaxed);
        candidate.timestampMs = slot.densityTimestampMs.load(std::memory_order_relaxed);
        candidate.density = slot.density.load(std::memory_order_relaxed);
        const auto after = slot.densitySequence.load(std::memory_order_acquire);
        if (before == after && (after & 1u) == 0)
        {
            output = candidate;
            return true;
        }
    }
    return false;
}

bool GlobalConductorHub::readLeaderPublication (
    const Slot& slot, LeaderPublication& output) const noexcept
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const auto before = slot.leaderSequence.load(std::memory_order_acquire);
        if ((before & 1u) != 0)
            continue;
        LeaderPublication candidate;
        candidate.generation = slot.leaderGeneration.load(std::memory_order_relaxed);
        candidate.timestampMs = slot.leaderTimestampMs.load(std::memory_order_relaxed);
        candidate.attackBudget = slot.globalAttackBudget.load(std::memory_order_relaxed);
        candidate.voiceBudget = slot.globalVoiceBudget.load(std::memory_order_relaxed);
        const auto after = slot.leaderSequence.load(std::memory_order_acquire);
        if (before == after && (after & 1u) == 0)
        {
            output = candidate;
            return true;
        }
    }
    return false;
}

bool GlobalConductorHub::readQuota (const Slot& slot,
                                    QuotaPublication& output) const noexcept
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const auto before = slot.quotaSequence.load(std::memory_order_acquire);
        if ((before & 1u) != 0)
            continue;
        QuotaPublication candidate;
        candidate.registrationGeneration =
            slot.quotaRegistrationGeneration.load(std::memory_order_relaxed);
        candidate.leaderGeneration =
            slot.quotaLeaderGeneration.load(std::memory_order_relaxed);
        candidate.allocationTimestampMs =
            slot.quotaAllocationTimestampMs.load(std::memory_order_relaxed);
        candidate.leaderTimestampMs =
            slot.quotaLeaderTimestampMs.load(std::memory_order_relaxed);
        candidate.epoch = slot.quotaEpoch.load(std::memory_order_relaxed);
        candidate.groupKey = slot.quotaGroupKey.load(std::memory_order_relaxed);
        candidate.valid = slot.quotaValid.load(std::memory_order_relaxed);
        candidate.attackQuota = slot.attackQuota.load(std::memory_order_relaxed);
        candidate.voiceQuota = slot.voiceQuota.load(std::memory_order_relaxed);
        candidate.globalAttackBudget =
            slot.quotaGlobalAttackBudget.load(std::memory_order_relaxed);
        candidate.globalVoiceBudget =
            slot.quotaGlobalVoiceBudget.load(std::memory_order_relaxed);
        candidate.zoneDensity = slot.quotaZoneDensity.load(std::memory_order_relaxed);
        candidate.activeZoneCount =
            slot.quotaActiveZoneCount.load(std::memory_order_relaxed);
        candidate.leaderSlot = slot.quotaLeaderSlot.load(std::memory_order_relaxed);
        candidate.leaderUdpPort =
            slot.quotaLeaderUdpPort.load(std::memory_order_relaxed);
        candidate.leaderZoneKey =
            slot.quotaLeaderZoneKey.load(std::memory_order_relaxed);
        const auto after = slot.quotaSequence.load(std::memory_order_acquire);
        if (before == after && (after & 1u) == 0)
        {
            output = candidate;
            return true;
        }
    }
    return false;
}

void GlobalConductorHub::publishQuota (Slot& slot,
                                       const QuotaPublication& quota) noexcept
{
    slot.quotaSequence.fetch_add(1, std::memory_order_acq_rel);
    slot.quotaRegistrationGeneration.store(quota.registrationGeneration,
                                            std::memory_order_relaxed);
    slot.quotaLeaderGeneration.store(quota.leaderGeneration,
                                     std::memory_order_relaxed);
    slot.quotaAllocationTimestampMs.store(quota.allocationTimestampMs,
                                          std::memory_order_relaxed);
    slot.quotaLeaderTimestampMs.store(quota.leaderTimestampMs,
                                      std::memory_order_relaxed);
    slot.quotaEpoch.store(quota.epoch, std::memory_order_relaxed);
    slot.quotaGroupKey.store(quota.groupKey, std::memory_order_relaxed);
    slot.quotaValid.store(quota.valid, std::memory_order_relaxed);
    slot.attackQuota.store(quota.attackQuota, std::memory_order_relaxed);
    slot.voiceQuota.store(quota.voiceQuota, std::memory_order_relaxed);
    slot.quotaGlobalAttackBudget.store(quota.globalAttackBudget,
                                       std::memory_order_relaxed);
    slot.quotaGlobalVoiceBudget.store(quota.globalVoiceBudget,
                                      std::memory_order_relaxed);
    slot.quotaZoneDensity.store(quota.zoneDensity, std::memory_order_relaxed);
    slot.quotaActiveZoneCount.store(quota.activeZoneCount,
                                    std::memory_order_relaxed);
    slot.quotaLeaderSlot.store(quota.leaderSlot, std::memory_order_relaxed);
    slot.quotaLeaderUdpPort.store(quota.leaderUdpPort,
                                  std::memory_order_relaxed);
    slot.quotaLeaderZoneKey.store(quota.leaderZoneKey,
                                  std::memory_order_relaxed);
    slot.quotaSequence.fetch_add(1, std::memory_order_release);
}

void GlobalConductorHub::allocateBudget (
    const std::array<Candidate, kMaxInstances>& candidates,
    int candidateCount, int requestedBudget, int cursor,
    std::array<int, kMaxInstances>& quotas) const noexcept
{
    quotas.fill(0);
    candidateCount = clamp(candidateCount, 0, kMaxInstances);
    int remaining = clamp(requestedBudget, 0,
                          candidateCount * kMaxQuotaPerZone);
    if (candidateCount == 0 || remaining == 0)
        return;

    // When the budget cannot cover every zone, rotate the scarce unit service
    // by slot each tick. This prevents a low-numbered instance from permanently
    // winning merely because it registered first.
    if (remaining < candidateCount)
    {
        for (int offset = 0; offset < kMaxInstances && remaining > 0; ++offset)
        {
            const int wantedSlot = (cursor + offset) % kMaxInstances;
            for (int index = 0; index < candidateCount; ++index)
            {
                if (candidates[(std::size_t) index].slot != wantedSlot)
                    continue;
                quotas[(std::size_t) wantedSlot] = 1;
                --remaining;
                break;
            }
        }
        return;
    }

    for (int index = 0; index < candidateCount; ++index)
        quotas[(std::size_t) candidates[(std::size_t) index].slot] = 1;
    remaining -= candidateCount;

    // Density-weighted D'Hondt allocation with a rotated deterministic tie
    // break. A +1 weight leaves an empty but live zone minimally reachable.
    while (remaining-- > 0)
    {
        int best = -1;
        for (int index = 0; index < candidateCount; ++index)
        {
            const auto& item = candidates[(std::size_t) index];
            const int quota = quotas[(std::size_t) item.slot];
            if (quota >= kMaxQuotaPerZone)
                continue;
            if (best < 0)
            {
                best = index;
                continue;
            }

            const auto& incumbent = candidates[(std::size_t) best];
            const int incumbentQuota = quotas[(std::size_t) incumbent.slot];
            const std::int64_t challengerScore =
                (std::int64_t) (item.density + 1) * (incumbentQuota + 1);
            const std::int64_t incumbentScore =
                (std::int64_t) (incumbent.density + 1) * (quota + 1);
            if (challengerScore > incumbentScore
                || (challengerScore == incumbentScore
                    && cyclicRank(item.slot, cursor)
                         < cyclicRank(incumbent.slot, cursor)))
                best = index;
        }

        if (best < 0)
            break;
        ++quotas[(std::size_t) candidates[(std::size_t) best].slot];
    }
}
