# SpektraSynth UI/UX Redesign — Design Spec

Date: 2026-05-30
Direction: **B · Spektra Performance** (bold, stage-ready, spectral hero)

## Goal
Transform the JUCE editor from a dense "mockup-looking" panel into a modern, premium, stage-readable DAW instrument. Preserve ALL controls, functionality, and APVTS parameter IDs/attachments.

## Locked decisions
- **Spectral field:** horizontal wavelength→color spectrum bar is the primary hero; existing radial wavelength wheel kept as a secondary/optional view.
- **Audience map:** below the hero, full width, with larger glowing seat markers (replaces 1–4px dots).
- **Sample-mode hero:** when engine = Sample Library, the hero shows the active sample waveform + info; when engine = Element Spectral Synth, it shows the spectral field. Hero adapts to engine mode.
- **Performance overlay (stage mode):** kept, enlarged, restyled to the new system.

## Design system
- **Base:** `#0b0d12` window, `#0e1218`/`#0e1116` panels, `#1c2530` hairlines.
- **Text:** `#eef2f7` primary, `#8a93a0` secondary (both ≥ WCAG AA on base). Minimum font size 11px (no 8–9px).
- **Accent (semantic, replaces the 6 random colors):**
  - `#5ec8ff` cyan = the single interactive accent (active control value, selection, focus).
  - **Spectral gradient** (violet→blue→teal→green→yellow→orange→red = short→long wavelength) = used only to encode wavelength/pitch (emission lines, element tags, active seats colored by their pitch).
  - `#e23d52` red = Panic / warnings only.
- **Typography:** clean sans, clear weight hierarchy (800 wordmark, 700 section labels, regular body).

## Layout (wide, ≥1180×720; degrades to current min)
1. **Top bar:** `SPEKTRA SYNTH` wordmark · engine name · Signature Mode selector · consolidated live readouts (SEATS / VOICES / DOMINANT) · Mute · Panic. Single source of telemetry (removes the 4× duplication).
2. **Left rail:** Elements / Samples tabs · search · list (elements tagged with their wavelength color) · active-library status.
3. **Center hero:** spectral field (or sample waveform) — the visual centerpiece.
4. **Audience map:** full-width, large glowing markers, colored by pitch.
5. **Macro row:** 4 large knobs (Energy / Motion / Tone / Space) + Root / Scale / Octaves group.
6. **Bottom tabbed panels (progressive disclosure):** Texture · Voices · FX · MIDI/MPE · Network · Simulator — one visible at a time.

## Remove
- Fake window chrome (macOS traffic-light dots), "01/02" panel numbers, duplicated telemetry, sub-10px fonts.

## Performance fixes folded in (from review)
- **#12:** no `std::array<float,8192>` stack buffers or 1024-voice loops in `paint()`; cache a small member sized to the active voice limit, filled in the timer.
- **#13:** `timerCallback` must not call full-window `repaint()` + `updateOutputModeVisibility()` every tick unconditionally; only repaint live sub-regions and update visibility on actual change.

## Constraints
- JUCE C++ in-place redesign (not a rewrite-from-scratch). Keep all `*Attach` APVTS attachments wired and parameter IDs unchanged.
- Build + run after each stage. Verify the standalone renders and controls still work.

## Execution stages (each compiles)
1. Theme/color + typography system (shared `cs::` palette + LookAndFeel).
2. Remove chrome noise + consolidate telemetry (top bar).
3. Audience map: larger markers + paint perf fix (#12).
4. Spectral hero bar (+ sample waveform variant).
5. Macro knobs larger + Root/Scale group.
6. Bottom tabbed panels (progressive disclosure).
7. Timer/repaint perf fix (#13) + contrast/size pass.
