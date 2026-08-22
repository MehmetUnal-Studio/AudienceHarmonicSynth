# 01 - Getting Started

> **Cosmic Microwave** - *one separated OSC zone in, source-safe MIDI out.*

## What it is

Cosmic Microwave (formerly SpektraSynth) is an audience-driven OSC-to-MIDI router.
Each OSC address identifies a zone, source/participant ID, its `finger0` touch, and a
control. The plugin keeps that identity stable while converting normalized movement into Tonal
or element-derived Atomic Scale MIDI pitch and expression.

In a multi-zone show, the upstream audience server sends every zone to a separate UDP
port and each port feeds its own Cosmic Microwave instance. A typical layout is Zone A
on `6060`, Zone B on `6061`, and one port-named virtual MIDI endpoint per instance.

Audience controls map directly to MIDI:

- **X / U** -> one pitch in the selected root, pitch system, and octave range, plus
  CC74.
- **Y / V** -> note-on velocity and CC11; MPE also sends channel pressure.
- **On / off** -> one ordered note lifecycle for each source.

Normal MIDI can route every source deterministically across Channels 1-16. MPE instead
allocates one member channel per active source touch for isolated expression.

Cosmic Microwave 2.5.0 does not produce sound. It keeps a silent stereo instrument shell
so Ableton can place it like the previous product and reopen existing sessions. The
actual sound comes from instruments receiving its MIDI.

## Products

One CMake project builds four MIDI-oriented targets:

| Product | Formats | Purpose |
|---|---|---|
| **Cosmic Microwave** | VST3 + Standalone | The flagship single-touch Normal MIDI/MPE router documented by this manual. |
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
  scale, MPE, OSC bridge, finger-router, audience-model, Tonal/Atomic pitch-map,
  Atomic integration, both Governors, Time Field, Global Conductor, Crowd Expression,
  Chaos Lab, timed MIDI integration, catalog, and state-migration tests.

The internal CMake target is still named `AudienceHarmonicSynth` for compatibility;
the user-facing product and bundle name are Cosmic Microwave.

## First routed MIDI in five steps

This test uses the simulator, so it does not require the audience server.

1. **Open Cosmic Microwave.** Load the VST3 on an active Ableton track or launch the
   Standalone app. Confirm the OSC card says it is listening on UDP `6060`.
2. **Choose timing and protocol.** Select **Flow** in **TIME FIELD** for this immediate
   connectivity test. In **MIDI ROUTING**, select **Normal MIDI** and
   **Per source 1-16**. Return to Ensemble after the route is confirmed.
3. **Choose one output path.** Leave **Host Only** for Ableton's plugin MIDI bus, or
   select **External Only** and then **Virtual: Cosmic Microwave 6060 Out**. Use
   **Mirror** only when both routes are intentionally consumed; otherwise it can create
   duplicate notes. The external-route status should confirm that the port opened.
4. **Prepare a receiver.** In Ableton, create a MIDI track with a sound-producing
   instrument. For Host Only, route from the Cosmic Microwave device/track; for
   External Only, set **MIDI From** to `Cosmic Microwave 6060 Out`. Initially listen to
   all channels and enable the required monitoring/arming. The Standalone app uses an
   external or hardware endpoint because it has no DAW host bus.
5. **Generate a source.** Click **+ Source** in the simulator. The header's source,
   touch and note counters should change; one cell lights in the activity map and
   the receiving instrument plays. Enable **Random movement** to exercise pitch and
   expression, then click **Clear** and verify the note releases.

New sessions begin with **Atomic / Helium / Extended**, rooted at C2 across four
octaves. Select **Tonal** in **PITCH MAPPING** if the first test should use a familiar
12-TET scale such as Major. Normal MIDI rounds Atomic targets to the nearest semitone;
MPE sends the exact target as a base note plus per-note pitch bend.

New 2.5.0 sessions also begin with **Ensemble / Host / 1/16**, a 70% gate, and the
**Adaptive Crowd Governor** enabled. Adaptive measures recent audience density and
softly changes attacks per step, active voices, and spread; in MPE its active limit is
always capped at the 15 member channels. Select **Manual** when you want the saved
fixed values (initially attack 4, active 16, spread 4). Switching modes never overwrites
those Manual values. Projects saved with state schema 6 or earlier open in Manual;
projects saved before schema 4 additionally migrate to **Flow**, preserving their
earlier direct timing. New state is schema 8. Schema-7-and-earlier projects retain
their historical Mirror/shared-port routing and start with the Safety Governor off;
review these choices in Show Console before the next performance.

If the counters move but the receiver does not, the OSC-to-MIDI path is working and
the remaining issue is destination or receiver routing. See
[06 - Troubleshooting & FAQ](06-troubleshooting-faq.md).

## Connect the real audience

For each instance:

1. Enter its assigned UDP port, choose the corresponding **Expected Zone**, keep
   **Exclusive UDP Port** on, and click **Apply**.
2. Choose exactly one normal output path: **Host Only** or **External Only**.
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

The plugin does not infer Zone A from `6060` or Zone B from `6061`. Those are upstream
deployment conventions. **Expected Zone** is an explicit filter, not port inference;
wrong-zone messages are counted and rejected before source state or MIDI generation.

## Next chapters

- [02 - UI Guide](02-ui-guide.md) explains every visible control.
- [03 - MPE Setup](03-mpe-setup.md) covers per-source-touch expression and receiver setup.
- [04 - OSC & the Audience](04-osc-audience.md) defines the complete wire protocol.
- [Capture/Replay Chaos Lab](../chaos-lab.md) covers external capture, replay,
  deterministic failure injection, and load generation.
- [05 - Pitch Systems & External Tuning](05-tuning-files.md) explains the seven tonal
  mappings, 29 Atomic elements, density modes, and external receiver tuning.
