#include "../Source/MpeMidiOutput.h"
#include "../Source/MidiPitch.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    int g_failed = 0;

    void expect (bool ok, const char* name)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << name << "\n";
        if (! ok)
            ++g_failed;
    }

    // Decoded view of one short MIDI message, in the order it appears in the buffer.
    struct Msg
    {
        int status = 0;   // high nibble (0x80..0xE0)
        int channel = 0;  // 1-based
        int d1 = 0;
        int d2 = 0;
        int size = 0;
    };

    std::vector<Msg> decode (const juce::MidiBuffer& buffer)
    {
        std::vector<Msg> out;
        for (const auto metadata : buffer)
        {
            const auto m = metadata.getMessage();
            const auto* raw = m.getRawData();
            const int size = m.getRawDataSize();
            if (size <= 0 || size > 3)
                continue;

            Msg msg;
            msg.size = size;
            msg.status = raw[0] & 0xf0;
            msg.channel = (raw[0] & 0x0f) + 1;
            msg.d1 = size > 1 ? (raw[1] & 0x7f) : 0;
            msg.d2 = size > 2 ? (raw[2] & 0x7f) : 0;
            out.push_back(msg);
        }
        return out;
    }

    constexpr int kNoteOn   = 0x90;
    constexpr int kNoteOff  = 0x80;
    constexpr int kCC       = 0xb0;
    constexpr int kPressure = 0xd0;
    constexpr int kBend     = 0xe0;

    bool isController (const Msg& m, int ch, int cc, int val)
    {
        return m.status == kCC && m.channel == ch && m.d1 == cc && m.d2 == val;
    }

    int pitchWheelValue (const Msg& m)
    {
        return m.d1 + (m.d2 << 7);
    }

    // Index of the first message matching a predicate, or -1.
    template <typename Pred>
    int indexOf (const std::vector<Msg>& msgs, Pred pred)
    {
        for (int i = 0; i < (int) msgs.size(); ++i)
            if (pred(msgs[(size_t) i]))
                return i;
        return -1;
    }

    MpeMidiOutput::MpeConfig mpeConfig (int memberFirst = 2, int memberLast = 16,
                                        int pitchBendChoice = 3)
    {
        MpeMidiOutput::MpeConfig c;
        c.outputType = 2;
        c.masterChannel = 1;
        c.memberFirst = memberFirst;
        c.memberLast = memberLast;
        c.pitchBendRangeChoice = pitchBendChoice;
        c.normalMidiChannel = 0;
        c.sendSetupMessages = true;
        c.pitchMode = 0;
        c.motionMacro = 0.5f;
        c.energy = 0.5f;
        return c;
    }

    // Upper-zone MPE config (B8): master 16, members 1..15. The zone fully
    // determines the channel layout; this mirrors what buildMpeConfig() derives
    // for mpeZone == 1.
    MpeMidiOutput::MpeConfig mpeConfigUpper (int pitchBendChoice = 3)
    {
        MpeMidiOutput::MpeConfig c;
        c.outputType = 2;
        c.masterChannel = 16;
        c.memberFirst = 1;
        c.memberLast = 15;
        c.pitchBendRangeChoice = pitchBendChoice;
        c.normalMidiChannel = 0;
        c.sendSetupMessages = true;
        c.pitchMode = 0;
        c.motionMacro = 0.5f;
        c.energy = 0.5f;
        return c;
    }

    MpeMidiOutput::NoteEvent noteOn (int sourceId, double freqHz, float vel = 0.8f,
                                     float x = 0.5f, float y = 0.5f)
    {
        MpeMidiOutput::NoteEvent e;
        e.type = MpeMidiOutput::NoteEvent::NoteOn;
        e.sourceId = sourceId;
        e.frequencyHz = freqHz;
        e.velocity = vel;
        e.x = x;
        e.y = y;
        return e;
    }

    MpeMidiOutput::NoteEvent noteOff (int sourceId)
    {
        MpeMidiOutput::NoteEvent e;
        e.type = MpeMidiOutput::NoteEvent::NoteOff;
        e.sourceId = sourceId;
        return e;
    }

    // A4 = 440 Hz lands exactly on note 69 (centered bend); convenient base note.
    constexpr double kA4 = 440.0;
}

int main()
{
    // ---- Test 1: MPE setup messages on first note ----
    {
        MpeMidiOutput mpe;
        const auto config = mpeConfig(2, 16, 3); // bend range choice 3 -> 48 st
        const auto bendRange = MpeMidiOutput::bendRangeFromChoice(3);
        const int memberCount = 16 - 2 + 1; // 15

        juce::MidiBuffer buffer;
        const auto ev = noteOn(0, kA4);
        mpe.render(config, &ev, 1, buffer, 64);
        const auto msgs = decode(buffer);

        // MCM on master channel 1, in order, at the very front.
        const bool mcmOk = msgs.size() >= 3
            && isController(msgs[0], 1, 101, 0)
            && isController(msgs[1], 1, 100, 6)
            && isController(msgs[2], 1, 6, memberCount);
        expect(mcmOk, "MPE setup emits MCM (CC101=0, CC100=6, CC6=memberCount) on master ch1 first");

        // Per-member pitch-bend-range RPN: each member channel gets CC6=bendRange.
        bool allMembersHaveRpn = true;
        for (int ch = 2; ch <= 16; ++ch)
        {
            const int idx = indexOf(msgs, [ch, bendRange] (const Msg& m)
                                    { return isController(m, ch, 6, bendRange); });
            if (idx < 0)
                allMembersHaveRpn = false;
        }
        expect(allMembersHaveRpn, "MPE setup emits per-member pitch-bend-range RPN (CC6=bendRange) on ch2..16");
    }

    // ---- Test 2: Note-on ordering (pitchWheel before noteOn) + expression CCs ----
    {
        MpeMidiOutput mpe;
        const auto config = mpeConfig();

        juce::MidiBuffer buffer;
        const auto ev = noteOn(0, kA4 * std::pow(2.0, 0.30 / 12.0), 0.8f, 0.6f, 0.4f);
        mpe.render(config, &ev, 1, buffer, 64);
        const auto msgs = decode(buffer);

        // Locate the note-on (on a member channel >= 2) and the pitch wheel that
        // shares its channel.
        const int noteOnIdx = indexOf(msgs, [] (const Msg& m)
            { return m.status == kNoteOn && m.channel >= 2 && m.d2 > 0; });
        expect(noteOnIdx >= 0 && msgs[(size_t) noteOnIdx].channel >= 2,
               "Note-on emitted on an MPE member channel (>=2)");

        const int memberCh = noteOnIdx >= 0 ? msgs[(size_t) noteOnIdx].channel : -1;

        const int bendIdx = indexOf(msgs, [memberCh] (const Msg& m)
            { return m.status == kBend && m.channel == memberCh; });
        expect(bendIdx >= 0 && noteOnIdx >= 0 && bendIdx < noteOnIdx,
               "pitchWheel precedes noteOn on the member channel");

        // CC74 (timbre) and CC11 (expression) precede the note-on; channel
        // pressure follows it (matching the historical order).
        const int cc74Idx = indexOf(msgs, [memberCh] (const Msg& m)
            { return m.status == kCC && m.channel == memberCh && m.d1 == 74; });
        const int cc11Idx = indexOf(msgs, [memberCh] (const Msg& m)
            { return m.status == kCC && m.channel == memberCh && m.d1 == 11; });
        const int pressureIdx = indexOf(msgs, [memberCh] (const Msg& m)
            { return m.status == kPressure && m.channel == memberCh; });

        expect(cc74Idx >= 0 && cc11Idx >= 0 && noteOnIdx >= 0
               && cc74Idx < noteOnIdx && cc11Idx < noteOnIdx,
               "CC74 (timbre) and CC11 (expression) precede the note-on");
        expect(pressureIdx >= 0 && noteOnIdx >= 0 && pressureIdx > noteOnIdx,
               "channel pressure is emitted at note-on (after the noteOn)");
    }

    // ---- Test 3: Allocation - 3 distinct sources -> 3 distinct member channels ----
    {
        MpeMidiOutput mpe;
        const auto config = mpeConfig();

        juce::MidiBuffer buffer;
        const MpeMidiOutput::NoteEvent evs[] {
            noteOn(0, 220.0),
            noteOn(1, 277.18),
            noteOn(2, 329.63),
        };
        mpe.render(config, evs, 3, buffer, 64);
        const auto msgs = decode(buffer);

        std::vector<int> noteOnChannels;
        for (const auto& m : msgs)
            if (m.status == kNoteOn && m.d2 > 0)
                noteOnChannels.push_back(m.channel);

        bool threeNotes = noteOnChannels.size() == 3;
        bool distinct = threeNotes
            && noteOnChannels[0] != noteOnChannels[1]
            && noteOnChannels[0] != noteOnChannels[2]
            && noteOnChannels[1] != noteOnChannels[2];
        bool allMembers = true;
        for (int ch : noteOnChannels)
            if (ch < 2 || ch > 16)
                allMembers = false;

        expect(threeNotes && distinct && allMembers,
               "three distinct sources allocate three distinct member channels");
        expect(mpe.getActiveMpeVoices() == 3,
               "active MPE voice count is 3 after three note-ons");
    }

    // ---- Test 4: Voice-stealing - more sources than channels steals oldest ----
    {
        // member range 2..4 -> only 3 channels available.
        const int first = 2, last = 4;
        MpeMidiOutput mpe;
        mpe.setMemberRange(first, last);
        const auto config = mpeConfig(first, last);

        // Fill all 3 channels first.
        {
            juce::MidiBuffer warmup;
            const MpeMidiOutput::NoteEvent evs[] {
                noteOn(0, 220.0),
                noteOn(1, 277.18),
                noteOn(2, 329.63),
            };
            mpe.render(config, evs, 3, warmup, 64);
        }

        // Identify the channel owned by the oldest source (source 0).
        int oldestChannel = -1;
        for (int i = 0; i < mpe.getVoiceDebugCount(); ++i)
        {
            const auto v = mpe.getVoiceDebugSnapshot(i);
            if (v.active && v.sourceId == 0)
                oldestChannel = v.channel;
        }
        expect(oldestChannel >= first && oldestChannel <= last,
               "oldest source (0) occupies a valid member channel before stealing");

        // A fourth simultaneous source must steal the oldest channel.
        juce::MidiBuffer buffer;
        const auto ev = noteOn(3, 392.0);
        mpe.render(config, &ev, 1, buffer, 64);
        const auto msgs = decode(buffer);

        // The stolen voice gets a note-off on the oldest channel...
        const int stolenNoteOffIdx = indexOf(msgs, [oldestChannel] (const Msg& m)
            { return m.status == kNoteOff && m.channel == oldestChannel; });
        // ...and the new note-on reuses that same channel afterwards.
        const int newNoteOnIdx = indexOf(msgs, [oldestChannel] (const Msg& m)
            { return m.status == kNoteOn && m.channel == oldestChannel && m.d2 > 0; });

        expect(stolenNoteOffIdx >= 0,
               "voice-stealing emits a note-off on the oldest source's channel");
        expect(newNoteOnIdx >= 0 && stolenNoteOffIdx >= 0 && stolenNoteOffIdx < newNoteOnIdx,
               "stolen channel is reused: note-off precedes the new note-on on that channel");

        // Source 0 should no longer be active; source 3 now owns its channel.
        bool source0Gone = true;
        bool source3OwnsOldChannel = false;
        for (int i = 0; i < mpe.getVoiceDebugCount(); ++i)
        {
            const auto v = mpe.getVoiceDebugSnapshot(i);
            if (v.active && v.sourceId == 0)
                source0Gone = false;
            if (v.active && v.sourceId == 3 && v.channel == oldestChannel)
                source3OwnsOldChannel = true;
        }
        expect(source0Gone && source3OwnsOldChannel,
               "oldest source released and its channel reassigned to the new source");
    }

    // ---- Test 5: Note-off (MPE) emits noteOff + pressure 0 + pitchWheel 8192 ----
    {
        MpeMidiOutput mpe;
        const auto config = mpeConfig();

        // Note-on, then note-off on the same source.
        juce::MidiBuffer onBuffer;
        const auto on = noteOn(0, kA4);
        mpe.render(config, &on, 1, onBuffer, 64);

        int memberCh = -1;
        for (const auto& m : decode(onBuffer))
            if (m.status == kNoteOn && m.d2 > 0)
                memberCh = m.channel;
        expect(memberCh >= 2, "note held on a member channel before note-off");

        juce::MidiBuffer offBuffer;
        const auto off = noteOff(0);
        mpe.render(config, &off, 1, offBuffer, 64);
        const auto msgs = decode(offBuffer);

        const bool hasNoteOff = indexOf(msgs, [memberCh] (const Msg& m)
            { return m.status == kNoteOff && m.channel == memberCh; }) >= 0;
        const bool hasPressureZero = indexOf(msgs, [memberCh] (const Msg& m)
            { return m.status == kPressure && m.channel == memberCh && m.d1 == 0; }) >= 0;
        const bool hasBendCenter = indexOf(msgs, [memberCh] (const Msg& m)
            { return m.status == kBend && m.channel == memberCh && pitchWheelValue(m) == 8192; }) >= 0;

        expect(hasNoteOff, "MPE note-off emits a note-off on the member channel");
        expect(hasPressureZero, "MPE note-off emits channel pressure 0 on the member channel");
        expect(hasBendCenter, "MPE note-off recenters pitch wheel to 8192 on the member channel");
    }

    // ---- Test 6: Bend scaling matches convertFrequencyToMidiPitch exactly ----
    {
        const int bendChoice = 0; // range 2 semitones -> small detune = large bend
        const int bendRange = MpeMidiOutput::bendRangeFromChoice(bendChoice);
        expect(bendRange == 2, "bendRangeFromChoice(0) == 2 semitones");

        // ~+40 cents above A4: well inside a 2-semitone range, clearly off-center.
        const double freq = kA4 * std::pow(2.0, 40.0 / 1200.0);
        const auto expectedPitch = convertFrequencyToMidiPitch(freq, bendRange);

        MpeMidiOutput mpe;
        const auto config = mpeConfig(2, 16, bendChoice);

        juce::MidiBuffer buffer;
        const auto ev = noteOn(0, freq);
        mpe.render(config, &ev, 1, buffer, 64);
        const auto msgs = decode(buffer);

        int memberCh = -1;
        for (const auto& m : msgs)
            if (m.status == kNoteOn && m.d2 > 0)
                memberCh = m.channel;

        // The note-on pitch wheel for this voice (the bend that precedes the
        // note-on on the member channel) must equal the converter's 14-bit value.
        int emittedBend = -1;
        for (const auto& m : msgs)
            if (m.status == kBend && m.channel == memberCh)
                emittedBend = pitchWheelValue(m); // last bend before/at note-on

        expect(expectedPitch.pitchBend14Bit != 8192,
               "chosen frequency produces a non-centered bend (sanity)");
        expect(emittedBend == expectedPitch.pitchBend14Bit,
               "emitted 14-bit pitch wheel equals convertFrequencyToMidiPitch value");
        // The note number must also match the converter.
        const int noteOnNote = indexOf(msgs, [memberCh] (const Msg& m)
            { return m.status == kNoteOn && m.channel == memberCh && m.d2 > 0; });
        expect(noteOnNote >= 0 && msgs[(size_t) noteOnNote].d1 == expectedPitch.noteNumber,
               "emitted note number equals convertFrequencyToMidiPitch note");
    }

    // ---- Test 7: Non-MPE (normal) - noteOn on configured channel, no MCM/RPN ----
    {
        MpeMidiOutput mpe;
        MpeMidiOutput::MpeConfig config;
        config.outputType = 1;            // normal MIDI
        config.normalMidiChannel = 4;     // 0-based -> channel 5
        config.sendSetupMessages = true;  // must be ignored for normal output
        config.pitchBendRangeChoice = 3;

        juce::MidiBuffer buffer;
        const auto ev = noteOn(0, kA4);
        mpe.render(config, &ev, 1, buffer, 64);
        const auto msgs = decode(buffer);

        const int noteOnIdx = indexOf(msgs, [] (const Msg& m)
            { return m.status == kNoteOn && m.d2 > 0; });
        expect(noteOnIdx >= 0 && msgs[(size_t) noteOnIdx].channel == 5,
               "normal MIDI note-on lands on configured channel (normalMidiChannel+1)");

        // No MPE configuration message (the RPN MSB CC101) anywhere.
        const bool anyRpn = indexOf(msgs, [] (const Msg& m)
            { return m.status == kCC && (m.d1 == 101 || m.d1 == 100); }) >= 0;
        expect(! anyRpn, "normal MIDI emits no MCM / RPN messages");

        // No pitch wheel for normal output at note-on.
        const bool anyBend = indexOf(msgs, [] (const Msg& m)
            { return m.status == kBend; }) >= 0;
        expect(! anyBend, "normal MIDI emits no pitch-wheel at note-on");
    }

    // ---- Test 8: AllNotesOff / panic resets channels (CC123/CC120) + state ----
    {
        MpeMidiOutput mpe;
        const auto config = mpeConfig();

        // Hold two notes.
        {
            juce::MidiBuffer warmup;
            const MpeMidiOutput::NoteEvent evs[] { noteOn(0, 220.0), noteOn(1, 277.18) };
            mpe.render(config, evs, 2, warmup, 64);
        }
        expect(mpe.getActiveMpeVoices() == 2, "two voices active before AllNotesOff");

        juce::MidiBuffer buffer;
        MpeMidiOutput::NoteEvent panic;
        panic.type = MpeMidiOutput::NoteEvent::AllNotesOff;
        mpe.render(config, &panic, 1, buffer, 64);
        const auto msgs = decode(buffer);

        // Every channel 1..16 gets All Notes Off (CC123=0) and All Sound Off (CC120=0).
        bool allChannelsReset = true;
        for (int ch = 1; ch <= 16; ++ch)
        {
            const bool cc123 = indexOf(msgs, [ch] (const Msg& m)
                { return isController(m, ch, 123, 0); }) >= 0;
            const bool cc120 = indexOf(msgs, [ch] (const Msg& m)
                { return isController(m, ch, 120, 0); }) >= 0;
            if (! cc123 || ! cc120)
                allChannelsReset = false;
        }
        expect(allChannelsReset, "AllNotesOff emits CC123 and CC120 on every channel 1..16");

        expect(mpe.getActiveMpeVoices() == 0 && mpe.getMidiNotesSent() == 0,
               "AllNotesOff resets internal voice/counter state");

        // After reset, available channels equal the full member pool.
        expect(mpe.getAvailableMpeChannels() == 16 - 2 + 1,
               "AllNotesOff restores the full member-channel pool");
    }

    // ---- Test 9: High sourceId capacity regression (seat + keyboard range) ----
    // Seat NoteOn events carry sourceId = row*100 + col (0..2599); keyboard slots
    // use sourceId 2600..2663 (max valid index 2663). kMaxMidiSources MUST be
    // 2664 so every one of these is accepted. The old buggy capacity (68) silently
    // dropped any source with sourceId >= 68 via the `sourceId >= midiOutVoices.size()`
    // guards, so all three of these would have produced NO output.
    {
        // Guard the constant directly so a silent shrink fails loudly here too.
        expect(MpeMidiOutput::kMaxMidiSources == 2664,
               "kMaxMidiSources == 2664 (PartialEngine::MAX_SEATS 2600 + MAX_KEYBOARD_SLOTS 64)");

        // 100 = seat (row 1, col 0); 2599 = last seat (row 25, col 99);
        // 2663 = last keyboard slot = MAX_SEATS + 63 = highest valid index.
        const int highSources[] { 100, 2599, 2663 };

        for (int sourceId : highSources)
        {
            MpeMidiOutput mpe;
            const auto config = mpeConfig();

            juce::MidiBuffer buffer;
            const auto ev = noteOn(sourceId, kA4);
            mpe.render(config, &ev, 1, buffer, 64);
            const auto msgs = decode(buffer);

            // The note-on must be emitted on an MPE member channel (>= 2).
            const int noteOnIdx = indexOf(msgs, [] (const Msg& m)
                { return m.status == kNoteOn && m.channel >= 2 && m.d2 > 0; });
            const int memberCh = noteOnIdx >= 0 ? msgs[(size_t) noteOnIdx].channel : -1;

            // ...and a pitch wheel must precede it on that same member channel.
            const int bendIdx = indexOf(msgs, [memberCh] (const Msg& m)
                { return m.status == kBend && m.channel == memberCh; });

            const std::string label = "high sourceId " + std::to_string(sourceId);
            expect(noteOnIdx >= 0 && memberCh >= 2,
                   (label + ": NoteOn emitted on an MPE member channel (>=2)").c_str());
            expect(bendIdx >= 0 && noteOnIdx >= 0 && bendIdx < noteOnIdx,
                   (label + ": pitchWheel precedes the noteOn on the member channel").c_str());
            expect(mpe.getActiveMpeVoices() == 1,
                   (label + ": one active MPE voice after the note-on").c_str());
        }
    }

    // ---- Test 10: Upper zone (B8) - master ch16, members ch1..15 ----
    // The MPE zone now fully determines the channel layout. Upper => master 16,
    // members 1..15. This must hold while every Lower-zone assertion above keeps
    // passing (Lower is verified by Tests 1-9 with the default master1/members2-16).
    {
        MpeMidiOutput mpe;
        const auto config = mpeConfigUpper(3); // bend range choice 3 -> 48 st
        const auto bendRange = MpeMidiOutput::bendRangeFromChoice(3);
        const int memberCount = 15 - 1 + 1; // 15 members (ch1..15)

        // ---- setup messages: MCM on master ch16, RPN on each member ch1..15 ----
        juce::MidiBuffer onBuffer;
        const auto on = noteOn(0, kA4);
        mpe.render(config, &on, 1, onBuffer, 64);
        const auto onMsgs = decode(onBuffer);

        // MCM (CC101=0, CC100=6, CC6=memberCount) on master channel 16, in order,
        // at the very front - the Upper-zone analogue of Test 1's ch1 MCM.
        const bool mcmOk = onMsgs.size() >= 3
            && isController(onMsgs[0], 16, 101, 0)
            && isController(onMsgs[1], 16, 100, 6)
            && isController(onMsgs[2], 16, 6, memberCount);
        expect(mcmOk, "Upper zone: MCM (CC101=0, CC100=6, CC6=15) emitted on master ch16 first");

        // Per-member pitch-bend-range RPN: each member channel 1..15 gets CC6=bendRange.
        bool allMembersHaveRpn = true;
        for (int ch = 1; ch <= 15; ++ch)
        {
            const int idx = indexOf(onMsgs, [ch, bendRange] (const Msg& m)
                                    { return isController(m, ch, 6, bendRange); });
            if (idx < 0)
                allMembersHaveRpn = false;
        }
        expect(allMembersHaveRpn, "Upper zone: per-member pitch-bend-range RPN (CC6=bendRange) on ch1..15");

        // The master channel 16 must NOT receive a per-member RPN (it is the zone
        // master, not a member) - confirms members 1..15 never include the master.
        // The only CC6 on ch16 is the MCM's CC6=memberCount(15); the member RPN
        // value is bendRange(48), so a CC6=48 on ch16 would mean the master got
        // treated as a member.
        const bool masterHasMemberBend = indexOf(onMsgs, [bendRange] (const Msg& m)
            { return m.status == kCC && m.channel == 16 && m.d1 == 6 && m.d2 == bendRange; }) >= 0;
        expect(! masterHasMemberBend,
               "Upper zone: master ch16 receives no per-member RPN (member range excludes master)");

        // ---- note allocates to a member channel in 1..15, pitchWheel before noteOn ----
        const int noteOnIdx = indexOf(onMsgs, [] (const Msg& m)
            { return m.status == kNoteOn && m.channel >= 1 && m.channel <= 15 && m.d2 > 0; });
        expect(noteOnIdx >= 0,
               "Upper zone: note-on emitted on an MPE member channel (1..15)");

        const int memberCh = noteOnIdx >= 0 ? onMsgs[(size_t) noteOnIdx].channel : -1;
        expect(memberCh >= 1 && memberCh <= 15 && memberCh != 16,
               "Upper zone: member channel is in 1..15 and is not the master (16)");

        const int bendIdx = indexOf(onMsgs, [memberCh] (const Msg& m)
            { return m.status == kBend && m.channel == memberCh; });
        expect(bendIdx >= 0 && noteOnIdx >= 0 && bendIdx < noteOnIdx,
               "Upper zone: pitchWheel precedes noteOn on the member channel");

        expect(mpe.getActiveMpeVoices() == 1,
               "Upper zone: one active MPE voice after the note-on");

        // ---- note-off resets on the member channel ----
        juce::MidiBuffer offBuffer;
        const auto off = noteOff(0);
        mpe.render(config, &off, 1, offBuffer, 64);
        const auto offMsgs = decode(offBuffer);

        const bool hasNoteOff = indexOf(offMsgs, [memberCh] (const Msg& m)
            { return m.status == kNoteOff && m.channel == memberCh; }) >= 0;
        const bool hasPressureZero = indexOf(offMsgs, [memberCh] (const Msg& m)
            { return m.status == kPressure && m.channel == memberCh && m.d1 == 0; }) >= 0;
        const bool hasBendCenter = indexOf(offMsgs, [memberCh] (const Msg& m)
            { return m.status == kBend && m.channel == memberCh && pitchWheelValue(m) == 8192; }) >= 0;

        expect(hasNoteOff, "Upper zone: note-off emits a note-off on the member channel");
        expect(hasPressureZero, "Upper zone: note-off emits channel pressure 0 on the member channel");
        expect(hasBendCenter, "Upper zone: note-off recenters pitch wheel to 8192 on the member channel");
        expect(mpe.getActiveMpeVoices() == 0,
               "Upper zone: voice released after note-off");
    }

    // ---- Test 11: MPE channel-reuse microtonal re-press (ROUND-ROBIN) ----
    //
    // Reproduces the reported "D#2 loses its microtone on re-press" scenario as a
    // RECEIVER-INDEPENDENT check of OUR emission, now under the round-robin
    // allocator. Two notes are pressed in a Lower-zone MPE config:
    //   sourceA: an exact 12-TET frequency  -> bend EXACTLY 8192 (centered root).
    //   sourceB: a +48-cent microtone       -> bend != 8192 (off-center).
    // Then BOTH are released (render #2 - channels freed), then BOTH are pressed
    // again with the same frequencies (render #3 - the re-press the bug report
    // identifies as the trigger).
    //
    // Originally this was a characterization test that documented immediate
    // "lowest-free" reuse: the re-press landed on the SAME channels just freed in
    // render #2. The root cause of the in-the-wild bug was that immediate reuse:
    // an MPE receiver that latches per-note pitch at noteOn could capture a stale
    // center bend on a channel it just freed, so the offset note (sourceB) lost its
    // microtone. The fix is round-robin allocation, so a just-freed channel is the
    // LAST to be reused.
    //
    // Under round-robin, the re-press now goes to a FRESH member channel rather than
    // the just-freed one. This test asserts that NEW behaviour: (a) the re-pressed
    // offset note (sourceB) still emits the CORRECT pre-noteOn pitch wheel
    // (== convertFrequencyToMidiPitch(freqB).pitchBend14Bit) BEFORE its noteOn
    // (byte-correctness, unchanged), AND (b) it does NOT land on the channel it just
    // freed (no immediate reuse). The raw re-press byte sequence is printed for the
    // report.
    {
        const auto config = mpeConfig(2, 16, 3); // Lower zone: master1, members2-16; bend range 48 st
        const int bendRange = MpeMidiOutput::bendRangeFromChoice(3);

        const int kSourceA = 0;
        const int kSourceB = 1;

        // sourceA: A4 = 440 Hz lands EXACTLY on note 69 -> centered bend 8192.
        const double freqA = kA4;
        // sourceB: +48 cents above C5 (note 72). Off a 12-TET note by a microtone
        // that the converter turns into a non-centered 14-bit bend.
        const double c5 = kA4 * std::pow(2.0, 3.0 / 12.0);          // note 72
        const double freqB = c5 * std::pow(2.0, 48.0 / 1200.0);     // +48 cents

        const auto pitchA = convertFrequencyToMidiPitch(freqA, bendRange);
        const auto pitchB = convertFrequencyToMidiPitch(freqB, bendRange);

        // Sanity on the chosen frequencies: A is centered, B is not.
        expect(pitchA.pitchBend14Bit == 8192,
               "char/re-press: sourceA frequency is exact 12-TET (bend == 8192, centered root)");
        expect(pitchB.pitchBend14Bit != 8192,
               "char/re-press: sourceB frequency is a non-center microtone (bend != 8192)");

        MpeMidiOutput mpe;

        // ---- render #1: press both notes ----
        juce::MidiBuffer buf1;
        {
            const MpeMidiOutput::NoteEvent evs[] {
                noteOn(kSourceA, freqA),
                noteOn(kSourceB, freqB),
            };
            mpe.render(config, evs, 2, buf1, 64);
        }
        const auto msgs1 = decode(buf1);

        // Member channels chosen on the first press.
        int chA1 = -1, chB1 = -1;
        for (const auto& m : msgs1)
            if (m.status == kNoteOn && m.d2 > 0)
            {
                if (m.d1 == pitchA.noteNumber && chA1 < 0) chA1 = m.channel;
                else if (m.d1 == pitchB.noteNumber && chB1 < 0) chB1 = m.channel;
            }
        expect(chA1 >= 2 && chB1 >= 2 && chA1 != chB1,
               "char/re-press: first press puts A and B on distinct member channels");

        // ---- render #2: release both notes ----
        juce::MidiBuffer buf2;
        {
            const MpeMidiOutput::NoteEvent evs[] {
                noteOff(kSourceA),
                noteOff(kSourceB),
            };
            mpe.render(config, evs, 2, buf2, 64);
        }
        const auto msgs2 = decode(buf2);

        // Does note-off recenter each channel's bend to 8192? (state reset probe)
        const bool offRecentersA = indexOf(msgs2, [chA1] (const Msg& m)
            { return m.status == kBend && m.channel == chA1 && pitchWheelValue(m) == 8192; }) >= 0;
        const bool offRecentersB = indexOf(msgs2, [chB1] (const Msg& m)
            { return m.status == kBend && m.channel == chB1 && pitchWheelValue(m) == 8192; }) >= 0;
        expect(offRecentersA && offRecentersB,
               "char/re-press: note-off (render #2) recenters BOTH channels' bend to 8192");

        // ---- render #3: RE-PRESS both notes (same frequencies) ----
        juce::MidiBuffer buf3;
        {
            const MpeMidiOutput::NoteEvent evs[] {
                noteOn(kSourceA, freqA),
                noteOn(kSourceB, freqB),
            };
            mpe.render(config, evs, 2, buf3, 64);
        }
        const auto msgs3 = decode(buf3);

        // Channels chosen on the re-press.
        int chA3 = -1, chB3 = -1;
        for (const auto& m : msgs3)
            if (m.status == kNoteOn && m.d2 > 0)
            {
                if (m.d1 == pitchA.noteNumber && chA3 < 0) chA3 = m.channel;
                else if (m.d1 == pitchB.noteNumber && chB3 < 0) chB3 = m.channel;
            }

        // Allocation strategy probe: under round-robin the re-press channel must
        // NOT be the one just freed in render #2 (no immediate reuse).
        const bool aReusedSameChannel = (chA3 == chA1);
        const bool bReusedSameChannel = (chB3 == chB1);

        // Per-channel ordered byte dump for the re-press (the report's centrepiece).
        auto dumpChannel = [&msgs3] (int ch, const char* label)
        {
            std::cout << "  [re-press bytes] " << label << " (channel " << ch << "): ";
            bool any = false;
            for (const auto& m : msgs3)
            {
                if (m.channel != ch) continue;
                any = true;
                std::cout << "{";
                switch (m.status)
                {
                    case kNoteOn:   std::cout << "noteOn n=" << m.d1 << " v=" << m.d2; break;
                    case kNoteOff:  std::cout << "noteOff n=" << m.d1; break;
                    case kCC:       std::cout << "cc" << m.d1 << "=" << m.d2; break;
                    case kPressure: std::cout << "pressure=" << m.d1; break;
                    case kBend:     std::cout << "bend=" << pitchWheelValue(m); break;
                    default:        std::cout << "status=0x" << std::hex << m.status << std::dec; break;
                }
                std::cout << "} ";
            }
            if (! any) std::cout << "(no messages)";
            std::cout << "\n";
        };

        std::cout << "\n  ---- Test 11 report (MPE re-press, round-robin) ----\n";
        std::cout << "  allocation strategy: round-robin free-channel (a just-freed channel is reused LAST);"
                     " oldest-age (LRU) stealing only when the member pool is full\n";
        std::cout << "  sourceA: note=" << pitchA.noteNumber
                  << " expectedBend=" << pitchA.pitchBend14Bit << " (centered)"
                  << " ch(press#1)=" << chA1 << " ch(re-press)=" << chA3
                  << " reusedSameChannel=" << (aReusedSameChannel ? "yes" : "no") << "\n";
        std::cout << "  sourceB: note=" << pitchB.noteNumber
                  << " expectedBend=" << pitchB.pitchBend14Bit << " (NON-center)"
                  << " ch(press#1)=" << chB1 << " ch(re-press)=" << chB3
                  << " reusedSameChannel=" << (bReusedSameChannel ? "yes" : "no") << "\n";
        dumpChannel(chA3, "sourceA");
        dumpChannel(chB3, "sourceB");
        std::cout << "  -------------------------------------------------------\n";

        // ROUND-ROBIN: the re-press must NOT reuse the just-freed channels. This is
        // the fix: a just-freed channel is the LAST to be reused, so the offset note
        // (sourceB) cannot land on a channel a receiver just freed and latch its
        // stale center bend. Both notes advance to fresh member channels.
        expect(chA3 >= 2 && chA3 != chA1,
               "re-press (round-robin): sourceA does NOT reuse its just-freed channel");
        expect(chB3 >= 2 && chB3 != chB1,
               "re-press (round-robin): sourceB does NOT reuse its just-freed channel");
        expect(chA3 != chB3,
               "re-press (round-robin): the two re-pressed notes land on distinct member channels");

        // ---- sourceB (NON-center note) re-press emission, on its member channel ----
        const int bBendIdx3 = indexOf(msgs3, [chB3] (const Msg& m)
            { return m.status == kBend && m.channel == chB3; });
        const int bNoteOnIdx3 = indexOf(msgs3, [chB3, &pitchB] (const Msg& m)
            { return m.status == kNoteOn && m.channel == chB3 && m.d1 == pitchB.noteNumber && m.d2 > 0; });

        expect(bBendIdx3 >= 0 && bNoteOnIdx3 >= 0 && bBendIdx3 < bNoteOnIdx3,
               "re-press: sourceB emits a pitchWheel BEFORE its noteOn on the fresh (round-robin) channel");

        // The bend that precedes the noteOn carries the CORRECT non-center value.
        int bBendValueBeforeNoteOn = -1;
        for (int i = 0; i < (int) msgs3.size() && (bNoteOnIdx3 < 0 || i < bNoteOnIdx3); ++i)
            if (msgs3[(size_t) i].status == kBend && msgs3[(size_t) i].channel == chB3)
                bBendValueBeforeNoteOn = pitchWheelValue(msgs3[(size_t) i]);
        expect(bBendValueBeforeNoteOn == pitchB.pitchBend14Bit,
               "re-press: sourceB's pre-noteOn pitchWheel equals convertFrequencyToMidiPitch(freqB).pitchBend14Bit (CORRECT microtone)");

        // ---- sourceA (centered root) re-press emission, on its member channel ----
        const int aBendIdx3 = indexOf(msgs3, [chA3] (const Msg& m)
            { return m.status == kBend && m.channel == chA3; });
        const int aNoteOnIdx3 = indexOf(msgs3, [chA3, &pitchA] (const Msg& m)
            { return m.status == kNoteOn && m.channel == chA3 && m.d1 == pitchA.noteNumber && m.d2 > 0; });

        expect(aBendIdx3 >= 0 && aNoteOnIdx3 >= 0 && aBendIdx3 < aNoteOnIdx3,
               "re-press: sourceA emits a pitchWheel BEFORE its noteOn on the fresh (round-robin) channel");

        int aBendValueBeforeNoteOn = -1;
        for (int i = 0; i < (int) msgs3.size() && (aNoteOnIdx3 < 0 || i < aNoteOnIdx3); ++i)
            if (msgs3[(size_t) i].status == kBend && msgs3[(size_t) i].channel == chA3)
                aBendValueBeforeNoteOn = pitchWheelValue(msgs3[(size_t) i]);
        expect(aBendValueBeforeNoteOn == 8192 && pitchA.pitchBend14Bit == 8192,
               "re-press: sourceA's pre-noteOn pitchWheel is 8192 (centered root, as expected)");

        // Voice accounting unchanged after the cycle: two voices active again.
        expect(mpe.getActiveMpeVoices() == 2,
               "re-press: two MPE voices active again after the re-press");
    }

    // ---- Test 12: ROUND-ROBIN allocation - just-freed channel is reused LAST ----
    //
    // Locks the round-robin allocator: from a fresh state three distinct sources
    // take consecutive member channels (memberFirst, +1, +2 = ch2,3,4); after ALL
    // three are freed, a NEW note must NOT reuse the just-freed LOWEST channel (ch2)
    // - it advances round-robin to the next channel (ch5) so a just-freed channel is
    // the LAST to be reused. Two more notes keep advancing to distinct channels.
    // Finally, when the member pool is FULL, voice-stealing still steals the OLDEST
    // (unchanged). Together this proves no immediate channel reuse + intact stealing.

    // Reads the member channel currently owned by `sourceId` from the voice-debug
    // snapshots (the same accessor Test 4 uses), or -1 if not active.
    auto channelOfSource = [] (const MpeMidiOutput& mpe, int sourceId)
    {
        for (int i = 0; i < mpe.getVoiceDebugCount(); ++i)
        {
            const auto v = mpe.getVoiceDebugSnapshot(i);
            if (v.active && v.sourceId == sourceId)
                return v.channel;
        }
        return -1;
    };

    {
        MpeMidiOutput mpe;
        const auto config = mpeConfig(2, 16); // Lower zone: members ch2..16

        // ---- allocate three distinct sources from a fresh pool ----
        {
            juce::MidiBuffer buffer;
            const MpeMidiOutput::NoteEvent evs[] {
                noteOn(0, 220.0),
                noteOn(1, 277.18),
                noteOn(2, 329.63),
            };
            mpe.render(config, evs, 3, buffer, 64);
        }

        const int ch0 = channelOfSource(mpe, 0);
        const int ch1 = channelOfSource(mpe, 1);
        const int ch2 = channelOfSource(mpe, 2);

        expect(ch0 == 2 && ch1 == 3 && ch2 == 4,
               "round-robin: three fresh sources take consecutive member channels (ch2, ch3, ch4)");
        expect(ch0 != ch1 && ch0 != ch2 && ch1 != ch2,
               "round-robin: the three fresh channels are distinct");

        // ---- free ALL three ----
        {
            juce::MidiBuffer buffer;
            const MpeMidiOutput::NoteEvent evs[] {
                noteOff(0),
                noteOff(1),
                noteOff(2),
            };
            mpe.render(config, evs, 3, buffer, 64);
        }
        expect(mpe.getActiveMpeVoices() == 0,
               "round-robin: all three voices released before the re-allocation probe");

        // ---- a NEW note must NOT reuse the just-freed LOWEST channel (ch2) ----
        {
            juce::MidiBuffer buffer;
            const auto ev = noteOn(3, 392.0);
            mpe.render(config, &ev, 1, buffer, 64);
        }
        const int ch3 = channelOfSource(mpe, 3);

        expect(ch3 == 5,
               "round-robin: after freeing ch2/3/4, the next note advances to ch5 (not the just-freed ch2)");
        expect(ch3 != 2,
               "round-robin: a just-freed channel (ch2) is NOT immediately reused");

        // ---- two more notes keep advancing to distinct channels ----
        {
            juce::MidiBuffer buffer;
            const MpeMidiOutput::NoteEvent evs[] {
                noteOn(4, 440.0),
                noteOn(5, 523.25),
            };
            mpe.render(config, evs, 2, buffer, 64);
        }
        const int ch4 = channelOfSource(mpe, 4);
        const int ch5 = channelOfSource(mpe, 5);

        expect(ch4 == 6 && ch5 == 7,
               "round-robin: subsequent notes keep advancing (ch6, ch7)");
        expect(ch3 != ch4 && ch3 != ch5 && ch4 != ch5,
               "round-robin: the advancing channels are distinct");
    }

    // ---- voice-stealing is UNCHANGED under round-robin: steal the OLDEST ----
    {
        const int first = 2, last = 4; // only 3 member channels -> pool fills fast
        MpeMidiOutput mpe;
        mpe.setMemberRange(first, last);
        const auto config = mpeConfig(first, last);

        // Fill all three channels (sources 0,1,2).
        {
            juce::MidiBuffer warmup;
            const MpeMidiOutput::NoteEvent evs[] {
                noteOn(0, 220.0),
                noteOn(1, 277.18),
                noteOn(2, 329.63),
            };
            mpe.render(config, evs, 3, warmup, 64);
        }

        const int oldestChannel = channelOfSource(mpe, 0); // source 0 is the oldest
        expect(oldestChannel >= first && oldestChannel <= last,
               "round-robin steal: oldest source (0) holds a valid member channel before the pool is full");

        // A fourth source with the pool full must STEAL the oldest channel (not pick
        // any free one - there are none), exactly as before the round-robin change.
        juce::MidiBuffer buffer;
        const auto ev = noteOn(3, 392.0);
        mpe.render(config, &ev, 1, buffer, 64);
        const auto msgs = decode(buffer);

        const int stolenNoteOffIdx = indexOf(msgs, [oldestChannel] (const Msg& m)
            { return m.status == kNoteOff && m.channel == oldestChannel; });
        const int newNoteOnIdx = indexOf(msgs, [oldestChannel] (const Msg& m)
            { return m.status == kNoteOn && m.channel == oldestChannel && m.d2 > 0; });

        expect(stolenNoteOffIdx >= 0 && newNoteOnIdx >= 0 && stolenNoteOffIdx < newNoteOnIdx,
               "round-robin steal: full pool steals the OLDEST channel (note-off precedes the reusing note-on)");
        expect(channelOfSource(mpe, 0) == -1 && channelOfSource(mpe, 3) == oldestChannel,
               "round-robin steal: oldest source released and its channel reassigned to the new source");
    }

    std::cout << "\nSummary: " << (g_failed == 0 ? "ok" : "failed") << "\n";
    return g_failed == 0 ? 0 : 1;
}
