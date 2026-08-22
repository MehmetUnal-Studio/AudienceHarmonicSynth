#include "../Source/GlobalConductorHub.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <thread>
#include <type_traits>
#include <utility>

namespace
{
    using Hub = GlobalConductorHub;

    int failures = 0;
    bool watchAllocations = false;
    std::size_t watchedAllocations = 0;

    void expect (bool condition, const char* name)
    {
        std::cout << (condition ? "PASS  " : "FAIL  ") << name << '\n';
        if (! condition)
            ++failures;
    }

    Hub::RegistrationResult add (Hub& hub, int port, std::uint32_t zone,
                                 Hub::Role role,
                                 std::uint32_t group = 0)
    {
        return hub.registerInstance({ port, zone, role, group });
    }

    int quotaSum (const std::array<Hub::AudioSnapshot, 3>& snapshots,
                  bool attacks)
    {
        int sum = 0;
        for (const auto& item : snapshots)
            sum += attacks ? item.attackQuota : item.voiceQuota;
        return sum;
    }
}

void* operator new (std::size_t size)
{
    if (watchAllocations)
        ++watchedAllocations;
    if (void* memory = std::malloc(size))
        return memory;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)
{
    if (watchAllocations)
        ++watchedAllocations;
    if (void* memory = std::malloc(size))
        return memory;
    throw std::bad_alloc();
}

void operator delete (void* memory) noexcept { std::free(memory); }
void operator delete[] (void* memory) noexcept { std::free(memory); }
void operator delete (void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[] (void* memory, std::size_t) noexcept { std::free(memory); }

int main()
{
    static_assert(std::is_nothrow_default_constructible<Hub>::value,
                  "hub construction must be realtime-safe");
    static_assert(noexcept(std::declval<Hub&>().publishZoneDensity(
                      std::declval<Hub::Handle>(), 0, 0)),
                  "density publication must remain noexcept");
    static_assert(noexcept(std::declval<Hub&>().allocationTick(
                      std::declval<Hub::Handle>(), 0)),
                  "allocation tick must remain noexcept");
    static_assert(noexcept(std::declval<const Hub&>().readAudioSnapshot(
                      std::declval<Hub::Handle>(), 0,
                      std::declval<Hub::LocalPolicy>())),
                  "audio snapshot read must remain noexcept");
    static_assert(Hub::kMaxInstances == 16, "fixed slot count changed");

    // Registration is fixed-capacity and port identity is unambiguous. Leader
    // election is independent of construction order: the lowest live UDP port
    // wins, while hostile role values become an intentional Off registration.
    {
        Hub hub;
        const auto invalidLow = add(hub, 0, 1, Hub::Role::Follower);
        const auto invalidHigh = add(hub, 70000, 2, Hub::Role::Follower);
        const auto leaderB = add(hub, 6061, 0x42, Hub::Role::Leader);
        const auto leaderA = add(hub, 6060, 0x41, Hub::Role::Leader);
        const auto duplicate = add(hub, 6060, 0x99, Hub::Role::Follower);
        const auto hostileRole = add(hub, 6062, 0,
                                     static_cast<Hub::Role>(255));

        const auto bypassed = hub.readAudioSnapshot(hostileRole.handle, 0,
                                                     { -50, 999 });
        expect(invalidLow.status == Hub::RegistrationStatus::InvalidPort
                   && invalidHigh.status == Hub::RegistrationStatus::InvalidPort
                   && leaderA.status == Hub::RegistrationStatus::Registered
                   && leaderB.status == Hub::RegistrationStatus::Registered
                   && duplicate.status == Hub::RegistrationStatus::DuplicatePort
                   && duplicate.conflictingHandle == leaderA.handle
                   && hub.electedLeader() == leaderA.handle,
               "port identity, duplicate detection and deterministic leader election");
        expect(hostileRole.status == Hub::RegistrationStatus::Registered
                   && bypassed.source == Hub::SnapshotSource::Bypassed
                   && bypassed.role == Hub::Role::Off
                   && bypassed.attackQuota == 0 && bypassed.voiceQuota == 16
                   && ! hub.publishZoneDensity(hostileRole.handle, 20, 0)
                   && ! hub.publishLeaderBudgets(leaderB.handle, 8, 16, 0),
               "hostile role sanitizes to Off and non-elected leader cannot publish");
    }

    // Each conductor group elects and allocates independently. Group B's lower
    // UDP ports intentionally win the legacy global diagnostic election, while
    // Group A must still accept its own leader and publish at the same 10 Hz
    // instant without sharing candidates, budgets, telemetry or cadence.
    {
        Hub hub;
        constexpr std::uint32_t groupA = 0x0a;
        constexpr std::uint32_t groupB = 0x0b;
        const auto backupA = add(hub, 6061, 0xa1, Hub::Role::Leader,
                                 groupA).handle;
        const auto leaderA = add(hub, 6060, 0xa0, Hub::Role::Leader,
                                 groupA).handle;
        const auto followerA = add(hub, 6062, 0xa2, Hub::Role::Follower,
                                   groupA).handle;
        const auto backupB = add(hub, 5051, 0xb1, Hub::Role::Leader,
                                 groupB).handle;
        const auto leaderB = add(hub, 5050, 0xb0, Hub::Role::Leader,
                                 groupB).handle;
        const auto followerB = add(hub, 5052, 0xb2, Hub::Role::Follower,
                                   groupB).handle;
        const std::array<Hub::Handle, 3> handlesA {{
            backupA, leaderA, followerA
        }};
        const std::array<Hub::Handle, 3> handlesB {{
            backupB, leaderB, followerB
        }};

        for (const auto handle : handlesA)
            hub.publishZoneDensity(handle, 10, 0);
        for (const auto handle : handlesB)
            hub.publishZoneDensity(handle, 10, 0);

        const bool backupABlocked =
            ! hub.publishLeaderBudgets(backupA, 99, 99, 0);
        const bool backupBBlocked =
            ! hub.publishLeaderBudgets(backupB, 99, 99, 0);
        const bool budgetA = hub.publishLeaderBudgets(leaderA, 3, 6, 0);
        const bool budgetB = hub.publishLeaderBudgets(leaderB, 6, 3, 0);
        const auto tickA = hub.allocationTick(leaderA, 0);
        const auto tickB = hub.allocationTick(leaderB, 0);

        std::array<Hub::AudioSnapshot, 3> viewsA {};
        std::array<Hub::AudioSnapshot, 3> viewsB {};
        for (std::size_t index = 0; index < handlesA.size(); ++index)
        {
            viewsA[index] = hub.readAudioSnapshot(handlesA[index], 0, { 7, 9 });
            viewsB[index] = hub.readAudioSnapshot(handlesB[index], 0, { 7, 9 });
        }

        expect(hub.electedLeader() == leaderB && backupABlocked
                   && backupBBlocked && budgetA && budgetB
                   && tickA == Hub::TickResult::Published
                   && tickB == Hub::TickResult::Published,
               "independent groups elect their own lowest-port leader and tick together");
        expect(viewsA[0].source == Hub::SnapshotSource::Global
                   && viewsA[1].source == Hub::SnapshotSource::Global
                   && viewsA[2].source == Hub::SnapshotSource::Global
                   && quotaSum(viewsA, true) == 3
                   && quotaSum(viewsA, false) == 6
                   && viewsA[0].globalAttackBudget == 3
                   && viewsA[0].globalVoiceBudget == 6
                   && viewsA[0].activeZoneCount == 3
                   && viewsA[0].leaderUdpPort == 6060
                   && viewsA[0].leaderZoneKey == 0xa0
                   && ! viewsA[0].electedLeader
                   && viewsA[1].electedLeader,
               "group A quota and leader telemetry exclude group B");
        expect(viewsB[0].source == Hub::SnapshotSource::Global
                   && viewsB[1].source == Hub::SnapshotSource::Global
                   && viewsB[2].source == Hub::SnapshotSource::Global
                   && quotaSum(viewsB, true) == 6
                   && quotaSum(viewsB, false) == 3
                   && viewsB[0].globalAttackBudget == 6
                   && viewsB[0].globalVoiceBudget == 3
                   && viewsB[0].activeZoneCount == 3
                   && viewsB[0].leaderUdpPort == 5050
                   && viewsB[0].leaderZoneKey == 0xb0
                   && ! viewsB[0].electedLeader
                   && viewsB[1].electedLeader,
               "group B quota and leader telemetry exclude group A");

        const bool removedB = hub.unregisterInstance(leaderB);
        const auto stillGlobalA = hub.readAudioSnapshot(followerA, 1, { 7, 9 });
        const auto invalidatedB = hub.readAudioSnapshot(followerB, 1, { 7, 9 });
        expect(removedB && stillGlobalA.source == Hub::SnapshotSource::Global
                   && stillGlobalA.leaderUdpPort == 6060
                   && invalidatedB.source == Hub::SnapshotSource::LocalFallback,
               "leader lifetime changes invalidate only their own conductor group");
    }

    // Exactly sixteen scalar slots are available, with no hidden overflow
    // allocation. The seventeenth instance fails explicitly.
    {
        Hub hub;
        std::array<Hub::Handle, Hub::kMaxInstances> handles {};
        bool registered = true;
        for (int index = 0; index < Hub::kMaxInstances; ++index)
        {
            const auto result = add(hub, 7000 + index, (std::uint32_t) index,
                                    Hub::Role::Follower);
            registered = registered
                      && result.status == Hub::RegistrationStatus::Registered;
            handles[(std::size_t) index] = result.handle;
        }
        const auto overflow = add(hub, 8000, 99, Hub::Role::Follower);
        bool allLive = true;
        for (const auto handle : handles)
            allLive = allLive && hub.isRegistered(handle);
        expect(registered && allLive
                   && overflow.status == Hub::RegistrationStatus::CapacityFull
                   && ! overflow.handle.isValid(),
               "sixteen fixed slots reject a seventeenth instance without allocation");
    }

    // Equal-density zones demonstrate both quota phases: scarce service rotates
    // across ticks, then remaining budget is distributed deterministically.
    {
        Hub hub;
        const auto leader = add(hub, 6060, 0x41, Hub::Role::Leader).handle;
        const auto followerB = add(hub, 6061, 0x42, Hub::Role::Follower).handle;
        const auto followerC = add(hub, 6062, 0x43, Hub::Role::Follower).handle;
        const std::array<Hub::Handle, 3> handles {{ leader, followerB, followerC }};
        for (const auto handle : handles)
            hub.publishZoneDensity(handle, 10, 0);
        const bool publishedBudgets = hub.publishLeaderBudgets(leader, 2, 5, 0);
        const auto firstTick = hub.allocationTick(leader, 0);
        std::array<Hub::AudioSnapshot, 3> first {};
        for (std::size_t index = 0; index < handles.size(); ++index)
            first[index] = hub.readAudioSnapshot(handles[index], 0, { 7, 9 });

        expect(publishedBudgets && firstTick == Hub::TickResult::Published
                   && first[0].source == Hub::SnapshotSource::Global
                   && first[1].source == Hub::SnapshotSource::Global
                   && first[2].source == Hub::SnapshotSource::Global
                   && first[0].attackQuota == 1
                   && first[1].attackQuota == 1
                   && first[2].attackQuota == 0
                   && first[0].voiceQuota == 2
                   && first[1].voiceQuota == 2
                   && first[2].voiceQuota == 1
                   && quotaSum(first, true) == 2 && quotaSum(first, false) == 5,
               "first 10 Hz tick publishes exact fair quotas within both budgets");
        expect(first[0].electedLeader && ! first[1].electedLeader
                   && first[1].activeZoneCount == 3
                   && first[1].globalAttackBudget == 2
                   && first[1].globalVoiceBudget == 5
                   && first[1].leaderUdpPort == 6060
                   && first[1].leaderZoneKey == 0x41
                   && first[1].zoneDensity == 10,
               "one coherent audio snapshot carries leader, budget and zone telemetry");
        expect(hub.allocationTick(leader, 99) == Hub::TickResult::TooSoon,
               "allocation cadence rejects a tick before exactly 100 ms");

        for (const auto handle : handles)
            hub.publishZoneDensity(handle, 10, 100);
        hub.publishLeaderBudgets(leader, 2, 5, 100);
        const auto secondTick = hub.allocationTick(leader, 100);
        std::array<Hub::AudioSnapshot, 3> second {};
        for (std::size_t index = 0; index < handles.size(); ++index)
            second[index] = hub.readAudioSnapshot(handles[index], 100, { 7, 9 });
        expect(secondTick == Hub::TickResult::Published
                   && second[0].attackQuota == 0
                   && second[1].attackQuota == 1
                   && second[2].attackQuota == 1
                   && second[0].voiceQuota == 1
                   && second[1].voiceQuota == 2
                   && second[2].voiceQuota == 2
                   && second[0].allocationEpoch != first[0].allocationEpoch,
               "scarce quotas and equal-weight remainders rotate on the next tick");

        hub.publishZoneDensity(leader, 1, 200);
        hub.publishZoneDensity(followerB, 100, 200);
        hub.publishZoneDensity(followerC, 1, 200);
        hub.publishLeaderBudgets(leader, 9, 9, 200);
        hub.allocationTick(leader, 200);
        std::array<Hub::AudioSnapshot, 3> weighted {};
        for (std::size_t index = 0; index < handles.size(); ++index)
            weighted[index] = hub.readAudioSnapshot(handles[index], 200, { 7, 9 });
        expect(weighted[0].attackQuota == 1
                   && weighted[1].attackQuota == 7
                   && weighted[2].attackQuota == 1
                   && weighted[0].voiceQuota == 1
                   && weighted[1].voiceQuota == 7
                   && weighted[2].voiceQuota == 1,
               "remaining capacity is density-weighted without starving live zones");

        const auto exactTimeout = hub.readAudioSnapshot(followerB, 1700, { 7, 9 });
        const auto expired = hub.readAudioSnapshot(followerB, 1701, { 7, 9 });
        expect(exactTimeout.source == Hub::SnapshotSource::Global
                   && expired.source == Hub::SnapshotSource::LocalFallback
                   && expired.attackQuota == 7 && expired.voiceQuota == 9,
               "leader/allocation heartbeat expires after the exact 1500 ms boundary");

        expect(hub.unregisterInstance(leader)
                   && hub.readAudioSnapshot(followerB, 201, { 6, 8 }).source
                        == Hub::SnapshotSource::LocalFallback,
               "leader lifetime ends without leaving followers on stale global state");
    }

    // A stale generation can neither publish nor unregister a recycled slot.
    // The hub stores no instance address, so reuse cannot become a UAF/ABA.
    {
        Hub hub;
        const auto old = add(hub, 9000, 1, Hub::Role::Leader).handle;
        const bool removed = hub.unregisterInstance(old);
        const auto current = add(hub, 9001, 2, Hub::Role::Leader).handle;
        const auto staleRead = hub.readAudioSnapshot(old, 0, { 3, 5 });
        expect(removed && old.slot == current.slot
                   && old.generation != current.generation
                   && ! hub.publishZoneDensity(old, 256, 0)
                   && ! hub.publishLeaderBudgets(old, 256, 256, 0)
                   && ! hub.unregisterInstance(old)
                   && hub.isRegistered(current)
                   && staleRead.source == Hub::SnapshotSource::LocalFallback
                   && staleRead.attackQuota == 3 && staleRead.voiceQuota == 5,
               "generation-tagged slot reuse rejects stale cross-instance handles");
    }

    // Hostile scalar publications are bounded. A zero budget is authoritative,
    // while excess global voice capacity stops at sixteen per live zone.
    {
        Hub hub;
        const auto leader = add(hub, 9100, 1, Hub::Role::Leader).handle;
        const auto follower = add(hub, 9101, 2, Hub::Role::Follower).handle;
        hub.publishZoneDensity(leader, std::numeric_limits<int>::min(), 0);
        hub.publishZoneDensity(follower, std::numeric_limits<int>::max(), 0);
        hub.publishLeaderBudgets(leader, std::numeric_limits<int>::min(),
                                 std::numeric_limits<int>::max(), 0);
        const auto tick = hub.allocationTick(leader, 0);
        const auto leaderView = hub.readAudioSnapshot(leader, 0, { -1, 999 });
        const auto followerView = hub.readAudioSnapshot(follower, 0, { -1, 999 });
        expect(tick == Hub::TickResult::Published
                   && leaderView.source == Hub::SnapshotSource::Global
                   && leaderView.zoneDensity == 0
                   && followerView.zoneDensity == 256
                   && leaderView.globalAttackBudget == 0
                   && leaderView.globalVoiceBudget == Hub::kMaxGlobalBudget
                   && leaderView.attackQuota == 0 && followerView.attackQuota == 0
                   && leaderView.voiceQuota == 16 && followerView.voiceQuota == 16,
               "hostile density and budget values sanitize to fixed safe bounds");

        const auto invalidFallback = hub.readAudioSnapshot({}, 0, { -99, 999 });
        expect(invalidFallback.source == Hub::SnapshotSource::LocalFallback
                   && invalidFallback.attackQuota == 0
                   && invalidFallback.voiceQuota == 16
                   && Hub::millisecondsFromSeconds(-1.0) == 0
                   && Hub::millisecondsFromSeconds(
                        std::numeric_limits<double>::quiet_NaN()) == 0
                   && Hub::millisecondsFromSeconds(
                        std::numeric_limits<double>::infinity()) == 0
                   && Hub::millisecondsFromSeconds(1.2349) == 1234,
               "hostile fallback policy and floating clocks remain finite and bounded");
    }

    // uint32 timestamps intentionally support the natural 49.7-day wrap while
    // preserving both 100 ms cadence and heartbeat freshness.
    {
        Hub hub;
        const auto leader = add(hub, 9200, 1, Hub::Role::Leader).handle;
        const auto follower = add(hub, 9201, 2, Hub::Role::Follower).handle;
        constexpr std::uint32_t beforeWrap =
            std::numeric_limits<std::uint32_t>::max() - 50u;
        hub.publishZoneDensity(leader, 10, beforeWrap);
        hub.publishZoneDensity(follower, 10, beforeWrap);
        hub.publishLeaderBudgets(leader, 2, 4, beforeWrap);
        const auto first = hub.allocationTick(leader, beforeWrap);
        const auto wrappedRead = hub.readAudioSnapshot(follower, 20, { 7, 9 });

        hub.publishZoneDensity(leader, 10, 50);
        hub.publishZoneDensity(follower, 10, 50);
        hub.publishLeaderBudgets(leader, 2, 4, 50);
        const auto wrappedTick = hub.allocationTick(leader, 50);
        expect(first == Hub::TickResult::Published
                   && wrappedRead.source == Hub::SnapshotSource::Global
                   && wrappedTick == Hub::TickResult::Published,
               "heartbeat and 10 Hz cadence remain correct across uint32 wrap");
    }

    // Writer/read stress is intentionally strict enough for TSan: every field
    // is atomic and every coherent publication is bounded by a sequence stamp.
    {
        Hub hub;
        const auto leader = add(hub, 9300, 1, Hub::Role::Leader).handle;
        const auto follower = add(hub, 9301, 2, Hub::Role::Follower).handle;
        std::atomic<std::uint32_t> publishedTime { 0 };
        std::atomic<bool> complete { false };
        std::atomic<bool> coherent { true };

        std::thread writer([&]
        {
            for (std::uint32_t tick = 0; tick < 4000; ++tick)
            {
                const std::uint32_t now = tick * Hub::kAllocationPeriodMs;
                hub.publishZoneDensity(leader, (int) (tick % 257), now);
                hub.publishZoneDensity(follower, (int) ((tick * 7) % 257), now);
                hub.publishLeaderBudgets(leader, 7, 13, now);
                hub.allocationTick(leader, now);
                publishedTime.store(now, std::memory_order_release);
            }
            complete.store(true, std::memory_order_release);
        });

        std::thread reader([&]
        {
            do
            {
                const auto now = publishedTime.load(std::memory_order_acquire);
                const auto view = hub.readAudioSnapshot(follower, now, { 4, 8 });
                coherent.store(coherent.load(std::memory_order_relaxed)
                               && view.attackQuota >= 0 && view.attackQuota <= 16
                               && view.voiceQuota >= 0 && view.voiceQuota <= 16
                               && view.zoneDensity >= 0 && view.zoneDensity <= 256
                               && view.activeZoneCount >= 0
                               && view.activeZoneCount <= Hub::kMaxInstances
                               && (view.source != Hub::SnapshotSource::Global
                                   || (view.globalAttackBudget == 7
                                       && view.globalVoiceBudget == 13
                                       && view.leaderUdpPort == 9300)),
                               std::memory_order_relaxed);
            }
            while (! complete.load(std::memory_order_acquire));
        });

        writer.join();
        reader.join();
        expect(coherent.load(std::memory_order_relaxed),
               "concurrent publications expose only bounded coherent audio snapshots");
    }

    // The complete intended audio path, including the O(16) fair allocation,
    // performs no heap allocation.
    {
        Hub hub;
        const auto leader = add(hub, 9400, 1, Hub::Role::Leader).handle;
        const auto follower = add(hub, 9401, 2, Hub::Role::Follower).handle;
        watchedAllocations = 0;
        watchAllocations = true;
        const bool leaderDensity = hub.publishZoneDensity(leader, 64, 0);
        const bool followerDensity = hub.publishZoneDensity(follower, 64, 0);
        const bool budgets = hub.publishLeaderBudgets(leader, 8, 16, 0);
        const auto tick = hub.allocationTick(leader, 0);
        const auto snapshot = hub.readAudioSnapshot(follower, 0, { 4, 8 });
        watchAllocations = false;
        expect(watchedAllocations == 0 && leaderDensity && followerDensity
                   && budgets && tick == Hub::TickResult::Published
                   && snapshot.source == Hub::SnapshotSource::Global,
               "publication, fair allocation and audio snapshot read allocate nothing");
    }

    if (failures != 0)
        std::cerr << failures << " GlobalConductorHub test(s) failed\n";
    return failures == 0 ? 0 : 1;
}
