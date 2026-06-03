#pragma once

#include <atomic>
#include <array>
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "SampleLibrary.h"
#include "SeatEventSink.h"

/*
    PartialEngine

    Polyphonic audience engine with three render routes:

      UDP / Simulator / UI Keyboard
        -> Seat State: X, Y, On/Off
        -> Realtime Event FIFO
        -> Audio processBlock
        -> APVTS Parameters -> realtime atomics
        -> Drain Events / Handle Seat Events
        -> X -> Pitch Target
        -> Scale Mode
        -> Voice Allocation
        -> Engine Source
             -> Sample Library
                  -> Sample Playback Mode
                       -> Direct Sample Player
                       -> Granular Sample Engine
             -> Element Spectral Synth
        -> Voice Mix
        -> Reverb / Delay / Wet-Dry / Master / Tape / Limiter
        -> Audio Output

    Mapping
    -------
    Per participant (row, col):
      X (0..1) -> quantized to the selected root, scale, and octave range
      Y (0..1) -> velocity / amplitude for active voice(s)

    Trigger logic
    -------------
	    On X change OR /on, compute a pitch target. If it differs from the
	    seat's current target AND `minTriggerMs` has elapsed since the last
	    trigger, fire a new voice. X-note replacements hard-stop the previous
	    unison group so simulator movement cannot accumulate old notes.

    Voices
    ------
        Sample Library voices pick the sample whose rootMidi is closest to the
        requested note. Direct Sample Player reads that buffer linearly from the
        start. Granular Sample Engine uses the same sample selection but schedules
        overlapping grains. Element Spectral Synth bypasses samples and renders
        additive atomic partials.

    Signature modes, granular controls, wet/dry blend, tape saturation, and a
    peak limiter sit at the output so dense crowd input remains playable.
*/
class PartialEngine : public SeatEventSink
{
public:
    enum class EngineSource
    {
        SampleLibrary = 0,
        ElementSpectralSynth = 1
    };

    enum class SamplePlaybackMode
    {
        DirectSamplePlayer = 0,
        Granular = 1
    };

    static constexpr int NORMAL_VOICE_LIMIT   = 256;
    static constexpr int HIGH_VOICE_LIMIT     = 512;
    static constexpr int ULTRA_VOICE_LIMIT    = 1024;
    static constexpr int MAX_VOICES           = ULTRA_VOICE_LIMIT;
    static constexpr int MAX_ROWS             = SeatEventSink::MAX_ROWS;
    static constexpr int MAX_COLS             = SeatEventSink::MAX_COLS;
    static constexpr int MAX_SEATS            = SeatEventSink::MAX_SEATS;
    static constexpr int MAX_KEYBOARD_SLOTS   = 64;
    static constexpr int MAX_ELEMENT_PARTIALS  = 512;
    static constexpr int EVENT_QUEUE_SIZE     = 8192;
    static constexpr int MIDI_EVENT_QUEUE_SIZE = 8192;
    static constexpr int AURORA_BANDS         = 96;
    static constexpr int MAX_DELAY_SAMPLES    = 96000;
   #if JUCE_ANDROID
    static constexpr int GRAINS_PER_VOICE     = 5;
   #else
    static constexpr int GRAINS_PER_VOICE     = 8;
   #endif
    static constexpr int HANN_LUT_SIZE        = 512;

    PartialEngine();

    void prepare (double sampleRate, int blockSize = 512);
    void reset();

    // ---- non-audio control threads ----
    void setX  (int row, int col, float xNorm) override;
    void setY  (int row, int col, float yNorm) override;
    void setOn (int row, int col, bool on) override;
    void setKeyboardStep (int slot, int scaleStep, float velocity, bool on);
    void releaseAllKeyboardNotes();

    // ---- audio thread ----
    void render (float* outL, float* outR, int numSamples);
    void processControlEvents (int numSamples);
    void processKeyboardStepRealtime (int slot, int scaleStep, float velocity, bool on);
    void processKeyboardPitchRealtime (int slot, int midiNote, double frequencyHz, float velocity, bool on);
    static double midiNoteToFrequencyHz (int midiNote) noexcept;

    struct MidiSourceEvent
    {
        enum Type : juce::uint8 { NoteOn, NoteOff, Expression, AllNotesOff };

        juce::uint8 type = NoteOn;
        juce::int16 row = -1;
        juce::int16 col = -1;
        juce::int16 sourceId = -1;
        juce::int16 midiNote = 60;
        double frequencyHz = 261.6255653005986;
        float velocity = 0.0f;
        float x = 0.5f;
        float y = 0.5f;
    };

    int drainMidiSourceEvents (MidiSourceEvent* dest, int maxEvents) noexcept;

    // ---- atomic parameters ----
    std::atomic<float> pitchSemitones { 0.0f };        // global transpose
    std::atomic<float> layerMix       { 0.7f };
    std::atomic<float> attackMs       { 600.0f };
    std::atomic<float> releaseMs      { 2000.0f };
    std::atomic<float> brightness     { 0.6f };
    std::atomic<float> movement       { 0.2f };
    std::atomic<float> reverbAmount   { 0.35f };
    std::atomic<float> delayAmount    { 0.25f };
    std::atomic<float> masterGain     { 0.7f };
    std::atomic<float> energyMacro    { 0.5f };
    std::atomic<float> motionMacro    { 0.5f };
    std::atomic<float> toneMacro      { 0.5f };
    std::atomic<float> spaceMacro     { 0.5f };
    std::atomic<int>   signatureMode  { 0 };
    std::atomic<float> grainSizeMs    { 260.0f };
    std::atomic<float> grainDensity   { 0.55f };
    std::atomic<float> pitchSpread    { 0.0f };
    std::atomic<float> positionJitter { 0.35f };
    std::atomic<float> stereoSpread   { 0.45f };
    std::atomic<int>   reverseGrains  { 0 };
    std::atomic<int>   freeze         { 0 };
    std::atomic<int>   grainShape     { 0 };
    std::atomic<float> wetDry         { 0.85f };
    std::atomic<float> tapeDrive      { 0.0f };
    std::atomic<int>   polyphonyMode  { 0 };           // Normal / High / Ultra
    std::atomic<int>   engineSource   { 0 };           // Sample Library / Element Spectral Synth
    std::atomic<int>   samplePlaybackMode { 0 };       // Direct Sample Player / Granular
    std::atomic<int>   spectralElement { 1 };          // H..Zn, with Nitrogen omitted until a matching dataset is available
    std::atomic<int>   spectralPartialCount { MAX_ELEMENT_PARTIALS };
    std::atomic<int>   spectralPartialSolo { 0 };       // Audition one raw spectral line.
    std::atomic<float> spectralStretch { 0.0f };
    std::atomic<int>   atomicScaleMode { 1 };          // Core / Extended / Microtonal / Scientific / Raw

    // ---- scale parameters ----
    std::atomic<int>   scaleRootMidi  { 36 };          // C2
    std::atomic<int>   scaleOctaves   { 4 };
    std::atomic<int>   scaleMode      { 0 };
    std::atomic<float> minTriggerMs   { 150.0f };      // per-seat hysteresis

    int getActiveVoiceCount()    const noexcept { return activeVoiceCount    .load(); }
    int getRegisteredSeatCount() const noexcept { return registeredSeatCount .load(); }
    int getVoiceLimit()          const noexcept;
    int getAdaptiveUnisonCount() const noexcept { return adaptiveUnisonCount.load(); }
    juce::String getPolyphonyModeName() const;

    SampleLibrary& getLibrary() noexcept             { return library; }
    const SampleLibrary& getLibrary() const noexcept { return library; }
    int  loadSampleLibrary (const juce::File& dir);
    void clearAllVoices();
    void clearAllSeats();
    void requestClearAllSeats();
    void requestRetuneActiveSeats();
    void processPendingCommands();
    void retriggerActiveSeats();
    void discardPendingEvents();
    juce::String getDominantSampleName() const;
    juce::String getScaleName() const;
    juce::String getSignatureModeName() const;

	    // Active scale info (matches the X-to-pitch map).
	    int   getScaleTableSize() const noexcept;
	    int   getScaleStepsPerOctave() const noexcept;
	    int   findNearestScaleStepForMidi (int midiNote) const noexcept;
	    int   findKeyboardScaleStepForMidi (int midiNote) const noexcept;
	    int   getScaleMidi (int idx) const noexcept;
	    double getScaleFrequencyHz (int idx) const noexcept;
	    double getScaleLineWavelengthNm (int idx) const noexcept;
	    float getScaleLineAmplitude (int idx) const noexcept;
	    bool  isSpectralScale() const noexcept;
    juce::String getScaleRangeName() const;
    juce::String getScaleOneOctaveDebugText() const;
    juce::String getEngineSourceName() const;
    juce::String getSamplePlaybackModeName() const;
    juce::String getSpectralElementName() const;
    double getSpectralElementRootWavelengthNm() const noexcept;
    int getSpectralElementLineCount() const noexcept;
    int getActiveGrainCount() const noexcept;

    // Snapshot of currently active seats for the debug panel
    // (UI thread reads, OSC thread writes - atomic per field).
    juce::String getActiveSeatsSnapshot (int maxLines = 24) const;

    float getAuroraBand (int i) const noexcept
    {
        return (i < 0 || i >= AURORA_BANDS) ? 0.0f
             : auroraBands[(size_t) i].load(std::memory_order_relaxed);
    }

    // Per-voice readouts for the UI (lock-free).
    int   getMaxVoices() const noexcept             { return MAX_VOICES; }
    float getVoiceAmp     (int i) const noexcept;
    int   getVoiceMidi    (int i) const noexcept;
    int   getVoiceScaleStep (int i) const noexcept;
    int   getVoiceSeatRow (int i) const noexcept;
    int   getVoiceSeatCol (int i) const noexcept;
    bool  isSeatActive    (int row, int col) const noexcept;
    float getSeatX        (int row, int col) const noexcept;
    float getSeatY        (int row, int col) const noexcept;

private:
    struct Grain
    {
        bool   active   = false;
        bool   reverse  = false;
        double position = 0.0;       // sample offset, fractional
        double rateScale = 1.0;
        float  pan      = 0.0f;
        float  panL     = 1.0f;
        float  panR     = 1.0f;
        int    age      = 0;         // samples elapsed
        int    duration = 0;         // total samples for this grain
    };

    struct Voice
    {
        bool   active     = false;
        bool   releasing  = false;
	        int    seatRow    = -1;
	        int    seatCol    = -1;
	        int    targetMidi = 60;
	        double targetFrequencyHz = 261.6255653005986;

        const SampleLibrary::Sample* sample = nullptr;
        double position    = 0.0;        // master/center playhead
        double playbackRate = 1.0;

        float  amp        = 0.0f;
        float  targetAmp  = 0.0f;
        float  lpCoef       = 0.5f;
        float  targetLpCoef = 0.5f;
        float  lpZL = 0.0f, lpZR = 0.0f;
        float  panL = 0.707f, panR = 0.707f;
        float  lpScale  = 1.0f;
        float  gainScale = 1.0f;   // unison balance (center louder, sides quieter)
        float  velocityGain = 1.0f;
        float  xPos = 0.5f;
        int    sourceMode = 0;
        int    playbackMode = 0;
        int    elementIndex = 0;
        int    elementPartials = 0;
        std::array<float, MAX_ELEMENT_PARTIALS> elementPhase {};

        // per-voice slow LFOs (Hz already encoded as phase increment in rad/sample)
        float pitchLfoPhase = 0.0f, pitchLfoInc = 0.0f, pitchLfoDepth = 0.0f;
        float ampLfoPhase   = 0.0f, ampLfoInc   = 0.0f, ampLfoDepth   = 0.0f;
        float pitchLfoSin = 0.0f, pitchLfoCos = 1.0f;
        float ampLfoSin   = 0.0f, ampLfoCos   = 1.0f;

        std::array<Grain, GRAINS_PER_VOICE> grains;
        int spawnSampleCounter = 0;

        std::atomic<float> uiAmp { 0.0f };
        std::atomic<float> uiX   { 0.5f };
        std::atomic<int>   uiMidi { -1 };
        std::atomic<int>   uiScaleStep { -1 };
        std::atomic<int>   uiSeatRow { -1 };
        std::atomic<int>   uiSeatCol { -1 };
    };

    struct SeatState
    {
        static constexpr int UNISON = 3;
        std::atomic<bool>     active        { false };
        std::atomic<float>    lastX         { 0.5f };
        std::atomic<float>    lastY         { 0.5f };
	        std::atomic<int>      currentMidi   { -1 };
	        std::atomic<int>      currentPitchKey { -1 };
	        std::atomic<uint32_t> lastTriggerMs { 0 };
	        std::array<std::atomic<int>, UNISON> voiceIdx { { {-1}, {-1}, {-1} } };
	    };

    struct VoiceEvent
    {
        enum Type : juce::uint8 { On, Off, XChange, YChange, KeyboardOn, KeyboardOff };
        juce::uint8 type;
        juce::int16 row;
        juce::int16 col;
        float       value;
    };

    struct PitchTarget
    {
        int midi = 60;
        int key = 60;
        double frequencyHz = 261.6255653005986;
        float velocityGain = 1.0f;
    };

    struct KeyboardState
    {
        bool active = false;
        int step = -1;
        int currentMidi = -1;
        int currentPitchKey = -1;
        float velocity = 0.0f;
        std::array<int, SeatState::UNISON> voiceIdx { { -1, -1, -1 } };
    };

	    static int   seatIndex (int row, int col) noexcept;
	    int          xToMidi   (float x) const noexcept;
	    PitchTarget  xToPitch  (float x) const noexcept;
	    PitchTarget  getScalePitch (int idx) const noexcept;
	    void         enqueueEvent (const VoiceEvent& e);
	    void         drainEvents();
	    void         handleEvent (const VoiceEvent& e);
	    void         maybeTrigger (int row, int col, int sIdx, float x, bool forceTrigger);
    void         keyboardOn (int slot, int scaleStep, float velocity);
    void         keyboardPitchOn (int slot, const PitchTarget& target, int scaleStep, float x, float velocity);
    void         keyboardOff (int slot);
	    void         retuneActiveSeatsNow();
	    int          allocateVoice (int row, int col, int midi,
	                                double frequencyHz, float velocityGain,
	                                float detuneCents, float gainScale, float panOffset,
	                                float x, float y, int scaleStep);
    void         freeVoice (int idx);
    void         enqueueMidiEvent (const MidiSourceEvent& e) noexcept;
    void         enqueueSeatMidiNoteOff (int row, int col, int sourceId) noexcept;
    void         enqueueSeatMidiExpression (int row, int col, int sourceId, int midi,
                                            double frequencyHz, float x, float y) noexcept;
    static int   keyboardSourceId (int slot) noexcept { return MAX_SEATS + slot; }
    int          computeAdaptiveUnisonCount (int registeredSeats, int voiceLimit) const noexcept;
    int          countActiveVoices() const noexcept;
    void         releaseExtraUnisonVoices (int desiredUnison) noexcept;
    void         trimVoicesToLimit (int voiceLimit) noexcept;
	    float        computeFilterCoefFromX (float x) const;
	    void         renderVoices (float* L, float* R, int n);
    void         applyReverb  (float* L, float* R, int n);
    void         applyDelay   (float* L, float* R, int n);
    void         applyTapeSaturation (float* L, float* R, int n);
    void         applyLimiter (float* L, float* R, int n);
    int          getScaleStepMidi (int step) const noexcept;
    int          getScaleDegreeOffsetSemis (int midi, int degreeOffset) const noexcept;

    std::array<Voice,     MAX_VOICES> voices;
    std::array<SeatState, MAX_SEATS>  seats;
    std::array<KeyboardState, MAX_KEYBOARD_SLOTS> keyboardSlots;

    juce::CriticalSection                     eventWriteLock;
    juce::AbstractFifo                        eventFifo { EVENT_QUEUE_SIZE };
    std::array<VoiceEvent, EVENT_QUEUE_SIZE>  eventBuffer;
    juce::AbstractFifo                            midiEventFifo { MIDI_EVENT_QUEUE_SIZE };
    std::array<MidiSourceEvent, MIDI_EVENT_QUEUE_SIZE> midiEventBuffer;
    std::atomic<bool>                         clearAllSeatsPending { false };
    std::atomic<bool>                         retuneActiveSeatsPending { false };

	    SampleLibrary library;

	    double sampleRate = 44100.0;
	    uint64_t audioCallbackSampleClock = 0;
    std::atomic<int> activeVoiceCount    { 0 };
    std::atomic<int> registeredSeatCount { 0 };
    std::atomic<int> adaptiveUnisonCount { 3 };

    std::array<std::atomic<float>, AURORA_BANDS> auroraBands;

    // FX
    juce::Reverb reverb;
    std::array<float, MAX_DELAY_SAMPLES> delayBufL {};
    std::array<float, MAX_DELAY_SAMPLES> delayBufR {};
    juce::AudioBuffer<float> dryScratch;
    int delayWriteIdx = 0;
    int delaySamples  = 17640;

    // Limiter state (simple feedback peak limiter)
    float limGain = 1.0f;
    int voiceSearchHint = 0;

    // Library-derived C-major scale table.  buildScaleTable() runs on the
    // message thread after each loadFromDirectory; the audio thread reads
    // it via the atomic count (release/acquire pair).
    std::array<int, 128> scaleTable {};
    std::atomic<int>     scaleTableCount { 0 };
    void buildScaleTable();

    // Granular helpers
    juce::Random rngVoice;                            // audio-thread only
    std::array<std::array<float, HANN_LUT_SIZE>, 4> envelopeLuts {};
    void initEnvelopeLuts();
};
