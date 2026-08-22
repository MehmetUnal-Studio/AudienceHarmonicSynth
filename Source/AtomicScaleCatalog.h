#pragma once

#include "AtomicScaleMap.h"

#include <array>

// Process-wide, immutable projection of the generated atomic scale catalog.
//
// Construction performs the only catalog-to-map conversion. Every subsequent
// query is a bounded fixed-array lookup, so processors can copy the selected
// AtomicScaleMap and re-root that snapshot without allocating or parsing data.
class AtomicScaleCatalog final
{
public:
    static constexpr int numElements = 29;
    static constexpr int numModes = 5;
    static constexpr int numScaleSets = numElements * numModes;

    // Initialises on the first non-realtime call and remains immutable for the
    // lifetime of the process. Plugin processors should acquire this in their
    // constructor, before audio processing starts.
    static const AtomicScaleCatalog& instance() noexcept;

    static int clampElementIndex (int elementIndex) noexcept;
    static int clampModeIndex (int modeIndex) noexcept;

    static const char* elementName (int elementIndex) noexcept;
    static const char* elementSymbol (int elementIndex) noexcept;
    static const char* modeName (int modeIndex) noexcept;

    const AtomicScaleMap& getMap (int elementIndex, int modeIndex) const noexcept;
    int getDegreeCount (int elementIndex, int modeIndex) const noexcept;
    double getReferenceWavelengthNm (int elementIndex, int modeIndex) const noexcept;

    AtomicScaleCatalog (const AtomicScaleCatalog&) = delete;
    AtomicScaleCatalog& operator= (const AtomicScaleCatalog&) = delete;

private:
    AtomicScaleCatalog() noexcept;

    static int scaleSetIndex (int elementIndex, int modeIndex) noexcept;

    std::array<AtomicScaleMap, numScaleSets> maps {};
};
