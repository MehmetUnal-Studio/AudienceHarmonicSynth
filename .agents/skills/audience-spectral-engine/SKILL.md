---
name: audience-spectral-engine
description: Use this skill when implementing, reviewing, or extending the Elemental Spektra Engine, Atomic Spectral Scales, wavelength mapping, spectral timbre generation, and atomic frequency systems used in Audience Harmonic Synth.
---

# Audience Spectral Engine

## Purpose

This plugin contains a custom musical system based on atomic emission spectra.

The system is not based on traditional 12-TET music theory.

The goal is to preserve scientific spectral relationships while making them playable as musical scales and timbres.

---

# Core philosophy

Atomic emission spectra contain:

- wavelength
- frequency
- intensity

These data are transformed into:

- playable scales
- spectral timbres
- harmonic systems
- performance interfaces

The goal is not to imitate western tuning systems.

The goal is to create new musical structures from real physical data.

---

# Spectral scale architecture

Each element contains:

- raw spectral lines
- wavelength values
- intensity values

The engine generates:

1. Playable Atomic Scale
2. Spectral Timbre Model

These are separate systems.

Never confuse them.

---

# Timbre system

The timbre system preserves all available spectral information.

Rule:

All raw spectral lines remain available as timbre partials whenever possible.

Example:

Hydrogen:
- preserve all visible lines

Fluorine:
- preserve all visible lines

Boron:
- preserve all visible lines

Timbre generation should prioritize preservation.

Do not simplify timbre data unless explicitly requested.

---

# Scale system

The playable scale is a reduction of the spectral data.

Purpose:

Create a musically playable representation.

The scale is not required to contain every spectral line.

---

# Spectral clustering

Many elements contain spectral lines that are extremely close together.

Problem:

Too many nearby scale degrees create:

- poor playability
- redundant intervals
- visually cluttered interfaces

Solution:

Cluster nearby spectral lines.

Generate representative degrees.

---

# Representative degree rules

When clustering:

Preserve:

- overall spectral distribution
- characteristic intervals
- important strong lines

Avoid:

- duplicate degrees
- nearly identical intervals
- excessive note density

---

# Root line selection

Default rule:

The longest wavelength becomes the reference line.

Reference:

lambda_ref = longest wavelength

All ratios are derived from:

ratio_i = lambda_ref / lambda_i

This rule must remain consistent unless explicitly overridden.

---

# Ratio system

The scale is ratio-based.

Each spectral line produces:

ratio_i = lambda_ref / lambda_i

Ratios define:

- scale positions
- harmonic relationships
- spectral structure

Do not derive scale degrees from arbitrary MIDI note numbers.

The ratio system is the source of truth.

---

# Frequency mapping

For playable scales:

Choose a root musical frequency.

Example:

C1
C2
C3
C4

Then:

spectral_frequency_i =
root_frequency * ratio_i

The same spectral structure may exist at different octaves.

---

# Spectral timbre generation

When a note is played:

played_frequency = current note frequency

Each spectral partial becomes:

partial_frequency =
played_frequency * ratio_i

partial_amplitude =
normalized spectral intensity

This preserves the atomic structure while allowing transposition.

---

# MIDI integration

For external MIDI:

Default mode:

Direct MIDI Pitch

Incoming MIDI notes determine:

played_frequency

Atomic ratios determine:

spectral partial positions

Do not force all MIDI notes to the current root note.

Different MIDI notes must produce different fundamentals.

---

# Elemental Spektra UI

Prioritize:

1. Spectral Lines
2. Wavelength Wheel
3. Active Note Feedback
4. Element Information

Hide controls that do not affect the spectral engine.

---

# Active note visualization

Visual feedback should follow note lifetime.

Note On:
- spectral line highlight ON
- wavelength wheel highlight ON

Note held:
- highlight remains ON

Note Off:
- highlight OFF

Do not use temporary flashes.

The UI should reflect actual note state.

---

# Performance mode

The performer must always be able to identify:

- active spectral degree
- active wavelength
- selected element
- scale mode
- root note

within one glance.

---

# Common mistakes

Flag these immediately:

- mapping all MIDI notes to the same root pitch
- using only one voice for spectral playback
- removing raw spectral lines from timbre generation
- duplicating spectral math in multiple systems
- using global pitch bend for polyphonic spectral tuning
- losing spectral ratios during transposition
- temporary note highlights that ignore note lifetime

---

# Review checklist

When reviewing code:

Check:

- Are ratios preserved?
- Is the longest wavelength used as reference?
- Are raw spectral lines preserved for timbre?
- Are clustered lines used only for scale reduction?
- Are MIDI notes mapped to unique frequencies?
- Does transposition preserve spectral structure?
- Do active-note visuals follow actual note lifetime?
- Is spectral math centralized?
- Is UI focused on spectral information?
