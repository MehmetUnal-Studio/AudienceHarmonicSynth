# 03 - Normal MIDI and MPE Setup

Cosmic Microwave offers two MIDI output models. Use **Normal MIDI** when source IDs
must feed deterministic Channels 1-16. Use **MPE MIDI** when each active finger needs
its own channel for independent CC, pressure, and pitch-wheel state.

Pitch behaviour depends on **PITCH SYSTEM**. Tonal maps contain standard 12-TET notes,
so their pitch wheel is normally centered. Atomic maps retain element-derived
microtonal targets: MPE encodes each target as the nearest base note plus per-note
pitch bend, while Normal MIDI sends only the nearest semitone.

## 1. Enable MIDI output

In the **MIDI ROUTING** card, **OUTPUT** has three choices:

- **Off** - OSC reception and UI monitoring remain active, but no MIDI is emitted.
- **Normal MIDI** - conventional channel MIDI with source-based or fixed-channel
  routing.
- **MPE MIDI** - one member channel per active finger, up to 15 simultaneous member
  channels.

Cosmic Microwave 2.1 is always silent and MIDI-oriented.

Incoming host MIDI is passed through unchanged whenever output is enabled. It is not
quantized, remapped, or converted into MPE. Avoid routing a keyboard into the plugin if
that pass-through stream would duplicate notes at the receiver.

## 2. Choose a destination

The **MIDI OUTPUT** destination menu contains:

- **Host MIDI Output** - the plugin's MIDI bus into the DAW.
- **Virtual: Cosmic Microwave <port> Out** - a system endpoint whose name is derived
  from the instance's UDP port, such as `Cosmic Microwave 6060 Out`.
- Available system or hardware MIDI devices.

**Rescan** refreshes the list. Selecting a virtual or hardware route does not disable
the host bus; the same stream remains available to the DAW. If a receiver listens to
both paths, it will receive duplicate MIDI.

The port-derived endpoint is stable across plugin creation order and session reopen.
Applying a new UDP port renames/reopens a selected virtual endpoint to match the new
port.

## 3. Normal MIDI source routing

Normal MIDI has two routing modes:

- **Per source 1-16** is the default. The source/participant ID selects a channel:
  source 1 -> Channel 1, source 16 -> Channel 16, source 17 -> Channel 1. Source 0
  is valid and maps to Channel 16.
- **Single channel** sends every OSC source through the selected **FIXED CHANNEL**.

The mapping belongs to source identity, not packet order. Every `u`, `v`, `on`, and
`off` message, across all ten fingers of a source, stays on the same channel. Each
finger remains an independent note owner, and reference counting prevents one finger
from releasing a same-channel/same-note value still owned by another finger.

Normal MIDI expression is channel-wide by definition:

- U/X sends CC74.
- V/Y sends CC11 and supplies note-on velocity.

Sources wrap after 16, so source 1 and source 17 share Channel 1. Their channel
controllers are therefore shared. Choose MPE when expression must be isolated per
active finger.

## 4. MPE zones

MPE divides the channel set into a master channel and member channels:

| Zone | Master | Members | Simultaneous member channels |
|---|---:|---|---:|
| **Lower** (default) | 1 | 2-16 | 15 |
| **Upper** | 16 | 1-15 | 15 |

Match the receiving instrument's zone to Cosmic Microwave. If one side uses Lower and
the other Upper, the receiver may treat member data as ordinary or global channel
data.

Changing zone, output protocol, bend range, fixed channel, source-routing mode, or
destination triggers a safety reset before active fingers are re-established. This
prevents notes held under the previous channel contract from remaining stuck.

## 5. Bend range and Setup

**BEND RANGE** offers `+/-2`, `+/-12`, `+/-24`, and `+/-48` semitones. The default is
`+/-2` semitones. Configure the receiver to the same range.

With **Send MPE setup** enabled, Cosmic Microwave sends:

1. MPE Configuration Message, RPN 6, on the master channel.
2. Pitch-bend-range RPN 0 on every member channel.

The setup is sent when MPE first needs output and again after a relevant configuration
change. Leave it enabled unless the receiver explicitly rejects incoming RPN setup.
Some instruments ignore RPN messages; configure their MPE zone and bend range manually.

For Tonal maps, pitch wheel is normally center (`8192`). Atomic targets can sit between
semitones, so their pitch wheel carries the offset from the nearest MIDI base note.
Atomic offsets stay within half a semitone, but the receiver must still match the
selected bend range or the resulting frequency will be wrong.

## 6. Retrigger and Glide

**PITCH MOTION** controls a held finger when horizontal movement selects another pitch:

- **Retrigger** - release the old mapped note and start the new one.
- **Glide** - update pitch bend without retriggering when the target can remain on the
  same base MIDI note; a target requiring a different base note still retriggers.

Tonal and Atomic maps are both discrete X regions. A move that changes the nearest base
note retriggers in either mode. In Atomic mode, **Glide** can update pitch bend without
retriggering when two target frequencies share the same nearest base note.

## 7. Messages sent for an MPE note

For a new active finger, member-channel messages are ordered as follows:

1. `Pitch Wheel` - sent before the note-on.
2. `CC74` - direct normalized U/X, converted to `0..127`.
3. `CC11` - direct normalized V/Y, converted to `0..127`.
4. `Note On` - mapped base note; velocity comes from V/Y and is limited to `1..127`.
5. `Channel Pressure` - direct normalized V/Y.

While held:

- U/X can change the mapped note and updates CC74.
- V/Y updates CC11 and channel pressure.
- Controller updates are suppressed when their quantized 7-bit value has not changed
  enough to produce a new MIDI value.

On release, the member channel receives `Note Off`, Channel Pressure 0, and a centered
pitch wheel before returning to the available pool. No removed sound-generation
control biases CC74, CC11, velocity, or pressure.

## 8. Member-channel allocation

Each active finger owns one member channel until release. Free member channels are
allocated round-robin, beginning at the first channel in the selected zone. A recently
released channel is visited after the others, reducing immediate channel-state reuse.

When all 15 member channels are occupied, the oldest active MPE note receives a proper
release and its channel is assigned to the new finger. The header's **MPE VOICES**
metric shows the number of occupied member channels.

## 9. Receiver checklist

On the receiving instrument or application:

- [ ] Enable MPE or per-note multi-channel expression mode.
- [ ] Match Lower/Upper zone.
- [ ] Match the pitch-bend range, normally `+/-2` semitones.
- [ ] Allow at least 15 notes if the full member pool is needed.
- [ ] Map CC74 if U/X should affect timbre or another destination parameter.
- [ ] Map CC11 and channel pressure if V/Y should affect expression.
- [ ] Confirm it receives one route, not both the host and virtual copies.

Use the simulator and the receiving application's MIDI monitor to verify that messages
for different fingers arrive on different member channels. The first MPE note after
setup should have its pitch wheel and controllers before `Note On`.

When validating an Atomic map, inspect the pitch wheel as well as Note On. A MIDI
monitor that shows only note names will display the nearest semitone and can make a
correct Atomic MPE stream appear quantized.

## 10. Ableton recipes

### Normal MIDI, one source-channel group per track

1. Set Cosmic Microwave to **Normal MIDI / Per source 1-16**.
2. Select `Virtual: Cosmic Microwave <port> Out`.
3. Create receiving tracks with **MIDI From** set to that endpoint.
4. Select Channel 1 on the first track, Channel 2 on the second, and so on.

This is the recommended layout when different source groups should play different
instruments. Zone A and Zone B use different Cosmic Microwave instances and therefore
have independent channel sets.

### MPE, one complete stream

1. Set Cosmic Microwave to **MPE MIDI**, select Lower or Upper, and leave Setup on.
2. Route the complete multi-channel stream to one MPE-capable receiver.
3. Match the receiver's zone and bend range.

Do not interpret MPE member channels as the stable source-to-channel map: member
channels are allocated dynamically per active finger. Some DAW routing paths can
remap or merge MIDI channels; if expression appears global, test the port-named virtual
endpoint with a standalone MPE receiver or a host with explicit MPE routing.

## 11. Panic and safe changes

Use **PANIC** after a lost OSC `off`, receiver disconnect, or unexpected routing loop.
It clears live/simulator source state and sends note-off/all-off safety messages to the
host and selected external destination.

Changing the UDP port also performs a safety release before restarting the listener.
After changing a port, confirm both the new OSC input and the newly named virtual MIDI
endpoint at the receiver.
