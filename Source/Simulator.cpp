#include "Simulator.h"

#include <algorithm>

Simulator::Simulator (SeatEventSink& sink, int sourceCapacity)
    : target (sink),
      model (std::max (1, std::min (SeatEventSink::MAX_OSC_SOURCES,
                                    sourceCapacity)))
{
    syncTelemetry();
}

Simulator::~Simulator()
{
    clear();
}

void Simulator::addRandomSeat()
{
    addSeat (false);
}

void Simulator::addRandomSeats (int count)
{
    const int remaining = std::max (0,
        model.getSourceCapacity() - model.getPopulation());
    const int toAdd = std::max (0, std::min (remaining, count));
    for (int i = 0; i < toAdd; ++i)
        addSeat (false);
}

void Simulator::addCrowdParticipants (int count)
{
    const int remaining = std::max (0,
        model.getSourceCapacity() - model.getPopulation());
    const int toAdd = std::max (0, std::min (remaining, count));
    for (int i = 0; i < toAdd; ++i)
        addSeat (true);
}

void Simulator::addSeat (bool automaticLifecycle)
{
    const auto result = automaticLifecycle
        ? model.addCrowdParticipant (eventBuffer)
        : model.addHeldParticipant (eventBuffer);
    dispatchEvents (result.eventCount);
    syncTelemetry();
    updateTimerState();
}

void Simulator::removeRandomSeat()
{
    const auto result = model.removeOneParticipant (eventBuffer);
    dispatchEvents (result.eventCount);
    syncTelemetry();
    updateTimerState();
}

void Simulator::setRandomMovement (bool on)
{
    randomMovement.store (on, std::memory_order_relaxed);
    updateTimerState();
}

void Simulator::setProfile (Profile newProfile) noexcept
{
    model.setProfile (newProfile);
    const auto accepted = model.getProfile();
    profileValue.store (static_cast<int> (accepted), std::memory_order_relaxed);
}

Simulator::Profile Simulator::getProfile() const noexcept
{
    const int value = profileValue.load (std::memory_order_relaxed);
    if (value == static_cast<int> (Profile::dense))
        return Profile::dense;
    if (value == static_cast<int> (Profile::stress))
        return Profile::stress;
    return Profile::human;
}

void Simulator::setSeed (std::uint64_t newSeed) noexcept
{
    model.setSeed (newSeed);
    seedValue.store (model.getSeed(), std::memory_order_relaxed);
}

std::uint64_t Simulator::getSeed() const noexcept
{
    return seedValue.load (std::memory_order_relaxed);
}

std::size_t Simulator::setSourceCapacity (int newCapacity)
{
    const auto released = model.setSourceCapacity (newCapacity, eventBuffer);
    dispatchEvents (released);
    syncTelemetry();
    updateTimerState();
    return released;
}

void Simulator::clear()
{
    dispatchEvents (model.clear (eventBuffer));
    randomMovement.store (false, std::memory_order_relaxed);
    syncTelemetry();
    stopTimer();
}

void Simulator::clearSilently()
{
    model.clearSilently();
    randomMovement.store (false, std::memory_order_relaxed);
    syncTelemetry();
    stopTimer();
}

void Simulator::timerCallback()
{
    if (model.getPopulation() <= 0)
    {
        stopTimer();
        return;
    }

    const auto eventCount = model.advance (
        randomMovement.load (std::memory_order_relaxed), eventBuffer);
    dispatchEvents (eventCount);
    syncTelemetry();
    updateTimerState();
}

void Simulator::dispatchEvents (std::size_t eventCount) noexcept
{
    const std::size_t boundedCount = std::min (eventCount, eventBuffer.size());
    for (std::size_t index = 0; index < boundedCount; ++index)
    {
        const auto& event = eventBuffer[index];
        switch (event.type)
        {
            case CrowdSimulatorModel::EventType::x:
                target.setX (event.row, event.sourceId, event.value);
                break;
            case CrowdSimulatorModel::EventType::y:
                target.setY (event.row, event.sourceId, event.value);
                break;
            case CrowdSimulatorModel::EventType::on:
                target.setOn (event.row, event.sourceId, true);
                break;
            case CrowdSimulatorModel::EventType::off:
                target.setOn (event.row, event.sourceId, false);
                break;
        }
    }
}

void Simulator::syncTelemetry() noexcept
{
    sourceCapacityValue.store (model.getSourceCapacity(),
                               std::memory_order_relaxed);
    simulatorPopulation.store (model.getPopulation(), std::memory_order_relaxed);
    heldPopulation.store (model.getHeldPopulation(), std::memory_order_relaxed);
    crowdPopulation.store (model.getCrowdPopulation(), std::memory_order_relaxed);
    activeCrowdPopulation.store (model.getActiveCrowdPopulation(),
                                 std::memory_order_relaxed);
    activeSimulatorPopulation.store (model.getActivePopulation(),
                                     std::memory_order_relaxed);
}

void Simulator::updateTimerState()
{
    const bool needsTimer = model.getCrowdPopulation() > 0
                         || (randomMovement.load (std::memory_order_relaxed)
                             && model.getPopulation() > 0);
    if (needsTimer)
    {
        if (! isTimerRunning())
            startTimer (CrowdSimulatorModel::tickIntervalMs);
    }
    else
    {
        stopTimer();
    }
}
