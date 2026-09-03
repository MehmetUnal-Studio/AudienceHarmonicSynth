#include "AtomicScaleCatalog.h"

#include "AtomicScaleCatalogData.h"

#include <algorithm>
#include <cstddef>

namespace
{
    constexpr bool generatedCatalogShapeIsValid() noexcept
    {
        if (AtomicScaleCatalogData::numElements != AtomicScaleCatalog::numElements
            || AtomicScaleCatalogData::numModes != AtomicScaleCatalog::numModes
            || AtomicScaleCatalogData::sets.size()
                != (std::size_t) AtomicScaleCatalog::numScaleSets)
            return false;

        for (const auto& set : AtomicScaleCatalogData::sets)
        {
            const auto offset = (std::size_t) set.offset;
            const auto count = (std::size_t) set.count;
            if (count == 0 || count > (std::size_t) AtomicScaleMap::maxDegrees
                || offset > AtomicScaleCatalogData::cents.size()
                || count > AtomicScaleCatalogData::cents.size() - offset)
                return false;
        }

        return true;
    }

    static_assert(generatedCatalogShapeIsValid(),
                  "Generated atomic scale catalog has invalid dimensions or offsets");
}

const AtomicScaleCatalog& AtomicScaleCatalog::instance() noexcept
{
    static const AtomicScaleCatalog catalog;
    return catalog;
}

int AtomicScaleCatalog::clampElementIndex (int elementIndex) noexcept
{
    return std::clamp(elementIndex, 0, numElements - 1);
}

int AtomicScaleCatalog::clampModeIndex (int modeIndex) noexcept
{
    return std::clamp(modeIndex, 0, numModes - 1);
}

const char* AtomicScaleCatalog::elementName (int elementIndex) noexcept
{
    return AtomicScaleCatalogData::elementNames[
        (std::size_t) clampElementIndex(elementIndex)];
}

const char* AtomicScaleCatalog::elementSymbol (int elementIndex) noexcept
{
    return AtomicScaleCatalogData::elementSymbols[
        (std::size_t) clampElementIndex(elementIndex)];
}

const char* AtomicScaleCatalog::modeName (int modeIndex) noexcept
{
    return AtomicScaleCatalogData::modeNames[
        (std::size_t) clampModeIndex(modeIndex)];
}

const AtomicScaleMap& AtomicScaleCatalog::getMap (int elementIndex,
                                                  int modeIndex) const noexcept
{
    return maps[(std::size_t) scaleSetIndex(elementIndex, modeIndex)];
}

int AtomicScaleCatalog::getDegreeCount (int elementIndex, int modeIndex) const noexcept
{
    return getMap(elementIndex, modeIndex).getDegreeCount();
}

double AtomicScaleCatalog::getReferenceWavelengthNm (int elementIndex,
                                                      int modeIndex) const noexcept
{
    const auto index = (std::size_t) scaleSetIndex(elementIndex, modeIndex);
    return AtomicScaleCatalogData::sets[index].referenceWavelengthNm;
}

AtomicScaleCatalog::AtomicScaleCatalog() noexcept
{
    for (int index = 0; index < numScaleSets; ++index)
    {
        const auto& set = AtomicScaleCatalogData::sets[(std::size_t) index];
        const auto* firstCent = AtomicScaleCatalogData::cents.data()
                              + (std::size_t) set.offset;
        (void) maps[(std::size_t) index].setCents(firstCent, (int) set.count);
    }
}

int AtomicScaleCatalog::scaleSetIndex (int elementIndex, int modeIndex) noexcept
{
    return clampElementIndex(elementIndex) * numModes + clampModeIndex(modeIndex);
}
