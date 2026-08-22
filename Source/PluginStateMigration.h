#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace CosmicStateMigration
{
    inline constexpr int currentSchema = 5;

    // Upgrades a serialized APVTS ValueTree in-place. APVTS stores choice
    // parameters as their denormalized choice index (0, 1, 2...), not as 0..1.
    // Keeping this in a small testable module prevents silent project-recall
    // regressions when parameter ranges evolve.
    void migrate (juce::ValueTree& state);

    juce::ValueTree findParameterNode (const juce::ValueTree& state,
                                       const juce::String& parameterId);
    bool containsParameter (const juce::ValueTree& state,
                            const juce::String& parameterId);
}
