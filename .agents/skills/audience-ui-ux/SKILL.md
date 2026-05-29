---
name: audience-ui-ux
description: Use this skill when designing or modifying UI/UX for Audience Harmonic Synth and related music software.
---

# Audience UI/UX

## Core philosophy

The software is a musical instrument.

The UI should feel:
- focused
- minimal
- performance-oriented
- visually calm
- readable from distance

Avoid:
- crowded layouts
- unnecessary panels
- parameter overload
- tiny controls
- debug-looking interfaces

## Visibility rules

If a control is irrelevant in the current mode:
- hide it
- or disable and visually de-emphasize it

Do not expose controls that have no effect.

## Context-sensitive UI

Different engines may require different interfaces.

Example:

Elemental Spektra:
- prioritize spectral information
- prioritize wavelength visualization
- prioritize active note feedback

Sample Engine:
- prioritize sample controls

Granular Engine:
- prioritize grain controls

Do not show all controls in every mode.

## Visual hierarchy

Important information should occupy the largest visual space.

Example:

Elemental Spektra:
1. Spectral lines
2. Wavelength wheel
3. Active notes
4. Engine controls

## Performance mode

Design for live stage usage.

A performer should instantly see:
- active notes
- active spectral lines
- selected element
- root note
- scale mode

without reading small text.

## Interaction feedback

Every user action should produce feedback.

Examples:

Note On:
- highlight note
- highlight spectral line
- highlight wavelength wheel segment

Note Off:
- remove highlight

Sustained notes remain visible.

Do not use temporary flashes for sustained notes.
