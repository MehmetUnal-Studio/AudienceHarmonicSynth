# 04 - OSC & the Audience

Cosmic Microwave receives normalized audience controls over OSC/UDP and turns them
into Notes Only MIDI. The production server has already separated the zones, so
each plugin instance listens to one dedicated UDP port.

Example layout:

| Audience zone | UDP port | Ableton device | Recommended virtual MIDI endpoint |
|---|---:|---|---|
| A | `6062` | Cosmic Microwave 1 | `Cosmic Microwave 6062 Out` |
| B | `6063` | Cosmic Microwave 2 | `Cosmic Microwave 6063 Out` |
| C | `6064` | Cosmic Microwave 3 | `Cosmic Microwave 6064 Out` |
| D | `6065` | Cosmic Microwave 4 | `Cosmic Microwave 6065 Out` |
| E | `6066` | Cosmic Microwave 5 | `Cosmic Microwave 6066 Out` |
| F | `6067` | Cosmic Microwave 6 | `Cosmic Microwave 6067 Out` |
| G | `6068` | Cosmic Microwave 7 | `Cosmic Microwave 6068 Out` |
| H | `6069` | Cosmic Microwave 8 | `Cosmic Microwave 6069 Out` |

Additional zones follow the same pattern with their own ports and instances. Cosmic
Microwave does not split or forward zones itself. Fresh automatic allocation is
deliberately bounded to the eight complete A-H routes shown above; additional/custom
routes are explicit saved, manual, or preset configuration.

The plugin is behaviourally MIDI-only. It presents a silent stereo instrument shell
to Ableton for host compatibility, but it does not create sound.

## Port and zone are different fields

The UDP port selects which socket an instance listens to. The zone letter remains part
of every OSC address:

```text
UDP 6062  <-  /cs/A/...
UDP 6063  <-  /cs/B/...
```

`6062 -> A` and `6063 -> B` are factory route-pair assignments, not rules inferred from
incoming traffic. A genuinely fresh instance selects the lowest free complete pair,
while Cosmic Microwave still reads each address's zone. In **SHOW CONSOLE**, **Expected
Zone** can be set to the assigned `A..Z`; otherwise-valid traffic from every other zone
is then counted and rejected before source state, accepted-message telemetry, and MIDI.
**Any** disables the filter for diagnostics.

The upstream split must still be correct. Use one already-separated zone, one port, and
one instance, then lock Expected Zone as a second safety boundary. **Exclusive UDP
Port** is on for new sessions, preventing two in-process instances from silently owning
the same socket. A saved/manual route conflict reports an ownership failure and may
recover only that exact route when it becomes available. Fresh A-H exhaustion instead
waits for **RETRY AUTO** and does not continuously rescan. Port or zone-policy changes
release held state before the new contract applies.

## Source and touch identity

An OSC control is identified by:

- **Zone:** `A` through `Z`.
- **Source/participant ID:** a dense zero-based value inside the zone's selected
  Source Capacity.
- **Touch:** `finger0` only.

Every source has one ordered note lifecycle. `finger1` through `finger9` are ignored
before state, crowd counts, zone telemetry, and MIDI generation. This bounds crowd
density to one live intent per phone and prevents accidental multi-touch duplication.

The show freezes one Source Capacity per zone:

| Source Capacity | Admitted IDs | Fixed MIDI channels | Sources per channel |
|---:|---|---:|---:|
| 64 | `0..63` | 16 | 4 |
| 128 | `0..127` | 16 | 8 |
| 256 | `0..255` | 16 | 16 |

An ID at or above the selected capacity is counted and rejected; it is never wrapped
into another identity. The server allocator and the Cosmic instance must use the same
frozen value. Fresh state uses 64. Schema-9-or-earlier projects migrate to 256 because
that was their implicit historical domain.

Channel assignment is source-based, not packet-based:

| Source ID | MIDI channel |
|---:|---:|
| `0` | 16 |
| `1` | 1 |
| `2` | 2 |
| `16` | 16 |
| `17` | 1 |
| `32` | 16 |
| `255` | 15 |

For the primary 1-based convention:

```text
channel = ((source_id - 1) mod 16) + 1
```

Source `0` is valid and wraps backward to Channel 16. Once assigned, a source's
`u`, `v`, and `on` lifecycle stays on that channel. This keeps finger0's note-on
and note-off together even when many sources are active.

## OSC wire format

Canonical addresses use:

```text
/cs/<zone>/<source>/finger0/<param>
```

| Segment | Meaning |
|---|---|
| `/cs/` | Cosmic Symphony/control-surface prefix. |
| `<zone>` | Zone letter `A` through `Z`. |
| `<source>` | Decimal source ID `0` through `255`. |
| `finger0` | The only admitted live touch token; secondary fingers are ignored. |
| `<param>` | Production senders use `u`, `v`, or `on`. |

The `/cs/` prefix, zone letter, and parameter name are accepted without regard to
letter case. The `finger` token itself is deliberately lower-case and strict.

### Parameters

| Parameter | Argument | MIDI meaning |
|---|---|---|
| `u` | Numeric, normally `0..1` | Horizontal position. Selects a pitch from the configured Tonal or Atomic map. |
| `v` | Numeric, normally `0..1` | Vertical position. Supplies the velocity sampled for the next Note On. |
| `on` | Numeric | Non-zero activates source/finger0; zero releases it. |

The live service sends only these three parameters. Cosmic Microwave continues to
accept legacy `/off` (release) and `/line 0..127` (horizontal position divided by
127) so older Max patches do not break; new senders should not produce them.

OSC `int32` and `float32` arguments are accepted. Non-numeric and non-finite values
are ignored. `u` and `v` are clamped to `0..1`; `line` is divided by 127 and then
clamped.

The only continuous-value-to-MIDI-data conversion is Note-On velocity:

```text
velocity = round(clamp(v, 0, 1) * 127), limited to 1..127
```

There is no sound-engine bias in these mappings. X/U controls the pitch-map position;
Y/V controls the velocity of the next scheduled Note On. No CC11, CC74, Channel
Pressure, Pitch Bend, RPN, MPE, or Crowd Macro CC message is generated.

### Message examples

```text
/cs/A/1/finger0/u     0.50   source 1, finger 0: horizontal midpoint
/cs/A/1/finger0/v     0.80   source 1, finger 0: next Note-On velocity 0.8
/cs/A/1/finger0/on    1      activate on Notes Only MIDI Channel 1
/cs/A/1/finger0/on    0      release source 1
/cs/A/17/finger0/on   1      source 17 wraps to Notes Only MIDI Channel 1
/cs/B/16/finger0/on   1      source 16 maps to Notes Only MIDI Channel 16
```

The production start bundle is ordered `u`, `v`, then `on 1`, so the first note uses
the intended pitch and velocity. Release is a separate `on 0`. Later `u`/`v` messages
update a held touch. In Flow they
are rendered directly; in Grid and Ensemble the latest values are sampled at attacks
and grid boundaries. A rendered pitch-map step change retriggers the note. A V-only
change while the note remains held emits nothing and is sampled at the next attack.
`on 0` ends the semantic touch but does not shorten a MIDI note that already captured
its fixed Note Duration deadline. A rendered pitch change starts the new pitch while
the old pitch keeps its stored tail. Internal watchdog Cancel, Panic, route reset, and
capacity shrink are the immediate-cleanup paths; the OSC protocol adds no `/cancel`
message.

Addresses outside the contract are ignored: wrong prefix, missing or extra segments,
source `256` or higher, any finger other than exact lower-case `finger0`, an unknown
parameter, an unsupported argument type, or the wrong number of arguments. Immediate
OSC bundles are supported and preserve their depth-first message order. Dated bundles
are intentionally ignored rather than being executed early; timing belongs to the
host-synchronised Time Field.

## Crowd Time Field

OSC phones remain free-running: they do not need to know the Ableton tempo or send
their touches on a beat. Cosmic Microwave's Time Field organizes the resulting burst
inside each zone instance.

| Mode | OSC-to-MIDI timing |
|---|---|
| **Flow** | Render On/Off and U/V directly. Timing controls are bypassed. |
| **Grid** | Queue each new attack to the selected musical division. Admit at most the configured attacks per step and active voices, using a fair rotating search through pending identities. Ordered Off ends the semantic scheduled voice; admitted short taps receive a minimum gate. Existing MIDI tails keep their deadlines. |
| **Ensemble** | Assign each source to one of the spread lanes, start it only when that lane reaches a grid boundary, apply a fixed semantic gate, and requeue it while the touch remains held. Same Note Tie retains identical-pitch ownership; Retrigger performs Note Off then Note On at every admitted pulse. Fixed-duration MIDI tails may overlap later pulses. |

New 2.8.0 sessions and factory performance presets default to **Flow / Host / 1/32**,
**Note Duration 16n / Same Note Tie**, **16 attacks per step**, **16 active voices**, **100% gate**,
**16 spread steps**, **Source Capacity 64**, and **Manual Crowd Governor**. Flow passes
attacks through directly, so the grid controls remain ready for an intentional switch
to Grid or Ensemble without changing the factory sound. Session state from schema 6 or
earlier still opens in Manual Governor mode without changing its saved policy. State
from schema 3 or earlier additionally migrates to **Flow**, preserving the direct
timing of existing Ableton sets. New state is schema 11. The historical schema-9 step
still coerces former MPE sessions to Notes Only / Per source and disables retired Crowd
Expression output. Schema 10 adds Note Duration and Source Capacity migration. Schema
11 adds Ensemble Same Note and safely restores every missing or hostile value as Tie.

### Adaptive crowd policy

The single **Manual / Adaptive** switch chooses whether the three density controls are
fixed or crowd-aware. Adaptive computes:

```text
observed density = max(currently held sources,
                       unique live sources seen in the last 8 seconds)
```

It then applies these inclusive profiles:

| Sources | Attacks / step | Spread / steps | Active voices |
|---:|---:|---:|---:|
| 0-8 | 4 | 1 | 8 |
| 9-24 | 4 | 2 | 10 |
| 25-64 | 3 | 4 | 12 |
| 65-128 | 2 | 8 | 14 |
| 129-256 | 2 | 16 | 16 |

The density envelope rises quickly, falls slowly, and uses transition holds plus
downward hysteresis. This gives a newly arriving crowd capacity promptly without
collapsing the texture during brief pauses or oscillating at a band boundary.

Adaptive affects Grid and Ensemble only. Flow bypasses it. The selected profile is a
soft policy for future admission: lowering a band never cuts an existing voice, and a
spread change preserves pending opportunities. Ordered Off, the three-second
live-touch watchdog, and Panic are never governed. Gate length remains Manual. The
fixed Manual attack, active-limit, and spread values are stored untouched while
Adaptive is selected and return exactly when Manual is selected again.

### Safety Governor under crowd pressure

The musical Adaptive policy responds to audience density. The separate Safety
Governor responds to system pressure: validated ingress events/second, lifecycle queue,
motion drops, Time Field pending depth, external FIFO depth and age, and callback
deadline use. NORMAL/HIGH/CRITICAL/EMERGENCY profiles progressively thin redundant
motion, cap new attacks and active voices, require wider spread, and finally close
new-attack admission. Escalation is immediate; recovery has
hysteresis and staged holds. Flow stays direct in NORMAL and uses the explicit safety
ceilings only in HIGH/CRITICAL/EMERGENCY. Ordered releases, the live watchdog, and Panic remain
available in every state. Show Console reports both the state and the signal(s) that
caused it.

### Global Conductor across zone instances

Time Field normally governs each zone independently. Global Conductor can coordinate
up to 16 instances in the same plugin process. Instances in one of four isolated groups
publish density; the elected Leader allocates its global attack and voice budgets at
10 Hz using fair density weighting and deterministic rotation. Timed modes apply the
resulting per-zone quota. A stale/missing leader expires after 1.5 seconds and each
instance immediately falls back to its local policy. Global Conductor never sends OSC
between machines and does not coordinate separate host processes.

### Clock behaviour

With **Host** selected, the scheduler follows the host's tempo and PPQ position while
the transport is playing. Every zone instance on that transport therefore shares the
same musical boundary. If the host does not provide a valid playing clock, Cosmic
Microwave falls back to a process-wide monotonic timebase at the saved Internal BPM.
Scheduling continues, while the UI reports that host lock is unavailable.

Selecting **Internal** explicitly uses that common monotonic reference at `40..240
BPM`. It is not a free-running accumulator unique to each instance, so instances in
the same process retain a common phase. The available divisions are `1/4`, `1/8`,
`1/16`, and `1/32`.

### Burst control and lifecycle safety

In Grid and Ensemble, every U/V packet still updates the canonical latest position,
but redundant intermediate movement events are coalesced. The scheduler samples the
latest position when a note attacks and at following grid boundaries. This bounds
redundant pitch-retrigger work without changing source ID, touch identity, channel
ownership, pitch mapping, or the source-to-channel formula.

On/Off transitions remain FIFO ordered and have priority over optional movement
updates. They are never assigned round-robin by packet and are never replaced by the
U/V coalescer. A short tap that enters an Ensemble lane remains pending long enough to
receive at least one full lane-cycle opportunity. If capacity remains unavailable,
excess scheduled work can be coalesced or expired rather than creating an unbounded
queue.

The Time Field's **PENDING**, **ACTIVE**, and **MERGED** status makes that pressure
visible. MERGED counts pending work whose admission window elapsed. Held intent is
renewed, while released short taps can expire; it is not packet loss and does not
generate any MIDI message.

### Independent zone lanes

Ensemble derives a stable lane-phase seed from the instance's UDP port. Zone A on
`6062` and Zone B on `6063` can therefore reuse participant IDs without all equivalent
sources landing on the same tick. The seed decorrelates timing only: the port still
does not define, infer, or filter the OSC zone letter.

## Pitch mapping

Normalized U/X is divided across the pitch table selected in the editor. Both systems
use:

- Root note: `C` through `B`.
- Root octave: `0` through `6`.
- Range: `1` through `6` octaves.

**Tonal** adds a choice of Major, Natural Minor, Pentatonic, Dorian, Lydian, Harmonic
Minor, or Whole Tone. **Atomic** adds an element and density choice. Its 29 catalog
entries span Hydrogen through Zinc (Nitrogen is unavailable in the current dataset),
and the Core/Extended/Microtonal/Scientific/Raw 128 modes cap the available one-octave
degree bank at 7/12/24/48/128 respectively. The actual count may be lower for an
element with fewer usable spectral lines.

U=`0` selects the first step and U=`1` selects the final available step. Each
intermediate region selects one pitch step. The map never emits an invalid MIDI note.
Notes Only rounds an Atomic target to its nearest MIDI semitone. It sends no Pitch Bend.

New sessions default to Atomic / Zinc / Core. Existing schema-2 MIDI-only
sessions restore as Tonal, so an older set does not silently change its pitch map.

## UDP configuration and status

1. Enter the assigned port in **OSC INPUT**.
2. In Show Console, select its **Expected Zone** and enable exclusive ownership.
3. Click **Apply** or press Return.
4. Confirm **Listening ... waiting for data** and exclusive ownership.
5. Send a valid OSC message; confirm **Receiving**, the expected observed zone, and
   zero mismatches.

Valid ports are `1..65535`. A fresh instance claims the lowest free retained exclusive
route from `6062/A` through `6069/H`; the first route on a clean system is Zone A /
`6062`. Invalid text leaves the active listener unchanged. Applying a different port is
an exact manual override and releases held notes before the listener restarts.

If all eight automatic routes are occupied, no OSC receiver, virtual MIDI endpoint, or
Global Conductor registration is opened. Free one route and press **RETRY AUTO** in
Routing Safety. Saved/restored routes, manual edits, and factory-preset recalls never
shift to another route automatically.

The status card counts admitted valid messages. Wrong-zone valid packets have a separate
mismatch counter. It retains the admitted observed-zone set while the listener remains
on that port. Multiple letters are possible only with Expected Zone set to Any and
indicate mixed upstream traffic, not an automatic multi-zone mode.

The virtual output name is derived from the active UDP port:

```text
Cosmic Microwave <port> Out
```

For example, UDP `6062` uses `Cosmic Microwave 6062 Out`. If a virtual destination is
selected and the port changes, the plugin reopens the endpoint with the new port-based
name.

## Ableton routing

Use this layout for each zone:

1. Place one Cosmic Microwave instance on its own Ableton track.
2. Set the instance's UDP port and matching Expected Zone; keep exclusive ownership on.
3. Select **Notes Only** and **Per source 1-16**.
4. For the factory performance preset, leave **TIME FIELD** at
   **Flow / Host / 1/32 / Note Duration 16n / Same Note Tie / Manual** and **Source Capacity 64**.
   Select Grid or Ensemble deliberately when the show requires quantised or pulsed
   timing; Adaptive remains an optional crowd-aware policy.
5. Select **External Only**, then choose **Virtual: Cosmic Microwave <port> Out**.
6. On receiving Ableton MIDI tracks, choose that virtual endpoint under **MIDI From**
   and select Channel 1, Channel 2, and so on.
7. Set the receiving tracks' monitoring/arming as your Live set requires and place the
   destination instruments there.

The production Omnisphere template routes Cosmic Channels 1-8 to **OMNI1** Multi Parts
1-8 and Channels 9-16 to **OMNI2** Multi Parts 1-8. At capacities 64/128/256, each part
therefore serves 4/8/16 source identities. This is still exactly sixteen MIDI channels
and two Omnisphere instances. It does not guarantee host parallelism; soak-test the
real patches, note tails, audio buffer, and planned zone count on the show computer.

The port-named virtual endpoint is recommended because it makes zone ownership and
channel selection explicit. **Host Only** is the alternative when the DAW can route
the device bus directly. **Mirror** makes both paths active and must be chosen
deliberately to avoid duplicate notes.

Zone A and Zone B each get an independent set of Channels 1-16 because they use
different Cosmic Microwave instances and virtual endpoints.

## Simulator

The **SIMULATOR** card sends controls through the same source/touch-to-MIDI path:

- **Human / Dense / Stress** selects the calibrated gesture model, a denser independent
  crowd, or bounded overload. It is ephemeral and never rewrites Cosmic mapping/timing.
- **+1 Held** adds one continuously held source for deterministic mapping tests.
- **+25 Crowd** adds a stable 25-person pool whose `finger0` touches automatically
  alternate between active and idle. Initial attacks are staggered instead of emitted
  as one 25-note burst.
- **Remove 1** removes one participant and releases it first when active.
- **Move active U/V** updates horizontal and vertical values only while a simulated
  touch is active.
- **Clear** releases all simulated sources.

Use it to confirm pitch mapping, source-to-channel assignment, destination selection,
and receiving-track monitoring before the network sender connects. Simulator activity
does not invent a UDP zone; the observed-zone status reflects valid OSC traffic only.
The Adaptive Crowd Governor sees the full simulated participant pool, while the header
and Source Matrix show only currently pressed touches. Safety ingress telemetry remains
real-OSC-only. Clear the simulator before opening live server traffic because both paths
share the same `0..255` source namespace.

The Human model uses independent per-source lifecycle and motion streams. Increasing
the pool does not change the first participants' underlying paths. Dense and Stress
remain single-finger and lifecycle-correct; they change only activity and motion budget.
The local Stress profile is intentionally capped at 4,000 scalar U/V events/s because
it exercises the plugin's JUCE message-thread and control path. The server LoadGen
Stress profile uses 8,000 scalar U/V events/s per zone for distributed network and
Venue Bridge capacity testing. They share lifecycle semantics, not an identical load
ceiling.

## Capture/replay and failure rehearsal

`tools/cosmic-chaos-lab.mjs` is a separate Node.js process that can proxy and capture
raw UDP to NDJSON, replay the original timing at `0.25x..16x`, generate production-form
OSC, and inject seeded loss, duplication, reordering, jitter, or burst loss. It is not
part of the plugin runtime: no file access, replay clock, or capture socket runs on the
audio thread. Put the proxy between the audience server and each zone port during a
rehearsal, never directly into an unreviewed live-show path. See
[Capture/Replay Chaos Lab](../chaos-lab.md).

## Network and lifecycle checklist

- The sender must be able to reach the Cosmic Microwave machine's LAN address and the
  assigned UDP port.
- Allow inbound UDP for the standalone application or Ableton in the system firewall.
- UDP has no acknowledgements or retransmission. Send explicit `/on 0` releases. As a
  second line of defence, an active live touch with no valid U/V/On heartbeat for three
  seconds creates one internal ordered **Cancel**. Cancel closes the semantic source
  and its scheduled MIDI tails immediately; **PANIC** remains the manual global reset.
- Send `u`/`v` only as fast as the performance requires. Flow, Grid, and Ensemble keep
  the latest U/V per touch while a separate priority queue protects On/Off, so redundant
  motion cannot starve a release; sender-side restraint still reduces network load.
- Confirm one zone, one port, and one instance together before the audience connects.
- Lock Expected Zone, confirm exclusive ownership, and resolve all failed Venue
  Preflight rows before admitting the audience.
- If a release (`/on 0`) packet is lost or the sender disappears, first allow the
  three-second watchdog to close that touch. Use **PANIC** when an immediate global reset
  is required; it clears live source state and sends bounded all-off safety messages to
  the host and selected external destination.
