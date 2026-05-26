---
name: juce-best-practices
description: Use this skill when implementing or reviewing JUCE/C++ audio plugin code, especially AudioProcessor, processBlock, APVTS, MIDI, MPE, editor/processor boundaries, plugin state, and realtime-safety.
---

# JUCE Best Practices Skill

## Purpose
Use this skill for JUCE/C++ audio plugin implementation and review.

Focus on:
- realtime-safe audio callback code
- AudioProcessor lifecycle
- APVTS parameter/state design
- MIDI/MPE handling
- UI/editor separation
- plugin format compatibility
- DAW-safe behavior

## Hard realtime rules

Inside processBlock or any function reachable from it:

Never:
- allocate memory
- resize vectors or buffers
- lock mutexes
- wait on threads, futures, semaphores, condition variables, or spinlocks
- do file I/O
- do network I/O
- call UI/message-thread code
- call MessageManager
- use juce::String formatting
- log repeatedly
- use DBG in realtime paths
- use exceptions as control flow

Prefer:
- prepareToPlay preallocation
- fixed-size arrays
- pre-sized buffers
- atomics for simple parameter/state sharing
- lock-free queues for event transfer
- immutable snapshots for sample/library data
- SmoothedValue for zipper-noise-prone parameters

## AudioProcessor lifecycle

prepareToPlay:
- allocate and size buffers
- prepare DSP modules
- reset filters, envelopes, delay lines, counters
- store sample rate and max block size
- clear denormals if needed

processBlock:
- do deterministic realtime work only
- clear unused output channels
- avoid heap work
- avoid UI access
- avoid file/library changes
- avoid heavy branching based on UI objects

releaseResources:
- release non-realtime resources safely
- do not assume host calls this reliably before destruction

## APVTS rules

Parameter IDs must be stable.
Changing IDs breaks old patches.

APVTS parameters should:
- have explicit defaults
- be included in getStateInformation / setStateInformation
- use attachments in the editor
- be mirrored into realtime-safe values if heavily used in processBlock

Avoid:
- reading ValueTree directly in processBlock
- using string comparisons in processBlock
- changing parameter layout in a way that breaks old sessions

## Editor / processor boundary

The editor must not own audio-critical state.

UI may:
- display state
- send parameter changes
- send commands through safe queues

UI must not:
- directly mutate audio engine internals used by processBlock
- hold locks needed by audio thread
- trigger file load on audio thread
- call repaint or message-thread operations from processBlock

## MIDI / MPE rules

MIDI output must be explicit and intentional.

For MPE:
- one active voice owns one member channel
- pitch bend must be sent before Note On
- Note Off must use the same channel as Note On
- channel reuse only after Note Off
- Panic must send explicit Note Offs plus All Notes Off / All Sound Off as cleanup

For spectral/microtonal tuning:
- resolve target frequency once
- convert to nearest MIDI note + pitch bend
- do not use global pitch bend for polyphonic tuning

## VST3 / AU / Standalone checks

Check:
- NEEDS_MIDI_INPUT
- NEEDS_MIDI_OUTPUT
- IS_SYNTH
- plugin bus layout
- producesMidi()
- acceptsMidi()
- isMidiEffect()
- host-specific MIDI output limitations

Do not assume all DAWs route MIDI output from instrument plugins the same way.

## Common JUCE anti-patterns

Flag these immediately:
- AudioBuffer::setSize inside processBlock
- std::vector::push_back / resize inside processBlock
- juce::File operations in realtime path
- juce::Time calls in realtime path
- MessageManager calls in realtime path
- UI component access from processor realtime code
- DBG/logging per block or per sample
- mutex lock around sample library access in audio thread
- sample loading or decoding in processBlock

## Review output format

When reviewing, report:

- PASS / WARNING / FAIL
- file and function name
- exact issue
- why it matters
- suggested fix
- whether it affects:
  - realtime safety
  - audio correctness
  - MIDI/MPE correctness
  - DAW compatibility
  - patch compatibility
