# Cosmic Microwave 2.8.0 WIKI

> **A single-touch, zone-oriented OSC-to-MIDI router for audience interaction.**

| Field | Value |
|---|---|
| Product | Cosmic Microwave (formerly SpektraSynth) |
| CMake project/target | `AudienceHarmonicSynth` |
| Version | 2.8.0 |
| Formats | VST3 + Standalone |
| Framework | JUCE 8.0.4, C++17, CMake 3.22+ |
| Runtime role | MIDI-only OSC router with a silent mono/stereo instrument output shell |
| Document date | 2026-08-26 |
| Source of truth | Current `CMakeLists.txt` and the source files compiled by `AudienceHarmonicSynth` |

## Contents

1. [Product contract](#1-product-contract)
2. [Products and host identity](#2-products-and-host-identity)
3. [OSC protocol and identity model](#3-osc-protocol-and-identity-model)
4. [Tonal and Atomic pitch mapping](#4-tonal-and-atomic-pitch-mapping)
5. [Crowd Time Field](#5-crowd-time-field)
6. [Notes Only MIDI](#6-notes-only-midi)
7. [Runtime architecture](#7-runtime-architecture)
8. [Threading and realtime boundaries](#8-threading-and-realtime-boundaries)
9. [Editor](#9-editor)
10. [Parameters and state](#10-parameters-and-state)
11. [Build and tests](#11-build-and-tests)
12. [Repository map](#12-repository-map)
13. [Operational limits and upgrade notes](#13-operational-limits-and-upgrade-notes)

---

## 1. Product contract

Cosmic Microwave receives OSC controls from an audience server and emits Normal
Notes Only MIDI. A production server separates zones before they reach the plugin, so each
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
- An ordinary Off ends the held gesture but does not shorten a MIDI tail that already
  owns a fixed Note Duration deadline. The internal watchdog publishes a distinct
  ordered Cancel, which immediately removes only that source/finger's scheduled tails.
- In Normal MIDI / Per source mode, source identity owns the channel.
- Source Capacity admits a dense per-zone domain of 64, 128, or 256 identities while
  the physical MIDI topology remains exactly 16 channels: 4, 8, or 16 sources per
  channel respectively.
- Every new attack snapshots Note Duration (`2n`, `4n`, `8n`, `16n`, or `32n`) and
  the current valid host BPM, then owns an absolute sample deadline. Attack starts are
  not quantized by this setting and later tempo changes do not rewrite old deadlines.
- Ensemble Same Note selects either backward-compatible **Tie** ownership or a safe
  **Retrigger** (Note Off then Note On) for each admitted identical-pitch pulse. The
  setting is local to Ensemble; Flow and Grid force Tie, and neither choice enables MPE.
- U/X selects pitch. V/Y is sampled only as Note-On velocity (`1..127`); held V motion
  produces no continuous MIDI.
- Participant performance output is Note On/Off only. It never generates CC11, CC74,
  Channel Pressure, Pitch Bend, RPN, MPE setup, or Crowd Macro CC; no LFO gates or
  modulates the message stream.
- Panic is the sole generated-controller exception: one CC123 and one CC120 on each
  MIDI Channel 1..16.
- Pitch System selects either seven 12-TET Tonal maps or a fixed, precomputed Atomic
  catalog of 29 elements and five density modes.
- The port and the OSC zone remain separate validation data; Expected Zone filters the
  zone carried in each OSC address. For a genuinely fresh instance only, the route
  allocator claims the lowest free complete factory pair from `6062/A` through
  `6069/H`; this is route selection, not inference from incoming traffic.
- A retained exclusive OSC bind is the fresh route claim. If A-H is exhausted, the
  instance opens no OSC receiver, virtual MIDI endpoint, or Global Conductor
  registration until the operator frees a route and presses **RETRY AUTO**.
- Every mode coalesces redundant ingress movement to the latest U/V value while a
  separate priority FIFO preserves On/Off lifecycle order. Flow consumes those markers
  as soon as the audio budget permits; Grid and Ensemble sample the canonical snapshot
  on musical boundaries.
- Separate instances can share host PPQ or a process-wide monotonic clock; Ensemble
  uses the UDP port only as a lane-phase seed, never as a zone filter.
- Adaptive crowd policy changes future Grid/Ensemble admission only. It cannot cut an
  existing voice or interfere with ordered Off, watchdog release, or Panic.
- Pressure-aware Safety policy responds to ingress, queue, FIFO, and deadline pressure;
  it can close new admission but never blocks release safety.
- Global Conductor groups coordinate bounded Time Field quotas process-locally; stale
  data always falls back to local policy.

## 2. Products and host identity

The CMake project builds four MIDI-oriented products:

| CMake target | Product name | Formats | Role |
|---|---|---|---|
| `AudienceHarmonicSynth` | Cosmic Microwave | VST3 + Standalone | Single-touch Notes Only MIDI flagship documented here. |
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
| `u` | normally `0..1` | Clamp to `0..1`; update touch X and pitch-map position. |
| `v` | normally `0..1` | Clamp to `0..1`; store Y for the next Note-On velocity; emit no MIDI by itself. |
| `on` | numeric | Non-zero activates; zero releases. |
| `off` | empty or finite numeric | Releases; numeric value is ignored. |
| `line` | legacy `0..127` | Divide by 127, clamp, and handle as X. |

Only immediate OSC bundles are traversed; the traversal is depth-bounded and preserves
the production `u`, `v`, `on` order. A dated bundle or dated nested subtree is ignored
instead of being executed early. Malformed addresses, invalid argument cardinality,
unsupported OSC types, non-finite values, and unknown parameters fail closed.

### 3.3 Port and zone

Typical deployment:

```text
Zone A server stream -> UDP 6062 -> Cosmic Microwave instance 1
Zone B server stream -> UDP 6063 -> Cosmic Microwave instance 2
```

This association lives upstream. `OscBridge` records a 26-bit observed-zone mask from
validated addresses but never derives a zone from `6062`. The schema-9 Expected Zone
choice is an independent admission contract: Any accepts every zone for diagnostics;
`A..Z` rejects and counts an otherwise-valid mismatch before source state and accepted-
traffic telemetry.

`MidiAudienceModel` intentionally ignores the zone when it forms source identity after
admission. One plugin instance represents one already-separated zone. Expected Zone
therefore prevents equal source IDs from another zone merging into this model; it does
not replace the upstream split.

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

### 3.5 UDP ownership policy

Schema-8-and-later sessions request **exclusive** process-local ownership. A second instance
on the same port fails visibly with `OWNERSHIP CONFLICT` and periodically retries;
closing the current owner can recover without recreating the device. Legacy schema-7-
and-earlier state migrates to shared ownership to preserve released behaviour. A shared
receiver has a bounded 16-client capacity, but production still requires a unique port
per zone. Port, zone-filter, or ownership changes form a safety-reset boundary.

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
sessions default to C2 (`36`), Atomic, Zinc, Core, and four octaves. Tonal's
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
the pitch and emits no MIDI. Crossing a boundary starts a new Note On; the old pitch
keeps its immutable Note Duration tail. A pitch-system or
pitch-map parameter change re-resolves held source touches in bounded batches of at most 256
retriggers per audio block.

Tonal tables contain exact MIDI-note frequencies. Atomic lookup retains exact-frequency
metadata for catalog/map analysis, but v2.8.0 performance output always sends the nearest
MIDI semitone. It emits no Pitch Bend or RPN and therefore does not transmit the
element-derived cents offset to the receiver.

The flagship does not import tuning files. A receiving instrument may independently
retune the nearest MIDI notes it receives.

## 5. Crowd Time Field

`CrowdTimeField` is a pure C++ fixed-capacity scheduler between OSC lifecycle input and
pitch/MIDI rendering. The wire model supports 256 sources while the selected Source
Capacity admits one touch for each of 64, 128, or 256 dense source IDs. The
core retains a 2560-slot compatibility reserve, and emits at most 64 output events per
audio block. It performs no allocation, locking, logging, I/O, or message-thread calls
from `process()`.

### 5.1 Modes

| Mode | Scheduler contract |
|---|---|
| Flow | Emit valid On/Off directly at their block offsets. Forward U/V movement through the existing event path. Timing controls are bypassed. |
| Grid | Queue attacks to the base division, select pending identities fairly, and obey attacks-per-step and active limits. Ordered Off ends the semantic scheduled voice; an admitted short tap receives a minimum gate. An existing MIDI tail keeps its deadline. |
| Ensemble | Restrict each source to one deterministic spread lane, apply a fixed semantic gate, and requeue a still-held voice after that gate. Tie retains identical-note ownership; Retrigger safely releases and restarts it at each admitted pulse. Fixed-duration MIDI tails may overlap later pulses. |

New 2.8.0 instances default to **Flow**, **Host**, **1/32**, 100% gate, **Same Note = Tie**, and the
**Manual Crowd Governor** policy. The Manual policy starts at 16 attacks per step,
16 active voices, and 16 spread slots. Every effective Time Field active count is
bounded by the product maximum of 16. Serialized state from schema 6 or earlier receives Manual Governor
mode, preserving its exact saved timing policy. State from schema 3 or earlier also
receives Flow, retaining direct timing.

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
held Grid semantic voice ends on its ordered Off without truncating an existing MIDI
tail. Ensemble always uses the configured semantic gate and
requeues a voice that remains held.

### 5.4 Movement coalescing and telemetry

In Grid and Ensemble, `MidiAudienceModel` continues to publish the canonical latest
U/V values but suppresses the per-packet movement FIFO. Attack and `SampleMotion`
requests sample those latest values at grid offsets. Thus redundant phone movement is
coalesced without changing source/touch identity, note ownership, channel mapping, or
pitch selection rules. On/Off input remains an ordered FIFO lifecycle stream and has
priority over best-effort motion output.

The editor exposes Pending, Active, and Merged. Merged is a saturating monitoring count
for pending work whose admission window elapsed. Held intent is renewed, while released
short taps can expire. It is not packet loss and does not emit a CC, Crowd Energy
message, or other MIDI event.

### 5.5 Adaptive Crowd Governor

One **Manual / Adaptive** switch selects the policy. Adaptive observes:

```text
density = max(currently held sources,
              unique live sources active during the previous 8 seconds)
```

It maps that bounded `0..256` density to the following inclusive bands:

| Sources | Attacks / step | Spread / steps | Active voices |
|---:|---:|---:|---:|
| 0-8 | 4 | 1 | 8 |
| 9-24 | 4 | 2 | 10 |
| 25-64 | 3 | 4 | 12 |
| 65-128 | 2 | 8 | 14 |
| 129-256 | 2 | 16 | 16 |

The final active recommendation remains bounded to 16. Density has a fast
rise and slow fall, plus promotion/demotion holds and 20% downward hysteresis, so brief
dropouts and boundary jitter do not repeatedly switch profiles.

Adaptive operates only in Grid and Ensemble. Flow keeps its direct path and reports the
Governor as bypassed. Profile changes are soft admission-policy updates: lowering the
active limit does not release a voice that already sounds, and spread changes retain
pending scheduling opportunities. Ordered Off, the three-second live-touch watchdog,
and Panic always retain authority. Gate remains the saved Manual setting. Adaptive
also leaves all three Manual policy values untouched, so returning to Manual restores
their saved or automated values exactly.

### 5.6 Pressure-aware Safety Governor

`PressureAwareSafetyGovernor` is distinct from `AdaptiveCrowdGovernor`. It consumes a
fixed scalar snapshot of validated ingress events/second, lifecycle-queue pressure,
motion-drop delta, Time Field pending pressure, external FIFO pressure/oldest age, and
audio process deadline ratio. Any invalid clock or numeric signal fails closed.

| State | Motion divisor | Attack ceiling | Active ceiling | Minimum spread | Admission |
|---|---:|---:|---:|---:|---|
| NORMAL | 1 | 16 | 16 | 1 | open |
| HIGH | 2 | 8 | 12 | 2 | open |
| CRITICAL | 4 | 2 | 8 | 4 | open |
| EMERGENCY | 8 | 1 | 4 | 8 | closed |

Escalation is immediate. Recovery uses 15% hysteresis, 2/3/5-second holds, and at most
one-state demotion per completed hold. Latest-value motion coalescing preserves current
positions while thinning redundant updates. Flow retains direct semantics in NORMAL
but obeys safety ceilings under pressure. Releases, watchdog actions, and Panic are
never closed by attack admission.

### 5.7 Global Conductor

`GlobalConductorHub` is a fixed 16-slot, process-local coordinator with four isolated
groups. Off/Leader/Follower roles publish scalar density and budget data only. Within a
group the lowest UDP-port leader wins deterministically; every 100 ms it allocates the
leader's global attack (`1..64`) and voice (`1..128`) budgets fairly across fresh zone
members, using density weighting and deterministic rotation for scarce units.

Audio readers use one coherent atomic policy snapshot. In-flight, stale, failed, or
missing publications never block: after the 1500 ms freshness window, or immediately
after registration loss, a member returns to its local Time Field quotas. The hub
coordinates instances in the same loaded plugin module/process only; it is not a UDP
or cross-machine conductor.

## 6. Notes Only MIDI

### 6.1 Output modes and message contract

| Mode | Behaviour |
|---|---|
| Off | OSC/UI state continues; no generated MIDI and no host MIDI thru. |
| Notes Only | Participant output contains Note On and Note Off only, on a fixed or source-owned Normal MIDI channel. |

U/X selects the mapped pitch. V/Y is sampled when a Note On is created and maps as:

```text
velocity = clamp(round(clamp(v, 0, 1) * 127), 1, 127)
```

A held V update changes stored state for the next attack or pitch retrigger, but emits
no MIDI by itself. A held U update inside the same mapped-note region is also silent;
crossing a mapped-note boundary creates a new Note On while the previous pitch keeps
its own fixed-duration tail until its stored sample deadline.

Cosmic-generated participant traffic never includes CC11, CC74, CC20-23, Channel
Pressure, Pitch Bend, RPN, MPE setup, or Crowd Macro CC. Panic is deliberately outside
the performance-message contract and emits only CC123 plus CC120 once on each Channel
1..16. When MIDI output is enabled, host MIDI thru is restricted to incoming Note On
and Note Off messages; incoming controllers are not copied into Cosmic's output.

### 6.2 Source-to-channel rule

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
reference-count table keeps a physical channel/note held until the last scheduled
owner reaches its deadline or is explicitly cancelled.

Source Capacity never changes the formula or creates extra MIDI channels:

| Source Capacity | Admitted IDs | MIDI channels | Sources/channel |
|---:|---:|---:|---:|
| 64 | `0..63` | 16 | 4 |
| 128 | `0..127` | 16 | 8 |
| 256 | `0..255` | 16 | 16 |

Fresh instances and factory presets use 64. Schema-9-or-earlier projects that lack the
new capacity node migrate to 256 so their former implicit source range is preserved.
Out-of-capacity IDs are dropped and counted rather than wrapped. Shrinking capacity
retires upper identities deterministically and performs bounded note cleanup;
expansion never remaps an already admitted source.

Sources that wrap onto the same channel do not share an expression controller because
v2.8 emits none. Equal-pitch overlap still relies on the reference-count table and the
receiving instrument's repeated-note behaviour.

Changing output mode/path, source routing/channel, destination, Expected Zone,
ownership policy, UDP port, or pitch map causes a safety reset and re-arms active OSC
touches only under the new configuration.

### 6.3 Note Duration and ownership

The five Note Duration choices are independent of Time Field attack quantisation:

| Choice | Quarter-note units | 120 BPM |
|---:|---:|---:|
| 2n | 2 | 1000 ms |
| 4n | 1 | 500 ms |
| 8n | 0.5 | 250 ms |
| 16n | 0.25 | 125 ms |
| 32n | 0.125 | 62.5 ms |

Fresh state and factory presets use 16n. Each Note On snapshots the valid host BPM;
when that is unavailable it uses the saved Internal BPM. Its deadline is sample-based
and remains unchanged by later BPM or duration automation. The central scheduler is
fixed at 4096 semantic tails, advances on every audio block including silent blocks,
and applies deterministic oldest stealing at its global, per-channel, and per-source
hard limits. With Same Note = Tie, a same-source/same-channel/same-note pulse coalesces
and extends one ownership. Ensemble Retrigger instead releases that ownership
immediately and starts a new Note On at each admitted pulse. It is a Notes Only
articulation policy, not MPE; Flow and Grid force Tie. A pitch change may leave the old
tail alive beside the new one.

Ordinary source Off does not cut a musical tail early. `CancelVoice` is the internal
safety operation used by the three-second live watchdog; it closes only that semantic
voice immediately. Panic, route/transport reset, and capacity-shrink cleanup can also
end tails immediately. `CancelVoice` is not an OSC address or a server message type.

### 6.4 Recommended Omnisphere 2 x 8 layout

The show template splits sixteen receiver channels across two Omnisphere Multi
instances:

```text
Ch 1..8  -> OMNI1 parts 1..8
Ch 9..16 -> OMNI2 parts 1..8
```

This is two Omnisphere instances with eight parts each, not sixteen instances and not
one altered Cosmic MIDI domain. At capacity 64/128/256, each part receives 4/8/16
source identities. The split is a downstream CPU/layout choice; host parallelism is
not guaranteed and depends on patch/effect cost, buffer size, routing and hardware.
The complete multi-zone Live set must pass a show-machine CPU/dropout soak.

### 6.5 Destinations

The destination list is:

1. Host MIDI Output.
2. `Virtual: Cosmic Microwave <UDP port> Out`.
3. Available system/hardware MIDI outputs.

`midiOutputPath` is the authority above that list: Host Only publishes only to the host
bus, External Only queues only to item 2/3 and clears the host `MidiBuffer` fail-closed,
and Mirror deliberately publishes to both. A bounded high-resolution sender drains
external short messages; the audio thread never calls the operating-system MIDI device
directly. New sessions default to External Only with the port-named virtual endpoint;
schema-7-and-earlier state migrates to Mirror for compatibility.

### 6.6 Legacy state compatibility

The implementation class and established APVTS IDs still use historical names such as
`MpeMidiOutput`, `mpeZone`, `mpePitchBendRange`, `mpeSendSetupMessages`,
`mpePitchMode`, and `crowdMacro*`. They are retained only so older Ableton projects and
automation lanes can be recalled without corrupting unrelated state. They are inert at
runtime and are not v2.8 product controls. Schema-9 migration converts a former MPE
`midiOutputType` value to Notes Only/Per Source and forces `crowdMacrosEnabled` off.
Neither automation nor legacy state can reactivate MPE or controller output.

Schema 10 adds Note Duration and Source Capacity without deleting that historical
schema-9 step. Missing duration migrates to 16n. A schema-9-or-earlier project missing
capacity migrates to 256; a partial schema-10 state missing capacity receives the fresh
64-source default. Schema 11 adds Ensemble Same Note; every older or partial state
missing it receives Tie, and malformed/out-of-range values also fail safely to Tie.
Every current save is stamped schema 11.

## 7. Runtime architecture

### 7.1 Flagship source boundary

Only these implementation units are compiled into `AudienceHarmonicSynth`:

```text
PluginProcessor.cpp      PluginEditor.cpp
PluginStateMigration.cpp AtomicScaleCatalog.cpp
AtomicScaleMap.cpp       MidiPitchMap.cpp
MpeMidiOutput.cpp        MidiAudienceModel.cpp
AdaptiveCrowdGovernor.cpp PressureAwareSafetyGovernor.cpp
CrowdTimeField.cpp        CrowdExpressionMacros.cpp
GlobalConductorHub.cpp
OscFingerRouter.cpp
Simulator.cpp
OscBridge.cpp
```

Files remaining from older product generations are not part of the flagship runtime
unless they appear in this CMake target list.

### 7.2 Data flow

```text
OSC UDP callback                         Simulator / UI thread
       |                                         |
       +-> expected-zone filter -> MidiAudienceModel <---+
                           |
                           | source snapshots + touch events
                           v
             lifecycle FIFO (8192) + latest U/V FIFO (8192)
                           |
                    audio processBlock
                           |
          PressureAwareSafetyGovernor (pressure ceilings)
                           |
          AdaptiveCrowdGovernor (Grid/Ensemble policy)
                           |
             CrowdTimeField (Flow / Grid / Ensemble)
                           |
                    Pitch System lookup
                  /                     \
          MidiPitchMap             AtomicScaleMap
                           |
             fixed FingerMidiState[2560]
                           |
          legacy-named MpeMidiOutput Notes Only render
                     /                 \
       Host Only/Mirror       External Only/Mirror
             host MidiBuffer      external FIFO (16384)
                                      |
                           2 ms high-resolution sender
                                      |
                          virtual/hardware MidiOutput

audio buffer -> cleared silent instrument shell
host MIDI in -> Note On/Off-only thru when output enabled, then routed by the same
                Host Only / External Only / Mirror policy

GlobalConductorHub (16 slots / four process-local groups / 10 Hz quotas)
external tools/cosmic-chaos-lab.mjs (proxy/capture/replay/generate; never audio thread)
```

### 7.3 Main classes

| Class | Responsibility |
|---|---|
| `AudienceProcessor` | APVTS, process lifecycle, pitch configuration, input MIDI thru, OSC touch state, safety resets, host/external MIDI routing, state migration. |
| `CosmicStateMigration` | Schema-aware, bounded restoration through schema 11, retaining the historical schema-9 Notes Only coercion, schema-10 duration/capacity defaults, and schema-11 Tie fallback. |
| `OscBridge` | Shared/exclusive UDP receiver, wire validation, Expected Zone admission, value decoding/clamping, ownership/traffic/zone telemetry. |
| `MidiAudienceModel` | Atomic 256-source UI/control state, selected-capacity admission and three-second live-touch Cancel watchdog. |
| `OscFingerRouter` | Separate fixed lifecycle and latest-motion queues; lifecycle-first On/Off/Cancel draining, U/V coalescing, epochs, and reset-on-lifecycle-overflow recovery. |
| `CrowdTimeField` | Allocation-free host/monotonic clock resolution, pending admission, fair Grid scheduling, port-seeded Ensemble lanes, gates, and telemetry. |
| `AdaptiveCrowdGovernor` | Allocation-free density smoothing, hysteretic band selection, and soft Grid/Ensemble admission recommendations. |
| `PressureAwareSafetyGovernor` | Allocation-free four-state ingress/queue/FIFO/deadline pressure policy with staged recovery. |
| `GlobalConductorHub` | Fixed-slot process-local leader election, group isolation, density publication, and coherent quota allocation. |
| `CrowdExpressionMacros` | Legacy fixed-cost analyzer retained internally; v2.8.0 routing keeps its MIDI emission permanently disabled. |
| `MidiPitchMap` | Seven fixed-capacity tonal tables and normalized-X lookup. |
| `AtomicScaleCatalog` | Immutable 29-element x 5-mode generated degree catalog and fixed-map lookup. |
| `AtomicScaleMap` | Fixed 128-degree/768-step Atomic projection; runtime output selects the nearest MIDI note. |
| `MpeMidiOutput` | Legacy-named Notes Only ownership plus central sample-deadline scheduler, channel/note reference counts, deterministic bounded stealing, CancelVoice, and CC123/CC120 panic reset. MPE/controller paths are unreachable. |
| `Simulator` | Message-thread held test touches plus a stable participant pool with calibrated Human/Dense/Stress Pad-style lifecycle, independent per-source motion, bounded pacing, and optional active-touch movement. |
| `AudienceEditor` | Two-page Perform/Show Console MIDI-only UI, telemetry, Venue Preflight, and parameter attachments. |

## 8. Threading and realtime boundaries

| Context | Work | Boundary mechanism |
|---|---|---|
| OSC realtime callback | Validate/decode OSC, apply Expected Zone, update source atomics, enqueue touch events. | Shared/exclusive-port callback boundary plus producer lock; never the audio thread. |
| Audio processing thread | Filter input MIDI to Note On/Off, sample pressure/conductor policy, drain bounded lifecycle, run Time Field, render Notes Only output, publish host output, and enqueue external output only when selected. | Pre-reserved MIDI buffers, fixed arrays, bounded FIFOs/schedulers, coherent atomics; no parsing, catalog generation, file I/O, or device I/O. |
| Message/UI thread | Editor refresh, simulator/watchdog, route/port/state changes, preflight telemetry, conductor publication, external MIDI sending. | Atomics, pending-state lock, processor suspension for destructive route changes. |
| External Node.js process | Chaos proxy/capture/replay/generator. | Separate process and sockets; no plugin/audio-thread file access. |

Key capacities:

| Resource | Capacity |
|---|---:|
| Physical source IDs | 256 (`0..255`) |
| Selected admitted Source Capacity | 64 / 128 / 256 |
| Admitted live touches per source | 1 (`finger0`) |
| Reserved semantic MIDI voice states | 2560 (10 slots/source for compatibility) |
| Fixed MIDI channels / sources per channel | 16 / 4, 8, or 16 |
| Scheduled Note Duration ownerships | 4096 global / 512 per channel / 16 per source |
| OSC lifecycle FIFO | 8192 |
| OSC latest-motion marker FIFO | 8192 (at most one pending marker per voice/axis/epoch) |
| Lifecycle events drained per block | `min(64, max(1, block samples))` |
| Time Field output events per block | 64 |
| Generated note-event scratch | 64 |
| External short-message FIFO | 16384 |
| Pre-reserved MIDI buffer storage | 262144 bytes each |
| Shared clients per UDP port | 16 |
| Global Conductor instances | 16 total, four isolated groups |
| Global Conductor update/freshness | 100 ms / 1500 ms |
| Atomic degrees per element/mode | 128 maximum |
| Atomic projected pitch steps | 768 maximum |

On/Off lifecycle and U/V motion use separate FIFOs. Lifecycle always drains first;
repeated motion updates one latest-value slot instead of growing an unbounded packet
backlog. If the lifecycle FIFO fills, `OscFingerRouter` requests a canonical-state
reset and the audio consumer emits a bounded all-off sweep before rehydrating held
touches. This favours note safety over preserving every intermediate movement sample.

`MidiAudienceModel` also owns a three-second live-touch watchdog. Valid live U/V refresh
only an already-started touch, explicit On starts it, and explicit Off stops it. If the
upstream phone disappears without Off, the message-thread watchdog publishes one
ordered internal Cancel. The audio path preserves its provenance through Time Field
and immediately removes only that source/finger's scheduled tails. Simulator voices do
not arm this watchdog, and no `/cancel` extension is added to OSC.

Destination change and Panic temporarily suspend processing before immediate external
MIDI device operations. `releaseResources` publishes a pending external reset; the
message timer performs device I/O outside the host's processing callback.

## 9. Editor

The flagship editor is resizable (`1280 x 760` default, `1000 x 650` minimum) with
**PERFORM** and **SHOW CONSOLE** pages. It has:

- a permanent build-derived version label (for example `v2.8.0`) beside the MIDI-only
  product identity;
- header metrics for active sources, active touches, emitted note count, and active
  scheduled notes;
- OSC port/status and canonical-address cards;
- a read-only routing summary and observed-zone display;
- simulator controls;
- a capacity-aware **SOURCE MATRIX** with 16 fixed channel columns and 4, 8, or 16
  rows per column for the selected 64/128/256-source domain;
- a **TIME FIELD** card for mode, clock, BPM/division, attacks per step, active limit,
  Note Duration, Ensemble Same Note Tie/Retrigger, gate, spread, one Manual/Adaptive switch, effective Adaptive values, and
  Pending/Active/Merged status;
- Tonal/Atomic selector; shared root, octave, and range; Tonal scale or Atomic
  element/density controls;
- Notes Only output, fixed/per-source Normal MIDI routing, optional fixed channel, and
  Source Capacity with its derived 4/8/16 sources-per-channel readout;
- destination status, Rescan, and Panic;
- Host Only / External Only / Mirror, Expected Zone, and exclusive ownership controls;
- a Routing Safety **Factory Performance Preset** selector for full Zone A-H recall;
- fresh-route allocation status plus **RETRY AUTO** when all eight factory routes are busy;
- Safety state/reason plus ingress, deadline, FIFO, and queue telemetry;
- an eight-row Venue Preflight with explicit PASS/WARN/FAIL/BYPASS states, including
  the operator-armed Source Quality Ready Gate;
- Global Conductor role, group, budgets, leader, source, and quota readouts;
- a read-only Notes Only policy card confirming no musical controllers/MPE and the
  CC120/123 Panic exception; and
- an explicit hand-off to the external Capture/Replay Chaos Lab CLI.

The activity map's columns are MIDI Channels 1..16. Its rows contain each channel's
wrapped source IDs; source 0 is the final cell in the Channel 16 column. Active-cell
position follows the source's latest X/Y snapshot.

Show Console is operational telemetry, not an unbounded log. It reads bounded atomic
snapshots and does not perform capture, replay, file I/O, or OS MIDI I/O itself.

### 9.1 Source Quality Controller and Ready Gate

**START 64 CHECK** starts a new, runtime-only evidence epoch for the currently selected
64/128/256 Source Capacity. The default state is BYPASS and the arm/result state is not
stored in APVTS or host presets. WARMING closes only the final
`attackAdmissionOpen` condition; Off, watchdog Cancel, Panic, existing voices, timing,
pitch mapping, MIDI channels, and Notes Only policy are unchanged.

At capacity 64, the proof domain is exactly `0..63`; the corresponding exact domains
for the other choices are `0..127` and `0..255`. Each accepted live OSC source must
produce finite U, V, and On-1 after START, remain actively held, and keep both its U
and V heartbeat fresh throughout one clean two-second pre-ready hold. The two axes are
timed independently, so repeated U cannot make a stale V appear healthy. The plug-in's
1.2-second heartbeat ceiling is a receiver tolerance for transport jitter; the server
must still send both axes at least once per 900 ms.

The clean hold also requires combined U/V motion at or below 50 events/s per source and
1,200 events/s in aggregate for the instance. Duplicate On, orphan Off, watchdog
Cancel, capacity drops, motion drops, and lifecycle drops are counted from the epoch
baseline. Motion and lifecycle loss are reported separately: capacity or lifecycle
loss is a hard latched fault, while motion loss blocks the clean epoch without being
misreported as lifecycle loss. Soft degradation after a latched READY remains audible
and visible.

Only `setLiveFinger*` traffic feeds the census, and internal simulator population must
be zero before the result can become READY. The resulting coverage is a local accepted
OSC **signal census**, not a connected-phone roster or a production-path soak. Server
ownership, allocator uniqueness, connection health, the frozen route manifest, and a
separate 60-second production soak with average/P95 traffic evidence remain mandatory.

### 9.2 Factory performance presets and state authority

The Show Console > Routing Safety preset selector applies a complete performance
baseline. It is not merely a port shortcut:

| Preset | UDP | Expected Zone | Conductor role |
|---|---:|---|---|
| Zone A | 6062 | A | Group 1 Leader |
| Zone B | 6063 | B | Group 1 Follower |
| Zone C | 6064 | C | Group 1 Follower |
| Zone D | 6065 | D | Group 1 Follower |
| Zone E | 6066 | E | Group 1 Follower |
| Zone F | 6067 | F | Group 1 Follower |
| Zone G | 6068 | G | Group 1 Follower |
| Zone H | 6069 | H | Group 1 Follower |

The common recall is Notes Only / Per source 1-16, External Only with the matching
`Cosmic Microwave <port> Out` virtual endpoint, Flow / Host / 1/32, Manual attack 16 /
active 16 / gate 100% / spread 16, Note Duration 16n / Same Note Tie,
Atomic / Zinc / Core at C2 across four octaves,
exclusive ownership, Group 1 budgets 16/16, Safety Governor Off, and legacy Crowd
Macro output Off. Factory Safety Off is intentional and therefore remains a Venue
Preflight blocker until the operator enables it.

A genuinely fresh instance uses the same table as an atomic allocation pool. It claims
the lowest free retained exclusive route—A first, then B through H—and publishes the
matching Expected Zone, virtual endpoint, and Leader/Follower role only after the OSC
bind succeeds. It never wraps or silently shares an occupied route. When all eight are
busy, the instance is fail-closed and waits for an explicit **RETRY AUTO** after a route
has been released; it does not continuously rescan in the background.

Preset recall runs only from the message/UI thread. It sends Panic, clears ephemeral
simulator/live cards, restores the Human simulator profile, and then applies the new
UDP/MIDI identity. The selector reports **CUSTOM / SAVED PROJECT STATE** whenever the
live values do not exactly match one factory performance preset.

Recall uses a latest-wins queue of complete `ValueTree` snapshots, so a state request
made reentrantly by an APVTS/host listener cannot leave mixed parameters and routing.
No state lock is held across parameter callbacks. APVTS completion and external-route
readiness use separate generations: Host MIDI can complete in a headless/offline
restore, while OSC and external MIDI remain fail-closed until their message-thread
route is ready.

Normal host state restoration remains authoritative: opening an Ableton set does not
automatically overlay a factory performance preset or run fresh auto-assignment.
Complete saved values are preserved exactly, including an occupied route; a conflict
fails closed on that saved route instead of shifting the instance. A direct route edit
or explicit factory-preset recall likewise disables fresh assignment and wins exactly.
A partial legacy blob uses schema-specific compatibility defaults; only missing root
UDP and destination metadata falls back to the Zone A `6062` virtual route.

## 10. Parameters and state

### 10.1 APVTS parameters

| ID | Name | Values | Default |
|---|---|---|---|
| `midiOutputType` | MIDI Format | Off / Notes Only | Notes Only |
| `midiOutputPath` | MIDI Output Path | Host Only / External Only / Mirror | External Only |
| `expectedZone` | Expected OSC Zone | Any / A..Z | A |
| `exclusiveUdpPort` | Exclusive UDP Port | Off / On | On |
| `safetyGovernorEnabled` | Safety Governor | Off / On | Off |
| `normalMidiRoutingMode` | Normal MIDI Routing | Single Channel / Per Source 1-16 | Per Source 1-16 |
| `normalMidiChannel` | Normal MIDI Channel | 1..16 | 1 |
| `timeMode` | Time Field Mode | Flow / Grid / Ensemble | Flow |
| `clockSource` | Time Field Clock | Host / Internal | Host |
| `internalBpm` | Internal BPM | 40..240 | 120 |
| `gridDivision` | Grid Division | 1/4 / 1/8 / 1/16 / 1/32 | 1/32 |
| `maxAttacksPerStep` | Attacks Per Step | 1..16 | 16 |
| `maxActiveVoices` | Maximum Active Voices | 1..16 | 16 |
| `gatePercent` | Gate Length | 5..100% | 100% |
| `temporalSpread` | Temporal Spread | 1 / 2 / 4 / 8 / 16 | 16 |
| `crowdGovernorEnabled` | Adaptive Crowd Governor | Manual / Adaptive | Manual |
| `noteDuration` | Note Duration | 2n / 4n / 8n / 16n / 32n | 16n |
| `ensembleSameNoteMode` | Ensemble Same Note | Tie / Retrigger | Tie |
| `sourceCapacity` | Source Capacity | 64 / 128 / 256 | 64 |
| `conductorRole` | Global Conductor Role | Off / Leader / Follower | Leader |
| `conductorGroup` | Global Conductor Group | 1..4 | 1 |
| `conductorAttackBudget` | Conductor Attack Budget | 1..64 | 16 |
| `conductorVoiceBudget` | Conductor Voice Budget | 1..128 | 16 |
| `pitchSystem` | Pitch System | Tonal / Atomic | Atomic |
| `scaleRoot` | Root | C..B | C |
| `scaleRootOctave` | Root Octave | 0..6 | 2 |
| `scaleMode` | Scale | seven tonal maps | Major |
| `spectralElement` | Atomic Element | 29 elements, H through Zn | Zinc |
| `atomicScaleMode` | Atomic Scale Mode | Core / Extended / Microtonal / Scientific / Raw 128 | Core |
| `scaleOctaves` | Octave Range | 1..6 | 4 |

The following established IDs remain serialized only for backward-compatible state and
automation lookup: `mpeZone`, `mpePitchBendRange`, `mpeSendSetupMessages`,
`mpePitchMode`, `crowdMacrosEnabled`, `crowdMacroChannel`,
`crowdMacroDensityCc`, `crowdMacroCentroidXCc`, `crowdMacroCentroidYCc`,
`crowdMacroMotionCc`, and `crowdMacroRate`. They are hidden/inert compatibility data;
they cannot alter v2.8 MIDI output.

### 10.2 Non-parameter state

The APVTS ValueTree also stores:

- `udpPort` (schema/fallback default 6062; fresh runtime allocation claims 6062-6069);
- `midiOutputOption` (schema/fallback default `Virtual: Cosmic Microwave 6062 Out`;
  fresh runtime selection follows the claimed port); and
- `cosmicMicrowaveSchema` (current schema 11).

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
  parameters;
- accepts schema-5 input and discards its retired experimental fields;
- assigns Manual Governor mode to every schema-6-or-earlier session without changing
  its saved attack, active-limit, spread, gate, clock, or timing mode values;
- gives schema-7-and-earlier state Mirror output, Expected Zone Any, shared UDP
  ownership, Safety Governor Off, Conductor Off, and inert Crowd Macro state Off, preserving the
  released routing/admission contract;
- preserves schema-8 routing defaults while schema 9 coerces every former MPE output
  selection to Notes Only/Per Source, forces legacy Crowd Macro enable state Off, and
  retains those historical migration rules;
- schema 10 adds Note Duration and Source Capacity: missing duration becomes 16n,
  schema-9-or-earlier state missing capacity keeps the former 256-source domain, while
  partial current state receives the fresh 64-source default;
- schema 11 adds Ensemble Same Note: missing and hostile values become Tie; and
- clamps other malformed or non-finite choice state to safe bounds.

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
| `AudienceMidiPitchTests` | Frequency-to-nearest-note and retained bend metadata utility; flagship output emits no Pitch Bend. |
| `AudienceMidiOutputTests` | Exact five-duration deadlines, BPM snapshots, Tie coalescing, hard Retrigger articulation, pitch tails, same-note ownership, bounded steal/overflow, CancelVoice, forbidden-controller scan, and CC123/CC120-only Panic. |
| `AudienceOscBridgeTests` | Parser boundaries, scaling, bundles, fan-out, telemetry, shared-client limit. |
| `AudienceOscFingerRouterTests` | Ordered fixed-FIFO delivery and reset semantics. |
| `AudienceMidiAudienceModelTests` | Finger masks/counts, boundary IDs, snapshots, source-to-channel mapping including `0 -> 16`. |
| `AudienceSimulatorTests` | Calibrated Human/Dense/Stress lifecycle and motion, deterministic per-source RNG, population invariance, 256-source pacing, event order, capacity, and allocation-free ticks. |
| `AudienceMidiPitchMapTests` | All seven tables, X boundaries, clamping, root/range limits, finite frequencies. |
| `AudienceAtomicScaleBuilderTests` | Offline spectral normalization/selection, density caps, and hostile numeric input. |
| `AudienceAtomicScaleMapTests` | Allocation-free fixed map, exact-frequency projection, range bounds, and hostile numeric input. |
| `AudienceAtomicScaleCatalogTests` | 29 x 5 generated catalog integrity, mode caps, metadata, and map parity. |
| `AudienceAtomicMidiIntegrationTests` | Atomic map nearest-note output through the Notes Only renderer, with no Pitch Bend. |
| `AudienceAdaptiveCrowdGovernorTests` | Exact five-band policy, fast-rise/slow-fall smoothing, hysteresis, active limit 16, hostile input, reset, and allocation-free updates. |
| `AudienceCrowdTimeFieldTests` | Flow/Grid/Ensemble timing, host/internal/fallback clocks, fairness, lane seeding, taps, gates, saturation, hostile input, and reset/rehydration. |
| `AudienceCrowdMidiIntegrationTests` | Host-PPQ grid offsets, fixed tails, source/channel ownership, and watchdog Cancel through the full timed MIDI path. |
| `AudiencePressureAwareSafetyGovernorTests` | Four exact safety profiles, all pressure reasons, hostile input, hysteresis/holds, reset, and allocation-free update. |
| `AudienceGlobalConductorHubTests` | Registration/lifetime, group isolation, leader election, fair quotas, stale fallback, and coherent lock-free reads. |
| `AudienceCrowdExpressionMacrosTests` | Legacy analyzer regression only; product routing remains inert and emits no Crowd Macro CC. |
| `AudienceChaosLabTests` | External CLI mapping/chaos/capture-replay helpers and bounded deterministic policies under Node's test runner. |
| `AudiencePluginStateMigrationTests` | Released channel/scale representations, schema-2 Tonal, schema-4 Flow, schema-5 cleanup, schema-6 Manual, schema-7/8 routing compatibility, historical schema-9 Notes Only coercion, schema-10 duration/capacity defaults, schema-11 Tie migration, inert legacy state, hostile-choice handling, and idempotence. |

## 12. Repository map

| Path | Current role |
|---|---|
| `CMakeLists.txt` | Authoritative product source boundaries, dependencies, signing, installation, tests. |
| `Source/PluginProcessor.*` | Flagship processor and state/routing orchestration. |
| `Source/PluginStateMigration.*` | Flagship schema-11 state migration, including retained schema-9 and schema-10 compatibility. |
| `Source/PluginEditor.*` | Flagship MIDI-only editor. |
| `Source/AdaptiveCrowdGovernor.*` | Realtime-safe crowd-density policy for Grid/Ensemble admission. |
| `Source/PressureAwareSafetyGovernor.*` | Realtime-safe system-pressure protection and telemetry policy. |
| `Source/GlobalConductorHub.*` | Process-local group coordination and fair per-zone quotas. |
| `Source/CrowdExpressionMacros.*` | Legacy fixed-cost analyzer retained behind a permanently disabled v2.8.0 MIDI-output gate. |
| `Source/CrowdTimeField.*` | Realtime Flow/Grid/Ensemble scheduler and shared clock-domain logic. |
| `Source/MidiAudienceModel.*` | Source/touch state and UI snapshots. |
| `Source/MidiPitchMap.*` | Flagship seven-scale pitch table. |
| `Source/AtomicScaleMap.*` | Fixed-capacity Atomic projection; Notes Only output uses its nearest MIDI note. |
| `Source/AtomicScaleCatalog.*`, `Source/AtomicScaleCatalogData.h` | Immutable generated 29-element/five-mode runtime catalog. |
| `tools/GenerateAtomicScaleCatalog.cpp` | Developer-only catalog generator; not a flagship runtime source. |
| `Source/AtomicScaleBuilder.*`, `Source/ElementSpectralData.*` | Offline generator inputs and tests; not compiled into the flagship runtime. |
| `Source/MpeMidiOutput.*` | Legacy-named Notes Only emission/ownership and CC123/CC120 panic safety. |
| `Source/OscBridge.*` | UDP receiver and telemetry. |
| `Source/OscWireFormat.h` | Allocation-free canonical address parser. |
| `Source/OscFingerRouter.*` | Fixed touch-event FIFO; generic finger slots remain internal. |
| `Source/Simulator.*` | Shared simulator. |
| `Source/MidiProcessor.*`, `MidiEngine.*`, `MidiScaleModule.*` | Auxiliary MIDI products, not the flagship pipeline. |
| `Source/AudienceMidiDeviceApp.cpp` | Standalone auxiliary bridge. |
| `Tests/` | Unit/regression harnesses selected by CMake. |
| `tools/cosmic-chaos-lab.mjs` | External Node.js proxy/capture/replay/generator; not a plugin runtime source. |
| `docs/chaos-lab.md` | Chaos Lab operator contract and examples. |
| `docs/manual/` | Current user manual. |

Old research data, media, design files, or implementation units may still exist in an
upgraded checkout. Their presence does not make them a 2.8.0 product feature. Check the
target's `target_sources` list before documenting or modifying runtime behaviour.

## 13. Operational limits and upgrade notes

### Limits

- One instance has a physical `0..255` source ceiling and selects an admitted capacity
  of 64, 128, or 256, with one live `finger0` touch per admitted source.
- Notes Only MIDI has 16 channels and the active Time Field limit is 16.
- Wrapped sources share a channel but no participant controller state; performance
  output is Note On/Off only.
- External/virtual short messages are sent by a 2 ms high-resolution sender, while host MIDI
  stays in the process block's `MidiBuffer`.
- The simulator and live OSC share the same source-ID namespace. Use the simulator for
  soundcheck before live traffic and clear it before the server path opens. The editor
  shows an amber `LOCAL + OSC INPUT` warning if both are observed together.
- Every mode coalesces redundant movement to the latest value, but an extreme On/Off
  lifecycle burst can still overflow its fixed priority FIFO and trigger a safety reset.
- Merged telemetry is cumulative admission-lifetime telemetry; held intent is renewed,
  while released short taps can expire. It is not packet loss or a MIDI control output.

### Upgrade to 2.8.0

Install 2.8.0 on a copied show set first. Existing state opens with **Same Note = Tie**,
so its established sound remains unchanged. In Ensemble, choose **Retrigger** only when
you want each admitted identical-pitch pulse to articulate as Note Off then Note On.
Confirm the disabled Same Note control in Flow/Grid, then test held-pitch Tie and
Retrigger behaviour on the actual receiving instrument before show approval.

### Historical upgrade to 2.7.1

1. Back up the old VST3 outside the scanned plugin folder.
2. Install Cosmic Microwave 2.7.1 and rescan the host.
3. Open a copied Ableton set first.
4. Confirm each instance's UDP port, Expected Zone, ownership, MIDI Output Path,
   endpoint, Time Field/clock, Notes Only source routing, Pitch System, and map.
5. Add downstream instruments because the flagship no longer creates sound.
6. Open Show Console, resolve Venue Preflight failures, verify Safety NORMAL and any
   Global Conductor quotas and the Notes Only policy card, then test Panic and every
   receiving channel before connecting the audience server.

Fresh instances claim the lowest free complete A-H route: UDP 6062 / Expected Zone A /
Group 1 Leader first, then UDP 6063-6069 / Zones B-H / Follower. Every route uses
External Only with its matching virtual endpoint, Flow / Host / 1/32 / Note Duration
16n / Same Note Tie with Manual attack 16 / active 16 / gate 100% / spread 16, Source Capacity 64,
Atomic / Zinc / Core, Safety Off, and 16/16
budgets. Exhausting A-H fails closed until **RETRY AUTO**; saved state, direct edits,
and explicit presets are never shifted. Schema-6-or-earlier sessions
migrate with the Governor in Manual;
schema-3-or-earlier sessions additionally migrate to Flow. Schema-5 input remains
compatible and its retired experimental fields are discarded. Schema-7-and-earlier
state preserves Mirror/shared-port/Safety-Off behaviour. New sessions use exclusive
ownership, factory Safety Off, and schema 11. Complete host-restored state stays
authoritative. Partial legacy state uses schema-specific compatibility defaults;
missing root UDP/destination metadata alone falls back to the Zone A `6062` virtual
route. Historical schema 9 still maps legacy MPE sessions to Notes Only/Per Source and
keeps old MPE/Crowd Macro APVTS IDs inert. Schema 10 adds 16n/64 fresh defaults while
missing-capacity schema-9-or-earlier projects retain 256. Schema 11 adds Tie as the
safe default for every missing or hostile Same Note value. The historical schema-2
Tonal and 1.x Atomic recovery rules remain active;
Atomic output is always the nearest MIDI note.

For user workflows, continue with [the manual](docs/manual/README.md).
