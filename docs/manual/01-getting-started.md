# 01 - Getting Started

> **Cosmic Microwave** - *one separated OSC zone in, source-safe MIDI out.*

## What it is

Cosmic Microwave (formerly SpektraSynth) is an audience-driven OSC-to-MIDI router.
Each OSC address identifies a zone, source/participant ID, finger, and control. The
plugin keeps those identities stable while converting normalized movement into Tonal
or element-derived Atomic Scale MIDI pitch and expression.

In a multi-zone show, the upstream audience server sends every zone to a separate UDP
port and each port feeds its own Cosmic Microwave instance. A typical layout is Zone A
on `6060`, Zone B on `6061`, and one port-named virtual MIDI endpoint per instance.

Audience controls map directly to MIDI:

- **X / U** -> one pitch in the selected root, pitch system, and octave range, plus
  CC74.
- **Y / V** -> note-on velocity and CC11; MPE also sends channel pressure.
- **On / off** -> an independent note lifecycle for each finger.

Normal MIDI can route every source deterministically across Channels 1-16. MPE instead
allocates one member channel per active finger for isolated expression.

Cosmic Microwave 2.1 does not produce sound. It keeps a silent stereo instrument shell
so Ableton can place it like the previous product and reopen existing sessions. The
actual sound comes from instruments receiving its MIDI.

## Products

One CMake project builds four MIDI-oriented targets:

| Product | Formats | Purpose |
|---|---|---|
| **Cosmic Microwave** | VST3 + Standalone | The flagship finger-aware Normal MIDI/MPE router documented by this manual. |
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
- **To test without a network:** use the built-in simulator.
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
  catalog, and state-migration tests.

The internal CMake target is still named `AudienceHarmonicSynth` for compatibility;
the user-facing product and bundle name are Cosmic Microwave.

## First routed MIDI in five steps

This test uses the simulator, so it does not require the audience server.

1. **Open Cosmic Microwave.** Load the VST3 on an active Ableton track or launch the
   Standalone app. Confirm the OSC card says it is listening on UDP `6060`.
2. **Choose the protocol.** In **MIDI ROUTING**, select **Normal MIDI** and
   **Per source 1-16**.
3. **Choose the destination.** Select
   **Virtual: Cosmic Microwave 6060 Out**. The status line should confirm that the
   virtual port opened. You can use **Host MIDI Output** instead when the host exposes
   the plugin output directly in its routing menus.
4. **Prepare a receiver.** In Ableton, create a MIDI track with a sound-producing
   instrument, set **MIDI From** to `Cosmic Microwave 6060 Out`, initially listen to
   all channels, and enable the track's required monitoring/arming. For the Standalone
   app, choose the same virtual port in an external receiver.
5. **Generate a source.** Click **+ Source** in the simulator. The header's source,
   finger, and note counters should change; one cell lights in the activity map and
   the receiving instrument plays. Enable **Random movement** to exercise pitch and
   expression, then click **Clear** and verify the note releases.

New sessions begin with **Atomic / Helium / Extended**, rooted at C2 across four
octaves. Select **Tonal** in **PITCH MAPPING** if the first test should use a familiar
12-TET scale such as Major. Normal MIDI rounds Atomic targets to the nearest semitone;
MPE sends the exact target as a base note plus per-note pitch bend.

If the counters move but the receiver does not, the OSC-to-MIDI path is working and
the remaining issue is destination or receiver routing. See
[06 - Troubleshooting & FAQ](06-troubleshooting-faq.md).

## Connect the real audience

For each instance:

1. Enter its assigned UDP port and click **Apply**.
2. Send `/cs/<zone>/<source>/finger<n>/u`, `/v`, and `/on` messages.
3. Confirm **Receiving** and the expected observed zone letter.
4. Confirm the source appears in the activity map's expected MIDI-channel column.
5. Route the port-named endpoint to the receiving tracks.

The plugin does not infer Zone A from `6060` or Zone B from `6061`. Those are upstream
deployment conventions. If the UI reports more than one observed zone, correct the
server-side split.

## Next chapters

- [02 - UI Guide](02-ui-guide.md) explains every visible control.
- [03 - MPE Setup](03-mpe-setup.md) covers per-finger expression and receiver setup.
- [04 - OSC & the Audience](04-osc-audience.md) defines the complete wire protocol.
- [05 - Pitch Systems & External Tuning](05-tuning-files.md) explains the seven tonal
  mappings, 29 Atomic elements, density modes, and external receiver tuning.
