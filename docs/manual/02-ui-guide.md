# 02 - UI Guide

Cosmic Microwave is an OSC-to-MIDI router. The editor is focused on five jobs:
selecting the UDP input, confirming source identity, mapping horizontal movement to
notes, choosing Normal MIDI or MPE, and selecting a MIDI destination.

The product does not generate sound. It remains a silent stereo instrument shell so
Ableton can keep the same device placement and session identity, but its behaviour and
all visible controls are MIDI-only.

## Layout

```
+--------------------------------------------------------------------------+
| COSMIC MICROWAVE   MIDI ONLY   SOURCES | FINGERS | NOTES | MPE VOICES    |
+----------------------+------------------------+--------------------------+
| OSC INPUT            | SOURCE ROUTING         | SIMULATOR                |
+----------------------+------------------------+--------------------------+
|                                                       | PITCH MAPPING    |
| SOURCE MATRIX                                        +------------------+
| 256 source IDs grouped into 16 MIDI-channel columns  | MIDI ROUTING     |
|                                                       +------------------+
|                                                       | MIDI OUTPUT      |
+-------------------------------------------------------+------------------+
```

The editor opens at `1120 x 640`, is resizable, and keeps the activity map large while
the routing controls stay together in the right column.

## Header

The header identifies the device as **COSMIC MICROWAVE**, labels its role as
**OSC / MIDI ROUTING INSTRUMENT**, and shows a **MIDI ONLY** badge. Four live metrics
appear on the right:

- **SOURCES** - OSC or simulator source IDs with at least one active finger.
- **FINGERS** - the total number of active fingers across those sources.
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
- A red error - the port could not be used or the shared receiver has no free client
  slot.

The address reminder beneath the status is:

```text
/cs/{zone}/{source}/finger{n}/{on|off|u|v}
```

Each instance listens to one UDP port. The upstream server should therefore send one
already-separated zone to each instance. The zone is still read from each OSC address;
it is not inferred from the port number.

## SOURCE ROUTING

This card is a read-only summary of the current MIDI mode. It does not assign channels
packet by packet.

In **Normal MIDI / Per source 1-16**, the card shows the stable mapping:

```text
1 -> Ch 1   2 -> Ch 2   ...   16 -> Ch 16   17 -> Ch 1
```

Source `0` is accepted and wraps to Channel 16. Every finger and every `on`, `off`,
`u`, and `v` message belonging to a source uses that source's channel.

The summary changes when **Single channel**, **MPE MIDI**, or **Off** is selected. Its
bottom line reports the zone letters observed in valid OSC addresses and the current
source/finger totals. If more than one zone appears, check the upstream port split;
the plugin observes zone data but does not filter traffic by zone.

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
position follows the source's most recent X/Y values. The point grows slightly when
more fingers are active. The map is a monitor; selecting a cell does not change
routing.

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

Changing any pitch-map control safely releases and retriggers held OSC fingers at
their new mapped notes.

## MIDI ROUTING

### OUTPUT

- **Off** - keep receiving and displaying OSC, but emit no MIDI.
- **Normal MIDI** - send conventional channel MIDI.
- **MPE MIDI** - allocate a member channel per active finger and send per-note
  expression.

Incoming MIDI from the host is passed through while output is enabled.

### Normal MIDI controls

- **Per source 1-16** - source identity selects Channels 1-16 with the stable wrap
  shown above. This is the intended audience-routing mode.
- **Single channel** - send every source through the selected **FIXED CHANNEL**.

Each finger is still an independent note owner. If several fingers land on the same
channel and note, reference counting keeps that note held until the last owner releases
it.

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
route and whether it opened successfully. Selecting a virtual or hardware destination
does not disable the host MIDI bus; the same stream remains available to the host.

For Ableton channel separation, the virtual endpoint is recommended. Receiving tracks
can select `Cosmic Microwave 6060 Out` and then choose Channel 1, Channel 2, and so on.
The endpoint name follows the instance's UDP port, so the Zone A and Zone B instances
remain easy to identify after reopening a session.

### PANIC

**PANIC** is the global safety control. It clears the active source state and sends
note-off, channel-pressure reset, centered pitch bend, All Notes Off, and All Sound
Off messages as appropriate across the host and selected external route. Use it after
a sender disconnect, a routing change, or any suspected missing `off` packet.

## Recommended one-zone workflow

1. Place one Cosmic Microwave instance for each already-separated zone.
2. Set the instance's UDP port, for example `6060` for Zone A and `6061` for Zone B.
3. Choose **Normal MIDI -> Per source 1-16** for channel-separated routing, or choose
   **MPE MIDI** for per-note expression.
4. Select the port-named virtual destination for the clearest Ableton routing.
5. Confirm the observed zone, source count, activity map, destination status, and then
   test **PANIC** before the audience connects.

See [04 - OSC & the Audience](04-osc-audience.md) for the wire format and
[03 - MPE Setup](03-mpe-setup.md) for receiver configuration.
