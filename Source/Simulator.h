#pragma once

#include <juce_events/juce_events.h>
#include <juce_core/juce_core.h>
#include <vector>
#include "SeatEventSink.h"

/*
    Simulator

    UI-driven fake audience. Pretends to be a crowd of phones so you can
    play the plugin without any UDP traffic.

    - addRandomSeat()    : drops a participant onto a free seat
    - removeRandomSeat() : ends one participant
    - setRandomMovement(true) : drifts every active participant's X/Y
    - clear()            : ends all participants
*/
class Simulator : private juce::Timer
{
public:
    explicit Simulator (SeatEventSink& target,
                        int sourceCapacity = SeatEventSink::MAX_COLS);
    ~Simulator() override;

    void addRandomSeat();
    void addRandomSeats (int count);
    void removeRandomSeat();
    void setRandomMovement (bool on);
    void clear();
    void clearSilently();

    bool isRandomMovementOn() const noexcept { return randomMovement; }
    int  getSimSeatCount()    const noexcept { return (int) simSeats.size(); }

private:
    void timerCallback() override;

    struct SimSeat
    {
        int   row, col;
        float x, y;
        float vx, vy;
    };

    SeatEventSink&        target;
    const int             maxSourceCount;
    std::vector<SimSeat>  simSeats;
    juce::Random          rng;
    bool                  randomMovement = false;
};
