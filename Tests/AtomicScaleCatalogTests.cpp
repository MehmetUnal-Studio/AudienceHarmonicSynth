#include "../Source/AtomicScaleCatalog.h"
#include "../Source/AtomicScaleCatalogData.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>

namespace
{
    bool orderedEqual (double a, double b) noexcept
    {
        return ! std::isless(a, b) && ! std::isless(b, a);
    }

    void expect (bool ok, const char* name, int& failed)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << name << "\n";
        if (! ok)
            ++failed;
    }

    bool sameText (const char* actual, const char* expected)
    {
        return actual != nullptr && expected != nullptr
            && std::string(actual) == expected;
    }
}

int main()
{
    static_assert(AtomicScaleCatalog::numElements == 29,
                  "The product exposes the 29 generated atomic elements");
    static_assert(AtomicScaleCatalog::numModes == 5,
                  "The product exposes all five atomic scale modes");
    static_assert(std::is_same<
                      decltype(std::declval<const AtomicScaleCatalog&>().getMap(0, 0)),
                      const AtomicScaleMap&>::value,
                  "Catalog maps must only be exposed as immutable references");

    int failed = 0;
    const auto& catalog = AtomicScaleCatalog::instance();

    expect(&catalog == &AtomicScaleCatalog::instance(),
           "catalog is a single process-wide immutable instance", failed);

    expect(AtomicScaleCatalog::clampElementIndex(std::numeric_limits<int>::min()) == 0
        && AtomicScaleCatalog::clampElementIndex(-1) == 0
        && AtomicScaleCatalog::clampElementIndex(29) == 28
        && AtomicScaleCatalog::clampElementIndex(std::numeric_limits<int>::max()) == 28
        && AtomicScaleCatalog::clampModeIndex(std::numeric_limits<int>::min()) == 0
        && AtomicScaleCatalog::clampModeIndex(-1) == 0
        && AtomicScaleCatalog::clampModeIndex(5) == 4
        && AtomicScaleCatalog::clampModeIndex(std::numeric_limits<int>::max()) == 4,
           "hostile element and mode indexes clamp to generated catalog bounds", failed);

    expect(sameText(AtomicScaleCatalog::elementSymbol(0), "H")
        && sameText(AtomicScaleCatalog::elementName(0), "Hydrogen")
        && sameText(AtomicScaleCatalog::elementSymbol(1), "He")
        && sameText(AtomicScaleCatalog::elementName(1), "Helium")
        && sameText(AtomicScaleCatalog::elementSymbol(28), "Zn")
        && sameText(AtomicScaleCatalog::elementName(28), "Zinc"),
           "H, He and Zn element metadata is preserved", failed);

    expect(sameText(AtomicScaleCatalog::modeName(0), "Core")
        && sameText(AtomicScaleCatalog::modeName(1), "Extended")
        && sameText(AtomicScaleCatalog::modeName(2), "Microtonal")
        && sameText(AtomicScaleCatalog::modeName(3), "Scientific")
        && sameText(AtomicScaleCatalog::modeName(4), "Raw 128"),
           "all generated mode names are exposed", failed);

    expect(std::abs(catalog.getReferenceWavelengthNm(0, 0) - 656.279) < 1.0e-12
        && std::abs(catalog.getReferenceWavelengthNm(1, 4) - 667.815) < 1.0e-12,
           "Hydrogen and Helium reference wavelengths are preserved in nanometres", failed);

    expect(&catalog.getMap(-100, -100) == &catalog.getMap(0, 0)
        && &catalog.getMap(std::numeric_limits<int>::max(),
                           std::numeric_limits<int>::max()) == &catalog.getMap(28, 4)
        && sameText(AtomicScaleCatalog::elementName(-100), "Hydrogen")
        && sameText(AtomicScaleCatalog::elementSymbol(100), "Zn")
        && sameText(AtomicScaleCatalog::modeName(-100), "Core")
        && sameText(AtomicScaleCatalog::modeName(100), "Raw 128"),
           "all public getters apply the same deterministic clamping policy", failed);

    bool allSetsValid = true;
    bool exactGeneratedDegrees = true;
    bool modeCapsValid = true;
    bool modeSizesMonotonic = true;

    for (int element = 0; element < AtomicScaleCatalog::numElements; ++element)
    {
        int previousModeCount = 0;
        for (int mode = 0; mode < AtomicScaleCatalog::numModes; ++mode)
        {
            const auto setIndex = (std::size_t) (element * AtomicScaleCatalog::numModes + mode);
            const auto& generatedSet = AtomicScaleCatalogData::sets[setIndex];
            const auto& map = catalog.getMap(element, mode);
            const int expectedCount = (int) generatedSet.count;

            allSetsValid = allSetsValid
                && map.getDegreeCount() == expectedCount
                && catalog.getDegreeCount(element, mode) == expectedCount
                && expectedCount >= 1
                && expectedCount <= AtomicScaleMap::maxDegrees
                && std::isfinite(catalog.getReferenceWavelengthNm(element, mode))
                && catalog.getReferenceWavelengthNm(element, mode) > 0.0
                && map.getScaleTableSize() >= expectedCount
                && map.getScaleTableSize() <= AtomicScaleMap::maxTableSize;

            static constexpr int modeCaps[AtomicScaleCatalog::numModes] {
                7, 12, 24, 48, AtomicScaleMap::maxDegrees
            };
            modeCapsValid = modeCapsValid && expectedCount <= modeCaps[mode];
            modeSizesMonotonic = modeSizesMonotonic && expectedCount >= previousModeCount;
            previousModeCount = expectedCount;

            for (int degree = 0; degree < expectedCount; ++degree)
            {
                const double actual = map.getDegreeCents(degree);
                const double expected = AtomicScaleCatalogData::cents[
                    (std::size_t) generatedSet.offset + (std::size_t) degree];
                exactGeneratedDegrees = exactGeneratedDegrees && orderedEqual(actual, expected);
                allSetsValid = allSetsValid
                    && std::isfinite(actual) && actual >= 0.0 && actual < 1200.0;
            }

            for (int step = 0; step < map.getScaleTableSize(); ++step)
            {
                const auto pitch = map.getPitch(step);
                allSetsValid = allSetsValid
                    && pitch.isValid()
                    && pitch.step == step
                    && pitch.degreeIndex >= 0
                    && pitch.degreeIndex < expectedCount
                    && pitch.midiNote >= 0 && pitch.midiNote <= 127
                    && std::isfinite(pitch.frequencyHz)
                    && std::isfinite(pitch.exactMidiNote)
                    && std::isfinite(pitch.centsFromNearestMidi);
            }
        }
    }

    expect(allSetsValid,
           "all 145 catalog maps contain finite bounded degrees and playable pitches", failed);
    expect(exactGeneratedDegrees,
           "every map retains the exact generated cents through setCents", failed);
    expect(modeCapsValid,
           "Core, Extended, Microtonal, Scientific and Raw obey 7/12/24/48/128 caps", failed);
    expect(modeSizesMonotonic,
           "higher-detail modes never expose fewer degrees than lower-detail modes", failed);

    bool rawSetsBounded = true;
    for (int element = 0; element < AtomicScaleCatalog::numElements; ++element)
    {
        const int count = catalog.getDegreeCount(element, AtomicScaleCatalog::numModes - 1);
        rawSetsBounded = rawSetsBounded && count >= 1 && count <= 128;
    }
    expect(rawSetsBounded, "every Raw scale is non-empty and capped at 128 degrees", failed);

    const auto& immutableHydrogen = catalog.getMap(0, 1);
    const int originalRoot = immutableHydrogen.getRootMidi();
    const double originalRootHz = immutableHydrogen.getRootFrequencyHz();
    auto processorSnapshot = immutableHydrogen;
    processorSnapshot.setRootMidiAndOctaves(72, 2);
    expect(processorSnapshot.getRootMidi() == 72
        && immutableHydrogen.getRootMidi() == originalRoot
        && orderedEqual(immutableHydrogen.getRootFrequencyHz(), originalRootHz),
           "processors can re-root a copied map without mutating the catalog", failed);

    if (failed != 0)
        std::cerr << failed << " AtomicScaleCatalog test(s) failed\n";
    return failed == 0 ? 0 : 1;
}
