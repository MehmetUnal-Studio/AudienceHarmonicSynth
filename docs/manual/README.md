# Cosmic Microwave - User Manual

> *Zone OSC in. Source-locked Notes Only MIDI out.*

Cosmic Microwave (formerly SpektraSynth) is an audience-driven OSC-to-MIDI router.
Each instance receives one already-separated UDP zone stream, tracks one `finger0`
touch for each source ID, maps normalized X/Y controls through Tonal or Atomic Scale
pitch maps, and sends note lifecycles to Ableton, a virtual endpoint, or a system MIDI
device. U/X selects pitch; V/Y supplies Note-On velocity.

Version 2.8.0 does not generate sound. It remains a silent stereo instrument shell for
Ableton placement and old-session compatibility; the product's behaviour and editor
are MIDI-only. Put sound-producing instruments after it or on receiving MIDI tracks.

The generated performance stream is deliberately **Notes Only**: Note On and Note Off.
It does not emit MPE, CC11, CC74, Channel Pressure, Pitch Bend, RPN setup, or Crowd
Macro CC messages, and no LFO gates or modulates the message flow. Panic retains CC120
and CC123 solely as stuck-note safety messages.

The **TIME FIELD** provides Flow, Grid, and Ensemble timing. New 2.8.0 sessions begin in
Flow with Host clock, stored 1/32 division, and Manual 16/16/100%/16 timing values.
**Note Duration** starts at 16n and controls only the BPM-relative lifetime of each new
MIDI note, not its attack position. Ensemble **Same Note** starts at Tie; Retrigger can
instead articulate every admitted identical-pitch pulse as Note Off then Note On.
Flow and Grid always use Tie. **Source Capacity** starts at 64 while the MIDI
topology remains sixteen channels—four sources per channel; 128 and 256 provide eight
and sixteen per channel. Current saves use state schema 11. Schema-9-or-earlier projects
without a capacity node retain their former 256-source domain during migration.
A genuinely fresh instance claims the lowest free complete External Only factory route
from UDP 6062 / Zone A through UDP 6069 / Zone H. Projects
saved with schema 6 or earlier open with Manual policy, and projects saved before
schema 4 also open in Flow, preserving their established behaviour.

The current release includes explicit Host/External/Mirror output safety, Expected Zone
filtering, exclusive UDP ownership, a pressure-aware Safety Governor with telemetry, a
Venue Preflight/Show Console, and process-local Global Conductor quotas. Routing Safety
also provides Zone A-H factory performance presets for UDP `6062..6069`; A is the Group 1
Leader and B-H are Followers. Preset recall, direct route edits, and Ableton-restored
state win exactly over fresh assignment. If all eight routes are occupied, the new
instance fails closed until a route is freed and **RETRY AUTO** is pressed. The
Capture/Replay Chaos Lab is an external
Node.js rehearsal CLI, not code that runs inside the plugin.

## Chapters

| # | Chapter | What it covers |
|---|---|---|
| 1 | [Getting Started](01-getting-started.md) | Requirements, install/build, product variants, and the first routed MIDI test. |
| 2 | [UI Guide](02-ui-guide.md) | Perform/Show Console views, routing safety, telemetry, Venue Preflight, Global Conductor, Time Field, pitch mapping, simulator, and Panic. |
| 3 | [MIDI Output Setup](03-midi-output-setup.md) | Notes Only policy, source routing, destinations, receiver setup, Panic safety, and upgrade cleanup. |
| 4 | [OSC & the Audience](04-osc-audience.md) | One separated zone per UDP port, zone locking, exclusive ownership, OSC scaling, Time Field/Conductor scheduling, channel ownership, and Chaos Lab rehearsals. |
| 5 | [Pitch Systems & External Tuning](05-tuning-files.md) | Seven tonal maps, 29 element-derived Atomic maps, X-to-pitch selection, nearest-note output, and receiver-side tuning. |
| 6 | [Troubleshooting & FAQ](06-troubleshooting-faq.md) | Silent-shell expectations, missing MIDI, OSC/port problems, channel routing, legacy-controller cleanup, stuck notes, and upgrade questions. |

## Quick orientation

- **First setup?** Start with Chapter 1, then use **+1 Held** in the simulator while
  watching a MIDI monitor or receiving instrument.
- **Splitting a zone across Ableton tracks?** Use **Notes Only**, **Per source 1-16**,
  and the instance's port-named virtual endpoint. Chapters 2 and 4 show the layout.
- **Using the production Omnisphere layout?** Route Channels 1-8 to OMNI1 parts 1-8
  and Channels 9-16 to OMNI2 parts 1-8. Chapter 3 explains this two-instance CPU
  topology and the required show-machine soak.
- **Checking exactly what is emitted?** Chapter 3 documents the Note On/Off contract,
  channel routing, and the two Panic-only CC messages.
- **Want element-derived pitch?** New sessions start at **Atomic / Zn / Core**.
  Chapter 5 explains all density modes and nearest-MIDI-note output.
- **Connecting the audience server?** Chapter 4 documents the exact OSC address and
  normalized value contract.
- **Taming asynchronous crowd timing?** The factory performance preset starts in Flow/Manual;
  choose Grid or Ensemble and Adaptive when the show needs quantized or density-aware
  admission. Chapters 2 and 4 explain both approaches.
- **Coordinating several zone instances?** Put them in one Global Conductor group,
  appoint a Leader, and verify the live quotas in Show Console.
- **Rehearsing loss and burst traffic?** Run the external
  [Capture/Replay Chaos Lab](../chaos-lab.md) between the sender and plugin.
- **No result?** Chapter 6 starts with the shortest routing checklist.

---

*This manual documents the Cosmic Microwave 2.8.0 flagship VST3 and Standalone products.
Other MIDI-oriented targets in the repository have their own, more specialized UI and
parameter sets.*
