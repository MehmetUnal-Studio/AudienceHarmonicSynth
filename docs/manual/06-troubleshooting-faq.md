# 06 - Troubleshooting & FAQ

Start with the visible state in Cosmic Microwave: OSC status, observed zone letters,
source/touch/note counters, the activity map, MIDI mode, and destination status. Then
check the receiving application's MIDI monitor and track configuration.

If Ableton lists both the old and new product names, remove one bundle from the scanned
VST3 folder. `SpektraSynth.vst3` and `Cosmic Microwave.vst3` intentionally share a
class identity for old-session recall and must not be installed side by side.

## Cosmic Microwave itself makes no sound

That is expected in version 2.8.0. Cosmic Microwave is an OSC-to-MIDI router with a
silent stereo instrument shell. It must feed a sound-producing instrument or hardware
receiver.

For a first test:

1. Select **Notes Only / Per source 1-16**.
2. Select one explicit path: Host Only, or External Only plus
   `Virtual: Cosmic Microwave <port> Out`.
3. Make a receiver listen to all channels from that route.
4. Enable the receiver track's required monitoring/arming.
5. Click **+1 Held** in Cosmic Microwave.

If the Cosmic Microwave counters move but nothing is heard, troubleshoot the receiver
and MIDI route rather than looking for a sound control in the plugin.

## The NOTES counter does not move

Check these items in order:

1. **OUTPUT** must be **Notes Only**, not **Off**.
2. A source must receive `on 1`; `u` and `v` alone only update stored control state.
3. The source/touch counters should rise. If they stay at zero, use **+1 Held** to
   separate an OSC-input problem from a MIDI-output problem.
4. The VST3 track must be active so the host processes the plugin. Inactive, frozen,
   or disabled tracks may not run its MIDI generation path.
5. Click **PANIC**, then create one new simulator source and test again.

Incoming host Note On/Off is passed through only when output is enabled. Other incoming
MIDI messages are filtered. Host notes do not create source cells because they are MIDI
thru, not OSC audience sources.

## A touch is delayed, gated, or repeats

Check **TIME FIELD -> MODE**:

- **Flow** renders the OSC lifecycle directly.
- **Grid** starts attacks on the selected division. Ordered Off ends the semantic
  scheduled voice; an admitted short tap receives the configured minimum gate. An
  already-started MIDI tail keeps its fixed deadline.
- **Ensemble** assigns the source to a spread lane, applies a fixed semantic gate, and
  queues a still-held source touch for another pulse. MIDI tails may overlap pulses.
  **Same Note = Tie** preserves identical-pitch ownership; **Retrigger** safely emits
  Note Off then Note On for every admitted identical-pitch pulse.

New 2.8.0 sessions intentionally default to Flow with Manual policy and Same Note Tie. Grid, Ensemble,
and Adaptive are explicit performance choices. Projects saved before state schema 4
also migrate to Flow, so opening an old set does not silently quantize it.

If too few notes begin in Adaptive, inspect its effective `AUTO` values and recent
source count. Switch to Manual before lowering spread or raising **ATTACKS / STEP** and
**ACTIVE LIMIT**.

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
change an established performance. New 2.8.0 instances also default to Manual;
Adaptive is enabled only when the operator selects it.

## Grid plays the first group and then becomes silent

Install version 2.7.1 or later. Earlier Grid builds could leave the Time Field active
counter occupied after the matching fixed-duration MIDI notes had already ended. Once
the active limit was full, pending sources could no longer enter even though the header
showed no active MIDI notes. Version 2.7.1 reconciles each final renderer deadline with
its Grid active slot and keeps the pending queue drainable under Safety Governor load.

In the live display, **ACTIVE NOTES** is the renderer's scheduled MIDI-tail count and
**TF ACTIVE** is Time Field admission ownership. In Grid they should both return to zero
after all admitted one-shot tails have ended. If sound is still absent while both are
zero, inspect the destination route and downstream instrument rather than raising the
Grid active limit.

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
  instance's current UDP port, for example `Cosmic Microwave 6062 Out`.
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

## Notes arrive on the wrong Notes Only MIDI channel

With **Per source 1-16**, source ID defines the channel:

```text
1 -> Ch 1   16 -> Ch 16   17 -> Ch 1   0 -> Ch 16
```

The activity map groups all 256 IDs by this rule. Find the active source cell and read
its column heading. The mapping is one-based for normal participant IDs; source `0` is
valid and wraps to Channel 16.

If every source uses one channel, **Single channel** is selected. Change to
**Per source 1-16**, or change **FIXED CHANNEL** if single-channel routing is intended.

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

Use **+1 Held** as a control test. If the simulator produces MIDI, the failure is
before the plugin's source model: sender address, network, firewall, or port.

## The OSC card reports more than one zone

The plugin never infers a zone from the port. With Expected Zone at **Any**, multiple
observed letters mean multiple streams reached the same UDP input. Set the assigned
zone explicitly; wrong-zone packets are then rejected and counted as mismatches.

Correct the upstream server so one already-separated zone feeds one port and one
Cosmic Microwave instance. The factory performance presets use Zone A on `6062` and
Zone B on `6063`. Fresh auto-assignment chooses a complete pair from that table; it
still does not infer a zone from received packets.

If an old schema-7 session still uses Any, source ID owns state independently of the
zone letter and equal IDs from two zones can share touch state. Fix the split and lock
Expected Zone; do not rely on display telemetry alone.

## The OSC port is unavailable

- Another application may already own the UDP port. A genuinely fresh instance skips
  an occupied A-H route and claims the next complete free route.
- New sessions use exclusive ownership. **OWNERSHIP CONFLICT** means another in-process
  instance owns an exact saved/manual route. Close/change the conflicting instance;
  that route may recover at a bounded interval without changing its saved identity.
- If all automatic routes `6062/A..6069/H` are occupied, the fresh instance deliberately
  opens no OSC receiver, virtual MIDI endpoint, or Global Conductor registration. Free
  a route and press **RETRY AUTO**; it does not continuously rescan or wrap.
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

Click **PANIC**. It clears live/simulator source state, sends matching Note Off, then
sends CC123 (All Notes Off) and CC120 (All Sound Off) to the host and selected external
destination. CC120/123 are the only generated CC messages in v2.7.1 and are reserved for
stuck-note safety.

Then check the sender:

- Every successful `on 1` lifecycle needs `off` or `on 0` for the same source and
  touch.
- Do not send the release under a different source ID or any token other than `finger0`.
- Send U/V before On, but do not substitute movement messages for Off.
- UDP has no delivery guarantee. Keep Panic available for sender/network failure.

An ordinary `/on 0` ends the semantic touch but does not cut a note before its captured
Note Duration deadline. If a live touch stops sending every valid U/V/On heartbeat,
the three-second watchdog produces one internal Cancel that immediately clears all
scheduled ownerships for that source. There is no server-facing `/cancel` message.

Cosmic Microwave keys live note ownership by source and its `finger0` touch, so packet-by-packet channel
round-robin is not used. In Notes Only, same-channel/same-note owners are reference
counted so one wrapped source cannot prematurely release another held owner.

Changing output state, source routing mode/channel, destination,
MIDI Output Path, Expected Zone, UDP ownership, or UDP port triggers safety reset
handling. Changing the active Time Field domain also
releases and rehydrates canonical held touches under the new schedule. If a receiver
ignores All Notes Off, use its own panic control as well.

## I still see CC11/CC74 or hear old expression changes

Cosmic Microwave 2.7.1 does not generate CC11, CC74, Channel Pressure, Pitch Bend, RPN,
MPE, or Crowd Macro CC messages during normal performance. If a MIDI monitor still
shows one of them, check the rest of the Live set:

1. Open every recorded/receiving MIDI clip and inspect **Envelopes -> MIDI Ctrl**.
2. Delete legacy CC11 and CC74 envelopes, or test with a new empty clip/track.
3. Disable MIDI effects, Max for Live devices, controller mappings, and automation
   lanes that can independently generate those messages.
4. Confirm the receiver consumes only one path; Mirror plus Host and External routing
   can duplicate old clip data.
5. Reload the receiving instrument or preset, or use its own Reset/Panic command.

Many instruments latch the last CC11 or CC74 value. Stopping transport, sending Note
Off, replacing Cosmic Microwave, or installing v2.7.1 may not clear that value inside the
receiver. If the sound remains quiet or filtered, reset/reload the **receiving
instrument**. Cosmic Microwave deliberately does not send a migration CC because that
would violate the Notes Only contract.

During a clean performance test, the monitor should show Note On and Note Off only.
Moving V while a note is held produces no MIDI; V is sampled as the next Note-On
velocity. Crossing a U pitch boundary starts the new pitch; the old pitch remains until
its own stored duration deadline.

## Atomic pitch is quantized or external tuning is unexpected

First check **PITCH SYSTEM**:

- **Tonal** contains standard 12-TET notes.
- **Atomic** contains element-derived targets, but Notes Only represents each target
  with its nearest MIDI semitone.

This quantization is intentional in v2.7.1: Cosmic Microwave sends no Pitch Bend. If the
result is otherwise out of tune:

- check whether it has its own `.tun`, Scala, transpose, or pitch map enabled;
- apply the same receiver tuning to every channel that should sound alike; and
- check for another transpose, tuning, or Pitch Bend source on the route.

A MIDI monitor should show Atomic's nearest Note On/Off numbers and no Pitch Bend.
External receiver tuning is explained in
[05 - Pitch Systems & External Tuning](05-tuning-files.md).

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
- Turn off simulator **Move active U/V** when it is not needed.
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

The **MERGED** figure is cumulative load telemetry. It counts pending work whose
admission window elapsed. Held intent is renewed, while released short taps can
expire. It is not packet loss and does not send MIDI, change velocity, or alter another
controller. A pending Ensemble short tap is kept for at least one full lane-cycle
opportunity before expiry.

## FAQ

### Why is Cosmic Microwave still listed as an instrument?

The silent stereo instrument shell preserves Ableton placement and the existing VST3
class contract. Its runtime behaviour is MIDI-only. If a host specifically requires a
MIDI-effect category, the repository also builds **Cosmic Microwave MIDI** and
**Cosmic Microwave MIDI Generator**.

### Can I use the Standalone app?

Yes. Select its port-named virtual output or a hardware MIDI destination, then subscribe
to that route in an external receiver. The Standalone app does not make sound itself.

### Does the plugin infer a zone from 6062 or 6063?

No. It reads the zone segment from valid `/cs/...` messages. Port-to-zone assignment is
owned by the upstream server and show configuration. Fresh instances may select the
lowest free complete factory pair (`6062/A..6069/H`), but Expected Zone remains an
explicit validation filter and is never inferred from received traffic. Saved state,
manual edits, and presets win exactly over that fresh allocator.

### Global Conductor says Local Fallback

Confirm every participating instance is in the same group, has a unique valid UDP
port, and is set to Leader or Follower. At least one live Leader is required; the lowest
leader port wins deterministically. A missing allocation or heartbeat older than 1.5
seconds safely returns each instance to its local Time Field quotas. Groups coordinate
only instances in the same plugin process, not another computer or host process.

### Where did Crowd Expression controls go?

They were retired from the v2.8.0 Notes Only product. Old session state may still contain
their parameter values for compatibility, but the controls are hidden and inert and no
Crowd Macro CC is emitted.

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

### Why did a new session open on Zinc / Core?

Cosmic Microwave 2.8.0 intentionally defaults new sessions to **Atomic / Zinc /
Core**. Choose **Tonal** for the seven conventional 12-TET maps. The historical
schema-2 migration still selects Tonal, while released 1.x element/spectral choices
migrate to Atomic and recover their corresponding element. Separately, schema-3 and
older state receives Flow for the Time Field. Schema-5 input remains compatible and
its retired experimental fields are discarded. Schema-6-or-earlier state receives
Manual Governor mode without altering its saved Time Field controls; new state uses
schema 11. The historical schema-9 migration still coerces retired MPE state to Notes
Only / Per source and disables Crowd Expression output. Schema 10 adds Note Duration
and Source Capacity: missing duration becomes 16n, schema-9-or-earlier state restores
its implicit capacity 256, and fresh/schema-10 partial state uses capacity 64.
Schema 11 adds Ensemble Same Note; missing, malformed, and out-of-range state restores
Tie so an older set cannot begin hard-retriggering unexpectedly.
Schema-7-and-earlier state also preserves Mirror output, shared UDP ownership,
Expected Zone Any and Safety Governor Off. A genuinely fresh session starts External
Only with exclusive ownership and atomically claims the lowest free complete A-H route:
Zone A / Leader first, then Zones B-H / Follower. Its port-named virtual endpoint follows
that claim. Safety Governor stays Off. Safety remains an explicit show-time enable so Venue
Preflight can report it as a blocker. Retired MPE and Crowd Expression values cannot
reactivate non-note output.

### What happened to the previous sound-generation controls?

They were removed from the 2.0 flagship and remain absent in 2.7.1. Old repositories may
retain archival media or implementation files, but the current flagship target neither
compiles nor loads them.

### What should I include in a bug report?

Include the Cosmic Microwave version, host/OS version, UDP port, Expected Zone and
mismatch count, ownership status, exact OSC address/argument, MIDI Output Path and
endpoint, Safety state/reasons/telemetry, Time Field/Conductor quota, observed zones,
receiver channel/configuration, and a short MIDI-monitor or Chaos Lab capture when
possible.
