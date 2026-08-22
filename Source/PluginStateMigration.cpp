#include "PluginStateMigration.h"

#include <cmath>

namespace CosmicStateMigration
{
namespace
{
    void appendParameterValue (juce::ValueTree& state,
                               const juce::String& parameterId,
                               float denormalizedValue)
    {
        juce::ValueTree parameter("PARAM");
        parameter.setProperty("id", parameterId, nullptr);
        parameter.setProperty("value", denormalizedValue, nullptr);
        state.appendChild(parameter, nullptr);
    }

    int readChoiceIndex (const juce::ValueTree& parameter,
                         int minimum, int maximum,
                         int fallback = 0) noexcept
    {
        if (! parameter.isValid())
            return juce::jlimit(minimum, maximum, fallback);

        const float value = (float) parameter.getProperty("value", (float) fallback);
        if (! std::isfinite(value))
            return juce::jlimit(minimum, maximum, fallback);

        return juce::jlimit(minimum, maximum, juce::roundToInt(value));
    }

    void writeChoiceIndex (juce::ValueTree& parameter, int index)
    {
        if (parameter.isValid())
            parameter.setProperty("value", (float) index, nullptr);
    }
}

juce::ValueTree findParameterNode (const juce::ValueTree& state,
                                   const juce::String& parameterId)
{
    if (state.getProperty("id").toString() == parameterId)
        return state;

    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        auto result = findParameterNode(state.getChild(i), parameterId);
        if (result.isValid())
            return result;
    }

    return {};
}

bool containsParameter (const juce::ValueTree& state,
                        const juce::String& parameterId)
{
    return findParameterNode(state, parameterId).isValid();
}

void migrate (juce::ValueTree& state)
{
    if (! state.isValid())
        return;

    const int schema = (int) state.getProperty("cosmicMicrowaveSchema", 0);
    const bool hasLegacySpectralParameters = containsParameter(state, "spectralElement");
    bool legacyAtomicSelection = false;
    int legacyPitchElement = -1;

    if (! containsParameter(state, "normalMidiRoutingMode"))
        appendParameterValue(state, "normalMidiRoutingMode", 0.0f);

    // Every released 1.x build through v1.0.35 stored a 1..16
    // AudioParameterInt. The later 0..15 Choice conversion was never released
    // with an on-disk schema marker, so schema-0 values 1..16 must favour the
    // released contract. A value of 0 can only be the newer Choice's channel 1
    // and is already in the correct representation.
    if (schema == 0)
    {
        auto channel = findParameterNode(state, "normalMidiChannel");
        if (channel.isValid())
        {
            const int oldStoredValue = readChoiceIndex(channel, 0, 16, 1);
            writeChoiceIndex(channel, oldStoredValue > 0 ? oldStoredValue - 1 : 0);
        }
    }

    // Legacy 1.x builds exposed 36 scale choices: seven tonal maps followed
    // by 29 element spectra. The stored APVTS value is the denormalized index.
    if (schema < 2)
    {
        auto scale = findParameterNode(state, "scaleMode");
        if (scale.isValid())
        {
            const int choiceCount = hasLegacySpectralParameters ? 36 : 7;
            const int oldIndex = readChoiceIndex(scale, 0, choiceCount - 1, 0);
            legacyAtomicSelection = oldIndex >= 7;
            if (legacyAtomicSelection)
                legacyPitchElement = oldIndex - 7;
            writeChoiceIndex(scale, oldIndex < 7 ? oldIndex : 0);
        }

        auto engineSource = findParameterNode(state, "engineSource");
        legacyAtomicSelection = legacyAtomicSelection
                             || readChoiceIndex(engineSource, 0, 1, 0) == 1;
    }

    // New instances default to Atomic in createLayout(). Existing MIDI-only
    // schema-2 sessions explicitly migrate to Tonal, while old element-synth
    // sessions recover their Atomic pitch selection.
    if (! containsParameter(state, "pitchSystem"))
        appendParameterValue(state, "pitchSystem", legacyAtomicSelection ? 1.0f : 0.0f);

    // Reuse the original stable parameter IDs. Values are denormalized choice
    // indices: Helium (1) and Extended (1) match the historical defaults.
    if (! containsParameter(state, "spectralElement"))
        appendParameterValue(state, "spectralElement", 1.0f);
    if (legacyPitchElement >= 0)
    {
        auto element = findParameterNode(state, "spectralElement");
        writeChoiceIndex(element, legacyPitchElement);
    }
    if (! containsParameter(state, "atomicScaleMode"))
        appendParameterValue(state, "atomicScaleMode", 1.0f);

    // State blobs are untrusted input. Clamp every choice touched by this
    // migration before APVTS publishes it to parameter atomics.
    const auto sanitizeChoice = [&state] (const char* id, int maximum, int fallback)
    {
        auto parameter = findParameterNode(state, id);
        if (parameter.isValid())
            writeChoiceIndex(parameter, readChoiceIndex(parameter, 0, maximum, fallback));
    };
    sanitizeChoice("scaleMode", 6, 0);
    sanitizeChoice("pitchSystem", 1, 0);
    sanitizeChoice("spectralElement", 28, 1);
    sanitizeChoice("atomicScaleMode", 4, 1);
    sanitizeChoice("normalMidiRoutingMode", 1, 0);
    sanitizeChoice("normalMidiChannel", 15, 0);

    state.setProperty("cosmicMicrowaveSchema", currentSchema, nullptr);
}
}
