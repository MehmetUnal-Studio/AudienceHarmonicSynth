#include "MidiEngine.h"
#include <algorithm>
#include <cmath>

namespace
{
    struct ScaleDef
    {
        const char* name;
        std::array<int, 12> degrees;
        int count;
    };

    constexpr std::array<ScaleDef, 16> kScales { {
        { "Major",       { 0, 2, 4, 5, 7, 9, 11, 0, 0, 0, 0, 0 }, 7 },
        { "Minor",       { 0, 2, 3, 5, 7, 8, 10, 0, 0, 0, 0, 0 }, 7 },
        { "Dorian",      { 0, 2, 3, 5, 7, 9, 10, 0, 0, 0, 0, 0 }, 7 },
        { "Phrygian",    { 0, 1, 3, 5, 7, 8, 10, 0, 0, 0, 0, 0 }, 7 },
        { "Lydian",      { 0, 2, 4, 6, 7, 9, 11, 0, 0, 0, 0, 0 }, 7 },
        { "Mixolydian",  { 0, 2, 4, 5, 7, 9, 10, 0, 0, 0, 0, 0 }, 7 },
        { "Locrian",     { 0, 1, 3, 5, 6, 8, 10, 0, 0, 0, 0, 0 }, 7 },
        { "Harm. Minor", { 0, 2, 3, 5, 7, 8, 11, 0, 0, 0, 0, 0 }, 7 },
        { "Mel. Minor",  { 0, 2, 3, 5, 7, 9, 11, 0, 0, 0, 0, 0 }, 7 },
        { "Pentatonic",  { 0, 2, 4, 7, 9, 0, 0, 0, 0, 0, 0, 0 }, 5 },
        { "Min. Pent.",  { 0, 3, 5, 7, 10, 0, 0, 0, 0, 0, 0, 0 }, 5 },
        { "Blues",       { 0, 3, 5, 6, 7, 10, 0, 0, 0, 0, 0, 0 }, 6 },
        { "Whole Tone",  { 0, 2, 4, 6, 8, 10, 0, 0, 0, 0, 0, 0 }, 6 },
        { "Chromatic",   { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, 12 },
        { "Phr. Dom.",   { 0, 1, 4, 5, 7, 8, 10, 0, 0, 0, 0, 0 }, 7 },
        { "User",        { 0, 2, 4, 5, 7, 9, 11, 0, 0, 0, 0, 0 }, 7 },
    } };

    const ScaleDef& getScaleDef (int mode)
    {
        return kScales[(size_t) juce::jlimit(0, (int) kScales.size() - 1, mode)];
    }

    float clamp01 (float value) noexcept
    {
        return juce::jlimit(0.0f, 1.0f, value);
    }

    int normalizedToCc (float value) noexcept
    {
        return juce::jlimit(0, 127, (int) std::round(clamp01(value) * 127.0f));
    }

    juce::String midiName (int midi)
    {
        static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        return juce::String(names[((midi % 12) + 12) % 12]) + juce::String(midi / 12 - 1);
    }
}

MidiEngine::MidiEngine()
{
    reset();
}

int MidiEngine::scaleModeForAudienceScaleIndex (int audienceScaleIndex) noexcept
{
    static constexpr std::array<int, 7> audienceToMidiScale {
        0,  // Major
        1,  // Natural Minor
        9,  // Pentatonic
        2,  // Dorian
        4,  // Lydian
        7,  // Harmonic Minor
        12  // Whole Tone
    };

    return audienceToMidiScale[(size_t) juce::jlimit(0, (int) audienceToMidiScale.size() - 1,
                                                     audienceScaleIndex)];
}

void MidiEngine::prepare (double newSampleRate, int)
{
    sampleRate = juce::jmax(1.0, newSampleRate);
    reset();
}

void MidiEngine::reset()
{
    eventFifo.reset();
    for (auto& s : seats)
    {
        s.active.store(false, std::memory_order_relaxed);
        s.lastX.store(0.0f, std::memory_order_relaxed);
        s.lastY.store(0.75f, std::memory_order_relaxed);
        s.speed.store(0.0f, std::memory_order_relaxed);
        s.midi.currentNote.store(-1, std::memory_order_relaxed);
        s.midi.currentVelocity.store(0, std::memory_order_relaxed);
        s.midi.midiChannel.store(1, std::memory_order_relaxed);
        s.midi.isNoteOn.store(false, std::memory_order_relaxed);
        s.midi.lastTriggerTime.store(0.0, std::memory_order_relaxed);
        s.lastExpressionCc.store(-1, std::memory_order_relaxed);
        s.lastModCc.store(-1, std::memory_order_relaxed);
        s.lastCcSample.store(0, std::memory_order_relaxed);
    }

    for (int i = 0; i < 16; ++i)
    {
        lastBrightnessCc[(size_t) i].store(-1, std::memory_order_relaxed);
        lastReverbCc[(size_t) i].store(-1, std::memory_order_relaxed);
        lastDelayCc[(size_t) i].store(-1, std::memory_order_relaxed);
    }

    for (auto& slot : monitorSlots)
    {
        slot.serial.store(0, std::memory_order_relaxed);
        slot.type.store(0, std::memory_order_relaxed);
        slot.channel.store(0, std::memory_order_relaxed);
        slot.data1.store(0, std::memory_order_relaxed);
        slot.data2.store(0, std::memory_order_relaxed);
    }

    registeredSeatCount.store(0, std::memory_order_relaxed);
    lastNote.store(-1, std::memory_order_relaxed);
    clearAllSeatsPending.store(false, std::memory_order_relaxed);
    allNotesOffPending.store(false, std::memory_order_relaxed);
    retunePending.store(false, std::memory_order_relaxed);
    sampleClock = 0;
    lastGlobalCcSample = 0;
    monitorSerial.store(0, std::memory_order_relaxed);
}

int MidiEngine::seatIndex (int row, int col) noexcept
{
    if (row < 0 || row >= MAX_ROWS) return -1;
    if (col < 0 || col >= MAX_COLS) return -1;
    return row * MAX_COLS + col;
}

void MidiEngine::setX (int row, int col, float xNorm)
{
    const int idx = seatIndex(row, col);
    if (idx < 0) return;

    auto& seat = seats[(size_t) idx];
    const float x = clamp01(xNorm);
    const float oldX = seat.lastX.exchange(x, std::memory_order_relaxed);
    const float delta = std::abs(x - oldX);
    const float previousSpeed = seat.speed.load(std::memory_order_relaxed);
    seat.speed.store(juce::jlimit(0.0f, 1.0f, previousSpeed * 0.75f + delta * 3.0f), std::memory_order_relaxed);

    enqueueEvent({ MidiEvent::XChange, (juce::int16) row, (juce::int16) col, x });
}

void MidiEngine::setY (int row, int col, float yNorm)
{
    const int idx = seatIndex(row, col);
    if (idx < 0) return;

    auto& seat = seats[(size_t) idx];
    const float y = clamp01(yNorm);
    const float oldY = seat.lastY.exchange(y, std::memory_order_relaxed);
    const float delta = std::abs(y - oldY);
    const float previousSpeed = seat.speed.load(std::memory_order_relaxed);
    seat.speed.store(juce::jlimit(0.0f, 1.0f, previousSpeed * 0.75f + delta * 2.0f), std::memory_order_relaxed);

    enqueueEvent({ MidiEvent::YChange, (juce::int16) row, (juce::int16) col, y });
}

void MidiEngine::setOn (int row, int col, bool on)
{
    const int idx = seatIndex(row, col);
    if (idx < 0) return;

    const bool wasActive = seats[(size_t) idx].active.exchange(on, std::memory_order_relaxed);
    if (wasActive != on)
    {
        const int delta = on ? 1 : -1;
        const int updated = registeredSeatCount.fetch_add(delta, std::memory_order_relaxed) + delta;
        if (updated < 0)
            registeredSeatCount.store(0, std::memory_order_relaxed);
    }

    enqueueEvent({ on ? MidiEvent::On : MidiEvent::Off,
                   (juce::int16) row, (juce::int16) col, 0.0f });
}

void MidiEngine::enqueueEvent (const MidiEvent& e)
{
    int s1, sz1, s2, sz2;
    eventFifo.prepareToWrite(1, s1, sz1, s2, sz2);
    if (sz1 > 0)      { eventBuffer[(size_t) s1] = e; eventFifo.finishedWrite(1); }
    else if (sz2 > 0) { eventBuffer[(size_t) s2] = e; eventFifo.finishedWrite(1); }
}

void MidiEngine::renderMidi (juce::MidiBuffer& midi, int numSamples)
{
    const int sampleOffset = juce::jlimit(0, juce::jmax(0, numSamples - 1), 0);

    if (clearAllSeatsPending.exchange(false, std::memory_order_relaxed))
    {
        addAllNotesOff(midi, sampleOffset);
        clearAllSeatsNow();
    }

    if (allNotesOffPending.exchange(false, std::memory_order_relaxed))
        addAllNotesOff(midi, sampleOffset);

    if (retunePending.exchange(false, std::memory_order_relaxed))
    {
        addAllNotesOff(midi, sampleOffset);
        clearMidiNotesOnly();
        for (int row = 0; row < MAX_ROWS; ++row)
            for (int col = 0; col < MAX_COLS; ++col)
            {
                const int idx = seatIndex(row, col);
                if (idx >= 0 && seats[(size_t) idx].active.load(std::memory_order_relaxed))
                    noteOnForSeat(row, col, idx, midi, sampleOffset, true);
            }
    }

    drainEvents(midi, numSamples);
    sendGlobalCCs(midi, sampleOffset, false);
    sampleClock += (uint64_t) juce::jmax(0, numSamples);
}

void MidiEngine::drainEvents (juce::MidiBuffer& midi, int numSamples)
{
    const int avail = eventFifo.getNumReady();
    if (avail <= 0) return;

    int s1, sz1, s2, sz2;
    eventFifo.prepareToRead(avail, s1, sz1, s2, sz2);
    const int sampleOffset = juce::jlimit(0, juce::jmax(0, numSamples - 1), 0);
    for (int i = 0; i < sz1; ++i) handleEvent(eventBuffer[(size_t) (s1 + i)], midi, sampleOffset);
    for (int i = 0; i < sz2; ++i) handleEvent(eventBuffer[(size_t) (s2 + i)], midi, sampleOffset);
    eventFifo.finishedRead(sz1 + sz2);
}

void MidiEngine::handleEvent (const MidiEvent& e, juce::MidiBuffer& midi, int sampleOffset)
{
    const int sIdx = seatIndex(e.row, e.col);
    if (sIdx < 0) return;

    switch ((MidiEvent::Type) e.type)
    {
        case MidiEvent::On:
            noteOnForSeat(e.row, e.col, sIdx, midi, sampleOffset, true);
            sendSeatCCs(sIdx, midi, sampleOffset, true);
            break;

        case MidiEvent::Off:
            noteOffForSeat(sIdx, midi, sampleOffset);
            break;

        case MidiEvent::XChange:
            if (seats[(size_t) sIdx].midi.currentNote.load(std::memory_order_relaxed) >= 0)
                noteOnForSeat(e.row, e.col, sIdx, midi, sampleOffset, false);
            sendSeatCCs(sIdx, midi, sampleOffset, false);
            break;

        case MidiEvent::YChange:
            sendSeatCCs(sIdx, midi, sampleOffset, false);
            break;
    }
}

int MidiEngine::xToMidi (float x) const noexcept
{
    const auto& scale = getScaleDef(scaleMode.load(std::memory_order_relaxed));
    const int lowOctave = juce::jlimit(0, 7, rangeLowOctave.load(std::memory_order_relaxed));
    const int highOctave = juce::jlimit(lowOctave + 1, 8, rangeHighOctave.load(std::memory_order_relaxed));
    const int octaves = juce::jmax(1, highOctave - lowOctave);
    const int totalSteps = juce::jmax(1, octaves * scale.count);
    int step = (int) std::floor(juce::jlimit(0.0f, 0.999999f, x) * (float) totalSteps);
    step = juce::jlimit(0, totalSteps - 1, step);

    const int midi = lowestMidi.load(std::memory_order_relaxed)
                   + lowOctave * 12
                   + (step / scale.count) * 12
                   + scale.degrees[(size_t) (step % scale.count)]
                   + transpose.load(std::memory_order_relaxed);

    return juce::jlimit(0, 127, midi);
}

int MidiEngine::yToVelocity (float y) const noexcept
{
    const float shaped = [this, y]
    {
        const float v = clamp01(y);
        switch (velocityCurve.load(std::memory_order_relaxed))
        {
            case 1:  return std::sqrt(v);
            case 2:  return v * v;
            default: return v;
        }
    }();

    const float energy = clamp01(energyMacro.load(std::memory_order_relaxed));
    const float scaled = shaped * (0.45f + energy * 0.75f);
    return juce::jlimit(1, 127, 1 + (int) std::round(clamp01(scaled) * 126.0f));
}

void MidiEngine::noteOnForSeat (int row, int col, int seatIdx, juce::MidiBuffer& midi, int sampleOffset, bool force)
{
    auto& seat = seats[(size_t) seatIdx];
    if (! seat.active.load(std::memory_order_relaxed))
        return;

    const int newMidi = xToMidi(seat.lastX.load(std::memory_order_relaxed));
    const int newVelocity = yToVelocity(seat.lastY.load(std::memory_order_relaxed));
    const int oldMidi = seat.midi.currentNote.load(std::memory_order_relaxed);
    const int oldChannel = seat.midi.midiChannel.load(std::memory_order_relaxed);
    const int newChannel = channelForSeat(row, col, seatIdx);

    if (! force && oldMidi == newMidi && oldChannel == newChannel)
        return;

    const double nowSeconds = sampleRate > 0.0 ? (double) sampleClock / sampleRate : 0.0;
    const double lastSeconds = seat.midi.lastTriggerTime.load(std::memory_order_relaxed);
    const double minSeconds = (double) juce::jmax(0.0f, retriggerMs.load(std::memory_order_relaxed)) * 0.001;
    if (! force && oldMidi >= 0 && nowSeconds - lastSeconds < minSeconds)
        return;

    if (oldMidi >= 0)
    {
        midi.addEvent(juce::MidiMessage::noteOff(juce::jlimit(1, 16, oldChannel), oldMidi), sampleOffset);
        logEvent(MonitorType::NoteOff, juce::jlimit(1, 16, oldChannel), oldMidi, 0);
    }

    midi.addEvent(juce::MidiMessage::noteOn(newChannel, newMidi, (juce::uint8) newVelocity), sampleOffset);
    logEvent(MonitorType::NoteOn, newChannel, newMidi, newVelocity);

    seat.midi.currentNote.store(newMidi, std::memory_order_relaxed);
    seat.midi.currentVelocity.store(newVelocity, std::memory_order_relaxed);
    seat.midi.midiChannel.store(newChannel, std::memory_order_relaxed);
    seat.midi.isNoteOn.store(true, std::memory_order_relaxed);
    seat.midi.lastTriggerTime.store(nowSeconds, std::memory_order_relaxed);
    lastNote.store(newMidi, std::memory_order_relaxed);
}

void MidiEngine::noteOffForSeat (int seatIdx, juce::MidiBuffer& midi, int sampleOffset)
{
    auto& seat = seats[(size_t) seatIdx];
    const int oldMidi = seat.midi.currentNote.exchange(-1, std::memory_order_relaxed);
    const int oldChannel = seat.midi.midiChannel.load(std::memory_order_relaxed);
    seat.midi.currentVelocity.store(0, std::memory_order_relaxed);
    seat.midi.isNoteOn.store(false, std::memory_order_relaxed);

    if (oldMidi >= 0)
    {
        const int ch = juce::jlimit(1, 16, oldChannel);
        midi.addEvent(juce::MidiMessage::noteOff(ch, oldMidi), sampleOffset);
        logEvent(MonitorType::NoteOff, ch, oldMidi, 0);
    }
}

int MidiEngine::channelForSeat (int row, int col, int seatIdx) const noexcept
{
    const int base = juce::jlimit(1, 16, channel.load(std::memory_order_relaxed));
    switch ((MidiChannelMode) channelMode.load(std::memory_order_relaxed))
    {
        case MidiChannelMode::PerParticipant: return (seatIdx % 16) + 1;
        case MidiChannelMode::PerRow:         return juce::jlimit(1, 16, row + 1);
        case MidiChannelMode::PerColumn:      return juce::jlimit(1, 16, col + 1);
        case MidiChannelMode::Single:
        default:                              return base;
    }
}

void MidiEngine::sendSeatCCs (int seatIdx, juce::MidiBuffer& midi, int sampleOffset, bool force)
{
    if (! ccEnabled.load(std::memory_order_relaxed))
        return;

    auto& seat = seats[(size_t) seatIdx];
    if (! seat.active.load(std::memory_order_relaxed))
        return;

    const uint64_t minIntervalSamples = (uint64_t) (sampleRate / 50.0);
    const uint64_t previous = seat.lastCcSample.load(std::memory_order_relaxed);
    if (! force && sampleClock >= previous && sampleClock - previous < minIntervalSamples)
        return;

    const int row = seatIdx / MAX_COLS;
    const int col = seatIdx % MAX_COLS;
    const int channelToUse = channelForSeat(row, col, seatIdx);

    const float y = seat.lastY.load(std::memory_order_relaxed);
    const float speed = seat.speed.load(std::memory_order_relaxed);
    const float motion = clamp01(motionMacro.load(std::memory_order_relaxed));

    const int mod = normalizedToCc(juce::jlimit(0.0f, 1.0f, speed * 0.75f + motion * 0.25f));
    const int expression = normalizedToCc(y);

    const bool sentMod = sendCcIfChanged(midi, sampleOffset, channelToUse, 1, mod, seat.lastModCc, force ? 0 : 2);
    const bool sentExpression = sendCcIfChanged(midi, sampleOffset, channelToUse, 11, expression,
                                                seat.lastExpressionCc, force ? 0 : 2);
    if (sentMod || sentExpression)
        seat.lastCcSample.store(sampleClock, std::memory_order_relaxed);
}

void MidiEngine::sendGlobalCCs (juce::MidiBuffer& midi, int sampleOffset, bool force)
{
    if (! ccEnabled.load(std::memory_order_relaxed))
        return;

    const uint64_t minIntervalSamples = (uint64_t) (sampleRate / 30.0);
    if (! force && sampleClock >= lastGlobalCcSample && sampleClock - lastGlobalCcSample < minIntervalSamples)
        return;

    const float crowd = juce::jlimit(0.0f, 1.0f,
                                     (float) registeredSeatCount.load(std::memory_order_relaxed) / 128.0f);
    const int brightness = normalizedToCc(toneMacro.load(std::memory_order_relaxed) * 0.75f
                                          + motionMacro.load(std::memory_order_relaxed) * 0.15f
                                          + crowd * 0.10f);
    const int reverb = normalizedToCc(spaceMacro.load(std::memory_order_relaxed) * 0.75f + crowd * 0.25f);
    const int delay = normalizedToCc(spaceMacro.load(std::memory_order_relaxed) * 0.55f + crowd * 0.45f);

    bool anySent = false;
    std::array<bool, 16> activeChannels {};

    if ((MidiChannelMode) channelMode.load(std::memory_order_relaxed) == MidiChannelMode::Single)
    {
        activeChannels[(size_t) (juce::jlimit(1, 16, channel.load(std::memory_order_relaxed)) - 1)] = true;
    }
    else
    {
        for (int row = 0; row < MAX_ROWS; ++row)
            for (int col = 0; col < MAX_COLS; ++col)
            {
                const int idx = seatIndex(row, col);
                if (idx >= 0 && seats[(size_t) idx].midi.currentNote.load(std::memory_order_relaxed) >= 0)
                    activeChannels[(size_t) (channelForSeat(row, col, idx) - 1)] = true;
            }
    }

    bool hasActiveChannel = false;
    for (bool active : activeChannels)
        hasActiveChannel = hasActiveChannel || active;

    if (! hasActiveChannel)
        activeChannels[(size_t) (juce::jlimit(1, 16, channel.load(std::memory_order_relaxed)) - 1)] = true;

    for (int i = 0; i < 16; ++i)
    {
        if (! activeChannels[(size_t) i])
            continue;

        const int ch = i + 1;
        anySent = sendCcIfChanged(midi, sampleOffset, ch, 74, brightness, lastBrightnessCc[(size_t) i], force ? 0 : 2) || anySent;
        anySent = sendCcIfChanged(midi, sampleOffset, ch, 91, reverb, lastReverbCc[(size_t) i], force ? 0 : 2) || anySent;
        anySent = sendCcIfChanged(midi, sampleOffset, ch, 93, delay, lastDelayCc[(size_t) i], force ? 0 : 2) || anySent;
    }

    if (anySent)
        lastGlobalCcSample = sampleClock;
}

bool MidiEngine::sendCcIfChanged (juce::MidiBuffer& midi, int sampleOffset, int ch, int cc, int value,
                                  std::atomic<int>& previous, int threshold)
{
    const int safeValue = juce::jlimit(0, 127, value);
    const int safeChannel = juce::jlimit(1, 16, ch);
    const int old = previous.load(std::memory_order_relaxed);
    if (old >= 0 && std::abs(safeValue - old) < threshold)
        return false;

    midi.addEvent(juce::MidiMessage::controllerEvent(safeChannel, juce::jlimit(0, 127, cc), safeValue), sampleOffset);
    previous.store(safeValue, std::memory_order_relaxed);
    logEvent(MonitorType::CC, safeChannel, cc, safeValue);
    return true;
}

void MidiEngine::addAllNotesOff (juce::MidiBuffer& midi, int sampleOffset)
{
    for (int ch = 1; ch <= 16; ++ch)
    {
        midi.addEvent(juce::MidiMessage::allNotesOff(ch), sampleOffset);
        midi.addEvent(juce::MidiMessage::allSoundOff(ch), sampleOffset);
        logEvent(MonitorType::AllNotesOff, ch, 0, 0);
    }

    clearMidiNotesOnly();
}

void MidiEngine::clearMidiNotesOnly()
{
    for (auto& seat : seats)
    {
        seat.midi.currentNote.store(-1, std::memory_order_relaxed);
        seat.midi.currentVelocity.store(0, std::memory_order_relaxed);
        seat.midi.isNoteOn.store(false, std::memory_order_relaxed);
    }
    lastNote.store(-1, std::memory_order_relaxed);
}

void MidiEngine::requestAllNotesOff()
{
    allNotesOffPending.store(true, std::memory_order_relaxed);
    clearMidiNotesOnly();
}

void MidiEngine::requestRetuneActiveNotes()
{
    retunePending.store(true, std::memory_order_relaxed);
}

void MidiEngine::clearAllSeats()
{
    clearAllSeatsNow();
    allNotesOffPending.store(true, std::memory_order_relaxed);
}

void MidiEngine::requestClearAllSeats()
{
    clearAllSeatsPending.store(true, std::memory_order_relaxed);
}

void MidiEngine::clearAllSeatsNow() noexcept
{
    for (auto& s : seats)
        s.active.store(false, std::memory_order_relaxed);

    eventFifo.reset();
    retunePending.store(false, std::memory_order_relaxed);
    clearMidiNotesOnly();
    registeredSeatCount.store(0, std::memory_order_relaxed);
}

bool MidiEngine::isSeatActive (int row, int col) const noexcept
{
    const int idx = seatIndex(row, col);
    return idx >= 0 && seats[(size_t) idx].active.load(std::memory_order_relaxed);
}

float MidiEngine::getSeatX (int row, int col) const noexcept
{
    const int idx = seatIndex(row, col);
    return idx < 0 ? 0.0f : seats[(size_t) idx].lastX.load(std::memory_order_relaxed);
}

float MidiEngine::getSeatY (int row, int col) const noexcept
{
    const int idx = seatIndex(row, col);
    return idx < 0 ? 0.0f : seats[(size_t) idx].lastY.load(std::memory_order_relaxed);
}

int MidiEngine::getSeatMidi (int row, int col) const noexcept
{
    const int idx = seatIndex(row, col);
    return idx < 0 ? -1 : seats[(size_t) idx].midi.currentNote.load(std::memory_order_relaxed);
}

juce::String MidiEngine::getScaleName() const
{
    return getScaleDef(scaleMode.load(std::memory_order_relaxed)).name;
}

juce::String MidiEngine::getScaleRangeName() const
{
    const int low = xToMidi(0.0f);
    const int high = xToMidi(0.999999f);
    return midiName(low) + " - " + midiName(high) + "  " + getScaleName();
}

juce::String MidiEngine::getMonitorText() const
{
    struct Snapshot
    {
        uint32_t serial = 0;
        int type = 0;
        int channel = 0;
        int data1 = 0;
        int data2 = 0;
    };

    std::array<Snapshot, 16> snapshots;
    for (size_t i = 0; i < monitorSlots.size(); ++i)
    {
        const auto& slot = monitorSlots[i];
        snapshots[i].serial = slot.serial.load(std::memory_order_relaxed);
        snapshots[i].type = slot.type.load(std::memory_order_relaxed);
        snapshots[i].channel = slot.channel.load(std::memory_order_relaxed);
        snapshots[i].data1 = slot.data1.load(std::memory_order_relaxed);
        snapshots[i].data2 = slot.data2.load(std::memory_order_relaxed);
    }

    std::sort(snapshots.begin(), snapshots.end(), [] (const Snapshot& a, const Snapshot& b)
    {
        return a.serial > b.serial;
    });

    juce::String text;
    int lines = 0;
    for (const auto& item : snapshots)
    {
        if (item.serial == 0 || item.type == 0)
            continue;

        if (lines++ > 0)
            text << "\n";

        switch ((MonitorType) item.type)
        {
            case MonitorType::NoteOn:
                text << "Note On: ch " << item.channel << ", note " << item.data1
                     << ", velocity " << item.data2;
                break;
            case MonitorType::NoteOff:
                text << "Note Off: ch " << item.channel << ", note " << item.data1;
                break;
            case MonitorType::CC:
                text << "CC" << item.data1 << ": ch " << item.channel << ", value " << item.data2;
                break;
            case MonitorType::AllNotesOff:
                text << "All Notes Off: ch " << item.channel;
                break;
        }
    }

    return text.isEmpty() ? "Waiting for outgoing MIDI..." : text;
}

juce::String MidiEngine::getCCMappingSummary() const
{
    return "CC1 movement, CC11 Y/expression, CC74 tone, CC91 space, CC93 density delay";
}

void MidiEngine::logEvent (MonitorType type, int ch, int data1, int data2) noexcept
{
    const uint32_t serial = monitorSerial.fetch_add(1, std::memory_order_relaxed) + 1;
    auto& slot = monitorSlots[(size_t) (serial % monitorSlots.size())];
    slot.type.store((int) type, std::memory_order_relaxed);
    slot.channel.store(ch, std::memory_order_relaxed);
    slot.data1.store(data1, std::memory_order_relaxed);
    slot.data2.store(data2, std::memory_order_relaxed);
    slot.serial.store(serial, std::memory_order_release);
}
