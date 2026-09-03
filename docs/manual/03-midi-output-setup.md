# 03 - MIDI Output Setup

Cosmic Microwave 2.8.0 uses one deliberately narrow performance protocol: **Notes
Only**. Audience gestures generate Note On and Note Off messages on conventional MIDI
Channels 1-16. The product does not provide an MPE or LFO message-gating mode and does
not generate musical Control Change, Channel Pressure, Pitch Bend, or RPN setup
messages.

This keeps the audience-to-instrument boundary predictable when several sources share
the same receiving instrument or channel.

## 1. Choose the output state

The **OUTPUT** selector has two choices:

- **Off** - continue receiving, validating, scheduling, and displaying OSC, but emit no
  performance MIDI.
- **Notes Only** - emit Note On and Note Off according to the current Time Field, pitch
  map, and source-channel routing.

Old sessions can still contain retired protocol parameters in their saved state.
Every legacy non-Off protocol choice is treated as Notes Only at runtime. The retired
settings are hidden and inert; automation cannot reactivate MPE or controller output.

## 2. Exact performance message contract

For OSC and simulator sources, normal performance output contains only:

| Event | MIDI output |
|---|---|
| A scheduled attack | Note On with mapped note, source channel, V-derived velocity, and a BPM-relative deadline captured from the current Note Duration. |
| A normal OSC release or Time Field gate end | End the semantic held state. The already-started MIDI tail keeps its captured deadline. |
| U crosses into another pitch region while sounding | Start a new Note On at the newly mapped pitch. The previous pitch keeps its own captured tail. |
| V changes while sounding | No MIDI message; the latest V is used by the next Note On. |

The velocity conversion is:

```text
velocity = round(clamp(v, 0, 1) * 127), limited to 1..127
```

U/X selects a step in the configured Tonal or Atomic pitch table. Tonal steps are
ordinary 12-TET notes. Atomic targets are represented by the nearest MIDI semitone.
Cosmic Microwave does not send Pitch Bend to restore the fractional Atomic offset.

The generated performance stream does **not** contain:

- CC11 or CC74;
- any other musical CC, including retired Crowd Macro CCs;
- Channel Pressure or Polyphonic Key Pressure;
- Pitch Bend;
- MPE zone/member-channel messages; or
- RPN/NRPN setup messages.

Incoming host MIDI is also filtered to Note On and Note Off while Notes Only is
enabled. Other incoming MIDI messages are not forwarded through the plugin.

### Fixed Note Duration

**Note Duration** is independent of the Time Field division. It changes the length of
every newly generated note without moving or quantising its start:

| Choice | Quarter-note length | Duration at 120 BPM |
|---|---:|---:|
| `2n` | 2 beats | 1,000 ms |
| `4n` | 1 beat | 500 ms |
| `8n` | 1/2 beat | 250 ms |
| `16n` | 1/4 beat | 125 ms |
| `32n` | 1/8 beat | 62.5 ms |

Each Note On snapshots the current BPM and duration. Later tempo or Note Duration
changes affect only subsequent attacks. Deadline processing also runs in silent audio
blocks, so a quiet OSC interval cannot strand a note.

An ordinary `/on 0`, Flow release, or Time Field gate end does not truncate an already
started tail. The internal three-second live-touch watchdog uses a distinct **Cancel**
event and immediately removes all scheduled ownerships for that semantic source.
Panic, route reset, capacity shrink, and processor reset also clean up immediately.
Cancel is internal safety provenance; the server does not send a new `/cancel` OSC
message.

### Ensemble Same Note articulation

The **Same Note** choice applies only when the Time Field mode is **Ensemble**:

- **Tie** (factory and migration default) retains one source/channel/note ownership.
  Later admitted pulses extend it without a new attack.
- **Retrigger** safely emits Note Off then Note On for that same ownership on every
  admitted pulse. This gives a held source an audible sequencer-like re-articulation.

Flow and Grid always force Tie. Retrigger is still conventional Notes Only MIDI: it
does not enable MPE, Pitch Bend, pressure, CC11, CC74, or RPN. Because receivers differ
in envelope reset and repeated-note handling, approve both modes with the actual show
instrument and patch.

## 3. Source-to-channel routing

**Per source 1-16** is the recommended audience mode. Channel ownership is derived
from source identity, not packet order:

```text
1 -> Ch 1   2 -> Ch 2   ...   16 -> Ch 16   17 -> Ch 1
0 -> Ch 16
```

For the normal 1-based source convention:

```text
channel = ((source_id - 1) mod 16) + 1
```

Source `0` is valid and wraps to Channel 16. Every Note On and its matching Note Off
stay on the same channel. Sources separated by 16 share a physical channel; for
example, sources 1 and 17 both use Channel 1.

**Source Capacity** freezes the admitted dense identity domain for one zone while the
physical MIDI channel count remains sixteen:

| Source Capacity | Admitted source IDs | Sources per channel |
|---:|---|---:|
| 64 | `0..63` | 4 |
| 128 | `0..127` | 8 |
| 256 | `0..255` | 16 |

The factory default is 64. A schema-9-or-earlier project had the historical implicit
256-source domain and migrates to 256 so an old show cannot silently lose identities.
Schema-10 partial state with no saved value uses the new 64-source default.

**Single channel** sends all source notes through the selected **FIXED CHANNEL**.
Each source still owns its lifecycle. If several sources share the same channel and
note, reference counting keeps one owner's deadline or Cancel from prematurely ending
another scheduled ownership.

## 4. Choose one output path

The **DESTINATION** menu offers:

- **Host MIDI Output** - the plugin's MIDI bus inside the DAW;
- **Virtual: Cosmic Microwave <port> Out** - a stable virtual endpoint derived from
  the instance's active UDP port; and
- available system or hardware MIDI outputs.

The **MIDI Output Path** setting in Show Console decides which route is active:

- **Host Only** - send only to the DAW bus;
- **External Only** - send only to the selected virtual/hardware endpoint and clear
  the host output; or
- **Mirror** - deliberately send identical notes to both paths.

Use one path unless two consumers are intentional. If one instrument hears both a
Host and External copy, every attack is duplicated. External Only fails closed when
its endpoint cannot be opened; it does not silently fall back to Host output.

A genuinely fresh instance uses **External Only** and atomically claims the lowest free
complete factory route from `Virtual: Cosmic Microwave 6062 Out` / Zone A through
`Virtual: Cosmic Microwave 6069 Out` / Zone H. Host Only and Mirror remain available
for saved/custom Ableton layouts.

Changing the destination, output path, source-routing mode, fixed channel, UDP port,
Expected Zone, or exclusive ownership is a safety boundary. Cosmic Microwave releases
held state before applying the new routing identity.

### Factory performance presets

In **SHOW CONSOLE > Routing Safety**, the **FACTORY PERFORMANCE PRESET** menu provides eight
complete recalls:

| Preset | UDP | Expected Zone | Virtual endpoint | Conductor |
|---|---:|---|---|---|
| Zone A | 6062 | A | `Cosmic Microwave 6062 Out` | Group 1 Leader |
| Zone B | 6063 | B | `Cosmic Microwave 6063 Out` | Group 1 Follower |
| Zone C | 6064 | C | `Cosmic Microwave 6064 Out` | Group 1 Follower |
| Zone D | 6065 | D | `Cosmic Microwave 6065 Out` | Group 1 Follower |
| Zone E | 6066 | E | `Cosmic Microwave 6066 Out` | Group 1 Follower |
| Zone F | 6067 | F | `Cosmic Microwave 6067 Out` | Group 1 Follower |
| Zone G | 6068 | G | `Cosmic Microwave 6068 Out` | Group 1 Follower |
| Zone H | 6069 | H | `Cosmic Microwave 6069 Out` | Group 1 Follower |

They also restore Notes Only / Per source 1-16, External Only, Flow / Host / 1/32,
Note Duration 16n / Same Note Tie, Manual 16 attacks / 16 active / 100% gate / 16-step spread, Source
Capacity 64, Atomic / Zinc / Core / C2 / four octaves, exclusive UDP ownership, 16/16
Conductor budgets, Safety Governor Off, and Crowd Macros Off. Recall first sends Panic,
then clears ephemeral simulator and live cards before applying the new route. It never
emits MPE or a musical CC.

The same table is the allocation pool only while an instance is genuinely fresh. The
instance retains the lowest free exclusive OSC bind, then opens its matching virtual
endpoint and Conductor role. If A-H is exhausted, it opens none of those three
resources; free a route and press **RETRY AUTO**. There is no wrap or silent sharing.

This menu is an operator recall, not an automatic overwrite of a DAW set. Ableton's
restored plugin state, direct route edits, and explicit presets remain authoritative
and exact. They disable fresh assignment; an occupied saved route fails closed on that
same route instead of shifting. A custom combination appears as **CUSTOM / SAVED
PROJECT STATE**. Partial legacy state uses schema-specific compatibility defaults;
only missing root UDP/destination metadata falls back to the Zone A `6062` virtual
route.

## 5. Ableton layouts

### Channel-separated zone

1. Put one Cosmic Microwave instance on the zone's routing track.
2. Select **Notes Only / Per source 1-16**.
3. Choose **External Only** and `Virtual: Cosmic Microwave <port> Out`, or use Host
   Only when the Live set routes the plugin bus directly.
4. On receiving tracks, select the corresponding source and Channel 1, Channel 2, and
   so on.
5. Enable the required Monitor/arm state and place the sound-producing instruments on
   those tracks.

### One collective receiver

1. Select **Notes Only / Single channel** and the required fixed channel, or let the
   receiver accept all 16 channels from Per source routing.
2. Route exactly one Host or External path to the receiving instrument.
3. Confirm that the receiver responds to velocity as intended.

Because no expression controllers are generated, notes sharing a receiver cannot
change one another's volume through CC11 or timbre through CC74. Velocity can still
produce different attack levels if the receiving instrument is velocity-sensitive.

### Recommended Omnisphere 2 x 8 receiver layout

The production template keeps the sixteen Cosmic MIDI channels explicit while using
two Omnisphere Multi instances:

| Cosmic MIDI channels | Receiver | Multi parts |
|---|---|---|
| 1-8 | OMNI1 | Parts 1-8, one channel per part |
| 9-16 | OMNI2 | Parts 1-8, one channel per part |

At Source Capacity 64, 128, or 256, each part receives respectively 4, 8, or 16 source
identities. Capacity never creates extra MIDI channels or extra Omnisphere instances.
This layout is a routing topology, not a promise that Ableton or Omnisphere will spread
the two instances across CPU cores. Validate the exact patches, tails, buffer size, and
maximum zone count on the show computer with a dropout/CPU soak test.

## 6. Panic and safety CC messages

**PANIC** first releases tracked notes and then sends these two channel-mode safety
messages across the relevant MIDI channels:

- **CC123 = 0** - All Notes Off;
- **CC120 = 0** - All Sound Off.

These are the only generated CC messages in v2.8.0. They are not performance controls;
they exist solely to recover from a lost UDP release, disconnected sender, or routing
change. Panic does not send CC11, CC74, Pressure, Pitch Bend, or RPN resets.

Some receivers ignore one or both channel-mode safety messages. Keep the receiving
instrument's own Panic/Reset control available for show operation.

## 7. Upgrading an Ableton set from an earlier version

Installing v2.8.0 stops new controller generation, but MIDI already recorded in an
Ableton clip belongs to that clip. Existing CC11 or CC74 envelopes can therefore keep
modulating a receiver even though Cosmic Microwave no longer emits them.

Before evaluating the new build:

1. Stop playback and save a backup of the Live set.
2. Inspect every receiving or recorded MIDI clip's Envelopes/MIDI Ctrl lanes.
3. Delete legacy CC11 and CC74 envelopes, or create a new clean clip/track for the
   acceptance test.
4. Remove any MIDI effect or automation lane that independently generates those
   controllers.
5. Reload the receiving instrument or its preset, or use that instrument's own
   Reset/Panic command, to clear any CC11/CC74 value it previously latched.
6. Start a new Cosmic Microwave touch and confirm the MIDI monitor shows only Note On
   and Note Off during normal performance.

A receiver may preserve its last CC11 value after Note Off, after the Live transport
stops, or even after the Cosmic Microwave plugin is replaced. If the sound remains
unexpectedly quiet or filtered, reload/reset the **receiver**. Cosmic Microwave does
not send a one-time CC11/CC74 value during migration because that would violate the
Notes Only contract.

## 8. Acceptance checklist

- [ ] OUTPUT is **Notes Only**.
- [ ] Routing is **Per source 1-16** or the intended fixed channel.
- [ ] Exactly one output path reaches each receiver; the factory path is External Only.
- [ ] Note Duration is the intended `2n..32n` choice and a 120-BPM timing test matches
  its exact deadline.
- [ ] Ensemble Same Note is intentionally Tie or Retrigger; held identical-pitch
  pulses were tested on the production receiver, and Flow/Grid remain Tie.
- [ ] Source Capacity matches the frozen server manifest for this zone.
- [ ] A MIDI monitor shows Note On and the deadline-matched Note Off for a simulator
  touch.
- [ ] The Note-On velocity follows V at the attack.
- [ ] Moving V while held creates no MIDI message.
- [ ] Moving U inside one pitch region creates no MIDI message.
- [ ] Crossing a U pitch boundary starts the new pitch while the old pitch keeps its
  stored tail.
- [ ] Two semantic owners sharing one physical channel+note do not cut one another off.
- [ ] The internal watchdog Cancel and Panic both clean up immediately.
- [ ] The production template maps Channels 1-8 to OMNI1 Parts 1-8 and Channels 9-16
  to OMNI2 Parts 1-8, and passes a show-machine CPU soak.
- [ ] No CC11, CC74, Pressure, Pitch Bend, RPN, or Crowd Macro CC appears in normal
  performance.
- [ ] Panic releases all notes and may show only CC123 and CC120 as safety messages.
- [ ] Old Ableton CC envelopes are removed and the receiving instrument has been
  reloaded/reset before comparison.

For the OSC wire format, source lifecycle, and unchanged Time Field behaviour, continue
with [04 - OSC & the Audience](04-osc-audience.md).
