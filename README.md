# SpektraSynth

Version 1.0.35

VST3 + Standalone JUCE instrument driven by an audience's phones. Each
participant is identified by seat position (row letter + column number). Their
X/Y touch data triggers and shapes either direct sample playback, granular
sample playback, or an element spectral synth voice, so a crowd becomes a
collective harmonic texture.

## Mapping

OSC into UDP `6060`:

```text
/cs/<row>/<col>/finger0/on    1          seat activates
/cs/<row>/<col>/finger0/on    0          seat deactivates
/cs/<row>/<col>/finger0/off              seat deactivates
/cs/<row>/<col>/finger0/line  <0..127>   X position, mapped to the selected scale
/cs/<row>/<col>/finger0/v     <0..1>     Y position, mapped to voice amplitude
```

Rows are `A..Z`; columns are `0..99`. X is quantized to the selected root,
scale, and octave range. The default is C major over four octaves. Each trigger creates a three-voice unison layer using the
closest matching sample from the active library, then pitch-shifts to the target
MIDI note. Sample libraries default to direct sampler playback; the granular
engine is selected explicitly from `Sample Playback`.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

The first configure downloads JUCE 8.0.4 via FetchContent. Release outputs:

- VST3: `build/AudienceHarmonicSynth_artefacts/Release/VST3/SpektraSynth.vst3`
- Standalone: `build/AudienceHarmonicSynth_artefacts/Release/Standalone/SpektraSynth.app`

The build copies `Samples/` into each bundle's `Contents/Resources/Samples`
folder. During development, the plugin can also load the source-tree `Samples/`
folder directly.

## Parameters

| Parameter | Range | Default | What it does |
|---|---:|---:|---|
| Pitch | -12..12 semitones | 0 | Global transpose |
| Layer Mix | 0..1 | 0.7 | Per-seat voice amplitude scale |
| Attack | 10..3000 ms | 800 | Voice fade-in time |
| Release | 100..6000 ms | 2500 | Voice release time |
| Brightness | 0..1 | 0.6 | Low-pass response from X position |
| Movement (Grain) | 0..1 | 0.45 | Grain density, length, and spread |
| Grain Size | 40..800 ms | 260 | Base grain length before sound-mode shaping |
| Grain Density | 0..1 | 0.55 | Simultaneous grain overlap per active voice |
| Pitch Spread | 0..12 semitones | 0 | Scale-quantized pitch drift for grains |
| Position Jitter | 0..1 | 0.35 | Random start offset around the voice playhead |
| Stereo Spread | 0..1 | 0.45 | Random per-grain stereo placement |
| Grain Envelope | 4 choices | Hann | Grain window shape: Hann, Triangle, Soft Gate, or Pulse |
| Reverse Grains | on/off | off | Allows spawned grains to run backwards |
| Freeze | on/off | off | Holds active grain clouds and freezes the reverb tail |
| Engine Source | 2 choices | Sample Library | Chooses Sample Library or Element Spectral Synth |
| Sample Playback | 2 choices | Sample Player | For Sample Library: direct sampler playback or Granular |
| Reverb | 0..1 | 0.35 | Global reverb send |
| Delay | 0..1 | 0.25 | Cross-feedback delay amount |
| Wet Dry | 0..1 | 0.85 | Dry grain voice vs wet FX return balance |
| Tape Drive | 0..1 | 0 | Soft tape-style saturation before the limiter |
| Master | 0..1 | 0.7 | Output gain before limiter |
| Energy | 0..1 | 0.5 | Macro for level, attack response, and trigger density |
| Motion | 0..1 | 0.5 | Macro for grain spread and per-voice movement |
| Tone | 0..1 | 0.5 | Macro for filter and ambience brightness |
| Space | 0..1 | 0.5 | Macro for reverb and delay depth |
| Root | C..B | C | Root note for X-axis quantization |
| Scale | 7 choices | Major | Scale map for audience movement |
| Octaves | 1..6 | 4 | X-axis note range |
| Signature Mode | 5 choices | Choir Cloud | Engine personality: Choir Cloud, Glass Harmonics, Sub Swarm, Spectral Rain, or Frozen Hall |
| Audio MIDI Output Mode | 3 choices | Audio Only | Renders internal audio only, MIDI only, or audio plus outgoing MIDI |
| MIDI Output Type | 3 choices | Off | Sends no MIDI, normal MIDI notes, or MPE per-note pitch-bend output |
| Normal MIDI Channel | 1..16 | 1 | Channel used when MIDI Output Type is Normal MIDI |
| MPE Pitch Bend Range | 4 choices | 48 st | Pitch-bend range for MPE member channels; the receiving synth must match |
| MPE Send Setup | on/off | on | Sends MPE lower-zone and bend-range RPN setup messages when needed |
| MPE Pitch Mode | 2 choices | Retrigger | Retrigger degree changes or glide by updating per-note pitch bend when possible |

## Performance UI

Version 1.0.5 redesigns the editor around a compact DAW-friendly layout:
always-visible top status bar, narrow library rail, audience map + musical
scale strip, and dense lower control bands for macros, texture, voices, network,
and simulator controls. It keeps the five signature sound modes and four macro controls
(`Energy`, `Motion`, `Tone`, `Space`), root/scale/range controls, `Mute`, and
`Panic`. The texture and voice panels now expose wet/dry, tape drive, freeze,
reverse grains, grain size, density, pitch spread, position jitter, stereo
spread, and grain envelope shape. The `Performance` toggle opens a large stage-readable overlay with
OSC state, active seats, active voices, current library, scale range, and
dominant note. The library rail includes search, rescan, cached sample counts,
and active-library status.

## Sample Libraries

Libraries live as subfolders under `Samples/`. Files are scanned from the
selected subfolder and may be `.wav`, `.aif`, `.aiff`, or `.flac`.

Sample root notes are parsed from filenames, for example:

```text
C2.wav
F#3.wav
Bb4.wav
```

At load time the library trims trailing silence in memory and keeps a short
fade-out tail. The Python tools can also prepare files on disk:

```sh
python3 trim_silence.py "Samples/Piano Dream"
python3 split_voice.py Sources/HumanVoices/HumanVoice.wav Samples/HumanLive C1 D1 E1 F1 G1 A1 B1
```

## `Source/` vs `Sources/`

Two top-level directories differ by a single trailing `s`. They are unrelated and
must not be confused:

- **`Source/`** (singular) — the C++/JUCE plugin source code (`PluginProcessor.cpp`,
  `PartialEngine.cpp`, the generated `ElementSpectralData.cpp`, etc.). This is what
  `CMakeLists.txt` compiles.
- **`Sources/`** (plural) — raw, unprocessed source recordings used as **input** to
  the Python prep tools, not loaded by the plugin at runtime. It currently holds
  `Lyre.wav` and `HumanVoices/HumanVoice.wav` (plus `.orig` backups and an Ableton
  `.asd` analysis sidecar).

The prep tools (`trim_silence.py`, `split_voice.py`) read raw audio from `Sources/`
and write playable, root-note-named libraries into `Samples/`. At runtime the plugin
only ever scans `Samples/` (the `AUDIENCE_SYNTH_SOURCE_SAMPLES_PATH` baked into the
build), never `Sources/`.

Do not rename `Sources/`: tool invocations and local asset paths reference it by name.

## Simulator

The editor includes a fake audience for testing without UDP traffic:

- Add Participant
- +25 Crowd
- Remove Participant
- Random Movement
- Clear All

## Architecture

```text
UDP / OSC / Simulator / UI Keyboard / MIDI Keyboard
  -> OscBridge
  -> PartialEngine lock-free event queue
  -> voice allocation
  -> shared pitch resolver
       -> Normal Scale
       -> Element Spectral Scale
  -> Engine Source
       -> Sample Library
            -> Sample Playback
                 -> Direct Sample Player
                 -> Granular Sample Engine
       -> Element Spectral Synth
  -> signature mode, reverb, delay, tape saturation, limiter
  -> audio output

Parallel MIDI path:
  -> shared pitch resolver
  -> MIDI Output Type
       -> Normal MIDI: nearest MIDI note on one channel
       -> MPE MIDI: nearest MIDI note + per-note pitch bend on member channels
  -> host MIDI output
```

The OSC callback only validates and enqueues seat events. The audio thread
drains those events, updates voices, renders the selected sample/spectral path
when audio is enabled, generates outgoing MIDI/MPE when MIDI is enabled, and
publishes lightweight UI readouts for the editor visualizer.

## Element Spectral Scale Clustering

Element Spectral Synth keeps two related but separate representations of each
atomic spectrum:

- Raw/timbre spectrum: every positive-intensity emission line is preserved as
  an additive timbre partial. Zero-intensity catalogue placeholders remain in
  the source dataset but are not treated as playable or audible spectral lines.
- Playable scale: nearby spectral lines can be clustered into a smaller set of
  playable scale degrees.

This means dense elements can become playable without deleting the physical
spectral fingerprint. The full raw spectrum remains available to the timbre
engine, while the scale view exposes a musically useful subset.

The element source data is copied from the Max/Cosmic Unity `data/*.txt`
datasets into this repository's `data/` folder. The plugin does not read those
text files from the audio thread. Instead, run:

```sh
python3 tools/generate_element_spectral_data.py
```

to regenerate `Source/ElementSpectralData.cpp` and
`Source/ElementSpectralData.h`. The generated C++ table is compiled into the
plugin and then prewarmed by `PartialEngine::prepare`, so runtime spectral
mapping remains realtime-safe.

For each element, the longest positive-intensity wavelength is the spectral
reference and maps to the played root:

```text
ratio_i = lambda_ref / lambda_i
cents_i = 1200 * log2(ratio_i)
cents_i = cents_i mod 1200
```

Pitch distance is measured in circular octave space, because `0 ct` and
`1200 ct` are the same pitch class:

```text
distance(a, b) = min(abs(a - b), 1200 - abs(a - b))
```

The builder first keeps the root line, then sorts the remaining lines by
log-compressed salience. A candidate line becomes a new scale degree only if it
is far enough from all selected degrees. Otherwise, it is later assigned to the
nearest selected degree as part of that degree's cluster.

Default scale-reduction modes:

| Atomic Scale Mode | Max degrees | Minimum separation | Use case |
|---|---:|---:|---|
| Core | 7 | 80 ct | sparse melodic performance |
| Extended | 12 | 40 ct | default playable atomic scale |
| Microtonal | 24 | 20 ct | denser microtonal performance |
| Scientific | 48 | 10 ct | high-detail inspection |
| Raw | unlimited | 0 ct | one degree per raw line |

Every raw line is assigned to exactly one scale-degree cluster. Each cluster
stores:

- representative wavelength
- representative cents and frequency
- total and maximum intensity
- cluster density
- cluster spread in cents
- source line IDs back to the raw dataset

The representative pitch defaults to a medoid: a real source line inside the
cluster with the smallest weighted circular distance to the other clustered
lines. This avoids inventing artificial average pitches while still choosing a
central, musically stable representative. The root cluster is special: the
longest wavelength always remains fixed at `0 ct`.

Timbre partials are not reduced:

```text
timbre_ratio_i = lambda_ref / lambda_i
timbre_amp_i   = intensity_i / max_intensity
partial_freq_i = played_root_hz * timbre_ratio_i
```

Scale-degree velocity is derived from cluster total intensity, but this is only
used for scale weighting and display. It does not replace the raw partial
amplitudes used by the timbre engine.

In short:

```text
source data     -> original catalogue rows, including silent placeholders
raw spectrum    -> positive-intensity emission lines
timbre partials -> all positive lines preserved for additive tone color
playable scale  -> clustered representative degrees for performance
```

## MIDI Scale Module

The Ableton-focused `SpektraSynth MIDI Generator` target includes a Scale MIDI
module before MIDI is sent to the host/external output. When enabled, note
events are locked to a selected root, scale type, and correction mode while
velocity, timing, channel, CC, pitch bend, aftertouch, and other non-note data
pass through unchanged.

Scale controls:

- Scale On
- Root: C through B
- Scale Type: Major, Natural Minor, Harmonic Minor, Melodic Minor, Major
  Pentatonic, Minor Pentatonic, Blues, Dorian, Phrygian, Lydian, Mixolydian,
  Locrian, Chromatic, Custom
- Correction: Nearest, Up, Down
- Pitch-class remap matrix: each input pitch class can be redirected to any
  output pitch class after scale correction and before transpose

Note-on mappings are stored per original note and MIDI channel, so note-offs
return to the exact remapped output note and avoid stuck notes. The Custom
scale data model is saved as a 12-bit mask parameter and can be edited from the
pitch-class pad strip. Version 1.0.17 also ports the Ableton-style prototype UI
into JUCE with the audience venue, note pulse, crowd controls, Scale editor,
I/O card, range card, and dual activity logs. Version 1.0.18 makes the activity
logs collapsible by default, keeps octave/range controls visible in DAW-sized
windows, and shows note names with octaves in the outgoing MIDI debug stream.
Version 1.0.19 adds a Scale Keyboard strip to the audio synth: on-screen keys
and the computer keyboard trigger the currently selected scale/spectral steps
directly without registering them as audience seats. Version 1.0.20 adds the
Element Spectral Synth engine: atomic element datasets can be rendered as
an additive partial bank, with the longest wavelength mapped to the played root
and relative intensity normalized as each line's amplitude. In this mode the
instrument no longer needs a sample library for tone generation; audience X/Y,
root, scale, range, macros, FX, and the keyboard strip continue to drive it.
Version 1.0.21 keeps timbre and scale separate: every raw spectral line
remains available as an additive timbre partial, while the playable atomic
scale can be reduced into Core, Extended, Microtonal, Scientific, or Raw degree
sets. Core favors a sparse 7-degree musical scale, Extended keeps up to 12
performable degrees, Microtonal and Scientific preserve progressively denser
pitch detail, and Raw exposes every spectral line as pitch material. Reduced
scale degrees keep traceable clusters back to the original lines, so dense
elements become playable without deleting spectral data. Version 1.0.22 adds
Partial Solo auditioning: the full atomic timbre remains the default, but the
PARTIAL control can isolate one raw spectral line at a time so individual
frequencies can be heard directly.
Version 1.0.23 adds the next two periodic elements, Lithium and Beryllium, to
both the Element Spectral Synth and the playable spectral-scale list.
Version 1.0.24 adds the next five available element datasets in formation/order
sequence: Boron, Carbon, Oxygen, Fluorine, and Neon. The source data folder does
not currently include Nitrogen, so it is intentionally left out until a matching
`N.txt` spectral dataset is available.
Version 1.0.25 separates Sample Library from Sample Playback: sample libraries
now default to Kontakt-style direct sample playback, while the existing granular
engine remains available behind the `Granular` playback mode.
Version 1.0.26 lets incoming MIDI notes play the same Scale Keyboard path as the
on-screen/computer keyboard: the current root MIDI note triggers scale step 1
and successive MIDI keys walk upward through the displayed scale/spectral steps.
Spectral keyboard keys also show stronger emission lines with brighter strength
bars, while Partial Solo auditions every raw line at equal loudness.
Version 1.0.27 adds MIDI/MPE output to the main audio instrument without
replacing the internal engine. Output can now be Audio Only, MIDI Only, or
Audio + MIDI. Normal MIDI emits nearest scale notes, while MPE uses member
channels with per-note pitch bend so Element/Atomic spectral cents are preserved
for compatible receiving synths.
Version 1.0.28 improves the performance debug overlay by moving outgoing
MIDI/MPE monitoring into the upper console area and active-seat diagnostics into
the wider lower pane. It also reduces duplicate MPE retrigger spam by treating
same-source, same-note, same-bend Note On events as expression updates instead
of forcing a Note Off / Note On cycle.
Version 1.0.29 improves external MIDI keyboard input: notes that fall outside
the scale-degree keyboard range now fall back to the nearest displayed
scale/spectral step instead of being ignored. This makes compact atomic scales,
such as Hydrogen Core, playable from normal MIDI keyboards even when a pressed
key is beyond the current degree count.
Version 1.0.30 improves spectral MIDI keyboard playability and polyphony. In
Element/Atomic Spectral Scale mode, incoming external MIDI notes are mapped onto
the same one-octave scale keyboard degrees shown in the UI, so compact scales
such as Lithium can be played from a normal C1-C2 keyboard octave without
collapsing held notes into a single nearest pitch.
Version 1.0.31 adds the next five available spectral elements after Neon:
Sodium, Magnesium, Aluminium, Silicon, and Phosphorus. Nitrogen remains omitted
until a matching local `N.txt` dataset is available.
Version 1.0.32 adds the next five available spectral elements after Phosphorus:
Sulfur, Chlorine, Argon, Potassium, and Calcium.
Version 1.0.33 adds the next five available spectral elements after Calcium:
Scandium, Titanium, Vanadium, Chromium, and Manganese.
Version 1.0.34 adds the next five available spectral elements after Manganese:
Iron, Cobalt, Nickel, Copper, and Zinc.
Version 1.0.35 publishes the Elemental Spektra UI/review pass: spectral line
activity now drives synchronized Wavelength Wheel and Scale Keyboard
highlighting from active voice snapshots, element spectra can be browsed from
the left library rail, and the Max-derived spectral data/generator remain
tracked for reproducible element tables.
