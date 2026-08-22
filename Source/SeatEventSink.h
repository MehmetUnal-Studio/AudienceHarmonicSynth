#pragma once

class SeatEventSink
{
public:
    static constexpr int MAX_ROWS = 26;
    static constexpr int MAX_COLS = 100;
    static constexpr int MAX_SEATS = MAX_ROWS * MAX_COLS;
    static constexpr int MAX_OSC_SOURCES = 256;

    virtual ~SeatEventSink() = default;

    virtual void setX  (int row, int col, float xNorm) = 0;
    virtual void setY  (int row, int col, float yNorm) = 0;
    virtual void setOn (int row, int col, bool on) = 0;

    // Finger-aware OSC entry points. Existing seat-grid sinks keep their
    // historical behaviour through these defaults; routing-aware sinks can
    // override them to preserve each source/finger identity independently.
    virtual void setFingerX (int row, int sourceId, int finger, float xNorm)
    {
        (void) finger;
        setX(row, sourceId, xNorm);
    }

    virtual void setFingerY (int row, int sourceId, int finger, float yNorm)
    {
        (void) finger;
        setY(row, sourceId, yNorm);
    }

    virtual void setFingerOn (int row, int sourceId, int finger, bool on)
    {
        (void) finger;
        setOn(row, sourceId, on);
    }
};
