# SpektraSynth WIKI

> **A spectral instrument that turns elemental emission lines into playable scales, tunings, and timbres.**

| Field | Value |
|---|---|
| Project | SpektraSynth (CMake project `AudienceHarmonicSynth`) |
| Version | 1.0.35 (`CMakeLists.txt` `project(... VERSION 1.0.35)`) |
| Framework | JUCE 8.0.4 (pinned via CMake FetchContent), C++17 |
| Repository | `https://github.com/MehmetUnal-Studio/AudienceHarmonicSynth` |
| Branch documented | `spektrasynth-overhaul` (PR #1, open) |
| Document purpose | Single self-contained reference for AI models and developers: concept, translation pipeline, architecture, parameters, MIDI/MPE, OSC, build/test |
| Document date | 2026-06-12 |
| Source of truth | The repository source code. Where `README.md`, `AGENTS.md`, or `docs/SpektraSynth-Mimari-Harita.md` disagree with code, **the code wins**; such discrepancies are flagged inline. |

## Table of contents

1. [What is SpektraSynth](#1-what-is-spektrasynth)
2. [Scientific framing & artistic licence](#2-scientific-framing--artistic-licence)
3. [The translation pipeline: emission lines to audible music](#3-the-translation-pipeline-emission-lines-to-audible-music)
4. [Products & artifacts](#4-products--artifacts)
5. [Architecture](#5-architecture)
6. [Parameters reference (APVTS)](#6-parameters-reference-apvts)
7. [MIDI & MPE behaviour](#7-midi--mpe-behaviour)
8. [OSC protocol](#8-osc-protocol)
9. [Build & test](#9-build--test)
10. [Repository map](#10-repository-map)
11. [Glossary](#11-glossary)
12. [Version & history](#12-version--history)

---

## 1. What is SpektraSynth

**Headline:** *A spectral instrument that turns elemental emission lines into playable scales, tunings, and timbres.*

SpektraSynth is a VST3 + Standalone JUCE instrument with two intertwined ideas. First, it is an **audience-driven instrument**: every participant in a crowd is identified by a seat position (row letter `A..Z` + column number `0..99`, up to 2,600 seats), and each phone's X/Y touch data — arriving over OSC/UDP — triggers and shapes a voice, so a crowd becomes a collective harmonic texture. Second, it is a **spectral translation engine**: the emission-line spectra of 29 chemical elements (Hydrogen through Zinc, Nitrogen omitted — see [§3.1](#31-source-data)) are translated into microtonal *playable scales* and additive *timbral fingerprints*. The X axis of each touch is quantized either to a conventional musical scale (Major, Dorian, etc.) or to an element's spectral scale; the Y axis drives amplitude. Sound generation is selectable between a sample library (direct or granular playback) and the **Element Spectral Synth**, an additive oscillator bank whose partials are the element's emission lines. A parallel MIDI/MPE output path can drive external synthesizers with the same microtonal pitches, preserved via per-note pitch bend. A companion artifact of the same pipeline is the **"Atomic CORE WhiteKeys"** Omnisphere-compatible `.tun` tuning library ([§4.2](#42-companion-release-atomic-core-whitekeys-tun-tuning-library)).

---

## 2. Scientific framing & artistic licence

**Editorial position (authoritative for all derived copy, UI text, and marketing):** SpektraSynth must **not** be described as "the real musical sound of atoms," "what atoms actually sound like," or any equivalent claim. That framing is scientifically contentious, because making spectra audible necessarily involves creative and musical decisions. The correct framing is **translation**: *atomic spectra are translated into playable musical scales and timbral fingerprints.* The translation is grounded in real measured data and is deterministic and reproducible, but it is a designed mapping, not a discovery of inherent atomic music.

What is **physical data** versus a **creative/musical mapping** in this project:

| Step | Status | Notes |
|---|---|---|
| Emission-line wavelengths and relative intensities (`data/*.txt`) | **Physical data** | NIST-style wavelength (nm) + relative-intensity catalogues per element. |
| Wavelength → frequency ratio (`lambda_ref / lambda_i`) | **Physical relationship** | Ratios between lines are intrinsic to the spectrum. |
| Choosing the **longest** positive-intensity wavelength as the root | **Creative decision** | Any line could serve as a reference; "longest wavelength = root" is a design rule (`AtomicScaleBuilder.cpp`). |
| **Octave reduction** (folding ratios into one octave, mod 1200 cents) | **Creative decision** | Visible-light frequencies are in the hundreds of THz, ~40 octaves above hearing. Folding by octave equivalence is a musical convention borrowed from pitch-class theory; physically, the folded intervals are not the original intervals. |
| Mapping the root to a **player-chosen root note** (default C2) | **Creative decision** | The absolute pitch of every element scale is set by the performer, not by the atom. |
| **Clustering** lines into 7/12/24/48 scale degrees, minimum-separation rules, medoid representative | **Creative decision** | Perceptual/ergonomic reduction so dense spectra become playable; parameterized by `ScaleMode`. |
| Degree "velocity" from cluster total intensity | **Creative decision** (data-derived) | A display/weighting choice. |
| Additive timbre: partial frequencies at `root_hz * (lambda_ref / lambda_i)`, amplitudes `intensity_i / max_intensity` | **Data-derived mapping** | Preserves every positive line's relative position and strength, but the transposition into the audible band, the linear intensity→amplitude rule, brightness tilt, and level normalization are sound-design choices. |
| Sample/granular engines, FX chain, signature modes, macros | **Pure sound design** | No physical claim whatsoever. |

A correct one-sentence description: *"SpektraSynth translates the measured emission-line spectrum of an element into (a) a microtonal scale, by octave-reducing each line's ratio to the element's longest-wavelength line and clustering the results into playable degrees, and (b) an additive timbre, by treating every line as a partial above the played root."* Statements implying the result is the element's inherent sound are incorrect and must be avoided.

---

## 3. The translation pipeline: emission lines to audible music

The entire spectral math lives in `Source/AtomicScaleBuilder.h/.cpp` (a stateless static utility class) and is consumed by both the audio engine and the MIDI/MPE output through one shared pitch resolver (`PartialEngine::getScalePitch`, `Source/PartialEngine.cpp`). This satisfies the rule in `AGENTS.md`: *"The audio engine and MIDI/MPE engine must share the same pitch resolver."*

### 3.1 Source data

- `data/<symbol>.txt` — plain-text catalogues, one line per emission line: `wavelength_nm intensity` (whitespace-separated; `#` comments and blank lines skipped). Example, `data/H.txt` (6 rows): `656.279 1.00000000` is the strongest line (H-alpha). The repo's `data/` folder holds **91** element files copied from the Max/Cosmic Unity datasets, but only **29** are compiled in.
- `tools/generate_element_spectral_data.py` — generator script with a hardcoded `ELEMENTS` list of 29 `(symbol, name)` pairs: H, He, Li, Be, B, C, O, F, Ne, Na, Mg, Al, Si, P, S, Cl, Ar, K, Ca, Sc, Ti, V, Cr, Mn, Fe, Co, Ni, Cu, Zn. That is atomic numbers 1–30 **with Nitrogen (N, Z=7) intentionally omitted** because no matching `N.txt` dataset is available locally (documented in `README.md`).
- The script emits `Source/ElementSpectralData.h` and `Source/ElementSpectralData.cpp` (~20.7k lines, git-tracked). Each row becomes an `AtomicScaleBuilder::SourceLine { id, label, wavelength, Unit::Nanometer, intensity }`, with `id` like `"B-0008-412.193"` and `label` like `"B 412.193 nm"`. The generated table is compiled into the plugin so **no file I/O ever happens on or near the audio thread**; CI verifies the generated file matches the generator output ([§9.4](#94-generated-data-sync)).
- `ElementSpectralData` API (`Source/ElementSpectralData.h`): `numSupportedElements()` (=29), `symbolForElement(i)`, `nameForElement(i)`, `linesForElement(i)`.
- Zero-intensity catalogue placeholders are kept in the source data but are filtered out by the builder (`intensity <= 0.0` lines are skipped), so they are never playable or audible.

### 3.2 Two representations per element: raw timbre vs playable scale

The builder produces **both** in one `AtomicScaleBuilder::Result`:

- **Raw/timbre spectrum** (`rawLines`, `timbrePartials`): *every* positive-intensity emission line, preserved un-reduced as additive partials. This is the element's timbral fingerprint.
- **Playable scale** (`scaleDegrees`): nearby lines clustered into a smaller set of degrees for performance.

Dense elements (e.g. Iron, 4,041 raw lines) therefore become playable without deleting spectral information: the scale view exposes a musically useful subset while the full spectrum stays available to the timbre engine.

### 3.3 Octave reduction (the core math)

For each element (`AtomicScaleBuilder::buildPlayableAtomicScale`):

1. Normalize wavelengths to nm: `normalizeWavelengthNm` multiplies Angstrom values by 0.1; if the unit is unknown, values > 1000 are interpreted as Angstroms (heuristic; the generated data is always tagged `Nanometer`).
2. Drop non-finite/non-positive wavelengths and non-positive intensities.
3. Pick the **reference wavelength** `lambda_ref` = the *longest* surviving wavelength. Its line is the **root** and is pinned to exactly `0` cents.
4. For every line *i*:

```text
ratio_i = lambda_ref / lambda_i              (>= 1, since lambda_ref is longest)
cents_i = normalizedCents(1200 * log2(ratio_i))   # folded into [0, 1200)
```

`normalizedCents` is `fmod(c, 1200)` shifted into `[0, 1200)` with a 1e-9 snap of ~1200 back to 0. This is **octave reduction**: every line is collapsed to a pitch class within a single octave above the root. Pitch distance is then always measured in **circular octave space**, because 0 ct and 1200 ct are the same pitch class:

```text
circularDistanceCents(a, b) = min(|a - b| mod 1200, 1200 - |a - b| mod 1200)
```

5. Per-line weights:

```text
salience_i  = log1p(intensity_i) / max_j log1p(intensity_j)   # log-compressed, 0..1
timbreAmp_i = intensity_i / max_intensity                     # linear, 0..1
```

`salience` (perceptual, log-compressed) drives *scale-degree selection*; `timbreAmp` (linear) drives *additive amplitudes*. They are deliberately different.

### 3.4 Scale modes (degree caps and minimum separation)

`AtomicScaleBuilder::ScaleMode` enum names vs the UI names exposed by the `atomicScaleMode` APVTS parameter (mapping in `Source/PartialEngine.cpp`, `scaleModeForAtomicIndex` / `atomicScaleModeName`):

| APVTS index | UI name | Enum name (`AtomicScaleBuilder::ScaleMode`) | Max degrees | Min separation | Intended use |
|---:|---|---|---:|---:|---|
| 0 | `Core` | `Melodic` | 7 | 80 ct | sparse melodic performance |
| 1 | `Extended` (default) | `Performable` | 12 | 40 ct | default playable atomic scale |
| 2 | `Microtonal` | `Microtonal` | 24 | 20 ct | denser microtonal performance |
| 3 | `Scientific` | `Scientific` | 48 | 10 ct | high-detail inspection |
| 4 | `Raw` | `Raw` | unlimited (`INT_MAX`) | 0 ct | one degree per raw line |

Defaults come from `defaultMaxScaleDegrees()` / `defaultMinSeparationCents()`; `Options.maxScaleDegrees`/`minSeparationCents` can override them (the engine uses the defaults).

### 3.5 Degree selection (salience-ranked greedy anchor pick)

For non-`Raw` modes:

1. If `Options.alwaysIncludeRoot` (true in the engine), the root line is selected first.
2. All other lines are sorted by: `salience` desc → `intensity` desc → `cents` asc → `wavelengthNm` desc (deterministic tie-breaking; `meaningfullyDifferent` uses a 1e-12 epsilon).
3. Walk the sorted candidates; accept a candidate as a new degree anchor only if its circular distance to **every** already-selected anchor is `>= minSeparationCents`, until `maxDegrees` anchors exist.
4. If nothing was selected and candidates exist, the top candidate is force-selected.

In `Raw` mode every raw line is its own degree (no clustering).

### 3.6 Clustering and the medoid representative

Every raw line (not just the selected ones) is assigned to the **nearest selected anchor** by circular distance — so *every* line belongs to exactly one cluster, and every cluster traces back to source-line IDs.

Each cluster then chooses a **representative pitch** per `Options.representativeMode` (`RepresentativeMode` enum):

- **`Medoid` (the engine's default, set in `buildAtomicResult`)** — the *actual source line* inside the cluster with the smallest salience-weighted mean circular distance to all other cluster members (tie broken toward higher salience). This avoids inventing artificial average pitches; the representative is always a real measured line.
- `Strongest` — highest-intensity member.
- `WeightedMean` — circular (vector) mean of cluster cents, then snapped to the nearest real member.

Special cases:

- **Root cluster:** the cluster containing the root line always uses the root as representative, pinned to `0.0` ct, so the element root reliably maps to the chosen root note.
- **Spacing guard:** if a chosen representative would land closer than `minSeparationCents` to *another* selected anchor, the cluster falls back to its originally selected anchor line — the playable-scale spacing constraint outranks the representative heuristic.

Per-degree statistics stored in `ScaleDegree`: `cents`, `frequencyHz`, `velocity`, `representativeLineId` / `representativeWavelengthNm` / `representativeIntensity`, `clusterTotalIntensity`, `clusterMaxIntensity`, `clusterDensity` (member count), `clusterSpreadCents` (salience-weighted RMS of signed circular deltas), `sourceLineIds`, `sourceLineIndices`. Degrees are finally sorted ascending by cents and indexed (`degreeIndex`).

**Degree velocity** = `clusterTotalIntensity / max cluster total` (0..1). It is used for scale weighting and UI strength bars only — it does **not** replace raw partial amplitudes in the timbre engine.

> **Caveat (verified in code):** `ScaleDegree.frequencyHz` is computed from `Options.rootHz` (default `130.8128` Hz = C3) inside the builder, but the runtime pitch resolver `PartialEngine::getScalePitch` **ignores it** and recomputes frequency from the live `scaleRootMidi` parameter: `hz = midiToHz(scaleRootMidi) * 2^(cents/1200) * 2^octave`. Treat `ScaleDegree.frequencyHz` as informational/debug only.

### 3.7 Timbre partials (additive synthesis)

Timbre partials are **never reduced**. For every positive line:

```text
timbre_ratio_i = lambda_ref / lambda_i        # NOT octave-folded
timbre_amp_i   = intensity_i / max_intensity
partial_freq_i = played_root_hz * timbre_ratio_i
```

The Element Spectral Synth voice (`PartialEngine::renderVoices`, spectral branch) renders up to `MAX_ELEMENT_PARTIALS = 512` partials per voice via a shared **4096-point sine lookup table with linear interpolation** (`kSineTable`, with a guard entry for branch-free interpolation — replacing the previous per-sample `std::sin`, PR #1). Additional shaping, all sound-design (not physics):

- `spectralStretch` ∈ [-0.35, +0.35] warps ratios as `ratio^(1 + stretch)`.
- A brightness tilt per partial from `brightness`, the `Tone` macro, and the signature-mode profile.
- Level normalization `baseLevel = 0.34 / sqrt(partials)` (0.42 in solo).
- `spectralPartialCount` ("Element Partial") limits how many of the raw lines are audible in normal mode (`1..512`, default 512 = all); with `spectralPartialSolo` ("Partial Solo") on, it instead selects **which single raw line** is auditioned, at equal loudness. (The non-solo limiting behaviour was made functional in the overhaul — it was previously inert.)

### 3.8 The playable grid: X → pitch

`PartialEngine::getScalePitch(idx)` is the single resolver shared by audio, on-screen/computer keyboard, external MIDI mapping, and MPE output:

- **Spectral scale modes** (`scaleMode` indices 7..35, i.e. "Hydrogen Spectrum".."Zinc Spectrum"): the scale table is `octaves x degreeCount` entries; entry `idx` selects octave `idx / degreeCount` and degree `idx % degreeCount`; frequency = `midiToHz(scaleRootMidi) * 2^(cents/1200) * 2^octave`. The returned `PitchTarget` carries the exact `frequencyHz`, the nearest MIDI note (for sample selection/display), a unique retrigger key (`10000 + mode*1000 + idx`), and `velocityGain` = degree velocity (or 1.0 in Partial Solo).
- **Tonal modes** (indices 0..6: Major, Natural Minor, Pentatonic, Dorian, Lydian, Harmonic Minor, Whole Tone): plain 12-TET degrees from `scaleRootMidi`.
- `xToPitch(x)` maps touch X ∈ [0,1) linearly onto the scale table.

A cache of all **29 elements x 5 scale modes = 145** prebuilt `Result` objects (`AtomicScaleCache`, `Source/PartialEngine.cpp`) is built **once on the message thread** (engine constructor + `prepare()`, explicitly owned via `std::unique_ptr` — moved out of a function-local magic-static in the overhaul) so the audio thread only ever reads.

### 3.9 Worked examples (from the shipped tuning-library manifest)

- **Hydrogen, Core mode:** 6 raw positive lines, `lambda_ref = 656.279 nm` (H-alpha) → only **5** degrees survive the 80 ct minimum separation: `0.0, 519.5, 715.7, 813.7, 905.9` ct.
- **Boron, Core mode:** 74 raw positive lines, `lambda_ref = 678.612 nm` → 7 degrees at `0.0, 177.4, 322.3, 472.3, 605.2, 721.7, 863.1` ct (mapped to C D E F G A B in the tuning-file release, [§4.2](#42-companion-release-atomic-core-whitekeys-tun-tuning-library)).

### 3.10 Pipeline summary

```text
source data      -> original catalogue rows, incl. zero-intensity placeholders (data/*.txt)
raw spectrum     -> positive-intensity emission lines                 (Result.rawLines)
timbre partials  -> ALL positive lines preserved for additive tone    (Result.timbrePartials)
playable scale   -> octave-reduced, clustered representative degrees  (Result.scaleDegrees)
```

---

## 4. Products & artifacts

### 4.1 Build targets (`CMakeLists.txt`)

| CMake target | Product name | Plugin code | Formats | Role |
|---|---|---|---|---|
| `AudienceHarmonicSynth` | **SpektraSynth** | `Ahss` (manufacturer `Mhmt`) | VST3 + Standalone | Flagship audio instrument: seats engine, sample/granular/spectral synthesis, MIDI/MPE output. The only target that compiles `PluginProcessor.cpp` + `PartialEngine.cpp`. |
| `AudienceHarmonicMidi` | SpektraSynth MIDI | `Ahmd` | VST3 + Standalone | MIDI-effect plugin: non-MPE audience MIDI generator (built on `MidiProcessor`/`MidiEngine`). |
| `AudienceMidiGenerator` | SpektraSynth MIDI Generator | `Amgn` | VST3 | Ableton-focused MIDI generator with the Scale MIDI module (`MidiScaleModule`: 12-TET scale lock, 14 scale types incl. Custom 12-bit mask, Nearest/Up/Down correction, pitch-class remap matrix, per-note/channel note-off mapping to avoid stuck notes). |
| `AudienceMidiDevice` | SpektraSynth MIDI Device | (GUI app) | Standalone app | Simple UDP→MIDI bridge application (`AudienceMidiDeviceApp.cpp`). |

Key build facts: macOS deployment target 10.13; on macOS the build copies `Samples/` into each bundle's `Contents/Resources/Samples`, ad-hoc codesigns, and (option `AUDIENCE_SYNTH_AUTO_INSTALL`, default ON) auto-installs the synth VST3 into `~/Library/Audio/Plug-Ins/VST3/`. An `if(ANDROID)` path still exists in CMake (Standalone-only, version 1.0.1, 5 grains/voice instead of 8), though the separate Android Gradle project was removed in the overhaul branch. Release zips live under `releases/` (not part of the build).

**Important naming boundary** (verified against code; the architecture map repeats it): MPE functionality lives **only** in the flagship `AudienceHarmonicSynth` target (`AudienceProcessor` + `MpeMidiOutput`). `MidiEngine`/`MidiProcessor` (the other three targets) emit plain notes + CCs (CC1/CC11/CC74/CC91/CC93) with channel modes Single/PerParticipant/PerRow/PerColumn — **no** pitch bend, RPN, or pressure.

### 4.2 Companion release: "Atomic CORE WhiteKeys" `.tun` tuning library

A sibling product of the **same translation pipeline**, released as an **Omnisphere-compatible tuning-file library** (AnaMark `.tun` format). It lives **outside this repository**, in the user's Omnisphere installation: `…/Omnisphere/Settings Library/Presets/Tuning File/Atomic/` — 29 files, one per element (`Hydrogen_CORE_WhiteKeys.tun` … `Zinc_CORE_WhiteKeys.tun`) plus `Atomic_CORE_WhiteKeys_manifest.txt`.

Concept (from the manifest, verbatim rules):

- **`CORE: max 7 degree, min 80 cent, medoid, root included`** — i.e. each element's scale is the Core/`Melodic` build of [§3.4](#34-scale-modes-degree-caps-and-minimum-separation).
- **`Mapping: degrees 1-7 -> C D E F G A B. Degree 8+ -> nearest free black key.`** (With the 7-degree Core cap, degree 8+ does not occur; the rule exists for safety.)
- **`Unassigned keys duplicate the nearest assigned atomic degree across octave copies.`** — black keys are never silent or detuned arbitrarily; e.g. in Boron, C#=177 ct duplicates D, D#=322 ct duplicates E.
- Example manifest row: `B Boron degrees=7 raw=74 lambda_ref=678.612 nm mapping C:0.0, D:177.4, E:322.3, F:472.3, G:605.2, A:721.7, B:863.1` (cents from root).

File format: AnaMark TUN — a commented header (element, CORE rules, source file, `lambda_ref`, per-degree `cents / representative nm / cluster size` table) followed by `[Tuning]` and `[Exact Tuning]` sections containing `note 0=` … `note 127=` absolute cents values (root C at note 0 = `0`; each octave adds 1200; values rounded to integer cents). Example (Boron): `note 0= 0`, `note 1= 177`, `note 2= 177`, … `note 12= 1200`.

"White-key-first" design intent: a keyboard player gets the element's Core scale on the white keys in every octave, with black keys safely doubling neighbours — playable without any microtonal controller. The related open interchange format for such scales is **Scala (`.scl`)**; `.tun` was chosen here because Omnisphere loads AnaMark TUN files directly. The `.tun` exporter itself is not part of this repository; the files are generated from the same `data/*.txt` + Core-mode clustering rules documented above (the manifest numbers match `AtomicScaleBuilder` output).

---

## 5. Architecture

### 5.1 Subsystems

| Subsystem | Responsibility | Key files | Depends on |
|---|---|---|---|
| DSP / Spectral Synthesis Engine | Turns seat input into voices; renders additive partial bank, direct/granular sample playback, FX chain | `PartialEngine.h/.cpp` (~4.3k lines) | `AtomicScaleBuilder`, `ElementSpectralData`, `SampleLibrary`, `SeatEventSink`, JUCE |
| Microtonal Scale System | Spectrum → cents → degrees + timbre partials; separate 12-TET quantizer for the MIDI generator | `AtomicScaleBuilder.h/.cpp`, `MidiScaleModule.h/.cpp`, `MidiPitch.h` | `ElementSpectralData`, JUCE |
| MIDI / MPE Output Engine | MPE/Normal MIDI emission state machine (flagship synth only) | `MpeMidiOutput.h/.cpp`, `MidiPitch.h`, glue in `PluginProcessor.cpp` | `PartialEngine` event types (decoupled via `NoteEvent`) |
| Plugin Core | APVTS (47 parameters), `pullParams` → engine atomics, `processBlock`, state save/restore, external MIDI port, debug rings | `PluginProcessor.h/.cpp` | `PartialEngine`, `MpeMidiOutput`, `OscBridge`, `Simulator` |
| UI / Visualization | Editor (~70 controls), Aurora seat-map/spectral visualizer, library rail, debug panel; separate MIDI-generator editor | `PluginEditor.*`, `AuroraComponent.*`, `LibraryRail.*`, `DebugPanel.*`, `MidiGeneratorEditor.*`, `UiText.h` | APVTS attachments + lock-free getters |
| Networking, Samples & Seat Model | OSC/UDP ingress (shared port, fan-out), fake-audience simulator, sample loading | `SeatEventSink.h`, `OscWireFormat.h`, `OscBridge.*`, `Simulator.*`, `SampleLibrary.*` | JUCE osc/audio_formats |
| Build / Apps / Test | CMake (4 products + 9 test targets), UDP→MIDI app, Python codegen, CI | `CMakeLists.txt`, `AudienceMidiDeviceApp.cpp`, `Tests/*`, `tools/*`, `.github/workflows/ci.yml` | all of the above |

The central abstraction is `SeatEventSink` (`Source/SeatEventSink.h`): `MAX_ROWS=26`, `MAX_COLS=100`, `MAX_SEATS=2600`, with the pure interface `setX/setY/setOn(row, col, …)`. Every input source (OSC, Simulator, UI keyboard, external MIDI) writes into this interface; the consumer differs per target (synth: `PartialEngine` via a `DualSeatRouter` adaptor; MIDI targets: `MidiEngine`).

### 5.2 Data flow (flagship synth)

```text
UDP/OSC  /cs/<row>/<col>/finger<n>/{on|off|line|v}      Simulator / UI keyboard / external MIDI
   -> OscBridge::SharedPort (OSC realtime thread)            -> same SeatEventSink interface
   -> lock-free fan-out (<=16 clients per UDP port)
   -> SeatEventSink::setX/setY/setOn  ->  PartialEngine
        per-seat atomics (lastX/lastY/active) + VoiceEvent -> eventFifo (juce::AbstractFifo, 8192)
   --- AUDIO THREAD BOUNDARY ---
   -> AudienceProcessor::processBlock
        pullParams (APVTS -> ~40 engine atomics)
        PartialEngine::render()
          drainEvents -> handleEvent:
            On       -> maybeTrigger(force)
            XChange  -> maybeTrigger   [hysteresis: max(8ms, minTriggerMs * (1.15 - 0.55*Energy)); X change REPLACES the seat's note]
            YChange  -> targetAmp = Y * layerMix * (0.70 + 0.65*Energy) * modeFactor
            Off      -> voices -> releasing
          maybeTrigger -> getScalePitch/xToPitch -> PitchTarget {midi, key, frequencyHz, velocityGain}
                       -> allocateVoice x (1..3 adaptive unison per seat)
          renderVoices  (per voice: Element Spectral additive bank | Direct sample | Granular grains)
          applyReverb -> applyDelay -> wet/dry -> master -> applyTapeSaturation -> applyLimiter
   -> stereo audio out + 96 aurora band atomics + per-voice UI atomics

Parallel MIDI path (same block):
   PartialEngine writes MidiSourceEvent {NoteOn|NoteOff|Expression|AllNotesOff, sourceId, frequencyHz, velocity, x, y}
     into midiEventFifo (8192); sourceId = row*100+col (seats 0..2599) or 2600..2663 (keyboard slots)
   -> AudienceProcessor::renderOutgoingMidi: drain <=512 events/block -> MpeMidiOutput::render
        -> Normal MIDI: nearest note on one channel
        -> MPE: per-note channel + pitch bend (see §7)
   -> host MIDI buffer  AND  (optional) external MIDI queue (SPSC fifo 8192)
        -> 60 Hz message-thread timer drains to juce::MidiOutput (audio thread never touches the device)
```

Voice/engine capacities (`PartialEngine.h` constants): `MAX_VOICES` 1024 (polyphony modes Normal 256 / High 512 / Ultra 1024), 3-voice unison per seat that folds down adaptively as the crowd grows, `MAX_KEYBOARD_SLOTS` 64, `GRAINS_PER_VOICE` 8 (5 on Android), `MAX_DELAY_SAMPLES` 96000, `AURORA_BANDS` 96. An **active-voice index list** (overhaul) lets `renderVoices` iterate only live voices instead of sweeping all 1024.

### 5.3 Threading / realtime model

Three thread contexts; the hard rules are codified in `AGENTS.md` (no allocation, locks, file/network I/O, `juce::String`, or MessageManager on the audio thread):

| Thread | Runs | Discipline |
|---|---|---|
| Audio thread | `processBlock`, `PartialEngine::render`, FX, `renderOutgoingMidi`/`MpeMidiOutput::render` | No locks, no allocation; `ScopedNoDenormals`; all scratch buffers sized in `prepareToPlay`; reads the prebuilt 145-entry scale cache only |
| Message thread | `prepare`, UI, state save/load, `setSampleDirectory` (wrapped in `suspendProcessing`), Simulator (~30 Hz timer), 60 Hz timer that drains the external-MIDI fifo and debug snapshots | Disk I/O only here |
| OSC realtime thread | `SharedPort::oscMessageReceived` (JUCE `RealtimeCallback`) | Allocation-free address parse (`OscWireFormat.h`); lock-free client fan-out |

Boundary mechanisms: `juce::AbstractFifo` queues (producers take a `CriticalSection` among themselves; the audio-thread consumer never blocks), per-field `std::atomic` seat/voice state (mostly relaxed ordering), sequence-stamped ring buffers (release/acquire) for MIDI debug, and atomics for the 96-band aurora visualization. Message-thread mutations that touch audio-thread state (sample loads, immediate all-notes-off to the external port) are guarded with `suspendProcessing(true/false)`.

---

## 6. Parameters reference (APVTS)

All host-visible parameters are created in `AudienceProcessor::createLayout()` (`Source/PluginProcessor.cpp`). There are **47 parameters**, all with `ParameterID(id, 1)` versioning.

> **Stability rule (from `AGENTS.md`, binding):** existing APVTS parameter IDs and the 4-char plugin codes must **never change** — sessions and presets depend on them. New parameters must use stable IDs, defaults, and migration fallbacks. Note `mpeMasterChannel`/`mpeMemberFirstChannel`/`mpeMemberLastChannel` are now *inert* (the MPE zone derives the layout, [§7.2](#72-zones--channel-layout)) but **remain in the layout purely for session compatibility**.

| ID | Name | Type / range | Default | Effect |
|---|---|---|---|---|
| `pitch` | Pitch (semitones) | float −12..12 | 0 | Global transpose |
| `layerMix` | Layer Mix | float 0..1 | 0.7 | Per-seat voice amplitude scale |
| `attack` | Attack | float 10..3000 ms | 800 | Voice fade-in |
| `release` | Release | float 100..6000 ms | 2500 | Voice release |
| `brightness` | Brightness | float 0..1 | 0.6 | Low-pass response from X; also tilts spectral partials |
| `movement` | Movement (Grain) | float 0..1 | 0.45 | Grain density/length/spread modulation |
| `reverb` | Reverb | float 0..1 | 0.35 | Global reverb send |
| `delay` | Delay | float 0..1 | 0.25 | Cross-feedback delay amount |
| `master` | Master | float 0..1 | 0.7 | Output gain before limiter |
| `energy` | Energy | float 0..1 | 0.5 | **Macro:** level + trigger responsiveness (scales the per-seat retrigger hysteresis and Y→amp gain) |
| `motionMacro` | Motion | float 0..1 | 0.5 | **Macro:** organic movement / grain spread; also feeds MPE CC74 mix |
| `toneMacro` | Tone | float 0..1 | 0.5 | **Macro:** filter/ambience brightness |
| `spaceMacro` | Space | float 0..1 | 0.5 | **Macro:** reverb + delay depth |
| `signatureMode` | Signature Mode | choice x5 | Choir Cloud | Engine personality presets: Choir Cloud, Glass Harmonics, Sub Swarm, Spectral Rain, Frozen Hall (`kModes` profile table) |
| `grainSize` | Grain Size | float 40..800 ms | 260 | Base grain length |
| `grainDensity` | Grain Density | float 0..1 | 0.55 | Grain overlap per voice |
| `pitchSpread` | Pitch Spread | float 0..12 st | 0 | Scale-quantized grain pitch drift |
| `positionJitter` | Position Jitter | float 0..1 | 0.35 | Random grain start offset |
| `stereoSpread` | Stereo Spread | float 0..1 | 0.45 | Random per-grain stereo placement |
| `reverseGrains` | Reverse Grains | bool | off | Some grains play backwards |
| `freeze` | Freeze | bool | off | Holds grain clouds + freezes reverb tail |
| `grainShape` | Grain Envelope | choice x4 | Hann | Hann, Triangle, Soft Gate, Pulse |
| `wetDry` | Wet Dry | float 0..1 | 0.85 | Dry voices vs FX return |
| `tapeDrive` | Tape Drive | float 0..1 | 0 | Tape-style saturation pre-limiter |
| `polyphonyMode` | Polyphony Mode | choice x3 | Normal | Normal 256 / High 512 / Ultra 1024 voices |
| `engineSource` | Sound Engine | choice x2 | Sample Library | Sample Library / Element Spectral Synth |
| `samplePlaybackMode` | Sample Playback | choice x2 | Sample Player | Direct sample player / Granular |
| `audioMidiOutputMode` | Audio MIDI Output Mode | choice x3 | Audio Only | Audio Only / MIDI Only / Audio + MIDI |
| `midiOutputType` | MIDI Output Type | choice x3 | Off | Off / Normal MIDI / MPE MIDI |
| `externalMidiPitchMode` | External MIDI Pitch Mode | choice x3 | Direct MIDI Pitch | Direct / Quantize To Current Scale / Use As Trigger For Audience Pitch |
| `mpeZone` | MPE Zone | choice x2 | Lower | Lower (master 1, members 2–16) / Upper (master 16, members 1–15). **Owns the whole channel layout** (overhaul B8) |
| `normalMidiChannel` | Normal MIDI Channel | choice 1..16 | 1 | Channel for Normal MIDI output |
| `mpeMasterChannel` | MPE Master Channel | int 1..16 | 1 | **Inert** (zone-derived); kept for session compatibility |
| `mpeMemberFirstChannel` | MPE First Member Channel | int 2..16 | 2 | **Inert** (zone-derived); kept for session compatibility |
| `mpeMemberLastChannel` | MPE Last Member Channel | int 2..16 | 16 | **Inert** (zone-derived); kept for session compatibility |
| `mpePitchBendRange` | MPE Pitch Bend Range | choice {2, 12, 24, 48} st | **2 st** | Bend range for member channels; receiver must match ([§7.4](#74-pitch-math)) |
| `mpeSendSetupMessages` | MPE Send Setup Messages | bool | on | Emit MPE zone (MCM) + bend-range RPNs when needed |
| `mpePitchMode` | MPE Pitch Mode | choice x2 | Retrigger | Retrigger vs Glide (update bend for same note) |
| `spectralElement` | Element | choice x29 | **Helium** (index 1) | Element fingerprint for the spectral synth (H..Zn, no N) |
| `spectralPartialCount` | Element Partial | int 1..512 | 512 | Non-solo: limits audible partial count; solo: selects the auditioned line |
| `spectralPartialSolo` | Partial Solo | bool | off | Audition one raw line at equal loudness |
| `spectralStretch` | Spectral Stretch | float −0.35..0.35 | 0 | Warps partial ratios `ratio^(1+stretch)` |
| `atomicScaleMode` | Atomic Scale Mode | choice x5 | **Extended** (index 1) | Core / Extended / Microtonal / Scientific / Raw ([§3.4](#34-scale-modes-degree-caps-and-minimum-separation)) |
| `scaleRoot` | Root | choice C..B | C | Root pitch class; `rootMidi = (octave+1)*12 + pitchClass` |
| `scaleRootOctave` | Root Octave | choice 0..6 | 2 (→ root C2, MIDI 36) | Root octave |
| `scaleMode` | Scale | choice x36 | Major | 7 tonal scales + 29 "<Element> Spectrum" scales |
| `scaleOctaves` | Octaves | int 1..6 | 4 | X-axis range in octaves |

> **Doc-vs-code discrepancies (code wins):** `README.md`'s table and `AGENTS.md` state the MPE bend-range default is 48 st; the code's APVTS default is choice index 0 = **2 st**, with an in-source rationale: spectral degrees are emitted as nearest-12-TET note + bend with offsets always ≤ ±50 cents, so 2 st (the universal MPE default) is sufficient *and* survives receivers that ignore the bend-range RPN; wider ranges only matter for large Glide-mode slides.

Non-APVTS persisted state (`getStateInformation`): `udpPort` (default 6060), current sample library name, MIDI output option index.

---

## 7. MIDI & MPE behaviour

All output emission lives in `Source/MpeMidiOutput.h/.cpp` (extracted from `AudienceProcessor` in the overhaul as a standalone, testable class; behaviour byte-identical) driven by `AudienceProcessor::renderOutgoingMidi` (`Source/PluginProcessor.cpp`). Pitch math lives in `Source/MidiPitch.h`.

### 7.1 Output modes

- `audioMidiOutputMode`: **Audio Only** (engine renders sound, MIDI events drained and discarded), **MIDI Only** (engine processes control events silently, emits MIDI), **Audio + MIDI** (both).
- `midiOutputType`: **Off**, **Normal MIDI** (nearest 12-TET note, single channel `normalMidiChannel`, plus CC74/CC11 expression), **MPE MIDI** (per-note pitch bend on member channels — exact spectral cents preserved).
- Event sources: every engine note event carries a `sourceId` — `row*100 + col` for seats (0..2599) and `2600..2663` for the 64 keyboard slots; `MpeMidiOutput::kMaxMidiSources = 2664`, with a `static_assert` in `PluginProcessor.cpp` tying it to the `PartialEngine` constants.

### 7.2 Zones & channel layout

`mpeZone` **fully owns** the channel layout via `zoneChannels()` (`PluginProcessor.cpp`, single source of truth for config build *and* change detection):

| Zone | Master channel | Member channels |
|---|---:|---|
| Lower (default) | 1 | 2..16 (15 members) |
| Upper | 16 | 1..15 (15 members) |

When MPE is active and `mpeSendSetupMessages` is on, the **MPE Configuration Message** (MCM: RPN 6 — CC101=0, CC100=6, CC6=memberCount, CC38=0, then RPN null CC101=127/CC100=127) is sent on the master channel, followed by a **pitch-bend-sensitivity RPN** (RPN 0: CC101=0, CC100=0, CC6=bendRangeSemitones, CC38=0, RPN null) on **every member channel**. Setup is re-sent whenever the MIDI config changes (`markSetupDirty`, atomic flag). Any config change while notes are active triggers a safety reset: note-offs for all active voices, then CC123 (all notes off) + CC120 (all sound off) + pressure 0 + pitch-wheel center on all 16 channels.

### 7.3 Per-note channel allocation

- Each active source owns one member channel until its note-off (`mpeChannelOwner[17]` map).
- Free channels are scanned **round-robin**: the scan starts *after* a cursor and wraps within `[memberFirst, memberLast]`, and the cursor advances to the channel handed out. Consequence: a just-freed channel is the **last** to be reused, which mitigates a receiver-side per-note pitch-capture race (a new note landing instantly on a just-freed channel could be latched with the channel's stale bend). The cursor is re-armed only on an actual zone/range change or reset — not on the unconditional per-block `setMemberRange` call (a fixed cursor-clobber bug, commit `7dc7c54`).
- If no channel is free, the **oldest** active voice (smallest age counter) is stolen: its note-off is emitted first, then the channel is reassigned.
- Counters exposed for UI/debug: `getActiveMpeVoices()`, `getAvailableMpeChannels()`, `getMidiNotesSent()`.

### 7.4 Pitch math

`convertFrequencyToMidiPitch(targetHz, bendRangeSemitones)` (`Source/MidiPitch.h`):

```text
midiFloat = 69 + 12*log2(hz/440)
note      = clamp(round(midiFloat), 0, 127)        # nearest 12-TET note
cents     = (midiFloat - note) * 100               # ALWAYS within ±50 cents
bend14    = clamp(8192 + round((cents / (bendRange*100)) * 8192), 0, 16383)
```

Because the note is the *nearest* 12-TET note, the microtonal offset is mathematically guaranteed ≤ ±50 cents — which is why the 2-semitone default bend range is always sufficient for static spectral pitches ([§6](#6-parameters-reference-apvts) note). The same exact `frequencyHz` resolved by `getScalePitch` for the audio engine is used here — no separate approximation (shared-resolver rule).

### 7.5 Message order & expression

For an MPE note-on, the emission order on the allocated channel is (verified by `Tests/MpeOutputTests.cpp`):

1. `pitchWheel` (**bend before note-on** — receivers latch per-note pitch at note-on)
2. `CC74` (timbre: `x*0.68 + Motion*0.32` scaled to 0..127)
3. `CC11` (expression: `y*0.70 + Energy*0.30`)
4. `noteOn` (velocity from engine 0..1 → 1..127)
5. `channelPressure` (from y)

Continuous `Expression` events update bend/pressure/CC74/CC11 only when the new value differs by more than 1 step (spam suppression). A NoteOn for the **same source, same note, same bend (±1)** is treated as an expression refresh, not a retrigger. With `mpePitchMode = Glide`, a same-note NoteOn with *different* bend also becomes a bend update instead of a retrigger (per-note glide within the bend range). Note-off emits `noteOff` + pressure 0 + pitch-wheel re-center, and releases the channel unconditionally (fixes a historical channel leak when the output type changed mid-note).

### 7.6 Receiver requirements & known limitation

- The receiving synth must be MPE-capable, set to the **same zone** (default Lower) and the **same per-note pitch-bend range** (default 2 st), or accept the bend-range RPN the plugin sends. Editor tooltips state both requirements (`PluginEditor.cpp`).
- **Known limitation (Ableton Live):** routing SpektraSynth's MPE output through an Ableton track into a single MPE receiver plugin loses simultaneous per-note microtones. Investigated during the overhaul (PR #1) and traced to **Ableton's MPE routing limitation for a single plugin-to-plugin MIDI route — not a plugin bug**: the same output is correct with a standalone MPE receiver or per-channel routing. Workarounds: use the virtual/physical MIDI port output, a standalone receiver, or 16 per-channel routings.

### 7.7 MIDI input (playing the synth from a keyboard)

`processIncomingMidiKeyboard` maps incoming notes through `externalMidiPitchMode`: **Direct MIDI Pitch** (play the MIDI note as-is), **Quantize To Current Scale**, or **Use As Trigger For Audience Pitch** (keys index the on-screen one-octave scale-keyboard degrees, base C1 + root pitch class, with nearest-step fallback so compact atomic scales like Hydrogen Core remain playable). Input arriving on the active MPE *member* channels is ignored while MPE output is on, to avoid feedback loops. Keyboard voices use the 64 keyboard source slots.

### 7.8 MIDI destinations

`getMidiOutputOptions()`: **Host MIDI Output** (the plugin's MIDI bus; in Ableton: "route from this plugin track"), **Virtual: SpektraSynth MIDI Out** (a created virtual port; numbered per instance), or any physical MIDI output device. Non-host destinations are fed from a lock-free SPSC fifo (8192 packed events, overflow counted in `externalMidiDropped`) drained by a 60 Hz message-thread timer — the audio thread never calls `juce::MidiOutput`.

---

## 8. OSC protocol

Defined and parsed by `Source/OscWireFormat.h` (shared, documented, allocation-free address parser) and `Source/OscBridge.cpp`. Default transport: **UDP port 6060** (persisted; changeable in the UI).

Address pattern:

```text
/cs/<row>/<col>/finger<n>/<param>
```

| Segment | Rule |
|---|---|
| `/cs/` | Literal prefix ("control surface"), case-insensitive |
| `<row>` | One letter `A..Z` (case-insensitive) → row 0..25. Extra characters after the letter up to the next `/` are ignored (`/cs/A1/...` ⇒ row 0) |
| `<col>` | Decimal digits → column, must be in `[0, 100)`; parser clamps pathological digit strings to avoid overflow |
| `finger<n>` | **Opaque and ignored** — the finger index is skipped, so multi-touch collapses onto the single seat (one voice-group per seat) |
| `<param>` | `on` (arg int/float, `!=0` ⇒ active; `0` ⇒ inactive) · `off` (no arg needed) · `line` (float 0..127 → X = value/127) · `v` (float 0..1 → Y) |

Examples (from `README.md`):

```text
/cs/<row>/<col>/finger0/on    1          seat activates
/cs/<row>/<col>/finger0/on    0          seat deactivates
/cs/<row>/<col>/finger0/off              seat deactivates
/cs/<row>/<col>/finger0/line  <0..127>   X position -> selected scale degree
/cs/<row>/<col>/finger0/v     <0..1>     Y position -> voice amplitude
```

Semantics downstream: X is quantized to the selected root/scale/octave range ([§3.8](#38-the-playable-grid-x--pitch)); a *changed* X **replaces** that seat's note (the previous unison group is hard-stopped) subject to the energy-scaled retrigger hysteresis (`minTriggerMs` engine default 150 ms); Y continuously re-targets amplitude. Up to 16 `OscBridge` clients in one process can share a single UDP port via the internal `SharedPort` registry (lock-free atomic fan-out); when the 16-client cap is hit, the instance's `oscStatus()` reports "PORT FULL" instead of failing silently. Both int32 and float32 OSC argument types are accepted. The bridge also unpacks nested OSC bundles.

The editor's **Simulator** (`Source/Simulator.cpp`) generates the same seat events without UDP: Add Participant, +25 Crowd, Remove Participant, Random Movement (~30 Hz drift), Clear All.

---

## 9. Build & test

### 9.1 Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

- First configure downloads **JUCE 8.0.4** via FetchContent (shallow clone).
- Outputs: `build/AudienceHarmonicSynth_artefacts/Release/VST3/SpektraSynth.vst3` and `.../Standalone/SpektraSynth.app` (plus the three other product targets, [§4.1](#41-build-targets-cmakeliststxt)).
- macOS post-build: copies `Samples/` into bundle resources, strips `.orig`/`.DS_Store`, ad-hoc codesigns; `-DAUDIENCE_SYNTH_AUTO_INSTALL=OFF` disables the auto-copy of the VST3 into `~/Library/Audio/Plug-Ins/VST3` (default ON for local dev).
- `AUDIENCE_SYNTH_SOURCE_SAMPLES_PATH` is baked in so dev builds can also load the source-tree `Samples/` directly.

### 9.2 Tests — 9 ctest targets (`-DAUDIENCE_SYNTH_BUILD_TESTS=ON`, default ON)

```sh
ctest --test-dir build -C Release --output-on-failure
```

| Target | Source | Covers |
|---|---|---|
| `AudienceAtomicScaleBuilderTests` | `Tests/AtomicScaleBuilderTests.cpp` | Angstrom→nm, longest-wavelength root, degree caps & min separation, every-raw-line-assigned-once invariant, representatives are real source lines, Raw mode 1:1 |
| `AudienceMidiPitchTests` | `Tests/MidiPitchTests.cpp` | `convertFrequencyToMidiPitch` (A4→note 69/bend 8192, clamps) |
| `AudienceMpeOutputTests` | `Tests/MpeOutputTests.cpp` | ~70 assertions: MCM bytes, per-member bend-range RPN, **bend-before-note-on order**, CC74/CC11 before noteOn, pressure after, distinct member channels for poly notes, oldest-voice stealing, zones, round-robin |
| `AudienceMidiScaleModuleTests` | `Tests/MidiScaleModuleTests.cpp` | 12-TET scale correction (Nearest/Up/Down), pitch-class remap, note-off mapping |
| `AudienceMidiMappingTests` | `Tests/MidiMappingTests.cpp` | `MidiEngine` (non-MPE): X→note, Y→velocity, retrigger debounce, channel modes |
| `AudienceOscBridgeTests` | `Tests/OscBridgeTests.cpp` | Shared-port fan-out over loopback, `/cs/...` parsing, edge cases (col ≥ 100, finger collapse, zero-arg off) |
| `AudienceSampleLibraryTests` | `Tests/SampleLibraryTests.cpp` | Filename root-note parsing, closest-MIDI lookup, load budget |
| `AudienceSignalFlowTests` | `Tests/SignalFlowTests.cpp` | `PartialEngine`+`SampleLibrary` integration, 29-element line-count oracle, render finiteness; degrades gracefully if `Samples/` absent |
| `AudiencePerformanceSmokeTests` | `Tests/PerformanceSmokeTests.cpp` | 60/130/512/1024 participants, wall-time and peak-level bounds |

Known coverage gap (architecture map): `PluginProcessor`/`PluginEditor` themselves (APVTS state recall, output-mode switching) have no automated tests; `MpeMidiOutput` is covered, its processor glue is not.

### 9.3 CI (`.github/workflows/ci.yml`)

On every push/PR, `macos-latest`: (1) generated-data sync check, (2) JUCE FetchContent cache keyed on `8.0.4`, (3) configure, (4) build **only the 9 test targets** (not the plugins, for speed), (5) `ctest`.

### 9.4 Generated-data sync

`tools/check_generated_data.sh` regenerates `ElementSpectralData.cpp` from `data/*.txt` into a temp tree and diffs it against the committed file (never writes into `Source/`). Exit 0 = in sync, 1 = drift (fix: `python3 tools/generate_element_spectral_data.py` and commit), 2 = generator unrunnable. Run the generator after any change to `data/` or the `ELEMENTS` list.

### 9.5 Sample-prep tooling (offline, not in the audio path)

`trim_silence.py` (trim tails on disk) and `split_voice.py` (split one recording into root-named notes) read raw audio from `Sources/` (plural — raw inputs, never loaded at runtime) and write playable libraries into `Samples/` (singular `Source/` is the C++ code — do not confuse the three). Sample filenames encode the root note (`C2.wav`, `F#3.wav`, `Bb4.wav`); `SampleLibrary` parses them, auto-trims trailing silence in memory (~−56 dB threshold), accepts `.wav/.aif/.aiff/.flac`, and enforces safety budgets (≤4096 samples, ≤2 GiB decoded).

---

## 10. Repository map

| Path | What it is |
|---|---|
| `Source/PluginProcessor.h/.cpp` | Flagship `AudienceProcessor`: APVTS (47 params), `pullParams` → engine atomics, `processBlock` (audio + MIDI orchestration), MPE config build (`zoneChannels`/`buildMpeConfig`), external-MIDI port management, incoming-MIDI keyboard mapping, state save/restore, debug reports |
| `Source/PluginEditor.h/.cpp` | Flagship editor: status bar, library rail, audience map + scale strip, macro/texture/voice/network/simulator bands, Performance overlay, tooltips |
| `Source/PartialEngine.h/.cpp` | The audience synthesis engine: seats (2600), voices (1024), event FIFOs, X→pitch resolver (`getScalePitch`), additive Element Spectral Synth (sine LUT), direct/granular sample playback, signature-mode profiles, FX chain, aurora/UI telemetry, atomic-scale cache owner |
| `Source/AtomicScaleBuilder.h/.cpp` | The spectra→scale translation algorithm (octave reduction, salience, greedy selection, clustering, medoid; §3) |
| `Source/ElementSpectralData.h/.cpp` | **Generated** 29-element emission-line tables (do not hand-edit; regenerate) |
| `Source/MpeMidiOutput.h/.cpp` | MPE/Normal MIDI emission engine: channel allocator, MCM/RPN setup, note/expression state machines, debug rings (§7) |
| `Source/MidiPitch.h` | `convertFrequencyToMidiPitch` (frequency → nearest note + 14-bit bend) |
| `Source/SeatEventSink.h` | The 26x100 seat-grid fan-in interface (`setX/setY/setOn`) |
| `Source/OscWireFormat.h` | Documented OSC `/cs/...` grammar + allocation-free address parser (§8) |
| `Source/OscBridge.h/.cpp` | UDP/OSC receiver; process-global shared-port registry with ≤16-client lock-free fan-out |
| `Source/Simulator.h/.cpp` | Fake-audience generator (timer-driven random crowd) |
| `Source/SampleLibrary.h/.cpp` | Sample scanning/decoding, filename→root-note parsing, closest-MIDI lookup, auto-trim, load budgets |
| `Source/AuroraComponent.h/.cpp` | Seat-map + spectral-strip visualizer (30 Hz polling of lock-free engine getters) |
| `Source/LibraryRail.h/.cpp` | Left rail: sample-library browser + element spectra browser |
| `Source/DebugPanel.h/.cpp` | MIDI/MPE diagnostics panel (incoming/outgoing byte rings, voice table) |
| `Source/UiText.h` | Shared `midiNoteName` helper (MIDI 60 → "C4") |
| `Source/MidiEngine.h/.cpp` | Non-MPE audience→MIDI generator (notes + CC1/11/74/91/93; channel modes) for the MIDI targets |
| `Source/MidiProcessor.h/.cpp` | AudioProcessor wrapper for the two MIDI-effect plugin targets |
| `Source/MidiScaleModule.h/.cpp` | 12-TET scale-lock module (root/type/correction/remap matrix) for the MIDI Generator |
| `Source/MidiGeneratorEditor.h/.cpp` | Editor for the MIDI generator targets (Ableton-style UI) |
| `Source/AudienceMidiDeviceApp.cpp` | Standalone UDP→MIDI bridge GUI app |
| `Tests/*.cpp` | The 9 test harnesses (§9.2; ad-hoc `expect()` runner, no framework dependency) |
| `data/*.txt` | Emission-line catalogues, 91 elements present, 29 compiled (`wavelength_nm intensity` rows) |
| `tools/generate_element_spectral_data.py` | data→C++ codegen (29-element list; Nitrogen omitted) |
| `tools/check_generated_data.sh` | Drift check used by CI |
| `Samples/` | Runtime sample libraries (subfolder = library; root-note filenames) |
| `Sources/` | **Raw** recordings consumed by the Python prep tools only — never loaded at runtime; do not rename |
| `docs/SpektraSynth-Mimari-Harita.md` | Deep architecture map (Turkish; condensed into §5 here; where it conflicts with code, code wins) |
| `docs/superpowers/specs/` | UI-redesign design spec |
| `AGENTS.md` | Binding engineering rules: realtime-audio constraints, architecture invariants, APVTS ID stability, testing checklist |
| `README.md` | User-facing overview + per-version changelog (1.0.5 → 1.0.35) |
| `CMakeLists.txt` | 4 product targets + 9 test targets + packaging/install steps |
| `.github/workflows/ci.yml` | macOS CI pipeline |
| `releases/` | Archived binary releases (not built) |
| `split_voice.py`, `trim_silence.py` | Offline sample-prep tools (repo root) |

---

## 11. Glossary

| Term | Definition |
|---|---|
| **Emission line** | A discrete wavelength at which an excited atom emits light; catalogued as wavelength (nm) + relative intensity. The project's physical input data. |
| **lambda_ref (reference wavelength)** | The longest positive-intensity wavelength of an element; design rule maps it to the musical root (0 cents). |
| **Octave reduction** | Folding a frequency ratio into a single octave: `cents mod 1200`. A musical convention (pitch-class equivalence), not a physical operation. |
| **Cents** | 1/100 of a 12-TET semitone; 1200 cents = 1 octave. `cents = 1200*log2(ratio)`. |
| **Circular distance** | Pitch-class distance on the octave circle: `min(d, 1200−d)`; 0 ct ≡ 1200 ct. |
| **Salience** | Log-compressed normalized intensity (`log1p(I)/max`), used to rank lines for scale-degree selection. |
| **Scale degree / anchor** | A selected pitch class in the playable atomic scale; each is a cluster of one or more raw lines. |
| **Medoid** | Cluster representative chosen as the *real member line* minimizing the weighted mean circular distance to the rest of the cluster (no synthetic average pitches). |
| **Cluster spread** | Weighted RMS of signed circular deltas of cluster members around the representative, in cents. |
| **Timbre partial** | One additive-synthesis component at `root_hz * lambda_ref/lambda_i` with amplitude `I_i/I_max`; the full, un-reduced spectrum. |
| **Scale modes (Core/Extended/Microtonal/Scientific/Raw)** | UI names for `ScaleMode::{Melodic, Performable, Microtonal, Scientific, Raw}` — degree caps 7/12/24/48/∞ with min separations 80/40/20/10/0 ct. |
| **Seat** | One audience participant, addressed `row (A..Z) x column (0..99)`; 2,600 max; identified in MIDI as `sourceId = row*100+col`. |
| **APVTS** | `juce::AudioProcessorValueTreeState` — the host-visible, session-persisted parameter store. Parameter IDs are frozen. |
| **MPE** | MIDI Polyphonic Expression: one note per member channel so per-note pitch bend/pressure/CC74 are independent. |
| **MCM** | MPE Configuration Message: RPN 6 on the zone master channel declaring the member-channel count. |
| **RPN 0 (pitch-bend sensitivity)** | Registered parameter setting a channel's bend range in semitones; sent to every member channel. |
| **Zone (Lower/Upper)** | MPE channel grouping. Lower: master 1 + members 2–16. Upper: master 16 + members 1–15. |
| **Bend-before-note-on** | MPE requirement: the per-note pitch wheel value is sent *before* note-on so the receiver latches the correct pitch. |
| **Round-robin allocation** | Member-channel pick strategy that reuses a just-freed channel last (stale-bend race mitigation). |
| **Channel stealing** | When all members are busy, the oldest voice is note-off'd and its channel reassigned. |
| **MIDI Source Event** | Engine-side note/expression event (`PartialEngine::MidiSourceEvent`) crossing to the MIDI layer via lock-free FIFO. |
| **Signature mode** | One of 5 macro sound-design profiles (Choir Cloud, Glass Harmonics, Sub Swarm, Spectral Rain, Frozen Hall) scaling grain/env/FX behaviour. |
| **Macros (Energy/Motion/Tone/Space)** | Four 0..1 performance controls: trigger-density+level / movement+grain-spread (+CC74 mix) / filter+brightness / reverb+delay. |
| **Granular engine** | Sample playback as overlapping windowed grains (≤8/voice) with jitter/spread/reverse options. |
| **Direct Sample Player** | Plain pitched sample playback (closest root-note sample, linear read). |
| **Aurora bands** | 96 log-spaced level bins published atomically for the UI visualizer. |
| **AnaMark `.tun`** | Text tuning-file format (`[Exact Tuning]`, `note N= cents`) read by Omnisphere; used for the Atomic CORE WhiteKeys library. |
| **Scala `.scl`** | The widespread open scale-interchange format; the natural alternative representation of these atomic scales. |
| **Seat hysteresis (`minTriggerMs`)** | Minimum per-seat retrigger interval (engine default 150 ms), scaled down by the Energy macro. |
| **Unison** | Up to 3 slightly detuned voices per seat trigger, folded down adaptively as the crowd grows. |

---

## 12. Version & history

- **Lineage:** `README.md` carries the user-facing changelog from 1.0.5 (compact DAW UI) through 1.0.20 (Element Spectral Synth introduced), 1.0.21 (timbre/scale separation + the 5 atomic scale modes), 1.0.27 (MIDI/MPE output added), 1.0.29–1.0.30 (external-keyboard spectral playability), 1.0.23–1.0.34 (element roster grown five-at-a-time to 29, Nitrogen pending data), to **1.0.35** (Elemental Spektra UI pass; current `CMakeLists.txt` version).
- **The overhaul (branch `spektrasynth-overhaul`, PR #1 — "SpektraSynth overhaul: MPE microtonal correctness, DSP performance, tests/CI hardening, UI & OSC cleanups", 20 commits after 1.0.35, developed in 6 parallel waves with multi-agent review):**
  - **MPE correctness:** MIDI-output engine extracted into the testable `MpeMidiOutput` class (byte-identical behaviour); round-robin member-channel allocation (+ cursor-clobber fix); `mpeZone` made functional (Lower/Upper derive the full channel layout; previously a dormant Lower-only stub); thread-safety fix around message-thread all-notes-off (`suspendProcessing`); voice/channel arrays sized to 2664 with a `static_assert` guard. The remaining poly-microtone issue through Ableton was diagnosed as an Ableton MPE-routing limitation, not a plugin bug ([§7.6](#76-receiver-requirements--known-limitation)).
  - **DSP performance (behaviour-preserving):** per-partial `std::sin` → shared 4096-point interpolated sine LUT; the 145-entry atomic-scale cache moved to explicit message-thread ownership (was a function-local magic-static reachable from the audio thread); active-voice index list replaces full 1024-voice sweeps.
  - **Feature fix:** the "Element Partial" knob now limits audible partials in non-solo mode (was inert); default unchanged (all partials).
  - **Tests & CI:** test targets 7 → **9** (new `AudienceMpeOutputTests`, `AudienceSampleLibraryTests`); OSC edge-case tests; sample-absence graceful degradation; GitHub Actions CI; generated-data sync check.
  - **Robustness/UI:** OSC "PORT FULL" surfaced; allocation-free OSC parsing; shared `OscWireFormat.h`; sample-load RAM budget; shared note-name helper; cached hit-test rects; reused paint buffers; macOS auto-install gated behind a CMake option; Android Gradle project removed; the architecture map added under `docs/`.
- All overhaul changes preserve APVTS parameter IDs and 4-char plugin codes — existing sessions and presets stay compatible.
