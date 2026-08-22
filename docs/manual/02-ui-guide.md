# 02 - UI Guide

Cosmic Microwave is an OSC-to-MIDI router. The editor is focused on selecting the UDP
input, confirming source identity, organizing crowd attacks in time, mapping movement
to notes, choosing Normal MIDI or MPE, and selecting a MIDI destination.

The product does not generate sound. It remains a silent stereo instrument shell so
Ableton can keep the same device placement and session identity, but its behaviour and
all visible controls are MIDI-only.

## Layout

```
+--------------------------------------------------------------------------+
| COSMIC MICROWAVE   MIDI ONLY   v2.5.0   SOURCES | TOUCHES | MIDI NOTES | MPE VOICES |
+----------------------+------------------------+--------------------------+
| [ PERFORM ] [ SHOW CONSOLE ]                                           |
+----------------------+------------------------+--------------------------+
| PERFORM: OSC / Source Matrix / Time Field / Pitch / MIDI / Simulator    |
| SHOW CONSOLE: Routing Safety / Safety Governor / Venue Preflight        |
|               Global Conductor / Crowd Expression / external Chaos Lab |
+-------------------------------------------------------+------------------+
```

The editor opens at `1280 x 760`, is resizable down to `1000 x 650`, and separates
performance controls from venue engineering. **PERFORM** keeps the source map and
musical controls visible. **SHOW CONSOLE** groups safety, readiness, multi-instance
coordination, and aggregate-control features without crowding the performance view.

## Header

The header identifies the device as **COSMIC MICROWAVE**, labels its role as
**OSC / MIDI ROUTING**, shows a **MIDI ONLY** badge, and permanently displays the
build-derived product version (for example **v2.5.0**). Four live metrics appear on
the right:

- **SOURCES** - OSC or simulator source IDs with an active `finger0` touch.
- **TOUCHES** - the total number of active `finger0` touches across those sources.
- **NOTES** - note-on messages emitted since the current MIDI state was reset.
- **MPE VOICES** - member channels currently occupied in MPE mode. This is MIDI
  channel allocation, not an internal sound-engine count.

## OSC INPUT

Enter the UDP listen port for this instance and click **Apply**, or press Return.
Valid ports are `1` through `65535`; the default is `6060`. Escape abandons an edit
and restores the active port.

The status line distinguishes these states:

- **Listening ... waiting for data** - the socket is ready but has not received a
  valid message.
- **Receiving** - valid OSC traffic arrived recently.
- **Listening ... last message ... ago** - the socket is still ready and shows the
  age of the most recent valid message.
- **OWNERSHIP CONFLICT** or another red error - exclusive ownership could not be
  established, the port could not be used, or the receiver has no free client slot.

The address reminder beneath the status is:

```text
/cs/{zone}/{source}/finger0/{u|v|on}
```

Each instance listens to one UDP port. The upstream server should therefore send one
already-separated zone to each instance. The zone is still read from each OSC address;
it is not inferred from the port number. New sessions request exclusive ownership. The
receiver periodically retries a failed exclusive bind, so releasing the conflicting
owner can restore listening without recreating the device.

## SOURCE ROUTING

This card is a read-only summary of the current MIDI mode. It does not assign channels
packet by packet.

In **Normal MIDI / Per source 1-16**, the card shows the stable mapping:

```text
1 -> Ch 1   2 -> Ch 2   ...   16 -> Ch 16   17 -> Ch 1
```

Source `0` is accepted and wraps to Channel 16. Every `u`, `v`, and `on` message
belonging to a source's admitted finger0 touch uses that source's channel.

The summary changes when **Single channel**, **MPE MIDI**, or **Off** is selected. Its
bottom line reports the zone letters observed in valid OSC addresses and the current
source/touch totals. **Expected Zone** in Show Console can lock the instance to one
letter. Wrong-zone packets are counted and rejected before source state, accepted-
traffic telemetry, and MIDI generation. **Any** is intended for diagnostics; it does
not infer a zone from the UDP port.

## SIMULATOR

The simulator exercises the same source-to-MIDI path without network traffic:

- **+ Source** - add one simulated source.
- **+ 25** - add 25 simulated sources.
- **Remove** - release one simulated source.
- **Clear** - release and remove all simulated sources.
- **Random movement** - continuously change the simulated horizontal and vertical
  values.

Simulator sources use the same `0..255` source pool and the same channel rules as OSC
sources. **Clear** affects simulator sources; use **PANIC** for a global release.

## SOURCE MATRIX

The central map displays all 256 source IDs in 16 columns. Each column represents one
Normal MIDI channel, and each column contains the 16 IDs assigned to it. For example,
the Channel 1 column contains `1, 17, 33, ...`, while source `0` occupies the final
cell of the Channel 16 column.

Inactive cells are dim. An active source lights its cell and shows a white point whose
position follows the source's most recent X/Y values. The map is a monitor; selecting
a cell does not change routing.

## TIME FIELD

The **TIME FIELD** turns independently timed phone gestures into one shared temporal
system while keeping every source's single-touch lifecycle intact.

### MODE

- **Flow** - direct response. On/Off and movement retain their incoming block timing;
  the other Time Field controls are bypassed. This matches pre-2.2 behaviour.
- **Grid** - new attacks wait for the next selected division. Selection is fair and
  bounded by **ATTACKS / STEP** and **ACTIVE LIMIT**. A held note releases on its
  ordered Off; a tap admitted after ending before its grid opportunity receives the
  configured minimum gate.
- **Ensemble** - each source belongs to a deterministic lane within **SPREAD / STEPS**.
  Attacks receive a fixed gate; a source that remains held is queued for a later pulse.
  This is the default for new sessions.

New 2.5.0 sessions use **Ensemble**, **Host**, **1/16**, a 70% gate, and **Adaptive**
crowd policy. The preserved Manual values begin at four attacks per step, an active
limit of 16, and a four-step spread. MPE can use only 15 member channels, so its
effective active limit is always capped at 15. State saved with schema 6 or earlier
opens in **Manual**; state saved before schema 4 additionally opens in **Flow**. These
migration rules avoid changing the established timing of an older set.

### CLOCK and DIVISION

**Host** follows the host tempo and PPQ position while its transport is playing. If
the host clock is incomplete or stopped, the scheduler continues on a process-wide
monotonic fallback at the **INTERNAL BPM**. The status shows **WAIT** in that fallback
state because host lock is absent, even though safe scheduling continues.

**Internal** explicitly uses the same common monotonic timebase at `40..240 BPM`.
Separate instances therefore share an absolute grid even without usable host PPQ.
**DIVISION** selects `1/4`, `1/8`, `1/16`, or `1/32`. The Internal BPM control appears
when Internal clocking is selected in a timed mode.

### Density and gate controls

- **ATTACKS / STEP** (`1..16`) limits how many queued attacks can start on one grid
  boundary.
- **ACTIVE LIMIT** (`1..16`) limits simultaneous scheduled voices. The MPE runtime
  cap is 15.
- **GATE** (`5..100%`) sets the fixed Ensemble note length and the minimum Grid gate
  for a short tap.
- **SPREAD / STEPS** (`1`, `2`, `4`, `8`, or `16`) defines Ensemble's lane cycle.
  Grid uses the base division directly.

### Manual / Adaptive Crowd Governor

The pill in the TIME FIELD header is the single policy switch:

- **Manual** uses the saved or automated **ATTACKS / STEP**, **ACTIVE LIMIT**, and
  **SPREAD / STEPS** controls.
- **Adaptive** replaces those three control readouts with their current effective
  `AUTO` values. Their Manual values remain stored and are restored exactly when you
  switch back. **GATE** stays Manual in both policies.

Adaptive uses the larger of the current held-source count and the number of unique live
OSC sources active during the preceding eight seconds. It selects these inclusive
bands:

| Observed sources | Attacks / step | Spread / steps | Active voices |
|---:|---:|---:|---:|
| 0-8 | 4 | 1 | 8 |
| 9-24 | 4 | 2 | 10 |
| 25-64 | 3 | 4 | 12 |
| 65-128 | 2 | 8 | 14 |
| 129-256 | 2 | 16 | 16 |

The active result is capped at 15 in MPE. A fast rising and slow falling density
envelope, transition holds, and downward hysteresis stop short gaps or boundary jitter
from making the policy flicker. The Governor applies only in Grid and Ensemble; Flow
shows it as bypassed and keeps direct timing.

These are soft admission changes. A lower recommendation controls future attacks but
does not stop a voice already sounding. Spread changes preserve pending opportunities,
and ordered Off, the live-source watchdog, and Panic always pass through their safety
paths.

In Ensemble, the instance's UDP port supplies a stable phase seed for the lane map.
Zones that reuse the same source IDs on different ports are therefore decorrelated
instead of all attacking on the same host tick. A pending short tap remains eligible
for at least one full lane cycle before it can expire.

### Live status

The compact status reports clock lock, effective BPM/division, and:

- **P / PENDING** - lifecycle attacks waiting for a permitted grid/lane boundary.
- **A / ACTIVE** - currently sounding Time Field voices.
- **M / MERGED** - scheduled work coalesced or expired after it could not be admitted.

With Adaptive selected, the status also shows the recent unique-source load and the
effective attack, active, and spread values. In Flow it reports **GOVERNOR BYPASS**.

MERGED is cumulative telemetry for judging crowd pressure; it is not converted into a
MIDI CC or any other musical control. In every mode, incoming U/V bursts update one
latest-value state while a separate priority queue protects On/Off. Flow consumes the
latest movement markers directly; Grid and Ensemble sample the canonical state at
attacks and grid boundaries. On/Off events remain ordered and are never replaced by
movement coalescing.

## PITCH MAPPING

The **PITCH MAPPING** card controls how normalized horizontal position is
mapped. The compact selector in the card header chooses **Tonal** or **Atomic**. Both
systems share:

- **ROOT** - `C` through `B`.
- **OCTAVE** - root octave `0` through `6`.
- **RANGE** - `1` through `6` octaves.

With **Tonal** selected, **SCALE** offers **Major**, **Natural Minor**,
**Pentatonic**, **Dorian**, **Lydian**, **Harmonic Minor**, and **Whole Tone**. These
are standard 12-TET maps.

With **Atomic** selected, two fields replace Scale:

- **ELEMENT** - one of 29 stored element spectra from Hydrogen (`H`) through Zinc
  (`Zn`).
- **DENSITY** - **Core**, **Extended**, **Microtonal**, **Scientific**, or
  **Raw 128**. Their maximum degree counts per octave are respectively 7, 12, 24,
  48, and 128; an element can contain fewer usable degrees than the cap.

New sessions default to **Atomic / Helium / Extended**, root C2, across four octaves.
The Pitch System control's tooltip reports the selected element, actual degree count,
and reference wavelength.

An input at X=`0` selects the first available scale step and X=`1` selects the last.
Intermediate values divide the configured pitch table into equal step regions. If a
high root and a long range reach MIDI's upper representable edge, the table stops
safely rather than pinning additional steps to note 127.

For Atomic maps, Normal MIDI sends the nearest semitone. MPE uses the exact
element-derived frequency target, represented by the nearest base note and per-note
pitch bend. Match the receiving instrument's MPE bend range.

Changing any pitch-map control safely releases and retriggers held OSC touches at
their new mapped notes.

## MIDI ROUTING

### OUTPUT

- **Off** - keep receiving and displaying OSC, but emit no MIDI.
- **Normal MIDI** - send conventional channel MIDI.
- **MPE MIDI** - allocate a member channel per active source touch and send per-note
  expression.

Incoming MIDI from the host is passed through while output is enabled.

### Normal MIDI controls

- **Per source 1-16** - source identity selects Channels 1-16 with the stable wrap
  shown above. This is the intended audience-routing mode.
- **Single channel** - send every source through the selected **FIXED CHANNEL**.

Each source touch remains an independent note owner. If wrapped source IDs land on the
same channel and note, reference counting keeps that note held until the last owner
releases it.

### MPE controls

- **ZONE** - **Lower** uses master Channel 1 and members 2-16; **Upper** uses master
  Channel 16 and members 1-15.
- **BEND RANGE** - `+/-2`, `+/-12`, `+/-24`, or `+/-48` semitones. Match this on the
  receiving instrument.
- **PITCH MOTION** - **Retrigger** starts a new MIDI note when a mapped pitch changes;
  **Glide** can update pitch bend while the target remains within the same base note.
- **Send MPE setup** - send the MPE zone message and member-channel pitch-bend-range
  RPN messages when setup is required.

MPE has 15 member channels. When all are occupied, the oldest active MPE note is
released before its member channel is reused.

## MIDI OUTPUT

The **DESTINATION** menu contains:

- **Host MIDI Output** - the plugin's MIDI output bus in the DAW.
- **Virtual: Cosmic Microwave <port> Out** - a port-stable system endpoint, for
  example `Cosmic Microwave 6060 Out`.
- Available system or hardware MIDI outputs.

**Rescan** refreshes the destination list. The two lines below it report the selected
route and whether it opened successfully. Selecting an endpoint prepares the external
route; **MIDI Output Path** in Show Console decides whether it is actually used.

For Ableton channel separation, the virtual endpoint is recommended. Receiving tracks
can select `Cosmic Microwave 6060 Out` and then choose Channel 1, Channel 2, and so on.
The endpoint name follows the instance's UDP port, so the Zone A and Zone B instances
remain easy to identify after reopening a session.

## SHOW CONSOLE

### Routing Safety

- **OUTPUT PATH** - **Host Only** sends only to the plugin bus, **External Only** sends
  only to the selected endpoint and clears host output, and **Mirror** deliberately
  sends to both. Host Only is the new-session default.
- **EXPECTED ZONE** - **Any** accepts every valid zone letter; `A..Z` rejects and
  counts every otherwise-valid wrong-zone packet before it reaches source state.
- **Exclusive UDP ownership** - requires this instance to be the only in-process owner
  of its port. New sessions enable it. A conflict is a visible failure, not a shared
  subscription.

Changing output path, expected zone, exclusive ownership, port, MIDI protocol, or other
identity-bearing routing safely releases current note state. Sessions written with
schema 7 or earlier intentionally migrate to Mirror, Any, shared ownership, and Safety
Governor Off so an update does not silently reroute a show.

### Safety Governor and telemetry

The Safety Governor protects the realtime path independently of the musical Adaptive
Crowd Governor. It samples seven pressure signals: validated OSC events/second,
lifecycle queue depth, motion-drop delta, Time Field pending depth, external FIFO depth,
oldest external event age, and audio callback deadline ratio.

| State | Motion updates | Attack ceiling | Active ceiling | Minimum spread | New attacks | Macros |
|---|---:|---:|---:|---:|---|---|
| NORMAL | every update | 16 | 16 | 1 | open | enabled |
| HIGH | every 2nd | 8 | 12 | 2 | open | enabled |
| CRITICAL | every 4th | 2 | 8 | 4 | open | suspended |
| EMERGENCY | every 8th | 1 | 4 | 8 | closed | suspended |

Escalation is immediate. Recovery is hysteretic, held for 2/3/5 seconds depending on
the current state, and descends one state at a time. Invalid numeric or clock input
fails closed to EMERGENCY. Motion thinning coalesces to the latest position; it never
reorders lifecycle. Existing voices, Off, watchdog releases, and Panic remain available.
Flow remains direct in NORMAL but obeys the Safety ceilings when pressure rises.

The card shows the state, active reason flags, ingress rate, DSP deadline percentage,
external FIFO pressure/age, current lifecycle/motion queue depths, high-water marks,
dropped count, and coalesced-motion count.

### Venue Preflight

The live checklist evaluates seven independent items:

1. OSC receiver and recent traffic.
2. Expected-zone contract and mismatch count.
3. Exclusive UDP ownership.
4. Output-path/endpoint coherence.
5. Safety Governor enabled and current state.
6. Time Field mode and host-clock lock/fallback.
7. Global Conductor registration and allocation freshness.

**FAIL** is a blocker, **WARN** is an advisory requiring an operator decision, and
**BYPASS** describes an intentionally disabled subsystem. The summary never replaces a
real MIDI-monitor or Panic rehearsal; it makes the configuration contract inspectable.

### Global Conductor

Global Conductor coordinates at most 16 Cosmic Microwave instances loaded in the same
plugin process. Choose one of four isolated groups and a role:

- **Off** - use the local Time Field policy.
- **Leader** - participate and offer the group's attack (`1..64`) and voice (`1..128`)
  budgets. If several leaders exist, the lowest UDP port wins deterministically.
- **Follower** - publish local density and consume the elected leader's allocation.

At 10 Hz the leader divides budgets fairly across live group members, weighted by
density with deterministic rotation when capacity is scarce. The status shows
registration, Global/Local Fallback/Bypassed source, current attack/voice quota, active
zone count, and leader port. A missing or stale publication expires after 1.5 seconds
and falls back to the instance's local policy without blocking the audio thread.
Global quotas apply to timed modes; Flow remains the direct diagnostic mode.

### Crowd Expression macros

When enabled, the analyzer scans the fixed 256-source model and emits change-only MIDI
CC snapshots after note lifecycle traffic:

| Macro | Default | Meaning |
|---|---:|---|
| Density | CC20 | Active sources normalized to the 256-source capacity. |
| Centroid X | CC21 | Mean horizontal position of active sources with valid positions. |
| Centroid Y | CC22 | Mean vertical position of active sources with valid positions. |
| Motion | CC23 | Smoothed aggregate change in valid source positions. |

Choose Channel 1-16 or Broadcast and 5, 10, 20, or 30 Hz. The first enabled tick sends
all four values; later ticks send only changed values. An empty crowd reports density
and motion 0 with both centroids centred at 64. Non-finite positions are excluded from
centroid/motion while the participant can still count as active. CRITICAL and EMERGENCY
suspend MIDI macro emission while telemetry continues, and recovery rehydrates a full
snapshot.

### Capture/Replay Chaos Lab

This card is an operator hand-off to `tools/cosmic-chaos-lab.mjs`. Capture, replay,
load generation, files, and proxy sockets run in a separate Node.js process; none of
them runs inside Cosmic Microwave or on the audio thread. See
[Capture/Replay Chaos Lab](../chaos-lab.md) for commands and failure policies.

### PANIC

**PANIC** is the global safety control. It clears the active source state and sends
note-off, channel-pressure reset, centered pitch bend, All Notes Off, and All Sound
Off messages as appropriate across the host and selected external route. Use it after
a sender disconnect, a routing change, or any suspected missing `off` packet.

## Recommended one-zone workflow

1. Place one Cosmic Microwave instance for each already-separated zone.
2. Set the instance's UDP port and matching Expected Zone, for example `6060` / A and
   `6061` / B; keep exclusive ownership enabled.
3. Choose **Normal MIDI -> Per source 1-16** for channel-separated routing, or choose
   **MPE MIDI** for per-note expression.
4. Leave **Ensemble / Host / 1/16 / Adaptive** for the starting crowd-control preset,
   or use Flow while checking the raw end-to-end route.
5. Choose Host Only, or choose External Only plus the port-named virtual destination.
   Use Mirror only for a deliberately duplicated route.
6. Open Show Console, resolve preflight failures, confirm Safety is NORMAL, and verify
   any Global Conductor group/quota and Crowd Expression mappings.
7. Confirm source activity at the receiver and test **PANIC** before the audience
   connects.

See [04 - OSC & the Audience](04-osc-audience.md) for the wire format and
[03 - MPE Setup](03-mpe-setup.md) for receiver configuration.
