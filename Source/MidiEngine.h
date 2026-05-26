#pragma once

#include <array>
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include "SeatEventSink.h"

class MidiEngine : public SeatEventSink
{
public:
    static constexpr int EVENT_QUEUE_SIZE = 8192;

    enum class MidiChannelMode
    {
        Single = 0,
        PerParticipant,
        PerRow,
        PerColumn
    };

    enum class MidiCCSource
    {
        ParticipantY = 0,
        ParticipantSpeed,
        CrowdDensity,
        EnergyMacro,
        MotionMacro,
        ToneMacro,
        SpaceMacro
    };

    struct MidiCCMapping
    {
        bool enabled = true;
        int ccNumber = 1;
        MidiCCSource source = MidiCCSource::ParticipantSpeed;
    };

    struct ParticipantMidiState
    {
        std::atomic<int>    currentNote     { -1 };
        std::atomic<int>    currentVelocity { 0 };
        std::atomic<int>    midiChannel     { 1 };
        std::atomic<bool>   isNoteOn        { false };
        std::atomic<double> lastTriggerTime { 0.0 };
    };

    MidiEngine();

    static int scaleModeForAudienceScaleIndex (int audienceScaleIndex) noexcept;

    void prepare (double sampleRate, int blockSize = 512);
    void reset();
    void setX  (int row, int col, float xNorm) override;
    void setY  (int row, int col, float yNorm) override;
    void setOn (int row, int col, bool on) override;

    void renderMidi (juce::MidiBuffer& midi, int numSamples);
    void clearAllSeats();
    void requestClearAllSeats();
    void requestAllNotesOff();
    void requestRetuneActiveNotes();

    std::atomic<int> channel      { 1 };  // base channel, 1..16
    std::atomic<int> channelMode  { (int) MidiChannelMode::Single };
    std::atomic<int> lowestMidi   { 36 }; // C2, shared with the audio engine default.
    std::atomic<int> rangeOctaves { 4 };
    std::atomic<int> rangeLowOctave  { 0 };
    std::atomic<int> rangeHighOctave { 4 };
    std::atomic<int> scaleMode    { 0 };  // Same index order as PartialEngine.
    std::atomic<int> transpose    { 0 };
    std::atomic<int> velocityCurve { 0 }; // Linear, soft, hard.
    std::atomic<float> retriggerMs { 45.0f };
    std::atomic<float> energyMacro { 0.5f };
    std::atomic<float> motionMacro { 0.5f };
    std::atomic<float> toneMacro   { 0.5f };
    std::atomic<float> spaceMacro  { 0.5f };
    std::atomic<bool>  ccEnabled   { true };

    int getRegisteredSeatCount() const noexcept { return registeredSeatCount.load(); }
    int getLastNote() const noexcept            { return lastNote.load(); }
    juce::String getScaleName() const;
    juce::String getScaleRangeName() const;
    juce::String getMonitorText() const;
    juce::String getCCMappingSummary() const;

    bool  isSeatActive (int row, int col) const noexcept;
    float getSeatX     (int row, int col) const noexcept;
    float getSeatY     (int row, int col) const noexcept;
    int   getSeatMidi  (int row, int col) const noexcept;

private:
    struct SeatState
    {
        std::atomic<bool>  active      { false };
        std::atomic<float> lastX       { 0.0f };
        std::atomic<float> lastY       { 0.75f };
        std::atomic<float> speed       { 0.0f };
        ParticipantMidiState midi;
        std::atomic<int> lastExpressionCc { -1 };
        std::atomic<int> lastModCc        { -1 };
        std::atomic<uint64_t> lastCcSample { 0 };
    };

    struct MidiEvent
    {
        enum Type : juce::uint8 { On, Off, XChange, YChange };
        juce::uint8 type;
        juce::int16 row;
        juce::int16 col;
        float value;
    };

    enum class MonitorType : int { NoteOn = 1, NoteOff, CC, AllNotesOff };

    struct MonitorSlot
    {
        std::atomic<uint32_t> serial { 0 };
        std::atomic<int> type { 0 };
        std::atomic<int> channel { 0 };
        std::atomic<int> data1 { 0 };
        std::atomic<int> data2 { 0 };
    };

    static int seatIndex (int row, int col) noexcept;
    void enqueueEvent (const MidiEvent& e);
    void drainEvents (juce::MidiBuffer& midi, int numSamples);
    void handleEvent (const MidiEvent& e, juce::MidiBuffer& midi, int sampleOffset);
    int  xToMidi (float x) const noexcept;
    int  yToVelocity (float y) const noexcept;
    void noteOnForSeat (int row, int col, int seatIdx, juce::MidiBuffer& midi, int sampleOffset, bool force);
    void noteOffForSeat (int seatIdx, juce::MidiBuffer& midi, int sampleOffset);
    int  channelForSeat (int row, int col, int seatIdx) const noexcept;
    void sendSeatCCs (int seatIdx, juce::MidiBuffer& midi, int sampleOffset, bool force);
    void sendGlobalCCs (juce::MidiBuffer& midi, int sampleOffset, bool force);
    bool sendCcIfChanged (juce::MidiBuffer& midi, int sampleOffset, int channel, int cc, int value,
                          std::atomic<int>& previous, int threshold = 2);
    void addAllNotesOff (juce::MidiBuffer& midi, int sampleOffset);
    void clearMidiNotesOnly();
    void clearAllSeatsNow() noexcept;
    void logEvent (MonitorType type, int channel, int data1, int data2 = 0) noexcept;

    std::array<SeatState, MAX_SEATS> seats;
    juce::AbstractFifo eventFifo { EVENT_QUEUE_SIZE };
    std::array<MidiEvent, EVENT_QUEUE_SIZE> eventBuffer;
    std::atomic<int> registeredSeatCount { 0 };
    std::atomic<int> lastNote { -1 };
    std::atomic<bool> clearAllSeatsPending { false };
    std::atomic<bool> allNotesOffPending { false };
    std::atomic<bool> retunePending { false };

    double sampleRate = 44100.0;
    uint64_t sampleClock = 0;
    uint64_t lastGlobalCcSample = 0;
    std::array<std::atomic<int>, 16> lastBrightnessCc;
    std::array<std::atomic<int>, 16> lastReverbCc;
    std::array<std::atomic<int>, 16> lastDelayCc;

    std::array<MonitorSlot, 16> monitorSlots;
    std::atomic<uint32_t> monitorSerial { 0 };
};
