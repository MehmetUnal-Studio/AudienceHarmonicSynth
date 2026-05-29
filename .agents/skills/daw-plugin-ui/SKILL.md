---
name: daw-plugin-ui
description: Use this skill when designing or reviewing DAW plugin UI/UX, especially dark themes, scalable layouts, high-DPI rendering, touch support, keyboard navigation, panel consistency, color hierarchy, and parameter grouping.
---

# DAW Plugin UI Skill

## Purpose
Use this skill for music software and DAW plugin interface design.

The UI should feel like a professional musical instrument, not a generic app.

## Core UI principles

- dark theme first
- scalable layouts
- high DPI support
- touch support
- keyboard navigation
- panel consistency
- color hierarchy
- parameter grouping

## Dark theme first

The default visual language should work well in dark studios and stage environments.

Use:
- dark background
- soft contrast
- readable text
- controlled accent colors
- no unnecessary bright areas

Avoid:
- white full-screen panels
- low-contrast grey text
- excessive neon glow
- noisy backgrounds behind text

## Scalable layouts

The UI must remain usable at different plugin window sizes.

Rules:
- avoid fixed pixel-only layouts where possible
- use relative positioning or layout helpers
- prioritize important controls when space is limited
- avoid overlapping components
- preserve readable minimum sizes

## High DPI support

The interface must look sharp on Retina / high-DPI screens.

Rules:
- avoid bitmap-only UI elements unless multiple resolutions exist
- prefer vector drawing for custom components
- test scaling behavior
- avoid hardcoded tiny font sizes
- ensure text remains crisp

## Touch support

Controls should be usable on touchscreen devices and performance setups.

Rules:
- avoid tiny knobs/sliders
- provide enough spacing between interactive controls
- make important performance controls larger
- avoid requiring precise mouse-only interaction for live use

## Keyboard navigation

The UI should be usable with keyboard workflows where practical.

Rules:
- important controls should be reachable or focusable if applicable
- shortcuts should not conflict with DAW shortcuts
- text entry should not accidentally capture transport keys
- escape/enter behavior should be predictable

## Panel consistency

Panels should follow a consistent visual and layout language.

Rules:
- consistent spacing
- consistent typography
- consistent header style
- consistent control alignment
- consistent enable/disable visual treatment
- consistent empty-state behavior

## Color hierarchy

Color should communicate importance and state.

Use color for:
- active notes
- selected engine
- warnings
- modulation/activity
- focused/active controls

Avoid:
- too many unrelated accent colors
- decorative colors that compete with data
- making inactive controls look active

## Parameter grouping

Group controls by musical meaning, not by implementation detail.

Good groups:
- Engine
- Pitch / Scale
- Timbre
- Space / FX
- Performance
- MIDI / MPE
- Debug / Monitor

Bad groups:
- random parameter dumps
- controls with no effect in the current mode
- hidden dependencies without explanation

## Context-sensitive UI

Only show controls that are meaningful in the current mode.

If a control has no effect:
- hide it
- or disable and visually de-emphasize it

Never show dead controls as if they are active.

## Review checklist

When reviewing UI changes, check:

- Is the hierarchy clear?
- Are irrelevant controls hidden or disabled?
- Does the layout scale?
- Is it readable on high-DPI displays?
- Are important controls large enough?
- Are colors used consistently?
- Are parameters grouped musically?
- Does the UI avoid clutter?
- Does the change preserve realtime safety?
