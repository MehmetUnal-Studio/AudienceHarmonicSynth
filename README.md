# Cosmic Microwave

Formerly named **SpektraSynth**.

Version 2.8.0

2.8.0 adds an explicit Ensemble **Same Note** articulation choice. **Tie** keeps the
historical sound by extending ownership when a held source repeats the same pitch.
**Retrigger** emits a safe Note Off followed by Note On for every admitted Ensemble
pulse, without enabling MPE or performance controller messages. Flow and Grid always
use Tie.

2.7.1 fixes a Grid admission deadlock: when a fixed-duration MIDI tail ends,
the matching Grid active slot is now released from renderer feedback, so queued
sources continue after the first active group. Pending-pressure telemetry is
saturated safely and can reduce density without closing the queue it must drain.

Cosmic Microwave is a JUCE VST3 and standalone OSC-to-MIDI router for
audience interaction. It receives already-separated zone streams over UDP, keeps each
source's single-touch lifecycle intact, maps normalized movement through either tonal or
element-derived Atomic Scale pitch maps, and sends Notes Only MIDI to Ableton, a
virtual MIDI endpoint, or a system MIDI device.

Cosmic Microwave 2.8.0 is behaviourally MIDI-only: it does not generate sound. The VST3
keeps a silent stereo instrument shell, its existing class identity, and its instrument
placement so Ableton sets made with the earlier product can still resolve the device.

## OSC mapping

Each instance listens on one configured UDP port. The upstream server is responsible
for separating zones before they reach the plugin; for example:

| Zone | UDP port | Virtual MIDI endpoint |
|---|---:|---|
| A | `6062` | `Cosmic Microwave 6062 Out` |
| B | `6063` | `Cosmic Microwave 6063 Out` |
| C | `6064` | `Cosmic Microwave 6064 Out` |
| D | `6065` | `Cosmic Microwave 6065 Out` |
| E | `6066` | `Cosmic Microwave 6066 Out` |
| F | `6067` | `Cosmic Microwave 6067 Out` |
| G | `6068` | `Cosmic Microwave 6068 Out` |
| H | `6069` | `Cosmic Microwave 6069 Out` |

The port does not define the zone. Set **Expected Zone** to `A..Z` to make a production
instance reject otherwise-valid messages from every other zone, or leave it at **Any**
for diagnostics. **Exclusive UDP Port** is enabled in new sessions, so a second plugin
instance cannot silently subscribe to the same port. A zone-policy change is a routing
boundary and safely releases held notes before the new filter becomes active.

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
  Cosmic Microwave publishes an internal ordered Cancel. It immediately releases only
  that source/finger's scheduled tails; simulator voices are excluded.

### Notes Only performance contract

```text
U/X -> selected Tonal or Atomic MIDI note
V/Y -> velocity for the next Note On
On 1 -> Note On
On 0 -> end the semantic touch; its already-created MIDI tail keeps its deadline
```

Note-on velocity is limited to `1..127`.
Horizontal position is divided into equal regions across the selected root, pitch map,
and octave range. New sessions default to **Atomic / Zinc / Core**, root C2, and a
four-octave range.

Normal performance output contains only Note On and Note Off. Cosmic Microwave does
not generate CC11, CC74, Channel Pressure, Pitch Bend, RPN/MPE setup, or aggregate
Crowd Macro CCs, and it has no LFO message gate or LFO modulation mode. The only
controller exception is the explicit panic/safety sweep:
CC123 and CC120 once on each of the 16 MIDI channels.

### Note Duration

**Note Duration** controls only the generated note length; it never quantizes the
attack. Choices are `2n`, `4n`, `8n`, `16n`, and `32n`, with fresh instances and
factory presets using `16n`. Every new Note On snapshots the current host BPM (or the
saved Internal BPM when host tempo is unavailable), converts the musical value to an
absolute sample deadline, and retains that deadline through later tempo automation.
A fixed 4096-entry scheduler advances once per audio block, including silent blocks.
With **Same Note = Tie**, repeated identical source/channel/note attacks coalesce to one
ownership token. In Ensemble, **Retrigger** instead hard-releases and restarts that
source's identical note on every admitted pulse; it does not change Note Duration and
does not enable MPE. Panic, transport stop, route/zone changes, capacity shrink, and
watchdog Cancel bypass the musical tail and clean up immediately.

## Crowd Time Field

Cosmic Microwave 2.8.0 can turn an asynchronous crowd into a shared rhythmic field
without changing source identity or note ownership:

| Mode | Behaviour |
|---|---|
| **Flow** | Pass lifecycle and movement through directly, preserving the earlier 2.1 timing behaviour. |
| **Grid** | Queue attacks to the next selected musical division, with fair selection, an attacks-per-step limit, and an active-voice limit. Ordered Off ends the semantic scheduled voice; an admitted short tap receives the configured minimum gate. An already-started MIDI tail keeps its duration deadline, and completion of its final tail releases the active slot for the next queued source. |
| **Ensemble** | Place each source in a deterministic lane across the selected spread, apply a fixed semantic gate, and requeue a still-held source for later pulses. **Same Note = Tie** preserves one sounding ownership across identical-pitch pulses; **Retrigger** performs Note Off then Note On on every admitted pulse. Fixed-duration MIDI tails may overlap later pulses. |

New sessions default to **Flow / Host / 1/32** with the **Crowd Governor** in
**Manual**, a **100% gate**, **16 attacks per step**, **16 active voices**, and a
**16-step spread**. Flow passes the current lifecycle directly; the timing values are
already prepared if the operator later selects Grid or Ensemble. Sessions
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
at least one complete lane cycle. **MERGED** is monitoring only: it counts pending work
whose admission window elapsed. Held intent is renewed, while a released short tap can
expire; the figure is not packet loss and does not produce a MIDI CC.

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

### Pressure-aware Safety Governor

The v2.5 **Safety Governor** is separate from the musical Adaptive Crowd Governor.
It watches validated OSC ingress rate, lifecycle-queue pressure, dropped/coalesced
motion, Time Field pending pressure, external-MIDI FIFO pressure and age, and audio
callback deadline ratio. It escalates immediately through **NORMAL**, **HIGH**,
**CRITICAL**, and **EMERGENCY**, then recovers one level at a time with hysteresis and
holds. Higher states progressively thin redundant motion, lower new-attack and active
voice ceilings, increase minimum spread, suspend macros, and finally close new attack
admission. Flow remains direct in NORMAL and adopts those safety ceilings only while
pressure is elevated. Releases, the watchdog, and Panic remain available. Show Console exposes
the active state, reason flags, ingress/deadline/FIFO telemetry, and effective limits.

### Global Conductor

Up to 16 Cosmic Microwave instances in the same plugin process can share one of four
**Global Conductor** groups. Set instances to Leader or Follower; the deterministically
elected leader publishes a global attack budget (`1..64`) and voice budget (`1..128`)
at 10 Hz. Each live zone receives a fair, density-weighted quota, with scarce capacity
rotating deterministically. Groups are isolated. Missing, stale, or incoherent leader
data fails back to each instance's local Time Field policy after 1.5 seconds; the audio
thread never waits. `Off` keeps the instance local.

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
Notes Only sends the nearest 12-TET MIDI note for an Atomic target. Exact microtonal
retuning, when required, belongs downstream in the receiving instrument rather than in
Cosmic Microwave's performance stream.

## Source-to-channel routing

Notes Only defaults to **Per source 1-16**. Channel ownership is based on source ID,
not packet order:

```text
1 -> Ch 1   2 -> Ch 2   ...   16 -> Ch 16   17 -> Ch 1
0 -> Ch 16
```

All `u`, `v`, `on`, and `off` messages from one source's `finger0` touch use that
source's channel. A second **Single channel** mode sends every source through one
selected channel.

## MIDI destinations

The editor offers:

- **Host MIDI Output** - the VST3 MIDI bus.
- **Virtual: Cosmic Microwave <port> Out** - a stable endpoint derived from the
  instance's UDP port.
- Available system or hardware MIDI outputs.

**MIDI Output Path** decides where generated MIDI is delivered:

- **Host Only** sends only to the DAW bus.
- **External Only** sends only to the selected virtual/hardware destination and clears
  the host buffer fail-closed.
- **Mirror** sends to both paths deliberately.

**External Only** with the port-named virtual endpoint is the 2.8.0 factory performance default and
the recommended route when an Ableton set needs separate receiving tracks for Channels
1-16. Route changes are safety boundaries, so held state is released before switching.
Schema-7-and-earlier sessions migrate to Mirror to preserve their historical dual-output
behaviour.

## Ableton layout

For each audience zone:

1. Put one Cosmic Microwave instance on its own track.
2. Recall its **ZONE A..H** factory performance preset, or apply the zone's UDP port and
   Expected Zone manually.
3. Confirm **Notes Only** and **Per source 1-16**.
4. Keep **External Only** and `Virtual: Cosmic Microwave <port> Out`, or use
   **Host Only** when routing exclusively through Ableton's device output.
5. On receiving tracks, choose the Cosmic Microwave device for Host Only or that
   endpoint for External Only, then select the required channel.
6. Put the sound-producing instruments on those receiving tracks.

Each Cosmic Microwave instance has an independent set of Channels 1-16.

### Source Capacity

Each zone independently selects `64`, `128`, or `256` dense source IDs while keeping
exactly 16 MIDI channels. This yields 4, 8, or 16 sources per channel. Fresh instances
and factory presets use `64`; a legacy project with no saved capacity opens at `256`
to preserve its earlier admission range. IDs outside the selected capacity are
dropped and counted rather than wrapped or queued. Expansion never remaps an existing
ID; shrink retires upper IDs deterministically and performs a bounded stuck-note
cleanup before the remaining canonical sources are rehydrated.

### Two-Omnisphere receiver layout

The production Ableton template keeps Cosmic Microwave's sixteen MIDI channels and
splits only the downstream receiver workload:

```text
one zone / one Cosmic Microwave
  Ch 1..8  -> OMNI1 Multi parts 1..8
  Ch 9..16 -> OMNI2 Multi parts 1..8
```

At Source Capacity `64`, each MIDI channel—and therefore each Omnisphere part—serves
four source identities. Capacity `128` serves eight per part and `256` serves sixteen
per part. This is still exactly sixteen MIDI channels; it is not 16 Omnisphere
instances and it does not alter Cosmic Microwave's modulo mapping. Two 8-part receiver
instances are the intended show-template CPU layout, but actual parallelism and load
depend on Ableton, the selected patches, effects, audio buffer, and machine. Approve
the complete multi-zone template only after a real show-machine CPU/dropout soak.

## Editor

The MIDI-only editor is split into **PERFORM** and **SHOW CONSOLE** views and contains:

- a permanent build-derived **v2.8.0** version label in the header;
- live **SOURCES**, **TOUCHES**, **NOTES**, and **ACTIVE NOTES** metrics;
- an **OSC INPUT** card with port and validated-traffic status;
- a source-routing summary with observed zone letters;
- a simulator with one held mapping-test touch, a server-like pulsing crowd pool,
  and ephemeral **Human / Dense / Stress** behaviour profiles;
- a capacity-aware 64/128/256-source **SOURCE MATRIX** grouped into 16 fixed
  MIDI-channel columns;
- a **TIME FIELD** card for Flow/Grid/Ensemble timing, host/internal clocking,
  Manual/Adaptive crowd policy, Ensemble Tie/Retrigger articulation, density limits, gate, spread, and live
  Pending/Active/Merged telemetry;
- a Tonal/Atomic pitch system with element and density selection;
- Notes Only source/channel routing controls;
- host, virtual, and hardware destination selection;
- a global **PANIC** control;
- explicit Host Only / External Only / Mirror routing, Expected Zone, and exclusive
  UDP ownership controls;
- a **FACTORY PERFORMANCE PRESET** selector in Show Console > Routing Safety for Zone A-H,
  including port, zone, timing, pitch, Notes Only, and Conductor recall;
- Safety Governor state, reasons, and pressure telemetry;
- an eight-point Venue Preflight checklist for receiver, zone contract, UDP ownership,
  MIDI route, safety, Time Field, Global Conductor, and Source Quality readiness;
- process-local Global Conductor role/group/budget and live-quota controls; and
- a visible Notes Only policy summary for operator verification.

Show Console also displays commands for the external Capture/Replay Chaos Lab. The Lab
is a separate Node.js rehearsal tool; it is not embedded in the plugin and performs no
filesystem or network capture from the audio callback.

### 64-Source Quality Controller and Ready Gate

Show Console's **START 64 CHECK** begins a fresh runtime-only census for the selected
Source Capacity (`64`, `128`, or `256`). Existing projects and presets open in
**BYPASS**, so this feature never silently mutes an older session. While the check is
WARMING, only new attacks wait in the existing Time Field scheduler; Note Off,
watchdog Cancel, Panic, and notes that are already sounding remain release-safe.

For the default 64-source show domain, every accepted live OSC identity `0..63` must
send finite U, V, and On-1 evidence after START, remain actively held, and keep both U
and V heartbeats fresh throughout one clean two-second pre-ready hold. U and V freshness
is measured separately: neither axis can conceal a stale partner. The receiver allows
up to 1.2 seconds of heartbeat age as a transport/jitter tolerance, while the server
contract remains stricter and requires each held source to publish both axes with no
gap longer than 900 ms.

The clean hold also requires combined U/V motion at or below 50 events/s for every
source and at or below 1,200 events/s for the whole instance. Capacity drops, motion
drops, and lifecycle drops are counted separately. A capacity or lifecycle drop is a
hard latched fault until a fresh check; a motion drop prevents the current clean pass
and remains separately visible. Soft degradation after READY does not cut a running
performance.

Internal simulator population must be zero before the check can pass; simulator calls
are deliberately excluded from evidence. This is a local Cosmic Microwave **signal
census**, not proof of 64 connected browser sockets and not a complete venue sign-off.
Server owner/roster health, collision counts, the frozen bridge route manifest, and a
separate 60-second production-path soak with average/P95 traffic evidence must still
pass independently.

## Flagship parameters

| Parameter | Choices/range | Default |
|---|---|---|
| MIDI Format | Off, Notes Only | Notes Only |
| MIDI Output Path | Host Only, External Only, Mirror | External Only |
| Expected OSC Zone | Any, A..Z | A |
| Exclusive UDP Port | Off, On | On |
| Safety Governor | Off, On | Off |
| Notes Only Routing | Single Channel, Per Source 1-16 | Per Source 1-16 |
| Fixed MIDI Channel | 1..16 | 1 |
| Time Field Mode | Flow, Grid, Ensemble | Flow |
| Time Field Clock | Host, Internal | Host |
| Internal BPM | 40..240 BPM | 120 BPM |
| Grid Division | 1/4, 1/8, 1/16, 1/32 | 1/32 |
| Attacks Per Step | 1..16 | 16 |
| Maximum Active Voices | 1..16 | 16 |
| Gate Length | 5..100% | 100% |
| Temporal Spread | 1, 2, 4, 8, 16 steps | 16 steps |
| Adaptive Crowd Governor | Manual, Adaptive | Manual |
| Note Duration | 2n, 4n, 8n, 16n, 32n | 16n |
| Ensemble Same Note | Tie, Retrigger | Tie |
| Source Capacity | 64 (4/ch), 128 (8/ch), 256 (16/ch) | 64 (4/ch) |
| Global Conductor Role | Off, Leader, Follower | Leader |
| Global Conductor Group | 1..4 | 1 |
| Global Attack Budget | 1..64 | 16 |
| Global Voice Budget | 1..128 | 16 |
| Pitch System | Tonal, Atomic | Atomic |
| Root | C..B | C |
| Root Octave | 0..6 | 2 |
| Scale | Major, Natural Minor, Pentatonic, Dorian, Lydian, Harmonic Minor, Whole Tone | Major |
| Atomic Element | 29 elements, Hydrogen through Zinc | Zinc |
| Atomic Scale Mode | Core, Extended, Microtonal, Scientific, Raw 128 | Core |
| Octave Range | 1..6 | 4 |

The factory route family begins at UDP `6062` / Zone A and ends at UDP `6069` /
Zone H, with a matching port-named virtual endpoint on each route. A fresh instance
retains the lowest free member of that family before publishing the route. The UDP
port, selected MIDI destination,
safety/routing policy, conductor settings, and macro mapping are also saved with plugin
state. Incoming host MIDI is passed through
unchanged whenever MIDI output is enabled and the selected output path includes the
host.

### Factory performance presets

Show Console > Routing Safety contains eight complete factory recalls:

| Preset | UDP | Expected Zone | Conductor role |
|---|---:|---|---|
| Zone A | 6062 | A | Leader |
| Zone B | 6063 | B | Follower |
| Zone C | 6064 | C | Follower |
| Zone D | 6065 | D | Follower |
| Zone E | 6066 | E | Follower |
| Zone F | 6067 | F | Follower |
| Zone G | 6068 | G | Follower |
| Zone H | 6069 | H | Follower |

Every recall applies the full factory performance baseline shown above: Notes Only,
Per source 1-16, External Only and the matching port-named virtual endpoint, Flow /
Host / 1/32, Note Duration 16n, Ensemble Same Note Tie, Source Capacity 64 (4/channel),
Manual 16/16/100%/16, Atomic / Zinc / Core / C2 / four octaves,
exclusive UDP ownership, Group 1 with 16/16 budgets, Safety Governor Off, and Crowd
Macros Off. Recall is an explicit routing boundary: it sends Panic, clears ephemeral
simulator/live cards, returns the simulator profile to Human, and then binds the new
route. Enabling the Safety Governor after recall is an intentional show-readiness step;
Venue Preflight reports its factory-Off state until the operator enables it.

The preset selector is not a replacement for host state. A saved Ableton set or other
host state restores its own parameter, port, and destination values and remains
authoritative, even when that state is `CUSTOM`. Direct route edits and preset recalls
also disable fresh auto-assignment and apply exactly. Factory values apply automatically
only while a genuinely fresh instance claims its lowest free A-H route, or through an
explicit preset recall. A partial/legacy blob is healed with its schema-specific
compatibility defaults; only missing root UDP/destination metadata falls back to the
Zone A `6062` virtual route.

## Capture/Replay Chaos Lab

`tools/cosmic-chaos-lab.mjs` is an external, dependency-free Node.js CLI for bounded
UDP proxy/capture, deterministic replay (`0.25x..16x`), production-OSC generation, and
seeded drop/duplicate/reorder/jitter/burst-loss rehearsal. Captures are newline-delimited
JSON with the original datagram preserved as base64. Lifecycle packets have priority
over coalescible motion in bounded queues. See [docs/chaos-lab.md](docs/chaos-lab.md).

## Build

Requirements: CMake 3.22+, a C++17 toolchain, and internet access for the first
configure. JUCE 8.0.4 is fetched with CMake `FetchContent`. Node.js 20+ is recommended
for the external Chaos Lab; when Node is present, its regression suite joins CTest.

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
| **Cosmic Microwave** | VST3 + Standalone | Flagship single-touch OSC-to-Notes-Only MIDI router documented here. |
| **Cosmic Microwave MIDI** | VST3 + Standalone | MIDI-effect audience generator with scale processing. |
| **Cosmic Microwave MIDI Generator** | VST3 | Ableton-focused MIDI-effect variant with scale correction/remapping. |
| **Cosmic Microwave MIDI Device** | Standalone | Lightweight UDP-to-MIDI application. |

## Runtime architecture

```text
already-separated OSC zone / simulator
  -> OscBridge validation + expected-zone filter + exclusive ownership
  -> MidiAudienceModel (256 sources x one admitted live touch)
  -> OscFingerRouter fixed-capacity event queue
  -> PressureAwareSafetyGovernor (traffic/deadline/FIFO protection)
  -> AdaptiveCrowdGovernor (soft Grid/Ensemble admission policy)
  -> CrowdTimeField
       -> Flow: direct lifecycle/motion
       -> Grid: clocked attack queue
       -> Ensemble: port-seeded temporal lanes
  -> pitch lookup
       -> MidiPitchMap (7 tonal 12-TET maps)
       -> AtomicScaleMap (29 elements x 5 density modes)
  -> MpeMidiOutput
       -> Notes Only: fixed channel or stable source -> Ch 1..16
  -> explicit Host Only / External Only / Mirror output policy
       -> host MIDI bus
       -> port-derived virtual or hardware MIDI destination

up to 16 in-process instances
  <-> GlobalConductorHub group (10 Hz density-weighted attack/voice quotas)

external rehearsal process
  -> tools/cosmic-chaos-lab.mjs proxy / capture / replay / generate

host MIDI input -> Note On/Off-only thru when output is enabled, then routed by the
                   same Host Only / External Only / Mirror policy
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

Every fresh 2.8.0 instance atomically claims the lowest free complete factory route:
`6062 / Zone A / Group 1 Leader`, then `6063..6069 / Zone B..H / Group 1 Follower`,
with the matching port-named virtual MIDI endpoint. The retained exclusive OSC bind is
the claim; a UDP number is never selected speculatively. All other fresh performance
defaults remain External Only, Notes Only / Per source 1-16, Flow / Host / 1/32,
Manual 16/16/100%/16, Atomic / Zinc / Core at C2 over four octaves, and 16/16
Conductor budgets. Safety Governor is deliberately Off so readiness is an explicit
operator decision; Crowd Expression controller output remains retired and forced off.

If every A-H route is occupied, the fresh instance fails closed: it opens no OSC
receiver, no virtual MIDI endpoint, and no Global Conductor registration. It does not
silently wrap, share a port, or keep rescanning. After a route is released, use
**RETRY AUTO** in Show Console > Routing Safety.

Factory performance presets for Zones A-H and direct UDP/zone/destination edits are
explicit operator choices and always win exactly. Loading an existing Ableton set does
not auto-assign over its saved state; complete host-restored state remains authoritative
and stays on its exact saved route even when that route is currently busy, in which
case it fails closed there instead of shifting zones. Partial legacy state uses
schema-specific compatibility defaults; missing root UDP/destination metadata alone
uses the Zone A `6062` virtual route.

Existing state from schema 7 or earlier receives Mirror output, shared-port behaviour,
and the Safety Governor disabled so an upgrade cannot silently change its routing or
admission behaviour. Existing state from schema 6 or earlier receives Manual Crowd
Governor mode, preserving its saved attack, active-limit, and spread behaviour. State
from schema 3 or earlier additionally receives Flow timing. Schema-5 input remains
compatible; schema 6 discarded its retired experimental fields. Schema 9 migrates
former MPE output to Notes Only / Per source 1-16 and forces legacy Crowd Macro output
off. Schema 10 adds Note Duration and Source Capacity: missing duration becomes 16n,
fresh state defaults to 64 sources, and legacy state without a capacity value retains
the former 256-source range.
Schema 11 adds Ensemble Same Note. Missing or hostile values safely become **Tie**, so
older sessions retain their established articulation. Current saves are stamped as
schema 11. The historical schema-9 migration remains a
separate step: it is still responsible for converting former MPE output to Notes Only
and retiring Crowd Macro emission.
Existing schema-2 MIDI-only sessions still
migrate explicitly to Tonal so they keep their previous pitch-map intent.
Released 1.x sessions that selected an element spectrum migrate to Atomic and recover
the corresponding element; their stable `spectralElement` and `atomicScaleMode`
parameter values are retained.

Repositories upgraded from pre-2.0 versions may still contain old media, preparation
tools, or implementation files. The `AudienceHarmonicSynth` 2.8.0 target does not load
or compile them; `CMakeLists.txt` is the authoritative runtime source list.

## Manual

Start with [docs/manual/README.md](docs/manual/README.md). The most useful chapters for
a live setup are the [UI guide](docs/manual/02-ui-guide.md),
[MIDI output setup](docs/manual/03-midi-output-setup.md), and
[OSC/Ableton routing guide](docs/manual/04-osc-audience.md).
