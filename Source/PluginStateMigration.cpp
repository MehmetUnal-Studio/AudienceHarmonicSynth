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

        // Clamp before converting to int. A finite hostile value such as
        // FLT_MAX is outside roundToInt's representable domain.
        const float bounded = juce::jlimit((float) minimum, (float) maximum, value);
        return juce::jlimit(minimum, maximum, juce::roundToInt(bounded));
    }

    void writeChoiceIndex (juce::ValueTree& parameter, int index)
    {
        if (parameter.isValid())
            parameter.setProperty("value", (float) index, nullptr);
    }

    float readNumericValue (const juce::ValueTree& parameter,
                            float minimum, float maximum,
                            float fallback) noexcept
    {
        if (! parameter.isValid())
            return juce::jlimit(minimum, maximum, fallback);

        const float value = (float) parameter.getProperty("value", fallback);
        if (! std::isfinite(value))
            return juce::jlimit(minimum, maximum, fallback);

        return juce::jlimit(minimum, maximum, value);
    }

    void writeNumericValue (juce::ValueTree& parameter, float value)
    {
        if (parameter.isValid())
            parameter.setProperty("value", value, nullptr);
    }

    void removeParameterNodes (juce::ValueTree& state,
                               const juce::String& parameterId)
    {
        for (int index = state.getNumChildren() - 1; index >= 0; --index)
        {
            auto child = state.getChild(index);
            if (child.getProperty("id").toString() == parameterId)
                state.removeChild(index, nullptr);
            else
                removeParameterNodes(child, parameterId);
        }
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

    const auto schemaValue = state.getProperty("cosmicMicrowaveSchema", 0);
    double rawSchema = 0.0;
    if (schemaValue.isInt() || schemaValue.isInt64()
        || schemaValue.isDouble() || schemaValue.isBool())
        rawSchema = (double) schemaValue;

    if (! std::isfinite(rawSchema))
        rawSchema = 0.0;

    // Do not destructively downgrade a state written by a future product.
    // Current hosts can still load the parameter nodes they recognise.
    if (rawSchema > (double) currentSchema)
        return;

    const int schema = juce::roundToInt(
        juce::jlimit(0.0, (double) currentSchema, rawSchema));
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

    // Schema 4 adds the MIDI-only Time Field. APVTS serializes denormalized
    // values, so these are choice indices and parameter-unit values rather
    // than 0..1 normalized values. Append only missing nodes: valid values from
    // development builds and hand-authored states must survive migration.
    if (! containsParameter(state, "timeMode"))
        appendParameterValue(state, "timeMode", 0.0f);             // Flow
    if (! containsParameter(state, "clockSource"))
        appendParameterValue(state, "clockSource", 0.0f);          // Host
    if (! containsParameter(state, "internalBpm"))
        appendParameterValue(state, "internalBpm", 120.0f);
    if (! containsParameter(state, "gridDivision"))
        appendParameterValue(state, "gridDivision", 2.0f);         // 1/16
    if (! containsParameter(state, "maxAttacksPerStep"))
        appendParameterValue(state, "maxAttacksPerStep", 4.0f);
    if (! containsParameter(state, "maxActiveVoices"))
        appendParameterValue(state, "maxActiveVoices", 16.0f);
    if (! containsParameter(state, "gatePercent"))
        appendParameterValue(state, "gatePercent", 70.0f);
    if (! containsParameter(state, "temporalSpread"))
        appendParameterValue(state, "temporalSpread", 2.0f);       // 4 slots

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
    sanitizeChoice("timeMode", 2, 0);
    sanitizeChoice("clockSource", 1, 0);
    sanitizeChoice("gridDivision", 3, 2);
    sanitizeChoice("temporalSpread", 4, 2);

    const auto sanitizeNumeric = [&state] (const char* id,
                                           float minimum, float maximum,
                                           float fallback, bool integerValue)
    {
        auto parameter = findParameterNode(state, id);
        if (! parameter.isValid())
            return;

        float value = readNumericValue(parameter, minimum, maximum, fallback);
        if (integerValue)
            value = (float) juce::roundToInt(value);
        writeNumericValue(parameter, value);
    };
    sanitizeNumeric("internalBpm", 40.0f, 240.0f, 120.0f, false);
    sanitizeNumeric("maxAttacksPerStep", 1.0f, 16.0f, 4.0f, true);
    sanitizeNumeric("maxActiveVoices", 1.0f, 16.0f, 16.0f, true);
    sanitizeNumeric("gatePercent", 5.0f, 100.0f, 70.0f, false);
    // Schema 6 retires the short-lived schema-5 gate experiment completely.
    // Remove only those exact obsolete IDs; every established parameter and
    // root routing property remains untouched.
    removeParameterNodes(state, "timeGateEnabled");
    removeParameterNodes(state, "timeGateWaveform");
    removeParameterNodes(state, "timeGateRateMode");
    removeParameterNodes(state, "timeGateSyncDivision");
    removeParameterNodes(state, "timeGateRateHz");

    state.setProperty("cosmicMicrowaveSchema", currentSchema, nullptr);
}
}
