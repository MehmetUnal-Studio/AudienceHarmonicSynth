# The Sound of Light: Translating Atomic Emission Spectra into Musical Scales, Tunings, and Timbres

**Mehmet Ünal**
Sound Technologies, Bahçeşehir University, Istanbul, Turkey
info@mehmetunal.com

*Advisor:* Barış Kıratlı

> **Draft v3 — full integrated manuscript.** Supersedes v2 ("Sonification of Atomic Emission Spectra into Musical Scales and Timbres"). Equations and tables are numbered consecutively; figures are given as placeholders `[Figure N]` with capture notes. Math is written in LaTeX notation for direct transplantation.

---

## Abstract

This paper presents a systematic methodology for sonifying atomic emission spectra — **translating** the spectral line radiation data of chemical elements **into playable musical scales, tunings, and timbral fingerprints**. Wavelengths from the NIST Atomic Spectra Database are converted to frequencies via the fundamental relation $f = c/\lambda$, yielding element-specific frequency ratios. These dimensionless ratios are mapped into the audible range to construct microtonal scales measured in cents, while the relative intensities of the same spectral lines serve as amplitude coefficients in an additive synthesis framework to generate distinctive timbres. The two outputs — *scale* and *timbre* — are conceptually and procedurally distinct: the scale encodes the *intervallic* structure inherent in an element's spectrum, whereas the timbre encodes its *spectral envelope*. Relative to the previous revision, this version extends the methodology in four directions: (i) an intensity-weighted **clustering stage** that reduces the dense line sets of heavier elements (up to several thousand observed lines) to playable *scale families* at five resolutions; (ii) an exportable **tuning library** in the AnaMark `.tun` / Scala ecosystem covering 29 elements, which decouples the scales from any single instrument; (iii) a production **real-time instrument** (*SpektraSynth*, C++/JUCE), driven by audience interaction over OSC, superseding the original Max/MSP prototype; and (iv) an **MPE-based delivery layer** that carries per-note microtonality to third-party synthesizers, together with an empirical taxonomy of receiver compatibility in commodity music software. Throughout, the epistemic status of every pipeline stage is made explicit, distinguishing physical data from creative mapping decisions. The approach establishes a reproducible, physically grounded bridge between atomic physics and musical composition, contributing to the broader field of scientific data sonification.

**Keywords:** sonification, atomic emission spectra, spectral lines, musical scales, microtonal tuning, tuning files, timbre, additive synthesis, MIDI Polyphonic Expression (MPE), audience interaction

---

## 1 Introduction

Every chemical element possesses a unique set of spectral lines — discrete wavelengths of electromagnetic radiation emitted when electrons transition between energy levels. These spectral fingerprints have long served analytical chemistry and astrophysics as identifiers of elemental composition [5, 8]. In a parallel domain, music is fundamentally structured around frequency relationships: scales define the set of pitches available to a composer, while timbre — the "color" of sound — is determined by the relative amplitudes of frequency components within a tone [3].

This study asks a direct question: *can the physical frequency relationships embedded in an element's emission spectrum be translated into a coherent musical language?* We propose a sonification pipeline that performs two distinct mappings:

1. **Scale construction:** the frequency *ratios* among an element's spectral lines define a set of musical intervals, yielding an element-specific microtonal scale.
2. **Timbre construction:** the relative *intensities* of those same spectral lines serve as amplitude weights in an additive synthesis model, producing a characteristic tone color.

It is essential to distinguish these two outputs clearly. A musical scale determines *which notes* are available; timbre determines *how each note sounds*. In conventional Western music these are independent choices — a pianist and a violinist may play the same C-major scale, but their timbres differ. Our methodology preserves this independence while grounding both outputs in the same physical data source.

The present revision matures the methodology from a single-element demonstration into a usable ecosystem. Where the original pipeline was validated on hydrogen's seven visible Balmer lines and a Max/MSP prototype, we now address the questions a working musician would ask next. *How does the method scale to elements whose visible spectra contain thousands of lines?* (Section 5: an intensity-weighted clustering stage producing five-resolution scale families.) *Can the scales be played on instruments other than ours?* (Section 6: an exportable tuning library for the AnaMark `.tun`/Scala ecosystem.) *What does a production instrument look like?* (Section 7: *SpektraSynth*, a real-time C++/JUCE instrument — in one sentence, *a spectral instrument that turns elemental emission lines into playable scales, tunings, and timbres* — whose primary input is an audience rather than a keyboard.) *And how is per-note microtonality delivered to the synthesizers musicians already own?* (Section 8: an MPE encoding layer and an empirical study of receiver behavior.)

One framing principle governs the entire paper and is formalized in Section 3.1: the methodology **translates** spectra into music; it does not claim to reveal "the real sound of atoms." Electromagnetic radiation at $\sim 10^{14}$ Hz has no acoustic correlate, and rendering its structure audible necessarily involves interpretive, musically motivated decisions. We state precisely which quantities are physical invariants and which are artistic choices, so that the translation remains both honest and auditable.

The practical application of this research was first showcased in the *Beyond the Light* installation, a collaborative effort with NASA and ARTECHOUSE, premiered at ARTECHOUSE New York in June 2023 and subsequently exhibited at ARTECHOUSE DC in September 2023. The audience-interaction model formalized in Section 7 grew directly out of that installation practice.

The remainder of this paper is organized as follows. Section 2 reviews the relevant physics, sonification literature, and music-theoretic concepts. Section 3 details the sonification pipeline, beginning with its epistemic framing. Section 4 presents results for hydrogen as the canonical case study. Section 5 scales the methodology to a 29-element library via clustering. Section 6 describes the exported tuning library. Section 7 presents the SpektraSynth instrument. Section 8 develops the MPE delivery layer and receiver-compatibility findings. Section 9 discusses implications, Section 10 states limitations, and Section 11 concludes with directions for future work.

---

## 2 Background

### 2.1 Atomic Emission Spectra

When an atom absorbs energy, one or more of its electrons are excited to higher energy states. Upon returning to lower states, each electron emits a photon whose energy equals the difference between the two levels. The relationship between the photon's energy $E$, its frequency $\nu$, and its wavelength $\lambda$ is given by:

$$E = h\nu = \frac{hc}{\lambda}, \tag{1}$$

where $h$ is Planck's constant ($6.626 \times 10^{-34}\,\mathrm{J\,s}$) and $c$ is the speed of light in vacuum ($2.998 \times 10^{8}\,\mathrm{m\,s^{-1}}$). Because each element has a unique electronic structure, its emission spectrum — the collection of all such wavelengths — acts as a spectral fingerprint [5].

`[Figure 1 — carry over from v2: visible emission spectra of five representative elements (Hg, Ne, Na, He, H).]`

#### 2.1.1 The Hydrogen Spectrum and the Rydberg Formula

Hydrogen, the simplest atom, provides the canonical example and serves as our primary case study. The Balmer series comprises the visible emission lines corresponding to electron transitions from upper levels $n > 2$ to the $n = 2$ level. The wavelengths of these lines are described by the Rydberg formula:

$$\frac{1}{\lambda} = R_\infty \left( \frac{1}{n_1^2} - \frac{1}{n_2^2} \right), \qquad n_2 > n_1, \tag{2}$$

where $R_\infty \approx 1.097 \times 10^7\,\mathrm{m^{-1}}$ is the Rydberg constant, and $n_1, n_2$ are the principal quantum numbers of the lower and upper energy levels, respectively. For the Balmer series, $n_1 = 2$ and $n_2 = 3, 4, 5, \ldots$, producing lines at approximately 656.3 nm (H$_1$, red), 486.1 nm (H$_2$, cyan), 434.0 nm (H$_3$, blue), and 410.2 nm (H$_4$, violet). The convergence of these lines toward the series limit ($n_2 \to \infty$) is a direct consequence of the $1/n^2$ spacing of energy levels — a pattern that, as we shall demonstrate, produces a characteristically compressed upper register when mapped to a musical scale.

`[Figure 2 — carry over from v2: Balmer energy-level diagram.]`

#### 2.1.2 Spectral Line Data

The National Institute of Standards and Technology (NIST) maintains the Atomic Spectra Database (ASD), which catalogs observed wavelengths and relative intensities for emission lines of all elements [8]. Each entry provides a wavelength (in vacuum or air), a relative intensity value, and the associated electronic transition. These two quantities — wavelength and relative intensity — form the primary dataset for our sonification methodology. Crucially, the Rydberg formula and analogous quantum-mechanical relations for heavier elements determine the specific wavelength values, ensuring that the resulting frequency ratios are not arbitrary but reflect the fundamental electronic structure of each atom.

### 2.2 Data Sonification

Sonification is the systematic representation of data through non-speech audio [4]. It has been applied to domains ranging from seismology [2] to astronomical data [1]. Effective sonification strategies require a principled mapping between data dimensions and perceptual audio parameters. Kramer et al. [7] identify key design principles: the mapping should be intuitive, the output should be perceptually distinguishable across data variations, and the sonification should serve either analytical or aesthetic goals (or both).

In this work, we adopt a *parameter mapping* strategy: wavelength ratios map to pitch intervals (scale), and intensity ratios map to partial amplitudes (timbre). This dual mapping is physically motivated — both data dimensions carry independent information about the element's electronic structure.

### 2.3 Musical Scales and Tuning Systems

A musical scale is an ordered set of pitches spanning an octave (a 2:1 frequency ratio). In twelve-tone equal temperament (12-TET), the octave is divided into twelve equal semitones of 100 cents each, where the cent is defined as:

$$\text{cents} = 1200 \cdot \log_2\!\left(\frac{f_1}{f_0}\right), \tag{3}$$

with $f_0$ and $f_1$ being two frequencies. Scales not conforming to 12-TET are termed *microtonal*. Many non-Western musical traditions employ microtonal intervals — for example, Turkish *makam* music uses intervals as small as approximately 22.6 cents (a Holdrian comma) [13]. The scales derived from atomic spectra are inherently microtonal, as the frequency ratios imposed by quantum mechanics bear no predetermined relationship to 12-TET intervals.

Two practical channels exist for delivering microtonal scales to commodity electronic instruments, and both are exercised in this paper. **Tuning files** (the Scala `.scl`/`.kbm` ecosystem [15] and the AnaMark `.tun` format [16]) statically retune a synthesizer's keyboard map. **MIDI Polyphonic Expression (MPE)** [14] dynamically assigns each sounding note its own MIDI channel so that per-note pitch-bend can place every note at an arbitrary frequency; this is the route taken by our real-time instrument (Section 8).

### 2.4 Timbre and Additive Synthesis

Timbre is the perceptual attribute that distinguishes two sounds of the same pitch and loudness [3, 9]. Physically, timbre is largely determined by the spectral envelope — the distribution of energy across frequency components — and by temporal characteristics such as the attack and decay profile of the sound.

`[Figure 3 — carry over from v2: stylized waveforms (tuning fork / clarinet / trumpet).]`

Additive synthesis constructs complex tones by summing sinusoidal partials, each with a specified frequency and amplitude [11]:

$$y(t) = \sum_{k=1}^{N} A_k \sin(2\pi f_k t + \phi_k), \tag{4}$$

where $A_k$, $f_k$, and $\phi_k$ are the amplitude, frequency, and phase of the $k$-th partial, respectively. When the partial frequencies $f_k$ are integer multiples of a fundamental frequency, the resulting sound is *harmonic* and perceived as having a clear pitch. When the ratios are non-integer, the sound is *inharmonic* — a characteristic of bells, metallophones, and, as we shall see, spectrally derived timbres.

It is critical to note that the partials used in our additive synthesis are *not* harmonics in the musical sense. The frequency ratios arise from quantum-mechanical transitions, producing inharmonic spectra that yield timbres with metallic, bell-like, or otherwise unconventional qualities. This is a fundamental distinction from conventional instrument modeling, where additive synthesis typically assumes harmonic partials [6].

---

## 3 Methodology

### 3.1 Epistemic Framing: Translation and Artistic Licence

Before detailing the pipeline, we make its epistemic status explicit. The mapping from emission spectrum to music is a **translation**, not a transcription: the source data are physical, but several stages necessarily involve creative, musically motivated decisions. Claims that the result is "the sound an atom really makes" would overstate the case — electromagnetic radiation at $\sim 10^{14}$ Hz has no acoustic correlate, and no canonical mapping into the audible band exists. What the methodology preserves is the **dimensionless intervallic structure** (frequency ratios) and the **relative energy distribution** (intensity ratios) of the spectrum; what it adds are the interpretive choices required to render those structures playable. Table 1 partitions the pipeline accordingly.

**Table 1.** Epistemic status of each pipeline stage.

| Stage | Physical data (invariant) | Creative / musical decision |
|---|---|---|
| Acquisition (§3.3) | NIST wavelengths $\lambda_k$, relative intensities $I_k$ | Restriction to the visible band (380–700 nm) |
| Conversion (§3.4) | $f = c/\lambda$; ratios $r_k$, $a_k$ | Choice of the longest-wavelength line as reference $f_1$ |
| Octave reduction (§3.6) | Ratio arithmetic (exact) | Folding into a single octave — a Western-octave convention |
| Clustering (§3.6) | Cents values of surviving degrees | Degree cap $D$, minimum separation $s$; root pinned at 0 cents |
| Keyboard mapping (§6) | — | White-key-first assignment of degrees |
| Timbre (§3.7) | Partial ratios and amplitudes | Envelope, filtering, effects; performance-time partial limiting |
| Delivery (§§7–8) | — | Root/register placement; MPE encoding choices |

This framing has a practical benefit beyond intellectual honesty: it identifies exactly which parameters a composer may vary without losing the physical grounding (the right-hand column), and which quantities must remain untouched for the result to remain element-specific (the left-hand column). All subsequent sections annotate their choices against this table.

### 3.2 Pipeline Overview

The sonification pipeline consists of six stages:

```
1. Acquire      NIST spectral data  →  wavelengths λ_k + intensities I_k
2. Convert      f_k = c / λ_k
3. Ratios       frequency ratios r_k  +  intensity ratios a_k
4. Reduce       octave folding + intensity-weighted clustering  →  scale families
5a. Scale       degrees in cents (microtonal pitch sets)        ┐ combined in
5b. Timbre      a_k → additive-synthesis amplitudes             ┘ the instruments
6. Deliver      real-time instrument (SpektraSynth) · tuning files (.tun/Scala) · MPE stream
```

`[Figure 4 — redraw of the v2 pipeline schematic with the new Reduce and Deliver stages.]`

Stages 1–3 and 5 are inherited from the original formulation; stage 4 (Section 3.6) is the principal methodological addition of this revision, and stage 6 is expanded from a single prototype into three delivery channels (Sections 6–8).

### 3.3 Stage 1: Data Acquisition

Spectral line data were obtained from the NIST Atomic Spectra Database [8]. For each element, we retrieved all observed emission lines within the visible spectrum (380 nm to 700 nm), recording both the wavelength $\lambda_k$ (in Å, where 1 Å = 0.1 nm) and the relative intensity $I_k$. The visible range was chosen because it provides a manageable number of lines for sonification while ensuring that the spectral data are well-characterized and experimentally reliable. (Per Table 1, this restriction is a pragmatic choice; ultraviolet and infrared lines are a natural extension.)

### 3.4 Stage 2: Wavelength-to-Frequency Conversion

Each wavelength is converted to a frequency using the fundamental relationship:

$$f_k = \frac{c}{\lambda_k}, \tag{5}$$

where $c = 2.99792458 \times 10^8\,\mathrm{m\,s^{-1}}$. Note that these are electromagnetic frequencies (on the order of $10^{14}$ Hz), not audible frequencies. The conversion to the audible range is achieved through the ratio-based mapping described in the following stages — at no point do we claim a literal acoustic identity for these frequencies.

### 3.5 Stage 3: Frequency and Intensity Ratio Extraction

**Frequency ratios.** We designate the spectral line with the lowest frequency (longest wavelength) as the fundamental reference $f_1$. The frequency ratio of each subsequent line is:

$$r_k = \frac{f_k}{f_1}, \qquad k = 1, 2, \ldots, N, \tag{6}$$

where $r_1 = 1$ by definition. These ratios are dimensionless and independent of the electromagnetic frequency magnitudes, capturing only the *intervallic structure* of the spectrum. The reference line's wavelength is recorded as the element's $\lambda_{\mathrm{ref}}$ and anchors both the scale root and the timbre fundamental.

**Intensity ratios.** Similarly, we normalize the relative intensities by the intensity of the fundamental line:

$$a_k = \frac{I_k}{I_1}, \qquad k = 1, 2, \ldots, N. \tag{7}$$

These normalized values serve as amplitude coefficients in the additive synthesis model.

### 3.6 Stage 4: Octave Reduction and Clustering into Scale Families

The frequency ratios $r_k$ define intervals that, when folded into a single octave (reduced by successive halving until $1 \le r' < 2$), form a microtonal pitch set. The interval of each candidate degree in cents is:

$$C_k = 1200 \cdot \log_2(r'_k), \tag{8}$$

where $r'_k$ is the octave-reduced ratio.

For hydrogen this pitch set is directly playable (Section 4). For heavier elements it is not: within the same visible window, the curated dataset contains 27 observed lines for helium, 333 for carbon, 1000 for neon, and **4041 for iron**. Direct application of Equation (8) to such elements produces scales with hundreds of degrees separated by fractions of a cent — physically faithful, but unplayable and perceptually undifferentiated. Translating *libraries* of elements therefore requires a principled reduction stage.

We reduce each element's octave-folded cents set with an agglomerative clustering governed by two parameters: a maximum degree count $D$ and a minimum inter-degree separation $s$ (in cents). Within each cluster, the representative degree is the **medoid** — the member minimizing total intensity-weighted distance to its cluster — so that every published scale degree corresponds to an *actually observed* emission line rather than a synthetic average. The cluster containing the reference line is pinned to 0 cents, preserving the root invariant. Greedy selection by cluster weight then caps the result at $D$ degrees.

Rather than a single compromise, we publish **five resolutions** of every element — a *scale family* (Table 2). The ends of the family serve different users: *Core* yields ≤ 7-degree scales mappable onto a conventional keyboard's white keys (Section 6), while *Raw* preserves every observed line for analytical listening.

**Table 2.** Scale-family modes. $D$ = maximum degrees, $s$ = minimum separation.

| Mode | $D$ | $s$ (cents) | Intended use |
|---|---|---|---|
| Core | 7 | 80 | Diatonic-like performance; white-key tuning export |
| Extended | 12 | 40 | Full chromatic-keyboard mapping |
| Microtonal | 24 | 20 | Quarter-tone-class resolution |
| Scientific | 48 | 10 | Dense analytical scales |
| Raw | unlimited | 0 | Unreduced spectrum |

Per Table 1, $D$ and $s$ are creative parameters; the cents values of the surviving degrees are physical. The same machinery, with intensity weights retained, also bounds the **timbre** branch: the real-time instrument caps each voice's additive bank at the strongest lines of the raw spectrum, a fidelity/efficiency trade-off under performer control (Section 7).

To render any scale playable, the octave-reduced ratios are multiplied by a chosen root frequency:

$$f_k^{\text{audible}} = r'_k \times f_{\text{root}}. \tag{9}$$

Each resulting frequency can be compared to the nearest 12-TET pitch to express the deviation in cents, providing musicians with a familiar reference point — a representation that also becomes the basis of the MPE encoding in Section 8.

### 3.7 Stage 5b: Timbre Construction via Additive Synthesis

Independently of the scale, the spectral data define a timbre. Given a desired fundamental playback frequency $f_0$ (e.g., any note the musician plays), the synthesized waveform is:

$$y(t) = \sum_{k=1}^{N} a_k \sin(2\pi \cdot r_k \cdot f_0 \cdot t), \tag{10}$$

where $r_k$ are the frequency ratios from Equation (6) and $a_k$ are the intensity ratios from Equation (7). Because the ratios $r_k$ are generally not integers, the resulting timbre is *inharmonic*, imparting a quality distinct from conventional musical instruments.

This separation is a key feature of the methodology: the *scale* tells the performer which pitches to play; the *timbre* determines how each pitch sounds. Both are derived from the same spectral data, but they encode different physical dimensions — intervallic structure and spectral envelope, respectively.

### 3.8 Stage 6: Delivery

The original pipeline terminated in a Max/MSP prototype in which the performer selects an element, plays its microtonal scale via MIDI, and hears the additive timbre in real time. That prototype validated the methodology and powered the *Beyond the Light* installation. This revision replaces the single endpoint with three production delivery channels, each described in its own section: a portable tuning library (Section 6), a real-time audience-driven instrument (Section 7), and a per-note MPE stream for third-party synthesizers (Section 8).

---

## 4 Results I: Hydrogen as a Case Study

We demonstrate the core pipeline using the seven visible emission lines of hydrogen (Balmer series plus near-ultraviolet extensions). Table 3 lists the spectral data obtained from NIST ASD.

**Table 3.** Visible emission lines of hydrogen (Balmer series) from NIST ASD.

| Line # | Wavelength (Å) | Rel. Intensity | Transition ($n_2 \to n_1$) |
|---|---|---|---|
| 1 | 6562.79 | 6500 | $3 \to 2$ |
| 2 | 4861.35 | 1500 | $4 \to 2$ |
| 3 | 4340.47 | 1000 | $5 \to 2$ |
| 4 | 4101.73 | 675 | $6 \to 2$ |
| 5 | 3970.08 | 255 | $7 \to 2$ |
| 6 | 3889.06 | 195 | $8 \to 2$ |
| 7 | 3835.40 | 135 | $9 \to 2$ |

`[Figure 6 — carry over from v2: hydrogen line positions and intensities.]`

### 4.1 Frequency Ratios

Applying Equations (5) and (6), we obtain the frequency ratios in Table 4. These ratios are purely determined by quantum mechanics — specifically, by the energy level spacings of the hydrogen atom.

**Table 4.** Frequency ratios and electromagnetic frequencies for hydrogen's visible lines.

| Line # | Wavelength (Å) | Frequency (Hz) | Freq. Ratio $r_k$ |
|---|---|---|---|
| 1 | 6562.79 | $4.568 \times 10^{14}$ | 1.0000 |
| 2 | 4861.35 | $6.167 \times 10^{14}$ | 1.3500 |
| 3 | 4340.47 | $6.907 \times 10^{14}$ | 1.5120 |
| 4 | 4101.73 | $7.309 \times 10^{14}$ | 1.6000 |
| 5 | 3970.08 | $7.551 \times 10^{14}$ | 1.6531 |
| 6 | 3889.06 | $7.709 \times 10^{14}$ | 1.6875 |
| 7 | 3835.40 | $7.816 \times 10^{14}$ | 1.7111 |

### 4.2 Scale Construction

Since all ratios already satisfy $1 \le r_k < 2$, no octave folding is needed, and with only seven lines the clustering stage passes the set through unchanged — hydrogen's Core and Raw scales coincide. Applying Equation (8) and mapping to a root of $C_4 = 261.63$ Hz (middle C), we obtain the "Hydrogen Scale" shown in Table 5.

**Table 5.** The "Hydrogen Scale": microtonal pitches derived from hydrogen's Balmer series.

| Freq. (Hz) | Cents | Nearest 12-TET Note | Deviation |
|---|---|---|---|
| 261.63 | 0 | C4 | ±0 ct |
| 353.19 | 519.5 | F4 | +20 ct |
| 395.58 | 715.7 | G4 | +16 ct |
| 418.60 | 813.7 | G♯4 | +14 ct |
| 432.48 | 870.2 | A4 | −30 ct |
| 441.49 | 905.9 | A4 | +6 ct |
| 447.67 | 929.9 | A4 | +30 ct |
| 523.25 | 1200 | C5 | ±0 ct |

The resulting scale is notably compressed in its upper register, with five of the seven pitches falling between approximately 700 and 930 cents. This clustering reflects the convergence of the Balmer series toward the series limit — a direct audible consequence of hydrogen's quantum structure.

### 4.3 Timbre Construction

Using the intensity ratios from Equation (7):

**Table 6.** Intensity ratios for hydrogen, used as additive synthesis amplitudes.

| Line # | Rel. Intensity | Amplitude $a_k$ |
|---|---|---|
| 1 | 6500 | 1.0000 |
| 2 | 1500 | 0.2308 |
| 3 | 1000 | 0.1538 |
| 4 | 675 | 0.1038 |
| 5 | 255 | 0.0392 |
| 6 | 195 | 0.0300 |
| 7 | 135 | 0.0208 |

The timbre is dominated by the fundamental (H$_1$ line), with rapidly decaying upper partials. Because the partial frequencies are inharmonic (ratios such as 1.35 and 1.512 are not integers), the resulting sound exhibits a metallic, bell-like quality absent in conventional harmonic timbres.

`[Figure 7 — carry over from v2: rendered hydrogen spectral profile vs. its visible emission spectrum.]`

---

## 5 Results II: Scaling to a 29-Element Library

### 5.1 The Density Problem

Hydrogen is a benign case. Table 7 samples the raw visible-line counts across the curated dataset, which covers the first thirty elements (hydrogen through zinc; nitrogen is presently omitted pending a curated line list of matching quality — 29 elements in total).

**Table 7.** Raw visible-line counts (selected elements) and Core-mode reduction.

| Element | Raw lines | Core degrees | $\lambda_{\mathrm{ref}}$ (nm) |
|---|---|---|---|
| H | 6 | 5 | 656.279 |
| He | 27 | 7 | 667.815 |
| Li | 15 | 6 | 670.791 |
| B | 74 | 7 | 678.612 |
| C | 333 | 7 | 696.231 |
| Ne | 1000 | 7 | 699.300 |
| Ti | 1896 | 7 | 699.893 |
| V | 2234 | 7 | 699.240 |
| Fe | 4041 | 7 | 699.988 |
| Zn | 57 | 7 | 694.320 |

*(The full 29-row table is published machine-readably with the tuning library manifest; see Section 6.)*

A scale of 4041 degrees is not a musical resource. The clustering stage of Section 3.6 reduces every element to the five resolutions of Table 2 while guaranteeing that each surviving degree is an observed emission line. Applying the family construction across the library yields $29 \times 5 = 145$ scales plus 29 timbral fingerprints.

### 5.2 A Dense-Element Example: Boron

Boron's 74 visible lines reduce, in Core mode, to seven degrees anchored at $\lambda_{\mathrm{ref}} = 678.612$ nm:

**Table 8.** Boron Core scale (cents from root).

| Degree | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| Cents | 0.0 | 177.4 | 322.3 | 472.3 | 605.2 | 721.7 | 863.1 |

Against 12-TET, the degrees read approximately: ±0, −23, −78, −28, +5, −178, −237 cents relative to their nearest named notes — an immediately audible departure from equal temperament whose internal logic is nonetheless consistent across every octave. Different elements yield audibly distinct interval characters: hydrogen's series-limit compression (Section 4.2), boron's quasi-diatonic spread, iron's dense quasi-continuum (in the wider family modes).

### 5.3 Reproducibility

The full dataset is generated from the curated NIST-derived line lists by a deterministic script; a continuous-integration check regenerates the dataset on every revision and fails on any drift between the source data and the published tables. Every scale, tuning file, and timbre in this paper is therefore mechanically reproducible from the public repository, and the instrument's MIDI output (Section 8) is locked by an automated byte-level test suite. We regard this engineering posture — *the paper's claims as executable checks* — as part of the methodology.

---

## 6 The Atomic Tuning Library: Exporting Scales as Tunings

A scale that lives only inside one bespoke instrument is a demonstration; a scale that any synthesizer can load is a *musical resource*. We therefore export the Core family as a library of tuning files — **Atomic CORE WhiteKeys** — in the AnaMark `.tun` format [16] consumed by widely deployed synthesizers (e.g., Spectrasonics Omnisphere), with the Scala (`.scl`/`.kbm`) ecosystem [15] as the companion interchange route. The library covers all 29 elements, one file per element, accompanied by a machine-readable manifest recording each element's retained degree count, the raw line count it was reduced from, $\lambda_{\mathrm{ref}}$, and the exact cents mapping.

### 6.1 White-Key-First Mapping

The Core mode's ≤ 7 degrees map onto the conventional keyboard as follows:

1. Degrees 1–7 are assigned, in ascending cents order, to the white keys **C D E F G A B** of every octave; the root (0 cents, the $\lambda_{\mathrm{ref}}$ line) is always C.
2. If an element retains more than seven degrees in a given export, degree 8 and beyond occupy the **nearest free black key**.
3. Keys with no assigned degree **duplicate the nearest assigned atomic degree**, so no key is silent and the playable surface remains continuous.

This mapping is, per Table 1, a creative convention — it deliberately gives a pianist's muscle memory a foothold in each element's intervallic world: a C-major hand shape on the boron tuning *is* a boron cluster chord. The boron file realizes Table 8 directly on the white keys.

`[Figure 8 — new: white-key mapping diagram for boron (keyboard graphic with cents annotations).]`

The tuning library decouples the *scale* half of the methodology from our own instrument entirely: composers can perform atomic scales on any tuning-capable synthesizer, while the *timbre* half remains available through the instrument described next.

---

## 7 SpektraSynth: A Real-Time Spectral Instrument

The Max/MSP prototype validated the pipeline; deploying it for installation and ensemble use motivated a production rewrite. **SpektraSynth** is a C++17/JUCE instrument (VST3 and standalone) realizing the complete translation chain in real time — *a spectral instrument that turns elemental emission lines into playable scales, tunings, and timbres.*

**Synthesis.** Each voice renders an element's timbral fingerprint with a phase-accurate additive bank of up to 512 partials drawn from the raw spectrum (lookup-table oscillators with linear interpolation), shaped by per-voice low-pass filtering, slow stochastic pitch/amplitude modulation, and a master effects chain (reverb, cross-delay, tape-style saturation, limiter). A performance control continuously limits the audible partial count, trading spectral fidelity against density — per Table 1, a creative layer atop the physical amplitudes — and a *solo* mode auditions individual emission lines for analytical listening. A complementary sample/granular engine allows recorded material to be quantized to the same atomic scales, so spectral and concrete sources share one intonation system. The 145-scale library of Section 5 is compiled in and selectable per element and mode. Four macro controls (energy, motion, tone, space) expose the creative layer to performers as continuous gestures.

**Audience as ensemble.** The instrument's primary input is not a keyboard but a crowd. A venue is modeled as a seat grid of 26 rows × 100 columns; audience phones transmit per-seat control over UDP/OSC (`/cs/<row>/<col>/finger<n>/<param>`), where the horizontal axis is quantized to the active atomic scale and the vertical axis drives dynamics. Up to 1024 simultaneous voices with adaptive unison realize the *multi-element polyphonic textures* anticipated in the previous revision's future work — in the form of crowd-driven polyphony within an element, and multi-element counterpoint across multiple instances. This formalizes the interaction model first explored in *Beyond the Light* into a reproducible, openly testable instrument, with an automated test suite and continuous integration.

`[Figure 9 — new: SpektraSynth editor screenshot (Element mode, Performance view) replacing/complementing v2's Figure 5.]`

---

## 8 Delivering Microtonality: MPE Encoding and Receiver Compatibility

The tuning library (Section 6) serves synthesizers that load tuning files; a larger population of instruments does not, but does implement MIDI Polyphonic Expression [14]. SpektraSynth therefore also delivers its scales as a real-time MPE stream, encoding each sounding voice as an independently bent note. This section documents the encoding and — equally important for practitioners — what existing receivers actually do with it.

### 8.1 Encoding

Each scale degree's frequency is expressed as the nearest 12-TET note plus a per-note pitch-bend offset. Because every degree lies within ±50 cents of some 12-TET pitch by construction, the offset never exceeds a quarter tone — which motivates transmitting at the **±2-semitone bend range**, the de facto universal default. The choice is deliberately defensive: a receiver that honors the transmitted bend-range RPN agrees exactly, and a receiver that *ignores* the RPN but defaults to ±2 st still reproduces the microtones correctly; wider ranges (±12/±24/±48 st, available for glide effects) would scale such mis-readings by up to 24×. Per note-on, the emitter sends the bend *before* the note (pitch-bend → timbre CC74 → expression CC11 → note-on → channel pressure), per the MPE convention that receivers latch per-note pitch at note-on. Both MPE zones are supported (lower: master channel 1, members 2–16; upper: master 16, members 1–15), with the zone's configuration message emitted on activation and per-member bend-range RPNs alongside. The emitted byte stream — message ordering, zone setup, channel allocation, and voice stealing — is locked by the automated test suite at byte level, so the claims in this section are mechanically verified properties of the implementation rather than intentions.

### 8.2 Channel Allocation Under Reuse

MPE multiplexes polyphony over at most 15 member channels, so channels are continuously recycled. We observed that a *naive* allocator (lowest free channel) immediately reuses the channel just vacated by a released note; some receivers then mis-associate the fresh note's bend with residual per-channel state, audibly collapsing the new note to 12-TET — precisely the failure a musician would report as "the microtones stopped working." Replacing the allocator with a **round-robin** policy, under which a freed channel is the *last* to be reused, gives every receiver maximal settling time per channel and removed the failure in our tests without altering the byte-level protocol. We propose freed-channel-last allocation as a general robustness practice for microtonal MPE emitters.

### 8.3 Receiver Compatibility: An Empirical Taxonomy

Spectral scales are an unusually sensitive probe of MPE conformance: a receiver that mishandles *any* per-note pitch state converts microtones into audible 12-TET errors. Field testing against commodity software (2025–2026 versions) yields three receiver classes (Table 9).

**Table 9.** Observed receiver behavior for per-note microtonal MPE.

| Class | Behavior | Consequence for atomic scales |
|---|---|---|
| Full MPE | Per-note bend and pressure honored per member channel | Scales reproduce exactly |
| Partial | Notes follow per-channel bend, but channel pressure (and some controllers) applied globally | Pitches correct; dynamics of held notes track the newest note |
| Collapsed | Multichannel stream merged before the synthesis engine; the last-arriving bend governs all notes | Polyphonic microtonality destroyed (every chord re-tempers itself) |

Two findings deserve emphasis. First, classification is **not a fixed property of the receiver plugin**: one widely used MPE synthesizer behaved as *full MPE* standalone, yet *collapsed* when the identical stream was routed to it as a hosted plugin through a DAW's track MIDI input — demonstrating that the host's internal MIDI routing, not the instrument, discarded the per-note channel association. Second, the practical workarounds are systematic: (i) host the receiver standalone or in an MPE-native host; or (ii) split the member channels across multiple single-channel receiver instances, which converts per-note bend into ordinary per-channel bend and proved robust in every configuration we tested. We document these findings because deployment knowledge of this kind is, in our experience, the actual barrier between a published sonification methodology and its adoption by working musicians.

---

## 9 Discussion

### 9.1 Scale versus Timbre: A Critical Distinction

A central contribution of this work is the explicit separation of scale and timbre as independent sonification outputs. In earlier formulations, the distinction was not clearly articulated, leading to potential confusion between the two processes. We emphasize:

- **Scale** is a *pitch-domain* construct: it determines which discrete frequencies are available for melodic and harmonic composition. It is derived from frequency *ratios*.
- **Timbre** is a *spectral-envelope* construct: it determines the tonal color of each sounded pitch. It is derived from intensity *ratios* applied in additive synthesis.

An analogy may clarify: changing the scale is like choosing a different alphabet; changing the timbre is like choosing a different typeface. Both convey information, but they operate on different perceptual dimensions. The delivery channels of this revision preserve the separation end-to-end: the tuning library carries scales without timbres; the MPE stream carries scales into arbitrary third-party timbres; SpektraSynth combines both.

### 9.2 Inharmonicity and Perceptual Implications

The inharmonic nature of spectrally derived timbres has significant perceptual consequences. Research in psychoacoustics has shown that inharmonic partials weaken pitch salience and can produce sensations of "roughness" or "shimmer" [9, 12]. The degree of inharmonicity varies across elements: hydrogen's Balmer series produces relatively gentle inharmonicity (ratios near simple fractions like $4/3 \approx 1.333$ and $3/2 = 1.5$), while heavier elements with more complex spectra yield more dissonant timbres. The scale-family construction (Section 5) extends this observation to the pitch domain: the same element supports both a tame seven-degree Core scale and an aggressively microtonal Scientific scale, making the fidelity/playability trade-off a compositional parameter rather than a fixed property.

### 9.3 Relationship to Existing Sonification Work

Prior sonification projects have mapped astronomical or physical data to sound [1, 2], but typically employ *ad hoc* mappings (e.g., brightness to pitch). Our approach is distinctive in that the mapping is *structurally direct*: light-frequency **ratios** become sound-frequency ratios via a dimensionless transformation, preserving the mathematical structure of the source data. This is closer in spirit to the concept of *audification* — direct translation of a data waveform to audio — than to arbitrary parameter mapping [4]. At the same time, Section 3.1 deliberately stops short of audification's literalism: the octave reduction, clustering, and keyboard mapping are interpretive layers, and we label them as such. We suggest that this explicit two-column accounting (physical invariants vs. creative choices) is itself a useful pattern for sonification research, where the epistemic status of mappings is often left implicit.

### 9.4 From Methodology to Ecosystem

The previous revision closed by listing future work: extending to more elements, incorporating lines beyond the visible range, perceptual evaluation, temporal dynamics, and multi-element polyphonic textures. The present revision discharges the first and last of these (29-element library; crowd-driven polyphony and multi-instance counterpoint) and partially addresses temporal dynamics (envelopes, stochastic modulation, and granular treatment in SpektraSynth — creative layers per Table 1). It also surfaces a class of contributions the original framing did not anticipate: *deployment engineering* — tuning-file ecosystems, MPE conformance, host routing behavior — as a first-class research output. A sonification methodology that musicians cannot route through their existing tools remains a laboratory artifact; Sections 6 and 8 are our answer to that gap.

---

## 10 Limitations

Several limitations should be noted:

- The restriction to the visible spectrum excludes ultraviolet and infrared lines, which could provide additional partials and scale degrees.
- Relative intensity values in the NIST database depend on experimental conditions and may not perfectly represent intrinsic transition probabilities; clustering weights inherit this uncertainty.
- Temporal evolution is now addressed only by *creative* layers (envelopes, modulation, granular treatment); the spectral data themselves contain no temporal information, and transition-probability-informed envelope shaping remains unexplored.
- Perceptual evaluation of the resulting scales and timbres has not yet been conducted in a controlled listening study. The tuning library lowers the barrier: third parties can now reproduce the stimuli exactly.
- MPE delivery is constrained by receiver conformance (Section 8.3); the published taxonomy and workarounds mitigate but do not eliminate this dependency.
- Nitrogen is absent from the current library pending a curated line list of matching quality.

---

## 11 Conclusion

We have presented a reproducible methodology for **translating** atomic emission spectra into two independent musical constructs — microtonal scales and additive-synthesis timbres — and, in this revision, for delivering them to working musicians. By grounding both mappings in the physical properties of spectral lines (frequency ratios for scales, intensity ratios for timbres) and by accounting explicitly for every creative decision layered on top, the approach maintains a direct and transparent connection between the scientific data and the musical output. The hydrogen case study demonstrates that even the simplest atom yields a musically distinct and physically grounded result; the clustering stage extends the same guarantee to elements whose visible spectra contain thousands of lines.

Relative to the previous revision, the methodology has matured from a single-element demonstration into an ecosystem: a 29-element, five-resolution scale library; a portable tuning library that carries the scales to any tuning-capable synthesizer; a production real-time instrument that turns crowds into spectral ensembles; and an MPE delivery layer — with empirically grounded receiver guidance — that carries per-note microtonality into existing musical workflows. Throughout, we have kept the translation honest: the physics fixes the intervals and the spectral envelopes; everything else is, and is documented as, a musical choice. *Atomic spectra are translated into playable musical scales and timbral fingerprints* — and, as of this revision, into tunings and instruments that musicians can use today.

Future work will incorporate spectral lines beyond the visible range, conduct controlled perceptual studies using the now-reproducible stimuli, explore transition-probability-informed temporal shaping, and extend the library beyond zinc — including the currently omitted nitrogen.

---

## Acknowledgments

Special thanks to Riccardo Sellan, Çağatay Güçlü, Assoc. Prof. Dr. Berkay Camgöz, Prof. Dr. Şenol Sert, and ARTECHOUSE for their steadfast support. This work was showcased in the *Beyond the Light* installation produced by ARTECHOUSE Studio in collaboration with NASA.

## References

[1] Diaz-Merced, W. L., Candey, R. M., Brickhouse, N., Schneps, M., Mannone, J. C., Brewster, S., & Kolenberg, K. (2011). Sonification of astronomical data. In *Proceedings of the International Astronomical Union*, 7(S285), 133–136.

[2] Dombois, F. (2001). Using audification in planetary seismology. In *Proceedings of the 7th International Conference on Auditory Display (ICAD)*, 227–230.

[3] Grey, J. M. (1977). Multidimensional perceptual scaling of musical timbres. *The Journal of the Acoustical Society of America*, 61(5), 1270–1277.

[4] Hermann, T., Hunt, A., & Neuhoff, J. G. (Eds.). (2011). *The Sonification Handbook*. Logos Verlag Berlin.

[5] Herzberg, G. (1944). *Atomic Spectra and Atomic Structure*. Dover Publications.

[6] Jaffe, D. A., & Smith, J. O. (1983). Extensions of the Karplus-Strong plucked-string algorithm. *Computer Music Journal*, 7(2), 56–69.

[7] Kramer, G., Walker, B., Bonebright, T., Cook, P., Flowers, J. H., Miner, N., & Neuhoff, J. (1999). Sonification report: Status of the field and research agenda. *Faculty Publications, Department of Psychology*, 444.

[8] Kramida, A., Ralchenko, Yu., Reader, J., & NIST ASD Team. (2022). *NIST Atomic Spectra Database* (ver. 5.10). National Institute of Standards and Technology.

[9] McAdams, S., & Bigand, E. (1993). *Thinking in Sound: The Cognitive Psychology of Human Audition*. Clarendon Press.

[10] Puckette, M. (2002). Max at seventeen. *Computer Music Journal*, 26(4), 31–43.

[11] Roads, C. (1996). *The Computer Music Tutorial*. MIT Press.

[12] Sethares, W. A. (2005). *Tuning, Timbre, Spectrum, Scale* (2nd ed.). Springer.

[13] Signell, K. L. (1977). *Makam: Modal Practice in Turkish Art Music*. Asian Music Publications.

[14] MIDI Manufacturers Association (2018). *MIDI Polyphonic Expression (MPE) Specification*, version 1.0.

[15] Op de Coul, M. *Scala scale file format*. Huygens-Fokker Foundation, huygens-fokker.org/scala.

[16] Schäfer, M. *The AnaMark tuning file format (.tun) specification*.

---

> **Editor's notes for integration (not part of the manuscript):**
> - Figures 1–3, 6, 7 carry over from v2 unchanged; Figure 4 needs a redraw (6-stage pipeline); Figure 5 (Max/MSP UI) may be kept in §3.8 as the historical prototype or dropped; Figures 8 (boron keyboard diagram) and 9 (SpektraSynth screenshot) are new.
> - v2 §3.6 (Max/MSP implementation) is condensed into §3.8 here; cite [10] there if the Max/MSP description is retained at length.
> - Search-and-soften pass: the v2 phrase "physically direct" is retained but qualified as "structurally direct" (§9.3) per the translation framing.
> - Repository/CI/tuning-library URLs to be added at the author's discretion (Section 5.3 and 6 reference them generically).
