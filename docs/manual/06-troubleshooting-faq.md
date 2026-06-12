# 06 — Troubleshooting & FAQ

Quick fixes first, then the longer explanations. The **Debug** view (top bar) is your
best friend throughout: it shows the outgoing MIDI/MPE byte stream, incoming MIDI,
scale state, and OSC status — with **Copy** / **Copy Report** buttons for sharing.

---

## No sound

Check in this order:

1. **OUTPUT MODE** (MIDI output row) — if it is *MIDI Only*, the internal engine is
   intentionally silent. Set *Audio Only* or *Audio + MIDI* to hear the plugin.
2. **Mute** (top bar) — the macro panel must read **OUTPUT ARMED**. If it says
   **OUTPUT MUTED** (and the Performance overlay shows a red **MUTED**), untoggle
   Mute.
3. **Is anything playing it?** If the audience map shows **“Waiting for audience”**,
   no seats are active — click **+ Add** or **+25 Crowd** in the SIMULATOR row, or
   play the SCALE KEYBOARD strip. The **PARTICIPANTS** counter should leave 0.
4. **Library loaded?** In Sample mode the top-bar **LIB** pill shows the loaded
   sample count; `0` means the active library is empty or missing — pick another
   library in the rail or hit **Rescan**.
5. **Levels** — TEXTURE tab: **MASTER** up; extreme **WET/DRY** with **SPACE** at
   zero can starve the output.
6. Standalone only: check the app's audio device settings; in a DAW check the track's
   monitor/output routing.

**Panic** (top right) is always safe: it stops voices, delay, and reverb and resets
MIDI state without changing your patch.

---

## MIDI is not arriving at the receiver

1. **Generation on?** MIDI is emitted only when **OUTPUT MODE** ≠ *Audio Only* AND
   **MIDI OUT** ≠ *Off*. The activity readout (`MIDI n`) must count up while notes
   play — if it does not, the output is off at the source.
2. **Right destination?** Check the **MIDI OUTPUT** combo and its status line:
   - *Host MIDI Output* — the stream goes to the DAW; in Ableton, route *from* the
     SpektraSynth track into the receiver track.
   - *Virtual: SpektraSynth MIDI Out* — the receiver must subscribe to the port named
     **SpektraSynth MIDI Out**. If the status says *“Virtual MIDI port unavailable”*,
     the OS refused the port and output fell back to the host bus.
   - A device name — physical/system port; if the status says *“Failed to open MIDI
     output”* or *“MIDI output device not found”*, click **Rescan** and reselect.
3. **Ghost numbered ports** — after a host plugin rescan (or with several instances
   open) you may see **“SpektraSynth MIDI Out 2”**, **“… 3”** listed in other apps:
   each live plugin instance that opens a virtual port gets the next number. **Pick
   the unnumbered “SpektraSynth MIDI Out”** (the first/primary instance). If stale
   numbered entries linger after rescans, restart the host — the leftover instances
   (and their ports) disappear.
4. Note that selecting a virtual/physical port does **not** silence the host bus —
   the status line reminds you *“host MIDI output remains available”*. Duplicate
   notes at the receiver usually mean you are listening to both paths at once.

---

## Microtones sound like plain 12-TET

The notes arrive but every degree lands on an ordinary semitone:

- **Receiver bend range mismatch** — the receiving synth's pitch-bend range must
  equal SpektraSynth's **BEND** value (default **2 st**). Many receivers ignore the
  bend-range RPN that the **Setup** toggle sends; set the range by hand on the
  receiver. (This default exists precisely because ±2 st is the universal fallback —
  see [03 — MPE Setup](03-mpe-setup.md).)
- **Receiver not in MPE mode** — in single-channel/omni mode the receiver applies the
  *last* pitch bend to *all* notes; chords collapse to one shared detune. Enable its
  MPE / multi-channel mode and match the **ZONE** (Lower: master 1, members 2–16).
- **MIDI OUT is set to Normal MIDI** — Normal MIDI deliberately sends no pitch bends;
  only *MPE MIDI* carries the microtones.
- **The Ableton single-stream routing limitation** — routing the whole MPE stream
  through one Live MIDI track into one plugin collapses per-note bends (the receiver
  ends up applying bend globally). The plugin's own stream is spec-correct (verified
  byte-level; see the Debug console). Use the verified workarounds: per-channel split
  routing to multiple receiver instances, a standalone receiver via the virtual port,
  or an MPE-native host such as Bitwig. Details in
  [03 — MPE Setup §9](03-mpe-setup.md).

To localize the problem: open **Debug** view, play one key, and confirm a
`Pitch Wheel` message on a member channel immediately *before* each `Note On`. If
those bytes are right (they are also covered by the automated MPE tests), the
remaining suspect is the receiver or the routing.

---

## Velocity / pressure feels global, not per note

In **MPE MIDI** mode, expression is per note *because* each note has its own channel:
channel pressure, CC74 (Slide), and CC11 only affect that one note — provided the
receiver is in MPE mode. If the receiver treats the stream as ordinary multi-channel
MIDI, **channel pressure is applied to the whole channel/patch**, so one seat's Y
movement appears to swell *everything*. Fix on the receiver side: enable MPE (or at
minimum per-channel pressure handling).

In **Normal MIDI** mode this behaviour is expected and unavoidable: all notes share
the single **CH** channel, so pressure/CC74/CC11 are genuinely global there.

---

## CPU is too high / audio dropouts

Biggest levers first:

- **POLY** — drop from *Ultra* (1024) / *High* (512) to **Normal** (256 voices). The
  engine already folds unison from 3 → 2 → 1 voices per trigger as the crowd grows,
  but the hard voice cap is yours to set.
- **Element Partial** (VOICES tab, **PARTIAL**, with **Solo** off) — in Element mode
  this caps how many raw lines each voice renders (up to 512). Dense elements (Iron
  has thousands of catalogue lines; up to 512 render per voice) are the most
  expensive thing in the instrument — pulling PARTIAL down to e.g. 64–128 keeps the
  character and cuts the additive cost dramatically.
- Prefer **Sample Player** over **Granular** when you don't need grain clouds; in
  Granular mode, lower **DENSITY** and **SIZE**.
- Reduce simulated crowd size; turn off **Random Movement** when idle (every move is
  an engine event).
- Raise the host/standalone audio buffer size.
- *Audio Only* vs *Audio + MIDI*: the MIDI path is cheap, but if you only need MIDI,
  *MIDI Only* skips audio rendering entirely.

---

## Samples load with the wrong pitch / “load truncated” warning

**Root-note parsing.** A sample's root pitch is read from its *filename*: the first
note-name token — letter `A`–`G`, optional `#` or `b`, then the octave number
(negative allowed) — wins. Examples: `C2.wav`, `F#3.wav`, `Bb4.wav`, `Piano_C3.wav`
(→ root C3). Standard MIDI numbering applies (C-1 = 0, C4 = middle C = 60).

- **No parsable root → fallback C4.** A file like `Pad_Warm.wav` is treated as C4 and
  will be pitch-shifted from there — often audibly wrong. Rename it with its real
  root (e.g. `Pad_Warm_A2.wav`) and **Rescan**.
- Beware accidental matches: the parser scans for the first letter+digits pattern, so
  embedded tokens can be read as roots. Keep names in the `Name_Note.ext` shape.
- Files are `.wav`, `.aif`, `.aiff`, or `.flac`, in subfolders of `Samples/`; macOS
  `._` resource files are skipped automatically. Trailing silence is trimmed in
  memory at load (with a short fade), so files report slightly shorter than on disk.

**Load budget.** To protect RAM, a library load stops at **4096 samples** or **2 GiB
of decoded audio**, whichever comes first. When a cap is hit the library status shows
**“WARNING: load truncated (…cap reached)”** (also written to the log) and the rest
of the folder is skipped — nothing fails silently. Fix: split the folder into
smaller libraries, or reduce file lengths/sample rates.

When a note is triggered, the engine picks the sample whose root is *nearest* the
target pitch, then pitch-shifts the remainder — so well-spread root notes across the
library mean less shifting and better tone.

---

## OSC quick checks

(Full detail in [04 — OSC & the Audience](04-osc-audience.md).)

- Top-bar **UDP** pill red / `busy` → another app owns the port. Change the port in
  **NETWORK** and **Apply**.
- Status **“UDP 6060 PORT FULL - max 16 clients reached…”** → too many plugin
  instances share that port; this instance receives nothing. Close instances or give
  this one its own port.
- Messages arrive but nothing happens → verify the address shape
  `/cs/<A–Z>/<0–99>/finger<n>/<on|off|line|v>`; out-of-range columns and malformed
  addresses are ignored by design.

---

## FAQ

**Is this what atoms really sound like?**
No — and the instrument doesn't claim to be. Atoms emit light, not sound.
SpektraSynth *translates*: atomic spectra are translated into playable musical scales
and timbral fingerprints. The wavelength ratios are real (from emission-line
catalogues); their mapping into pitch and amplitude is a musical translation, applied
consistently across the synth, the MPE output, and the tuning files.

**Why is Nitrogen missing?**
The source data folder has no matching `N.txt` spectral dataset yet, so Nitrogen is
deliberately left out of the 29-element list (Hydrogen … Zinc) rather than shipped
with placeholder data.

**Why does my MIDI keyboard play “wrong” keys in element scales?**
Check **MIDI IN**: in *Trigger* mode incoming keys index the visible scale-keyboard
steps (root key = step 1, walking upward), which is the intended way to play compact
atomic scales; keys beyond the degree count fall back to the nearest displayed step.
*Scale* quantizes incoming pitches to the current scale; *Direct* plays plain MIDI
pitch.

**Do I lose the timbre when I reduce ATOM SCALE to Core?**
No. Scale reduction (Core/Extended/Microtonal/Scientific/Raw) only changes the
*playable degrees*; the full raw spectrum stays available to the timbre engine. Only
the **PARTIAL** control (non-solo) limits the audible partials.

**Where do I report what I'm seeing?**
Debug view → **Copy Report** copies the full MIDI/state report (incoming, outgoing,
voices, scale) to the clipboard — paste that into your report along with the plugin
version (see `README.md`).
