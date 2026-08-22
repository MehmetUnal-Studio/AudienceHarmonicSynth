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

    std::cout << "\nSummary: " << (failed == 0 ? "ok" : "failed") << "\n";
    return failed == 0 ? 0 : 1;
}
