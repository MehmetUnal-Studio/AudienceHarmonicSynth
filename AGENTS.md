# AGENTS.md

## Project
This is a JUCE/C++ audio plugin called SpektraSynth.

The plugin is an audience-driven musical instrument with:
- internal audio synthesis
- sample library playback
- granular synthesis
- Element / Atomic Spectral Synth
- optional MIDI / MPE output

## Hard realtime audio rules
Never do any of the following inside processBlock or any function reachable from the audio callback:
- allocate memory
- resize vectors or buffers
- lock mutexes
- wait on futures/semaphores/threads
- perform file I/O
- perform network I/O
- call UI/message-thread code
- log repeatedly
- use juce::String or heap-heavy formatting
- call MessageManager
- trigger async UI updates directly

Use:
- preallocated buffers
- atomics
- lock-free queues
- immutable snapshots
- double-buffered sample data
- smoothed parameters where needed

## Audio architecture
The internal audio engine must remain intact.

Audio engines:
- Direct Sample Player
- Granular Sample Engine
- Element / Atomic Spectral Synth

Do not remove or replace the internal audio engine when adding MIDI features.

## Sample playback architecture
Sample Library is the source.
Sample Playback Mode decides how it is played:

- Direct Sample Player
- Granular Sample Engine

Do not force every sample library through granular synthesis.

Direct Sample Player must be a real sample playback path, not fake granular playback with large grains.

## MIDI / MPE architecture
MIDI/MPE output is a parallel output layer.

Output modes:
- Audio Only
- MIDI Only
- Audio + MIDI

MIDI output types:
- Off
- Normal MIDI
- MPE MIDI

For Element / Atomic Spectral Scales:
- resolve the exact target frequency first
- convert frequency to nearest MIDI note + pitch bend
- use MPE member channels for independent per-note tuning
- do not fake MPE using global pitch bend

## MPE defaults
Zone (selectable via the mpeZone parameter):
- Lower (default): master channel 1, member channels 2-16
- Upper: master channel 16, member channels 1-15
- pitch bend range: configurable (2/12/24/48), default 2 semitones
  (spectral degrees are emitted as nearest 12-TET note + bend, always <= +/-50 cents)

Each active voice owns one MPE member channel until note off.

Send pitch bend before note on.

On panic:
- send note off for all active notes
- all notes off / all sound off where appropriate
- reset pitch bend to center
- clear MPE channel allocator state

## Spectral scale rule
The audio engine and MIDI/MPE engine must share the same pitch resolver.

Do not duplicate or approximate spectral scale math separately for MIDI.

## Build and validation
After meaningful code changes:
- build the project
- fix compile errors
- report changed files
- report known DAW/plugin-format limitations

## Review expectations
Before risky implementation work, use:
- juce-review
- audio-dsp-review
- vst3-review
- dsp-algorithm-guide when DSP design is involved

## Required workflow before implementation
Before making changes:
- inspect the existing architecture first
- identify the smallest safe change
- do not rewrite large systems unless explicitly asked
- explain the planned files/classes to modify

After making changes:
- build the project
- fix compile errors
- run existing tests if available
- summarize exact changed files
- mention behavior that was intentionally not changed

## Regression protection
Do not break:
- existing presets / patch recall
- existing APVTS parameter IDs
- existing UI attachments
- existing sample loading behavior
- existing Element / Atomic Spectral Scale behavior
- existing Granular Engine behavior
- existing Direct Sample Player behavior
- existing MIDI generator behavior

When adding new parameters:
- choose stable IDs
- provide defaults
- include migration/fallback for old sessions

## Testing checklist
For every audio/MIDI feature change, test:

- plugin builds successfully
- Audio Only mode still produces sound
- MIDI Only mode is silent but emits MIDI
- Audio + MIDI does both
- Panic clears audio and MIDI state
- no stuck notes after mode switch
- no crash when changing presets
- no crash when changing sample library during playback
- no click/pop when switching modes
- spectral scale still produces expected pitch

## Code-change discipline
Prefer:
- small focused patches
- preserving existing public APIs
- adding helper functions/classes only when they reduce risk
- clear comments around realtime-sensitive code

Avoid:
- large unrelated refactors
- renaming parameters
- moving files unnecessarily
- changing UI layout while fixing DSP/MIDI bugs
- changing sound behavior unless requested

After editing documentation-only guidance files:
- show the exact diff
- do not modify any source code files unless the user explicitly requested implementation
