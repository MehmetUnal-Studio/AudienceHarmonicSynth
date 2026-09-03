# 02 - UI Guide

Cosmic Microwave is an OSC-to-MIDI router. The editor is focused on selecting the UDP
input, confirming source identity, organizing crowd attacks in time, mapping movement
to notes, choosing Notes Only output, and selecting a MIDI destination.

The product does not generate sound. It remains a silent stereo instrument shell so
Ableton can keep the same device placement and session identity, but its behaviour and
all visible controls are MIDI-only.

## Layout

```
+--------------------------------------------------------------------------+
| COSMIC MICROWAVE   MIDI ONLY   v2.8.0   SOURCES | TOUCHES | MIDI NOTES | ACTIVE NOTES |
+----------------------+------------------------+--------------------------+
| [ PERFORM ] [ SHOW CONSOLE ]                                           |
+----------------------+------------------------+--------------------------+
| PERFORM: OSC / Source Matrix / Time Field / Pitch / MIDI / Simulator    |
| SHOW CONSOLE: Routing Safety / Safety Governor / Venue Preflight        |
|               Global Conductor / Notes Only policy / external Chaos Lab |
+-------------------------------------------------------+------------------+
```

The editor opens at `1280 x 760`, is resizable down to `1000 x 650`, and separates
performance controls from venue engineering. **PERFORM** keeps the source map and
musical controls visible. **SHOW CONSOLE** groups safety, readiness, multi-instance
coordination, and output-policy status without crowding the performance view.

## Header

The header identifies the device as **COSMIC MICROWAVE**, labels its role as
**OSC / MIDI ROUTING**, shows a **MIDI ONLY** badge, and permanently displays the
build-derived product version (for example **v2.8.0**). Four live metrics appear on
the right:

- **SOURCES** - OSC or simulator source IDs with an active `finger0` touch.
- **TOUCHES** - the total number of active `finger0` touches across those sources.
- **NOTES** - note-on messages emitted since the current MIDI state was reset.
- **ACTIVE NOTES** - Time Field voices currently sounding. This is scheduled MIDI
  note state, not an internal sound-engine count.

## OSC INPUT

Enter the UDP listen port for this instance and click **Apply**, or press Return.
Valid ports are `1` through `65535`. Fresh automatic assignment scans the factory
family from `6062` through `6069` and retains the lowest free complete route. Escape
abandons an edit and restores the active port.

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
receiver periodically retries an exact saved/manual route that failed to bind, so
releasing its conflicting owner can restore listening without recreating the device.
Fresh A-H exhaustion is different: it waits for **RETRY AUTO**.

## SOURCE ROUTING

This card is a read-only summary of the current MIDI mode. It does not assign channels
packet by packet.

In **Notes Only / Per source 1-16**, the card shows the stable mapping:

```text
1 -> Ch 1   2 -> Ch 2   ...   16 -> Ch 16   17 -> Ch 1
```

Source `0` is accepted and wraps to Channel 16. Every `u`, `v`, and `on` message
belonging to a source's admitted finger0 touch uses that source's channel.

The summary changes when **Single channel** or **Off** is selected. Its
bottom line reports the zone letters observed in valid OSC addresses and the current
source/touch totals. **Expected Zone** in Show Console can lock the instance to one
letter. Wrong-zone packets are counted and rejected before source state, accepted-
traffic telemetry, and MIDI generation. **Any** is intended for diagnostics; it does
not infer a zone from the UDP port.

## SIMULATOR

The simulator exercises the same source-to-MIDI path without network traffic:

- **Human / Dense / Stress** - choose calibrated natural behaviour, heavier but
  independent crowd activity, or an explicitly bounded worst-case load. The profile
  is temporary test state and does not change saved mapping or Time Field parameters.
- **+1 Held** - add one continuously held `finger0` touch for deterministic pitch,
  channel, and note-hold checks.
- **+25 Crowd** - add a stable 25-person pool. Each participant keeps the same source
  ID but independently presses and releases, so active matrix cells appear and return
  to idle like production Pad traffic.
- **Remove 1** - remove one participant, releasing it first if currently active.
- **Clear** - release and remove all simulated sources.
- **Move active U/V** - continuously change U/V only for currently pressed simulator
  touches. Crowd lifecycle cycling continues when movement is off.

Simulator sources use the same `0..255` source pool and the same channel rules as OSC
sources. The status line distinguishes held touches from `active / pool` crowd counts.
**Clear** affects simulator sources; use **PANIC** for a global release. Do not run the
simulator and live OSC together: they intentionally share the same source identities,
and the editor warns `LOCAL + OSC INPUT` when both are present.

Human is derived from recorded phone timing and motion. For real UDP/parser/bridge
pressure use the server LoadGen; the local simulator intentionally bypasses the network.

## SOURCE MATRIX

The central map keeps 16 fixed Notes Only MIDI-channel columns and changes only its
admitted rows with **Source Capacity**: 64 shows 4 IDs per channel, 128 shows 8, and
256 shows 16. For example, the Channel 1 column begins `1, 17, 33, 49`, while source
`0` occupies the first wrapped position in Channel 16. IDs outside the selected dense
domain are dropped and counted, not modulo-wrapped back into the visible matrix.

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
  bounded by **ATTACKS / STEP** and **ACTIVE LIMIT**. Ordered Off ends the semantic
  scheduled voice without truncating its MIDI tail; a tap admitted after ending before
  its grid opportunity receives the configured minimum gate. When the final fixed MIDI
  tail for an admitted held source ends, that Grid active slot is released so the next
  queued source can enter; the held source remains one-shot until a fresh Off -> On.
- **Ensemble** - each source belongs to a deterministic lane within **SPREAD / STEPS**.
  Attacks receive a fixed semantic gate; a source that remains held is queued for a
  later pulse, while fixed-duration MIDI tails may overlap. **Same Note = Tie** keeps
  identical-pitch ownership continuous; **Retrigger** safely emits Note Off then Note On
  for each admitted pulse. It can be selected when a show needs a pulsed crowd texture.

New 2.8.0 sessions use **Flow**, **Host**, **1/32**, a 100% gate, **Same Note = Tie**, and **Manual** crowd
policy. The prepared Manual values begin at 16 attacks per step, an active limit of 16,
and a 16-step spread. State saved with schema 6 or earlier
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

### NOTE DURATION

**Note Duration** selects `2n`, `4n`, `8n`, `16n`, or `32n`; fresh state and factory
presets use `16n`. It controls only how long each newly created MIDI note lives and
never quantizes the attack. Each Note On snapshots the current valid host BPM, or the
saved Internal BPM when the host value is unavailable, and stores an absolute sample
deadline. Later BPM or duration automation affects only later notes.

An ordinary source Off ends the held gesture but does not shorten a tail that already
owns a deadline. The three-second live watchdog carries an internal Cancel through
Time Field and closes only the disconnected source/finger immediately. Panic,
route/transport reset, and capacity shrink are also immediate safety paths.

### SAME NOTE

**Same Note** is enabled only in **Ensemble**:

- **Tie** is the backward-compatible default. Repeated identical source/channel/note
  pulses extend the existing ownership rather than re-articulating the receiver.
- **Retrigger** performs a bounded Note Off followed by Note On for each admitted
  Ensemble pulse, so a held position can sound like a conventional step sequencer.

This choice does not enable MPE, Pitch Bend, CC11, CC74, or another performance
controller. Flow and Grid force Tie even if a saved project contains Retrigger. Test
Retrigger with the actual receiving instrument because its envelope and voice-steal
behaviour determine the audible articulation.

### Density and gate controls

- **ATTACKS / STEP** (`1..16`) limits how many queued attacks can start on one grid
  boundary.
- **ACTIVE LIMIT** (`1..16`) limits simultaneous scheduled voices.
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

The active result can reach 16. A fast rising and slow falling density envelope,
transition holds, and downward hysteresis stop short gaps or boundary jitter
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

Load warnings use one coherent snapshot of the policy actually applied by the audio
scheduler after Manual/Adaptive, Safety Governor, Global Conductor, and the final
per-block clamp. A stricter venue limit therefore cannot be hidden by a higher saved
slider value.

- **P / PENDING** - lifecycle attacks waiting for a permitted grid/lane boundary.
- **A / ACTIVE** - currently sounding Time Field voices.
- **M / MERGED** - pending work whose admission window elapsed.

With Adaptive selected, the status also shows the recent unique-source load and the
effective attack, active, and spread values. In Flow it reports **GOVERNOR BYPASS**.

Held Ensemble intent is renewed, while a released short tap can expire. MERGED is
cumulative telemetry for judging crowd pressure; it is not packet loss and is not
converted into a MIDI CC or any other musical control. In every mode, incoming U/V bursts update one
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

New sessions default to **Atomic / Zinc / Core**, root C2, across four octaves.
The Pitch System control's tooltip reports the selected element, actual degree count,
and reference wavelength.

An input at X=`0` selects the first available scale step and X=`1` selects the last.
Intermediate values divide the configured pitch table into equal step regions. If a
high root and a long range reach MIDI's upper representable edge, the table stops
safely rather than pinning additional steps to note 127.

For Atomic maps, Notes Only sends the nearest MIDI semitone. The element-derived
frequency still defines the selected catalog degree, but no Pitch Bend is emitted.

Changing any pitch-map control safely releases and retriggers held OSC touches at
their new mapped notes.

## MIDI ROUTING

### OUTPUT

- **Off** - keep receiving and displaying OSC, but emit no MIDI.
- **Notes Only** - send Note On and Note Off on conventional MIDI channels.

Incoming host MIDI is filtered to Note On and Note Off while output is enabled.
Controller, Pressure, Pitch Bend, and system-setup messages are not passed through.

### Notes Only controls

- **Per source 1-16** - source identity selects Channels 1-16 with the stable wrap
  shown above. This is the intended audience-routing mode.
- **Single channel** - send every source through the selected **FIXED CHANNEL**.
- **Source Capacity** - admit `64 / 4 per channel`, `128 / 8 per channel`, or
  `256 / 16 per channel`. MIDI always remains sixteen channels. Fresh state uses 64;
  an older project with no saved capacity migrates to 256 to preserve its old domain.

Each source touch remains an independent note owner. If wrapped source IDs land on the
same channel and note, reference counting keeps that note held until the last owner
reaches its duration deadline or is explicitly cancelled.

V/Y is sampled only when a Note On is created and becomes that note's velocity. A V/Y
change while a note is held emits no MIDI message. U/X selects pitch; crossing into a
different pitch region starts the new note while the old pitch keeps its independent
tail until its stored deadline. No CC11,
CC74, Channel Pressure, Pitch Bend, RPN, MPE, or Crowd Macro CC is generated.

## MIDI OUTPUT

The **DESTINATION** menu contains:

- **Host MIDI Output** - the plugin's MIDI output bus in the DAW.
- **Virtual: Cosmic Microwave <port> Out** - a port-stable system endpoint, for
  example `Cosmic Microwave 6062 Out`.
- Available system or hardware MIDI outputs.

**Rescan** refreshes the destination list. The two lines below it report the selected
route and whether it opened successfully. Selecting an endpoint prepares the external
route; **MIDI Output Path** in Show Console decides whether it is actually used.

For Ableton channel separation, the virtual endpoint is recommended. Receiving tracks
can select `Cosmic Microwave 6062 Out` and then choose Channel 1, Channel 2, and so on.
The endpoint name follows the instance's UDP port, so the Zone A and Zone B instances
remain easy to identify after reopening a session.

## SHOW CONSOLE

### Routing Safety

- **OUTPUT PATH** - **Host Only** sends only to the plugin bus, **External Only** sends
  only to the selected endpoint and clears host output, and **Mirror** deliberately
  sends to both. External Only is the 2.8.0 factory performance default.
- **EXPECTED ZONE** - **Any** accepts every valid zone letter; `A..Z` rejects and
  counts every otherwise-valid wrong-zone packet before it reaches source state.
- **Exclusive UDP ownership** - requires this instance to be the only in-process owner
  of its port. New sessions enable it. A conflict is a visible failure, not a shared
  subscription.

Changing output path, expected zone, exclusive ownership, port, output state, or other
identity-bearing routing safely releases current note state. Sessions written with
schema 7 or earlier intentionally migrate to Mirror, Any, shared ownership, and Safety
Governor Off so an update does not silently reroute a show.

**FACTORY PERFORMANCE PRESET** recalls one complete show baseline rather than only changing a
port. Zone A-H map to UDP `6062..6069` and Expected Zone A-H. Zone A is Group 1 Leader;
Zones B-H are Followers. Every preset also restores Notes Only / Per source 1-16,
External Only with the matching port-named virtual endpoint, Flow / Host / 1/32,
Note Duration 16n / Same Note Tie, Manual attack 16 / active 16 / gate 100% / spread 16, Source
Capacity 64 / 4 per channel, Atomic / Zinc / Core / C2 / four
octaves, exclusive ownership, Group 1 budgets 16/16, Safety Governor Off, and Crowd
Macros Off.

Before any host restore or explicit route edit, a genuinely fresh instance treats this
table as an allocation pool: it retains the lowest free exclusive UDP bind, then
publishes the matching Expected Zone, virtual endpoint, and Conductor role. The status
reads **AUTO READY** after assignment. If all eight ports are occupied, the instance
fails closed with no OSC receiver, virtual endpoint, or Conductor registration; free a
route and press **RETRY AUTO**. It never wraps or silently shares an occupied port.

Recall sends Panic before switching identity, clears transient simulator/live cards,
returns the simulator profile to Human, and then binds the selected UDP/MIDI route. A
matching preset name is shown as active; any edited or host-restored combination is
reported as **CUSTOM / SAVED PROJECT STATE**. Ableton/host state, direct route edits,
and preset recalls remain authoritative and exact. An occupied saved route fails closed
on that same route instead of being shifted by the fresh allocator. Partial legacy
state uses schema-specific compatibility defaults; only missing root UDP/destination
metadata falls back to the Zone A `6062` virtual route.

### Safety Governor and telemetry

The Safety Governor protects the realtime path independently of the musical Adaptive
Crowd Governor. It samples seven pressure signals: validated OSC events/second,
lifecycle queue depth, motion-drop delta, Time Field pending depth, external FIFO depth,
oldest external event age, and audio callback deadline ratio.

| State | Motion updates | Attack ceiling | Active ceiling | Minimum spread | New attacks |
|---|---:|---:|---:|---:|---|
| NORMAL | every update | 16 | 16 | 1 | open |
| HIGH | every 2nd | 8 | 12 | 2 | open |
| CRITICAL | every 4th | 2 | 8 | 4 | open |
| EMERGENCY | every 8th | 1 | 4 | 8 | closed |

Escalation is immediate. Recovery is hysteretic, held for 2/3/5 seconds depending on
the current state, and descends one state at a time. Invalid numeric or clock input
fails closed to EMERGENCY. Motion thinning coalesces to the latest position; it never
reorders lifecycle. Existing voices, Off, watchdog releases, and Panic remain available.
Flow remains direct in NORMAL but obeys the Safety ceilings when pressure rises.

The card shows the state, active reason flags, ingress rate, DSP deadline percentage,
external FIFO pressure/age, current lifecycle/motion queue depths, high-water marks,
dropped count, and coalesced-motion count.

### Venue Preflight

The live checklist evaluates eight independent items:

1. OSC receiver and recent traffic.
2. Expected-zone contract and mismatch count.
3. Exclusive UDP ownership.
4. Output-path/endpoint coherence.
5. Safety Governor enabled and current state.
6. Time Field mode and host-clock lock/fallback.
7. Global Conductor registration and allocation freshness.
8. Source Quality Ready Gate: exact accepted-live-OSC signal census and current local
   gate state; server roster, route manifest, and production soak remain external.

**FAIL** is a blocker, **WARN** is an advisory requiring an operator decision, and
**BYPASS** describes an intentionally disabled subsystem. The summary never replaces a
real MIDI-monitor or Panic rehearsal; it makes the configuration contract inspectable.
Because the screenshot-aligned factory performance presets intentionally leave Safety Governor
Off, row 5 reports a blocker until the operator enables Safety for the show.

### Source Quality Ready Gate

Press **START 64 CHECK** only when the server/load-balancer test clients are ready to
exercise every identity in the selected capacity and the internal Cosmic simulator
population is zero. For the default capacity, the exact proof domain is `0..63`.
Every source must provide finite U, V, and On-1, remain actively held, and keep both U
and V independently fresh throughout one clean two-second pre-ready hold. Repeating
only U does not refresh V, and repeating only V does not refresh U.

The plug-in accepts a maximum heartbeat age of 1.2 seconds as a receiver-side
transport/jitter tolerance. This does not relax the server contract: while held, the
server must publish both U and V with no gap longer than 900 ms. The clean hold also
requires combined U/V motion at or below 50 events/s for each source and 1,200 events/s
for the whole instance. Capacity, motion, and lifecycle drops have separate counters;
capacity or lifecycle loss is a hard latched fault, while any motion loss prevents a
clean pass without being labelled as lifecycle loss.

WARMING holds new attacks but never blocks Off, Cancel, Panic, or notes already
sounding. **STOP 64 CHECK** returns to BYPASS immediately. The coverage line says
**CENSUS** intentionally: it proves a local accepted-OSC signal epoch, not the number
of connected browser sessions and not full venue readiness. Use the server roster and
collision telemetry to prove phone ownership, verify the frozen bridge manifest
separately, and complete a separate 60-second production-path soak with average/P95
traffic evidence.

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

### Notes Only policy

The console states the active v2.8 output contract: performance output is Note On and
Note Off; U selects pitch and V supplies Note-On velocity. Former MPE and Crowd
Expression parameters may remain inside an old session for safe state compatibility,
but they are hidden and inert. They cannot reactivate controller output.

### Capture/Replay Chaos Lab

This card is an operator hand-off to `tools/cosmic-chaos-lab.mjs`. Capture, replay,
load generation, files, and proxy sockets run in a separate Node.js process; none of
them runs inside Cosmic Microwave or on the audio thread. See
[Capture/Replay Chaos Lab](../chaos-lab.md) for commands and failure policies.

### PANIC

**PANIC** is the global safety control. It clears the active source state, sends the
required Note Off messages, then sends CC123 (All Notes Off) and CC120 (All Sound Off)
across the host and selected external route. These are the only generated CC messages.
Use Panic after a sender disconnect, a routing change, or any suspected missing `off`
packet.

## Recommended one-zone workflow

1. Place one Cosmic Microwave instance for each already-separated zone.
2. Recall the matching factory performance preset: Zone A-H use UDP `6062..6069`; keep
   exclusive ownership enabled.
3. Choose **Notes Only -> Per source 1-16** for channel-separated routing.
4. Confirm the factory **Flow / Host / 1/32 / Note Duration 16n / Same Note Tie / Manual** timing
   baseline and **Source Capacity 64 / 4 per channel**; choose Grid,
   Ensemble, or Adaptive only when the show design requires them.
5. Keep External Only plus the port-named virtual destination, or choose Host Only for
   a deliberately host-bus-only Ableton layout.
   Use Mirror only for a deliberately duplicated route.
6. Open Show Console, resolve preflight failures, confirm Safety is NORMAL, and verify
   any Global Conductor group/quota.
7. Confirm source activity at the receiver and test **PANIC** before the audience
   connects.

See [04 - OSC & the Audience](04-osc-audience.md) for the wire format and
[03 - MIDI Output Setup](03-midi-output-setup.md) for receiver configuration and the
exact Notes Only message contract.
