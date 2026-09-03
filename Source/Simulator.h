#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <cstdint>

#include "CrowdSimulatorModel.h"
#include "SeatEventSink.h"

/*
    Simulator

    Message-thread adapter around the allocation-free CrowdSimulatorModel.

    - addRandomSeat()        : one continuously held mapping-test participant
    - addRandomSeats()       : legacy held-participant helper
    - addCrowdParticipants() : stable identities with human On/Off gestures
    - setRandomMovement()    : enables measured, quantised U/V trajectories
    - clear()                : sends Off for every active simulated finger

    The Human profile is the default. Profile and seed are ephemeral simulator
    controls and intentionally do not add APVTS parameters or alter old presets.
*/
class Simulator : private juce::Timer
{
public:
    using Profile = CrowdSimulatorModel::Profile;

    explicit Simulator (SeatEventSink& target,
                        int sourceCapacity = SeatEventSink::MAX_COLS);
    ~Simulator() override;

    void addRandomSeat();
    void addRandomSeats (int count);
    void addCrowdParticipants (int count);
    void removeRandomSeat();
    void setRandomMovement (bool on);
    void setProfile (Profile newProfile) noexcept;
    Profile getProfile() const noexcept;
    void setSeed (std::uint64_t newSeed) noexcept;
    std::uint64_t getSeed() const noexcept;
    // Synchronously dispatches shrink Offs. Call this before narrowing the
    // target MidiAudienceModel so those releases remain inside its old domain.
    std::size_t setSourceCapacity (int newCapacity);
    int getSourceCapacity() const noexcept
    {
        return sourceCapacityValue.load (std::memory_order_relaxed);
    }
    void clear();
    void clearSilently();

    bool isRandomMovementOn() const noexcept
    {
        return randomMovement.load (std::memory_order_relaxed);
    }
    int getSimSeatCount() const noexcept
    {
        return simulatorPopulation.load (std::memory_order_relaxed);
    }
    int getHeldSeatCount() const noexcept
    {
        return heldPopulation.load (std::memory_order_relaxed);
    }
    int getCrowdParticipantCount() const noexcept
    {
        return crowdPopulation.load (std::memory_order_relaxed);
    }
    int getActiveCrowdParticipantCount() const noexcept
    {
        return activeCrowdPopulation.load (std::memory_order_relaxed);
    }
    int getActiveSimSeatCount() const noexcept
    {
        return activeSimulatorPopulation.load (std::memory_order_relaxed);
    }

private:
    friend struct SimulatorTestAccess;

    void timerCallback() override;
    void addSeat (bool automaticLifecycle);
    void dispatchEvents (std::size_t eventCount) noexcept;
    void syncTelemetry() noexcept;
    void updateTimerState();

    SeatEventSink& target;
    CrowdSimulatorModel model;
    CrowdSimulatorModel::EventBuffer eventBuffer {};
    std::atomic<bool> randomMovement { false };
    std::atomic<int> simulatorPopulation { 0 };
    std::atomic<int> heldPopulation { 0 };
    std::atomic<int> crowdPopulation { 0 };
    std::atomic<int> activeCrowdPopulation { 0 };
    std::atomic<int> activeSimulatorPopulation { 0 };
    std::atomic<int> profileValue {
        static_cast<int> (CrowdSimulatorModel::Profile::human) };
    std::atomic<std::uint64_t> seedValue { CrowdSimulatorModel::defaultSeed };
    std::atomic<int> sourceCapacityValue { CrowdSimulatorModel::maxParticipants };
};
