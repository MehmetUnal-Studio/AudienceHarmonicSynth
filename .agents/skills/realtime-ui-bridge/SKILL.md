---
name: realtime-ui-bridge
description: Use this skill when moving information between the audio thread and the UI in JUCE audio plugins. Focus on realtime safety, lock-free communication, snapshots, note visualization, MIDI monitoring, and voice-state display.
---

# Realtime UI Bridge

## Purpose

Safely move information from audio thread code to UI code.

The audio thread owns realtime state.

The UI owns rendering.

The UI must observe state.

The UI must never drive realtime processing.

## Ownership model

Audio thread owns:
- note state
- voice state
- MIDI state
- MPE state
- active channels
- pitch information
- spectral activity
- engine state used for sound generation

UI owns:
- drawing
- animation
- visual representation
- interaction widgets
- layout

Never blur these responsibilities.

## Hard realtime rules

Inside processBlock or any audio callback reachable path:

Never:
- repaint()
- triggerAsyncUpdate()
- MessageManager calls
- Component access
- Label access
- Slider access
- Button access
- UI state mutation
- String formatting for display
- logging
- file I/O
- allocations
- locks

The audio thread must never directly manipulate UI objects.

## Approved communication patterns

Use:
- atomics
- lock-free queues
- ring buffers
- immutable snapshots
- double-buffered state
- preallocated event buffers

Preferred pattern:

Audio Thread
    -> update realtime state
    -> publish snapshot

UI Timer
    -> read snapshot
    -> update visuals

## Snapshot design

A snapshot should contain only the data required for visualization.

Good:

struct ActiveNoteSnapshot
{
    int note;
    float velocity;
    bool active;
};

Bad:

storing Component pointers
storing UI references
storing editor objects

Snapshots should be:
- POD-like
- lightweight
- preallocated if possible

## MIDI visualization

For MIDI monitors:

Audio thread:
- parse MIDI
- push events into lock-free queue

UI:
- consume queue
- display events

Never build tables, strings, or UI rows inside processBlock.

## Note highlighting

For keyboards, spectral lines, and wavelength wheels:

Audio thread:
- update note active state

UI:
- read note active state
- draw highlight

A note highlight must remain visible while the note remains active.

Do not implement note highlighting as a temporary flash unless explicitly requested.

## Sustained notes

Visual state should follow note lifetime.

Expected:

Note On
    -> active highlight

Note held
    -> highlight remains visible

Note Off
    -> highlight removed

The UI must reflect the actual note state, not an arbitrary timeout.

## MPE visualization

Audio thread owns:
- member channel ownership
- pitch bend values
- pressure values
- CC74 values

UI displays:
- channel
- pitch bend
- pressure
- timbre
- note

Do not calculate ownership in the UI.

The UI only visualizes ownership.

## Spectral visualization

For Elemental Spektra:

Audio thread owns:
- active spectral note state
- active spectral line state
- wavelength activity

UI owns:
- spectral line drawing
- wavelength wheel drawing
- color/intensity mapping

The UI should not determine which note is active.

The audio engine already knows.

## Timer rules

Preferred:

UI refresh timer:
30-60 Hz

Avoid:
200+ Hz UI updates

Visual smoothness should come from interpolation, not excessive polling.

## Review checklist

When reviewing UI-related code:

Check:

- Any UI calls from processBlock?
- Any MessageManager calls from realtime code?
- Any locks shared between UI and audio thread?
- Any allocations in visualization path?
- Is note state snapshot-based?
- Is MIDI state snapshot-based?
- Are active notes driven by actual note lifetime?
- Are spectral highlights driven by actual note lifetime?
- Can UI freeze without affecting audio processing?

Flag violations immediately.
