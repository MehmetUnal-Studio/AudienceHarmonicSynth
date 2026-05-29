---
name: mpe-midi-monitor
description: Use this skill when implementing or reviewing an MPE MIDI Monitor JUCE plugin that visualizes MIDI and MPE messages in DAWs.
---

# MPE MIDI Monitor Skill

## Purpose
Create or review a realtime-safe MIDI/MPE monitor plugin.

## Core rules
- The plugin monitors MIDI/MPE.
- It does not generate sound.
- Audio output should be silent.
- MIDI pass-through must be explicit.
- UI must not read or mutate audio-thread data unsafely.

## Realtime rules
Inside processBlock:
- no allocation
- no locks
- no UI calls
- no juce::String formatting
- no logging
- no file I/O

Use a fixed-size ring buffer or preallocated event queue.

## Required views
- Note view
- Flow/event list view
- MPE view

## Required MIDI parsing
Monitor:
- Note On
- Note Off
- CC
- pitch bend
- channel pressure
- poly aftertouch
- program change
- clock/transport messages
- SysEx indicator

## MPE tracking
Track per member channel:
- active note
- velocity
- pitch bend
- pitch bend converted to cents if bend range is known
- pressure
- CC74 timbre
- CC11 expression
- last event time

Default:
- Lower Zone
- master channel 1
- member channels 2-16
- bend range 48 semitones

## UI behavior
Freeze stops UI updates only.
Clear clears displayed event history.
Pass-through controls whether incoming MIDI is forwarded.
