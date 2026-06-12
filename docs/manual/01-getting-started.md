# 01 — Getting Started

> **SpektraSynth** — *A spectral instrument that turns elemental emission lines into playable scales, tunings, and timbres.*

## What it is

SpektraSynth is an audience-driven spectral instrument. A crowd of phones (or a built-in
simulator, or a MIDI keyboard) plays it: every participant is a seat in a virtual venue
(row letter + column number), and their X/Y touch data triggers and shapes voices.

At its core is a translation step: **atomic spectra are translated into playable musical
scales and timbral fingerprints.** Real emission-line catalogues (wavelength + intensity
data for 29 chemical elements, Hydrogen through Zinc) are mapped into microtonal scale
degrees and additive-synthesis partials. To be clear about the framing: SpektraSynth does
not claim to be "the real sound of atoms" — atoms do not emit sound. It *translates* the
structure of each element's light spectrum into pitch ratios and amplitudes you can
perform with.

What a crowd or a player controls:

- **X position** → pitch, quantized to the selected scale — a conventional musical scale
  *or* an element's translated spectral scale.
- **Y position** → voice amplitude and expression.
- **On/off** → voices starting and releasing.

The result can be rendered three ways, freely combined:

- internal audio (sample playback, granular clouds, or the Element Spectral Synth),
- standard MIDI notes, or
- **MPE MIDI** with per-note pitch bend, so the microtonal spectral degrees survive the
  trip into other synths.

## The products

One build produces four targets (see `CMakeLists.txt`):

| Product | Formats | What it is |
|---|---|---|
| **SpektraSynth** | VST3 + Standalone | The flagship instrument: full audio engine (Sample Library / Granular / Element Spectral Synth) plus MIDI/MPE output. This manual is mostly about this product. |
| **SpektraSynth MIDI** | VST3 + Standalone | A MIDI-effect variant (non-MPE note generator). |
| **SpektraSynth MIDI Generator** | VST3 | An Ableton-focused MIDI generator with a scale-lock module (root, scale type, correction mode, pitch-class remapping). |
| **SpektraSynth MIDI Device** | Standalone app | A minimal UDP → MIDI bridge application. |

## Requirements

- **macOS** (the build targets macOS 10.13+; bundles are ad-hoc code-signed during the
  build). An Android standalone build also exists, but macOS is the primary platform.
- **To play:** any VST3 host (Ableton Live, Bitwig, Reaper, …) — or no host at all:
  the Standalone app runs by itself. There is no separate "JUCE runtime" to install;
  JUCE 8 is compiled into the binaries.
- **To receive the audience:** participants' devices send OSC over UDP to the machine
  running SpektraSynth (default port `6060`). For testing you need nothing — the built-in
  simulator stands in for a crowd (see [04 — OSC & the Audience](04-osc-audience.md)).
- **To build from source:** CMake 3.22+, a C++17 toolchain (Xcode command-line tools on
  macOS), and an internet connection for the first configure (JUCE 8.0.4 is fetched
  automatically).

## Install locations

On macOS the default build installs the VST3 for you:

- **VST3:** `~/Library/Audio/Plug-Ins/VST3/SpektraSynth.vst3`
  (copied automatically after each build)
- **Standalone:** `build/AudienceHarmonicSynth_artefacts/Release/Standalone/SpektraSynth.app`
  (run it in place, or move it to `/Applications`)

Sample libraries are bundled into each app/plugin at
`Contents/Resources/Samples`; during development the plugin can also read the
source-tree `Samples/` folder directly, and it additionally looks in
`~/Library/Application Support/SpektraSynth/Samples` (the user application-data folder).

## Build from source — quickstart

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

The first configure downloads JUCE 8.0.4 via CMake `FetchContent`. Release outputs land
under `build/AudienceHarmonicSynth_artefacts/Release/`:

- `VST3/SpektraSynth.vst3`
- `Standalone/SpektraSynth.app`

Two CMake options worth knowing:

- **`AUDIENCE_SYNTH_AUTO_INSTALL`** (default `ON`, macOS): after each build, also installs
  a cleaned copy of the synth VST3 into the user plug-ins folder. This is a local-dev
  convenience; on CI or other machines configure with
  `-DAUDIENCE_SYNTH_AUTO_INSTALL=OFF` to build without installing. (The standard JUCE
  copy-after-build step that produces `SpektraSynth.vst3` is always on.)
- **`AUDIENCE_SYNTH_BUILD_TESTS`** (default `ON`): builds the `ctest` test harnesses
  (scale-builder, MIDI/MPE output, OSC bridge, sample library, performance smoke tests).
  Run them with `ctest --test-dir build`.

## First sound in 5 steps

No network, no audience, no DAW required:

1. **Launch the Standalone** `SpektraSynth.app`. The top bar should show a green
   **LIVE** pill, and the big audience map should read **“Waiting for audience”** with
   *“Listening for OSC on UDP 6060”* underneath. Make sure **Mute** (top right) is off —
   the macro panel should say **OUTPUT ARMED**, not **OUTPUT MUTED**.
2. **Add a fake participant.** In the bottom **SIMULATOR** row, click **“+ Add”**. A dot
   appears on the audience map and a voice fades in (the default patch is the Sample
   Library engine with direct sample playback). Click **“+25 Crowd”** for an instant
   ensemble, and toggle **“Random Movement”** to make the crowd drift.
3. **Play it yourself.** Click or drag on the **SCALE KEYBOARD** strip, or press the
   computer keys `1–0`, `Q–P`, `A–L`, `Z–M` — each key is one scale step. Sounding steps
   glow.
4. **Choose your engine.** The **ENGINE** combo (left side of the macro panel) switches
   between **Sample Library** and **Element Spectral Synth**:
   - *Sample Library*: pick a library in the left rail (**Samples** tab); the
     **SAMPLE PLAYBACK** combo chooses **Sample Player** (direct, Kontakt-style) or
     **Granular**.
   - *Element Spectral Synth*: pick an element in the left rail (**Elements** tab) — the
     instrument now plays that element's translated spectrum as both scale and timbre,
     and the UI tints itself with the element's root-wavelength colour.
5. **Shape it live.** The four macro knobs — **ENERGY**, **MOTION**, **TONE**,
   **SPACE** — are the main performance controls. Set **ROOT / OCT / SCALE / RANGE** to
   taste, then click **+25 Crowd** again and listen to the texture grow.

From here:

- [02 — UI Guide](02-ui-guide.md) walks every control in the editor.
- [03 — MPE Setup](03-mpe-setup.md) gets the microtonal MIDI output into other synths.
- [04 — OSC & the Audience](04-osc-audience.md) connects a real crowd.
