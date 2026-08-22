# Cosmic Microwave - User Manual

> *Zone OSC in. Source-locked Normal MIDI or expressive MPE out.*

Cosmic Microwave (formerly SpektraSynth) is an audience-driven OSC-to-MIDI router.
Each instance receives one already-separated UDP zone stream, tracks one `finger0`
touch for each source ID, maps normalized X/Y controls through Tonal or Atomic Scale
pitch maps and expression, and sends MIDI to Ableton, a virtual endpoint, or a system
MIDI device.

Version 2.5.0 does not generate sound. It remains a silent stereo instrument shell for
Ableton placement and old-session compatibility; the product's behaviour and editor
are MIDI-only. Put sound-producing instruments after it or on receiving MIDI tracks.

The **TIME FIELD** provides Flow, Grid, and Ensemble timing. New 2.5.0 sessions begin in
Ensemble on a host-synced 1/16 grid with the Adaptive Crowd Governor enabled. Projects
saved with schema 6 or earlier open with Manual policy, and projects saved before
schema 4 also open in Flow, preserving their established behaviour.

Version 2.5 adds explicit Host/External/Mirror output safety, Expected Zone filtering,
exclusive UDP ownership, a pressure-aware Safety Governor with telemetry, a Venue
Preflight/Show Console, process-local Global Conductor quotas, and optional Crowd
Expression CC macros. The Capture/Replay Chaos Lab is an external Node.js rehearsal
CLI, not code that runs inside the plugin.

## Chapters

| # | Chapter | What it covers |
|---|---|---|
| 1 | [Getting Started](01-getting-started.md) | Requirements, install/build, product variants, and the first routed MIDI test. |
| 2 | [UI Guide](02-ui-guide.md) | Perform/Show Console views, routing safety, telemetry, Venue Preflight, Global Conductor, Crowd Expression, Time Field, pitch mapping, simulator, and Panic. |
| 3 | [MPE Setup](03-mpe-setup.md) | Normal MIDI source routing, MPE zones, expression messages, receiver setup, channel allocation, and Ableton choices. |
| 4 | [OSC & the Audience](04-osc-audience.md) | One separated zone per UDP port, zone locking, exclusive ownership, OSC scaling, Time Field/Conductor scheduling, channel ownership, and Chaos Lab rehearsals. |
| 5 | [Pitch Systems & External Tuning](05-tuning-files.md) | Seven tonal maps, 29 element-derived Atomic maps, X-to-pitch selection, Normal/MPE tuning, and receiver-side tuning. |
| 6 | [Troubleshooting & FAQ](06-troubleshooting-faq.md) | Silent-shell expectations, missing MIDI, OSC/port problems, channel routing, MPE expression, stuck notes, and upgrade questions. |

## Quick orientation

- **First setup?** Start with Chapter 1, then use **+ Source** in the simulator while
  watching a MIDI monitor or receiving instrument.
- **Splitting a zone across Ableton tracks?** Use Normal MIDI, **Per source 1-16**,
  and the instance's port-named virtual endpoint. Chapters 2 and 4 show the layout.
- **Need per-source-touch expression?** Use MPE and match the receiver's zone and bend range;
  see Chapter 3.
- **Want element-derived pitch?** New sessions start at **Atomic / He / Extended**.
  Chapter 5 explains all density modes and the Normal-MIDI-versus-MPE pitch result.
- **Connecting the audience server?** Chapter 4 documents the exact OSC address and
  normalized value contract.
- **Taming asynchronous crowd timing?** Start with Ensemble's default 1/16 grid and
  Adaptive policy; switch to Manual when you want fixed attack, active-limit, and
  spread values. Chapters 2 and 4 explain both approaches.
- **Coordinating several zone instances?** Put them in one Global Conductor group,
  appoint a Leader, and verify the live quotas in Show Console.
- **Driving global effects or scenes?** Enable Crowd Expression and map its density,
  centroid X/Y, and motion CCs; it is disabled by default.
- **Rehearsing loss and burst traffic?** Run the external
  [Capture/Replay Chaos Lab](../chaos-lab.md) between the sender and plugin.
- **No result?** Chapter 6 starts with the shortest routing checklist.

---

*This manual documents the Cosmic Microwave 2.5.0 flagship VST3 and Standalone products.
Other MIDI-oriented targets in the repository have their own, more specialized UI and
parameter sets.*
