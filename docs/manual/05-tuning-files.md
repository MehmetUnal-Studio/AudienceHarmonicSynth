# 05 - Pitch Systems and External Tuning

Cosmic Microwave 2.8.0 maps normalized horizontal position through one of two pitch
systems. **Tonal** provides seven conventional 12-TET scale tables. **Atomic** projects
stored element emission spectra into playable one-octave degree banks. Both systems
share **ROOT**, **OCTAVE**, and **RANGE**.

The plugin does not load or export `.tun`, `.scl`, or `.kbm` files. Receiver-side
tuning can still be applied after Cosmic Microwave's MIDI output.

## Choosing a pitch system

The selector in the **PITCH MAPPING** card switches between:

- **Tonal** - choose a familiar scale under **SCALE**.
- **Atomic** - choose an **ELEMENT** and a **DENSITY**.

New sessions default to **Atomic / Zinc / Core**. The historical 2.0-to-2.1
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

These entries are exact 12-TET MIDI notes and require no tuning messages.

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

Movement inside one region emits no MIDI message and keeps the same pitch. Crossing a
region starts a new Note On at the new pitch while the old pitch keeps its captured
Note Duration tail. Changing Pitch System, Root, Octave, Scale, Element, Density, or
Range safely re-resolves held source touches in bounded batches.

## Notes Only Atomic pitch

Tonal maps produce their configured 12-TET notes. Atomic maps are derived from
element spectra and can contain fractional-semitone degrees, but the v2.8.0 output
contract contains no Pitch Bend:

| Output | Atomic result |
|---|---|
| Notes Only | Send the nearest 12-TET MIDI note as Note On/Off. |

The exact element-derived frequency remains part of the catalog and UI description,
but only the nearest MIDI note number leaves Cosmic Microwave. Crossing to another
pitch step starts the new note while the old note keeps its captured duration tail;
there is no bend/glide path.

## Applying tuning in a receiving instrument

A receiving instrument can reinterpret Cosmic Microwave's MIDI note numbers using its
own tuning system. Keep these ownership boundaries in mind:

- Cosmic Microwave selects the outgoing base note and owns matching Note Off messages.
- The receiver owns the final sounding result after applying its tuning map, transpose,
  and any receiver-side pitch configuration.
- Cosmic Microwave emits no MPE, Pitch Bend, CC11, CC74, Channel Pressure, RPN, or
  Crowd Macro CC data.
- Note-On velocity, source channels, and touch lifecycles remain owned by Cosmic
  Microwave.

Receiver-side microtuning is one way to reinterpret the nearest notes. Keep the same
tuning configuration on every receiving channel that should sound alike.

## Session migration

Cosmic Microwave 2.8.0 writes state schema 11 and remains compatible with earlier
state. Pitch migration remains compatible with the earlier schema-3 Pitch System
transition:

- new sessions start at Atomic / Zinc / Core;
- existing schema-2 MIDI-only sessions receive an explicit Tonal selection;
- released 1.x sessions whose old 36-choice Scale selected an element migrate to
  Atomic and recover that corresponding element;
- released 1.x element-engine sessions retain their stored `spectralElement` and
  `atomicScaleMode` choice where possible; and
- invalid or non-finite stored choice values are clamped to safe defaults.

Schema 4 added the Time Field. New 2.8.0 sessions start in Flow with Manual Crowd
Governor; any older state that lacks the timing parameters also receives Flow so its
attacks stay direct. Schema-5 input remains compatible and its retired experimental
fields are discarded. Every schema-6-or-earlier state receives Manual Governor mode
while retaining its saved Time Field values. Schema 8 added routing safety, the
pressure-aware Safety Governor, Global Conductor, and now-retired Crowd Expression
parameters. Schema 9 stores the Notes Only and route-safety contract and remains a
historical migration step. Schema 10 adds the saved Note Duration and Source Capacity
choices: missing duration becomes 16n, schema-9-or-earlier state restores the former
implicit 256-source domain, and fresh/schema-10 partial state uses capacity 64.
Schema 11 adds the Ensemble Same Note choice; absent, malformed, or out-of-range values
restore Tie so older pitch and timing behaviour remains unchanged.
Schema-7-and-earlier sessions keep their historical Mirror/shared-port behaviour with
Safety Governor disabled; these compatibility defaults do not change any saved pitch
selection.

This migration restores pitch intent only. Removed sample, granular, timbre, expression,
MPE, and internal sound-generation controls do not return in the 2.8.0 flagship.
