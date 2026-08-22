# Cosmic Microwave

Formerly named **SpektraSynth**.

Version 2.4.0

Cosmic Microwave is a JUCE VST3 and standalone OSC-to-MIDI router for
audience interaction. It receives already-separated zone streams over UDP, keeps each
source's single-touch lifecycle intact, maps normalized movement through either tonal or
element-derived Atomic Scale pitch maps, and sends Normal MIDI or MPE to Ableton, a
virtual MIDI endpoint, or a system MIDI device.

Cosmic Microwave 2.4.0 is behaviourally MIDI-only: it does not generate sound. The VST3
keeps a silent stereo instrument shell, its existing class identity, and its instrument
placement so Ableton sets made with the earlier product can still resolve the device.

## OSC mapping

Each instance listens on one configured UDP port. The upstream server is responsible
for separating zones before they reach the plugin; for example:

| Zone | UDP port | Virtual MIDI endpoint |
|---|---:|---|
| A | `6060` | `Cosmic Microwave 6060 Out` |
| B | `6061` | `Cosmic Microwave 6061 Out` |

The port does not define the zone. The plugin reads the zone from every valid OSC
address and reports mixed-zone traffic, but it does not filter or infer zones.

Production messages are:

```text
/cs/<zone>/<source>/finger0/u     <0..1>     horizontal position
/cs/<zone>/<source>/finger0/v     <0..1>     vertical position
/cs/<zone>/<source>/finger0/on    1          activate touch
/cs/<zone>/<source>/finger0/on    0          release touch
```

Legacy `/off` and `/line 0..127` inputs remain accepted for older patches, but the
live service sends `u`, `v`, and `on` only.

- Zones are `A..Z`.
- Source IDs are `0..255`.
- The live product accepts `finger0` only. Secondary finger tokens are ignored before
  state and telemetry, matching the one-person/one-touch performance model.
- OSC `int32` and `float32` arguments are accepted; non-finite values are ignored.
- `u` and `v` are clamped to `0..1`. Legacy `line` is divided by 127 and clamped.
- Each source owns one ordered touch/note lifecycle.
- Production OSC bundles must use the immediate timetag. Dated bundles are ignored so
  they are never executed early against Ableton's independent musical clock.
- Repeated movement is reduced to its latest U/V value while ordered On/Off traffic is
  kept in a separate priority queue. A movement flood therefore cannot trap a note.
- If an active live touch receives no valid U, V, or On heartbeat for three seconds,
  Cosmic Microwave publishes a synthetic ordered Off. Simulator voices are excluded.

### Direct MIDI controls

There is no sound-engine or macro layer modifying the OSC values:

```text
U/X -> selected Tonal or Atomic pitch + CC74
V/Y -> note-on velocity + CC11 (+ channel pressure in MPE)
```

Controller values are `round(value * 127)`. Note-on velocity is limited to `1..127`.
Horizontal position is divided into equal regions across the selected root, pitch map,
and octave range. New sessions default to the Atomic system with Helium, Extended
density, root C2, and a four-octave range.

## Crowd Time Field

Cosmic Microwave 2.4.0 can turn an asynchronous crowd into a shared rhythmic field
without changing source identity or note ownership:

| Mode | Behaviour |
|---|---|
| **Flow** | Pass lifecycle and movement through directly, preserving the earlier 2.1 timing behaviour. |
| **Grid** | Queue attacks to the next selected musical division, with fair selection, an attacks-per-step limit, and an active-voice limit. Held notes release with their ordered Off; an admitted short tap receives the configured minimum gate. |
| **Ensemble** | Place each source in a deterministic lane across the selected spread, apply a fixed gate, and requeue a still-held source for later pulses. |

New sessions default to **Ensemble / Host / 1/16** with the **Adaptive Crowd
Governor** enabled and a **70% gate**. The Governor adjusts attack admission, active
voice capacity, and temporal spread from audience density; the saved Manual values
remain **4 attacks per step**, **16 active voices**, and a **4-step spread**. MPE has
only 15 member channels, so every effective active limit is capped at 15. Sessions
saved with schema 6 or earlier open in **Manual**, preserving their established Time
Field settings. State saved before schema 4 also receives **Flow**, preserving its
immediate timing rather than silently quantizing an existing performance.

With **Host** selected, a playing host's tempo and PPQ timeline define the grid. If
that clock is missing or the transport is stopped, the scheduler continues from a
process-wide monotonic timebase at the Internal BPM. Selecting **Internal** uses that
common monotonic clock explicitly, so separate instances still share one absolute
time reference.

Grid and Ensemble retain the latest U/V values and sample them at scheduled attack and
grid boundaries instead of forwarding every redundant movement packet. Ordered On/Off
lifecycle events are not packet-round-robined or replaced by movement coalescing. In
Ensemble, the UDP port supplies a stable lane seed so different zone instances do not
all place the same source ID on the same tick. A pending short tap remains eligible for
at least one complete lane cycle. **MERGED** is monitoring only: it counts scheduled
work coalesced or expired under pressure and does not produce a MIDI CC.

### Adaptive Crowd Governor

The Time Field header has one **Manual / Adaptive** switch. Adaptive measures the
larger of the currently held source count and the number of unique live sources seen
in the previous eight seconds, then applies this deterministic policy in Grid and
Ensemble:

| Observed sources | Attacks / step | Spread / steps | Active voices |
|---:|---:|---:|---:|
| 0-8 | 4 | 1 | 8 |
| 9-24 | 4 | 2 | 10 |
| 25-64 | 3 | 4 | 12 |
| 65-128 | 2 | 8 | 14 |
| 129-256 | 2 | 16 | 16 |

Density rises quickly and falls slowly, with transition holds and hysteresis to prevent
rapid switching near a band edge. Policy changes are soft: they affect future admission
only and never cut an existing voice, replace an ordered Off, suppress the three-second
watchdog, or block Panic. Flow bypasses the policy completely. Switching to Manual
restores the saved/automated attack, active-limit, and spread values; Adaptive never
overwrites them.

## Pitch systems

**Tonal** provides seven familiar 12-TET maps: Major, Natural Minor, Pentatonic,
Dorian, Lydian, Harmonic Minor, and Whole Tone.

**Atomic** derives one-octave pitch degrees from 29 stored element emission spectra
spanning Hydrogen through Zinc (Nitrogen is unavailable in the current dataset). Five
density modes expose progressively more detail; their maximum degree counts per octave
are:

| Atomic mode | Maximum degrees |
|---|---:|
| Core | 7 |
| Extended | 12 |
| Microtonal | 24 |
| Scientific | 48 |
| Raw 128 | 128 |

The selected element may contain fewer usable spectral degrees than the mode cap. Root,
root octave, and the `1..6` octave range transpose and repeat either pitch system.
Normal MIDI sends the nearest 12-TET MIDI note for an Atomic target. MPE keeps the
element-derived target frequency and encodes it as the nearest base note plus per-note
pitch bend, subject to MIDI pitch-wheel resolution.

## Source-to-channel routing

Normal MIDI defaults to **Per source 1-16**. Channel ownership is based on source ID,
not packet order:

```text
1 -> Ch 1   2 -> Ch 2   ...   16 -> Ch 16   17 -> Ch 1
0 -> Ch 16
```

All `u`, `v`, `on`, and `off` messages from one source's `finger0` touch use that
source's channel. A second **Single channel** mode sends every source through one
selected channel.

MPE assigns each active source touch to one member channel. Lower zone uses master Channel 1
and members 2-16; Upper uses master Channel 16 and members 1-15. When all 15 member
channels are occupied, the oldest active MPE note is released before its channel is
reused.

## MIDI destinations

The editor offers:

- **Host MIDI Output** - the VST3 MIDI bus.
- **Virtual: Cosmic Microwave <port> Out** - a stable endpoint derived from the
  instance's UDP port.
- Available system or hardware MIDI outputs.

Selecting a virtual or hardware destination does not disable the host bus; the same
MIDI stream remains available to the host. The port-named virtual endpoint is the
recommended route when an Ableton set needs separate receiving tracks for Channels
1-16.

## Ableton layout

For each audience zone:

1. Put one Cosmic Microwave instance on its own track.
2. Apply that zone's UDP port.
3. Select **Normal MIDI** and **Per source 1-16**.
4. Select `Virtual: Cosmic Microwave <port> Out` as the destination.
5. On receiving tracks, choose that endpoint under **MIDI From** and select the
   required channel.
6. Put the sound-producing instruments on those receiving tracks.

Each Cosmic Microwave instance has an independent set of Channels 1-16. For MPE,
route the complete multi-channel stream to one correctly configured MPE receiver
instead of treating its member channels as independent source channels.

## Editor

The MIDI-only editor contains:

- a permanent build-derived **v2.4.0** version label in the header;
- live **SOURCES**, **TOUCHES**, **NOTES**, and **MPE VOICES** metrics;
- an **OSC INPUT** card with port and validated-traffic status;
- a source-routing summary with observed zone letters;
- a simulator for source-count and movement tests;
- a 256-source **SOURCE MATRIX** grouped into 16 MIDI-channel columns;
- a **TIME FIELD** card for Flow/Grid/Ensemble timing, host/internal clocking,
  Manual/Adaptive crowd policy, density limits, gate, spread, and live
  Pending/Active/Merged telemetry;
- a Tonal/Atomic pitch system with element and density selection;
- Normal MIDI and MPE routing controls;
- host, virtual, and hardware destination selection; and
- a global **PANIC** control.

## Flagship parameters

| Parameter | Choices/range | Default |
|---|---|---|
| MIDI Format | Off, Normal MIDI, MPE MIDI | Normal MIDI |
| Normal MIDI Routing | Single Channel, Per Source 1-16 | Per Source 1-16 |
| Normal MIDI Channel | 1..16 | 1 |
| MPE Zone | Lower, Upper | Lower |
| MPE Pitch Bend Range | 2, 12, 24, 48 semitones | 2 semitones |
| MPE Send Setup Messages | Off, On | On |
| MPE Pitch Mode | Retrigger, Glide | Retrigger |
| Time Field Mode | Flow, Grid, Ensemble | Ensemble |
| Time Field Clock | Host, Internal | Host |
| Internal BPM | 40..240 BPM | 120 BPM |
| Grid Division | 1/4, 1/8, 1/16, 1/32 | 1/16 |
| Attacks Per Step | 1..16 | 4 |
| Maximum Active Voices | 1..16 | 16 (effective 15 in MPE) |
| Gate Length | 5..100% | 70% |
| Temporal Spread | 1, 2, 4, 8, 16 steps | 4 steps |
| Adaptive Crowd Governor | Manual, Adaptive | Adaptive |
| Pitch System | Tonal, Atomic | Atomic |
| Root | C..B | C |
| Root Octave | 0..6 | 2 |
| Scale | Major, Natural Minor, Pentatonic, Dorian, Lydian, Harmonic Minor, Whole Tone | Major |
| Atomic Element | 29 elements, Hydrogen through Zinc | Helium |
| Atomic Scale Mode | Core, Extended, Microtonal, Scientific, Raw 128 | Extended |
| Octave Range | 1..6 | 4 |

The UDP port and selected MIDI destination are also saved with plugin state. Incoming
host MIDI is passed through unchanged whenever MIDI output is enabled.

## Build

Requirements: CMake 3.22+, a C++17 toolchain, and internet access for the first
configure. JUCE 8.0.4 is fetched with CMake `FetchContent`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
ctest --test-dir build --output-on-failure
```

Flagship release outputs:

- VST3: `build/AudienceHarmonicSynth_artefacts/Release/VST3/Cosmic Microwave.vst3`
- Standalone: `build/AudienceHarmonicSynth_artefacts/Release/Standalone/Cosmic Microwave.app`

On macOS, `AUDIENCE_SYNTH_AUTO_INSTALL` defaults to `ON` and copies plugin targets to
the user VST3 folder. Configure with `-DAUDIENCE_SYNTH_AUTO_INSTALL=OFF` to build
without installing. `AUDIENCE_SYNTH_BUILD_TESTS` controls the test targets and defaults
to `ON`.

## Products

The CMake project contains four MIDI-oriented products:

| Product | Formats | Purpose |
|---|---|---|
| **Cosmic Microwave** | VST3 + Standalone | Flagship single-touch OSC-to-Normal-MIDI/MPE router documented here. |
| **Cosmic Microwave MIDI** | VST3 + Standalone | MIDI-effect audience generator with scale processing. |
| **Cosmic Microwave MIDI Generator** | VST3 | Ableton-focused MIDI-effect variant with scale correction/remapping. |
| **Cosmic Microwave MIDI Device** | Standalone | Lightweight UDP-to-MIDI application. |

## Runtime architecture

```text
already-separated OSC zone / simulator
  -> OscBridge validation
  -> MidiAudienceModel (256 sources x one admitted live touch)
  -> OscFingerRouter fixed-capacity event queue
  -> AdaptiveCrowdGovernor (soft Grid/Ensemble admission policy)
  -> CrowdTimeField
       -> Flow: direct lifecycle/motion
       -> Grid: clocked attack queue
       -> Ensemble: port-seeded temporal lanes
  -> pitch lookup
       -> MidiPitchMap (7 tonal 12-TET maps)
       -> AtomicScaleMap (29 elements x 5 density modes)
  -> MpeMidiOutput
       -> Normal MIDI: fixed channel or stable source -> Ch 1..16
       -> MPE: one member channel per active source touch
  -> host MIDI bus
  -> optional port-derived virtual or hardware MIDI destination

host MIDI input -> unchanged MIDI thru when output is enabled
stereo instrument output -> silent compatibility shell
```

The OSC callback validates bounded source/touch data before enqueueing it. The realtime
path uses preallocated event and MIDI storage. Lifecycle and motion have independent
bounded queues; motion keeps only the latest value for each axis and lifecycle always
drains first. UI monitoring reads lightweight source snapshots rather than the network
receiver directly.

## Upgrade note

The renamed VST3 intentionally keeps SpektraSynth's manufacturer/plugin codes and
bundle identity for existing session lookup. Do not keep `SpektraSynth.vst3` and
`Cosmic Microwave.vst3` together in the scanned VST3 directory: they identify the
same plugin class. Back up the old bundle outside the plugin folder, install Cosmic
Microwave, and rescan the host.

New 2.4.0 sessions open on Ensemble timing with the Adaptive Crowd Governor enabled,
plus Atomic / Helium / Extended pitch mapping. Existing state from schema 6 or earlier
receives Manual Governor mode, preserving its saved attack, active-limit, and spread
behaviour. State from schema 3 or earlier additionally receives Flow timing, so
upgrading does not move established attacks onto a grid. Schema-5 input remains
compatible; schema 6 discarded its retired experimental fields, and migrated state is
now stamped as schema 7.
Existing schema-2 MIDI-only sessions still
migrate explicitly to Tonal so they keep their previous pitch-map intent.
Released 1.x sessions that selected an element spectrum migrate to Atomic and recover
the corresponding element; their stable `spectralElement` and `atomicScaleMode`
parameter values are retained.

Repositories upgraded from pre-2.0 versions may still contain old media, preparation
tools, or implementation files. The `AudienceHarmonicSynth` 2.4.0 target does not load
or compile them; `CMakeLists.txt` is the authoritative runtime source list.

## Manual

Start with [docs/manual/README.md](docs/manual/README.md). The most useful chapters for
a live setup are the [UI guide](docs/manual/02-ui-guide.md),
[MPE setup](docs/manual/03-mpe-setup.md), and
[OSC/Ableton routing guide](docs/manual/04-osc-audience.md).
