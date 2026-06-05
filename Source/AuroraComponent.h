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

    // Pushed by the editor each frame so the empty-state hint can name the live
    // OSC/UDP port and reflect whether the bridge is actually listening. Stored
    // (not queried) because AuroraComponent only holds the engine, while the
    // port/running state live on the processor.
    void setListeningInfo (int udpPort, bool running) noexcept
    {
        listenPort = udpPort;
        listening  = running;
    }

private:
    void timerCallback() override;
    juce::Colour bandColour (float t) const;

    // Active element identity tint (FIX 8): when the Element Spectral engine is
    // selected, the visualization is biased toward the element's reference
    // wavelength colour. Returns true (and fills `out`) only in element mode;
    // Sample-Library mode leaves the existing spectrum palette untouched.
    bool elementTint (juce::Colour& out) const;

    PartialEngine& engine;
    Palette palette { Palette::Spectrum };

    int  listenPort = 0;
    bool listening  = false;

    static constexpr int N_BANDS = 96;
    static constexpr int N_STARS = 220;

    // Per-frame energy scratch buffers, reused across paints (allocated once,
    // cleared each frame) instead of being stack-allocated and re-initialised
    // inside paint(). MIDI_ENERGY_SIZE covers all MIDI notes; STEP_ENERGY_SIZE
    // is a generous upper bound on the scale-table size we may index.
    static constexpr int MIDI_ENERGY_SIZE = 128;
    static constexpr int STEP_ENERGY_SIZE = 8192;

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

    // Reused per-paint scratch buffers (see *_ENERGY_SIZE above).
    std::array<float, MIDI_ENERGY_SIZE> midiEnergy {};
    std::array<float, STEP_ENERGY_SIZE> stepEnergy {};

    std::vector<FloatParticle> particles;
    juce::Random rng;
    float tSec = 0.0f;
    juce::int64 lastMs = 0;
};
