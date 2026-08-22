#include "AtomicScaleMap.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    constexpr double octaveCents = 1200.0;
    constexpr double minMidiFrequency = 8.175798915643707;    // MIDI note 0.
    constexpr double maxMidiFrequency = 12543.853951415975;   // MIDI note 127.

    bool isValidPositive (double value) noexcept
    {
        return std::isfinite(value) && value > 0.0;
    }

    bool orderedEqual (double a, double b) noexcept
    {
        return ! std::isless(a, b) && ! std::isless(b, a);
    }

    int clampMode (AtomicScaleMap::DeriveMode mode) noexcept
    {
        return std::clamp((int) mode,
                          (int) AtomicScaleMap::DeriveMode::Melodic,
                          (int) AtomicScaleMap::DeriveMode::Raw);
    }
}

AtomicScaleMap::AtomicScaleMap() noexcept
{
    installFallbackDegree();
    rebuildPitchTable();
}

void AtomicScaleMap::installFallbackDegree() noexcept
{
    degrees.fill({});
    degreeCount = 1;
    degrees[0].cents = 0.0;
    degrees[0].weight = 1.0;
    degrees[0].sourceIndex = -1;
}

bool AtomicScaleMap::setDegrees (const DegreeInput* inputs, int count) noexcept
{
    degrees.fill({});
    degreeCount = 0;
    referenceWavelengthNm = 0.0;
    inputWasTruncated = false;

    if (inputs != nullptr && count > 0)
    {
        for (int i = 0; i < count; ++i)
        {
            const auto& input = inputs[(size_t) i];
            if (! std::isfinite(input.cents))
                continue;

            if (degreeCount >= maxDegrees)
            {
                inputWasTruncated = true;
                break;
            }

            auto& degree = degrees[(size_t) degreeCount++];
            degree.cents = normalizedCents(input.cents);
            degree.weight = isValidPositive(input.weight) ? input.weight : 0.0;
            degree.intensity = degree.weight;
            degree.sourceIndex = input.sourceIndex >= 0 ? input.sourceIndex : i;
        }
    }

    const bool acceptedInput = degreeCount > 0;
    if (! acceptedInput)
        installFallbackDegree();
    else
    {
        normalizeDegreeWeights();
        sortDegrees();
    }

    rebuildPitchTable();
    return acceptedInput;
}

bool AtomicScaleMap::setCents (const double* cents, int count) noexcept
{
    degrees.fill({});
    degreeCount = 0;
    referenceWavelengthNm = 0.0;
    inputWasTruncated = false;

    if (cents != nullptr && count > 0)
    {
        for (int i = 0; i < count; ++i)
        {
            if (! std::isfinite(cents[(size_t) i]))
                continue;

            if (degreeCount >= maxDegrees)
            {
                inputWasTruncated = true;
                break;
            }

            auto& degree = degrees[(size_t) degreeCount++];
            degree.cents = normalizedCents(cents[(size_t) i]);
            degree.weight = 1.0;
            degree.intensity = 1.0;
            degree.sourceIndex = i;
        }
    }

    const bool acceptedInput = degreeCount > 0;
    if (! acceptedInput)
        installFallbackDegree();
    else
        sortDegrees();

    rebuildPitchTable();
    return acceptedInput;
}

bool AtomicScaleMap::deriveFromWavelengths (const SpectralLine* lines, int count,
                                            const DeriveOptions& options) noexcept
{
    degrees.fill({});
    degreeCount = 0;
    referenceWavelengthNm = 0.0;
    inputWasTruncated = false;

    const auto mode = (DeriveMode) clampMode(options.mode);
    const int requestedDegrees = options.maxDegreeCount > 0
                               ? options.maxDegreeCount
                               : defaultMaxDegreeCount(mode);
    const int degreeLimit = std::clamp(requestedDegrees, 1, maxDegrees);
    const double requestedSeparation = options.minSeparationCents >= 0.0
                                     && std::isfinite(options.minSeparationCents)
                                     ? options.minSeparationCents
                                     : defaultMinSeparationCents(mode);
    const double minSeparation = std::clamp(requestedSeparation, 0.0, 600.0);

    int rootInputIndex = -1;
    int validSourceCount = 0;
    double rootIntensity = 0.0;
    if (lines != nullptr && count > 0)
    {
        for (int i = 0; i < count; ++i)
        {
            const auto& line = lines[(size_t) i];
            const double nm = normalizeWavelengthNm(line.wavelength, line.unit);
            if (! isValidPositive(nm) || ! isValidPositive(line.intensity))
                continue;

            ++validSourceCount;

            if (nm > referenceWavelengthNm)
            {
                referenceWavelengthNm = nm;
                rootInputIndex = i;
                rootIntensity = line.intensity;
            }
        }
    }

    if (rootInputIndex < 0 || ! isValidPositive(referenceWavelengthNm))
    {
        installFallbackDegree();
        rebuildPitchTable();
        return false;
    }

    if (options.alwaysIncludeRoot)
    {
        const auto& rootLine = lines[(size_t) rootInputIndex];
        auto& root = degrees[(size_t) degreeCount++];
        root.cents = 0.0;
        root.weight = rootIntensity;
        root.wavelengthNm = referenceWavelengthNm;
        root.intensity = rootIntensity;
        root.sourceIndex = rootLine.sourceIndex >= 0 ? rootLine.sourceIndex : rootInputIndex;
    }

    auto inputAlreadySelected = [this] (int sourceIndex) noexcept
    {
        for (int i = 0; i < degreeCount; ++i)
            if (degrees[(size_t) i].sourceIndex == sourceIndex)
                return true;
        return false;
    };

    // Repeated best-candidate scans are equivalent to sorting all valid source
    // lines by the legacy salience order and greedily applying the separation
    // constraint, while avoiding a heap-sized copy of catalogs with 4000+ rows.
    while (degreeCount < degreeLimit)
    {
        int bestInputIndex = -1;
        int bestSourceIndex = -1;
        double bestNm = 0.0;
        double bestIntensity = 0.0;
        double bestCents = 0.0;

        for (int i = 0; i < count; ++i)
        {
            if (i == rootInputIndex)
                continue;

            const auto& line = lines[(size_t) i];
            const double nm = normalizeWavelengthNm(line.wavelength, line.unit);
            if (! isValidPositive(nm) || ! isValidPositive(line.intensity))
                continue;

            const int sourceIndex = line.sourceIndex >= 0 ? line.sourceIndex : i;
            if (inputAlreadySelected(sourceIndex))
                continue;

            const double cents = normalizedCents(octaveCents
                                                  * std::log2(referenceWavelengthNm / nm));
            bool separated = true;
            for (int selected = 0; selected < degreeCount; ++selected)
            {
                if (circularDistanceCents(cents, degrees[(size_t) selected].cents)
                    < minSeparation)
                {
                    separated = false;
                    break;
                }
            }
            if (! separated)
                continue;

            const bool sameIntensity = orderedEqual(line.intensity, bestIntensity);
            const bool sameCents = orderedEqual(cents, bestCents);
            const bool sameWavelength = orderedEqual(nm, bestNm);
            const bool better = bestInputIndex < 0
                || line.intensity > bestIntensity
                || (sameIntensity && cents < bestCents)
                || (sameIntensity && sameCents && nm > bestNm)
                || (sameIntensity && sameCents && sameWavelength
                    && sourceIndex < bestSourceIndex);

            if (better)
            {
                bestInputIndex = i;
                bestSourceIndex = sourceIndex;
                bestNm = nm;
                bestIntensity = line.intensity;
                bestCents = cents;
            }
        }

        if (bestInputIndex < 0)
            break;

        auto& degree = degrees[(size_t) degreeCount++];
        degree.cents = bestCents;
        degree.weight = bestIntensity;
        degree.wavelengthNm = bestNm;
        degree.intensity = bestIntensity;
        degree.sourceIndex = bestSourceIndex;
    }

    const bool acceptedInput = degreeCount > 0;
    if (! acceptedInput)
        installFallbackDegree();
    else
    {
        normalizeDegreeWeights();
        sortDegrees();
    }

    if (mode == DeriveMode::Raw && validSourceCount > maxDegrees)
        inputWasTruncated = true;

    rebuildPitchTable();
    return acceptedInput;
}

void AtomicScaleMap::setRootMidiAndOctaves (int rootMidi, int octaveCount,
                                            double rootFrequencyHz) noexcept
{
    configuredRootMidi = std::clamp(rootMidi, 0, 127);
    configuredOctaves = std::clamp(octaveCount, minOctaves, maxOctaves);

    if (isValidPositive(rootFrequencyHz))
        configuredRootFrequencyHz = std::clamp(rootFrequencyHz,
                                               minMidiFrequency,
                                               maxMidiFrequency);
    else
        configuredRootFrequencyHz = midiToFrequencyHz((double) configuredRootMidi);

    rebuildPitchTable();
}

void AtomicScaleMap::setRootPitchClassAndOctave (int rootPitchClass, int rootOctave,
                                                 int octaveCount,
                                                 double rootFrequencyHz) noexcept
{
    setRootMidiAndOctaves(rootMidiFor(rootPitchClass, rootOctave),
                          octaveCount, rootFrequencyHz);
}

AtomicScaleMap::Degree AtomicScaleMap::getDegree (int index) const noexcept
{
    return index >= 0 && index < degreeCount ? degrees[(size_t) index] : Degree {};
}

double AtomicScaleMap::getDegreeCents (int index) const noexcept
{
    return index >= 0 && index < degreeCount ? degrees[(size_t) index].cents : 0.0;
}

double AtomicScaleMap::getDegreeWeight (int index) const noexcept
{
    return index >= 0 && index < degreeCount ? degrees[(size_t) index].weight : 0.0;
}

int AtomicScaleMap::getScaleMidi (int step) const noexcept
{
    return step >= 0 && step < tableSize ? pitchTable[(size_t) step].midiNote : -1;
}

double AtomicScaleMap::getScaleFrequencyHz (int step) const noexcept
{
    return step >= 0 && step < tableSize ? pitchTable[(size_t) step].frequencyHz : 0.0;
}

AtomicScaleMap::Pitch AtomicScaleMap::getPitch (int step) const noexcept
{
    return step >= 0 && step < tableSize ? pitchTable[(size_t) step] : Pitch {};
}

int AtomicScaleMap::xToStep (float normalisedX) const noexcept
{
    if (tableSize <= 0)
        return -1;

    if (std::isnan(normalisedX) || normalisedX <= 0.0f)
        return 0;

    if (normalisedX >= 1.0f)
        return tableSize - 1;

    return std::min(tableSize - 1,
                    (int) (normalisedX * (float) tableSize));
}

int AtomicScaleMap::xToMidi (float normalisedX) const noexcept
{
    return getScaleMidi(xToStep(normalisedX));
}

double AtomicScaleMap::xToFrequencyHz (float normalisedX) const noexcept
{
    return getScaleFrequencyHz(xToStep(normalisedX));
}

AtomicScaleMap::Pitch AtomicScaleMap::xToPitch (float normalisedX) const noexcept
{
    return getPitch(xToStep(normalisedX));
}

int AtomicScaleMap::findNearestScaleStepForMidi (int midiNote) const noexcept
{
    if (tableSize <= 0)
        return -1;

    const double target = (double) std::clamp(midiNote, 0, 127);
    int best = 0;
    double bestDistance = std::abs(pitchTable[0].exactMidiNote - target);

    for (int i = 1; i < tableSize; ++i)
    {
        const double distance = std::abs(pitchTable[(size_t) i].exactMidiNote - target);
        if (distance < bestDistance)
        {
            best = i;
            bestDistance = distance;
        }
    }

    return best;
}

int AtomicScaleMap::findNearestScaleStepForFrequency (double frequencyHz) const noexcept
{
    if (tableSize <= 0 || ! isValidPositive(frequencyHz))
        return -1;

    const double targetMidi = 69.0 + 12.0 * std::log2(frequencyHz / 440.0);
    int best = 0;
    double bestDistance = std::abs(pitchTable[0].exactMidiNote - targetMidi);

    for (int i = 1; i < tableSize; ++i)
    {
        const double distance = std::abs(pitchTable[(size_t) i].exactMidiNote - targetMidi);
        if (distance < bestDistance)
        {
            best = i;
            bestDistance = distance;
        }
    }

    return best;
}

int AtomicScaleMap::rootMidiFor (int rootPitchClass, int rootOctave) noexcept
{
    const long long pitchClass = (long long) std::clamp(rootPitchClass, 0, 11);
    const long long midi = ((long long) rootOctave + 1LL) * 12LL + pitchClass;
    return (int) std::clamp(midi, 0LL, 127LL);
}

double AtomicScaleMap::midiToFrequencyHz (double midiNote) noexcept
{
    if (! std::isfinite(midiNote))
        midiNote = 69.0;
    return 440.0 * std::exp2((std::clamp(midiNote, 0.0, 127.0) - 69.0) / 12.0);
}

double AtomicScaleMap::normalizeWavelengthNm (double wavelength,
                                              WavelengthUnit unit) noexcept
{
    if (! std::isfinite(wavelength) || wavelength <= 0.0)
        return 0.0;

    if (unit == WavelengthUnit::Angstrom)
        return wavelength * 0.1;
    if (unit == WavelengthUnit::Nanometer)
        return wavelength;

    return wavelength > 1000.0 ? wavelength * 0.1 : wavelength;
}

double AtomicScaleMap::normalizedCents (double cents) noexcept
{
    if (! std::isfinite(cents))
        return 0.0;

    double result = std::fmod(cents, octaveCents);
    if (result < 0.0)
        result += octaveCents;
    if (std::abs(result - octaveCents) < 1.0e-9 || std::abs(result) < 1.0e-12)
        return 0.0;
    return result;
}

double AtomicScaleMap::circularDistanceCents (double a, double b) noexcept
{
    if (! std::isfinite(a) || ! std::isfinite(b))
        return std::numeric_limits<double>::infinity();
    const double difference = std::abs(normalizedCents(a - b));
    return std::min(difference, octaveCents - difference);
}

int AtomicScaleMap::defaultMaxDegreeCount (DeriveMode mode) noexcept
{
    switch ((DeriveMode) clampMode(mode))
    {
        case DeriveMode::Melodic:     return 7;
        case DeriveMode::Performable: return 12;
        case DeriveMode::Microtonal:  return 24;
        case DeriveMode::Scientific:  return 48;
        case DeriveMode::Raw:         return maxDegrees;
    }
    return 12;
}

double AtomicScaleMap::defaultMinSeparationCents (DeriveMode mode) noexcept
{
    switch ((DeriveMode) clampMode(mode))
    {
        case DeriveMode::Melodic:     return 80.0;
        case DeriveMode::Performable: return 40.0;
        case DeriveMode::Microtonal:  return 20.0;
        case DeriveMode::Scientific:  return 10.0;
        case DeriveMode::Raw:         return 0.0;
    }
    return 40.0;
}

void AtomicScaleMap::normalizeDegreeWeights() noexcept
{
    double maximum = 0.0;
    for (int i = 0; i < degreeCount; ++i)
        maximum = std::max(maximum, degrees[(size_t) i].weight);

    if (! isValidPositive(maximum))
    {
        for (int i = 0; i < degreeCount; ++i)
            degrees[(size_t) i].weight = 1.0;
        return;
    }

    for (int i = 0; i < degreeCount; ++i)
        degrees[(size_t) i].weight = std::clamp(degrees[(size_t) i].weight / maximum,
                                               0.0, 1.0);
}

void AtomicScaleMap::sortDegrees() noexcept
{
    // Stable insertion sort: fixed O(128^2) upper bound and no implementation-
    // dependent temporary allocation (unlike stable_sort).
    for (int i = 1; i < degreeCount; ++i)
    {
        const Degree value = degrees[(size_t) i];
        int j = i;
        while (j > 0)
        {
            const auto& previous = degrees[(size_t) (j - 1)];
            const bool before = value.cents < previous.cents
                || (orderedEqual(value.cents, previous.cents)
                    && value.sourceIndex < previous.sourceIndex);
            if (! before)
                break;

            degrees[(size_t) j] = previous;
            --j;
        }
        degrees[(size_t) j] = value;
    }
}

void AtomicScaleMap::rebuildPitchTable() noexcept
{
    pitchTable.fill({});
    tableSize = 0;

    if (degreeCount <= 0 || ! isValidPositive(configuredRootFrequencyHz))
        return;

    const double rootExactMidi = 69.0
        + 12.0 * std::log2(configuredRootFrequencyHz / 440.0);

    for (int octave = 0; octave < configuredOctaves; ++octave)
    {
        for (int degreeIndex = 0; degreeIndex < degreeCount; ++degreeIndex)
        {
            if (tableSize >= maxTableSize)
                return;

            const auto& degree = degrees[(size_t) degreeIndex];
            const double absoluteCents = (double) octave * octaveCents + degree.cents;
            const double exactMidi = rootExactMidi + absoluteCents / 100.0;

            // Outside +/-50 cents of MIDI's endpoints cannot be represented by
            // nearest-note + per-note bend without pinning to note 0/127.
            if (! std::isfinite(exactMidi) || exactMidi > 127.5)
                return;
            if (exactMidi < -0.5)
                continue;

            const int nearest = std::clamp((int) std::llround(exactMidi), 0, 127);
            const double frequency = configuredRootFrequencyHz
                                   * std::exp2(absoluteCents / octaveCents);
            if (! isValidPositive(frequency))
                return;

            auto& pitch = pitchTable[(size_t) tableSize];
            pitch.step = tableSize;
            pitch.degreeIndex = degreeIndex;
            pitch.octave = octave;
            pitch.midiNote = nearest;
            pitch.frequencyHz = frequency;
            pitch.exactMidiNote = exactMidi;
            pitch.centsFromNearestMidi = (exactMidi - (double) nearest) * 100.0;
            pitch.degreeWeight = degree.weight;
            pitch.sourceIndex = degree.sourceIndex;
            ++tableSize;
        }
    }
}
