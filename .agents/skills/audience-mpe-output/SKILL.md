---
name: audience-mpe-output
description: Review and implement MPE MIDI output for Audience Harmonic Synth, including spectral tuning, per-note pitch bend, realtime-safe MIDI rendering, and MPE channel allocation.
---

# Audience MPE Output Skill

## Core rules

- MPE output is a parallel layer to the internal audio engine.
- Never replace or break the internal synth/audio engine.
- Spectral scales must preserve exact target frequencies.
- Use nearest MIDI note + per-note pitch bend.
- Never fake polyphonic tuning with global pitch bend.

## MPE defaults

Lower Zone:
- master channel: 1
- member channels: 2..16

Each active note owns one member channel until note off.

## Required MIDI order

On note start:
1. pitch bend
2. optional CC74
3. optional CC11
4. Note On
5. pressure if used

On note end:
- Note Off
- optional bend reset before reuse

## Realtime rules

Inside processBlock:
- no allocation
- no locks
- no file I/O
- no UI calls
- no heavy logging

Use:
- atomics
- lock-free queues
- preallocated arrays

## Review focus

Check:
- MPE channel ownership
- pitch bend correctness
- spectral frequency conversion
- stuck note prevention
- panic/reset behavior
- VST3 MIDI output support
- Audio Only / MIDI Only / Audio + MIDI separation
