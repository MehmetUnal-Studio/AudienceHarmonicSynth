# 04 — OSC & the Audience

SpektraSynth is played by a crowd. Each participant's phone sends OSC messages over
UDP to the machine running the plugin; each participant is one **seat** in a virtual
venue. This chapter documents the seat model, the wire format, port configuration,
and the built-in simulator.

## The seat model

The venue is a fixed grid:

- **26 rows**, lettered `A` … `Z`
- **100 columns**, numbered `0` … `99`
- → up to **2,600 seats**

A seat is identified purely by its address (row + column) — there is no login or
session. Each active seat contributes one voice layer to the texture; its **X** value
chooses pitch on the selected scale and its **Y** value drives amplitude/expression.
The **AUDIENCE MAP** in the editor mirrors this grid live.

## Wire format

Messages use this OSC address pattern (defined in `Source/OscWireFormat.h`):

```
/cs/<row>/<col>/finger<n>/<param>
```

| Segment | Meaning |
|---|---|
| `/cs/` | Literal "control surface" prefix. Case-insensitive. |
| `<row>` | A single letter `A`–`Z` (case-insensitive) → row 0–25. Any extra characters after the letter up to the next `/` are ignored (`/cs/A/...` and `/cs/A1/...` both mean row A). |
| `<col>` | One or more decimal digits → column index. Must be in `0…99`; out-of-range columns are rejected. |
| `finger<n>` | The finger id. The segment is **not inspected** — finger indices are ignored, so multiple fingers from one seat collapse onto that single seat. |
| `<param>` | The action — one of the four below (case-insensitive). |

### Parameters

| Param | Argument | Semantics |
|---|---|---|
| `on` | int or float; any non-zero value | Seat activates (note on). `0` deactivates. |
| `off` | none required | Seat deactivates (note off). |
| `line` | float `0 … 127` | **X position** → pitch. Internally normalized to 0–1 and quantized to the current root/scale/octave range (or to the element's translated spectral scale). |
| `v` | float `0 … 1` | **Y position** → voice amplitude / expression. |

### Examples

```text
/cs/A/0/finger0/on    1        seat A0 activates
/cs/A/0/finger0/line  64.0     seat A0 moves X to mid-scale
/cs/A/0/finger0/v     0.8      seat A0 raises Y to 0.8
/cs/A/0/finger0/off            seat A0 releases
/cs/M/41/finger2/on   1        seat M41 activates (finger index irrelevant)
```

Anything that does not match the pattern (wrong prefix, invalid row letter, column
≥ 100, unknown trailing param) is silently ignored — malformed traffic cannot crash
or detune the instrument. The OSC receive path only validates and enqueues events;
the audio thread consumes them lock-free, so a flood of messages does not glitch
audio.

## UDP port configuration & status

- **Default port: `6060`.** Change it in the **NETWORK** section of the ribbon: type
  the port and click **Apply** (the OSC listener restarts on the new port).
- The top-bar **UDP** pill shows the port in green while listening, or `busy` in red
  if the port could not be bound.
- The empty-state hint on the audience map repeats it in plain words:
  *“Waiting for audience — Listening for OSC on UDP 6060.”*
- The exact status string (also visible in Debug view) is one of:
  - `Listening on UDP <port>` — all good.
  - `FAILED to bind UDP <port> - port busy?` — some other app owns the port; pick a
    different one or close the conflict.
  - `UDP <port> PORT FULL - max 16 clients reached, this instance is not receiving OSC`
    — see below.
  - `Stopped` — the listener is off.

### Port sharing and the 16-client cap (“PORT FULL”)

Multiple SpektraSynth-family instances inside the same process (e.g. several plugin
instances in one DAW) can all listen on the **same** UDP port: the first one binds
the socket and the others attach to it, with incoming messages fanned out to every
attached instance. The fan-out table holds at most **16 clients**. The 17th instance
on that port is not fed any OSC and reports **PORT FULL** instead of failing
silently. Fix: close unused instances, or give extra instances their own port
numbers.

## The simulator — a stand-in audience

Everything the network can do, the **SIMULATOR** row can fake — same seat events,
same engine path, no traffic:

- **+ Add** — drops one simulated participant onto a free seat.
- **+25 Crowd** — adds 25 at once.
- **Remove** — ends one simulated participant.
- **Random Movement** — continuously drifts every active participant's X/Y, which is
  the quickest way to hear scale quantization and expression mapping.
- **Clear All** — clears all simulated/live seats and immediately stops voices,
  delay, and reverb (equivalent to **Panic**).

Use the simulator to soundcheck a patch, rehearse macro moves, and verify MIDI/MPE
output before a single phone connects. Simulated and real seats coexist — you can pad
a thin real audience with simulated seats.

## Basic network tips

- **Same network:** participants' devices and the SpektraSynth machine must reach
  each other — typically one Wi-Fi LAN. Send to the host machine's LAN IP, port 6060
  (or your configured port).
- **Firewall:** allow inbound UDP on the chosen port for the host/standalone app
  (macOS will usually prompt the first time).
- **UDP is fire-and-forget:** there are no acknowledgements and no retransmits. The
  protocol is deliberately tolerant — a lost `line`/`v` message just means the next
  one lands a moment later; a lost `off` is cleaned up the next time that seat sends
  anything, and **Panic** always provides a hard reset from the stage.
- **Keep rates sane:** phones should throttle `line`/`v` updates (tens of messages
  per second per seat is plenty). The engine coalesces per-seat state, so the *last*
  value wins anyway.
- **One venue, many engines:** because instances can share the port (up to 16), you
  can run the audio synth and a MIDI generator side by side on the same audience
  feed.
- For audience-scale stress testing without a crowd, combine **+25 Crowd** several
  times with **Random Movement**, and watch the **PARTICIPANTS / ACTIVE VOICES**
  counters and your CPU meter.
