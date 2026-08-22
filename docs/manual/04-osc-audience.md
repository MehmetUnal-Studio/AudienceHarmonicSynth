# 04 - OSC & the Audience

Cosmic Microwave receives normalized audience controls over OSC/UDP and turns them
into Normal MIDI or MPE. The production server has already separated the zones, so
each plugin instance listens to one dedicated UDP port.

Example layout:

| Audience zone | UDP port | Ableton device | Recommended virtual MIDI endpoint |
|---|---:|---|---|
| A | `6060` | Cosmic Microwave 1 | `Cosmic Microwave 6060 Out` |
| B | `6061` | Cosmic Microwave 2 | `Cosmic Microwave 6061 Out` |

Additional zones follow the same pattern with their own ports and instances. Cosmic
Microwave does not split or forward zones itself.

The plugin is behaviourally MIDI-only. It presents a silent stereo instrument shell
to Ableton for host compatibility, but it does not create sound.

## Port and zone are different fields

The UDP port selects which socket an instance listens to. The zone letter remains part
of every OSC address:

```text
UDP 6060  <-  /cs/A/...
UDP 6061  <-  /cs/B/...
```

`6060 -> A` and `6061 -> B` are deployment conventions, not rules inside the plugin.
Cosmic Microwave reads and displays the zone letters it receives; it never infers a
zone from the port. It also does not filter a packet because its zone letter differs
from the expected deployment convention.

This means the upstream split must be correct. If A and B traffic are both sent to one
port, the instance reports both observed zones and processes both streams. Source
identity inside an instance is based on source ID, so the same source ID arriving from
two zones would share state. Use one already-separated zone, one port, and one instance.

## Source and finger identity

An OSC control is identified by:

- **Zone:** `A` through `Z`.
- **Source/participant ID:** `0` through `255`.
- **Finger:** `finger0` through `finger9`.

Every finger is an independent note lifecycle. Releasing `finger0` does not release
`finger1`. In Normal MIDI's default **Per source 1-16** mode, all ten fingers of one
source use the same MIDI channel.

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
`u`, `v`, `on`, `off`, and all finger messages stay on that channel. This keeps a
finger's note-on and note-off together even when many sources are active.

MPE works differently: each active finger receives an available MPE member channel.
The source ID still owns the lifecycle, but the selected MPE zone defines the channel
pool.

## OSC wire format

Canonical addresses use:

```text
/cs/<zone>/<source>/finger<n>/<param>
```

| Segment | Meaning |
|---|---|
| `/cs/` | Cosmic Symphony/control-surface prefix. |
| `<zone>` | Zone letter `A` through `Z`. |
| `<source>` | Decimal source ID `0` through `255`. |
| `finger<n>` | Lower-case `finger` followed by one digit `0` through `9`. |
| `<param>` | `u`, `v`, `on`, `off`, or legacy `line`. |

The `/cs/` prefix, zone letter, and parameter name are accepted without regard to
letter case. The `finger` token itself is deliberately lower-case and strict.

### Parameters

| Parameter | Argument | MIDI meaning |
|---|---|---|
| `u` | Numeric, normally `0..1` | Horizontal position. Selects a pitch from the configured Tonal or Atomic map and directly sets CC74. |
| `v` | Numeric, normally `0..1` | Vertical position. Sets note-on velocity and directly sets CC11; MPE also sends channel pressure. |
| `on` | Numeric | Non-zero activates the source/finger; zero releases it. |
| `off` | No argument required | Releases the source/finger. A finite numeric argument is accepted and ignored. |
| `line` | Numeric, legacy `0..127` | Divided by 127 and handled as horizontal position. Prefer `u` for new senders. |

OSC `int32` and `float32` arguments are accepted. Non-numeric and non-finite values
are ignored. `u` and `v` are clamped to `0..1`; `line` is divided by 127 and then
clamped.

The direct controller conversion is:

```text
CC74 = round(clamp(u, 0, 1) * 127)
CC11 = round(clamp(v, 0, 1) * 127)
velocity = round(clamp(v, 0, 1) * 127), limited to 1..127
```

There is no macro or sound-engine bias in these mappings. X/U directly controls the
pitch-map position and CC74; Y/V directly controls velocity, CC11, and MPE pressure.

### Message examples

```text
/cs/A/1/finger0/u     0.50   source 1, finger 0: horizontal midpoint
/cs/A/1/finger0/v     0.80   source 1, finger 0: velocity/expression 0.8
/cs/A/1/finger0/on    1      activate on Normal MIDI Channel 1
/cs/A/1/finger1/on    1      independent finger, still Normal MIDI Channel 1
/cs/A/1/finger0/off          release finger 0 only
/cs/A/17/finger3/on   1      source 17 wraps to Normal MIDI Channel 1
/cs/B/16/finger9/on   1      source 16 maps to Normal MIDI Channel 16
```

Send `u` and `v` before `on` when starting a new finger so the first note uses the
intended pitch and velocity. Later `u`/`v` messages update a held finger. A pitch-map
step change retriggers the note by default; MPE can glide while the target remains
within the same base MIDI note.

Addresses outside the contract are ignored: wrong prefix, missing segments, source
`256` or higher, `finger10`, a non-lower-case `finger` token, or an unknown parameter.
OSC bundles are supported.

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
Normal MIDI rounds an Atomic target to its nearest semitone. MPE preserves the exact
element-derived target frequency by sending a per-note pitch wheel before Note On.

New sessions default to Atomic / Helium / Extended. Existing schema-2 MIDI-only
sessions restore as Tonal, so an older set does not silently change its pitch map.

## UDP configuration and status

1. Enter the assigned port in **OSC INPUT**.
2. Click **Apply** or press Return.
3. Confirm **Listening ... waiting for data**.
4. Send a valid OSC message and confirm **Receiving** plus the observed zone letter.

The default port is `6060`. Valid ports are `1..65535`. Invalid text leaves the active
listener unchanged. Applying a different port releases held notes before the listener
restarts.

The status card counts only valid messages. It retains the observed zone set while the
listener remains on that port. Multiple letters indicate mixed upstream traffic, not
an automatic multi-zone mode.

The virtual output name is derived from the active UDP port:

```text
Cosmic Microwave <port> Out
```

For example, UDP `6060` uses `Cosmic Microwave 6060 Out`. If a virtual destination is
selected and the port changes, the plugin reopens the endpoint with the new port-based
name.

## Ableton routing

Use this layout for each zone:

1. Place one Cosmic Microwave instance on its own Ableton track.
2. Set the instance's UDP port to the already-separated stream for that zone.
3. Select **Normal MIDI** and **Per source 1-16**.
4. Under **DESTINATION**, select **Virtual: Cosmic Microwave <port> Out**.
5. On receiving Ableton MIDI tracks, choose that virtual endpoint under **MIDI From**
   and select Channel 1, Channel 2, and so on.
6. Set the receiving tracks' monitoring/arming as your Live set requires and place the
   destination instruments there.

The port-named virtual endpoint is recommended because it makes zone ownership and
channel selection explicit. The plugin's host MIDI output remains available at the
same time, but the virtual endpoint is usually clearer when one instance must feed up
to 16 channel-specific Ableton tracks.

Zone A and Zone B each get an independent set of Channels 1-16 because they use
different Cosmic Microwave instances and virtual endpoints.

For MPE, select **MPE MIDI**, match the receiving instrument's Lower/Upper zone and
bend range, and route the full MPE channel set together instead of splitting it into
16 independent tracks.

## Simulator

The **SIMULATOR** card sends controls through the same source/finger-to-MIDI path:

- **+ Source** adds one simulated source.
- **+ 25** adds 25.
- **Remove** releases one simulated source.
- **Random movement** updates horizontal and vertical values continuously.
- **Clear** releases all simulated sources.

Use it to confirm pitch mapping, source-to-channel assignment, destination selection,
and receiving-track monitoring before the network sender connects. Simulator activity
does not invent a UDP zone; the observed-zone status reflects valid OSC traffic only.

## Network and lifecycle checklist

- The sender must be able to reach the Cosmic Microwave machine's LAN address and the
  assigned UDP port.
- Allow inbound UDP for the standalone application or Ableton in the system firewall.
- UDP has no acknowledgements or retransmission. Send explicit `off` messages and keep
  **PANIC** available for a hard release.
- Send `u`/`v` only as fast as the performance requires; avoid needless duplicate
  traffic.
- Confirm one zone, one port, and one instance together before the audience connects.
- If an `off` packet is lost or the sender disappears, click **PANIC**. It clears live
  source state and sends note-off/all-off safety messages to the host and selected
  external destination.
