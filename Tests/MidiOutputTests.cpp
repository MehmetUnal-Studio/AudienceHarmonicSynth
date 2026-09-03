#include "../Source/MpeMidiOutput.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
    int failed = 0;

    void expect (bool condition, const char* name)
    {
        std::cout << (condition ? "PASS  " : "FAIL  ") << name << "\n";
        if (! condition)
            ++failed;
    }

    struct Message
    {
        int status = 0;
        int channel = 0;
        int data1 = 0;
        int data2 = 0;
        int sampleOffset = 0;
    };

    std::vector<Message> decode (const juce::MidiBuffer& buffer)
    {
        std::vector<Message> result;
        for (const auto metadata : buffer)
        {
            if (metadata.numBytes < 1 || metadata.numBytes > 3)
                continue;

            const auto* bytes = metadata.data;
            result.push_back ({ bytes[0] & 0xf0,
                                (bytes[0] & 0x0f) + 1,
                                metadata.numBytes > 1 ? bytes[1] & 0x7f : 0,
                                metadata.numBytes > 2 ? bytes[2] & 0x7f : 0,
                                metadata.samplePosition });
        }
        return result;
    }

    MpeMidiOutput::MpeConfig notesOnlyConfig (bool perSource = false)
    {
        MpeMidiOutput::MpeConfig config;
        config.outputType = 1;
        config.normalMidiChannel = 5; // zero-based: MIDI channel 6
        config.normalRoutingMode = perSource ? 1 : 0;
        return config;
    }

    MpeMidiOutput::NoteEvent noteOn (int sourceId, int participantId,
                                     double frequencyHz, float velocity)
    {
        MpeMidiOutput::NoteEvent event;
        event.type = MpeMidiOutput::NoteEvent::NoteOn;
        event.sourceId = sourceId;
        event.participantId = participantId;
        event.frequencyHz = frequencyHz;
        event.velocity = velocity;
        event.x = 0.91f;
        event.y = 0.37f;
        return event;
    }

    MpeMidiOutput::NoteEvent noteOff (int sourceId)
    {
        MpeMidiOutput::NoteEvent event;
        event.type = MpeMidiOutput::NoteEvent::NoteOff;
        event.sourceId = sourceId;
        return event;
    }

    MpeMidiOutput::NoteEvent cancelVoice (int sourceId)
    {
        auto event = noteOff(sourceId);
        event.type = MpeMidiOutput::NoteEvent::CancelVoice;
        return event;
    }

    MpeMidiOutput::TimingConfig timing (
        MpeMidiOutput::NoteDuration duration,
        double bpm = 120.0, double sampleRate = 48000.0,
        MpeMidiOutput::SameNotePolicy sameNotePolicy =
            MpeMidiOutput::SameNotePolicy::Tie)
    {
        MpeMidiOutput::TimingConfig result;
        result.sampleRate = sampleRate;
        result.bpm = bpm;
        result.noteDuration = duration;
        result.sameNotePolicy = sameNotePolicy;
        return result;
    }

    double frequencyForMidiNote (int note)
    {
        return 440.0 * std::pow(2.0, ((double) note - 69.0) / 12.0);
    }

    bool isMusicalController (const Message& message)
    {
        return message.status == 0xb0
            || message.status == 0xd0
            || message.status == 0xe0
            || message.status == 0xa0;
    }

    int countStatus (const std::vector<Message>& messages, int status)
    {
        int count = 0;
        for (const auto& message : messages)
            count += message.status == status ? 1 : 0;
        return count;
    }
}

int main()
{
    expect(MpeMidiOutput::velocityFromUnit(0.0f) == 1
               && MpeMidiOutput::velocityFromUnit(0.5f) == 64
               && MpeMidiOutput::velocityFromUnit(1.0f) == 127
               && MpeMidiOutput::velocityFromUnit(
                      std::numeric_limits<float>::quiet_NaN()) == 1,
           "V maps only to a bounded Note-On velocity");

    expect(MpeMidiOutput::normalChannelForParticipant(1) == 1
               && MpeMidiOutput::normalChannelForParticipant(16) == 16
               && MpeMidiOutput::normalChannelForParticipant(17) == 1
               && MpeMidiOutput::normalChannelForParticipant(0) == 16,
           "source IDs wrap deterministically over MIDI channels 1-16");

    // Output Off must remain completely silent, including legacy expression data.
    {
        MpeMidiOutput output;
        auto config = notesOnlyConfig();
        config.outputType = 0;
        const auto event = noteOn(1, 1, 440.0, 0.8f);
        juce::MidiBuffer buffer;
        output.render(config, &event, 1, buffer, 64);
        expect(buffer.isEmpty(), "Off emits no MIDI");
    }

    // The attack contract is exactly one Note On. U chooses the nearest note;
    // V supplies velocity. X/Y must not leak into CC11, CC74, pressure or bend.
    {
        MpeMidiOutput output;
        const auto config = notesOnlyConfig();
        const auto event = noteOn(3, 3, 440.0, 0.25f);
        juce::MidiBuffer buffer;
        output.render(config, &event, 1, buffer, 64);
        const auto messages = decode(buffer);

        const bool exactNote = messages.size() == 1
                            && messages[0].status == 0x90
                            && messages[0].channel == 6
                            && messages[0].data1 == 69
                            && messages[0].data2 == 32;
        bool hasController = false;
        for (const auto& message : messages)
            hasController = hasController || isMusicalController(message);

        expect(exactNote && output.getMidiNotesSent() == 1,
               "Notes Only attack emits and counts one physical Note On with V velocity");
        expect(! hasController,
               "Notes Only attack emits no CC, Pressure, Pitch Bend or poly pressure");
    }

    // A legacy preset/direct caller may still present outputType=2. It must be
    // coerced to Notes Only and must never revive setup RPNs or MPE expression.
    {
        MpeMidiOutput output;
        auto legacyConfig = notesOnlyConfig(true);
        legacyConfig.outputType = 2;
        legacyConfig.sendSetupMessages = true;
        const auto event = noteOn(17, 17, 445.0, 0.8f);
        juce::MidiBuffer buffer;
        output.render(legacyConfig, &event, 1, buffer, 64);
        const auto messages = decode(buffer);

        bool hasController = false;
        for (const auto& message : messages)
            hasController = hasController || isMusicalController(message);

        expect(messages.size() == 1 && messages[0].status == 0x90
                   && messages[0].channel == 1,
               "legacy MPE selection is coerced to per-source Notes Only");
        expect(! hasController, "legacy state cannot emit MPE setup or expression messages");
    }

    // Held motion is canonical state for a future attack only. The renderer
    // emits no continuous participant MIDI while the note is held.
    {
        MpeMidiOutput output;
        const auto config = notesOnlyConfig();
        const auto attack = noteOn(9, 9, 440.0, 0.8f);
        juce::MidiBuffer first;
        output.render(config, &attack, 1, first, 64);

        auto motion = attack;
        motion.type = MpeMidiOutput::NoteEvent::Expression;
        motion.frequencyHz = 466.1637615;
        motion.x = 0.01f;
        motion.y = 0.99f;
        juce::MidiBuffer moved;
        output.render(config, &motion, 1, moved, 64);
        expect(moved.isEmpty(), "held U/V motion emits no continuous MIDI");
    }

    // A pitch-region change starts a new fixed tail. The previous pitch remains
    // owned until its own sample deadline instead of being cut by the attack.
    {
        MpeMidiOutput output;
        const auto config = notesOnlyConfig();
        auto event = noteOn(11, 11, 440.0, 0.8f);
        juce::MidiBuffer first;
        output.render(config, &event, 1, first, 64);

        event.frequencyHz = 493.8833013;
        event.velocity = 0.4f;
        juce::MidiBuffer changed;
        output.render(config, &event, 1, changed, 64);
        const auto messages = decode(changed);

        bool onlyNotes = true;
        for (const auto& message : messages)
            onlyNotes = onlyNotes && (message.status == 0x80 || message.status == 0x90);

        expect(messages.size() == 1 && messages[0].status == 0x90
                   && messages[0].data1 == 71,
               "pitch-region change preserves the old tail and emits the new Note On");
        expect(output.getScheduledNoteCount() == 2,
               "pitch-region change owns two bounded scheduled tails");
        expect(onlyNotes, "pitch change contains no musical controller messages");

        juce::MidiBuffer tails;
        output.render(config, nullptr, 0, tails, 2937);
        const auto tailMessages = decode(tails);
        expect(tailMessages.size() == 2
                   && tailMessages[0].status == 0x80
                   && tailMessages[0].data1 == 69
                   && tailMessages[0].sampleOffset == 2872
                   && tailMessages[1].status == 0x80
                   && tailMessages[1].data1 == 71
                   && tailMessages[1].sampleOffset == 2936,
               "pitch-change tails retain independent sample deadlines");
    }

    // Sources 1 and 17 share channel 1 in per-source routing. A physical note
    // stays held until the last semantic owner releases it.
    {
        MpeMidiOutput output;
        const auto config = notesOnlyConfig(true);
        const auto firstAttack = noteOn(10, 1, 440.0, 0.8f);
        const auto secondAttack = noteOn(170, 17, 440.0, 0.6f);
        juce::MidiBuffer attacks;
        output.render(config, &firstAttack, 1, attacks, 64);
        output.render(config, &secondAttack, 1, attacks, 64);
        expect(countStatus(decode(attacks), 0x90) == 1
                   && output.getMidiNotesSent() == 1,
               "same channel/note semantic owners share one physical Note On");

        const auto firstRelease = cancelVoice(10);
        juce::MidiBuffer releaseOne;
        output.render(config, &firstRelease, 1, releaseOne, 64);
        expect(releaseOne.isEmpty(), "first shared owner release keeps the note held");

        const auto lastRelease = cancelVoice(170);
        juce::MidiBuffer releaseTwo;
        output.render(config, &lastRelease, 1, releaseTwo, 64);
        const auto finalMessages = decode(releaseTwo);
        expect(finalMessages.size() == 1 && finalMessages[0].status == 0x80
                   && finalMessages[0].channel == 1 && finalMessages[0].data1 == 69,
               "last shared owner emits the one physical Note Off");
    }

    // Every menu choice is converted from the onset BPM snapshot to an exact
    // sample deadline. The second block proves the scheduler advances even
    // when there are no new lifecycle events.
    {
        struct DurationCase
        {
            MpeMidiOutput::NoteDuration duration;
            uint64_t samples;
        };
        constexpr DurationCase cases[] {
            { MpeMidiOutput::NoteDuration::Half,         48000 },
            { MpeMidiOutput::NoteDuration::Quarter,      24000 },
            { MpeMidiOutput::NoteDuration::Eighth,       12000 },
            { MpeMidiOutput::NoteDuration::Sixteenth,     6000 },
            { MpeMidiOutput::NoteDuration::ThirtySecond,  3000 }
        };

        bool exact = true;
        for (const auto& durationCase : cases)
        {
            MpeMidiOutput output;
            const auto config = notesOnlyConfig();
            const auto clock = timing(durationCase.duration);
            exact = exact
                 && MpeMidiOutput::durationSamplesFor(clock)
                        == durationCase.samples;

            const auto attack = noteOn(1, 1, 440.0, 0.8f);
            juce::MidiBuffer attackBuffer;
            output.render(config, &attack, 1, attackBuffer, 1, clock);

            juce::MidiBuffer deadlineBuffer;
            output.render(config, nullptr, 0, deadlineBuffer,
                          (int) durationCase.samples, clock);
            const auto messages = decode(deadlineBuffer);
            exact = exact && messages.size() == 1
                 && messages[0].status == 0x80
                 && messages[0].data1 == 69
                 && messages[0].sampleOffset
                        == (int) durationCase.samples - 1
                 && output.getEndedVoiceCount() == 1
                 && output.getEndedVoiceId(0) == 1
                 && output.getScheduledNoteCountForVoice(1) == 0;
        }
        expect(exact,
               "2n/4n/8n/16n/32n produce exact 120 BPM sample deadlines");
    }

    // Ordinary gesture release must not truncate a musical tail. The explicit
    // safety cancellation closes it immediately and removes its deadline.
    {
        MpeMidiOutput output;
        const auto config = notesOnlyConfig();
        const auto clock = timing(MpeMidiOutput::NoteDuration::Quarter);
        const auto attack = noteOn(4, 4, 440.0, 0.8f);
        juce::MidiBuffer on;
        output.render(config, &attack, 1, on, 1, clock);

        const auto release = noteOff(4);
        juce::MidiBuffer semanticRelease;
        output.render(config, &release, 1, semanticRelease, 128, clock);
        expect(semanticRelease.isEmpty() && output.getScheduledNoteCount() == 1,
               "semantic NoteOff leaves the fixed tail scheduled");

        const auto cancel = cancelVoice(4);
        juce::MidiBuffer safetyRelease;
        output.render(config, &cancel, 1, safetyRelease, 64, clock);
        const auto messages = decode(safetyRelease);
        expect(messages.size() == 1 && messages[0].status == 0x80
                   && output.getScheduledNoteCount() == 0,
               "CancelVoice immediately closes and unschedules the voice");
    }

    // Tempo belongs to each attack, not to the scheduler globally. Changing
    // BPM after note A starts must only affect newly started note B.
    {
        MpeMidiOutput output;
        const auto config = notesOnlyConfig();
        const auto at120 = timing(MpeMidiOutput::NoteDuration::Quarter, 120.0);
        const auto at60 = timing(MpeMidiOutput::NoteDuration::Quarter, 60.0);
        auto first = noteOn(1, 1, frequencyForMidiNote(69), 0.8f);
        auto second = noteOn(2, 2, frequencyForMidiNote(71), 0.8f);
        juce::MidiBuffer firstOn;
        output.render(config, &first, 1, firstOn, 1, at120);
        juce::MidiBuffer secondOn;
        output.render(config, &second, 1, secondOn, 1, at60);

        juce::MidiBuffer firstDeadline;
        output.render(config, nullptr, 0, firstDeadline, 23999, at60);
        const auto firstMessages = decode(firstDeadline);
        const bool firstStayedAt120 = firstMessages.size() == 1
                                   && firstMessages[0].status == 0x80
                                   && firstMessages[0].data1 == 69
                                   && firstMessages[0].sampleOffset == 23998;

        juce::MidiBuffer secondDeadline;
        output.render(config, nullptr, 0, secondDeadline, 24001, at120);
        const auto secondMessages = decode(secondDeadline);
        const bool secondUsed60 = secondMessages.size() == 1
                               && secondMessages[0].status == 0x80
                               && secondMessages[0].data1 == 71
                               && secondMessages[0].sampleOffset == 24000;
        expect(firstStayedAt120 && secondUsed60,
               "BPM is snapshotted independently at each Note On");
    }

    // A same-source/same-key retrigger coalesces into one ownership and moves
    // its deadline. It must neither emit a duplicate Note On nor inflate refs.
    {
        MpeMidiOutput output;
        const auto config = notesOnlyConfig();
        const auto at120 = timing(
            MpeMidiOutput::NoteDuration::ThirtySecond, 120.0, 48000.0,
            MpeMidiOutput::SameNotePolicy::Tie);
        const auto at60 = timing(MpeMidiOutput::NoteDuration::ThirtySecond, 60.0);
        const auto attack = noteOn(7, 7, 440.0, 0.8f);
        juce::MidiBuffer first;
        output.render(config, &attack, 1, first, 1, at120);
        juce::MidiBuffer retrigger;
        output.render(config, &attack, 1, retrigger, 1, at60);
        const bool tieOwnershipStayedStable =
            output.getScheduledNoteCount() == 1
            && output.getPhysicalNoteCount() == 1
            && output.getScheduledNoteCountForVoice(7) == 1;

        juce::MidiBuffer oldDeadline;
        output.render(config, nullptr, 0, oldDeadline, 2999, at120);
        juce::MidiBuffer newDeadline;
        output.render(config, nullptr, 0, newDeadline, 3001, at120);
        const auto messages = decode(newDeadline);
        expect(retrigger.isEmpty() && oldDeadline.isEmpty()
                   && tieOwnershipStayedStable
                   && output.getCoalescedRetriggerCount() == 1
                   && output.getHardRetriggerCount() == 0
                   && output.getMidiNotesSent() == 1
                   && messages.size() == 1 && messages[0].status == 0x80
                   && messages[0].sampleOffset == 3000,
               "Tie preserves same-source coalescing and reschedules exactly once");
    }

    // Retrigger re-articulates an already-held physical key while retaining the
    // same semantic token. The final cancellation must still emit one clean off.
    {
        MpeMidiOutput output;
        const auto config = notesOnlyConfig();
        const auto clock = timing(
            MpeMidiOutput::NoteDuration::Quarter, 120.0, 48000.0,
            MpeMidiOutput::SameNotePolicy::Retrigger);
        auto attack = noteOn(7, 7, 440.0, 0.8f);
        juce::MidiBuffer first;
        output.render(config, &attack, 1, first, 1, clock);

        attack.velocity = 0.4f;
        juce::MidiBuffer retrigger;
        output.render(config, &attack, 1, retrigger, 1, clock);
        const auto retriggerMessages = decode(retrigger);
        const bool exactHardRetrigger = retriggerMessages.size() == 2
            && retriggerMessages[0].status == 0x80
            && retriggerMessages[0].channel == 6
            && retriggerMessages[0].data1 == 69
            && retriggerMessages[0].sampleOffset == 0
            && retriggerMessages[1].status == 0x90
            && retriggerMessages[1].channel == 6
            && retriggerMessages[1].data1 == 69
            && retriggerMessages[1].data2 == 51
            && retriggerMessages[1].sampleOffset == 0;
        expect(exactHardRetrigger
                   && output.getScheduledNoteCount() == 1
                   && output.getPhysicalNoteCount() == 1
                   && output.getScheduledNoteCountForVoice(7) == 1
                   && output.getCoalescedRetriggerCount() == 1
                   && output.getHardRetriggerCount() == 1
                   && output.getMidiNotesSent() == 2,
               "Retrigger hard-restarts a same-source key without inflating ownership");

        const auto cancel = cancelVoice(7);
        juce::MidiBuffer cleanup;
        output.render(config, &cancel, 1, cleanup, 1, clock);
        const auto cleanupMessages = decode(cleanup);
        expect(cleanupMessages.size() == 1
                   && cleanupMessages[0].status == 0x80
                   && cleanupMessages[0].channel == 6
                   && cleanupMessages[0].data1 == 69
                   && output.getScheduledNoteCount() == 0
                   && output.getPhysicalNoteCount() == 0
                   && output.getScheduledNoteCountForVoice(7) == 0,
               "same-source hard retrigger retains one final cleanup owner");
    }

    // A newly admitted source sharing a channel/note also hard-retriggers that
    // physical key, but both semantic owners keep their independent deadlines.
    {
        MpeMidiOutput output;
        const auto config = notesOnlyConfig(true);
        const auto clock = timing(
            MpeMidiOutput::NoteDuration::Quarter, 120.0, 48000.0,
            MpeMidiOutput::SameNotePolicy::Retrigger);
        const auto firstAttack = noteOn(10, 1, 440.0, 0.8f);
        const auto secondAttack = noteOn(170, 17, 440.0, 0.6f);
        juce::MidiBuffer first;
        output.render(config, &firstAttack, 1, first, 1, clock);
        juce::MidiBuffer second;
        output.render(config, &secondAttack, 1, second, 1, clock);
        const auto secondMessages = decode(second);
        const bool exactSharedHardRetrigger = secondMessages.size() == 2
            && secondMessages[0].status == 0x80
            && secondMessages[0].channel == 1
            && secondMessages[0].data1 == 69
            && secondMessages[0].sampleOffset == 0
            && secondMessages[1].status == 0x90
            && secondMessages[1].channel == 1
            && secondMessages[1].data1 == 69
            && secondMessages[1].data2 == 76
            && secondMessages[1].sampleOffset == 0;
        expect(exactSharedHardRetrigger
                   && output.getScheduledNoteCount() == 2
                   && output.getPhysicalNoteCount() == 1
                   && output.getScheduledNoteCountForVoice(10) == 1
                   && output.getScheduledNoteCountForVoice(170) == 1
                   && output.getCoalescedRetriggerCount() == 0
                   && output.getHardRetriggerCount() == 1
                   && output.getMidiNotesSent() == 2,
               "Retrigger hard-restarts a cross-source shared key without merging owners");

        const auto firstCancel = cancelVoice(10);
        juce::MidiBuffer firstCleanup;
        output.render(config, &firstCancel, 1, firstCleanup, 1, clock);
        const bool firstOwnerStayedSilent = firstCleanup.isEmpty()
            && output.getScheduledNoteCount() == 1
            && output.getPhysicalNoteCount() == 1
            && output.getScheduledNoteCountForVoice(10) == 0
            && output.getScheduledNoteCountForVoice(170) == 1;

        const auto lastCancel = cancelVoice(170);
        juce::MidiBuffer lastCleanup;
        output.render(config, &lastCancel, 1, lastCleanup, 1, clock);
        const auto lastMessages = decode(lastCleanup);
        expect(firstOwnerStayedSilent
                   && lastMessages.size() == 1
                   && lastMessages[0].status == 0x80
                   && lastMessages[0].channel == 1
                   && lastMessages[0].data1 == 69
                   && output.getScheduledNoteCount() == 0
                   && output.getPhysicalNoteCount() == 0
                   && output.getScheduledNoteCountForVoice(170) == 0,
               "cross-source hard retrigger releases only after the final owner");
    }

    // Stable input ordering must also hold when both shared-key attacks arrive
    // in one render call at the exact same sample.
    {
        MpeMidiOutput output;
        const auto config = notesOnlyConfig(true);
        const auto clock = timing(
            MpeMidiOutput::NoteDuration::Quarter, 120.0, 48000.0,
            MpeMidiOutput::SameNotePolicy::Retrigger);
        std::array<MpeMidiOutput::NoteEvent, 2> attacks {
            noteOn(10, 1, 440.0, 0.8f),
            noteOn(170, 17, 440.0, 0.6f)
        };
        attacks[0].sampleOffset = 17;
        attacks[1].sampleOffset = 17;

        juce::MidiBuffer buffer;
        output.render(config, attacks.data(), (int) attacks.size(), buffer,
                      64, clock);
        const auto messages = decode(buffer);
        expect(messages.size() == 3
                   && messages[0].status == 0x90
                   && messages[1].status == 0x80
                   && messages[2].status == 0x90
                   && messages[0].channel == 1
                   && messages[1].channel == 1
                   && messages[2].channel == 1
                   && messages[0].data1 == 69
                   && messages[1].data1 == 69
                   && messages[2].data1 == 69
                   && messages[0].sampleOffset == 17
                   && messages[1].sampleOffset == 17
                   && messages[2].sampleOffset == 17
                   && output.getScheduledNoteCount() == 2
                   && output.getPhysicalNoteCount() == 1
                   && output.getHardRetriggerCount() == 1
                   && output.getMidiNotesSent() == 2,
               "same-render same-sample shared attacks keep deterministic On-Off-On order");
    }

    // Unrecognised serialized/runtime enum values fail safely to legacy Tie.
    {
        MpeMidiOutput output;
        const auto config = notesOnlyConfig();
        auto clock = timing(MpeMidiOutput::NoteDuration::Quarter);
        clock.sameNotePolicy =
            static_cast<MpeMidiOutput::SameNotePolicy>(255);
        const auto attack = noteOn(7, 7, 440.0, 0.8f);
        juce::MidiBuffer first;
        output.render(config, &attack, 1, first, 1, clock);
        juce::MidiBuffer second;
        output.render(config, &attack, 1, second, 1, clock);
        expect(decode(first).size() == 1
                   && second.isEmpty()
                   && output.getScheduledNoteCount() == 1
                   && output.getPhysicalNoteCount() == 1
                   && output.getCoalescedRetriggerCount() == 1
                   && output.getHardRetriggerCount() == 0
                   && output.getMidiNotesSent() == 1,
               "invalid same-note policy is sanitised to Tie");
    }

    // A hard retrigger re-articulates immediately and also moves the retained
    // token's sample deadline. The original deadline must not release it.
    {
        MpeMidiOutput output;
        const auto config = notesOnlyConfig();
        const auto clock = timing(
            MpeMidiOutput::NoteDuration::ThirtySecond, 120.0, 48000.0,
            MpeMidiOutput::SameNotePolicy::Retrigger);
        const auto attack = noteOn(7, 7, 440.0, 0.8f);
        juce::MidiBuffer first;
        output.render(config, &attack, 1, first, 1, clock);

        juce::MidiBuffer beforeRetrigger;
        output.render(config, nullptr, 0, beforeRetrigger, 1499, clock);
        juce::MidiBuffer retrigger;
        output.render(config, &attack, 1, retrigger, 1, clock);
        const auto retriggerMessages = decode(retrigger);

        juce::MidiBuffer oldDeadline;
        output.render(config, nullptr, 0, oldDeadline, 1500, clock);
        const bool retainedPastOldDeadline = oldDeadline.isEmpty()
            && output.getScheduledNoteCount() == 1
            && output.getPhysicalNoteCount() == 1;

        juce::MidiBuffer movedDeadline;
        output.render(config, nullptr, 0, movedDeadline, 1500, clock);
        const auto deadlineMessages = decode(movedDeadline);
        expect(beforeRetrigger.isEmpty()
                   && retriggerMessages.size() == 2
                   && retriggerMessages[0].status == 0x80
                   && retriggerMessages[1].status == 0x90
                   && retainedPastOldDeadline
                   && deadlineMessages.size() == 1
                   && deadlineMessages[0].status == 0x80
                   && deadlineMessages[0].sampleOffset == 1499
                   && output.getScheduledNoteCount() == 0
                   && output.getPhysicalNoteCount() == 0
                   && output.getDeadlineReleaseCount() == 1
                   && output.getCoalescedRetriggerCount() == 1
                   && output.getHardRetriggerCount() == 1
                   && output.getMidiNotesSent() == 2,
               "Retrigger moves the semantic deadline without leaving an old release");
    }

    // Policy is sampled per admitted attack. Switching in either direction
    // never mutates or duplicates the retained ownership token.
    {
        MpeMidiOutput output;
        const auto config = notesOnlyConfig();
        const auto tieClock = timing(
            MpeMidiOutput::NoteDuration::Quarter, 120.0, 48000.0,
            MpeMidiOutput::SameNotePolicy::Tie);
        const auto retriggerClock = timing(
            MpeMidiOutput::NoteDuration::Quarter, 120.0, 48000.0,
            MpeMidiOutput::SameNotePolicy::Retrigger);
        const auto attack = noteOn(7, 7, 440.0, 0.8f);

        juce::MidiBuffer initialTie;
        output.render(config, &attack, 1, initialTie, 1, tieClock);
        juce::MidiBuffer tieToRetrigger;
        output.render(config, &attack, 1, tieToRetrigger, 1,
                      retriggerClock);
        juce::MidiBuffer retriggerToTie;
        output.render(config, &attack, 1, retriggerToTie, 1, tieClock);
        juce::MidiBuffer tieToRetriggerAgain;
        output.render(config, &attack, 1, tieToRetriggerAgain, 1,
                      retriggerClock);

        expect(decode(initialTie).size() == 1
                   && decode(tieToRetrigger).size() == 2
                   && retriggerToTie.isEmpty()
                   && decode(tieToRetriggerAgain).size() == 2
                   && output.getScheduledNoteCount() == 1
                   && output.getPhysicalNoteCount() == 1
                   && output.getScheduledNoteCountForVoice(7) == 1
                   && output.getCoalescedRetriggerCount() == 3
                   && output.getHardRetriggerCount() == 2
                   && output.getMidiNotesSent() == 3,
               "Tie and Retrigger transitions preserve one ownership token");

        const auto cancel = cancelVoice(7);
        juce::MidiBuffer cleanup;
        output.render(config, &cancel, 1, cleanup, 1, tieClock);
        expect(decode(cleanup).size() == 1
                   && output.getScheduledNoteCount() == 0
                   && output.getPhysicalNoteCount() == 0,
               "policy transitions retain one deterministic final cleanup");
    }

    // Two semantic owners sharing one key may have different deadlines. The
    // first expiry changes only the refcount; the last expiry emits the off.
    {
        MpeMidiOutput output;
        const auto config = notesOnlyConfig(true);
        const auto at120 = timing(MpeMidiOutput::NoteDuration::ThirtySecond, 120.0);
        const auto at60 = timing(MpeMidiOutput::NoteDuration::ThirtySecond, 60.0);
        const auto first = noteOn(10, 1, 440.0, 0.8f);
        const auto second = noteOn(170, 17, 440.0, 0.6f);
        juce::MidiBuffer firstOn;
        output.render(config, &first, 1, firstOn, 1, at120);
        juce::MidiBuffer secondOn;
        output.render(config, &second, 1, secondOn, 1, at60);

        juce::MidiBuffer firstExpiry;
        output.render(config, nullptr, 0, firstExpiry, 2999, at120);
        const bool firstOwnerReported = output.getEndedVoiceCount() == 1
                                     && output.getEndedVoiceId(0) == 10
                                     && output.getScheduledNoteCountForVoice(10) == 0
                                     && output.getScheduledNoteCountForVoice(170) == 1;
        juce::MidiBuffer lastExpiry;
        output.render(config, nullptr, 0, lastExpiry, 3001, at120);
        const auto finalMessages = decode(lastExpiry);
        expect(firstExpiry.isEmpty() && finalMessages.size() == 1
                   && finalMessages[0].status == 0x80
                   && finalMessages[0].channel == 1
                   && firstOwnerReported
                   && output.getEndedVoiceCount() == 1
                   && output.getEndedVoiceId(0) == 170
                   && output.getScheduledNoteCountForVoice(170) == 0
                   && output.getPhysicalNoteCount() == 0,
               "semantic tail completion is reported even while a shared physical note stays held");
    }

    // A deadline and a fresh attack for the same voice may share one sample.
    // The ended candidate remains observable, while the final token count tells
    // the processor that Grid ownership must not be released yet.
    {
        MpeMidiOutput output;
        const auto config = notesOnlyConfig();
        const auto clock = timing(MpeMidiOutput::NoteDuration::ThirtySecond);
        const auto attack = noteOn(8, 8, 440.0, 0.8f);
        juce::MidiBuffer first;
        output.render(config, &attack, 1, first, 1, clock);
        juce::MidiBuffer beforeDeadline;
        output.render(config, nullptr, 0, beforeDeadline, 2999, clock);

        juce::MidiBuffer sameSample;
        output.render(config, &attack, 1, sameSample, 1, clock);
        const auto messages = decode(sameSample);
        expect(messages.size() == 2 && messages[0].status == 0x80
                   && messages[1].status == 0x90
                   && output.getEndedVoiceCount() == 1
                   && output.getEndedVoiceId(0) == 8
                   && output.getScheduledNoteCountForVoice(8) == 1,
               "same-sample tail/retrigger preserves the final-token reconciliation guard");
    }

    // Sixteen 2n-over-32n tails are a valid musical configuration. The 17th
    // distinct pitch is the first deterministic per-source capacity steal.
    {
        MpeMidiOutput output;
        const auto config = notesOnlyConfig();
        const auto longClock = timing(MpeMidiOutput::NoteDuration::Half, 1.0);
        std::array<MpeMidiOutput::NoteEvent,
                   MpeMidiOutput::kMaxScheduledPerSource + 1> attacks {};
        for (int index = 0; index < (int) attacks.size(); ++index)
            attacks[(size_t) index] = noteOn(
                12, 12, frequencyForMidiNote(40 + index), 0.7f);

        juce::MidiBuffer buffer;
        output.render(config, attacks.data(), (int) attacks.size(), buffer,
                      64, longClock);
        const auto messages = decode(buffer);
        expect(countStatus(messages, 0x90) == 17
                   && countStatus(messages, 0x80) == 1
                   && output.getScheduledNoteCount()
                        == MpeMidiOutput::kMaxScheduledPerSource
                   && output.getSourceLimitStealCount() == 1
                   && output.getCapacityStealCount() == 1,
               "per-source limit steals the deterministic oldest tail only");
    }

    // Channel ownership and global pool limits are independent hard bounds.
    // Shared physical keys keep the test MIDI output small while exercising
    // hundreds/thousands of semantic ownership tokens.
    {
        MpeMidiOutput output;
        const auto config = notesOnlyConfig();
        const auto longClock = timing(MpeMidiOutput::NoteDuration::Half, 1.0);
        std::array<MpeMidiOutput::NoteEvent,
                   MpeMidiOutput::kMaxEventsPerRender> batch {};
        int produced = 0;
        while (produced < MpeMidiOutput::kMaxScheduledPerChannel + 1)
        {
            const int count = juce::jmin(
                MpeMidiOutput::kMaxEventsPerRender,
                MpeMidiOutput::kMaxScheduledPerChannel + 1 - produced);
            for (int index = 0; index < count; ++index)
                batch[(size_t) index] = noteOn(
                    produced + index, produced + index,
                    frequencyForMidiNote(60), 0.7f);
            juce::MidiBuffer block;
            output.render(config, batch.data(), count, block, 1, longClock);
            produced += count;
        }
        expect(output.getScheduledNoteCount()
                    == MpeMidiOutput::kMaxScheduledPerChannel
                   && output.getChannelLimitStealCount() == 1,
               "per-channel ownership is hard-bounded with oldest steal");
    }

    {
        MpeMidiOutput output;
        const auto config = notesOnlyConfig(true);
        const auto longClock = timing(MpeMidiOutput::NoteDuration::Half, 1.0);
        std::array<MpeMidiOutput::NoteEvent,
                   MpeMidiOutput::kMaxEventsPerRender> batch {};
        int produced = 0;
        while (produced < MpeMidiOutput::kMaxScheduledNotes + 1)
        {
            const int count = juce::jmin(
                MpeMidiOutput::kMaxEventsPerRender,
                MpeMidiOutput::kMaxScheduledNotes + 1 - produced);
            for (int index = 0; index < count; ++index)
            {
                const int ordinal = produced + index;
                const int sourceId = ordinal % MpeMidiOutput::kMaxMidiSources;
                const int note = 60 + ordinal / MpeMidiOutput::kMaxMidiSources;
                batch[(size_t) index] = noteOn(
                    sourceId, ordinal % 16 + 1,
                    frequencyForMidiNote(note), 0.7f);
            }
            juce::MidiBuffer block;
            output.render(config, batch.data(), count, block, 1, longClock);
            produced += count;
        }
        expect(output.getScheduledNoteCount() == MpeMidiOutput::kMaxScheduledNotes
                   && output.getGlobalLimitStealCount() == 1,
               "global scheduled-note pool is hard-bounded with oldest steal");

        juce::MidiBuffer deadlineStorm;
        output.render(config, nullptr, 0, deadlineStorm,
                      (int) MpeMidiOutput::durationSamplesFor(longClock) + 1,
                      longClock);
        expect(output.getScheduledNoteCount() == 0
                   && output.getDeadlineReleaseCount()
                        == (uint64_t) MpeMidiOutput::kMaxScheduledNotes
                   && countStatus(decode(deadlineStorm), 0x80) == 32,
               "4096 due ownerships drain safely beyond the 64-event input cap");
    }

    // Panic is the only controller-producing path: CC123 and CC120 once on
    // every channel, no pressure/pitch-bend/RPN/CC11/CC74.
    {
        MpeMidiOutput output;
        const auto config = notesOnlyConfig();
        const auto attack = noteOn(3, 3, 440.0, 0.8f);
        juce::MidiBuffer started;
        output.render(config, &attack, 1, started, 1);
        MpeMidiOutput::NoteEvent panic;
        panic.type = MpeMidiOutput::NoteEvent::AllNotesOff;
        juce::MidiBuffer buffer;
        output.render(config, &panic, 1, buffer, 64);
        const auto messages = decode(buffer);

        bool exactSweep = messages.size() == 32;
        for (int channel = 1; channel <= 16; ++channel)
        {
            const auto first = messages[(size_t) ((channel - 1) * 2)];
            const auto second = messages[(size_t) ((channel - 1) * 2 + 1)];
            exactSweep = exactSweep
                      && first.status == 0xb0 && first.channel == channel
                      && first.data1 == 123 && first.data2 == 0
                      && second.status == 0xb0 && second.channel == channel
                      && second.data1 == 120 && second.data2 == 0;
        }
        expect(exactSweep && output.getScheduledNoteCount() == 0
                   && output.getPhysicalNoteCount() == 0,
               "panic emits the 16-channel sweep and clears every deadline");
    }

    // A hostile oversized lifecycle list fails closed with the same bounded
    // safety sweep instead of partially consuming Note Ons and losing releases.
    {
        MpeMidiOutput output;
        const auto config = notesOnlyConfig();
        std::vector<MpeMidiOutput::NoteEvent> events(
            (size_t) MpeMidiOutput::kMaxEventsPerRender + 1,
            noteOn(1, 1, 440.0, 0.8f));
        juce::MidiBuffer buffer;
        output.render(config, events.data(), (int) events.size(), buffer, 64);
        expect(decode(buffer).size() == 32,
               "oversized lifecycle input fails closed with one bounded panic sweep");
    }

    std::cout << "\nSummary: " << (failed == 0 ? "ok" : "failed") << "\n";
    return failed == 0 ? 0 : 1;
}
