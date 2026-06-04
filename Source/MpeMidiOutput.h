#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>
#include "MidiPitch.h"

// MpeMidiOutput owns the MIDI-output STATE and emission logic that used to live
// directly inside AudienceProcessor. It has NO dependency on juce::AudioProcessor
// or the APVTS: every value that the moved code previously read from the value
// tree (via rawParam*) is now supplied explicitly through MpeConfig. The class
// also owns the OUTGOING-midi debug state (per-voice debug + the outgoing debug
// ring buffer); AudienceProcessor's debug-text accessors forward to it so the
// produced text is byte-for-byte identical to before.
//
// This is a behaviour-preserving pure move: identical logic, identical message
// ORDER, identical byte sequences. The only structural change is the explicit
// config inputs and the self-contained NoteEvent input struct (so this header
// does not need to include PartialEngine.h).
class MpeMidiOutput
{
public:
    // The maximum number of distinct MIDI sources. This MUST equal
    // PartialEngine::MAX_SEATS (2600) + PartialEngine::MAX_KEYBOARD_SLOTS (64) =
    // 2664. It is kept as a local constant so this translation unit does not
    // depend on PartialEngine, but PluginProcessor.cpp has an authoritative
    // static_assert (against the real PartialEngine constants) that breaks the
    // build if the two ever drift. Seat NoteOn events carry sourceId = row*100 +
    // col (0..2599); keyboard slots use sourceId 2600..2663.
    static constexpr int kMaxMidiSources = 2664;

    // Every APVTS value the moved code reads. AudienceProcessor builds this from
    // its raw parameter pointers before each render.
    struct MpeConfig
    {
        int   outputType          = 0;   // rawParams.midiOutputType (0 off, 1 normal, 2 MPE)
        int   masterChannel       = 1;   // rawParams.mpeMasterChannel
        int   memberFirst         = 2;   // rawParams.mpeMemberFirstChannel
        int   memberLast          = 16;  // rawParams.mpeMemberLastChannel
        int   pitchBendRangeChoice = 3;  // rawParams.mpePitchBendRange (choice index)
        int   normalMidiChannel   = 0;   // rawParams.normalMidiChannel (0-based)
        bool  sendSetupMessages   = true;// rawParams.mpeSendSetupMessages
        int   pitchMode           = 0;   // rawParams.mpePitchMode
        float motionMacro         = 0.5f;// rawParamValue(rawParams.motionMacro, 0.5f)
        float energy              = 0.5f;// rawParamValue(rawParams.energy, 0.5f)
    };

    // Self-contained input event. AudienceProcessor maps
    // PartialEngine::MidiSourceEvent -> NoteEvent with a trivial field copy.
    struct NoteEvent
    {
        enum Type { NoteOn, NoteOff, Expression, AllNotesOff };

        Type   type        = NoteOn;
        int    sourceId     = -1;
        double frequencyHz  = 261.6255653005986;
        float  velocity     = 0.0f;
        float  x            = 0.5f;
        float  y            = 0.5f;
    };

    MpeMidiOutput() { reset(); }

    // Conversions that previously lived as static helpers on AudienceProcessor.
    static int bendRangeFromChoice (int choice) noexcept;
    static int velocityFromUnit (float value) noexcept;
    static int pressureFromUnit (float value) noexcept;

    // Updates the active MPE member-channel range used by the allocation /
    // voice-counting logic and by reset(). AudienceProcessor calls this from
    // processBlock at the same point it used to assign lastMpeMemberFirst/Last,
    // so a subsequent reset() observes the new range exactly as before.
    void setMemberRange (int first, int last) noexcept;

    // Marks the MPE setup (MCM + per-member RPN) as needing to be re-sent. B24:
    // mpeSetupDirty is atomic - this is written on the message thread (processor
    // change-detection) while render() reads+clears it on the audio thread. Relaxed
    // ordering is sufficient: the flag only gates whether the next render re-emits
    // the (idempotent) setup; no other state is published through it.
    void markSetupDirty() noexcept { mpeSetupDirty.store(true, std::memory_order_relaxed); }

    // Clears all output state (voices, channel ownership, counters, debug).
    void reset() noexcept;

    // Emits note-offs for every active voice followed by an all-notes-off/
    // all-sound-off sweep across channels 1..16. Used by the processor's
    // config-change "safety all-off" path; identical bytes to the old
    // AudienceProcessor::sendMidiResetMessages.
    void emitSafetyReset (juce::MidiBuffer& midiMessages, int sampleOffset)
    {
        sendMidiResetMessages(midiMessages, sampleOffset);
    }

    // Top-level render entry point. Equivalent to the old renderOutgoingMidi:
    // emits the MPE setup if dirty, then processes each event in order.
    void render (const MpeConfig& config,
                 const NoteEvent* events, int count,
                 juce::MidiBuffer& midiMessages, int numSamples);

    // ---- atomic counters mirrored from the old processor members ----
    int getMidiNotesSent() const noexcept       { return midiNotesSent.load(std::memory_order_relaxed); }
    int getActiveMpeVoices() const noexcept      { return activeMpeVoices.load(std::memory_order_relaxed); }
    int getAvailableMpeChannels() const noexcept { return availableMpeChannels.load(std::memory_order_relaxed); }

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
        return midiDebugWriteCounter.load(std::memory_order_acquire);
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
        int age = 0;
    };

    int getVoiceDebugCount() const noexcept { return (int) midiVoiceDebug.size(); }
    VoiceDebugSnapshot getVoiceDebugSnapshot (int index) const noexcept;

    // ---- live voice state read by sendImmediateAllNotesOffToExternal ----
    struct ActiveNoteOff
    {
        int channel = 1;
        int note = -1;
    };

    // Returns {channel, note} for every active voice with a valid note, in the
    // same order the old loop over midiOutVoices visited them.
    std::vector<ActiveNoteOff> getActiveNoteOffs() const;

private:
    struct MidiOutVoiceState
    {
        bool active = false;
        int sourceId = -1;
        int channel = 1;
        int note = -1;
        int pitchBend = 8192;
        int pressure = -1;
        int timbre = -1;
        int expression = -1;
        double frequencyHz = 0.0;
        uint32_t age = 0;
    };

    struct MidiVoiceDebugSlot
    {
        std::atomic<int> active { 0 };
        std::atomic<int> sourceId { -1 };
        std::atomic<int> channel { 0 };
        std::atomic<int> note { -1 };
        std::atomic<int> pitchBend { 8192 };
        std::atomic<int> age { 0 };
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
                                juce::MidiBuffer& midiMessages, int sampleOffset);
    void sendAllMidiNotesOff (juce::MidiBuffer& midiMessages, int sampleOffset);
    void sendMidiResetMessages (juce::MidiBuffer& midiMessages, int sampleOffset);
    void sendMpeSetupIfNeeded (const MpeConfig& config, juce::MidiBuffer& midiMessages, int sampleOffset);
    void sendPitchBendRangeRpn (juce::MidiBuffer& midiMessages, int sampleOffset,
                                int channel, int semitones);
    int  allocateMpeChannelForSource (const MpeConfig& config, int sourceId,
                                      juce::MidiBuffer& midiMessages, int sampleOffset);
    void releaseMpeChannelForSource (int sourceId) noexcept;
    void sendNoteOffForSource (const MpeConfig& config, int sourceId,
                               juce::MidiBuffer& midiMessages, int sampleOffset);
    void sendExpressionForSource (const MpeConfig& config, int sourceId, const NoteEvent& event,
                                  juce::MidiBuffer& midiMessages, int sampleOffset, bool force);

    std::array<MidiOutVoiceState, kMaxMidiSources> midiOutVoices {};
    std::array<MidiVoiceDebugSlot, kMaxMidiSources> midiVoiceDebug {};
    std::array<int, 17> mpeChannelOwner {};
    uint32_t midiVoiceAgeCounter = 0;
    std::atomic<bool> mpeSetupDirty { true };  // B24: see markSetupDirty().

    // Active member-channel range. Kept in sync with config by setMemberRange /
    // render; defaults match the old lastMpeMemberFirst/Last initial values.
    int memberFirst = 2;
    int memberLast = 16;

    // Round-robin allocation cursor: allocateMpeChannelForSource scans for a free
    // member channel starting AFTER this cursor and wrapping within
    // [memberFirst, memberLast], then sets the cursor to the channel it returns.
    // This makes a just-freed channel the LAST to be reused, which mitigates a
    // receiver-side per-note pitch-capture race on immediate channel reuse (an
    // offset note landing on a just-freed channel could otherwise be latched with
    // the channel's stale center bend). Initialised to memberLast so the FIRST
    // allocation wraps back to memberFirst, preserving the original first pick.
    // setMemberRange() resets it to the new memberLast on a zone switch.
    int roundRobinCursor = memberLast;

    std::atomic<int> midiNotesSent { 0 };
    std::atomic<int> activeMpeVoices { 0 };
    std::atomic<int> availableMpeChannels { 15 };

    std::array<MidiDebugSlot, kDebugEventQueueSize> midiDebugEvents {};
    std::atomic<uint32_t> midiDebugWriteCounter { 0 };
};
