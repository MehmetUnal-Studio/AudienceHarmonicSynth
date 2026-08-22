#include "../Source/AtomicScaleMap.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <type_traits>

namespace
{
    void expect (bool ok, const char* name, int& failed)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << name << "\n";
        if (! ok)
            ++failed;
    }

    bool approximately (double actual, double expected, double tolerance = 1.0e-8)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    bool allStateFiniteAndBounded (const AtomicScaleMap& map)
    {
        if (map.getDegreeCount() < 1
            || map.getDegreeCount() > AtomicScaleMap::maxDegrees
            || map.getScaleTableSize() < 1
            || map.getScaleTableSize() > AtomicScaleMap::maxTableSize)
            return false;

        for (int i = 0; i < map.getDegreeCount(); ++i)
        {
            const auto degree = map.getDegree(i);
            if (! std::isfinite(degree.cents) || degree.cents < 0.0 || degree.cents >= 1200.0
                || ! std::isfinite(degree.weight) || degree.weight < 0.0 || degree.weight > 1.0)
                return false;
        }

        for (int i = 0; i < map.getScaleTableSize(); ++i)
        {
            const auto pitch = map.getPitch(i);
            if (! pitch.isValid() || pitch.step != i
                || pitch.degreeIndex < 0 || pitch.degreeIndex >= map.getDegreeCount()
                || pitch.midiNote < 0 || pitch.midiNote > 127
                || ! std::isfinite(pitch.frequencyHz)
                || ! std::isfinite(pitch.exactMidiNote)
                || ! std::isfinite(pitch.centsFromNearestMidi)
                || std::abs(pitch.centsFromNearestMidi) > 50.0000001)
                return false;
        }

        return true;
    }
}

int main()
{
    static_assert(std::is_trivially_copyable<AtomicScaleMap>::value,
                  "AtomicScaleMap must remain a publishable fixed snapshot");

    int failed = 0;
    AtomicScaleMap map;

    const std::array<AtomicScaleMap::DegreeInput, 5> exactDegrees {{
        { 700.5, 0.25, 7 },
        { 0.0, 1.0, 0 },
        { std::numeric_limits<double>::quiet_NaN(), 1.0, 99 },
        { 100.0, std::numeric_limits<double>::infinity(), 1 },
        { -100.0, 0.5, 11 }
    }};

    expect(map.setDegrees(exactDegrees.data(), (int) exactDegrees.size()),
           "preselected degree bank accepts finite cents", failed);
    expect(map.getDegreeCount() == 4
        && approximately(map.getDegreeCents(0), 0.0)
        && approximately(map.getDegreeCents(1), 100.0)
        && approximately(map.getDegreeCents(2), 700.5)
        && approximately(map.getDegreeCents(3), 1100.0),
        "degrees are filtered, octave-normalised and deterministically sorted", failed);
    expect(approximately(map.getDegreeWeight(0), 1.0)
        && approximately(map.getDegreeWeight(1), 0.0)
        && approximately(map.getDegreeWeight(2), 0.25)
        && approximately(map.getDegreeWeight(3), 0.5),
        "finite positive degree weights are normalised and invalid weights are safe", failed);

    map.setRootMidiAndOctaves(36, 2);
    expect(map.getRootMidi() == 36 && map.getOctaveCount() == 2
        && map.getScaleTableSize() == 8,
        "one-octave bank projects across the requested octave span", failed);

    const auto root = map.getPitch(0);
    const auto microtone = map.getPitch(2);
    expect(root.isValid() && root.midiNote == 36
        && approximately(root.frequencyHz, AtomicScaleMap::midiToFrequencyHz(36)),
        "root projection follows standard MIDI tuning", failed);
    expect(microtone.isValid() && microtone.midiNote == 43
        && approximately(microtone.exactMidiNote, 43.005)
        && approximately(microtone.centsFromNearestMidi, 0.5)
        && ! approximately(microtone.frequencyHz,
                           AtomicScaleMap::midiToFrequencyHz(microtone.midiNote), 1.0e-6),
        "pitch exposes nearest Normal MIDI note and exact MPE microtonal frequency", failed);

    const auto degreeSnapshot = map.getDegree(2);
    const double oldRootHz = map.getScaleFrequencyHz(0);
    map.setRootMidiAndOctaves(48, 1);
    expect(map.getDegreeCount() == 4
        && approximately(map.getDegree(2).cents, degreeSnapshot.cents)
        && approximately(map.getScaleFrequencyHz(0), oldRootHz * 2.0),
        "re-rooting only rebuilds projection and preserves the atomic degree bank", failed);

    expect(map.xToStep(-std::numeric_limits<float>::infinity()) == 0
        && map.xToStep(std::numeric_limits<float>::quiet_NaN()) == 0
        && map.xToStep(0.0f) == 0,
        "negative, zero and NaN X select the first pitch", failed);
    expect(map.xToStep(1.0f) == map.getScaleTableSize() - 1
        && map.xToStep(std::numeric_limits<float>::infinity()) == map.getScaleTableSize() - 1,
        "one and positive infinity X select the last pitch", failed);

    const auto beforeLookup = map.getPitch(0);
    bool lookupStable = true;
    for (int i = 0; i < 100000; ++i)
    {
        const auto pitch = map.xToPitch((float) (i % 1000) / 999.0f);
        lookupStable = lookupStable && pitch.isValid()
            && std::isfinite(pitch.frequencyHz)
            && pitch.midiNote >= 0 && pitch.midiNote <= 127;
    }
    expect(lookupStable && approximately(map.getPitch(0).frequencyHz, beforeLookup.frequencyHz),
           "realtime lookup is finite and has no mutable state", failed);

    std::array<AtomicScaleMap::SpectralLine, 8> hydrogen {{
        { 6562.790, 6500.0, AtomicScaleMap::WavelengthUnit::Unknown, 0 },
        { 4861.350, 1500.0, AtomicScaleMap::WavelengthUnit::Unknown, 1 },
        { 4340.472, 1000.0, AtomicScaleMap::WavelengthUnit::Unknown, 2 },
        { 4101.734, 675.0, AtomicScaleMap::WavelengthUnit::Unknown, 3 },
        { 3970.075, 255.0, AtomicScaleMap::WavelengthUnit::Unknown, 4 },
        { 3889.064, 195.0, AtomicScaleMap::WavelengthUnit::Unknown, 5 },
        { std::numeric_limits<double>::infinity(), 10000.0,
          AtomicScaleMap::WavelengthUnit::Nanometer, 6 },
        { 900.0, 0.0, AtomicScaleMap::WavelengthUnit::Nanometer, 7 }
    }};

    AtomicScaleMap::DeriveOptions performable;
    performable.mode = AtomicScaleMap::DeriveMode::Performable;
    expect(map.deriveFromWavelengths(hydrogen.data(), (int) hydrogen.size(), performable),
           "wavelength catalog derives a playable degree bank", failed);
    expect(approximately(map.getReferenceWavelengthNm(), 656.279, 0.001)
        && map.getDegreeCount() <= 12
        && approximately(map.getDegreeCents(0), 0.0),
        "angstrom heuristic and longest-positive-wavelength root match legacy rules", failed);

    bool separated = true;
    for (int i = 0; i < map.getDegreeCount(); ++i)
        for (int j = i + 1; j < map.getDegreeCount(); ++j)
            separated = separated
                && AtomicScaleMap::circularDistanceCents(map.getDegreeCents(i),
                                                         map.getDegreeCents(j)) >= 40.0 - 1.0e-9;
    expect(separated, "performable wavelength derivation respects 40-cent spacing", failed);

    std::array<double, AtomicScaleMap::maxDegrees + 17> oversized {};
    for (int i = 0; i < (int) oversized.size(); ++i)
        oversized[(size_t) i] = (double) i * 7.0;
    expect(map.setCents(oversized.data(), (int) oversized.size())
        && map.getDegreeCount() == AtomicScaleMap::maxDegrees
        && map.wasInputTruncated(),
        "degree input is deterministically capped at 128", failed);

    map.setRootMidiAndOctaves(0, AtomicScaleMap::maxOctaves);
    expect(map.getScaleTableSize() == AtomicScaleMap::maxTableSize
        && map.getPitch(AtomicScaleMap::maxTableSize - 1).octave == 5,
        "128-degree Raw bank spans all six octaves without a table-capacity loss", failed);

    map.setRootMidiAndOctaves(127, 6);
    bool highRootValid = map.getScaleTableSize() > 0;
    for (int i = 0; i < map.getScaleTableSize(); ++i)
    {
        const auto pitch = map.getPitch(i);
        highRootValid = highRootValid && pitch.isValid()
            && pitch.midiNote >= 0 && pitch.midiNote <= 127
            && pitch.exactMidiNote <= 127.5
            && std::isfinite(pitch.frequencyHz);
    }
    expect(highRootValid, "high roots truncate instead of pinning out-of-range microtones", failed);

    const double invalidCents[] = {
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity()
    };
    expect(! map.setCents(invalidCents, 2)
        && map.getDegreeCount() == 1
        && map.getScaleTableSize() == 1
        && map.getPitch(0).isValid(),
        "invalid degree input installs a deterministic root-only fallback", failed);

    map.setRootMidiAndOctaves(-100, 0, std::numeric_limits<double>::quiet_NaN());
    expect(map.getRootMidi() == 0
        && map.getOctaveCount() == AtomicScaleMap::minOctaves
        && approximately(map.getRootFrequencyHz(), AtomicScaleMap::midiToFrequencyHz(0)),
        "invalid root, octave span and custom frequency are clamped safely", failed);

    map.setRootMidiAndOctaves(60, 1, std::numeric_limits<double>::max());
    expect(approximately(map.getRootFrequencyHz(), AtomicScaleMap::midiToFrequencyHz(127))
        && map.getPitch(0).midiNote == 127,
        "extreme finite custom root frequency is clamped to MIDI range", failed);

    map.setCents(std::array<double, 3> { 0.0, 200.0, 400.0 }.data(), 3);
    map.setRootMidiAndOctaves(60, 1);
    expect(map.findNearestScaleStepForMidi(61) == 0
        && map.findNearestScaleStepForFrequency(AtomicScaleMap::midiToFrequencyHz(64)) == 2
        && map.findNearestScaleStepForFrequency(std::numeric_limits<double>::quiet_NaN()) == -1,
        "nearest lookup resolves lower-pitch ties and rejects invalid frequency", failed);

    expect(AtomicScaleMap::rootMidiFor(0, 2) == 36
        && AtomicScaleMap::rootMidiFor(99, 99) == 127
        && approximately(AtomicScaleMap::normalizedCents(-100.0), 1100.0)
        && approximately(AtomicScaleMap::normalizeWavelengthNm(6562.79,
                         AtomicScaleMap::WavelengthUnit::Angstrom), 656.279)
        && std::isinf(AtomicScaleMap::circularDistanceCents(
             std::numeric_limits<double>::quiet_NaN(), 0.0)),
        "public migration helpers use stable MIDI and spectral conventions", failed);

    std::array<AtomicScaleMap::SpectralLine, 256> hostileLines {};
    for (int i = 0; i < (int) hostileLines.size(); ++i)
    {
        auto& line = hostileLines[(size_t) i];
        line.wavelength = 300.0 + (double) ((i * 7919) % 5000) * 0.1;
        line.intensity = 0.001 + (double) ((i * 3571) % 10000);
        line.unit = (AtomicScaleMap::WavelengthUnit) (i % 3);
        line.sourceIndex = i;
    }
    hostileLines[0].wavelength = std::numeric_limits<double>::quiet_NaN();
    hostileLines[1].wavelength = std::numeric_limits<double>::infinity();
    hostileLines[2].wavelength = std::numeric_limits<double>::denorm_min();
    hostileLines[3].wavelength = std::numeric_limits<double>::max();
    hostileLines[4].intensity = std::numeric_limits<double>::quiet_NaN();
    hostileLines[5].intensity = std::numeric_limits<double>::infinity();
    hostileLines[6].intensity = std::numeric_limits<double>::denorm_min();
    hostileLines[7].intensity = std::numeric_limits<double>::max();
    hostileLines[8].intensity = -1.0;

    bool hostileInputsSafe = true;
    for (const int rawMode : { -100, 0, 1, 2, 3, 4, 100 })
    {
        AtomicScaleMap::DeriveOptions hostileOptions;
        hostileOptions.mode = (AtomicScaleMap::DeriveMode) rawMode;
        hostileOptions.maxDegreeCount = rawMode == 100 ? std::numeric_limits<int>::max() : 0;
        hostileOptions.minSeparationCents = rawMode < 0
            ? std::numeric_limits<double>::quiet_NaN()
            : std::numeric_limits<double>::infinity();
        map.deriveFromWavelengths(hostileLines.data(), (int) hostileLines.size(), hostileOptions);

        for (const auto rootMidi : { std::numeric_limits<int>::min(), 0, 60, 127,
                                     std::numeric_limits<int>::max() })
        {
            map.setRootMidiAndOctaves(rootMidi, rawMode,
                rawMode == 2 ? std::numeric_limits<double>::denorm_min() : 0.0);
            hostileInputsSafe = hostileInputsSafe && allStateFiniteAndBounded(map);
        }
    }
    expect(hostileInputsSafe,
           "hostile catalogs, enum values and roots cannot leak NaN/Inf or invalid MIDI", failed);

    std::cout << "\nSummary: " << (failed == 0 ? "ok" : "failed") << "\n";
    return failed == 0 ? 0 : 1;
}
