#include "MpeMidiOutput.h"

#include <cmath>
#include <limits>

namespace
{
    struct RelativePitchBend
    {
        int value = 8192;
        bool reachable = false;
    };

    RelativePitchBend pitchBendFromBaseNote (double targetFrequencyHz,
                                             int baseNote,
                                             int bendRangeSemitones) noexcept
    {
        if (! std::isfinite(targetFrequencyHz) || targetFrequencyHz <= 0.0)
            return {};

        const int safeBase = juce::jlimit(0, 127, baseNote);
        const int safeRange = juce::jmax(1, bendRangeSemitones);
        const double targetMidi = 69.0 + 12.0 * std::log2(targetFrequencyHz / 440.0);
        const double delta = targetMidi - (double) safeBase;
        if (! std::isfinite(delta) || std::abs(delta) > (double) safeRange + 1.0e-9)
            return {};

        const double normalised = juce::jlimit(-1.0, 1.0, delta / (double) safeRange);
        return { juce::jlimit(0, 16383,
                              8192 + (int) std::round(normalised * 8192.0)),
                 true };
    }
}

int MpeMidiOutput::bendRangeFromChoice (int choice) noexcept
{
    static constexpr int ranges[] { 2, 12, 24, 48 };
    return ranges[juce::jlimit(0, 3, choice)];
}

int MpeMidiOutput::velocityFromUnit (float value) noexcept
{
    if (! std::isfinite(value))
        value = 0.0f;
    return juce::jlimit(1, 127, (int) std::round(juce::jlimit(0.0f, 1.0f, value) * 127.0f));
}

int MpeMidiOutput::pressureFromUnit (float value) noexcept
{
    if (! std::isfinite(value))
        value = 0.0f;
    return juce::jlimit(0, 127, (int) std::round(juce::jlimit(0.0f, 1.0f, value) * 127.0f));
}

int MpeMidiOutput::normalChannelForParticipant (int participantId, int sourceIdBase) noexcept
{
    // Do the subtraction in 64 bits so even sentinel/extreme int values cannot
    // overflow before the modulo is normalised into [0, 15].
    const auto offset = (int64_t) participantId - (int64_t) sourceIdBase;
    const auto wrapped = ((offset % 16) + 16) % 16;
    return (int) wrapped + 1;
}

void MpeMidiOutput::setMemberRange (int first, int last) noexcept
{
    // AudienceProcessor::processBlock calls this UNCONDITIONALLY every block, so we
    // must only re-arm the round-robin cursor when the range ACTUALLY changes.
    // Otherwise the per-block same-range calls reset the cursor every block, and
    // because sequential notes arrive in separate blocks every allocation would
    // start its scan from memberLast and always wrap back to memberFirst (ch2) -
    // round-robin would never advance across blocks.
    const bool rangeChanged = (first != memberFirst || last != memberLast);
    memberFirst = first;
    memberLast = last;

    // Re-arm the round-robin cursor to the (new) last member ONLY on an actual
    // zone/range change, so a Lower<->Upper zone switch starts allocation fresh
    // (next pick wraps to memberFirst) and the cursor stays within the active
    // [memberFirst, memberLast] range. An out-of-range cursor left over from a
    // previous range is still safe: allocateMpeChannelForSource normalises any
    // cursor value via the modulo over the current span.
    if (rangeChanged)
    {
        roundRobinCursor = memberLast;
        refreshMpeChannelCounts();
    }
}

void MpeMidiOutput::reset() noexcept
{
    for (auto& state : midiOutVoices)
        state = {};

#if COSMIC_MIDI_DIAGNOSTICS
    for (auto& state : midiVoiceDebug)
    {
        state.active.store(0, std::memory_order_relaxed);
        state.sourceId.store(-1, std::memory_order_relaxed);
        state.channel.store(0, std::memory_order_relaxed);
        state.note.store(-1, std::memory_order_relaxed);
        state.pitchBend.store(8192, std::memory_order_relaxed);
        state.age.store(0, std::memory_order_relaxed);
    }
#endif

    mpeChannelOwner.fill(-1);
    normalNoteRefCounts.fill(0);
    normalChannelTimbre.fill(-1);
    normalChannelExpression.fill(-1);
    // Re-arm the round-robin cursor so a freshly reset pool allocates memberFirst
    // first again (matches the header initialiser and the AllNotesOff/panic path).
    roundRobinCursor = memberLast;
    midiVoiceAgeCounter = 0;
    midiNotesSent.store(0, std::memory_order_relaxed);
    activeMpeVoices.store(0, std::memory_order_relaxed);
    availableMpeChannels.store(juce::jmax(0, memberLast - memberFirst + 1),
                               std::memory_order_relaxed);
}

void MpeMidiOutput::sendPitchBendRangeRpn (juce::MidiBuffer& midiMessages, int sampleOffset,
                                           int channel, int semitones)
{
    midiMessages.addEvent(juce::MidiMessage::controllerEvent(channel, 101, 0), sampleOffset);
    midiMessages.addEvent(juce::MidiMessage::controllerEvent(channel, 100, 0), sampleOffset);
    midiMessages.addEvent(juce::MidiMessage::controllerEvent(channel, 6, juce::jlimit(0, 127, semitones)), sampleOffset);
    midiMessages.addEvent(juce::MidiMessage::controllerEvent(channel, 38, 0), sampleOffset);
    midiMessages.addEvent(juce::MidiMessage::controllerEvent(channel, 101, 127), sampleOffset);
    midiMessages.addEvent(juce::MidiMessage::controllerEvent(channel, 100, 127), sampleOffset);
}

void MpeMidiOutput::sendMpeSetupIfNeeded (const MpeConfig& config, juce::MidiBuffer& midiMessages, int sampleOffset)
{
    // B24: atomic read+clear. Relaxed is fine - the flag only gates re-emission of
    // the idempotent MPE setup; render() reads+clears on the audio thread while
    // markSetupDirty() sets it on the message thread.
    if (! mpeSetupDirty.load(std::memory_order_relaxed))
        return;

    if (config.outputType != 2
        || ! config.sendSetupMessages)
    {
        mpeSetupDirty.store(false, std::memory_order_relaxed);
        return;
    }

    const int master = juce::jlimit(1, 16, config.masterChannel);
    // B8: member channels are now zone-derived; the Upper zone uses ch1 as a
    // member, so the lower clamp is relaxed from 2 to 1 (was jlimit(2,16,...)).
    const int first = juce::jlimit(1, 16, config.memberFirst);
    const int last = juce::jlimit(first, 16, config.memberLast);
    const int bendRange = bendRangeFromChoice(config.pitchBendRangeChoice);

    // The zone guarantees the member range never includes the master channel
    // (Lower: master1 / members2-16; Upper: master16 / members1-15).
    jassert (master < first || master > last);

    const int memberCount = juce::jlimit(0, 15, last - first + 1);
    midiMessages.addEvent(juce::MidiMessage::controllerEvent(master, 101, 0), sampleOffset);
    midiMessages.addEvent(juce::MidiMessage::controllerEvent(master, 100, 6), sampleOffset);
    midiMessages.addEvent(juce::MidiMessage::controllerEvent(master, 6, memberCount), sampleOffset);
    midiMessages.addEvent(juce::MidiMessage::controllerEvent(master, 38, 0), sampleOffset);
    midiMessages.addEvent(juce::MidiMessage::controllerEvent(master, 101, 127), sampleOffset);
    midiMessages.addEvent(juce::MidiMessage::controllerEvent(master, 100, 127), sampleOffset);

    for (int ch = first; ch <= last; ++ch)
        sendPitchBendRangeRpn(midiMessages, sampleOffset, ch, bendRange);

    mpeSetupDirty.store(false, std::memory_order_relaxed);
}

void MpeMidiOutput::sendAllMidiNotesOff (juce::MidiBuffer& midiMessages, int sampleOffset)
{
    for (int ch = 1; ch <= 16; ++ch)
    {
        midiMessages.addEvent(juce::MidiMessage::controllerEvent(ch, 123, 0), sampleOffset);
        midiMessages.addEvent(juce::MidiMessage::controllerEvent(ch, 120, 0), sampleOffset);
        midiMessages.addEvent(juce::MidiMessage::channelPressureChange(ch, 0), sampleOffset);
        midiMessages.addEvent(juce::MidiMessage::pitchWheel(ch, 8192), sampleOffset);
    }
}

void MpeMidiOutput::sendMidiResetMessages (juce::MidiBuffer& midiMessages, int sampleOffset)
{
    // A fixed 16-channel sweep is both semantically stronger and realtime-safe.
    // Enumerating all 2,560 semantic voices produced thousands of duplicate
    // NoteOffs in shared-channel Normal mode and could exceed an audio deadline.
    // CC123 + CC120 release every physical note regardless of ledger density.
    sendAllMidiNotesOff(midiMessages, sampleOffset);
}

void MpeMidiOutput::releaseMpeChannelForSource (int sourceId) noexcept
{
    for (int ch = 1; ch <= 16; ++ch)
        if (mpeChannelOwner[(size_t) ch] == sourceId)
            mpeChannelOwner[(size_t) ch] = -1;

    refreshMpeChannelCounts();
}

void MpeMidiOutput::refreshMpeChannelCounts() noexcept
{
    int active = 0;
    for (int ch = memberFirst; ch <= memberLast; ++ch)
        if (mpeChannelOwner[(size_t) ch] >= 0)
            ++active;

    activeMpeVoices.store(active, std::memory_order_relaxed);
    availableMpeChannels.store(juce::jmax(0, memberLast - memberFirst + 1 - active),
                               std::memory_order_relaxed);
}

void MpeMidiOutput::sendNoteOffForSource (const MpeConfig& config, int sourceId,
                                          juce::MidiBuffer& midiMessages, int sampleOffset)
{
    if (sourceId < 0 || sourceId >= (int) midiOutVoices.size())
        return;

    auto& state = midiOutVoices[(size_t) sourceId];
    if (! state.active || state.note < 0)
        return;

    const int ch = juce::jlimit(1, 16, state.channel);
    if (config.outputType == 2)
    {
        // MPE member channels are per-note, so their note-off also resets the
        // per-channel pressure and bend exactly as before.
        midiMessages.addEvent(juce::MidiMessage::noteOff(ch, state.note), sampleOffset);
        midiMessages.addEvent(juce::MidiMessage::channelPressureChange(ch, 0), sampleOffset);
        midiMessages.addEvent(juce::MidiMessage::pitchWheel(ch, 8192), sampleOffset);
    }
    else
    {
        // Multiple finger voices can intentionally share the same normal MIDI
        // channel and note. Keep the physical note held until the last semantic
        // owner releases it; channel pressure must not be zeroed because that
        // would affect every other voice sharing the channel.
        auto& refs = normalNoteRefCounts[(size_t) ((ch - 1) * 128 + state.note)];
        if (refs > 0)
        {
            --refs;
            if (refs == 0)
                midiMessages.addEvent(juce::MidiMessage::noteOff(ch, state.note), sampleOffset);
        }
        else
        {
            // Defensive recovery for state created before a routing-mode/config
            // transition: releasing the known active voice is safer than leaving
            // a stuck note when the accounting table has no matching owner.
            midiMessages.addEvent(juce::MidiMessage::noteOff(ch, state.note), sampleOffset);
        }
    }

    // Release the MPE member channel unconditionally. If the output type was
    // switched away from MPE while this note was held, gating the release on
    // type==2 leaked the channel, so the member pool shrank over time.
    state.active = false;
    releaseMpeChannelForSource(sourceId);

    state = {};
#if COSMIC_MIDI_DIAGNOSTICS
    auto& debug = midiVoiceDebug[(size_t) sourceId];
    debug.active.store(0, std::memory_order_relaxed);
    debug.sourceId.store(sourceId, std::memory_order_relaxed);
    debug.channel.store(0, std::memory_order_relaxed);
    debug.note.store(-1, std::memory_order_relaxed);
    debug.pitchBend.store(8192, std::memory_order_relaxed);
    debug.age.store(0, std::memory_order_relaxed);
#endif
}

int MpeMidiOutput::allocateMpeChannelForSource (const MpeConfig& config, int sourceId,
                                                juce::MidiBuffer& midiMessages, int sampleOffset)
{
    if (sourceId >= 0 && sourceId < (int) midiOutVoices.size())
    {
        const auto& state = midiOutVoices[(size_t) sourceId];
        if (state.active
            && state.channel >= memberFirst && state.channel <= memberLast
            && mpeChannelOwner[(size_t) state.channel] == sourceId)
            return state.channel;
    }

    // Round-robin free-channel scan: start AFTER the cursor and wrap within
    // [memberFirst, memberLast]. This still finds ANY free channel, but visits a
    // just-freed channel LAST, so an immediately following note is not assigned the
    // channel that was just released. Mitigates a receiver-side per-note
    // pitch-capture race on immediate channel reuse. The cursor advances to the
    // channel we hand out.
    const int span = memberLast - memberFirst + 1;
    if (span >= 1)
    {
        // Normalise the cursor offset into [0, span) BEFORE scanning. C++ `%` keeps
        // the sign of the dividend, so if roundRobinCursor is ever left out of range
        // (< memberFirst), the raw (cursor - memberFirst + i) % span could be
        // negative and place ch below memberFirst (even on the master channel). For
        // an in-range cursor off == (cursor - memberFirst), so the computed channels
        // are byte-identical to before; for any out-of-range cursor off is clamped
        // into [0, span) so ch always stays within [memberFirst, memberLast].
        const int off = (((roundRobinCursor - memberFirst) % span) + span) % span;
        for (int i = 1; i <= span; ++i)
        {
            const int ch = memberFirst + ((off + i) % span);
            if (mpeChannelOwner[(size_t) ch] < 0)
            {
                mpeChannelOwner[(size_t) ch] = sourceId;
                roundRobinCursor = ch;
                return ch;
            }
        }
    }

    int oldestSource = -1;
    uint64_t oldestAge = std::numeric_limits<uint64_t>::max();
    // A full MPE zone owns at most 15 channels. Scan those owners directly
    // instead of all 2,560 source/finger slots on every stolen note.
    for (int ch = memberFirst; ch <= memberLast; ++ch)
    {
        const int owner = mpeChannelOwner[(size_t) ch];
        if (owner < 0 || owner >= (int) midiOutVoices.size())
            continue;

        const auto& state = midiOutVoices[(size_t) owner];
        if (! state.active || state.channel != ch)
            continue;
        if (state.age < oldestAge)
        {
            oldestAge = state.age;
            oldestSource = owner;
        }
    }

    if (oldestSource >= 0)
    {
        const int stolenChannel = midiOutVoices[(size_t) oldestSource].channel;
        sendNoteOffForSource(config, oldestSource, midiMessages, sampleOffset);
        mpeChannelOwner[(size_t) stolenChannel] = sourceId;
        return stolenChannel;
    }

    return memberFirst;
}

void MpeMidiOutput::sendExpressionForSource (const MpeConfig& config, int sourceId, const NoteEvent& event,
                                             juce::MidiBuffer& midiMessages, int sampleOffset, bool force)
{
    if (sourceId < 0 || sourceId >= (int) midiOutVoices.size())
        return;

    auto& state = midiOutVoices[(size_t) sourceId];
    if (! state.active)
        return;

    const int type = config.outputType;
    const int ch = juce::jlimit(1, 16, state.channel);
    const int pressure = pressureFromUnit(event.y);
    // Cosmic Microwave is a direct OSC -> MIDI router. There is no longer an
    // audio-engine macro layer biasing expression: U maps to CC74 and V maps
    // to CC11 exactly.
    const int timbre = pressureFromUnit(event.x);
    const int expression = pressureFromUnit(event.y);

    if (type == 2)
    {
        const int bendRange = bendRangeFromChoice(config.pitchBendRangeChoice);
        const auto nearestPitch = convertFrequencyToMidiPitch(event.frequencyHz, bendRange);
        const auto glidePitch = pitchBendFromBaseNote(event.frequencyHz, state.note, bendRange);
        const int targetBend = config.pitchMode == 1 && glidePitch.reachable
                             ? glidePitch.value : nearestPitch.pitchBend14Bit;
        if (force || std::abs(targetBend - state.pitchBend) > 1)
        {
            midiMessages.addEvent(juce::MidiMessage::pitchWheel(ch, targetBend), sampleOffset);
            state.pitchBend = targetBend;
            state.frequencyHz = event.frequencyHz;
#if COSMIC_MIDI_DIAGNOSTICS
            midiVoiceDebug[(size_t) sourceId].pitchBend.store(state.pitchBend, std::memory_order_relaxed);
#endif
        }

        if (force || std::abs(pressure - state.pressure) > 1)
        {
            midiMessages.addEvent(juce::MidiMessage::channelPressureChange(ch, pressure), sampleOffset);
            state.pressure = pressure;
        }
    }

    const auto shouldSendTimbre = [&]
    {
        if (type == 2)
            return force || std::abs(timbre - state.timbre) > 1;

        // Normal participant routing lets several source voices share a channel.
        // A per-source cache can therefore be stale relative to the last value
        // actually sent on that channel, so normal mode uses channel-wide caches.
        return force || std::abs(timbre - normalChannelTimbre[(size_t) (ch - 1)]) > 1;
    };

    if (shouldSendTimbre())
    {
        midiMessages.addEvent(juce::MidiMessage::controllerEvent(ch, 74, timbre), sampleOffset);
        state.timbre = timbre;
        if (type != 2)
            normalChannelTimbre[(size_t) (ch - 1)] = timbre;
    }

    const auto shouldSendExpression = [&]
    {
        if (type == 2)
            return force || std::abs(expression - state.expression) > 1;

        return force || std::abs(expression - normalChannelExpression[(size_t) (ch - 1)]) > 1;
    };

    if (shouldSendExpression())
    {
        midiMessages.addEvent(juce::MidiMessage::controllerEvent(ch, 11, expression), sampleOffset);
        state.expression = expression;
        if (type != 2)
            normalChannelExpression[(size_t) (ch - 1)] = expression;
    }
}

void MpeMidiOutput::handleMidiSourceEvent (const MpeConfig& config, const NoteEvent& event,
                                           juce::MidiBuffer& midiMessages, int sampleOffset)
{
    if (event.type == NoteEvent::AllNotesOff)
    {
        sendMidiResetMessages(midiMessages, sampleOffset);
        reset();
        return;
    }

    const int sourceId = event.sourceId;
    if (sourceId < 0 || sourceId >= (int) midiOutVoices.size())
        return;

    if (event.type == NoteEvent::NoteOff)
    {
        sendNoteOffForSource(config, sourceId, midiMessages, sampleOffset);
        return;
    }

    if (event.type == NoteEvent::Expression)
    {
        sendExpressionForSource(config, sourceId, event, midiMessages, sampleOffset, false);
        return;
    }

    const int outputType = config.outputType;
    const int bendRange = bendRangeFromChoice(config.pitchBendRangeChoice);
    const auto pitch = convertFrequencyToMidiPitch(event.frequencyHz, bendRange);
    const int velocity = velocityFromUnit(event.velocity);

    auto& state = midiOutVoices[(size_t) sourceId];

    if (outputType == 2
        && state.active
        && state.note == pitch.noteNumber
        && std::abs(pitch.pitchBend14Bit - state.pitchBend) <= 1)
    {
        sendExpressionForSource(config, sourceId, event, midiMessages, sampleOffset, true);
        return;
    }

    if (outputType == 2 && config.pitchMode == 1 && state.active
        && pitchBendFromBaseNote(event.frequencyHz, state.note, bendRange).reachable)
    {
        sendExpressionForSource(config, sourceId, event, midiMessages, sampleOffset, true);
        return;
    }

    sendNoteOffForSource(config, sourceId, midiMessages, sampleOffset);

    state.active = true;
    state.sourceId = sourceId;
    state.note = pitch.noteNumber;
    state.frequencyHz = pitch.targetFrequencyHz;
    state.age = ++midiVoiceAgeCounter;

    if (outputType == 2)
    {
        sendMpeSetupIfNeeded(config, midiMessages, sampleOffset);
        const int ch = allocateMpeChannelForSource(config, sourceId, midiMessages, sampleOffset);
        state.channel = ch;
        state.pitchBend = pitch.pitchBend14Bit;
        mpeChannelOwner[(size_t) ch] = sourceId;

        midiMessages.addEvent(juce::MidiMessage::pitchWheel(ch, pitch.pitchBend14Bit), sampleOffset);
        const int timbre = pressureFromUnit(event.x);
        const int expression = pressureFromUnit(event.y);
        const int pressure = pressureFromUnit(event.y);
        midiMessages.addEvent(juce::MidiMessage::controllerEvent(ch, 74, timbre), sampleOffset);
        midiMessages.addEvent(juce::MidiMessage::controllerEvent(ch, 11, expression), sampleOffset);
        midiMessages.addEvent(juce::MidiMessage::noteOn(ch, pitch.noteNumber, (juce::uint8) velocity), sampleOffset);
        midiMessages.addEvent(juce::MidiMessage::channelPressureChange(ch, pressure), sampleOffset);
        state.pressure = pressure;
        state.timbre = timbre;
        state.expression = expression;
#if COSMIC_MIDI_DIAGNOSTICS
        auto& debug = midiVoiceDebug[(size_t) sourceId];
        debug.sourceId.store(sourceId, std::memory_order_relaxed);
        debug.channel.store(ch, std::memory_order_relaxed);
        debug.note.store(state.note, std::memory_order_relaxed);
        debug.pitchBend.store(state.pitchBend, std::memory_order_relaxed);
        debug.age.store(state.age, std::memory_order_relaxed);
        debug.active.store(1, std::memory_order_release);
#endif
        refreshMpeChannelCounts();
    }
    else
    {
        const bool routeByParticipant = config.normalRoutingMode == 1
                                     && event.participantId >= 0;
        const int ch = routeByParticipant
                     ? normalChannelForParticipant(event.participantId)
                     : juce::jlimit(1, 16, config.normalMidiChannel + 1);
        state.channel = ch;
        state.pitchBend = 8192;
        auto& refs = normalNoteRefCounts[(size_t) ((ch - 1) * 128 + state.note)];
        if (refs == 0)
            midiMessages.addEvent(juce::MidiMessage::noteOn(ch, state.note, (juce::uint8) velocity), sampleOffset);

        // The fixed source capacity bounds simultaneous semantic owners well
        // below uint16_t's maximum, but retain a release-safe saturation guard.
        if (refs < std::numeric_limits<uint16_t>::max())
            ++refs;
        sendExpressionForSource(config, sourceId, event, midiMessages, sampleOffset, true);
#if COSMIC_MIDI_DIAGNOSTICS
        auto& debug = midiVoiceDebug[(size_t) sourceId];
        debug.sourceId.store(sourceId, std::memory_order_relaxed);
        debug.channel.store(ch, std::memory_order_relaxed);
        debug.note.store(state.note, std::memory_order_relaxed);
        debug.pitchBend.store(state.pitchBend, std::memory_order_relaxed);
        debug.age.store(state.age, std::memory_order_relaxed);
        debug.active.store(1, std::memory_order_release);
#endif
    }

    midiNotesSent.fetch_add(1, std::memory_order_relaxed);
}

void MpeMidiOutput::render (const MpeConfig& config,
                            const NoteEvent* events, int count,
                            juce::MidiBuffer& midiMessages, int numSamples)
{
    // Clamp the member range exactly as AudienceProcessor::processBlock did when
    // it derived mpeFirst/mpeLast (memberLast is clamped to be >= memberFirst), so
    // the channel-allocation range matches the processor byte-for-byte. B8: the
    // lower clamp is relaxed from 2 to 1 so the zone-derived Upper range (members
    // 1..15) is honoured; Lower (2..16) is unaffected.
    const int configuredFirst = juce::jlimit(1, 16, config.memberFirst);
    const int configuredLast = juce::jlimit(configuredFirst, 16, config.memberLast);
    setMemberRange(configuredFirst, configuredLast);

    const int outputType = config.outputType;
    if (outputType == 2)
        sendMpeSetupIfNeeded(config, midiMessages, 0);

    for (int i = 0; i < count; ++i)
    {
        const auto& event = events[(size_t) i];
        const int sampleOffset = juce::jlimit(0, juce::jmax(0, numSamples - 1),
                                               event.sampleOffset);
        handleMidiSourceEvent(config, event, midiMessages, sampleOffset);
    }
}

void MpeMidiOutput::recordOutgoingMidiDebugEvents (const juce::MidiBuffer& midiMessages) noexcept
{
#if COSMIC_MIDI_DIAGNOSTICS
    if (midiMessages.isEmpty())
        return;

    for (const auto metadata : midiMessages)
    {
        const int rawSize = metadata.numBytes;
        if (rawSize <= 0 || rawSize > 3)
            continue;

        const auto seq = midiDebugWriteCounter.fetch_add(1, std::memory_order_relaxed) + 1;
        auto& slot = midiDebugEvents[(size_t) ((seq - 1) % kDebugEventQueueSize)];
        slot.sequence.store(0, std::memory_order_release);
        slot.sampleOffset.store(metadata.samplePosition, std::memory_order_relaxed);
        slot.size.store(rawSize, std::memory_order_relaxed);

        const auto* raw = metadata.data;
        slot.byte0.store(rawSize > 0 ? raw[0] : 0, std::memory_order_relaxed);
        slot.byte1.store(rawSize > 1 ? raw[1] : 0, std::memory_order_relaxed);
        slot.byte2.store(rawSize > 2 ? raw[2] : 0, std::memory_order_relaxed);
        slot.sequence.store(seq, std::memory_order_release);
    }
#else
    juce::ignoreUnused(midiMessages);
#endif
}

bool MpeMidiOutput::readOutgoingDebugSlot (uint32_t seq, OutgoingDebugEvent& out) const noexcept
{
#if COSMIC_MIDI_DIAGNOSTICS
    const auto& slot = midiDebugEvents[(size_t) ((seq - 1) % kDebugEventQueueSize)];
    const auto storedSeq = slot.sequence.load(std::memory_order_acquire);
    if (storedSeq != seq)
        return false;

    out.sampleOffset = slot.sampleOffset.load(std::memory_order_relaxed);
    out.size = slot.size.load(std::memory_order_relaxed);
    out.b0 = slot.byte0.load(std::memory_order_relaxed);
    out.b1 = slot.byte1.load(std::memory_order_relaxed);
    out.b2 = slot.byte2.load(std::memory_order_relaxed);
    return true;
#else
    juce::ignoreUnused(seq, out);
    return false;
#endif
}

MpeMidiOutput::VoiceDebugSnapshot MpeMidiOutput::getVoiceDebugSnapshot (int index) const noexcept
{
    VoiceDebugSnapshot out;
#if COSMIC_MIDI_DIAGNOSTICS
    if (index < 0 || index >= (int) midiVoiceDebug.size())
        return out;

    const auto& voice = midiVoiceDebug[(size_t) index];
    out.active = voice.active.load(std::memory_order_acquire);
    out.sourceId = voice.sourceId.load(std::memory_order_relaxed);
    out.channel = voice.channel.load(std::memory_order_relaxed);
    out.note = voice.note.load(std::memory_order_relaxed);
    out.pitchBend = voice.pitchBend.load(std::memory_order_relaxed);
    out.age = voice.age.load(std::memory_order_relaxed);
#else
    juce::ignoreUnused(index);
#endif
    return out;
}
