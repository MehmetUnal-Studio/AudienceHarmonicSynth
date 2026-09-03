#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <juce_audio_basics/juce_audio_basics.h>
#include "MidiPitch.h"

#ifndef COSMIC_MIDI_DIAGNOSTICS
 #define COSMIC_MIDI_DIAGNOSTICS 0
#endif

// MpeMidiOutput is the retained internal class name for preset/source compatibility.
// Since Cosmic Microwave 2.6 it is a Notes Only renderer: it can emit Note On,
// Note Off, and the bounded CC120/CC123 panic sweep, but no MPE, Pitch Bend,
// Pressure, RPN, CC11, CC74, or Crowd Macro messages. It has no dependency on
// juce::AudioProcessor or the APVTS and owns the outgoing MIDI debug state.
//
// It is deliberately independent of the processor and APVTS. The processor
// supplies a compact configuration plus fixed-capacity NoteEvents, keeping the
// audio-thread routing path allocation-free.
class MpeMidiOutput
{
public:
    // One stable semantic ID for every OSC source/finger pair:
    // 256 source IDs x 10 fingers = voice IDs 0..2559.
    static constexpr int kMaxMidiSources = 256 * 10;
    static constexpr int kMaxEventsPerRender = 64;

    // Fixed realtime limits for duration-owned semantic notes. A source is one
    // OSC source/finger voice. Several tails from the same source may coexist
    // after pitch changes, but every dimension remains strictly bounded.
    static constexpr int kMaxScheduledNotes = 4096;
    static constexpr int kMaxScheduledPerChannel = 512;
    // 2n tails retriggered on a 32n grid need 16 simultaneous ownerships for
    // one semantic source before the oldest legitimate tail expires.
    static constexpr int kMaxScheduledPerSource = 16;

    enum class NoteDuration : uint8_t
    {
        Half = 0,       // 2n  / 1/2
        Quarter,        // 4n  / 1/4
        Eighth,         // 8n  / 1/8
        Sixteenth,      // 16n / 1/16
        ThirtySecond    // 32n / 1/32
    };

    // Controls what a newly admitted attack does when its physical MIDI key is
    // already held. Tie preserves the legacy deadline-extension/ref-count
    // behaviour. Retrigger emits a deterministic same-sample NoteOff -> NoteOn
    // while leaving semantic ownership and scheduled deadlines intact.
    enum class SameNotePolicy : uint8_t
    {
        Tie = 0,
        Retrigger
    };

    // A block-level snapshot. Newly admitted notes retain the sample deadline
    // calculated from this snapshot even when tempo/duration changes later.
    struct TimingConfig
    {
        double sampleRate = 48000.0;
        double bpm = 120.0;
        NoteDuration noteDuration = NoteDuration::ThirtySecond;
        SameNotePolicy sameNotePolicy = SameNotePolicy::Tie;
    };

    // The legacy type name is retained to keep the processor diff and old state
    // topology stable. Only outputType, normalMidiChannel and normalRoutingMode
    // affect rendering. All former MPE fields are inert compatibility values.
    struct MpeConfig
    {
        int   outputType          = 0;   // 0 off; every non-zero value becomes Notes Only
        int   masterChannel       = 1;   // legacy/inert
        int   memberFirst         = 2;   // legacy/inert
        int   memberLast          = 16;  // legacy/inert
        int   pitchBendRangeChoice = 3;  // legacy/inert
        int   normalMidiChannel   = 0;   // rawParams.normalMidiChannel (0-based)
        int   normalRoutingMode   = 0;   // 0 single channel, 1 participant round-robin
        bool  sendSetupMessages   = false; // legacy/inert
        int   pitchMode           = 0;   // legacy/inert
    };

    // Self-contained input event produced by the OSC finger router.
    struct NoteEvent
    {
        enum Type { NoteOn, NoteOff, Expression, AllNotesOff, CancelVoice };

        Type   type        = NoteOn;
        int    sourceId     = -1;
        int    participantId = -1; // stable OSC participant/source id; -1 uses configured channel
        int    sampleOffset  = 0;  // block-relative emission time, clamped by render()
        double frequencyHz  = 261.6255653005986;
        float  velocity     = 0.0f;
        float  x            = 0.5f;
        float  y            = 0.5f;
    };

    MpeMidiOutput() { reset(); }

    // Conversions that previously lived as static helpers on AudienceProcessor.
    static int bendRangeFromChoice (int choice) noexcept;
    static int velocityFromUnit (float value) noexcept;
    static uint64_t durationSamplesFor (const TimingConfig& timing) noexcept;

    // Maps a participant id to a 1-based MIDI channel. With the default 1-based
    // participant ids this yields 1->ch1, 16->ch16, 17->ch1, and so on.
    static int normalChannelForParticipant (int participantId,
                                            int sourceIdBase = 1) noexcept;

    // Compatibility no-op. Member channels do not exist in Notes Only mode.
    void setMemberRange (int first, int last) noexcept;

    // Compatibility no-op. Notes Only never emits setup/RPN messages.
    void markSetupDirty() noexcept {}

    // Clears all output state (voices, channel ownership, counters, debug).
    void reset() noexcept;

    // Emits a bounded all-notes-off/all-sound-off sweep across channels 1..16.
    // Used by the processor's config-change safety path without emitting
    // thousands of individual Note Off messages on the realtime thread.
    void emitSafetyReset (juce::MidiBuffer& midiMessages, int sampleOffset);

    // Top-level bounded Notes Only render entry point. This overload must be
    // called once for every audio block, including blocks with zero input
    // events, so sample-deadline Note Offs are emitted on time.
    void render (const MpeConfig& config,
                 const NoteEvent* events, int count,
                 juce::MidiBuffer& midiMessages, int numSamples,
                 const TimingConfig& timing);

    // Source-compatible bridge for callers not yet supplying host timing. It
    // uses the deterministic 120 BPM / 48 kHz / 32n fallback.
    void render (const MpeConfig& config,
                 const NoteEvent* events, int count,
                 juce::MidiBuffer& midiMessages, int numSamples);

    // Counts physical Note On messages appended to the outgoing MidiBuffer
    // since the latest reset. Semantic attacks absorbed by Tie are excluded.
    int getMidiNotesSent() const noexcept       { return midiNotesSent.load(std::memory_order_relaxed); }
    int getScheduledNoteCount() const noexcept  { return scheduledNoteCount.load(std::memory_order_relaxed); }
    int getPhysicalNoteCount() const noexcept   { return physicalNoteCount.load(std::memory_order_relaxed); }
    uint64_t getDeadlineReleaseCount() const noexcept { return deadlineReleaseCount.load(std::memory_order_relaxed); }
    uint64_t getCoalescedRetriggerCount() const noexcept { return coalescedRetriggerCount.load(std::memory_order_relaxed); }
    uint64_t getHardRetriggerCount() const noexcept { return hardRetriggerCount.load(std::memory_order_relaxed); }
    uint64_t getCapacityStealCount() const noexcept { return capacityStealCount.load(std::memory_order_relaxed); }
    uint64_t getGlobalLimitStealCount() const noexcept { return globalLimitStealCount.load(std::memory_order_relaxed); }
    uint64_t getChannelLimitStealCount() const noexcept { return channelLimitStealCount.load(std::memory_order_relaxed); }
    uint64_t getSourceLimitStealCount() const noexcept { return sourceLimitStealCount.load(std::memory_order_relaxed); }
    uint64_t getSafetyResetCount() const noexcept { return safetyResetCount.load(std::memory_order_relaxed); }

    // Audio-thread-only reconciliation surface. A voice ID is reported when its
    // final fixed-duration ownership token ends during the latest render().
    // The processor uses this to release Grid's admission lease without
    // estimating a sample deadline from beat time (which would drift when the
    // host tempo changes after the attack).
    int getEndedVoiceCount() const noexcept { return endedVoiceCount; }
    int getEndedVoiceId (int index) const noexcept
    {
        return index >= 0 && index < endedVoiceCount
             ? endedVoiceIds[(size_t) index] : -1;
    }
    int getScheduledNoteCountForVoice (int voiceId) const noexcept
    {
        return voiceId >= 0 && voiceId < kMaxMidiSources
             ? (int) sourceScheduledCounts[(size_t) voiceId] : 0;
    }

    // ---- OUTGOING-midi debug ring (moved from AudienceProcessor) ----
    // Records the bytes of every <=3-byte message in the buffer, exactly as the
    // old recordOutgoingMidiDebugEvents did.
    void recordOutgoingMidiDebugEvents (const juce::MidiBuffer& midiMessages) noexcept;

    static constexpr int kDebugEventQueueSize = 256;

    // Snapshot of one outgoing debug ring slot, in the same shape the old
    // getOutgoingMidiDebugText loop read.
    struct OutgoingDebugEvent
    {
        int sampleOffset = 0;
        int size = 0;
        int b0 = 0;
        int b1 = 0;
        int b2 = 0;
    };

    uint32_t getOutgoingDebugLatest() const noexcept
    {
#if COSMIC_MIDI_DIAGNOSTICS
        return midiDebugWriteCounter.load(std::memory_order_acquire);
#else
        return 0;
#endif
    }

    // Reads the slot for sequence number `seq`. Returns false (and leaves `out`
    // untouched) if the slot has been overwritten - mirroring the old
    // `storedSeq != seq` continue-guard so iteration behaviour is identical.
    bool readOutgoingDebugSlot (uint32_t seq, OutgoingDebugEvent& out) const noexcept;

    // ---- per-voice debug (moved from AudienceProcessor::midiVoiceDebug) ----
    struct VoiceDebugSnapshot
    {
        int active = 0;
        int sourceId = -1;
        int channel = 0;
        int note = -1;
        int pitchBend = 8192;
        uint64_t age = 0;
    };

    int getVoiceDebugCount() const noexcept
    {
#if COSMIC_MIDI_DIAGNOSTICS
        return (int) midiVoiceDebug.size();
#else
        return 0;
#endif
    }
    VoiceDebugSnapshot getVoiceDebugSnapshot (int index) const noexcept;

private:
    struct ScheduledNote
    {
        bool active = false;
        int sourceId = -1;
        int channel = 1;
        int note = -1;
        uint64_t deadlineSample = 0;
        uint64_t age = 0;
        int heapIndex = -1;
        int nextFree = -1;
    };

    struct MidiVoiceDebugSlot
    {
        std::atomic<int> active { 0 };
        std::atomic<int> sourceId { -1 };
        std::atomic<int> channel { 0 };
        std::atomic<int> note { -1 };
        std::atomic<int> pitchBend { 8192 };
        std::atomic<uint64_t> age { 0 };
    };

    struct MidiDebugSlot
    {
        std::atomic<uint32_t> sequence { 0 };
        std::atomic<int> sampleOffset { 0 };
        std::atomic<int> size { 0 };
        std::atomic<int> byte0 { 0 };
        std::atomic<int> byte1 { 0 };
        std::atomic<int> byte2 { 0 };
    };

    // ---- moved emission helpers (formerly AudienceProcessor methods) ----
    void handleMidiSourceEvent (const MpeConfig& config, const NoteEvent& event,
                                juce::MidiBuffer& midiMessages, int sampleOffset,
                                uint64_t absoluteSample,
                                uint64_t durationSamples,
                                SameNotePolicy sameNotePolicy);
    void sendAllMidiNotesOff (juce::MidiBuffer& midiMessages, int sampleOffset);
    void sendMidiResetMessages (juce::MidiBuffer& midiMessages, int sampleOffset);
    void clearScheduledState() noexcept;
    void failClosed (juce::MidiBuffer& midiMessages, int sampleOffset,
                     bool resetClock) noexcept;
    void cancelVoice (int sourceId, juce::MidiBuffer& midiMessages,
                      int sampleOffset) noexcept;
    void startScheduledNote (const MpeConfig& config, const NoteEvent& event,
                             juce::MidiBuffer& midiMessages, int sampleOffset,
                             uint64_t absoluteSample,
                             uint64_t durationSamples,
                             SameNotePolicy sameNotePolicy) noexcept;
    bool releaseScheduledNote (int slot, juce::MidiBuffer& midiMessages,
                               int sampleOffset, bool deadlineRelease) noexcept;
    void releaseDueNotes (uint64_t limitSample, bool inclusive,
                          uint64_t blockStart, int lastSample,
                          juce::MidiBuffer& midiMessages) noexcept;

    int findMatchingToken (int sourceId, int channel, int note) const noexcept;
    int findOldestForSource (int sourceId) const noexcept;
    int findOldestForChannel (int channel) const noexcept;
    int findOldestGlobal() const noexcept;
    bool registerSourceToken (int sourceId, int slot) noexcept;
    void unregisterSourceToken (int sourceId, int slot) noexcept;

    bool heapLess (int lhsSlot, int rhsSlot) const noexcept;
    void heapSwap (int lhsIndex, int rhsIndex) noexcept;
    void heapSiftUp (int heapIndex) noexcept;
    void heapSiftDown (int heapIndex) noexcept;
    void heapInsert (int slot) noexcept;
    void heapRemove (int slot) noexcept;
    void refreshVoiceDebug (int sourceId) noexcept;
    void recordEndedVoice (int sourceId) noexcept;

    std::array<ScheduledNote, kMaxScheduledNotes> scheduledNotes {};
    std::array<int, kMaxScheduledNotes> deadlineHeap {};
    std::array<std::array<int, kMaxScheduledPerSource>, kMaxMidiSources>
        sourceTokenSlots {};
    std::array<uint16_t, kMaxMidiSources> sourceScheduledCounts {};
    std::array<uint16_t, 16> channelScheduledCounts {};
#if COSMIC_MIDI_DIAGNOSTICS
    std::array<MidiVoiceDebugSlot, kMaxMidiSources> midiVoiceDebug {};
#endif
    std::array<uint16_t, 16 * 128> normalNoteRefCounts {};
    int deadlineHeapSize = 0;
    int freeTokenHead = -1;
    int activeScheduledNotes = 0;
    int activePhysicalNotes = 0;
    std::array<int, kMaxMidiSources> endedVoiceIds {};
    std::array<std::uint8_t, kMaxMidiSources> endedVoiceRecorded {};
    int endedVoiceCount = 0;
    uint64_t renderSampleCursor = 0;
    uint64_t midiVoiceAgeCounter = 0;

    std::atomic<int> midiNotesSent { 0 };
    std::atomic<int> scheduledNoteCount { 0 };
    std::atomic<int> physicalNoteCount { 0 };
    std::atomic<uint64_t> deadlineReleaseCount { 0 };
    std::atomic<uint64_t> coalescedRetriggerCount { 0 };
    std::atomic<uint64_t> hardRetriggerCount { 0 };
    std::atomic<uint64_t> capacityStealCount { 0 };
    std::atomic<uint64_t> globalLimitStealCount { 0 };
    std::atomic<uint64_t> channelLimitStealCount { 0 };
    std::atomic<uint64_t> sourceLimitStealCount { 0 };
    std::atomic<uint64_t> safetyResetCount { 0 };

#if COSMIC_MIDI_DIAGNOSTICS
    std::array<MidiDebugSlot, kDebugEventQueueSize> midiDebugEvents {};
    std::atomic<uint32_t> midiDebugWriteCounter { 0 };
#endif
};
