#include "../Source/PluginStateMigration.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace
{
    int failed = 0;

    void expect (bool condition, const char* name)
    {
        std::cout << (condition ? "PASS  " : "FAIL  ") << name << "\n";
        if (! condition)
            ++failed;
    }

    juce::ValueTree makeState (int schema = 0)
    {
        juce::ValueTree state("PARAMS");
        if (schema > 0)
            state.setProperty("cosmicMicrowaveSchema", schema, nullptr);
        return state;
    }

    void addParam (juce::ValueTree& state, const char* id, float value)
    {
        juce::ValueTree parameter("PARAM");
        parameter.setProperty("id", id, nullptr);
        parameter.setProperty("value", value, nullptr);
        state.appendChild(parameter, nullptr);
    }

    float valueOf (const juce::ValueTree& state, const char* id)
    {
        const auto parameter = CosmicStateMigration::findParameterNode(state, id);
        return parameter.isValid()
             ? (float) parameter.getProperty("value", -999.0f) : -999.0f;
    }

    bool sameValue (float a, float b)
    {
        return std::abs(a - b) < 1.0e-6f;
    }

    int countParams (const juce::ValueTree& state, const char* id)
    {
        int count = state.getProperty("id").toString() == id ? 1 : 0;
        for (int i = 0; i < state.getNumChildren(); ++i)
            count += countParams(state.getChild(i), id);
        return count;
    }
}

int main()
{
    for (int tonal = 0; tonal < 7; ++tonal)
    {
        auto state = makeState();
        addParam(state, "scaleMode", (float) tonal);
        addParam(state, "spectralElement", 8.0f);
        addParam(state, "externalMidiPitchMode", 0.0f);
        CosmicStateMigration::migrate(state);
        expect(sameValue(valueOf(state, "scaleMode"), (float) tonal),
               "legacy tonal choice preserves its denormalized index");
        expect(valueOf(state, "pitchSystem") == 0.0f,
               "legacy tonal selection migrates to Tonal pitch system");
    }

    for (const int spectralIndex : { 7, 20, 35 })
    {
        auto state = makeState();
        addParam(state, "scaleMode", (float) spectralIndex);
        addParam(state, "spectralElement", (float) (spectralIndex - 7));
        addParam(state, "externalMidiPitchMode", 0.0f);
        CosmicStateMigration::migrate(state);
        expect(valueOf(state, "scaleMode") == 0.0f,
               "legacy spectral scale leaves a valid tonal fallback");
        expect(valueOf(state, "pitchSystem") == 1.0f,
               "legacy spectral scale migrates to Atomic pitch system");
        expect(sameValue(valueOf(state, "spectralElement"), (float) (spectralIndex - 7)),
               "legacy spectral pitch choice transfers its encoded element");
    }

    {
        auto state = makeState();
        addParam(state, "scaleMode", 35.0f);
        addParam(state, "spectralElement", 4.0f);
        addParam(state, "engineSource", 0.0f);
        addParam(state, "normalMidiChannel", 1.0f);
        CosmicStateMigration::migrate(state);
        expect(sameValue(valueOf(state, "scaleMode"), 0.0f)
                   && sameValue(valueOf(state, "pitchSystem"), 1.0f)
                   && sameValue(valueOf(state, "spectralElement"), 28.0f)
                   && sameValue(valueOf(state, "atomicScaleMode"), 1.0f)
                   && sameValue(valueOf(state, "normalMidiChannel"), 0.0f),
               "legacy spectral pitch element wins when the old timbre element disagrees");
    }

    {
        auto state = makeState();
        addParam(state, "scaleMode", 3.0f);
        addParam(state, "engineSource", 1.0f);
        addParam(state, "spectralElement", 4.0f);
        addParam(state, "atomicScaleMode", 3.0f);
        addParam(state, "externalMidiPitchMode", 0.0f);
        CosmicStateMigration::migrate(state);
        expect(sameValue(valueOf(state, "scaleMode"), 3.0f)
                   && sameValue(valueOf(state, "pitchSystem"), 1.0f)
                   && sameValue(valueOf(state, "spectralElement"), 4.0f)
                   && sameValue(valueOf(state, "atomicScaleMode"), 3.0f),
               "legacy element engine migrates to Atomic even with a tonal scale index");
    }

    for (const int oldChannel : { 1, 7, 16 })
    {
        auto state = makeState();
        addParam(state, "normalMidiChannel", (float) oldChannel);
        CosmicStateMigration::migrate(state);
        expect(sameValue(valueOf(state, "normalMidiChannel"), (float) (oldChannel - 1)),
               "v1.0.28 integer MIDI channel migrates from 1-based to 0-based");
    }

    for (const int releasedChannel : { 1, 7, 15, 16 })
    {
        auto state = makeState();
        addParam(state, "normalMidiChannel", (float) releasedChannel);
        addParam(state, "externalMidiPitchMode", 0.0f);
        CosmicStateMigration::migrate(state);
        expect(sameValue(valueOf(state, "normalMidiChannel"), (float) (releasedChannel - 1)),
               "released v1.0.35 integer MIDI channel migrates from 1-based");
    }

    for (const int choiceChannel : { 0, 7, 15 })
    {
        auto state = makeState(2);
        addParam(state, "normalMidiChannel", (float) choiceChannel);
        CosmicStateMigration::migrate(state);
        expect(sameValue(valueOf(state, "normalMidiChannel"), (float) choiceChannel),
               "schema-2 choice MIDI channel remains unchanged");
    }

    {
        auto state = makeState(2);
        addParam(state, "scaleMode", 6.0f);
        addParam(state, "spectralElement", 8.0f);
        addParam(state, "atomicScaleMode", 4.0f);
        CosmicStateMigration::migrate(state);
        expect(valueOf(state, "scaleMode") == 6.0f,
               "schema-2 tonal choice is not remapped");
        expect(valueOf(state, "pitchSystem") == 0.0f,
               "schema-2 MIDI-only session explicitly remains Tonal");
        expect(sameValue(valueOf(state, "spectralElement"), 8.0f)
                   && sameValue(valueOf(state, "atomicScaleMode"), 4.0f),
               "schema-2 Atomic parameter values remain intact while its pitch system stays Tonal");
        expect((int) state.getProperty("cosmicMicrowaveSchema", 0)
                   == CosmicStateMigration::currentSchema,
               "state advances to the current schema");

        CosmicStateMigration::migrate(state);
        expect(valueOf(state, "scaleMode") == 6.0f
            && valueOf(state, "pitchSystem") == 0.0f,
               "migration is idempotent");
    }

    {
        auto state = makeState(2);
        addParam(state, "scaleMode", std::numeric_limits<float>::quiet_NaN());
        CosmicStateMigration::migrate(state);
        expect(valueOf(state, "scaleMode") == 0.0f,
               "non-finite current choice is sanitized before APVTS publication");
    }

    {
        auto state = makeState();
        addParam(state, "scaleMode", std::numeric_limits<float>::quiet_NaN());
        addParam(state, "spectralElement", 0.0f);
        CosmicStateMigration::migrate(state);
        expect(valueOf(state, "scaleMode") == 0.0f,
               "non-finite legacy choice falls back deterministically");
    }

    {
        auto state = makeState(3);
        CosmicStateMigration::migrate(state);
        expect(sameValue(valueOf(state, "timeMode"), 0.0f)
                   && sameValue(valueOf(state, "clockSource"), 0.0f)
                   && sameValue(valueOf(state, "internalBpm"), 120.0f)
                   && sameValue(valueOf(state, "gridDivision"), 2.0f)
                   && sameValue(valueOf(state, "maxAttacksPerStep"), 4.0f)
                   && sameValue(valueOf(state, "maxActiveVoices"), 16.0f)
                   && sameValue(valueOf(state, "gatePercent"), 70.0f)
                   && sameValue(valueOf(state, "temporalSpread"), 2.0f),
               "schema-3 state receives backward-compatible Time Field defaults");
        expect((int) state.getProperty("cosmicMicrowaveSchema", 0)
                   == CosmicStateMigration::currentSchema,
               "schema-3 Time Field migration advances to schema 4");

        const int childCount = state.getNumChildren();
        CosmicStateMigration::migrate(state);
        expect(state.getNumChildren() == childCount
                   && countParams(state, "timeMode") == 1
                   && countParams(state, "temporalSpread") == 1
                   && sameValue(valueOf(state, "internalBpm"), 120.0f),
               "schema-4 migration is idempotent and does not duplicate Time Field nodes");
    }

    {
        auto state = makeState(3);
        addParam(state, "timeMode", 2.0f);
        addParam(state, "internalBpm", 137.5f);
        addParam(state, "gatePercent", 82.25f);
        CosmicStateMigration::migrate(state);
        expect(sameValue(valueOf(state, "timeMode"), 2.0f)
                   && sameValue(valueOf(state, "internalBpm"), 137.5f)
                   && sameValue(valueOf(state, "gatePercent"), 82.25f)
                   && sameValue(valueOf(state, "gridDivision"), 2.0f),
               "schema-3 migration preserves existing valid Time Field values and fills gaps");
    }

    {
        auto state = makeState(4);
        addParam(state, "timeMode", 2.0f);
        addParam(state, "clockSource", 1.0f);
        addParam(state, "internalBpm", 178.5f);
        addParam(state, "gridDivision", 3.0f);
        addParam(state, "maxAttacksPerStep", 12.0f);
        addParam(state, "maxActiveVoices", 9.0f);
        addParam(state, "gatePercent", 84.25f);
        addParam(state, "temporalSpread", 4.0f);
        CosmicStateMigration::migrate(state);
        expect(sameValue(valueOf(state, "timeMode"), 2.0f)
                   && sameValue(valueOf(state, "clockSource"), 1.0f)
                   && sameValue(valueOf(state, "internalBpm"), 178.5f)
                   && sameValue(valueOf(state, "gridDivision"), 3.0f)
                   && sameValue(valueOf(state, "maxAttacksPerStep"), 12.0f)
                   && sameValue(valueOf(state, "maxActiveVoices"), 9.0f)
                   && sameValue(valueOf(state, "gatePercent"), 84.25f)
                   && sameValue(valueOf(state, "temporalSpread"), 4.0f),
               "schema-4 preserves valid denormalized Time Field values");
    }

    {
        auto state = makeState(4);
        addParam(state, "timeMode", std::numeric_limits<float>::max());
        addParam(state, "clockSource", -100.0f);
        addParam(state, "internalBpm", 999.0f);
        addParam(state, "gridDivision", 99.0f);
        addParam(state, "maxAttacksPerStep", -4.0f);
        addParam(state, "maxActiveVoices", 99.0f);
        addParam(state, "gatePercent", -200.0f);
        addParam(state, "temporalSpread", 99.0f);
        CosmicStateMigration::migrate(state);
        expect(sameValue(valueOf(state, "timeMode"), 2.0f)
                   && sameValue(valueOf(state, "clockSource"), 0.0f)
                   && sameValue(valueOf(state, "internalBpm"), 240.0f)
                   && sameValue(valueOf(state, "gridDivision"), 3.0f)
                   && sameValue(valueOf(state, "maxAttacksPerStep"), 1.0f)
                   && sameValue(valueOf(state, "maxActiveVoices"), 16.0f)
                   && sameValue(valueOf(state, "gatePercent"), 5.0f)
                   && sameValue(valueOf(state, "temporalSpread"), 4.0f),
               "schema-4 clamps hostile finite Time Field values in parameter units");
    }

    {
        auto state = makeState(4);
        addParam(state, "timeMode", std::numeric_limits<float>::quiet_NaN());
        addParam(state, "clockSource", std::numeric_limits<float>::infinity());
        addParam(state, "internalBpm", -std::numeric_limits<float>::infinity());
        addParam(state, "gridDivision", std::numeric_limits<float>::quiet_NaN());
        addParam(state, "maxAttacksPerStep", std::numeric_limits<float>::infinity());
        addParam(state, "maxActiveVoices", -std::numeric_limits<float>::infinity());
        addParam(state, "gatePercent", std::numeric_limits<float>::quiet_NaN());
        addParam(state, "temporalSpread", std::numeric_limits<float>::infinity());
        CosmicStateMigration::migrate(state);
        expect(sameValue(valueOf(state, "timeMode"), 0.0f)
                   && sameValue(valueOf(state, "clockSource"), 0.0f)
                   && sameValue(valueOf(state, "internalBpm"), 120.0f)
                   && sameValue(valueOf(state, "gridDivision"), 2.0f)
                   && sameValue(valueOf(state, "maxAttacksPerStep"), 4.0f)
                   && sameValue(valueOf(state, "maxActiveVoices"), 16.0f)
                   && sameValue(valueOf(state, "gatePercent"), 70.0f)
                   && sameValue(valueOf(state, "temporalSpread"), 2.0f),
               "non-finite Time Field values recover field-specific defaults");
    }

    {
        auto state = makeState(4);
        addParam(state, "maxAttacksPerStep", 3.6f);
        addParam(state, "maxActiveVoices", 8.4f);
        CosmicStateMigration::migrate(state);
        expect(sameValue(valueOf(state, "maxAttacksPerStep"), 4.0f)
                   && sameValue(valueOf(state, "maxActiveVoices"), 8.0f),
               "integer Time Field values are rounded after safe range clamping");
    }

    {
        auto state = makeState();
        state.setProperty("cosmicMicrowaveSchema",
                          std::numeric_limits<double>::quiet_NaN(), nullptr);
        CosmicStateMigration::migrate(state);
        expect((int) state.getProperty("cosmicMicrowaveSchema", 0)
                   == CosmicStateMigration::currentSchema
                   && sameValue(valueOf(state, "timeMode"), 0.0f),
               "non-finite schema metadata safely falls back to legacy migration");
    }

    {
        auto state = makeState(CosmicStateMigration::currentSchema + 1);
        addParam(state, "timeMode", 2.0f);
        const int childrenBefore = state.getNumChildren();
        CosmicStateMigration::migrate(state);
        expect((int) state.getProperty("cosmicMicrowaveSchema", 0)
                   == CosmicStateMigration::currentSchema + 1
                   && state.getNumChildren() == childrenBefore
                   && sameValue(valueOf(state, "timeMode"), 2.0f),
               "future schemas are not destructively downgraded");
    }

    std::cout << "\nSummary: " << (failed == 0 ? "ok" : "failed") << "\n";
    return failed == 0 ? 0 : 1;
}
