# 06 - Troubleshooting & FAQ

Start with the visible state in Cosmic Microwave: OSC status, observed zone letters,
source/touch/note counters, the activity map, MIDI mode, and destination status. Then
check the receiving application's MIDI monitor and track configuration.

If Ableton lists both the old and new product names, remove one bundle from the scanned
VST3 folder. `SpektraSynth.vst3` and `Cosmic Microwave.vst3` intentionally share a
class identity for old-session recall and must not be installed side by side.

## Cosmic Microwave itself makes no sound

That is expected in version 2.5.0. Cosmic Microwave is an OSC-to-MIDI router with a
silent stereo instrument shell. It must feed a sound-producing instrument or hardware
receiver.

For a first test:

1. Select **Normal MIDI / Per source 1-16**.
2. Select one explicit path: Host Only, or External Only plus
   `Virtual: Cosmic Microwave <port> Out`.
3. Make a receiver listen to all channels from that route.
4. Enable the receiver track's required monitoring/arming.
5. Click **+ Source** in Cosmic Microwave.

If the Cosmic Microwave counters move but nothing is heard, troubleshoot the receiver
and MIDI route rather than looking for a sound control in the plugin.

## The NOTES counter does not move

Check these items in order:

1. **OUTPUT** must be **Normal MIDI** or **MPE MIDI**, not **Off**.
2. A source must receive `on 1`; `u` and `v` alone only update stored control state.
3. The source/touch counters should rise. If they stay at zero, use **+ Source** to
   separate an OSC-input problem from a MIDI-output problem.
4. The VST3 track must be active so the host processes the plugin. Inactive, frozen,
   or disabled tracks may not run its MIDI generation path.
5. Click **PANIC**, then create one new simulator source and test again.

Incoming host MIDI is passed through only when output is enabled. It does not create a
source cell because it is MIDI thru, not an OSC audience source.

## A touch is delayed, gated, or repeats

Check **TIME FIELD -> MODE**:

- **Flow** renders the OSC lifecycle directly.
- **Grid** starts attacks on the selected division. A held note stays active until its
  ordered Off; an admitted short tap receives the configured minimum gate.
- **Ensemble** assigns the source to a spread lane, applies a fixed gate, and queues a
  still-held source touch for another pulse.

New 2.5.0 sessions intentionally default to Ensemble with Adaptive policy. Use Flow
when diagnosing raw sender timing. Projects saved before state schema 4 migrate to
Flow, so opening an old set does not silently quantize it.

If too few notes begin in Adaptive, inspect its effective `AUTO` values and recent
source count. Switch to Manual before lowering spread or raising **ATTACKS / STEP** and
**ACTIVE LIMIT**. In MPE, the effective active limit is always at most 15 even if the
saved control reads 16.

## Adaptive changes after people stop touching

This is intentional. Adaptive uses the larger of currently held sources and unique
live OSC sources seen during the previous eight seconds. Its density rises quickly but
falls slowly, with transition holds and hysteresis, so a brief collective pause does
not immediately collapse the musical texture. The displayed profile can therefore
remain above the current held count for a while.

Adaptive changes future admission in Grid and Ensemble only. It never releases an
already sounding voice, replaces an ordered Off, suppresses the three-second watchdog,
or blocks Panic. Flow bypasses the Governor. Select Manual when fixed attacks, active
limit, and spread are required; the plugin preserves those Manual values while
Adaptive is active.

Projects saved by schema 6 or earlier intentionally open in Manual so an update cannot
change an established performance. Only a new 2.5.0 instance defaults to Adaptive.

## The Time Field says WAIT instead of LOCK

With **Host** clock selected, LOCK requires a valid host tempo and PPQ position while
the transport is playing. A stopped transport or unavailable clock displays WAIT.
Cosmic Microwave still schedules safely from its process-wide monotonic fallback at
the Internal BPM; it simply is not locked to host PPQ.

Start the host transport for PPQ lock, or select **Internal** to use the common
monotonic clock explicitly. Separate instances in the same process use that shared
absolute time reference rather than unrelated free-running counters.

## MIDI is generated but does not reach the receiver

### Destination

Read the **MIDI OUTPUT** status lines:

- **Host MIDI Output** - route from the Cosmic Microwave device/track inside the DAW.
- **Virtual: Cosmic Microwave <port> Out** - subscribe to the endpoint matching this
  instance's current UDP port, for example `Cosmic Microwave 6060 Out`.
- A hardware/system name - click **Rescan** and reselect it if the device changed.

Check **MIDI Output Path** in Show Console. **Host Only** ignores the external endpoint;
**External Only** fails closed with an empty host bus when the endpoint is missing;
**Mirror** sends to both. A missing endpoint in External Only is a Venue Preflight
failure. Duplicate notes usually mean Mirror is selected and a receiver consumes both
copies; choose one path or disconnect one input.

### Ableton receiving track

- Select the correct endpoint under **MIDI From**.
- For an initial test, receive all channels.
- Set Monitor/arm state so Live forwards incoming MIDI to the instrument.
- Make sure the receiving track is not taking MIDI from a different Cosmic Microwave
  port.
- After changing the UDP port, reconnect to the newly named virtual endpoint.

## Notes arrive on the wrong Normal MIDI channel

With **Per source 1-16**, source ID defines the channel:

```text
1 -> Ch 1   16 -> Ch 16   17 -> Ch 1   0 -> Ch 16
```

The activity map groups all 256 IDs by this rule. Find the active source cell and read
its column heading. The mapping is one-based for normal participant IDs; source `0` is
valid and wraps to Channel 16.

If every source uses one channel, **Single channel** is selected. Change to
**Per source 1-16**, or change **FIXED CHANNEL** if single-channel routing is intended.

MPE member channels are dynamic and do not follow the Normal MIDI source formula.
Switch to Normal MIDI when receiving tracks depend on stable source-channel groups.

## OSC is not arriving

1. Confirm the sender uses the instance's current UDP port.
2. Confirm the OSC card says **Listening**, not an error.
3. Confirm Expected Zone is Any or matches the address letter, and that the zone-
   mismatch counter is zero.
4. Send a canonical message:

   ```text
   /cs/A/1/finger0/u   0.5
   /cs/A/1/finger0/v   0.8
   /cs/A/1/finger0/on  1
   ```

5. The source range is `0..255`; only exact lower-case `finger0` is admitted.
6. The literal `finger` token must be lower-case. Unknown parameters are ignored.
7. OSC values must be finite `int32` or `float32` values.
8. Allow inbound UDP for Ableton or the Standalone app in the system firewall.

Use **+ Source** as a control test. If the simulator produces MIDI, the failure is
before the plugin's source model: sender address, network, firewall, or port.

## The OSC card reports more than one zone

The plugin never infers a zone from the port. With Expected Zone at **Any**, multiple
observed letters mean multiple streams reached the same UDP input. Set the assigned
zone explicitly; wrong-zone packets are then rejected and counted as mismatches.

Correct the upstream server so one already-separated zone feeds one port and one
Cosmic Microwave instance. Zone A on `6060` and Zone B on `6061` are conventions in
the server/deployment, not automatic plugin assignments.

If an old schema-7 session still uses Any, source ID owns state independently of the
zone letter and equal IDs from two zones can share touch state. Fix the split and lock
Expected Zone; do not rely on display telemetry alone.

## The OSC port is unavailable

- Another application may already own the UDP port. Assign a different port upstream
  and in Cosmic Microwave.
- New sessions use exclusive ownership. **OWNERSHIP CONFLICT** means another in-process
  instance already owns that port. Close/change the conflicting instance; the receiver
  retries at a bounded interval.
- Schema-7-and-earlier sessions preserve shared-port behaviour for compatibility, but
  production design remains one unique port per zone instance.
- A shared receiver supports a bounded number of clients. A **PORT FULL** status means
  this instance did not receive a client slot; use another port.
- Invalid port text does not replace the active listener. Enter `1..65535` and click
  **Apply**.

Applying a different port or Expected Zone performs a safety release, restarts/updates
OSC listening, clears
the observed-zone history, and updates a selected virtual endpoint's port-based name.

## A note is stuck

Click **PANIC**. It clears live/simulator source state and sends note-off plus all-off
safety messages to the host and selected external destination.

Then check the sender:

- Every successful `on 1` lifecycle needs `off` or `on 0` for the same source and
  touch.
- Do not send the release under a different source ID or any token other than `finger0`.
- Send U/V before On, but do not substitute movement messages for Off.
- UDP has no delivery guarantee. Keep Panic available for sender/network failure.

Cosmic Microwave keys live note ownership by source and its `finger0` touch, so packet-by-packet channel
round-robin is not used. In Normal MIDI, same-channel/same-note owners are reference
counted so one wrapped source cannot prematurely release another held owner.

Changing MIDI protocol, Normal routing mode/channel, MPE zone/range, destination,
MIDI Output Path, Expected Zone, UDP ownership, or UDP port triggers safety reset
handling. Changing the active Time Field domain also
releases and rehydrates canonical held touches under the new schedule. If a receiver
ignores All Notes Off, use its own panic control as well.

## Expression affects more than one note

### Normal MIDI

CC74 and CC11 are channel messages. In **Single channel**, every source shares them.
In **Per source 1-16**, each source owns one channel, but IDs wrap after 16; source 1
and source 17 both use Channel 1.

This is expected MIDI behaviour. Use MPE when expression must be independent for each
active source touch.

### MPE

Each active source touch gets one member channel, so CC74, CC11, and channel pressure are
isolated when the receiver handles MPE correctly. If expression still feels global:

- enable MPE/per-note expression in the receiver;
- match Lower or Upper zone;
- route the full channel set without remapping it to one channel; and
- confirm the receiver is not listening to a duplicate host route.

U/X maps directly to CC74. V/Y maps directly to CC11 and MPE channel pressure. No
removed sound-generation parameter is blended into those values.

## MPE notes are released when the crowd grows

Lower and Upper zones each provide 15 member channels. When a sixteenth active source touch
needs a member channel, Cosmic Microwave releases the oldest active MPE note and reuses
its channel. This is expected voice stealing in the MIDI allocator.

The header's **MPE VOICES** metric shows occupied member channels. Use Normal MIDI if
more than 15 simultaneous source touches are required and per-touch channel isolation is not.

## Pitch or MPE tuning is unexpected

First check **PITCH SYSTEM**:

- **Tonal** contains standard 12-TET notes, so its MPE pitch wheel is normally
  centered.
- **Atomic** contains element-derived targets. Normal MIDI rounds them to the nearest
  semitone; MPE sends the nearest base note plus a per-note pitch-wheel offset.

If Atomic MPE sounds like only ordinary semitones, confirm that the receiver accepts
per-note pitch bend and that the complete MPE channel set reaches it. If the result is
out of tune:

- check whether it has its own `.tun`, Scala, transpose, or pitch map enabled;
- match the receiver's pitch-bend range to Cosmic Microwave's **BEND RANGE**;
- confirm the receiver treats the selected channels as the same MPE zone; and
- check for another pitch-wheel source on the route.

Normal MIDI sends no per-note pitch-wheel data for OSC touches. A MIDI monitor that
shows only Note On names will therefore show Atomic's nearest semitone even when an MPE
stream is correct; inspect Pitch Wheel as well. External receiver tuning is explained
in [05 - Pitch Systems & External Tuning](05-tuning-files.md).

## Heavy OSC traffic causes resets or missed movement

The input path is bounded to protect the realtime MIDI path. In every mode, duplicate
U/V bursts collapse to the latest position while a separate priority queue preserves
ordered On/Off. Flow consumes the latest movement marker directly; Grid and Ensemble
sample the canonical position on attacks and grid boundaries. An extreme lifecycle
burst can still fill its fixed-capacity queue; recovery requests a reset and rehydrates
canonical held state so a dropped release cannot leave notes held indefinitely.

- Throttle continuous U/V updates to a musically useful rate.
- Do not resend unchanged values unnecessarily.
- Split zones before they reach the plugin.
- Turn off simulator **Random movement** when it is not needed.
- Avoid routing the plugin's output back into its own MIDI input.

Lifecycle messages are more important than redundant movement messages. Design the
upstream sender so On/Off are delivered promptly and keep Panic available.

Open **SHOW CONSOLE** and inspect the Safety Governor before changing musical policy.
HIGH, CRITICAL, or EMERGENCY identifies the active pressure reason: ingress, lifecycle
queue, motion drops, Time Field pending, external FIFO depth/age, DSP deadline, invalid
input/clock, or a recovery hold. Escalation is immediate, but recovery is intentionally
staged; a clean signal must remain below the hysteresis boundary. EMERGENCY closes new
attacks while Off/watchdog/Panic continue. If the external FIFO is the cause, use Host
Only for isolation or repair the endpoint rather than disabling safety.

The **MERGED** figure is cumulative load telemetry. It means scheduled work was
coalesced or expired after missing available capacity. It does not send a Crowd Energy
CC, change velocity, or alter another MIDI controller. A pending Ensemble short tap is
kept for at least one full lane-cycle opportunity before expiry.

## FAQ

### Why is Cosmic Microwave still listed as an instrument?

The silent stereo instrument shell preserves Ableton placement and the existing VST3
class contract. Its runtime behaviour is MIDI-only. If a host specifically requires a
MIDI-effect category, the repository also builds **Cosmic Microwave MIDI** and
**Cosmic Microwave MIDI Generator**.

### Can I use the Standalone app?

Yes. Select its port-named virtual output or a hardware MIDI destination, then subscribe
to that route in an external receiver. The Standalone app does not make sound itself.

### Does the plugin infer a zone from 6060 or 6061?

No. It reads the zone segment from valid `/cs/...` messages. Port-to-zone assignment is
owned by the upstream server and show configuration. Expected Zone is an explicit
validation filter; it still does not infer the letter from the port.

### Global Conductor says Local Fallback

Confirm every participating instance is in the same group, has a unique valid UDP
port, and is set to Leader or Follower. At least one live Leader is required; the lowest
leader port wins deterministically. A missing allocation or heartbeat older than 1.5
seconds safely returns each instance to its local Time Field quotas. Groups coordinate
only instances in the same plugin process, not another computer or host process.

### Crowd Expression CCs stopped

Check that macros are enabled, the receiver listens to the selected channel (or all
channels for Broadcast), and the four CC numbers are mapped as intended. CRITICAL and
EMERGENCY Safety states suspend macro emission. Telemetry continues, and a full CC
snapshot is sent when emission safely resumes. Change-only output also means a static
crowd does not resend identical values every tick.

### Is Capture/Replay running inside the plugin?

No. `tools/cosmic-chaos-lab.mjs` is an external Node.js CLI. Run it from Terminal as a
proxy, recorder, replayer, or load generator. The plugin never opens capture files or
replay sockets on its audio thread. See [Capture/Replay Chaos Lab](../chaos-lab.md).

### Why does source 0 use Channel 16?

The musical convention is one-based: 1 -> 1 through 16 -> 16. Source 0 remains valid
for compatibility and wraps one position backward to Channel 16.

### Can I import a tuning file?

Not into the flagship plugin. Apply `.tun`, `.scl`, or other tuning systems in the
receiving instrument. Cosmic Microwave continues to own MIDI note numbers and matching
note-off messages.

### Why did a new session open on Helium / Extended?

Cosmic Microwave 2.5.0 intentionally defaults new sessions to **Atomic / Helium /
Extended**. Choose **Tonal** for the seven conventional 12-TET maps. The historical
schema-2 migration still selects Tonal, while released 1.x element/spectral choices
migrate to Atomic and recover their corresponding element. Separately, schema-3 and
older state receives Flow for the Time Field. Schema-5 input remains compatible and
its retired experimental fields are discarded. Schema-6-or-earlier state receives
Manual Governor mode without altering its saved Time Field controls; new state uses
schema 8. Schema-7-and-earlier state also preserves Mirror output, shared UDP ownership,
Expected Zone Any, Safety Governor Off, and Crowd Expression Off. New sessions instead
start Host Only with exclusive ownership and Safety enabled; Global Conductor and
Crowd Expression remain opt-in.

### What happened to the previous sound-generation controls?

They were removed from the 2.0 flagship and remain absent in 2.5.0. Old repositories may
retain archival media or implementation files, but the current flagship target neither
compiles nor loads them.

### What should I include in a bug report?

Include the Cosmic Microwave version, host/OS version, UDP port, Expected Zone and
mismatch count, ownership status, exact OSC address/argument, MIDI Output Path and
endpoint, Safety state/reasons/telemetry, Time Field/Conductor quota, observed zones,
receiver channel/MPE configuration, and a short MIDI-monitor or Chaos Lab capture when
possible.
