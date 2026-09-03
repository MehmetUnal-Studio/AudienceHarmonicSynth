# 01 - Getting Started

> **Cosmic Microwave** - *one separated OSC zone in, source-safe MIDI out.*

## What it is

Cosmic Microwave (formerly SpektraSynth) is an audience-driven OSC-to-MIDI router.
Each OSC address identifies a zone, source/participant ID, its `finger0` touch, and a
control. The plugin keeps that identity stable while converting normalized movement into Tonal
or element-derived Atomic Scale MIDI notes.

In a multi-zone show, the upstream audience server sends every zone to a separate UDP
port and each port feeds its own Cosmic Microwave instance. The 2.8.0 factory performance
layout maps Zones A-H to UDP `6062..6069`, with one port-named virtual MIDI endpoint
per instance. A genuinely fresh instance atomically claims the lowest free complete
route in that order; it does not take over or share an occupied port.

Audience controls map to a Notes Only performance stream:

- **X / U** -> one pitch in the selected root, pitch system, and octave range.
- **Y / V** -> the velocity sampled when a Note On is created.
- **On / off** -> one ordered note lifecycle for each source.

Cosmic Microwave generates Note On and Note Off only. It does not generate MPE, CC11,
CC74, Channel Pressure, Pitch Bend, RPN, or Crowd Macro CC messages, and it has no LFO
message gate or modulation mode. Every source can
be routed deterministically across Channels 1-16.

Cosmic Microwave 2.8.0 does not produce sound. It keeps a silent stereo instrument shell
so Ableton can place it like the previous product and reopen existing sessions. The
actual sound comes from instruments receiving its MIDI.

## Products

One CMake project builds four MIDI-oriented targets:

| Product | Formats | Purpose |
|---|---|---|
| **Cosmic Microwave** | VST3 + Standalone | The flagship single-touch Notes Only router documented by this manual. |
| **Cosmic Microwave MIDI** | VST3 + Standalone | A MIDI-effect audience generator with scale processing. |
| **Cosmic Microwave MIDI Generator** | VST3 | An Ableton-oriented MIDI effect with scale correction and pitch-class remapping. |
| **Cosmic Microwave MIDI Device** | Standalone | A compact UDP-to-MIDI bridge application. |

## Requirements

- **macOS** is the primary platform. The project targets macOS 10.13 or newer.
- **To use the VST3:** a VST3 host such as Ableton Live, Bitwig, or Reaper, plus one
  or more instruments that receive the generated MIDI.
- **To use the Standalone app:** a virtual-port-aware or hardware MIDI receiver. The
  app must remain running while it receives OSC and emits MIDI.
- **To receive the audience:** UDP reachability from the upstream server to the
  Cosmic Microwave machine and a unique configured port per already-separated zone.
- **To test without a network:** use the built-in simulator. For captured UDP replay,
  load generation, and deterministic packet faults, use the separate Chaos Lab
  (Node.js 20+ recommended).
- **To build:** CMake 3.22+, a C++17 toolchain, and internet access for the first
  configure. JUCE 8.0.4 is fetched automatically.

## Install locations

The default macOS build installs the VST3 into:

```text
~/Library/Audio/Plug-Ins/VST3/Cosmic Microwave.vst3
```

The Standalone app is built at:

```text
build/AudienceHarmonicSynth_artefacts/Release/Standalone/Cosmic Microwave.app
```

The renamed VST3 intentionally keeps SpektraSynth's manufacturer code, plugin code,
and bundle identity so existing DAW sessions can resolve it. Do not leave
`SpektraSynth.vst3` and `Cosmic Microwave.vst3` in the scanned VST3 folder together:
they represent the same VST3 class. Move the old bundle to a backup outside the plugin
folder, install Cosmic Microwave, and rescan the host.

## Build from source

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
ctest --test-dir build --output-on-failure
```

Release outputs are under `build/AudienceHarmonicSynth_artefacts/Release/`:

- `VST3/Cosmic Microwave.vst3`
- `Standalone/Cosmic Microwave.app`

Useful CMake options:

- **`AUDIENCE_SYNTH_AUTO_INSTALL`** - defaults to `ON` on macOS and copies plugin
  targets to the user VST3 folder. Set it to `OFF` for CI or a build-only workflow.
- **`AUDIENCE_SYNTH_BUILD_TESTS`** - defaults to `ON` and builds the MIDI mapping,
  scale, MIDI output, OSC bridge, finger-router, audience-model, Tonal/Atomic pitch-map,
  Atomic integration, both Governors, Time Field, Global Conductor,
  Chaos Lab, timed MIDI integration, catalog, and state-migration tests.

The internal CMake target is still named `AudienceHarmonicSynth` for compatibility;
the user-facing product and bundle name are Cosmic Microwave.

## First routed MIDI in five steps

This test uses the simulator, so it does not require the audience server.

1. **Open Cosmic Microwave.** Load the VST3 on an active Ableton track or launch the
   Standalone app. On a clean system the first fresh instance claims Zone A / UDP
   `6062`; if an earlier instance owns it, the new one claims the next free complete
   route through Zone H / UDP `6069`. Confirm the OSC card shows the assigned route.
2. **Confirm timing and format.** **Flow / Host / 1/32**, **Note Duration 16n / Same Note Tie**,
   **Notes Only**, **Per source 1-16**, and **Source Capacity 64 / 4 per channel** are
   already selected for this immediate connectivity test.
3. **Confirm one output path.** The factory route is **External Only** through the
   virtual endpoint matching the assigned UDP port (on the first clean route,
   **Virtual: Cosmic Microwave 6062 Out**). Select **Host Only** instead only when the
   Ableton set deliberately uses the plugin MIDI bus. Use
   **Mirror** only when both routes are intentionally consumed; otherwise it can create
   duplicate notes. The external-route status should confirm that the port opened.
4. **Prepare a receiver.** In Ableton, create a MIDI track with a sound-producing
   instrument. For Host Only, route from the Cosmic Microwave device/track; for
   External Only, set **MIDI From** to the assigned `Cosmic Microwave <port> Out`
   endpoint. Initially listen to
   all channels and enable the required monitoring/arming. The Standalone app uses an
   external or hardware endpoint because it has no DAW host bus.
5. **Generate a source.** Click **+1 Held** in the simulator. The header's source,
   touch and note counters should change; one cell lights in the activity map and
   the receiving instrument plays. Enable **Move active U/V** to exercise pitch
   selection and the next attack's velocity, then click **Clear** and verify the note releases.

New sessions begin with **Atomic / Zinc / Core**, rooted at C2 across four
octaves. Select **Tonal** in **PITCH MAPPING** if the first test should use a familiar
12-TET scale such as Major. Notes Only rounds Atomic targets to the nearest MIDI
semitone; it sends no Pitch Bend.

New 2.8.0 sessions also begin with **Flow / Host / 1/32**, **Note Duration 16n / Same Note Tie**,
**Source Capacity 64**, a 100% gate, and the **Adaptive Crowd Governor** in **Manual**.
The prepared fixed values are attack 16, active 16, and spread 16. Adaptive remains
available when a Grid or Ensemble show
needs density-dependent admission. Switching modes never overwrites the Manual values.
Projects saved with state schema 6 or earlier open in Manual;
projects saved before schema 4 additionally migrate to **Flow**, preserving their
earlier direct timing. New state is schema 11. Schema-7-and-earlier projects retain
their historical Mirror/shared-port routing and start with the Safety Governor off;
review these choices in Show Console before the next performance.

Schema 10 adds the saved **Note Duration** and **Source Capacity** choices. Missing
duration migrates to 16n. A schema-9-or-earlier project without a capacity value keeps
its former implicit 256-source range, while genuinely fresh state starts at 64. The
historical schema-9 step that converts former MPE output to Notes Only remains intact.
Schema 11 adds **Ensemble Same Note**. Older, partial, malformed, and out-of-range state
opens as **Tie**, preserving the established sound until Retrigger is selected explicitly.

The Zone A screenshot baseline intentionally starts with the **Safety Governor Off**,
so Venue Preflight reports that row as a blocker until the operator enables Safety.
This is a visible show-readiness decision, not a hidden automatic change.

Each additional genuinely fresh instance automatically takes the next free complete
A-H route. **SHOW CONSOLE > Routing Safety > FACTORY PERFORMANCE DEFAULT** remains an
explicit exact recall: Zone A-H select ports `6062..6069`, set the matching Expected
Zone, and make A the Group 1 Leader while B-H are Followers. Preset recall sends Panic,
clears transient simulator/live cards, returns the simulator profile to Human, and
applies the entire Notes Only/timing/pitch/route baseline.

If all eight routes are occupied, the new instance opens no OSC receiver, virtual MIDI
endpoint, or Global Conductor registration. Free a route, then press **RETRY AUTO**;
the instance does not wrap, share, or continuously rescan on its own.

An Ableton set's restored plugin state is authoritative: opening a saved set does not
silently recall a factory performance preset or auto-shift its route. Complete saved
state, direct route edits, and explicit presets win exactly; an occupied saved route
fails closed there instead of moving to another zone. Partial legacy state is healed
with schema-specific compatibility defaults; only missing root UDP/destination
metadata falls back to the Zone A `6062` virtual route.

If the counters move but the receiver does not, the OSC-to-MIDI path is working and
the remaining issue is destination or receiver routing. See
[06 - Troubleshooting & FAQ](06-troubleshooting-faq.md).

## Connect the real audience

For each instance:

1. Recall the matching Zone A-H factory performance preset, or enter its assigned UDP port,
   choose the corresponding **Expected Zone**, keep **Exclusive UDP Port** on, and
   click **Apply**.
2. Choose exactly one output path: **Host Only** or **External Only**.
3. Send `/cs/<zone>/<source>/finger0/u`, `/v`, and `/on` messages.
4. Confirm **Receiving**, the expected observed zone letter, and zero zone mismatches.
5. Confirm the source appears in the activity map's expected MIDI-channel column.
6. Open **SHOW CONSOLE** and resolve every failed Venue Preflight row. Warnings may be
   intentional, such as Global Conductor being Off for a single-zone rehearsal.
7. If several instances need a shared capacity budget, assign the same Conductor group,
   set one instance to Leader, and confirm every member reports a live global quota.

For multiple instances on one playing Ableton transport, leave **CLOCK** at **Host**
so they share the host PPQ grid. If the host clock is unavailable or stopped, Cosmic
Microwave continues on its common monotonic fallback at the Internal BPM; selecting
**Internal** makes that process-wide clock explicit.

The plugin does not infer Zone A from `6062` or Zone B from `6063`. Those are factory
show conventions. **Expected Zone** is an explicit filter, not port inference;
wrong-zone messages are counted and rejected before source state or MIDI generation.

## Next chapters

- [02 - UI Guide](02-ui-guide.md) explains every visible control.
- [03 - MIDI Output Setup](03-midi-output-setup.md) covers Notes Only routing,
  receiver setup, Panic, and legacy-controller cleanup.
- [04 - OSC & the Audience](04-osc-audience.md) defines the complete wire protocol.
- [Capture/Replay Chaos Lab](../chaos-lab.md) covers external capture, replay,
  deterministic failure injection, and load generation.
- [05 - Pitch Systems & External Tuning](05-tuning-files.md) explains the seven tonal
  mappings, 29 Atomic elements, density modes, and external receiver tuning.
