#pragma once

#include <array>

// A fixed-capacity, MIDI-only projection of an atomic/spectral scale.
//
// The one-octave degree bank is independent from the musical root.  Catalog or
// UI code can therefore load an element/mode once with setDegrees(), then use
// setRootMidiAndOctaves() when the root parameters change.  Both configuration
// and lookup are allocation-free; the const lookup API only reads fixed arrays.
// Concurrent mutation is intentionally not hidden behind a lock: configure an
// inactive copy and publish it at a processing boundary, or update this object
// from the audio thread before using it in that block.
class AtomicScaleMap
{
public:
    static constexpr int maxDegrees = 128;
    static constexpr int minOctaves = 1;
    static constexpr int maxOctaves = 6;
    // 128 microtonal degrees can all be projected through the full six-octave
    // span. The actual populated count is still truncated at MIDI's high edge.
    static constexpr int maxTableSize = maxDegrees * maxOctaves;

    enum class WavelengthUnit : int
    {
        Unknown = 0,
        Angstrom,
        Nanometer
    };

    // These defaults match the legacy AtomicScaleBuilder modes. Raw is capped
    // at 128 degrees because a MIDI pitch table cannot expose more fixed regions.
    enum class DeriveMode : int
    {
        Melodic = 0,
        Performable,
        Microtonal,
        Scientific,
        Raw
    };

    struct DegreeInput
    {
        double cents = 0.0;       // One-octave position; normalised to [0, 1200).
        double weight = 1.0;      // Display/velocity metadata; normalised to [0, 1].
        int sourceIndex = -1;     // Optional stable spectral-line/catalog index.
    };

    struct SpectralLine
    {
        double wavelength = 0.0;
        double intensity = 0.0;
        WavelengthUnit unit = WavelengthUnit::Unknown;
        int sourceIndex = -1;
    };

    struct DeriveOptions
    {
        DeriveMode mode = DeriveMode::Performable;
        int maxDegreeCount = 0;         // 0 uses the mode default.
        double minSeparationCents = -1.0; // < 0 uses the mode default.
        bool alwaysIncludeRoot = true;  // Longest valid wavelength becomes 0 cents.
    };

    struct Degree
    {
        double cents = 0.0;
        double weight = 0.0;
        double wavelengthNm = 0.0;
        double intensity = 0.0;
        int sourceIndex = -1;
    };

    // Pitch keeps the MidiPitchMap-compatible step/midiNote/frequencyHz fields
    // and adds exact microtonal information for MPE output.
    struct Pitch
    {
        int step = -1;
        int degreeIndex = -1;
        int octave = 0;
        int midiNote = -1;                // Nearest 7-bit MIDI note (Normal MIDI).
        double frequencyHz = 0.0;          // Exact atomic target frequency (MPE).
        double exactMidiNote = 0.0;
        double centsFromNearestMidi = 0.0;
        double degreeWeight = 0.0;
        int sourceIndex = -1;

        bool isValid() const noexcept
        {
            return step >= 0 && degreeIndex >= 0 && midiNote >= 0
                && frequencyHz > 0.0;
        }
    };

    AtomicScaleMap() noexcept;

    // Loads an already-selected one-octave degree set, e.g. the exact output of
    // the legacy AtomicScaleBuilder or a compact precomputed element catalog.
    // Invalid/non-finite degrees are ignored.  Input beyond maxDegrees is
    // deterministically truncated before sorting.  An invalid set installs a
    // safe 0-cent fallback and returns false.
    bool setDegrees (const DegreeInput* inputs, int count) noexcept;
    bool setCents (const double* cents, int count) noexcept;

    // Allocation-free compact derivation for raw wavelength catalogs.  It keeps
    // the legacy longest-wavelength root and salience-first/minimum-separation
    // rules.  For bit-exact legacy Medoid/WeightedMean results, precompute with
    // AtomicScaleBuilder and call setDegrees() instead.
    bool deriveFromWavelengths (const SpectralLine* lines, int count,
                                const DeriveOptions& options) noexcept;

    // Reprojects the current one-octave bank without reloading/rederiving it.
    // rootFrequencyHz <= 0 or non-finite selects standard A4=440 tuning for
    // rootMidi.  A finite custom root is clamped to the MIDI 0..127 range.
    void setRootMidiAndOctaves (int rootMidi, int octaveCount,
                                double rootFrequencyHz = 0.0) noexcept;
    void setRootPitchClassAndOctave (int rootPitchClass, int rootOctave,
                                     int octaveCount,
                                     double rootFrequencyHz = 0.0) noexcept;

    int getDegreeCount() const noexcept                  { return degreeCount; }
    Degree getDegree (int index) const noexcept;
    double getDegreeCents (int index) const noexcept;
    double getDegreeWeight (int index) const noexcept;
    double getReferenceWavelengthNm() const noexcept     { return referenceWavelengthNm; }
    bool wasInputTruncated() const noexcept              { return inputWasTruncated; }

    int getRootMidi() const noexcept                     { return configuredRootMidi; }
    double getRootFrequencyHz() const noexcept           { return configuredRootFrequencyHz; }
    int getOctaveCount() const noexcept                  { return configuredOctaves; }
    int getScaleTableSize() const noexcept               { return tableSize; }

    int getScaleMidi (int step) const noexcept;
    double getScaleFrequencyHz (int step) const noexcept;
    Pitch getPitch (int step) const noexcept;

    // Equal-sized regions, matching MidiPitchMap: x <= 0 (and NaN) selects the
    // first step; x >= 1 selects the last step.
    int xToStep (float normalisedX) const noexcept;
    int xToMidi (float normalisedX) const noexcept;
    double xToFrequencyHz (float normalisedX) const noexcept;
    Pitch xToPitch (float normalisedX) const noexcept;

    // Ties resolve toward the lower exact pitch.
    int findNearestScaleStepForMidi (int midiNote) const noexcept;
    int findNearestScaleStepForFrequency (double frequencyHz) const noexcept;

    static int rootMidiFor (int rootPitchClass, int rootOctave) noexcept;
    static double midiToFrequencyHz (double midiNote) noexcept;
    static double normalizeWavelengthNm (double wavelength,
                                         WavelengthUnit unit) noexcept;
    static double normalizedCents (double cents) noexcept;
    static double circularDistanceCents (double a, double b) noexcept;
    static int defaultMaxDegreeCount (DeriveMode mode) noexcept;
    static double defaultMinSeparationCents (DeriveMode mode) noexcept;

private:
    void installFallbackDegree() noexcept;
    void normalizeDegreeWeights() noexcept;
    void sortDegrees() noexcept;
    void rebuildPitchTable() noexcept;

    std::array<Degree, maxDegrees> degrees {};
    std::array<Pitch, maxTableSize> pitchTable {};
    int degreeCount = 0;
    int tableSize = 0;
    int configuredRootMidi = 36;
    int configuredOctaves = 4;
    double configuredRootFrequencyHz = 65.40639132514966; // MIDI note 36.
    double referenceWavelengthNm = 0.0;
    bool inputWasTruncated = false;
};
