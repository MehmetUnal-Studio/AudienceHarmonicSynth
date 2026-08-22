# Cosmic Microwave 2.2 WIKI

> **A single-touch, zone-oriented OSC-to-MIDI router for audience interaction.**

| Field | Value |
|---|---|
| Product | Cosmic Microwave (formerly SpektraSynth) |
| CMake project/target | `AudienceHarmonicSynth` |
| Version | 2.2.0 |
| Formats | VST3 + Standalone |
| Framework | JUCE 8.0.4, C++17, CMake 3.22+ |
| Runtime role | MIDI-only OSC router with a silent mono/stereo instrument output shell |
| Document date | 2026-08-22 |
| Source of truth | Current `CMakeLists.txt` and the source files compiled by `AudienceHarmonicSynth` |

## Contents

1. [Product contract](#1-product-contract)
2. [Products and host identity](#2-products-and-host-identity)
3. [OSC protocol and identity model](#3-osc-protocol-and-identity-model)
4. [Tonal and Atomic pitch mapping](#4-tonal-and-atomic-pitch-mapping)
5. [Crowd Time Field](#5-crowd-time-field)
6. [Normal MIDI and MPE](#6-normal-midi-and-mpe)
7. [Runtime architecture](#7-runtime-architecture)
8. [Threading and realtime boundaries](#8-threading-and-realtime-boundaries)
9. [Editor](#9-editor)
10. [Parameters and state](#10-parameters-and-state)
11. [Build and tests](#11-build-and-tests)
12. [Repository map](#12-repository-map)
13. [Operational limits and upgrade notes](#13-operational-limits-and-upgrade-notes)

---

## 1. Product contract

Cosmic Microwave receives OSC controls from an audience server and emits either Normal
MIDI or MPE. A production server separates zones before they reach the plugin, so each
Cosmic Microwave instance listens to one UDP port and owns one independent MIDI routing
domain.

The flagship does not create sound. It retains a silent instrument bus because:

- Ableton can keep the device in the same instrument position;
- the VST3 category/class contract remains compatible with earlier sessions; and
- the host continues to process the MIDI-producing plugin as an instrument.

Sound is produced by downstream Ableton tracks, an external application, or hardware
receiving Cosmic Microwave's MIDI.

### Core invariants

- Valid source IDs are `0..255`.
- Every source owns one admitted live touch, `finger0`; secondary finger traffic is
  rejected before state and telemetry.
- A source's On and Off always use the same semantic MIDI owner.
- In Normal MIDI / Per source mode, source identity owns the channel.
- In MPE, each active source touch owns one dynamically allocated member channel.
- U/X and V/Y are normalized and map directly to MIDI; there is no intermediate
  sound-generation control layer.
- Pitch System selects either seven 12-TET Tonal maps or a fixed, precomputed Atomic
  catalog of 29 elements and five density modes.
- The port and the OSC zone are separate data. The plugin never infers one from the
  other.
- Grid and Ensemble coalesce movement to the latest U/V snapshot while preserving
  FIFO On/Off lifecycle order.
- Separate instances can share host PPQ or a process-wide monotonic clock; Ensemble
  uses the UDP port only as a lane-phase seed, never as a zone filter.

## 2. Products and host identity

The CMake project builds four MIDI-oriented products:

| CMake target | Product name | Formats | Role |
|---|---|---|---|
| `AudienceHarmonicSynth` | Cosmic Microwave | VST3 + Standalone | Single-touch Normal MIDI/MPE flagship documented here. |
| `AudienceHarmonicMidi` | Cosmic Microwave MIDI | VST3 + Standalone | MIDI-effect audience generator with scale processing. |
| `AudienceMidiGenerator` | Cosmic Microwave MIDI Generator | VST3 | Ableton-focused MIDI effect with scale correction/remapping. |
| `AudienceMidiDevice` | Cosmic Microwave MIDI Device | Standalone | Compact UDP-to-MIDI application. |

The flagship keeps these compatibility identifiers:

| Identifier | Value |
|---|---|
| Bundle ID | `com.mehmetunal.spektrasynth` |
| Manufacturer code | `Mhmt` |
| Plugin code | `Ahss` |
| CMake target | `AudienceHarmonicSynth` |

The historical identifiers are intentional. `SpektraSynth.vst3` and
`Cosmic Microwave.vst3` must not both remain in the host's scanned VST3 folder because
they resolve to the same class identity.

The auxiliary products use `MidiProcessor`/`MidiEngine` and have a separate UI and
parameter model. Do not assume that a flagship control or finger-routing guarantee is
implemented identically by those targets.

## 3. OSC protocol and identity model

### 3.1 Canonical address

```text
/cs/<zone>/<source>/finger0/<param>
```

| Segment | Accepted value |
|---|---|
| Prefix | `/cs/` (case-insensitive letters) |
| Zone | `A..Z` (case-insensitive) |
| Source | decimal `0..255` |
| Touch | exact lower-case `finger0` |
| Parameter | `u`, `v`, `on`, `off`, or legacy `line` (case-insensitive) |

`OscWireFormat.h` parses the address without constructing an additional per-message
string. `OscBridge` accepts OSC `int32` or `float32` payloads and rejects non-finite
values.

### 3.2 Parameter semantics

| Parameter | Input | Result |
|---|---|---|
| `u` | normally `0..1` | Clamp to `0..1`; update touch X, pitch-map position, and CC74. |
| `v` | normally `0..1` | Clamp to `0..1`; update touch Y, note-on velocity, CC11, and MPE pressure. |
| `on` | numeric | Non-zero activates; zero releases. |
| `off` | empty or finite numeric | Releases; numeric value is ignored. |
| `line` | legacy `0..127` | Divide by 127, clamp, and handle as X. |

OSC bundles are traversed recursively. Malformed addresses and unknown parameters are
ignored.

### 3.3 Port and zone

Typical deployment:

```text
Zone A server stream -> UDP 6060 -> Cosmic Microwave instance 1
Zone B server stream -> UDP 6061 -> Cosmic Microwave instance 2
```

This association lives upstream. `OscBridge` records a 26-bit observed-zone mask from
validated addresses. It does not reject a valid B message merely because the listener
uses port 6060.

`MidiAudienceModel` intentionally ignores the zone when it forms source identity. One
plugin instance is expected to represent one already-separated zone. If two zones with
the same source ID reach one port, they share that source's state; mixed-zone telemetry
therefore signals an upstream wiring error.

### 3.4 Source/touch state

`MidiAudienceModel` owns 256 atomic source snapshots. Each snapshot stores:

- most recently received X and Y;
- a reserved ten-bit mask whose live bridge writes only bit 0; and
- derived active source/touch counts.

The realtime MIDI identity is:

```text
voice_id = source_id * 10 + finger
```

The range is `0..2559`. Zone is deliberately absent from that key.

### 3.5 Shared UDP listener

Instances in one process can attach to a shared receiver for the same port. A shared
port supports at most 16 `OscBridge` clients. The production architecture still uses a
different port per zone instance; sharing exists for safe in-process fan-out and tests,
not for zone separation.

## 4. Tonal and Atomic pitch mapping

Both pitch systems use a fixed-capacity, allocation-free lookup on the audio thread.
`MidiPitchMap` exposes seven 12-TET Tonal maps:

| Scale | Semitone offsets |
|---|---|
| Major | `0, 2, 4, 5, 7, 9, 11` |
| Natural Minor | `0, 2, 3, 5, 7, 8, 10` |
| Pentatonic | `0, 2, 4, 7, 9` |
| Dorian | `0, 2, 3, 5, 7, 9, 10` |
| Lydian | `0, 2, 4, 6, 7, 9, 11` |
| Harmonic Minor | `0, 2, 3, 5, 7, 8, 11` |
| Whole Tone | `0, 2, 4, 6, 8, 10` |

Root MIDI is:

```text
root_midi = (root_octave + 1) * 12 + root_pitch_class
```

Root pitch class, root octave, and Range (`1..6` octaves) apply to either system. New
sessions default to C2 (`36`), Atomic, Helium, Extended, and four octaves. Tonal's
stored default remains Major for use when Tonal is selected.

`AtomicScaleCatalog` contains 29 element spectra spanning Hydrogen (`H`) through Zinc
(`Zn`); Nitrogen is omitted because no matching dataset is present. The developer-only
offline generator converts wavelength ratios relative to each element's longest usable
line into one-octave cents and stores only the compact generated catalog. The heavyweight
spectral source table and builder are not loaded or executed by the flagship runtime.

| Atomic mode | Maximum degrees | Catalog separation |
|---|---:|---:|
| Core | 7 | 80 cents |
| Extended | 12 | 40 cents |
| Microtonal | 24 | 20 cents |
| Scientific | 48 | 10 cents |
| Raw 128 | 128 | no added separation |

These counts are caps; the selected element may produce fewer degrees. `AtomicScaleMap`
repeats its selected one-octave bank for the requested range and holds at most 768
pitch-table entries (`128 degrees x 6 octaves`). It stops safely at MIDI's upper
representable edge.

U/X selects equal-width regions across the table:

```text
step = min(table_size - 1, floor(clamp(x, 0, 1) * table_size))
```

X=0 selects the first step; X=1 selects the final step. Moving inside one region keeps
the pitch and updates CC74. Crossing a boundary changes pitch. A pitch-system or
pitch-map parameter change re-resolves held source touches in bounded batches of at most 256
retriggers per audio block.

Tonal tables contain exact MIDI-note frequencies, so MPE pitch wheel is normally center
(`8192`). Atomic lookup retains both the exact target frequency and its nearest MIDI
note. Normal MIDI sends that nearest semitone without an OSC-touch pitch wheel. MPE
sends the nearest base note plus a per-note bend toward the exact Atomic frequency;
MIDI's 14-bit wheel sets the final representation resolution.

The flagship does not import tuning files. A receiving instrument may independently
retune the MIDI notes it receives, but receiver tuning compounds an Atomic MPE bend.

## 5. Crowd Time Field

`CrowdTimeField` is a pure C++ fixed-capacity scheduler between OSC lifecycle input and
pitch/MIDI rendering. The live bridge admits one touch for each of 256 sources. The
core retains a 2560-slot compatibility reserve, and emits at most 64 output events per
audio block. It performs no allocation, locking, logging, I/O, or message-thread calls
from `process()`.

### 5.1 Modes

| Mode | Scheduler contract |
|---|---|
| Flow | Emit valid On/Off directly at their block offsets. Forward U/V movement through the existing event path. Timing controls are bypassed. |
| Grid | Queue attacks to the base division, select pending identities fairly, and obey attacks-per-step and active limits. Held notes release on ordered Off; an admitted short tap receives a minimum gate. |
| Ensemble | Restrict each source to one deterministic spread lane, apply a fixed gate, and requeue a still-held voice after release. |

New 2.2 instances default to **Ensemble**, **Host**, **1/16**, four attacks per step,
16 active voices, 70% gate, and four spread slots. `AudienceProcessor` caps the
effective Time Field active count at 15 in MPE mode because Lower and Upper zones each
have 15 member channels. Serialized state from schema 3 or earlier receives Flow, so
an existing session retains its direct timing.

### 5.2 Clock resolution

`AudienceProcessor` samples JUCE `PositionInfo` in `processBlock`. Host is primary only
when BPM and PPQ are finite and the host transport is playing. In that case the PPQ
position is the block's beat origin and the host tempo defines sample-to-beat
conversion. Continuous tempo automation is accepted without resetting the domain;
seek, loop, clock-domain, and transport-boundary changes request a safety reset and
canonical held-state rehydration.

If Host is selected but unusable or stopped, the scheduler derives phase from one
process-wide monotonic timestamp and the saved Internal BPM. It continues to run but
reports host clock lock as false. Internal selects that same monotonic timebase
explicitly and reports locked. Because phase is calculated from absolute monotonic
time rather than a per-instance accumulator, multiple instances receive a common
fallback/internal grid.

### 5.3 Admission, lanes, and short taps

At every base grid boundary, admission is limited by both `maxAttacksPerStep` and the
remaining `maxActive` capacity. A rotating voice cursor prevents a low source ID from
permanently winning a busy Grid. Ensemble additionally compares the boundary's active
lane with:

```text
lane = (source_id + hash(udp_port) mod spread_slots) mod spread_slots
```

The UDP-derived seed decorrelates equivalent participant IDs across zone instances.
It does not infer the OSC zone. An Ensemble pending lifetime is at least the complete
`division * spread_slots` lane cycle (and never less than one quarter-note beat), so a
short tap receives one full lane-cycle opportunity before it can expire. Grid pending
work has a one-beat opportunity.

Grid uses the configured gate only for an admitted tap released before its attack; a
held Grid note ends on its ordered Off. Ensemble always uses the configured gate and
requeues a voice that remains held.

### 5.4 Movement coalescing and telemetry

In Grid and Ensemble, `MidiAudienceModel` continues to publish the canonical latest
U/V values but suppresses the per-packet movement FIFO. Attack and `SampleMotion`
requests sample those latest values at grid offsets. Thus redundant phone movement is
coalesced without changing source/touch identity, note ownership, channel mapping, or
pitch selection rules. On/Off input remains an ordered FIFO lifecycle stream and has
priority over best-effort motion output.

The editor exposes Pending, Active, and Merged. Merged is a saturating monitoring count
for scheduled work that was coalesced or expired after missing admission capacity. It
does not emit a CC, Crowd Energy message, or other MIDI event.

## 6. Normal MIDI and MPE

### 6.1 Output modes

| Mode | Behaviour |
|---|---|
| Off | OSC/UI state continues; no generated MIDI and no host MIDI thru. |
| Normal MIDI | Conventional notes/controllers on a fixed or source-owned channel. |
| MPE MIDI | One member channel per active source touch with per-channel expression. |

Host MIDI input is copied unchanged to the output when Normal or MPE output is enabled.
It is not quantized by either pitch system and does not create an audience source
snapshot.

### 6.2 Normal source-to-channel rule

Per source routing is base-1:

```text
channel = positive_mod(source_id - 1, 16) + 1
```

Required examples:

| Source | Channel |
|---:|---:|
| 0 | 16 |
| 1 | 1 |
| 16 | 16 |
| 17 | 1 |
| 32 | 16 |
| 255 | 15 |

Source `0` never maps to Channel 1 in the flagship. Its production mapping is always
Channel 16.

Each source's admitted `finger0` touch uses its Normal MIDI channel. A `16 x 128`
reference-count table keeps a physical channel/note held until its last wrapped source
owner releases.

CC74 and CC11 are channel messages. Sources that wrap to the same channel share their
latest controller state; use MPE for per-source-touch isolation.

### 6.3 MPE zones and allocation

| Zone | Master | Member range |
|---|---:|---|
| Lower | 1 | 2..16 |
| Upper | 16 | 1..15 |

Each zone offers 15 simultaneous member channels. Allocation scans round-robin after
the last assigned channel. A just-freed channel is considered after the other free
channels. If the pool is full, the oldest active voice is released before its channel
is reassigned.

Changing output mode, Normal routing/channel, MPE zone/range/setup state, or destination
causes a safety reset and re-arms active OSC touches under the new configuration.

### 6.4 MPE setup and message order

With Setup enabled, the first required MPE output sends:

1. MPE Configuration Message (RPN 6) on the master channel.
2. Pitch-bend range RPN 0 on every member channel.

Available bend ranges are 2, 12, 24, and 48 semitones; default is 2. The receiver must
match the selected zone and bend range.

MPE Note On order:

```text
Pitch Wheel
CC74             <- U/X
CC11             <- V/Y
Note On          <- velocity from V/Y, clamped to 1..127
Channel Pressure <- V/Y
```

MPE Note Off order is Note Off, Channel Pressure 0, then centered Pitch Wheel. Normal
MIDI sends no pitch wheel for OSC touches and uses channel/note reference counting.

### 6.5 Destinations

The destination list is:

1. Host MIDI Output.
2. `Virtual: Cosmic Microwave <UDP port> Out`.
3. Available system/hardware MIDI outputs.

The host bus always receives the generated stream. Choosing item 2 or 3 additionally
queues the same short MIDI messages to the external endpoint. A message-thread timer
drains that queue at 60 Hz; the audio thread never calls the operating-system MIDI
device directly.

## 7. Runtime architecture

### 7.1 Flagship source boundary

Only these implementation units are compiled into `AudienceHarmonicSynth`:

```text
PluginProcessor.cpp      PluginEditor.cpp
PluginStateMigration.cpp AtomicScaleCatalog.cpp
AtomicScaleMap.cpp       MidiPitchMap.cpp
MpeMidiOutput.cpp        MidiAudienceModel.cpp
CrowdTimeField.cpp       OscFingerRouter.cpp
Simulator.cpp
OscBridge.cpp
```

Files remaining from older product generations are not part of the flagship runtime
unless they appear in this CMake target list.

### 7.2 Data flow

```text
OSC UDP callback                         Simulator / UI thread
       |                                         |
       +----------> MidiAudienceModel <----------+
                           |
                           | source snapshots + touch events
                           v
                   OscFingerRouter FIFO (8192)
                           |
                    audio processBlock
                           |
             CrowdTimeField (Flow / Grid / Ensemble)
                           |
                    Pitch System lookup
                  /                     \
          MidiPitchMap             AtomicScaleMap
                           |
             fixed FingerMidiState[2560]
                           |
                    MpeMidiOutput render
                     /                 \
             host MidiBuffer      external FIFO (8192)
                                      |
                              60 Hz message timer
                                      |
                          virtual/hardware MidiOutput

audio buffer -> cleared silent instrument shell
host MIDI in -> preserved scratch -> unchanged thru when output enabled
```

### 7.3 Main classes

| Class | Responsibility |
|---|---|
| `AudienceProcessor` | APVTS, process lifecycle, pitch configuration, input MIDI thru, OSC touch state, safety resets, host/external MIDI routing, state migration. |
| `CosmicStateMigration` | Schema-aware, bounded restoration through schema 4, including Flow timing for older sessions. |
| `OscBridge` | Shared UDP receiver, wire validation, value decoding/clamping, traffic and zone telemetry. |
| `MidiAudienceModel` | Atomic 256-source UI/control state and single-touch hand-off. |
| `OscFingerRouter` | Fixed 8192-event multi-producer/single-consumer queue with reset-on-overflow recovery. |
| `CrowdTimeField` | Allocation-free host/monotonic clock resolution, pending admission, fair Grid scheduling, port-seeded Ensemble lanes, gates, and telemetry. |
| `MidiPitchMap` | Seven fixed-capacity tonal tables and normalized-X lookup. |
| `AtomicScaleCatalog` | Immutable 29-element x 5-mode generated degree catalog and fixed-map lookup. |
| `AtomicScaleMap` | Fixed 128-degree/768-step Atomic pitch projection with exact-frequency metadata. |
| `MpeMidiOutput` | Normal/MPE note ownership, controllers, RPN/MCM setup, channel allocation, reference counts, safety reset. |
| `Simulator` | Message-thread synthetic sources and random movement. |
| `AudienceEditor` | MIDI-only routing/monitoring UI. |

## 8. Threading and realtime boundaries

| Context | Work | Boundary mechanism |
|---|---|---|
| OSC realtime callback | Validate/decode OSC, update source atomics, enqueue touch events. | Shared-port callback/client lock plus producer spin lock; never the audio thread. |
| Audio processing thread | Copy input MIDI, capture host position, select/re-root a fixed pitch table, drain bounded lifecycle events, run the Time Field, generate sample-offset host MIDI, and enqueue short external messages. | Pre-reserved MIDI buffers, fixed arrays, bounded FIFOs/scheduler; no parsing, catalog generation, or device I/O. |
| Message/UI thread | Editor refresh, simulator, destination changes, state application, external MIDI sending. | Atomics, pending-state lock, processor suspension for destructive route changes. |

Key capacities:

| Resource | Capacity |
|---|---:|
| Sources | 256 |
| Admitted live touches per source | 1 (`finger0`) |
| Reserved semantic MIDI voice states | 2560 (10 slots/source for compatibility) |
| OSC touch event FIFO | 8192 |
| Lifecycle events drained per block | `min(64, max(1, block samples))` |
| Time Field output events per block | 64 |
| Generated note-event scratch | 64 |
| External short-message FIFO | 8192 |
| Pre-reserved MIDI buffer storage | 262144 bytes each |
| Shared clients per UDP port | 16 |
| Atomic degrees per element/mode | 128 maximum |
| Atomic projected pitch steps | 768 maximum |

If the OSC event FIFO fills, `OscFingerRouter` increments its dropped count and requests
a reset. The audio consumer prioritizes that reset, discards stale queued events, and
emits all-off safety messages. This favours lifecycle safety over preserving every
movement update.

Destination change and Panic temporarily suspend processing before immediate external
MIDI device operations. `releaseResources` publishes a pending external reset; the
message timer performs device I/O outside the host's processing callback.

## 9. Editor

The flagship editor is resizable (`1120 x 640` default, `900 x 560` minimum). It has:

- header metrics for active sources, active touches, emitted note count, and occupied
  MPE member channels;
- OSC port/status and canonical-address cards;
- a read-only routing summary and observed-zone display;
- simulator controls;
- a 16-column x 16-row **SOURCE MATRIX** covering all 256 IDs;
- a **TIME FIELD** card for mode, clock, BPM/division, attacks per step, active limit,
  gate, spread, and Pending/Active/Merged status;
- Tonal/Atomic selector; shared root, octave, and range; Tonal scale or Atomic
  element/density controls;
- mode-specific Normal MIDI or MPE controls;
- destination status, Rescan, and Panic.

The activity map's columns are MIDI Channels 1..16. Its rows contain each channel's
wrapped source IDs; source 0 is the final cell in the Channel 16 column. Active-cell
position follows the source's latest X/Y snapshot.

The editor exposes no internal diagnostics panel. Internal MIDI rings still support
tests and processor diagnostic text methods, but they are not part of the 2.2 visible
UI contract.

## 10. Parameters and state

### 10.1 APVTS parameters

| ID | Name | Values | Default |
|---|---|---|---|
| `midiOutputType` | MIDI Format | Off / Normal MIDI / MPE MIDI | Normal MIDI |
| `normalMidiRoutingMode` | Normal MIDI Routing | Single Channel / Per Source 1-16 | Per Source 1-16 |
| `normalMidiChannel` | Normal MIDI Channel | 1..16 | 1 |
| `mpeZone` | MPE Zone | Lower / Upper | Lower |
| `mpePitchBendRange` | MPE Pitch Bend Range | 2 / 12 / 24 / 48 st | 2 st |
| `mpeSendSetupMessages` | MPE Send Setup Messages | Off / On | On |
| `mpePitchMode` | MPE Pitch Mode | Retrigger / Glide | Retrigger |
| `timeMode` | Time Field Mode | Flow / Grid / Ensemble | Ensemble |
| `clockSource` | Time Field Clock | Host / Internal | Host |
| `internalBpm` | Internal BPM | 40..240 | 120 |
| `gridDivision` | Grid Division | 1/4 / 1/8 / 1/16 / 1/32 | 1/16 |
| `maxAttacksPerStep` | Attacks Per Step | 1..16 | 4 |
| `maxActiveVoices` | Maximum Active Voices | 1..16 | 16; effective maximum 15 in MPE |
| `gatePercent` | Gate Length | 5..100% | 70% |
| `temporalSpread` | Temporal Spread | 1 / 2 / 4 / 8 / 16 | 4 |
| `pitchSystem` | Pitch System | Tonal / Atomic | Atomic |
| `scaleRoot` | Root | C..B | C |
| `scaleRootOctave` | Root Octave | 0..6 | 2 |
| `scaleMode` | Scale | seven tonal maps | Major |
| `spectralElement` | Atomic Element | 29 elements, H through Zn | Helium |
| `atomicScaleMode` | Atomic Scale Mode | Core / Extended / Microtonal / Scientific / Raw 128 | Extended |
| `scaleOctaves` | Octave Range | 1..6 | 4 |

### 10.2 Non-parameter state

The APVTS ValueTree also stores:

- `udpPort` (default 6060);
- `midiOutputOption` (default Host MIDI Output); and
- `cosmicMicrowaveSchema` (current schema 4).

`setStateInformation` replaces the APVTS parameter tree, while UDP-port and destination
side effects are deferred to the message timer. Migration preserves released state
contracts:

- adds a missing Normal routing parameter as Single Channel for legacy behaviour;
- converts released schema-0 `normalMidiChannel` values from `1..16` to choice indices
  `0..15`;
- explicitly assigns Tonal to existing schema-2 MIDI-only sessions;
- keeps released 1.x tonal choices among the first seven entries;
- converts a released 1.x spectral Scale choice to Atomic and transfers its
  corresponding element index;
- restores released element-engine sessions as Atomic while retaining the stable
  `spectralElement` and `atomicScaleMode` choices;
- supplies Flow plus safe timing defaults to any state that lacks schema-4 Time Field
  parameters; and
- clamps malformed or non-finite choice state to safe bounds.

## 11. Build and tests

### 11.1 Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
ctest --test-dir build --output-on-failure
```

`AUDIENCE_SYNTH_AUTO_INSTALL` defaults to ON on macOS. Set it OFF for CI/build-only
work. `AUDIENCE_SYNTH_BUILD_TESTS` defaults to ON. macOS flagship bundles are ad-hoc
signed after build.

### 11.2 Flagship artifacts

```text
build/AudienceHarmonicSynth_artefacts/Release/VST3/Cosmic Microwave.vst3
build/AudienceHarmonicSynth_artefacts/Release/Standalone/Cosmic Microwave.app
```

### 11.3 CTest targets

| Test | Coverage |
|---|---|
| `AudienceMidiMappingTests` | Auxiliary `MidiEngine` note/channel/controller behaviour. |
| `AudienceMidiScaleModuleTests` | Auxiliary scale correction, remapping, and note-off pairing. |
| `AudienceMidiPitchTests` | Frequency-to-note/pitch-wheel conversion. |
| `AudienceMpeOutputTests` | MPE setup/order/allocation/stealing, Upper/Lower zones, full 2560 voice-ID range, source mapping, reference counts, resets. |
| `AudienceOscBridgeTests` | Parser boundaries, scaling, bundles, fan-out, telemetry, shared-client limit. |
| `AudienceOscFingerRouterTests` | Ordered fixed-FIFO delivery and reset semantics. |
| `AudienceMidiAudienceModelTests` | Finger masks/counts, boundary IDs, snapshots, source-to-channel mapping including `0 -> 16`. |
| `AudienceMidiPitchMapTests` | All seven tables, X boundaries, clamping, root/range limits, finite frequencies. |
| `AudienceAtomicScaleBuilderTests` | Offline spectral normalization/selection, density caps, and hostile numeric input. |
| `AudienceAtomicScaleMapTests` | Allocation-free fixed map, exact-frequency projection, range bounds, and hostile numeric input. |
| `AudienceAtomicScaleCatalogTests` | 29 x 5 generated catalog integrity, mode caps, metadata, and map parity. |
| `AudienceAtomicMidiIntegrationTests` | Atomic map exact-frequency output through the Normal/MPE renderer. |
| `AudienceCrowdTimeFieldTests` | Flow/Grid/Ensemble timing, host/internal/fallback clocks, fairness, lane seeding, taps, gates, saturation, hostile input, and reset/rehydration. |
| `AudienceCrowdMidiIntegrationTests` | Host-PPQ grid offsets and unchanged source-to-channel ownership through the full timed MIDI path. |
| `AudiencePluginStateMigrationTests` | Released channel/scale representations, schema-2 Tonal preservation, schema-4 Flow compatibility, numeric clamping, and idempotence. |

## 12. Repository map

| Path | Current role |
|---|---|
| `CMakeLists.txt` | Authoritative product source boundaries, dependencies, signing, installation, tests. |
| `Source/PluginProcessor.*` | Flagship processor and state/routing orchestration. |
| `Source/PluginStateMigration.*` | Flagship schema-4 state migration. |
| `Source/PluginEditor.*` | Flagship MIDI-only editor. |
| `Source/CrowdTimeField.*` | Realtime Flow/Grid/Ensemble scheduler and shared clock-domain logic. |
| `Source/MidiAudienceModel.*` | Source/touch state and UI snapshots. |
| `Source/MidiPitchMap.*` | Flagship seven-scale pitch table. |
| `Source/AtomicScaleMap.*` | Fixed-capacity exact-frequency Atomic projection used by the flagship. |
| `Source/AtomicScaleCatalog.*`, `Source/AtomicScaleCatalogData.h` | Immutable generated 29-element/five-mode runtime catalog. |
| `tools/GenerateAtomicScaleCatalog.cpp` | Developer-only catalog generator; not a flagship runtime source. |
| `Source/AtomicScaleBuilder.*`, `Source/ElementSpectralData.*` | Offline generator inputs and tests; not compiled into the flagship runtime. |
| `Source/MpeMidiOutput.*` | Normal MIDI/MPE emission and ownership. |
| `Source/OscBridge.*` | UDP receiver and telemetry. |
| `Source/OscWireFormat.h` | Allocation-free canonical address parser. |
| `Source/OscFingerRouter.*` | Fixed touch-event FIFO; generic finger slots remain internal. |
| `Source/Simulator.*` | Shared simulator. |
| `Source/MidiProcessor.*`, `MidiEngine.*`, `MidiScaleModule.*` | Auxiliary MIDI products, not the flagship pipeline. |
| `Source/AudienceMidiDeviceApp.cpp` | Standalone auxiliary bridge. |
| `Tests/` | Unit/regression harnesses selected by CMake. |
| `docs/manual/` | Current user manual. |

Old research data, media, design files, or implementation units may still exist in an
upgraded checkout. Their presence does not make them a 2.2 product feature. Check the
target's `target_sources` list before documenting or modifying runtime behaviour.

## 13. Operational limits and upgrade notes

### Limits

- One instance has 256 source IDs and one admitted live touch (`finger0`) per source.
- Normal MIDI has 16 channels; wrapped sources share channel controllers.
- MPE has 15 member channels; the oldest active MPE note is stolen when full.
- External/virtual short messages are sent by a 60 Hz message timer, while host MIDI
  stays in the process block's `MidiBuffer`.
- The simulator and live OSC share the same source-ID namespace; use the simulator for
  soundcheck before live traffic or avoid conflicting IDs.
- Timed modes coalesce movement to the latest value, but an extreme lifecycle burst
  can still overflow the fixed event FIFO and trigger a safety reset.
- Merged telemetry is cumulative and diagnostic; it is not a MIDI control output.

### Upgrade to 2.2

1. Back up the old VST3 outside the scanned plugin folder.
2. Install Cosmic Microwave 2.2 and rescan the host.
3. Open a copied Ableton set first.
4. Confirm each instance's UDP port, MIDI protocol, source-routing mode, destination,
   Time Field, clock, MIDI protocol, source-routing mode, destination, Pitch System,
   and Tonal or Atomic map.
5. Add downstream instruments because the flagship no longer creates sound.
6. Test Panic and every receiving channel before connecting the audience server.

New sessions default to Ensemble / Host / 1/16 with four attacks per step, active 16
(15 in MPE), 70% gate, four spread slots, and Atomic / Helium / Extended. Existing
schema-3-or-earlier sessions migrate to Flow. The historical schema-2 Tonal and 1.x
Atomic recovery rules remain active; verify the receiver's MPE bend range before a
performance.

For user workflows, continue with [the manual](docs/manual/README.md).
