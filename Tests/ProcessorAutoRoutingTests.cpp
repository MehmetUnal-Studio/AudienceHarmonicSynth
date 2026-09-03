#include "../Source/PluginProcessor.h"
#include "../Source/PluginStateMigration.h"

#include <atomic>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#if JUCE_MAC || JUCE_LINUX
 #include <arpa/inet.h>
 #include <netinet/in.h>
 #include <sys/socket.h>
 #include <unistd.h>
#endif

struct ProcessorAutoRoutingTestAccess
{
    static std::uint32_t restoreIntentCount (
        const AudienceProcessor& processor) noexcept
    {
        return processor.hostStateRestoreIntentCount.load (
            std::memory_order_acquire);
    }

    static void beginSyntheticRestoreIntent (
        AudienceProcessor& processor) noexcept
    {
        processor.hostStateRestoreIntentCount.fetch_add (
            1, std::memory_order_acq_rel);
    }

    static void endSyntheticRestoreIntent (
        AudienceProcessor& processor) noexcept
    {
        processor.hostStateRestoreIntentCount.fetch_sub (
            1, std::memory_order_acq_rel);
    }

    static bool runFreshAssignmentPhase (AudienceProcessor& processor)
    {
        return processor.handleFreshRouteAssignment();
    }

    static void dispatchPendingRouteHandoff (AudienceProcessor& processor)
    {
        processor.handleUpdateNowIfNeeded();
    }
};

namespace
{
    int failures = 0;

    void expect (bool condition, const char* name)
    {
        std::cout << (condition ? "PASS  " : "FAIL  ") << name << '\n';
        if (! condition)
            ++failures;
    }

    int parameterInt (AudienceProcessor& processor, const char* id)
    {
        if (const auto* value = processor.apvts.getRawParameterValue (id))
            return juce::roundToInt (
                value->load (std::memory_order_relaxed));
        return std::numeric_limits<int>::lowest();
    }

#if JUCE_MAC || JUCE_LINUX
    class RawUdpPortOwner final
    {
    public:
        explicit RawUdpPortOwner (int port)
        {
            descriptor = ::socket (AF_INET, SOCK_DGRAM, 0);
            if (descriptor < 0)
                return;

            sockaddr_in address {};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl (INADDR_ANY);
            address.sin_port = htons (static_cast<std::uint16_t> (port));
            if (::bind (descriptor,
                        reinterpret_cast<const sockaddr*> (&address),
                        sizeof (address)) != 0)
            {
                ::close (descriptor);
                descriptor = -1;
            }
        }

        ~RawUdpPortOwner()
        {
            if (descriptor >= 0)
                ::close (descriptor);
        }

        bool isBound() const noexcept { return descriptor >= 0; }

    private:
        int descriptor = -1;
    };
#endif

    bool waitUntil (const std::function<bool()>& predicate,
                    int timeoutMs = 2500)
    {
        const auto started = juce::Time::getMillisecondCounter();
        do
        {
            juce::Timer::callPendingTimersSynchronously();
            if (predicate())
                return true;
            juce::Thread::sleep (15);
        }
        while (static_cast<std::uint32_t> (
                   juce::Time::getMillisecondCounter() - started)
               < static_cast<std::uint32_t> (timeoutMs));
        return predicate();
    }

    bool waitForAssignedRoute (AudienceProcessor& processor, int presetIndex)
    {
        const auto preset = CosmicFactoryPresets::zonePreset (presetIndex);
        return waitUntil ([&]
        {
            return processor.getFreshRouteAssignmentState()
                       == AudienceProcessor::FreshRouteAssignmentState::assigned
                && processor.getUdpPort() == preset.udpPort
                && processor.osc.isRunning()
                && processor.osc.isReceiving()
                && processor.osc.isExclusive()
                && processor.osc.getCurrentPort() == preset.udpPort
                && processor.getExpectedZone() == preset.index
                && parameterInt (processor, "expectedZone")
                    == preset.expectedZoneChoice
                && parameterInt (processor, "conductorRole")
                    == preset.conductorRoleChoice
                && processor.getMatchingFactoryPresetIndex() == preset.index
                && processor.getResolvedMidiOutputOptionIndex()
                    == CosmicFactoryPresets::midiOutputOption
                && processor.isMidiOutputReady()
                && processor.getConductorRegistrationStatus()
                    == static_cast<int> (
                        GlobalConductorHub::RegistrationStatus::Registered)
                && processor.getVirtualMidiPortName()
                    == "Cosmic Microwave " + juce::String (preset.udpPort)
                                           + " Out";
        });
    }

    juce::MemoryBlock saveState (AudienceProcessor& processor)
    {
        juce::MemoryBlock data;
        processor.getStateInformation (data);
        return data;
    }

    int savedUdpPort (const juce::MemoryBlock& data)
    {
        if (auto xml = juce::AudioProcessor::getXmlFromBinary (
                data.getData(), (int) data.getSize()))
            return (int) juce::ValueTree::fromXml (*xml).getProperty (
                "udpPort", -1);
        return -1;
    }

    juce::MemoryBlock withoutRootProperty (const juce::MemoryBlock& data,
                                            const char* propertyName)
    {
        juce::MemoryBlock result;
        if (auto xml = juce::AudioProcessor::getXmlFromBinary (
                data.getData(), (int) data.getSize()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            state.removeProperty (propertyName, nullptr);
            if (auto updatedXml = state.createXml())
                juce::AudioProcessor::copyXmlToBinary (*updatedXml, result);
        }
        return result;
    }

    juce::MemoryBlock makeCustomHostState (int udpPort,
                                            int parsePaddingBytes = 0)
    {
        auto stateTemplate = std::make_unique<AudienceProcessor>();
        auto state = stateTemplate->apvts.copyState();
        auto setParameter = [&] (const char* id, float value)
        {
            if (auto node = CosmicStateMigration::findParameterNode (state, id);
                node.isValid())
                node.setProperty ("value", value, nullptr);
        };
        setParameter ("midiOutputPath", 0.0f);
        setParameter ("expectedZone", 4.0f);
        setParameter ("conductorRole", 2.0f);
        state.setProperty ("udpPort", udpPort, nullptr);
        state.setProperty ("midiOutputOption", 0, nullptr);
        state.setProperty ("midiOutputRouteKind", 0, nullptr);
        state.setProperty ("midiOutputDeviceIdentifier", {}, nullptr);
        state.setProperty ("cosmicMicrowaveSchema",
                           CosmicStateMigration::currentSchema, nullptr);
        if (parsePaddingBytes > 0)
            state.setProperty (
                "restoreParsePadding",
                juce::String::repeatedString (
                    "x", parsePaddingBytes), nullptr);

        juce::MemoryBlock data;
        if (auto xml = state.createXml())
            juce::AudioProcessor::copyXmlToBinary (*xml, data);
        return data;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

#if JUCE_MAC || JUCE_LINUX
    {
        RawUdpPortOwner foreignOwner (CosmicFactoryPresets::firstUdpPort);
        auto processor = std::make_unique<AudienceProcessor>();
        expect (foreignOwner.isBound()
                    && waitForAssignedRoute (*processor, 1),
                "an OS-level foreign UDP 6062 owner makes the first fresh instance retain 6063 / Zone B");
    }
    juce::Timer::callPendingTimersSynchronously();
#endif

    {
        std::vector<std::unique_ptr<AudienceProcessor>> instances;
        for (int index = 0; index < 3; ++index)
        {
            instances.push_back (std::make_unique<AudienceProcessor>());
            expect (waitForAssignedRoute (*instances.back(), index),
                    index == 0 ? "first fresh instance claims 6062 / Zone A / Leader"
                    : index == 1 ? "second fresh instance claims 6063 / Zone B / Follower"
                                 : "third fresh instance claims 6064 / Zone C / Follower");
        }

        instances[1].reset();
        juce::Timer::callPendingTimersSynchronously();
        auto replacement = std::make_unique<AudienceProcessor>();
        expect (waitForAssignedRoute (*replacement, 1),
                "a released middle route is reused as 6063 / Zone B");
    }
    juce::Timer::callPendingTimersSynchronously();

    {
        constexpr int manualPort = 18124;
        auto processor = std::make_unique<AudienceProcessor>();
        processor->setUdpPort (manualPort);
        const bool manualPortReady = waitUntil ([&]
        {
            return processor->getFreshRouteAssignmentState()
                       == AudienceProcessor::FreshRouteAssignmentState::preserved
                && processor->getUdpPort() == manualPort
                && processor->osc.isReceiving()
                && processor->osc.getCurrentPort() == manualPort
                && processor->getExpectedZone()
                    == CosmicFactoryPresets::zonePreset (0).index
                && processor->isMidiOutputReady()
                && processor->getVirtualMidiPortName()
                    == "Cosmic Microwave " + juce::String (manualPort)
                                           + " Out";
        });
        expect (manualPortReady,
                "direct UDP edit before the first timer wins over automatic A-H assignment");
    }
    juce::Timer::callPendingTimersSynchronously();

    {
        auto processor = std::make_unique<AudienceProcessor>();
        constexpr char invalidState[] = "not-a-juce-plugin-state";
        processor->setStateInformation (invalidState,
                                        (int) sizeof (invalidState));
        expect (waitForAssignedRoute (*processor, 0),
                "an invalid host blob clears restore intent and preserves fresh auto assignment");
    }
    juce::Timer::callPendingTimersSynchronously();

    {
        auto processor = std::make_unique<AudienceProcessor>();
        ProcessorAutoRoutingTestAccess::beginSyntheticRestoreIntent (
            *processor);
        juce::Timer::callPendingTimersSynchronously();
        const bool quarantined =
            processor->getFreshRouteAssignmentState()
                == AudienceProcessor::FreshRouteAssignmentState::pending
            && ! processor->osc.isRunning()
            && ! processor->isMidiOutputReady();
        ProcessorAutoRoutingTestAccess::endSyntheticRestoreIntent (*processor);
        expect (quarantined && waitForAssignedRoute (*processor, 0),
                "restore intent quarantines the first timer without consuming Zone A");
    }
    juce::Timer::callPendingTimersSynchronously();

    {
        auto processor = std::make_unique<AudienceProcessor>();
        const bool assignmentReturnedEarly =
            ProcessorAutoRoutingTestAccess::runFreshAssignmentPhase (
                *processor);
        const bool retainedBeforeIntent =
            ! assignmentReturnedEarly
            && processor->getFreshRouteAssignmentState()
                == AudienceProcessor::FreshRouteAssignmentState::assigned
            && processor->osc.isReceiving();
        ProcessorAutoRoutingTestAccess::beginSyntheticRestoreIntent (
            *processor);
        const bool quarantinedReturn =
            ProcessorAutoRoutingTestAccess::runFreshAssignmentPhase (
                *processor);
        const bool retainedDuringUnvalidatedIntent = quarantinedReturn
            && processor->osc.isReceiving()
            && processor->osc.getCurrentPort()
                == CosmicFactoryPresets::zonePreset (0).udpPort;

        auto sibling = std::make_unique<AudienceProcessor>();
        const bool siblingUsedNextRoute = waitForAssignedRoute (*sibling, 1);
        ProcessorAutoRoutingTestAccess::endSyntheticRestoreIntent (*processor);
        expect (retainedBeforeIntent && retainedDuringUnvalidatedIntent
                    && siblingUsedNextRoute
                    && waitForAssignedRoute (*processor, 0),
                "an unvalidated/invalid restore intent cannot release an assigned fresh claim for a sibling to steal");
    }
    juce::Timer::callPendingTimersSynchronously();

    {
        constexpr int restoredPort = 18123;
        const auto workerState = makeCustomHostState (restoredPort, 512 * 1024);
        bool everyWorkerRestoreWon = true;
        bool everySiblingKeptZoneA = true;
        for (int iteration = 0; iteration < 8; ++iteration)
        {
            auto processor = std::make_unique<AudienceProcessor>();
            std::atomic<bool> start { false };
            std::atomic<bool> workerEntered { false };
            std::atomic<bool> workerFinished { false };
            std::thread restoreWorker ([&]
            {
                while (! start.load (std::memory_order_acquire))
                    std::this_thread::yield();
                workerEntered.store (true, std::memory_order_release);
                processor->setStateInformation (workerState.getData(),
                                                (int) workerState.getSize());
                workerFinished.store (true, std::memory_order_release);
            });

            start.store (true, std::memory_order_release);
            while (! workerEntered.load (std::memory_order_acquire))
                std::this_thread::yield();

            const auto intentDeadline = juce::Time::getMillisecondCounter() + 1000u;
            while (ProcessorAutoRoutingTestAccess::restoreIntentCount (
                       *processor) == 0u
                   && ! workerFinished.load (std::memory_order_acquire)
                   && static_cast<std::int32_t> (
                          intentDeadline
                          - juce::Time::getMillisecondCounter()) > 0)
                std::this_thread::yield();

            juce::Timer::callPendingTimersSynchronously();
            restoreWorker.join();
            const bool settled = waitUntil ([&]
            {
                return processor->getFreshRouteAssignmentState()
                           == AudienceProcessor::FreshRouteAssignmentState::preserved
                    && processor->getUdpPort() == restoredPort
                    && parameterInt (*processor, "expectedZone") == 4
                    && parameterInt (*processor, "conductorRole") == 2;
            });
            everyWorkerRestoreWon = everyWorkerRestoreWon && settled
                && savedUdpPort (saveState (*processor)) == restoredPort;

            auto sibling = std::make_unique<AudienceProcessor>();
            everySiblingKeptZoneA = everySiblingKeptZoneA
                                  && waitForAssignedRoute (*sibling, 0);
        }
        expect (everyWorkerRestoreWon,
                "worker host restore always wins the first-timer auto-assignment race");
        expect (everySiblingKeptZoneA,
                "worker-restored instances leave Zone A free for the next fresh sibling");
    }
    juce::Timer::callPendingTimersSynchronously();

    {
        constexpr int restoredPort = 18126;
        auto processor = std::make_unique<AudienceProcessor>();
        const bool initiallyAssigned = waitForAssignedRoute (*processor, 0);
        const auto workerState = makeCustomHostState (restoredPort,
                                                      256 * 1024);
        std::thread restoreWorker ([&]
        {
            processor->setStateInformation (workerState.getData(),
                                            (int) workerState.getSize());
        });
        restoreWorker.join();

        // No Timer or message callback has been pumped since the worker joined.
        // CoreMIDI and the process-local Conductor are safe to retire on that
        // control thread; OscBridge remains untouched until the JUCE handoff.
        const bool immediateFailClosed = ! processor->isMidiOutputReady()
            && processor->getConductorRegistrationStatus()
                == static_cast<int> (
                    GlobalConductorHub::RegistrationStatus::InvalidPort);

        ProcessorAutoRoutingTestAccess::dispatchPendingRouteHandoff (
            *processor);
        const bool handedOffWithoutTimer =
            processor->getFreshRouteAssignmentState()
                == AudienceProcessor::FreshRouteAssignmentState::preserved
            && processor->getUdpPort() == restoredPort
            && processor->osc.isReceiving()
            && processor->osc.getCurrentPort() == restoredPort
            && parameterInt (*processor, "expectedZone") == 4
            && ! processor->isMidiOutputReady()
            && processor->getConductorRegistrationStatus()
                == static_cast<int> (
                    GlobalConductorHub::RegistrationStatus::Registered);

        auto sibling = std::make_unique<AudienceProcessor>();
        expect (initiallyAssigned && immediateFailClosed
                    && handedOffWithoutTimer
                    && waitForAssignedRoute (*sibling, 0),
                "valid worker restore retires CoreMIDI/Conductor immediately and hands OSC to the message thread without waiting for a Timer tick");
    }
    juce::Timer::callPendingTimersSynchronously();

    {
        constexpr int restoredPort = 18125;
        auto processor = std::make_unique<AudienceProcessor>();
        const bool initiallyAssigned = waitForAssignedRoute (*processor, 0);
        const auto replacementState = makeCustomHostState (restoredPort);
        processor->setStateInformation (replacementState.getData(),
                                        (int) replacementState.getSize());
        const bool liveRestoreReady = waitUntil ([&]
        {
            return processor->getFreshRouteAssignmentState()
                       == AudienceProcessor::FreshRouteAssignmentState::preserved
                && processor->getUdpPort() == restoredPort
                && processor->osc.isReceiving()
                && processor->osc.getCurrentPort() == restoredPort
                && parameterInt (*processor, "expectedZone") == 4
                && parameterInt (*processor, "conductorRole") == 2
                && processor->getResolvedMidiOutputOptionIndex() == 0
                && ! processor->isMidiOutputReady()
                && processor->getConductorRegistrationStatus()
                    == static_cast<int> (
                        GlobalConductorHub::RegistrationStatus::Registered);
        });
        auto sibling = std::make_unique<AudienceProcessor>();
        expect (initiallyAssigned && liveRestoreReady
                    && waitForAssignedRoute (*sibling, 0),
                "restoring an assigned live instance tears down its old complete route before Zone A is reused");
    }
    juce::Timer::callPendingTimersSynchronously();

    {
        auto owner = std::make_unique<AudienceProcessor>();
        expect (waitForAssignedRoute (*owner, 0),
                "saved-state fixture owns the Zone A route");
        const auto zoneAState = saveState (*owner);

        auto restored = std::make_unique<AudienceProcessor>();
        restored->setStateInformation (zoneAState.getData(),
                                       (int) zoneAState.getSize());
        const bool preservedConflict = waitUntil ([&]
        {
            return restored->getFreshRouteAssignmentState()
                       == AudienceProcessor::FreshRouteAssignmentState::preserved
                && restored->getUdpPort() == 6062
                && ! restored->osc.isReceiving()
                && ! restored->isMidiOutputReady()
                && restored->getResolvedMidiOutputOptionIndex()
                    == CosmicFactoryPresets::midiOutputOption;
        });

        const auto resaved = saveState (*restored);
        const auto withheldRouteRevision =
            restored->getMidiOutputRouteRevision();
        juce::Thread::sleep (1100);
        juce::Timer::callPendingTimersSynchronously();
        const bool unavailableRetryWasIdempotent =
            restored->getMidiOutputRouteRevision()
                == withheldRouteRevision;
        expect (preservedConflict
                    && restored->getUdpPort() == 6062
                    && parameterInt (*restored, "expectedZone") == 1
                    && parameterInt (*restored, "conductorRole") == 1
                    && restored->getMatchingFactoryPresetIndex() == 0
                    && ! restored->osc.isReceiving()
                    && ! restored->isMidiOutputReady()
                    && restored->getMidiOutputStatus().containsIgnoreCase (
                        "WITHHELD")
                    && restored->getConductorRegistrationStatus()
                        == static_cast<int> (
                            GlobalConductorHub::RegistrationStatus::InvalidPort)
                    && savedUdpPort (resaved) == 6062
                    && unavailableRetryWasIdempotent,
                "host-restored Zone A remains on 6062 and fails closed instead of shifting");

        const auto legacyZoneAState = withoutRootProperty (
            zoneAState, "midiOutputRouteKind");
        auto legacyRestored = std::make_unique<AudienceProcessor>();
        legacyRestored->setStateInformation (
            legacyZoneAState.getData(), (int) legacyZoneAState.getSize());
        const bool legacyConflictWithheld = waitUntil ([&]
        {
            return legacyRestored->getFreshRouteAssignmentState()
                       == AudienceProcessor::FreshRouteAssignmentState::preserved
                && legacyRestored->getUdpPort() == 6062
                && ! legacyRestored->osc.isReceiving()
                && ! legacyRestored->isMidiOutputReady()
                && legacyRestored->getResolvedMidiOutputOptionIndex()
                    == CosmicFactoryPresets::midiOutputOption;
        });
        expect (legacyConflictWithheld
                    && legacyRestored->getMidiOutputStatus()
                        .containsIgnoreCase ("WITHHELD"),
                "legacy virtual route without route-kind metadata also fails closed on UDP conflict");
        legacyRestored.reset();

        owner.reset();
        const bool recovered = waitUntil ([&]
        {
            return restored->getUdpPort() == 6062
                && restored->osc.isReceiving()
                && restored->isMidiOutputReady()
                && restored->getResolvedMidiOutputOptionIndex()
                    == CosmicFactoryPresets::midiOutputOption
                && restored->getConductorRegistrationStatus()
                    == static_cast<int> (
                        GlobalConductorHub::RegistrationStatus::Registered);
        }, 5000);
        expect (recovered
                    && parameterInt (*restored, "expectedZone") == 1
                    && savedUdpPort (saveState (*restored)) == 6062,
                "saved conflicting route recovers OSC, virtual MIDI and Conductor without shifting");
    }
    juce::Timer::callPendingTimersSynchronously();

    {
        auto zoneA = std::make_unique<AudienceProcessor>();
        const bool zoneAReady = waitForAssignedRoute (*zoneA, 0);
        auto zoneB = std::make_unique<AudienceProcessor>();
        const bool zoneBReady = waitForAssignedRoute (*zoneB, 1);
        expect (zoneAReady && zoneBReady,
                "manual-override fixture owns Zones A and B deterministically");

        auto manual = std::make_unique<AudienceProcessor>();
        manual->applyFactoryPreset (5);
        expect (manual->getFreshRouteAssignmentState()
                    == AudienceProcessor::FreshRouteAssignmentState::preserved
                    && manual->getUdpPort() == 6067
                    && manual->osc.isReceiving()
                    && manual->isMidiOutputReady()
                    && manual->getResolvedMidiOutputOptionIndex()
                        == CosmicFactoryPresets::midiOutputOption
                    && parameterInt (*manual, "expectedZone") == 6
                    && parameterInt (*manual, "conductorRole") == 2
                    && manual->getMatchingFactoryPresetIndex() == 5,
                "manual Zone F preset before the first timer tick overrides auto assignment");
    }
    juce::Timer::callPendingTimersSynchronously();

    {
        std::vector<std::unique_ptr<AudienceProcessor>> owners;
        bool allFactoryRoutesAssigned = true;
        for (int index = 0; index < CosmicFactoryPresets::count; ++index)
        {
            owners.push_back (std::make_unique<AudienceProcessor>());
            const bool routeAssigned = waitForAssignedRoute (
                *owners.back(), index);
            allFactoryRoutesAssigned = routeAssigned
                                    && allFactoryRoutesAssigned;
        }
        expect (allFactoryRoutesAssigned,
                "eight fresh instances claim every factory route A-H exactly once");

        auto ninth = std::make_unique<AudienceProcessor>();
        const bool exhausted = waitUntil ([&]
        {
            return ninth->getFreshRouteAssignmentState()
                       == AudienceProcessor::FreshRouteAssignmentState::exhausted;
        });
        const bool fullyFailClosed = exhausted
            && ! ninth->osc.isRunning()
            && ! ninth->osc.isReceiving()
            && ninth->getUdpPort() == CosmicFactoryPresets::firstUdpPort
            && ninth->getExpectedZone()
                == CosmicFactoryPresets::zonePreset (0).index
            && ! ninth->isMidiOutputReady()
            && ninth->getConductorRegistrationStatus()
                == static_cast<int> (
                    GlobalConductorHub::RegistrationStatus::InvalidPort)
            && ninth->getConductorActiveZoneCount() == 0
            && ninth->getConductorLeaderPort() == 0
            && ninth->oscStatus.containsIgnoreCase ("NO FREE UDP PORT");
        if (! fullyFailClosed)
            std::cout << "DIAG  exhausted=" << exhausted
                      << " running=" << ninth->osc.isRunning()
                      << " receiving=" << ninth->osc.isReceiving()
                      << " udp=" << ninth->getUdpPort()
                      << " zone=" << ninth->getExpectedZone()
                      << " midiReady=" << ninth->isMidiOutputReady()
                      << " resolvedMidi="
                      << ninth->getResolvedMidiOutputOptionIndex()
                      << " conductorStatus="
                      << ninth->getConductorRegistrationStatus()
                      << " zones=" << ninth->getConductorActiveZoneCount()
                      << " leader=" << ninth->getConductorLeaderPort()
                      << " oscStatus=" << ninth->oscStatus << '\n';
        expect (fullyFailClosed,
                "ninth instance is fully fail-closed when 6062-6069 are occupied");

        juce::Thread::sleep (1100);
        juce::Timer::callPendingTimersSynchronously();
        expect (ninth->getFreshRouteAssignmentState()
                    == AudienceProcessor::FreshRouteAssignmentState::exhausted
                    && ! ninth->osc.isRunning(),
                "exhausted instances stay idle until RETRY AUTO is requested");

        owners[3].reset();
        ninth->retryFreshRouteAssignment();
        expect (waitForAssignedRoute (*ninth, 3),
                "RETRY AUTO claims the newly released 6065 / Zone D route");
    }

    if (failures != 0)
        std::cerr << failures << " processor auto-routing test(s) failed\n";
    return failures == 0 ? 0 : 1;
}
