# SpektraSynth — User Manual

> *A spectral instrument that turns elemental emission lines into playable scales,
> tunings, and timbres.*

SpektraSynth is an audience-driven instrument: a crowd of phones (or a built-in
simulator, or a MIDI keyboard) plays a venue of seats, and **atomic spectra are
translated into playable musical scales and timbral fingerprints**. It renders
internal audio (samples, granular clouds, or an element spectral synth) and/or emits
standard or MPE MIDI — with per-note pitch bend so the microtonal translation
survives into other instruments.

A note on framing, used consistently throughout this manual: SpektraSynth
**translates** atomic spectra. It does not claim to be "the real sound of atoms" —
it maps real emission-line data (wavelengths and intensities) into pitch ratios,
scale degrees, and partial amplitudes you can perform with.

## Chapters

| # | Chapter | What it covers |
|---|---|---|
| 1 | [Getting Started](01-getting-started.md) | What it is, the four products, requirements, install paths, build-from-source quickstart, first sound in 5 steps. |
| 2 | [UI Guide](02-ui-guide.md) | Every section of the editor: top bar, library rail, audience map, SCALE KEYBOARD, the two engines, the four macros, scale/output controls, TEXTURE/VOICES tabs, Performance & Debug views, element-colour theming. |
| 3 | [MPE Setup](03-mpe-setup.md) | **Read this to get microtones into other synths.** Output modes, the virtual port, zones (Lower/Upper), why the bend-range default is 2 st, the receiver checklist, channel allocation, and the known Ableton single-stream routing limitation with verified workarounds. |
| 4 | [OSC & the Audience](04-osc-audience.md) | The 26×100 seat model, the `/cs/<row>/<col>/finger<n>/<param>` wire format, UDP port configuration and the 16-client "PORT FULL" cap, the simulator, network tips. |
| 5 | [Tuning Files](05-tuning-files.md) | The companion **Atomic CORE WhiteKeys** `.tun` library for Omnisphere: 29 element tunings (H…Zn), the white-key-first mapping, the CORE recipe, installation, and Scala as the interchange ecosystem. |
| 6 | [Troubleshooting & FAQ](06-troubleshooting-faq.md) | No sound, MIDI not received, microtones collapsing to 12-TET, global-feeling pressure, CPU tips, sample naming and load budget, OSC quick checks, FAQ. |

## Quick orientation

- **Just want sound?** Chapter 1, "First sound in 5 steps" — standalone app,
  **+ Add** in the SIMULATOR row, done.
- **Driving another synth microtonally?** Chapter 3 is the critical path: MPE MIDI,
  ZONE Lower, BEND 2 st, and match those on the receiver.
- **Connecting a real audience?** Chapter 4 — OSC to UDP 6060.
- **Something's wrong?** Chapter 6, then the **Debug** view's **Copy Report**.

---

*This manual documents the SpektraSynth flagship plugin (VST3 + Standalone) and its
companions. It is written in English and intended as the base for future
localizations.*
