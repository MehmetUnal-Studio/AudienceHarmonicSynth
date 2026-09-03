#include "MpeMidiOutput.h"

#include <cmath>
#include <limits>

int MpeMidiOutput::bendRangeFromChoice (int choice) noexcept
{
    // Retained for compatibility with processor-side legacy state comparison.
    // Notes Only never emits or consumes a pitch-bend message.
    static constexpr int ranges[] { 2, 12, 24, 48 };
    return ranges[juce::jlimit(0, 3, choice)];
}

int MpeMidiOutput::velocityFromUnit (float value) noexcept
{
    if (! std::isfinite(value))
        value = 0.0f;

    return juce::jlimit(1, 127,
                        (int) std::round(juce::jlimit(0.0f, 1.0f, value)
                                         * 127.0f));
}

uint64_t MpeMidiOutput::durationSamplesFor (const TimingConfig& timing) noexcept
{
    constexpr double fallbackSampleRate = 48000.0;
    constexpr double fallbackBpm = 120.0;

    const double finiteRate = std::isfinite(timing.sampleRate)
                            && timing.sampleRate > 0.0
                            ? timing.sampleRate : fallbackSampleRate;
    const double finiteBpm = std::isfinite(timing.bpm) && timing.bpm > 0.0
                           ? timing.bpm : fallbackBpm;
    const double sampleRate = juce::jlimit(1.0, 1536000.0, finiteRate);
    const double bpm = juce::jlimit(1.0, 1000.0, finiteBpm);

    double quarterNotes = 0.125;
    switch (timing.noteDuration)
    {
        case NoteDuration::Half:         quarterNotes = 2.0;   break;
        case NoteDuration::Quarter:      quarterNotes = 1.0;   break;
        case NoteDuration::Eighth:       quarterNotes = 0.5;   break;
        case NoteDuration::Sixteenth:    quarterNotes = 0.25;  break;
        case NoteDuration::ThirtySecond: quarterNotes = 0.125; break;
        default:                         quarterNotes = 0.125; break;
    }

    const double samples = sampleRate * 60.0 * quarterNotes / bpm;
    if (! std::isfinite(samples) || samples <= 1.0)
        return 1;

    return (uint64_t) std::floor(samples + 0.5);
}

int MpeMidiOutput::normalChannelForParticipant (int participantId,
                                                 int sourceIdBase) noexcept
{
    // Do the subtraction in 64 bits so even sentinel/extreme int values cannot
    // overflow before the modulo is normalised into [0, 15].
    const auto offset = (int64_t) participantId - (int64_t) sourceIdBase;
    const auto wrapped = ((offset % 16) + 16) % 16;
    return (int) wrapped + 1;
}

void MpeMidiOutput::setMemberRange (int first, int last) noexcept
{
    // Legacy source compatibility only. Notes Only owns no member channels.
    juce::ignoreUnused(first, last);
}

void MpeMidiOutput::sendAllMidiNotesOff (juce::MidiBuffer& midiMessages,
                                          int sampleOffset)
{
    for (int channel = 1; channel <= 16; ++channel)
    {
        midiMessages.addEvent(
            juce::MidiMessage::controllerEvent(channel, 123, 0), sampleOffset);
        midiMessages.addEvent(
            juce::MidiMessage::controllerEvent(channel, 120, 0), sampleOffset);
    }
}

void MpeMidiOutput::sendMidiResetMessages (juce::MidiBuffer& midiMessages,
                                            int sampleOffset)
{
    // A fixed 16-channel sweep is both semantically stronger and realtime-safe.
    // CC123 + CC120 release every physical note regardless of ledger density.
    sendAllMidiNotesOff(midiMessages, sampleOffset);
}

void MpeMidiOutput::clearScheduledState() noexcept
{
    for (int slot = 0; slot < kMaxScheduledNotes; ++slot)
    {
        scheduledNotes[(size_t) slot] = {};
        scheduledNotes[(size_t) slot].nextFree =
            slot + 1 < kMaxScheduledNotes ? slot + 1 : -1;
    }

    deadlineHeap.fill(-1);
    for (auto& sourceSlots : sourceTokenSlots)
        sourceSlots.fill(-1);
    sourceScheduledCounts.fill(0);
    channelScheduledCounts.fill(0);
    normalNoteRefCounts.fill(0);

    deadlineHeapSize = 0;
    freeTokenHead = 0;
    activeScheduledNotes = 0;
    activePhysicalNotes = 0;
    endedVoiceRecorded.fill(0);
    endedVoiceCount = 0;
    midiVoiceAgeCounter = 0;
    scheduledNoteCount.store(0, std::memory_order_relaxed);
    physicalNoteCount.store(0, std::memory_order_relaxed);

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
}

void MpeMidiOutput::recordEndedVoice (int sourceId) noexcept
{
    if (sourceId < 0 || sourceId >= kMaxMidiSources
        || endedVoiceCount >= (int) endedVoiceIds.size())
        return;

    // A source can become idle, retrigger, and become idle again in one large
    // offline block. Keep the list unique in O(1); the processor confirms the
    // final token count after render() completes.
    if (endedVoiceRecorded[(size_t) sourceId] != 0)
        return;

    endedVoiceRecorded[(size_t) sourceId] = 1;
    endedVoiceIds[(size_t) endedVoiceCount++] = sourceId;
}

void MpeMidiOutput::reset() noexcept
{
    clearScheduledState();
    renderSampleCursor = 0;
    midiNotesSent.store(0, std::memory_order_relaxed);
    deadlineReleaseCount.store(0, std::memory_order_relaxed);
    coalescedRetriggerCount.store(0, std::memory_order_relaxed);
    hardRetriggerCount.store(0, std::memory_order_relaxed);
    capacityStealCount.store(0, std::memory_order_relaxed);
    globalLimitStealCount.store(0, std::memory_order_relaxed);
    channelLimitStealCount.store(0, std::memory_order_relaxed);
    sourceLimitStealCount.store(0, std::memory_order_relaxed);
    safetyResetCount.store(0, std::memory_order_relaxed);
}

void MpeMidiOutput::emitSafetyReset (juce::MidiBuffer& midiMessages,
                                     int sampleOffset)
{
    sendMidiResetMessages(midiMessages, sampleOffset);
    clearScheduledState();
    renderSampleCursor = 0;
    midiNotesSent.store(0, std::memory_order_relaxed);
    safetyResetCount.fetch_add(1, std::memory_order_relaxed);
}

void MpeMidiOutput::failClosed (juce::MidiBuffer& midiMessages,
                                int sampleOffset, bool resetClock) noexcept
{
    sendMidiResetMessages(midiMessages, sampleOffset);
    clearScheduledState();
    if (resetClock)
        renderSampleCursor = 0;
    midiNotesSent.store(0, std::memory_order_relaxed);
    safetyResetCount.fetch_add(1, std::memory_order_relaxed);
}

bool MpeMidiOutput::heapLess (int lhsSlot, int rhsSlot) const noexcept
{
    const auto& lhs = scheduledNotes[(size_t) lhsSlot];
    const auto& rhs = scheduledNotes[(size_t) rhsSlot];
    if (lhs.deadlineSample != rhs.deadlineSample)
        return lhs.deadlineSample < rhs.deadlineSample;
    if (lhs.age != rhs.age)
        return lhs.age < rhs.age;
    return lhsSlot < rhsSlot;
}

void MpeMidiOutput::heapSwap (int lhsIndex, int rhsIndex) noexcept
{
    const int lhsSlot = deadlineHeap[(size_t) lhsIndex];
    const int rhsSlot = deadlineHeap[(size_t) rhsIndex];
    deadlineHeap[(size_t) lhsIndex] = rhsSlot;
    deadlineHeap[(size_t) rhsIndex] = lhsSlot;
    scheduledNotes[(size_t) lhsSlot].heapIndex = rhsIndex;
    scheduledNotes[(size_t) rhsSlot].heapIndex = lhsIndex;
}

void MpeMidiOutput::heapSiftUp (int index) noexcept
{
    while (index > 0)
    {
        const int parent = (index - 1) / 2;
        if (! heapLess(deadlineHeap[(size_t) index],
                       deadlineHeap[(size_t) parent]))
            break;
        heapSwap(index, parent);
        index = parent;
    }
}

void MpeMidiOutput::heapSiftDown (int index) noexcept
{
    for (;;)
    {
        const int left = index * 2 + 1;
        if (left >= deadlineHeapSize)
            break;
        const int right = left + 1;
        int child = left;
        if (right < deadlineHeapSize
            && heapLess(deadlineHeap[(size_t) right],
                        deadlineHeap[(size_t) left]))
            child = right;
        if (! heapLess(deadlineHeap[(size_t) child],
                       deadlineHeap[(size_t) index]))
            break;
        heapSwap(index, child);
        index = child;
    }
}

void MpeMidiOutput::heapInsert (int slot) noexcept
{
    const int index = deadlineHeapSize++;
    deadlineHeap[(size_t) index] = slot;
    scheduledNotes[(size_t) slot].heapIndex = index;
    heapSiftUp(index);
}

void MpeMidiOutput::heapRemove (int slot) noexcept
{
    auto& token = scheduledNotes[(size_t) slot];
    const int index = token.heapIndex;
    if (index < 0 || index >= deadlineHeapSize)
        return;

    const int lastIndex = --deadlineHeapSize;
    token.heapIndex = -1;
    if (index == lastIndex)
    {
        deadlineHeap[(size_t) lastIndex] = -1;
        return;
    }

    const int movedSlot = deadlineHeap[(size_t) lastIndex];
    deadlineHeap[(size_t) lastIndex] = -1;
    deadlineHeap[(size_t) index] = movedSlot;
    scheduledNotes[(size_t) movedSlot].heapIndex = index;

    if (index > 0
        && heapLess(movedSlot, deadlineHeap[(size_t) ((index - 1) / 2)]))
        heapSiftUp(index);
    else
        heapSiftDown(index);
}

bool MpeMidiOutput::registerSourceToken (int sourceId, int slot) noexcept
{
    auto& slots = sourceTokenSlots[(size_t) sourceId];
    for (auto& sourceSlot : slots)
    {
        if (sourceSlot < 0)
        {
            sourceSlot = slot;
            return true;
        }
    }
    return false;
}

void MpeMidiOutput::unregisterSourceToken (int sourceId, int slot) noexcept
{
    auto& slots = sourceTokenSlots[(size_t) sourceId];
    for (auto& sourceSlot : slots)
    {
        if (sourceSlot == slot)
        {
            sourceSlot = -1;
            return;
        }
    }
}

int MpeMidiOutput::findMatchingToken (int sourceId, int channel,
                                      int note) const noexcept
{
    for (const int slot : sourceTokenSlots[(size_t) sourceId])
    {
        if (slot < 0)
            continue;
        const auto& token = scheduledNotes[(size_t) slot];
        if (token.active && token.channel == channel && token.note == note)
            return slot;
    }
    return -1;
}

int MpeMidiOutput::findOldestForSource (int sourceId) const noexcept
{
    int oldest = -1;
    for (const int slot : sourceTokenSlots[(size_t) sourceId])
    {
        if (slot < 0 || ! scheduledNotes[(size_t) slot].active)
            continue;
        if (oldest < 0
            || scheduledNotes[(size_t) slot].age
                < scheduledNotes[(size_t) oldest].age
            || (scheduledNotes[(size_t) slot].age
                    == scheduledNotes[(size_t) oldest].age
                && slot < oldest))
            oldest = slot;
    }
    return oldest;
}

int MpeMidiOutput::findOldestForChannel (int channel) const noexcept
{
    int oldest = -1;
    for (int slot = 0; slot < kMaxScheduledNotes; ++slot)
    {
        const auto& token = scheduledNotes[(size_t) slot];
        if (! token.active || token.channel != channel)
            continue;
        if (oldest < 0 || token.age < scheduledNotes[(size_t) oldest].age
            || (token.age == scheduledNotes[(size_t) oldest].age
                && slot < oldest))
            oldest = slot;
    }
    return oldest;
}

int MpeMidiOutput::findOldestGlobal() const noexcept
{
    int oldest = -1;
    for (int slot = 0; slot < kMaxScheduledNotes; ++slot)
    {
        const auto& token = scheduledNotes[(size_t) slot];
        if (! token.active)
            continue;
        if (oldest < 0 || token.age < scheduledNotes[(size_t) oldest].age
            || (token.age == scheduledNotes[(size_t) oldest].age
                && slot < oldest))
            oldest = slot;
    }
    return oldest;
}

void MpeMidiOutput::refreshVoiceDebug (int sourceId) noexcept
{
#if COSMIC_MIDI_DIAGNOSTICS
    if (sourceId < 0 || sourceId >= kMaxMidiSources)
        return;

    int newest = -1;
    for (const int slot : sourceTokenSlots[(size_t) sourceId])
    {
        if (slot < 0 || ! scheduledNotes[(size_t) slot].active)
            continue;
        if (newest < 0
            || scheduledNotes[(size_t) slot].age
                > scheduledNotes[(size_t) newest].age)
            newest = slot;
    }

    auto& debug = midiVoiceDebug[(size_t) sourceId];
    if (newest < 0)
    {
        debug.active.store(0, std::memory_order_relaxed);
        debug.sourceId.store(sourceId, std::memory_order_relaxed);
        debug.channel.store(0, std::memory_order_relaxed);
        debug.note.store(-1, std::memory_order_relaxed);
        debug.pitchBend.store(8192, std::memory_order_relaxed);
        debug.age.store(0, std::memory_order_relaxed);
        return;
    }

    const auto& token = scheduledNotes[(size_t) newest];
    debug.sourceId.store(sourceId, std::memory_order_relaxed);
    debug.channel.store(token.channel, std::memory_order_relaxed);
    debug.note.store(token.note, std::memory_order_relaxed);
    debug.pitchBend.store(8192, std::memory_order_relaxed);
    debug.age.store(token.age, std::memory_order_relaxed);
    debug.active.store(1, std::memory_order_release);
#else
    juce::ignoreUnused(sourceId);
#endif
}

bool MpeMidiOutput::releaseScheduledNote (int slot,
                                          juce::MidiBuffer& midiMessages,
                                          int sampleOffset,
                                          bool deadlineRelease) noexcept
{
    if (slot < 0 || slot >= kMaxScheduledNotes)
        return false;

    auto& token = scheduledNotes[(size_t) slot];
    if (! token.active)
        return false;

    const int sourceId = token.sourceId;
    const int channel = token.channel;
    const int note = token.note;
    heapRemove(slot);
    unregisterSourceToken(sourceId, slot);

    if (sourceScheduledCounts[(size_t) sourceId] > 0)
        --sourceScheduledCounts[(size_t) sourceId];
    if (channelScheduledCounts[(size_t) (channel - 1)] > 0)
        --channelScheduledCounts[(size_t) (channel - 1)];
    if (activeScheduledNotes > 0)
        --activeScheduledNotes;

    auto& refs = normalNoteRefCounts[
        (size_t) ((channel - 1) * 128 + note)];
    if (refs == 0)
    {
        // Any ledger mismatch is safer as a bounded global reset than as a
        // guessed per-note repair that could strand a different owner.
        failClosed(midiMessages, sampleOffset, false);
        return false;
    }

    --refs;
    if (refs == 0)
    {
        midiMessages.addEvent(juce::MidiMessage::noteOff(channel, note),
                              sampleOffset);
        if (activePhysicalNotes > 0)
            --activePhysicalNotes;
    }

    token = {};
    token.nextFree = freeTokenHead;
    freeTokenHead = slot;
    scheduledNoteCount.store(activeScheduledNotes, std::memory_order_relaxed);
    physicalNoteCount.store(activePhysicalNotes, std::memory_order_relaxed);
    if (deadlineRelease)
        deadlineReleaseCount.fetch_add(1, std::memory_order_relaxed);
    if (sourceScheduledCounts[(size_t) sourceId] == 0)
        recordEndedVoice(sourceId);
    refreshVoiceDebug(sourceId);
    return true;
}

void MpeMidiOutput::releaseDueNotes (uint64_t limitSample, bool inclusive,
                                     uint64_t blockStart, int lastSample,
                                     juce::MidiBuffer& midiMessages) noexcept
{
    while (deadlineHeapSize > 0)
    {
        const int slot = deadlineHeap[0];
        const uint64_t deadline = scheduledNotes[(size_t) slot].deadlineSample;
        if (deadline > limitSample || (! inclusive && deadline == limitSample))
            break;

        int sampleOffset = 0;
        if (deadline > blockStart)
        {
            const uint64_t relative = deadline - blockStart;
            sampleOffset = relative > (uint64_t) lastSample
                         ? lastSample : (int) relative;
        }
        if (! releaseScheduledNote(slot, midiMessages, sampleOffset, true))
            return;
    }
}

void MpeMidiOutput::cancelVoice (int sourceId,
                                 juce::MidiBuffer& midiMessages,
                                 int sampleOffset) noexcept
{
    if (sourceId < 0 || sourceId >= kMaxMidiSources)
        return;

    while (sourceScheduledCounts[(size_t) sourceId] > 0)
    {
        int slot = -1;
        for (const int candidate : sourceTokenSlots[(size_t) sourceId])
        {
            if (candidate >= 0 && scheduledNotes[(size_t) candidate].active)
            {
                slot = candidate;
                break;
            }
        }

        if (slot < 0)
        {
            failClosed(midiMessages, sampleOffset, false);
            return;
        }
        if (! releaseScheduledNote(slot, midiMessages, sampleOffset, false))
            return;
    }
}

void MpeMidiOutput::startScheduledNote (const MpeConfig& config,
                                        const NoteEvent& event,
                                        juce::MidiBuffer& midiMessages,
                                        int sampleOffset,
                                        uint64_t absoluteSample,
                                        uint64_t durationSamples,
                                        SameNotePolicy sameNotePolicy) noexcept
{
    const int sourceId = event.sourceId;
    const auto pitch = convertFrequencyToMidiPitch(event.frequencyHz, 2);
    const int velocity = velocityFromUnit(event.velocity);
    const bool routeByParticipant = config.normalRoutingMode == 1
                                 && event.participantId >= 0;
    const int channel = routeByParticipant
        ? normalChannelForParticipant(event.participantId)
        : juce::jlimit(1, 16, config.normalMidiChannel + 1);
    const int note = pitch.noteNumber;
    const uint64_t deadline = absoluteSample
        > std::numeric_limits<uint64_t>::max() - durationSamples
        ? std::numeric_limits<uint64_t>::max()
        : absoluteSample + durationSamples;

    if (midiVoiceAgeCounter == std::numeric_limits<uint64_t>::max())
    {
        failClosed(midiMessages, sampleOffset, false);
        return;
    }

    const int matching = findMatchingToken(sourceId, channel, note);
    if (matching >= 0)
    {
        if (normalNoteRefCounts[
                (size_t) ((channel - 1) * 128 + note)] == 0)
        {
            // A semantic token without a physical-key owner is a corrupt
            // ledger. Fail closed rather than manufacturing a partial repair.
            failClosed(midiMessages, sampleOffset, false);
            return;
        }

        auto& token = scheduledNotes[(size_t) matching];
        heapRemove(matching);
        token.deadlineSample = deadline;
        token.age = ++midiVoiceAgeCounter;
        heapInsert(matching);

        if (sameNotePolicy == SameNotePolicy::Retrigger)
        {
            // The ownership token/refcount stays unchanged. Only the physical
            // attack envelope is restarted, with stable same-sample ordering.
            midiMessages.addEvent(juce::MidiMessage::noteOff(channel, note),
                                  sampleOffset);
            midiMessages.addEvent(
                juce::MidiMessage::noteOn(
                    channel, note, (juce::uint8) velocity),
                sampleOffset);
            midiNotesSent.fetch_add(1, std::memory_order_relaxed);
            hardRetriggerCount.fetch_add(1, std::memory_order_relaxed);
        }

        coalescedRetriggerCount.fetch_add(1, std::memory_order_relaxed);
        refreshVoiceDebug(sourceId);
        return;
    }

    if (sourceScheduledCounts[(size_t) sourceId] >= kMaxScheduledPerSource)
    {
        const int oldest = findOldestForSource(sourceId);
        if (oldest < 0)
        {
            failClosed(midiMessages, sampleOffset, false);
            return;
        }
        else
        {
            if (! releaseScheduledNote(
                    oldest, midiMessages, sampleOffset, false))
                return;
            capacityStealCount.fetch_add(1, std::memory_order_relaxed);
            sourceLimitStealCount.fetch_add(1, std::memory_order_relaxed);
        }
    }

    if (channelScheduledCounts[(size_t) (channel - 1)]
            >= kMaxScheduledPerChannel)
    {
        const int oldest = findOldestForChannel(channel);
        if (oldest < 0)
        {
            failClosed(midiMessages, sampleOffset, false);
            return;
        }
        else
        {
            if (! releaseScheduledNote(
                    oldest, midiMessages, sampleOffset, false))
                return;
            capacityStealCount.fetch_add(1, std::memory_order_relaxed);
            channelLimitStealCount.fetch_add(1, std::memory_order_relaxed);
        }
    }

    if (activeScheduledNotes >= kMaxScheduledNotes)
    {
        const int oldest = findOldestGlobal();
        if (oldest < 0)
        {
            failClosed(midiMessages, sampleOffset, false);
            return;
        }
        else
        {
            if (! releaseScheduledNote(
                    oldest, midiMessages, sampleOffset, false))
                return;
            capacityStealCount.fetch_add(1, std::memory_order_relaxed);
            globalLimitStealCount.fetch_add(1, std::memory_order_relaxed);
        }
    }

    if (freeTokenHead < 0 || freeTokenHead >= kMaxScheduledNotes)
    {
        failClosed(midiMessages, sampleOffset, false);
        return;
    }

    const int slot = freeTokenHead;
    freeTokenHead = scheduledNotes[(size_t) slot].nextFree;
    auto& token = scheduledNotes[(size_t) slot];
    token = {};
    token.active = true;
    token.sourceId = sourceId;
    token.channel = channel;
    token.note = note;
    token.deadlineSample = deadline;
    token.age = ++midiVoiceAgeCounter;

    if (! registerSourceToken(sourceId, slot))
    {
        failClosed(midiMessages, sampleOffset, false);
        return;
    }

    auto& refs = normalNoteRefCounts[
        (size_t) ((channel - 1) * 128 + note)];
    if (refs == std::numeric_limits<uint16_t>::max())
    {
        failClosed(midiMessages, sampleOffset, false);
        return;
    }

    heapInsert(slot);
    ++sourceScheduledCounts[(size_t) sourceId];
    ++channelScheduledCounts[(size_t) (channel - 1)];
    ++activeScheduledNotes;
    if (refs == 0)
    {
        midiMessages.addEvent(
            juce::MidiMessage::noteOn(channel, note, (juce::uint8) velocity),
            sampleOffset);
        midiNotesSent.fetch_add(1, std::memory_order_relaxed);
        ++activePhysicalNotes;
    }
    else if (sameNotePolicy == SameNotePolicy::Retrigger)
    {
        // A different semantic source can share this physical channel/note.
        // Restart the audible envelope without releasing either owner's token.
        midiMessages.addEvent(juce::MidiMessage::noteOff(channel, note),
                              sampleOffset);
        midiMessages.addEvent(
            juce::MidiMessage::noteOn(
                channel, note, (juce::uint8) velocity),
            sampleOffset);
        midiNotesSent.fetch_add(1, std::memory_order_relaxed);
        hardRetriggerCount.fetch_add(1, std::memory_order_relaxed);
    }
    ++refs;

    scheduledNoteCount.store(activeScheduledNotes, std::memory_order_relaxed);
    physicalNoteCount.store(activePhysicalNotes, std::memory_order_relaxed);
    refreshVoiceDebug(sourceId);
}

void MpeMidiOutput::handleMidiSourceEvent (const MpeConfig& config,
                                            const NoteEvent& event,
                                            juce::MidiBuffer& midiMessages,
                                            int sampleOffset,
                                            uint64_t absoluteSample,
                                            uint64_t durationSamples,
                                            SameNotePolicy sameNotePolicy)
{
    if (event.type == NoteEvent::AllNotesOff)
    {
        failClosed(midiMessages, sampleOffset, false);
        return;
    }

    const int sourceId = event.sourceId;
    if (sourceId < 0 || sourceId >= kMaxMidiSources)
        return;

    if (event.type == NoteEvent::CancelVoice)
    {
        cancelVoice(sourceId, midiMessages, sampleOffset);
        return;
    }

    // A normal semantic release ends the gesture but never truncates its fixed
    // musical tail. CancelVoice and AllNotesOff are the explicit safety paths.
    if (event.type == NoteEvent::NoteOff
        || event.type == NoteEvent::Expression
        || config.outputType == 0)
        return;

    if (event.type == NoteEvent::NoteOn)
        startScheduledNote(config, event, midiMessages, sampleOffset,
                           absoluteSample, durationSamples, sameNotePolicy);
}

void MpeMidiOutput::render (const MpeConfig& config,
                            const NoteEvent* events, int count,
                            juce::MidiBuffer& midiMessages, int numSamples,
                            const TimingConfig& timing)
{
    // Valid until the next render call. This is deliberately a fixed-capacity
    // audio-thread hand-off, not a queue shared with the message thread.
    endedVoiceRecorded.fill(0);
    endedVoiceCount = 0;

    MpeConfig safeConfig = config;
    safeConfig.outputType = safeConfig.outputType == 0 ? 0 : 1;
    safeConfig.normalMidiChannel = juce::jlimit(
        0, 15, safeConfig.normalMidiChannel);
    safeConfig.normalRoutingMode = juce::jlimit(
        0, 1, safeConfig.normalRoutingMode);

    if (count < 0)
        count = 0;

    const int blockSamples = juce::jmax(0, numSamples);
    if ((events == nullptr && count > 0) || count > kMaxEventsPerRender)
    {
        failClosed(midiMessages, 0, true);
        renderSampleCursor = (uint64_t) blockSamples;
        return;
    }

    if ((uint64_t) blockSamples
            > std::numeric_limits<uint64_t>::max() - renderSampleCursor)
    {
        failClosed(midiMessages, 0, true);
        renderSampleCursor = (uint64_t) blockSamples;
        return;
    }

    const uint64_t blockStart = renderSampleCursor;
    const uint64_t blockEnd = blockStart + (uint64_t) blockSamples;
    const int lastSample = blockSamples > 0 ? blockSamples - 1 : 0;
    const uint64_t durationSamples = durationSamplesFor(timing);
    const auto sameNotePolicy =
        timing.sameNotePolicy == SameNotePolicy::Retrigger
            ? SameNotePolicy::Retrigger : SameNotePolicy::Tie;

    // Hostile/direct callers are not required to pre-sort their fixed event
    // list. Stable insertion sort is bounded by 64 entries and allocates none.
    std::array<int, kMaxEventsPerRender> order {};
    for (int index = 0; index < count; ++index)
    {
        const int eventOffset = juce::jlimit(
            0, lastSample, events[(size_t) index].sampleOffset);
        int insertion = index;
        while (insertion > 0)
        {
            const int previous = order[(size_t) (insertion - 1)];
            const int previousOffset = juce::jlimit(
                0, lastSample, events[(size_t) previous].sampleOffset);
            if (previousOffset <= eventOffset)
                break;
            order[(size_t) insertion] = previous;
            --insertion;
        }
        order[(size_t) insertion] = index;
    }

    for (int ordered = 0; ordered < count; ++ordered)
    {
        const auto& event = events[(size_t) order[(size_t) ordered]];
        const int sampleOffset = juce::jlimit(
            0, lastSample, event.sampleOffset);
        const uint64_t absoluteSample = blockStart + (uint64_t) sampleOffset;

        // At an identical sample, close the old deadline before admitting a
        // new attack. This gives deterministic NoteOff -> NoteOn ordering.
        releaseDueNotes(absoluteSample, true, blockStart, lastSample,
                        midiMessages);
        handleMidiSourceEvent(safeConfig, event, midiMessages, sampleOffset,
                              absoluteSample, durationSamples, sameNotePolicy);
    }

    if (blockSamples > 0)
        releaseDueNotes(blockEnd, false, blockStart, lastSample, midiMessages);
    renderSampleCursor = blockEnd;
}

void MpeMidiOutput::render (const MpeConfig& config,
                            const NoteEvent* events, int count,
                            juce::MidiBuffer& midiMessages, int numSamples)
{
    render(config, events, count, midiMessages, numSamples, TimingConfig {});
}

void MpeMidiOutput::recordOutgoingMidiDebugEvents (
    const juce::MidiBuffer& midiMessages) noexcept
{
#if COSMIC_MIDI_DIAGNOSTICS
    if (midiMessages.isEmpty())
        return;

    for (const auto metadata : midiMessages)
    {
        const int rawSize = metadata.numBytes;
        if (rawSize <= 0 || rawSize > 3)
            continue;

        const auto sequence = midiDebugWriteCounter.fetch_add(
            1, std::memory_order_relaxed) + 1;
        auto& slot = midiDebugEvents[
            (size_t) ((sequence - 1) % kDebugEventQueueSize)];
        slot.sequence.store(0, std::memory_order_release);
        slot.sampleOffset.store(metadata.samplePosition,
                                std::memory_order_relaxed);
        slot.size.store(rawSize, std::memory_order_relaxed);

        const auto* raw = metadata.data;
        slot.byte0.store(rawSize > 0 ? raw[0] : 0,
                         std::memory_order_relaxed);
        slot.byte1.store(rawSize > 1 ? raw[1] : 0,
                         std::memory_order_relaxed);
        slot.byte2.store(rawSize > 2 ? raw[2] : 0,
                         std::memory_order_relaxed);
        slot.sequence.store(sequence, std::memory_order_release);
    }
#else
    juce::ignoreUnused(midiMessages);
#endif
}

bool MpeMidiOutput::readOutgoingDebugSlot (
    uint32_t sequence, OutgoingDebugEvent& out) const noexcept
{
#if COSMIC_MIDI_DIAGNOSTICS
    const auto& slot = midiDebugEvents[
        (size_t) ((sequence - 1) % kDebugEventQueueSize)];
    const auto storedSequence = slot.sequence.load(std::memory_order_acquire);
    if (storedSequence != sequence)
        return false;

    out.sampleOffset = slot.sampleOffset.load(std::memory_order_relaxed);
    out.size = slot.size.load(std::memory_order_relaxed);
    out.b0 = slot.byte0.load(std::memory_order_relaxed);
    out.b1 = slot.byte1.load(std::memory_order_relaxed);
    out.b2 = slot.byte2.load(std::memory_order_relaxed);
    return true;
#else
    juce::ignoreUnused(sequence, out);
    return false;
#endif
}

MpeMidiOutput::VoiceDebugSnapshot MpeMidiOutput::getVoiceDebugSnapshot (
    int index) const noexcept
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
