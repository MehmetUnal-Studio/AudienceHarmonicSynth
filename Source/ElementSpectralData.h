#pragma once

#include "AtomicScaleBuilder.h"

#include <vector>

namespace ElementSpectralData
{
    int numSupportedElements() noexcept;
    const char* symbolForElement (int elementIndex) noexcept;
    const char* nameForElement (int elementIndex) noexcept;
    const std::vector<AtomicScaleBuilder::SourceLine>& linesForElement (int elementIndex);
}
