# 03 — MPE Setup

SpektraSynth's scales are microtonal by design — atomic spectra are translated into
playable musical scales, and most of those degrees fall *between* the 12-TET notes.
Plain MIDI cannot carry that. **MPE MIDI** can: each sounding note gets its own MIDI
channel with its own pitch bend, so every voice lands exactly on its translated
spectral pitch in the receiving synth.

This chapter is the one to read carefully if you want the microtones to survive the
trip into another instrument.

## 1. Turn the MIDI output on

Two combos in the MIDI output row (macro panel, bottom) gate everything:

- **OUTPUT MODE** — *Audio Only* (default), *MIDI Only*, or *Audio + MIDI*.
  MIDI is only generated when this is **not** *Audio Only*. *MIDI Only* silences the
  internal engine and emits MIDI; *Audio + MIDI* does both.
- **MIDI OUT** — *Off* (default), *Normal MIDI*, or *MPE MIDI*.
  - **Normal MIDI** sends the nearest 12-TET note on one channel (**CH**), with CC74
    and CC11 expression but **no pitch bends** — microtonal detail is rounded away.
    Use it for conventional receivers.
  - **MPE MIDI** sends nearest-note **plus per-note pitch bend** on MPE member
    channels — the spectral cents are preserved for compatible receivers.

Both the audio engine and the MIDI/MPE output resolve pitches through the same scale
resolver, so what you hear internally and what you send externally are the same
translation.

## 2. Choose a destination

The **MIDI OUTPUT** combo selects where the generated stream goes:

- **Host MIDI Output** — the plugin's MIDI bus into the DAW (in Ableton: route *from*
  the SpektraSynth track into another track's MIDI input).
- **Virtual: SpektraSynth MIDI Out** — creates a system-wide virtual MIDI port named
  **“SpektraSynth MIDI Out”** that any other app (standalone synths, other DAWs) can
  subscribe to. If more than one SpektraSynth instance opens a virtual port, later
  instances get numbered names (*SpektraSynth MIDI Out 2*, *3*, …).
- **Any physical/system MIDI device** — the rest of the list. **Rescan** refreshes it.

Even with a virtual or physical port selected, the host MIDI bus keeps receiving the
stream as well (the status line notes *“host MIDI output remains available”*).

## 3. Zones — the ZONE control

MPE splits the 16 MIDI channels into a *master channel* (global messages) and *member
channels* (one per note). The **ZONE** combo selects the layout, and the zone fully
owns the channel mapping:

| ZONE | Master channel | Member channels | Notes |
|---|---|---|---|
| **Lower** *(default)* | 1 | 2–16 (15 voices) | The common convention; matches most receivers out of the box. |
| **Upper** | 16 | 1–15 (15 voices) | Use when the receiver is configured for an upper zone, or to keep ch 1 free. |

Switching the zone mid-performance is safe: SpektraSynth sends a safety all-off on
the old channels, resets its channel allocator, and re-sends the MPE configuration on
the new master before the next note.

**Match the receiver:** if SpektraSynth is on Lower and the receiving synth listens on
Upper (or vice versa), notes will land on channels the receiver treats incorrectly.

## 4. Bend range — why the default is 2 st

The **BEND** combo sets the MPE pitch-bend range for the member channels:
**2 st** *(default)*, **12 st**, **24 st**, **48 st**.

The default is deliberately the smallest:

- Every outgoing note is encoded as the **nearest 12-TET note plus a bend**, so the
  bend needed is always at most **±50 cents** — well inside a ±2-semitone range.
- SpektraSynth announces its bend range via RPN, but some receivers **ignore the
  RPN** and stay on their own default. ±2 semitones *is* the near-universal
  MIDI/synth default — so even an RPN-deaf receiver interprets the bends correctly
  and the microtonal scale survives.
- Larger ranges divide the same 14-bit bend resolution over more semitones; 2 st also
  gives the finest pitch resolution.

Pick a wider range only if you use **MPE PITCH = Glide** with large slides (a glide
can only travel as far as the bend range allows around the original note).

**Whatever you pick, the receiver must be set to the same value** — see the checklist
below. A mismatch does not mute anything; it just bends every note by the wrong
amount (a 2 st bend interpreted as 48 st sounds wildly sharp/flat — or, more commonly,
microtones quietly collapse to 12-TET).

## 5. The Setup toggle (MPE configuration messages)

With **Setup** on (default), SpektraSynth sends the standard MPE configuration
whenever it is needed (first note after enabling MPE, or after any zone / bend-range /
mode change):

1. **MPE Configuration Message (RPN 6)** on the **master** channel, declaring the
   member-channel count for the zone.
2. A **pitch-bend-range RPN (RPN 0)** on **every member channel**, set to the BEND
   value.

Leave it on unless the receiver documents that it must not receive RPNs. With it off,
you must configure zone and bend range entirely on the receiver.

## 6. What is actually sent (per note)

For each note-on, in this order (order matters — the bend always precedes the note):

1. `Pitch Wheel` — the per-note bend, **sent before the note-on** so the note starts
   in tune.
2. `CC74` (Timbre/Slide) — from the seat's X position blended with the **MOTION**
   macro (`x·0.68 + motion·0.32`).
3. `CC11` (Expression) — from Y blended with **ENERGY** (`y·0.70 + energy·0.30`).
4. `Note On` — velocity from the seat's Y value.
5. `Channel Pressure` — from Y; updated continuously while the note holds.

While a note sounds, movement updates re-send pitch bend, pressure, CC74, and CC11
(only when they change by more than one step, to keep the stream lean). Note-off
sends `Note Off`, `Channel Pressure 0`, and recenters the pitch wheel.

**MPE PITCH — Retrig vs Glide:** with *Retrig* (default), a moving seat that crosses
to a different scale degree retriggers as a new note (new bend + note-on). With
*Glide*, if the new degree rounds to the **same** MIDI note number, the pitch bend is
updated instead — a smooth slide; degree changes that need a different note number
still retrigger.

## 7. Receiver checklist

On the receiving synth/plugin:

- [ ] **Enable MPE mode** (sometimes called "MPE", "Multi-channel", or per-note
      expression mode). A receiver in plain omni/single-channel mode will play the
      notes but apply bends globally — microtones will smear or vanish.
- [ ] **Match the zone** — Lower (master 1 / members 2–16) by default.
- [ ] **Match the pitch-bend range** to SpektraSynth's **BEND** value (default
      **2 st**). If the receiver honours RPNs and SpektraSynth's **Setup** is on,
      this happens automatically; otherwise set it by hand.
- [ ] **Map CC74 (Slide) and CC11** if you want X/macro expression, and route
      **channel pressure** for Y.
- [ ] Give it **at least 15 voices** of polyphony if you intend to use the full
      member range — a crowd can hold many notes at once.

Then verify: enable **Debug** view in SpektraSynth and watch the **outgoing MIDI /
MPE** console while you press one key on the SCALE KEYBOARD. You should see the
pitch-wheel message immediately before each note-on, on a member channel.

## 8. Channel allocation (what the MPE a/f counter shows)

Each active note owns one member channel until its note-off. Channels are handed out
**round-robin**: the allocator scans for a free channel starting *after* the last
channel it handed out, wrapping around the member range. The practical effect is that
a **just-freed channel is the last one to be reused**.

Why: some receivers latch a channel's pitch state when a note-on arrives. If a new
note immediately reused the channel that a different note just left, a slow receiver
could capture the *stale* bend and start the note out of tune. Cycling through the
other channels first gives every receiver time to settle — this matters precisely
because SpektraSynth's whole point is per-note microtonal accuracy.

When **all member channels are busy**, the oldest sounding note is stolen: it gets a
proper note-off first, then its channel is reassigned. The activity readout next to
the MIDI row shows this live: `MPE 7/8` means 7 active MPE voices, 8 member channels
still free.

## 9. KNOWN LIMITATION — single-stream MPE routing in Ableton Live

**Symptom:** you route SpektraSynth's MPE output through an Ableton Live MIDI track
into one MPE-capable plugin, play a chord of microtonal degrees — and every note
snaps to the *same* detune, or to plain 12-TET.

**Cause:** when one MIDI track funnels the full multi-channel MPE stream into a
single receiving plugin, the per-channel separation can be collapsed on the way in
(channel data is merged, so the receiver applies the most recent pitch bend
*globally* instead of per note). The result: per-note bends stop being per-note.

**The plugin's own output is not the problem.** SpektraSynth's MPE stream is
spec-correct — bend-before-note-on, MCM + per-member RPN setup, per-channel
allocation — and this is verified at the byte level by the automated MPE output tests
(`Tests/MpeOutputTests.cpp`) and visible in the Debug-view MIDI console.

**Verified workarounds:**

- **Per-channel split routing** — create one receiver track *per member channel* in
  Live (track MIDI-From: SpektraSynth, channel 2; next track channel 3; …), each with
  its own instance of the receiving instrument. Each instance then sees exactly one
  note + one bend, and the microtones are exact. This works.
- **Standalone receivers** — send to a standalone synth app via the virtual port
  **SpektraSynth MIDI Out** (or a hardware synth). Outside Live's track routing the
  stream arrives intact. This works.
- **MPE-native hosts** — hosts with first-class MPE routing (e.g. **Bitwig Studio**)
  pass the per-note expression through to MPE plugins correctly.

If you must stay on a single Live track, prefer **BEND = 2 st** (default) so that
even a collapsed stream degrades to "nearest note, slightly mistuned" rather than
octave-wild jumps — but for real microtonal accuracy use one of the workarounds.

## 10. Quick recipes

**Standalone software synth (most reliable):**
1. MIDI OUTPUT → *Virtual: SpektraSynth MIDI Out*; OUTPUT MODE → *Audio + MIDI* (or
   *MIDI Only*); MIDI OUT → *MPE MIDI*; ZONE *Lower*; BEND *2 st*; Setup *on*.
2. In the receiver app: MIDI input = *SpektraSynth MIDI Out*, MPE on, bend range 2.

**Ableton Live, one microtonal voice per track:**
1. MIDI OUT → *MPE MIDI*, destination *Host MIDI Output*.
2. Create receiver tracks with MIDI-From = SpektraSynth, channels 2, 3, 4, … —
   one plugin instance per track.

**Plain MIDI sketching (no microtones):**
1. MIDI OUT → *Normal MIDI*, set **CH**, destination as needed. Notes are the nearest
   scale notes; no bends are sent.
