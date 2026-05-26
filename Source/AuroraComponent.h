#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <vector>
#include "PartialEngine.h"

/*
    AuroraComponent - audience map + scale strip.

    The primary visual is a compact stage/audience map: inactive seats form a
    dark venue grid, active seats glow by pitch/X position, and the lower strip
    shows the selected scale range with current activity.
*/
class AuroraComponent : public juce::Component, private juce::Timer
{
public:
    explicit AuroraComponent (PartialEngine& engine);
    ~AuroraComponent() override;

    void paint   (juce::Graphics&) override;
    void resized() override;

    enum class Palette { Spectrum, Warm, Mono };
    void setPalette (Palette p) { palette = p; }

private:
    void timerCallback() override;
    juce::Colour bandColour (float t) const;

    PartialEngine& engine;
    Palette palette { Palette::Spectrum };

    static constexpr int N_BANDS = 96;
    static constexpr int N_STARS = 220;

    struct Star
    {
        float x, y, r, baseAlpha, twPhase, twSpeed;
    };

    struct FloatParticle
    {
        float x, y, vy, life, decay, radius;
        juce::Colour colour;
    };

    std::array<Star, N_STARS> stars;
    std::array<float, N_BANDS> spectrum {};
    std::array<float, N_BANDS> targets  {};

    std::vector<FloatParticle> particles;
    juce::Random rng;
    float tSec = 0.0f;
    juce::int64 lastMs = 0;
};
