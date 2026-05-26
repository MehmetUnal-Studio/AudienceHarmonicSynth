#include "Simulator.h"

Simulator::Simulator (SeatEventSink& t) : target(t) {}

Simulator::~Simulator()
{
    clear();
}

void Simulator::addRandomSeat()
{
    // pick a (row, col) not already taken
    for (int attempt = 0; attempt < 64; ++attempt)
    {
        const int row = rng.nextInt(SeatEventSink::MAX_ROWS);
        const int col = rng.nextInt(SeatEventSink::MAX_COLS);

        bool taken = false;
        for (const auto& s : simSeats)
            if (s.row == row && s.col == col) { taken = true; break; }

        if (taken) continue;

        const float x = rng.nextFloat();
        const float y = rng.nextFloat();
        simSeats.push_back({ row, col, x, y, 0.0f, 0.0f });

        target.setX (row, col, x);
        target.setY (row, col, y);
        target.setOn(row, col, true);

        if (randomMovement && ! isTimerRunning())
            startTimer(33);
        return;
    }
}

void Simulator::addRandomSeats (int count)
{
    for (int i = 0; i < count; ++i)
        addRandomSeat();
}

void Simulator::removeRandomSeat()
{
    if (simSeats.empty()) return;
    const int idx = rng.nextInt((int) simSeats.size());
    const auto& s = simSeats[(size_t) idx];
    target.setOn(s.row, s.col, false);
    simSeats.erase(simSeats.begin() + idx);
}

void Simulator::setRandomMovement (bool on)
{
    randomMovement = on;
    if (on && ! simSeats.empty())  startTimer(33);
    else if (! on)                  stopTimer();
}

void Simulator::clear()
{
    for (const auto& s : simSeats)
        target.setOn(s.row, s.col, false);
    clearSilently();
}

void Simulator::clearSilently()
{
    simSeats.clear();
    randomMovement = false;
    stopTimer();
}

void Simulator::timerCallback()
{
    if (! randomMovement || simSeats.empty()) return;

    for (auto& s : simSeats)
    {
        // gentle random walk
        s.vx += (rng.nextFloat() - 0.5f) * 0.02f;
        s.vy += (rng.nextFloat() - 0.5f) * 0.02f;
        s.vx *= 0.95f;
        s.vy *= 0.95f;
        s.x  += s.vx;
        s.y  += s.vy;

        if (s.x < 0.0f) { s.x = 0.0f; s.vx = -s.vx * 0.3f; }
        if (s.x > 1.0f) { s.x = 1.0f; s.vx = -s.vx * 0.3f; }
        if (s.y < 0.0f) { s.y = 0.0f; s.vy = -s.vy * 0.3f; }
        if (s.y > 1.0f) { s.y = 1.0f; s.vy = -s.vy * 0.3f; }

        target.setX(s.row, s.col, s.x);
        target.setY(s.row, s.col, s.y);
    }
}
