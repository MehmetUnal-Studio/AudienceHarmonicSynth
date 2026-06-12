# 02 — UI Guide

This chapter walks the SpektraSynth editor section by section, using the labels you
actually see on screen. Almost every control has a tooltip — hover it for about a
second to read a one-line description in place.

Layout overview (top to bottom):

```
┌──────────────────────────────────────────────────────────────────────┐
│ Top bar: brand · status pills · Performance · Debug · Mute · Panic   │
├─────────────┬────────────────────────────────────────────────────────┤
│ LIBRARY     │ AUDIENCE MAP  (or MIDI CONSOLE in Debug view)          │
│ rail        ├────────────────────────────────────────────────────────┤
│ Samples /   │ SCALE KEYBOARD                                         │
│ Elements    ├────────────────────────────────────────────────────────┤
│             │ Macro panel: ENGINE/SOUND MODE · ENERGY MOTION TONE    │
│             │ SPACE · ROOT/OCT/SCALE/RANGE · POLY/Freeze             │
│             │ output row: OUTPUT MODE … MIDI OUT … ZONE … Rescan     │
│             ├────────────────────────────────────────────────────────┤
│             │ TEXTURE | VOICES   (tabbed module)                     │
│             ├────────────────────────────────────────────────────────┤
│             │ NETWORK · SIMULATOR · PARTICIPANTS / ACTIVE VOICES     │
└─────────────┴────────────────────────────────────────────────────────┘
```

## Top bar

Left to right:

- **Brand mark** — "SpektraSynth / Audience Spectral Engine".
- **Status pills** (informational, they appear as window width allows):
  - **LIVE / OFF** — whether the OSC listener is running.
  - **MODE** — `SAMPLE` or `ELEMENT` (current engine source).
  - **PLAY** — current sample playback mode (shown in Sample mode only).
  - **SEATS** — registered audience seats.
  - **VOICES** — active synth voices.
  - **LIB** — samples loaded in the active library.
  - **DOM** — the dominant sample/voice name.
  - **UDP** — the OSC port (green) or `busy` (red) if it could not be bound.
- **Performance** toggle — opens the large stage-readable overlay (below).
- **Debug** toggle — replaces the audience map with the MIDI console (below).
- **Mute** toggle — mutes plugin output without touching current seats.
- **Panic** — immediately clears all live/simulated seats and stops voices, delay,
  and reverb; also resets the MIDI/MPE output state.

## Library rail (left)

Header **LIBRARY**, with two tabs:

- **Samples** — your sample libraries (subfolders of `Samples/`). Click a library to
  load it; this also switches **ENGINE** to *Sample Library*. A search box
  ("Search libraries…") filters the list and **Rescan** re-scans the folder. Cached
  sample counts and the active library status are shown, plus an **ACTIVE PATCH**
  banner at the bottom.
- **Elements** — the 29 translated element spectra (Hydrogen … Zinc). Click an element
  to select it; this also switches **ENGINE** to *Element Spectral Synth*. The search
  box ("Search elements…") filters by name.

## Audience map (centre)

The **AUDIENCE MAP** visualizes the venue: 26 rows (A–Z) × 100 columns (0–99). Active
seats appear as glowing dots — horizontal position tracks pitch (labels read
**LOW PITCH / STAGE**, **MID**, **HIGH PITCH / STAGE**), dot size/intensity tracks the
seat's Y value. Beneath the map a spectral strip shows the active scale: for element
scales it draws the translated emission lines on a wavelength ruler (nm), a frequency
axis (Hz), the **DOM**inant note, and — when there is room — a circular
**WAVELENGTH WHEEL** (380–750 nm) that lights up with the lines that are sounding.

**Empty state:** when no seats are registered and nothing is sounding, the map shows
a dim centred hint — **“Waiting for audience”** — with the live listener status under
it (*“Listening for OSC on UDP 6060”*, *“OSC port 6060 unavailable”*, or *“OSC bridge
offline”*). It disappears the moment a seat or voice appears, so a live audience never
sees it.

## SCALE KEYBOARD row

A one-octave-style strip of keys, one per scale step of the current scale (musical or
spectral). Header shows **SCALE KEYBOARD**, the current scale name, and how much of it
the computer keyboard covers (e.g. *“computer keys cover first 36 / 74”*).

- **Mouse:** click or drag across keys to play steps directly (drag glides from step to
  step). These keyboard notes do *not* register as audience seats.
- **Computer keys:** `1–0`, `Q–P`, `A–L`, `Z–M` map to steps 1–36. The editor must have
  keyboard focus (click anywhere in it first).
- **Labels:** each key shows its nearest note name on top and its computer key at the
  bottom.
- **Spectral strength bars:** in element scales, each key carries a small bar whose
  height tracks the emission-line strength of that degree — stronger lines, brighter
  bars.
- **Active-step glow:** steps that are currently *sounding in the engine* (from
  audience, simulator, MIDI, or keyboard) get an unmistakable halo — an accent outline
  plus a white core outline and an underline, all scaling with the live voice
  amplitude. This is how you see which microtonal degrees are ringing at a glance.

External MIDI keyboards play through this same strip; see **MIDI IN** below.

## Macro panel

### ENGINE and SOUND MODE (left block)

- **ENGINE** — *Sample Library* or *Element Spectral Synth*. This is the master source
  switch:
  - **Sample Library** — voices play samples from the active library. The
    **SAMPLE PLAYBACK** combo (VOICES tab) selects **Sample Player** (direct,
    pitch-shifted sample playback) or **Granular** (the grain-cloud engine). Sample
    libraries default to direct playback; granular is an explicit choice.
  - **Element Spectral Synth** — voices are additive oscillator banks built from the
    selected element's raw emission lines. The longest positive-intensity wavelength
    maps to the played root; every other line becomes a partial at ratio
    `λ_ref / λ_i` with its catalogue intensity as amplitude. No sample library is
    needed in this mode.
- **SOUND MODE** — the engine personality: **Choir Cloud**, **Glass Harmonics**,
  **Sub Swarm**, **Spectral Rain**, or **Frozen Hall**.

### The four macros

The highlighted group of knobs is meant for live performance:

- **ENERGY** — raises level, saturation, density, and trigger responsiveness.
- **MOTION** — adds organic movement; in Granular mode it increases grain spread.
- **TONE** — opens the voice filter and reverb brightness.
- **SPACE** — expands reverb and cross-delay.

(ENERGY and MOTION also blend into the outgoing MPE expression — see
[03 — MPE Setup](03-mpe-setup.md).)

### Scale zone

- **ROOT** — root note, C through B.
- **OCT** — root octave (0–6). For element spectra the spectral *intervals* are
  preserved while the whole scale moves to this root.
- **SCALE** — what X movement quantizes to. Seven musical maps (**Major**,
  **Natural Minor**, **Pentatonic**, **Dorian**, **Lydian**, **Harmonic Minor**,
  **Whole Tone**) followed by 29 **… Spectrum** entries (*Hydrogen Spectrum* …
  *Zinc Spectrum*) — the translated atomic scales. In Element engine mode the SCALE
  combo is hidden; the selected **ELEMENT** defines the scale.
- **RANGE** — how many octaves the X axis covers (1–6, default 4).

### Output zone (right block)

- **POLY** — voice budget: **Normal** 256, **High** 512, **Ultra** 1024 voices.
  Unison folds down automatically (3 → 2 → 1 voices per trigger) as the crowd grows.
- **Freeze** — holds active grain clouds and freezes the reverb tail.
- **OUTPUT ARMED / OUTPUT MUTED** — live output status readout, plus the current
  voice **LIMIT** and adaptive unison count (e.g. `U3`).

### MIDI output row

A boxed row along the bottom of the macro panel (details in
[03 — MPE Setup](03-mpe-setup.md)):

- **OUTPUT MODE** — *Audio Only*, *MIDI Only*, or *Audio + MIDI*.
- **MIDI OUT** — *Off*, *Normal MIDI*, or *MPE MIDI*.
- **MIDI IN** — how *incoming* MIDI notes are interpreted: **Direct** (plain MIDI
  pitch), **Scale** (quantize to the current scale), or **Trigger** (keys index the
  visible scale-keyboard steps — ideal for compact atomic scales).
- **MIDI OUTPUT** — destination: the host MIDI bus, the virtual port
  *SpektraSynth MIDI Out*, or any system MIDI device.
- **CH** — the single channel used by Normal MIDI.
- **BEND** — MPE pitch-bend range: 2 / 12 / 24 / 48 st (default **2 st**).
- **MPE PITCH** — *Retrig* or *Glide*.
- **ZONE** — MPE zone: *Lower* (master ch 1, members 2–16) or *Upper* (master ch 16,
  members 1–15).
- **Setup** — sends the MPE zone + bend-range RPN setup messages when needed.
- **Rescan** — re-scans system MIDI output devices.
- A status line shows the active destination, and an activity readout counts notes
  (`MIDI n`) and, in MPE mode, active voices vs free member channels (`MPE a/f`).

Controls that do not apply to the current mode are dimmed and disabled rather than
hidden (e.g. **CH** is only active for Normal MIDI; **BEND/MPE PITCH/ZONE/Setup** only
for MPE). The **BEND**, **MPE PITCH**, **ZONE**, **Setup** and **Rescan** controls
need horizontal space — if you don't see them, widen the plugin window.

## TEXTURE / VOICES tabs

The bottom module is tabbed; click **TEXTURE** or **VOICES**. The panel subtitle shows
context — in Element mode, the element name and its root wavelength in nm.

**TEXTURE** — global mix and space:

- **PITCH** (global transpose, ±12 st), **LAYER MIX**, **WET/DRY**, **REVERB**,
  **DELAY**, **TAPE** (tape-style saturation before the limiter), **MASTER**, and
  **STRETCH** (see below).

**VOICES** — per-voice shaping, grain engine, and the spectral controls:

- **ATTACK**, **RELEASE**, **BRIGHTNESS**, **MOVEMENT**.
- Granular controls (active only when SAMPLE PLAYBACK = Granular; dimmed otherwise):
  **SIZE**, **DENSITY**, **PITCH SPREAD**, **POSITION**, **STEREO**, **ENV** (grain
  window: Hann / Triangle / Soft Gate / Pulse), **Reverse**.
- Engine-dependent column:
  - Sample mode: **SAMPLE PLAYBACK** (*Sample Player* / *Granular*).
  - Element mode: **ELEMENT** (the 29-element list) and **Solo**.
- **ATOM SCALE** — how the element's raw lines are clustered into a *playable* scale
  (the timbre always keeps the full raw spectrum):

  | Mode | Max degrees | Min separation | Use |
  |---|---:|---:|---|
  | Core | 7 | 80 ct | sparse melodic performance |
  | Extended *(default)* | 12 | 40 ct | playable atomic scale |
  | Microtonal | 24 | 20 ct | denser microtonal playing |
  | Scientific | 48 | 10 ct | high-detail inspection |
  | Raw | unlimited | 0 ct | one degree per raw line |

- **PARTIAL** (Element Partial, 1–512) with the **Solo** toggle:
  - **Solo off (default):** the knob **limits how many partials are audible** — the
    additive engine renders at most this many of the element's raw lines per voice
    (default: all, up to 512). Turn it down to thin the timbre or to save CPU; levels
    are compensated as the count drops.
  - **Solo on:** auditioning mode — the knob selects **one raw spectral line** and
    plays only that line, at full equal loudness regardless of its catalogue
    intensity. Use it to inspect individual frequencies of the fingerprint.
- **STRETCH** (on the TEXTURE tab) — stretches or compresses the element's frequency
  ratios around the root wavelength (±0.35 exponent): a subtle in/harmonicity control
  for the translated spectrum.

## NETWORK / SIMULATOR ribbon

- **NETWORK** — the UDP port field plus **Apply** (restarts the OSC listener on the
  new port), and a green dot while listening. See
  [04 — OSC & the Audience](04-osc-audience.md).
- **SIMULATOR** — a fake audience for testing without any network traffic:
  **+ Add** (one participant on a free seat), **+25 Crowd**, **Remove**,
  **Random Movement** (continuously drifts every active participant), and
  **Clear All** (same as Panic: clears all seats and stops voices/FX).
- **PARTICIPANTS** and **ACTIVE VOICES** counters (e.g. `0042/256`).

## Performance view

The **Performance** toggle (top bar) overlays a large, stage-readable display intended
for live use: the active library/patch name in big type; an engine / sound mode /
scale / range / polyphony summary line; a full-width **AUDIENCE MAP A–Z / 0–99**; a
live **SPECTRUM** band strip; and five metric cards — **ACTIVE SEATS**,
**ACTIVE VOICES**, **ELEMENT** (or **PLAYBACK** in Sample mode), **SCALE / ZONE**, and
**DOMINANT**. A red **MUTED** badge appears if output is muted. Performance view takes
precedence over Debug view while enabled.

## Debug view

The **Debug** toggle swaps the audience map for the **MIDI CONSOLE** — the diagnostics
panel:

- **outgoing MIDI / MPE** — a live console of every outgoing MIDI message (host /
  virtual port / MPE stream), with a **Copy** button that puts the full outgoing
  event ring (decoded, byte-level) on the clipboard.
- **scale + status** and **root octave** readouts — the resolved scale table and
  tuning state.
- **debug report** — incoming MIDI, keyboard slots, and active MPE voices (source,
  channel, note, pitch bend, age), with a **Copy Report** button that copies the
  complete state + incoming + outgoing report for bug reports.

Use Debug view whenever you need to verify exactly which bytes SpektraSynth is sending
— it is the fastest way to settle "is it the plugin or the receiver?" questions.

## Element-colour theming

When the Element Spectral Synth is active, the UI keys itself to the element's
identity: the root emission wavelength is converted to its visible-spectrum colour
(380–750 nm; wavelengths outside the visible band fall back to a neutral grey), and
that hue tints the module subtitle, the performance-overlay seat dots, the spectrum
strip, and the **ELEMENT** metric card. Scale-keyboard keys and spectral strips are
additionally colour-ramped by pitch, so low-to-high reads as a spectral gradient.
Pick Helium, then Neon, then Iron — the instrument visibly changes identity with the
translated spectrum.
