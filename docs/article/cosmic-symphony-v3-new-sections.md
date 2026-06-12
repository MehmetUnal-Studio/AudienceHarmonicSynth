# The Sound of Light — v3 New Sections (Draft)

> **Purpose.** Drafted additions for the next revision of *"The Sound of Light: Sonification of Atomic Emission Spectra into Musical Scales and Timbres"* (Ünal, v2). Written in the paper's academic register, in English, ready to be transplanted into the LaTeX source. Each section below states **where it slots into the v2 structure**. Equation/table/figure numbers are placeholders — renumber on integration.
>
> **Editorial thesis carried throughout (author's position):** the methodology *translates* atomic spectra into musical material; it does not claim to reveal "the real musical sound of atoms." Headline formulations used in this draft:
> - *"Atomic spectra are translated into playable musical scales and timbral fingerprints."*
> - *"A spectral instrument that turns elemental emission lines into playable scales, tunings, and timbres."*

---

## 0. Suggested revised Abstract (replaces v2 abstract)

This paper presents a systematic methodology for sonifying atomic emission spectra — **translating** the spectral line radiation data of chemical elements **into playable musical scales and timbral fingerprints**. Wavelengths from the NIST Atomic Spectra Database are converted to frequencies via the fundamental relation *f = c/λ*, yielding element-specific frequency ratios. These ratios are mapped into the audible range to construct microtonal scales measured in cents, while the relative intensities of the spectral lines serve as amplitude coefficients in an additive synthesis framework to generate distinctive timbres. The two outputs — *scale* and *timbre* — are conceptually and procedurally distinct: the scale encodes the *intervallic* structure inherent in an element's spectrum, whereas the timbre encodes its *spectral envelope*. Relative to the previous revision, this version extends the methodology in four directions: (i) a **clustering stage** that reduces the dense line sets of heavier elements (up to several thousand observed lines) to playable scale families at five resolutions; (ii) an exportable **tuning library** (AnaMark `.tun` / Scala ecosystem) covering 29 elements, decoupling the scales from any single instrument; (iii) a production **real-time instrument** (*SpektraSynth*, C++/JUCE) driven by audience interaction over OSC, superseding the Max/MSP prototype; and (iv) an **MPE-based delivery layer** that carries per-note microtonality to third-party synthesizers, together with an empirical study of receiver compatibility in commodity hosts. We also make the epistemic status of each pipeline stage explicit, distinguishing physical data from creative mapping decisions. The approach establishes a reproducible, physically grounded bridge between atomic physics and musical composition, contributing to the broader field of scientific data sonification.

*(Keywords: add — microtonal tuning files, MPE, MIDI Polyphonic Expression, audience interaction, real-time synthesis.)*

---

## 1. New subsection §3.1 — Epistemic Framing: Translation and Artistic Licence

> **Placement:** first subsection of §3 *Methodology* (existing §3.1–3.6 shift to §3.2–3.7). The v2 introduction already asks whether spectra "can be *translated* into a coherent musical language"; this subsection makes that stance explicit and auditable.

Before detailing the pipeline, we make its epistemic status explicit. The mapping from emission spectrum to music is a **translation**, not a transcription: the source data are physical, but several stages necessarily involve creative, musically motivated decisions. Claims that the result is "the sound an atom really makes" would overstate the case — electromagnetic radiation at ~10¹⁴ Hz has no acoustic correlate, and no canonical mapping into the audible band exists. What the methodology preserves is the **dimensionless intervallic structure** (frequency ratios) and the **relative energy distribution** (intensity ratios) of the spectrum; what it adds are interpretive choices required to render those structures playable. Table N1 partitions the pipeline accordingly.

**Table N1.** Epistemic status of each pipeline stage.

| Stage | Physical data (invariant) | Creative / musical decision |
|---|---|---|
| Acquisition | NIST wavelengths λₖ and relative intensities Iₖ | Restriction to the visible band (380–700 nm) |
| Conversion | f = c/λ; ratios rₖ = fₖ/f₁; aₖ = Iₖ/I₁ | Choice of the longest-wavelength line as reference f₁ |
| Octave reduction | Ratio arithmetic (exact) | Folding into a single octave (1 ≤ r′ < 2) — a Western-octave convention |
| Scale construction | Cents values of the reduced ratios | Clustering parameters (degree count, minimum separation); root pinned at 0 cents |
| Keyboard mapping | — | White-key-first assignment of degrees (Section §6 below) |
| Timbre construction | Partial ratios and amplitudes | Envelope, filtering, effects; partial-count limiting in performance |
| Delivery | — | Root/register placement; MPE encoding choices (Section §8 below) |

This framing has a practical benefit beyond intellectual honesty: it identifies exactly which knobs a composer may turn without losing the physical grounding (everything in the right-hand column), and which quantities must remain untouched for the result to remain element-specific (the left-hand column). All subsequent sections annotate their choices against this table.

---

## 2. New section §5 — Scaling the Methodology: From Hydrogen to a 29-Element Library

> **Placement:** new section directly after v2 §4 (*Results: Hydrogen as a Case Study*). Existing §5 *Discussion* and §6 *Conclusion* shift down. This section answers v2's future-work item "extend the methodology to a broader range of elements."

### 5.1 The density problem

Hydrogen is a benign case: its seven visible Balmer lines yield ratios already inside one octave, and the resulting eight-degree scale is directly playable. Heavier elements are not so accommodating. Within the same visible window, the curated dataset contains 27 observed lines for helium, 333 for carbon, 1000 for neon, and **4041 for iron**. Direct application of the v2 pipeline to such elements produces scales with hundreds of degrees separated by fractions of a cent — physically faithful, but unplayable and perceptually undifferentiated. Translating *libraries* of elements therefore requires a principled **reduction stage** between ratio extraction and scale construction.

### 5.2 Clustering into scale families

We reduce each element's octave-folded cents set with a single-linkage agglomerative clustering, governed by two parameters: a maximum degree count *D* and a minimum inter-degree separation *s* (in cents). Within each cluster, the representative degree is the **medoid** — the member minimizing total distance to its cluster, weighted by line intensity — so that every scale degree corresponds to an *actually observed* emission line rather than a synthetic average. The cluster containing the reference line is pinned to 0 cents, preserving the root invariant of §3. Greedy selection by cluster weight then caps the result at *D* degrees.

Rather than a single compromise, we publish **five resolutions** of every element — a *scale family* (Table N2). The two ends of the family serve different users: *Core* yields seven-degree scales mappable onto a conventional keyboard's white keys (Section §6), while *Raw* preserves every observed line for analytical listening.

**Table N2.** Scale-family modes. *D* = maximum degrees, *s* = minimum separation.

| Mode | D | s (cents) | Intended use |
|---|---|---|---|
| Core | 7 | 80 | Diatonic-like performance; white-key tuning export |
| Extended | 12 | 40 | Full chromatic-keyboard mapping |
| Microtonal | 24 | 20 | Quarter-tone-class resolution |
| Scientific | 48 | 10 | Dense analytical scales |
| Raw | unlimited | 0 | Unreduced spectrum |

Per Table N1, *D* and *s* are creative parameters; the cents values of the surviving degrees are physical. The same clustering machinery, with the intensity weights retained, also bounds the **timbre** branch: performance instruments cap the additive bank (Section §7) at the strongest lines of the raw spectrum, a fidelity/efficiency trade-off the player controls.

### 5.3 The element library and reproducibility

Applying the family construction across the first thirty elements (hydrogen through zinc; nitrogen is presently omitted pending a curated line list of matching quality) yields a library of 29 × 5 = 145 scales plus 29 timbral fingerprints. Each element carries a reference wavelength λ_ref — its longest-wavelength retained line (e.g., boron 678.612 nm) — which anchors both the scale root and the timbre fundamental. The full dataset is generated from the curated NIST-derived line lists by a deterministic script, and a continuous-integration check regenerates the dataset and fails on any drift between the source data and the published tables, making every scale and timbre in this paper mechanically reproducible.

---

## 3. New section §6 — Exporting Tunings: The Atomic Tuning Library

> **Placement:** immediately after new §5. This section reflects the released tuning artifacts and answers the portability question v2 left open. **This is the "scales and tunings" extension named in the revised title sentence.**

A scale that lives only inside one bespoke instrument is a demonstration; a scale that any synthesizer can load is a *musical resource*. We therefore export the Core family as a library of tuning files — **Atomic CORE WhiteKeys** — in the AnaMark `.tun` format consumed by widely deployed synthesizers (e.g., Spectrasonics Omnisphere), with the Scala (`.scl`/`.kbm`) ecosystem as the companion interchange route. The library covers all 29 elements, one file per element.

### 6.1 White-key-first mapping

The Core mode's ≤ 7 degrees map onto the conventional keyboard as follows:

1. Degrees 1–7 are assigned, in ascending cents order, to the white keys **C D E F G A B** of every octave; the root (0 cents, the λ_ref line) is always C.
2. If an element retains more than seven degrees in a given export, degree 8 and beyond occupy the **nearest free black key**.
3. Keys with no assigned degree **duplicate the nearest assigned degree**, so no key is silent and the playable surface remains continuous.

This mapping is, per Table N1, a creative convention — it deliberately gives a pianist's muscle memory a foothold in each element's intervallic world: a C-major hand shape on the boron tuning *is* a boron cluster chord. Table N3 shows the boron file as a worked example.

**Table N3.** Atomic CORE WhiteKeys — boron (λ_ref = 678.612 nm). Values are cents from the root.

| Key | C | D | E | F | G | A | B |
|---|---|---|---|---|---|---|---|
| Cents | 0.0 | 177.4 | 322.3 | 472.3 | 605.2 | 721.7 | 863.1 |

Compared with 12-TET, the boron white keys read approximately as: C (±0), D −23, E −78, F −28, G +5, A −178 (closer to an A♭+22), B −237 (closer to an A+63) — an immediately audible departure from equal temperament whose internal logic is nonetheless consistent across every octave. A machine-readable manifest accompanies the library, recording for each element the retained degree count, the raw line count it was reduced from, λ_ref, and the exact cents mapping, so that the files are auditable against the generating dataset.

The tuning library decouples the *scale* half of the methodology from our own instrument entirely: composers can perform atomic scales with any tuning-capable synthesizer, while the *timbre* half remains available through the instrument described next.

---

## 4. New section §7 — SpektraSynth: A Real-Time Spectral Instrument

> **Placement:** after new §6; **supersedes** v2 §3.6 (*Stage 5: Digital Instrument Implementation*), which should be retitled "Stage 5: Prototype Implementation (Max/MSP)" and kept as historical context. This section answers v2's future-work items on temporal dynamics and polyphonic textures.

The Max/MSP prototype validated the pipeline; deploying it for installation and ensemble use motivated a production rewrite. **SpektraSynth** is a C++17/JUCE instrument (VST3 and standalone) realizing the complete translation chain in real time — *a spectral instrument that turns elemental emission lines into playable scales, tunings, and timbres.*

**Synthesis.** Each voice renders an element's timbral fingerprint with a phase-accurate additive bank of up to 512 partials drawn from the raw spectrum (lookup-table oscillators with linear interpolation), shaped by per-voice low-pass filtering, slow stochastic pitch/amplitude modulation, and a master effects chain (reverb, cross-delay, tape-style saturation, limiter). A performance control continuously limits the audible partial count, trading spectral fidelity against density; a *solo* mode auditions individual emission lines. A complementary sample/granular engine allows recorded material to be quantized to the same atomic scales, so spectral and concrete sources share one intonation system. The 145-scale library of §5 is compiled in and selectable per element and mode.

**Audience as ensemble.** The instrument's primary input is not a keyboard but a crowd. A venue is modeled as a seat grid of 26 rows × 100 columns; audience phones transmit per-seat control over UDP/OSC (`/cs/<row>/<col>/finger<n>/<param>`), where the horizontal axis is quantized to the active atomic scale and the vertical axis drives dynamics. Up to 1024 simultaneous voices with adaptive unison realize the *multi-element polyphonic textures* anticipated in v2 §6 — in the form of crowd-driven polyphony within an element, and multi-element counterpoint across multiple instances. This formalizes the interaction model first explored in the "Beyond the Light" installation (ARTECHOUSE × NASA, 2023) into a reproducible, openly testable instrument: the implementation carries an automated test suite (including byte-level verification of its MIDI output, §8) and continuous integration.

---

## 5. New section §8 — Delivering Microtonality: MPE Encoding and Receiver Compatibility

> **Placement:** after new §7. **This is the most novel engineering contribution of v3**: the question of *how element scales reach third-party instruments in practice*, including empirical host/receiver findings. (Tone: measured, vendor-neutral, version-noted.)

The tuning library (§6) serves synthesizers that load tuning files; a larger population of instruments does not, but does implement **MIDI Polyphonic Expression (MPE)**. SpektraSynth therefore also delivers its scales as a real-time MPE stream, encoding each sounding voice as an independently bent note.

### 8.1 Encoding

Each scale degree's frequency is expressed as the nearest 12-TET note plus a per-note pitch-bend offset. Because every degree lies within ±50 cents of some 12-TET pitch by construction, the offset never exceeds a quarter tone — which motivates transmitting at the **±2-semitone bend range**, the de facto universal default. This choice is deliberately defensive: a receiver that honors the transmitted bend-range RPN agrees exactly, and a receiver that *ignores* the RPN but defaults to ±2 st still reproduces the microtones correctly; wider ranges (±12/±24/±48 st, available for glide effects) would scale mis-readings by up to 24×. Per note-on, the emitter sends the bend *before* the note (pitch-bend → timbre CC74 → expression CC11 → note-on → channel pressure), per the MPE convention that receivers latch per-note pitch at note-on. Both MPE zones are supported (lower: master channel 1, members 2–16; upper: master 16, members 1–15), with the zone's Manager-Channel configuration message emitted on activation. The emitted byte stream — message ordering, zone setup, per-member RPNs, channel allocation, and voice stealing — is locked by an automated test suite at byte level, so the claims in this section are mechanically verified properties of the implementation rather than intentions.

### 8.2 Channel allocation under reuse

MPE multiplexes polyphony over at most 15 member channels, so channels are continuously recycled. We observed that the *naive* allocator (lowest free channel) immediately reuses the channel just vacated by a released note; some receivers then mis-associate the fresh note's bend with residual per-channel state, audibly collapsing the new note to 12-TET — precisely the failure musicians would attribute to "the microtones not working." Replacing the allocator with a **round-robin** policy, under which a freed channel is the *last* to be reused, gives every receiver maximal settling time per channel and removed the failure in our tests without altering the byte-level protocol. We propose freed-channel-last allocation as a general robustness practice for microtonal MPE emitters.

### 8.3 Receiver compatibility: an empirical taxonomy

Spectral scales are an unusually sensitive probe of MPE conformance: a receiver that mishandles *any* per-note pitch state converts microtones into audible 12-TET errors. Field testing against commodity software (2025–2026 versions) yields three receiver classes:

**Table N4.** Observed receiver behavior for per-note microtonal MPE.

| Class | Behavior | Consequence for atomic scales |
|---|---|---|
| Full MPE | Per-note bend and pressure honored per member channel | Scales reproduce exactly |
| Partial | Notes follow per-channel bend, but channel pressure (and some controllers) applied globally | Pitches correct; dynamics of held notes track the newest note |
| Collapsed | Multichannel stream merged before the synthesis engine; last-arriving bend governs all notes | Polyphonic microtonality destroyed (every chord re-tempers itself) |

Two findings deserve emphasis. First, classification is **not a fixed property of the receiver plugin**: one widely used MPE synthesizer behaved as *full MPE* standalone, yet *collapsed* when the identical stream was routed to it as a hosted plugin through a DAW's track MIDI input — demonstrating that the host's internal MIDI routing, not the instrument, discarded per-note channel association. Second, the practical workarounds are systematic: (i) host the receiver standalone or in an MPE-native host; or (ii) split the member channels across multiple single-channel receiver instances, which converts per-note bend into ordinary per-channel bend and is robust in every host we tested. We document these because deployment knowledge of this kind is, in our experience, the actual barrier between a published sonification methodology and its use by working musicians.

---

## 6. Replacement text — §Limitations and §Conclusion deltas

> **Placement:** amend v2 §5.4 (*Limitations*) and §6 (*Conclusion*) as follows.

**§5.4 Limitations — updated list.**
- The restriction to the visible spectrum remains (UV/IR lines could supply further degrees and partials).
- NIST relative intensities remain condition-dependent; clustering weights inherit this uncertainty.
- ~~The additive model does not account for temporal evolution~~ → *partially addressed*: the production instrument adds envelopes, stochastic modulation, and granular treatment, though these are creative layers (Table N1), not spectral data.
- Perceptual evaluation in a controlled listening study remains open — now more tractable, since the tuning library lets third parties reproduce the stimuli exactly.
- New: MPE delivery is constrained by receiver conformance (§8.3); the published taxonomy mitigates but does not eliminate this dependency.

**§6 Conclusion — added closing paragraph.**
Relative to the previous revision, the methodology has matured from a single-element demonstration into an ecosystem: a clustering stage that renders all 29 first-row-through-zinc elements playable at five resolutions; a portable tuning library that carries the scales to any tuning-capable synthesizer; a production real-time instrument that turns crowds into spectral ensembles; and an MPE delivery layer — with empirically grounded receiver guidance — that carries per-note microtonality into existing musical workflows. Throughout, we have kept the translation honest: the physics fixes the intervals and the spectral envelopes; everything else is, and is documented as, a musical choice. *Atomic spectra are translated into playable musical scales and timbral fingerprints* — and, as of this revision, into tunings and instruments that musicians can use today.

---

## 7. Suggested new references (mark as [NEW-REF] on integration)

- MIDI Manufacturers Association (2018). *MIDI Polyphonic Expression (MPE) Specification, v1.0.* — for §8.
- Op de Coul, M. *Scala scale file format* (huygens-fokker.org/scala) — for §6.
- AnaMark. *The AnaMark tuning file format (.tun) specification.* — for §6.
- (Optional) Sethares, W. A. (2005) is already cited [12] and supports §5.2's playability argument; consider also citing it from §6.1.

## 8. Integration checklist

1. Replace abstract with §0 text; extend keywords.
2. Insert §3.1 (epistemic framing); renumber v2 §3.1–3.6 → §3.2–3.7; retitle old §3.6 as "Prototype Implementation (Max/MSP)".
3. Insert new §5–§8 after the hydrogen case study; shift v2 §5 *Discussion* → §9, §6 *Conclusion* → §10.
4. Apply §5.4/§6 deltas (this draft §6).
5. Add Tables N1–N4; consider two figures: (a) scale-family construction for one dense element (e.g., iron: raw line histogram → clustered Core degrees), (b) the white-key mapping diagram for boron.
6. Add [NEW-REF] entries; cross-check that "translation" language is used consistently (search the v2 text for "transforms"/"the sound of" claims and soften where needed).
7. Update acknowledgments if desired (open-source repository + CI now exist).
