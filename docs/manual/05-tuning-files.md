# 05 - Pitch Systems and External Tuning

Cosmic Microwave 2.3 maps normalized horizontal position through one of two pitch
systems. **Tonal** provides seven conventional 12-TET scale tables. **Atomic** projects
stored element emission spectra into playable one-octave degree banks. Both systems
share **ROOT**, **OCTAVE**, and **RANGE**.

The plugin does not load or export `.tun`, `.scl`, or `.kbm` files. Receiver-side
tuning can still be applied after Cosmic Microwave's MIDI output.

## Choosing a pitch system

The selector in the **PITCH MAPPING** card switches between:

- **Tonal** - choose a familiar scale under **SCALE**.
- **Atomic** - choose an **ELEMENT** and a **DENSITY**.

New sessions default to **Atomic / Helium / Extended**. The historical 2.0-to-2.1
migration still restores schema-2 sessions as Tonal so their established pitch mapping
does not change.

## The seven Tonal maps

Tonal degrees are semitone offsets from the selected root:

| Scale | Semitone offsets | Steps per octave |
|---|---|---:|
| Major | `0, 2, 4, 5, 7, 9, 11` | 7 |
| Natural Minor | `0, 2, 3, 5, 7, 8, 10` | 7 |
| Pentatonic | `0, 2, 4, 7, 9` | 5 |
| Dorian | `0, 2, 3, 5, 7, 9, 10` | 7 |
| Lydian | `0, 2, 4, 6, 7, 9, 11` | 7 |
| Harmonic Minor | `0, 2, 3, 5, 7, 8, 11` | 7 |
| Whole Tone | `0, 2, 4, 6, 8, 10` | 6 |

These entries are exact 12-TET MIDI notes. In MPE, their pitch wheel is normally
centered.

## Atomic elements

Atomic mode contains 29 catalog entries spanning Hydrogen (`H`) through Zinc (`Zn`):

```text
H  He Li Be B  C  O  F  Ne Na Mg Al Si P  S
Cl Ar K  Ca Sc Ti V  Cr Mn Fe Co Ni Cu Zn
```

Nitrogen is not present because the current source dataset does not contain its
matching spectral catalog. Element names in the menu are the authoritative available
set.

For each element, valid positive emission wavelengths are converted into wavelength
ratios against that element's longest usable line, then folded into one octave as
cents. Intensity-guided selection keeps the most useful separated representatives.
The musical Root anchors this interval bank; Cosmic Microwave does not treat optical
wavelength in nanometres as an audible frequency.

The Pitch System tooltip reports the chosen element, its actual degree count, and the
reference wavelength used to derive the ratios.

## Atomic density modes

| Density | Maximum degrees per octave | Minimum separation used by the catalog |
|---|---:|---:|
| Core | 7 | 80 cents |
| Extended | 12 | 40 cents |
| Microtonal | 24 | 20 cents |
| Scientific | 48 | 10 cents |
| Raw 128 | 128 | no added separation |

These are caps, not promises that every element has that many usable lines. For
example, Helium Extended exposes the actual count shown in the UI rather than padding
the bank to 12 duplicate degrees. **Raw 128** is still bounded: it uses at most 128
selected spectral representatives so the realtime MIDI table remains finite.

## Root note, octave, and range

**ROOT** chooses a pitch class from C through B. **OCTAVE** uses standard MIDI note
numbering:

```text
root_midi = (octave + 1) * 12 + root_pitch_class
```

Examples:

| Root setting | MIDI note |
|---|---:|
| C0 | 12 |
| C2 (default) | 36 |
| A4 | 69 |

**RANGE** repeats the selected one-octave degree bank from one through six octaves.
Tonal tables stop before exceeding MIDI note 127. Atomic tables also stop safely at
the upper representable edge rather than pinning multiple microtonal targets to the
same endpoint.

## How U/X selects a pitch

Incoming `u` values are clamped to `0..1`. The active pitch table is divided into
equal-width regions:

```text
step = floor(clamp(u, 0, 1) * table_size)
step = min(step, table_size - 1)
```

U=`0` selects the first step and U=`1` selects the last. Every source touch stores its own
current U/X position. Send U before `on` so the first note starts at the intended step.

Movement inside one region updates CC74 but keeps the same pitch. Crossing a region
selects a new pitch. Changing Pitch System, Root, Octave, Scale, Element, Density, or
Range safely re-resolves held source touches in bounded batches.

## Normal MIDI versus MPE pitch

Tonal maps produce the same base note in both output models. Atomic maps expose an
important difference:

| Output | Atomic result |
|---|---|
| Normal MIDI | Send the nearest 12-TET MIDI note. No per-note pitch wheel is sent for OSC touches. |
| MPE MIDI | Keep the element-derived target frequency. Send the nearest MIDI base note plus a per-note pitch-wheel offset before Note On. |

MPE pitch wheel has finite 14-bit resolution, so “exact” means the catalog's exact
frequency is the target and is represented to MIDI pitch-wheel resolution. Match the
receiver's bend range to Cosmic Microwave. The Atomic offset is measured from the
nearest semitone, but an incorrect receiver range still produces the wrong pitch.

With **Retrigger**, crossing to another pitch step releases the old note and starts the
new one. **Glide** can move by pitch bend without retriggering only while the target
can remain on the same nearest base note; changing the required base note retriggers.

## Applying tuning in a receiving instrument

A receiving instrument can reinterpret Cosmic Microwave's MIDI note numbers using its
own tuning system. Keep these ownership boundaries in mind:

- Cosmic Microwave selects the outgoing base note and owns matching Note Off messages.
- In Atomic MPE, Cosmic Microwave also owns the element-derived pitch-wheel offset.
- The receiver owns the final sounding result after applying its tuning map, transpose,
  and pitch-bend configuration.
- Adding receiver tuning on top of Atomic MPE compounds both tunings; do this only when
  intentional.
- CC74, CC11, velocity, pressure, source channels, and touch lifecycles are unaffected.

For Normal MIDI, receiver-side microtuning is one way to reinterpret the nearest notes.
For MPE, ensure the receiver applies tuning independently per member channel and does
not discard Cosmic Microwave's pitch wheel.

## Session migration

Cosmic Microwave 2.3 uses state schema 5. Pitch migration remains compatible with the
earlier schema-3 Pitch System transition:

- new sessions start at Atomic / Helium / Extended;
- existing schema-2 MIDI-only sessions receive an explicit Tonal selection;
- released 1.x sessions whose old 36-choice Scale selected an element migrate to
  Atomic and recover that corresponding element;
- released 1.x element-engine sessions retain their stored `spectralElement` and
  `atomicScaleMode` choice where possible; and
- invalid or non-finite stored choice values are clamped to safe defaults.

Schema 4 added the Time Field. New 2.3 sessions start in Ensemble; any state that lacks
the schema-4 timing parameters receives Flow so an older session's attacks stay direct.
Schema 5 adds the Time Gate LFO parameters. New sessions and every older or partial
state that lacks them receive **Off / Square / Sync / 1/4 / 1.00 Hz**, so loading an
older project cannot unexpectedly gate its MIDI.

This migration restores pitch intent only. Removed sample, granular, timbre, and
internal sound-generation controls do not return in the 2.3 flagship.
