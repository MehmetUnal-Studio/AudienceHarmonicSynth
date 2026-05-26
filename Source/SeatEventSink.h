#pragma once

class SeatEventSink
{
public:
    static constexpr int MAX_ROWS = 26;
    static constexpr int MAX_COLS = 100;
    static constexpr int MAX_SEATS = MAX_ROWS * MAX_COLS;

    virtual ~SeatEventSink() = default;

    virtual void setX  (int row, int col, float xNorm) = 0;
    virtual void setY  (int row, int col, float yNorm) = 0;
    virtual void setOn (int row, int col, bool on) = 0;
};
