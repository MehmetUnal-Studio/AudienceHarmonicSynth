# 05 — Tuning Files (Atomic CORE WhiteKeys)

The translation at the heart of SpektraSynth — atomic spectra translated into playable
musical scales — does not have to stay inside the plugin. The companion
**“Atomic CORE WhiteKeys”** tuning library exports the same translated scales as
AnaMark **`.tun`** files, so other instruments can play the elemental tunings too. The
set targets **Spectrum's Omnisphere** out of the box, and the format is readable by
many other `.tun`-aware synths.

It is the same pipeline, a different output: instead of an oscillator bank, each
element becomes a keyboard *tuning*.

## What's in the library

- **29 tuning files**, one per element from **Hydrogen to Zinc** — `Hydrogen_CORE_WhiteKeys.tun`,
  `Helium_CORE_WhiteKeys.tun`, … `Zinc_CORE_WhiteKeys.tun`.
  (**Nitrogen is omitted**, matching the plugin: no local `N.txt` spectral dataset is
  available yet.)
- A manifest, `Atomic_CORE_WhiteKeys_manifest.txt`, summarizing every element's degree
  count, raw line count, reference wavelength, and white-key mapping in cents.

## The concept: an element as a 12-key tuning

Each element's clustered spectrum is laid onto the ordinary 12-key octave so it is
*immediately* playable on a normal keyboard:

- **Degrees 1–7 go to the white keys C D E F G A B**, in ascending cents order, with
  degree 1 (the spectral root) on **C at 0 cents**.
- **Degree 8 and above** (for elements whose CORE scale has more than 7 degrees) go to
  the **nearest free black key**.
- **Unassigned keys duplicate the nearest assigned atomic degree** — so every key
  always sounds an actual translated spectral pitch; there are no dead or arbitrary
  keys. The 12-key pattern repeats identically in every octave (+1200 cents per
  octave).

Practical consequence: play only white keys and you are playing the element's CORE
scale in order; black keys are safe doublings (or extra degrees, where they exist).

## The CORE recipe

The scale reduction used for these files is the same **Core** mode you see in the
plugin's **ATOM SCALE** control:

- **Maximum 7 degrees** per element.
- **Minimum 80 cents separation** between degrees.
- **Medoid clustering** — each degree is represented by a *real* emission line from
  inside its cluster (the one with the smallest weighted circular distance to its
  neighbours), never an invented average pitch.
- **The root is always included**: the element's reference emission line,
  **λ_ref** — its longest positive-intensity catalogue wavelength — is fixed at
  **0 cents** on C.

Every other line's pitch comes from the standard translation:

```
cents_i = 1200 · log2(λ_ref / λ_i)   (folded into one octave, 0–1200)
```

## Reading the values: cents from the root

All tuning values are **cents above the root key** (C = 0). Example — **Boron**,
whose reference line is **λ_ref = 678.612 nm** (74 raw positive lines in the
catalogue):

| Key | Cents | Representative line |
|---|---:|---|
| C | 0.0 | 678.612 nm (root) |
| D | 177.4 | 612.502 nm |
| E | 322.3 | 563.327 nm |
| F | 472.3 | 516.596 nm |
| G | 605.2 | 478.421 nm |
| A | 721.7 | 447.285 nm |
| B | 863.1 | 412.193 nm |

Boron's CORE scale has exactly 7 degrees, so all black keys are duplicates of their
nearest white-key degree (C♯ doubles D's 177 ct, D♯ doubles E's 322 ct, and so on).
An element like Hydrogen, with only 5 CORE degrees, fills C–G and duplicates more
keys; the manifest lists each element's exact mapping.

Inside each `.tun` file you'll find the same data in AnaMark form — a commented header
documenting the recipe and the per-key atomic-degree placement, then `[Tuning]` /
`[Exact Tuning]` sections with `note 0` … `note 127` entries in (integer-rounded)
cents relative to note 0.

## Installing in Omnisphere

1. Quit Omnisphere / your host.
2. Copy the `.tun` files into Omnisphere's tuning-file folder inside its STEAM data
   directory:

   ```
   …/STEAM/Omnisphere/Settings Library/Presets/Tuning File/
   ```

   A subfolder keeps things tidy, e.g. `…/Tuning File/Atomic/`.
3. Reopen Omnisphere and choose the tuning from its tuning-file selector (SYSTEM
   page). Pick e.g. **Boron_CORE_WhiteKeys** — the keyboard now plays Boron's
   translated spectrum, root on C.

Tip: pair an Omnisphere patch tuned to an element with SpektraSynth playing the *same*
element (same **ROOT**), and layer the translated timbre with the translated tuning.

## Scala and the wider microtonal ecosystem

The `.tun` set is one export of the translation. If your instrument speaks **Scala**
(`.scl`, plus `.kbm` keyboard mappings) rather than AnaMark `.tun`, the same data
converts directly — the manifest's cents-from-root values per element *are* the scale
definition (7 degrees + the 1200 ct octave), and the white-key-first layout is a
keyboard-mapping concern. Scala is the de-facto interchange format of the microtonal
world, so treat the manifest as the authoritative, human-readable source for porting
the atomic CORE scales to any tuning-capable environment.

And remember the framing that governs the whole project: these files do not make your
synth sound "like an atom" — they let it play scales *translated from* each element's
emission-line structure. That translation, consistently applied across the plugin, the
MPE output, and these tuning files, is what makes the elements recognizable from one
instrument to the next.
