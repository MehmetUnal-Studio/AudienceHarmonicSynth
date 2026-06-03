#include "AuroraComponent.h"
#include "UiText.h"
#include <cmath>

namespace
{
    // OKLCH stops from the design spec, pre-converted to sRGB.
    // (0.55,0.18,290) (0.62,0.20,250) (0.78,0.18,210) (0.78,0.22,160)
    // (0.85,0.20,130) (0.78,0.18,80)  (0.70,0.20,40)  (0.60,0.22,25)
    struct Stop { float t; juce::uint8 r, g, b; };
    constexpr std::array<Stop, 8> kSpectrumStops = { {
        { 0.00f, 0x6c, 0x47, 0xc8 },  // violet
        { 0.16f, 0x46, 0x66, 0xd6 },  // indigo
        { 0.32f, 0x4f, 0xae, 0xdb },  // cyan
        { 0.48f, 0x3a, 0xc4, 0x86 },  // green
        { 0.62f, 0xa6, 0xc4, 0x47 },  // y-green
        { 0.78f, 0xcc, 0xa9, 0x4d },  // yellow
        { 0.90f, 0xc9, 0x85, 0x5b },  // amber
        { 1.00f, 0xc4, 0x5a, 0x4d },  // red
    } };

    juce::Colour interpStops (float t)
    {
        if (t <= kSpectrumStops.front().t) return juce::Colour(kSpectrumStops.front().r, kSpectrumStops.front().g, kSpectrumStops.front().b);
        if (t >= kSpectrumStops.back ().t) return juce::Colour(kSpectrumStops.back ().r, kSpectrumStops.back ().g, kSpectrumStops.back ().b);
        for (size_t i = 0; i + 1 < kSpectrumStops.size(); ++i)
        {
            const auto& a = kSpectrumStops[i];
            const auto& b = kSpectrumStops[i + 1];
            if (t <= b.t)
            {
                const float k = (t - a.t) / (b.t - a.t);
                const auto lerp = [](juce::uint8 x, juce::uint8 y, float kk)
                { return (juce::uint8) std::round((float) x + ((float) y - (float) x) * kk); };
                return juce::Colour(lerp(a.r, b.r, k), lerp(a.g, b.g, k), lerp(a.b, b.b, k));
            }
        }
        return juce::Colours::white;
    }

    juce::Colour wavelengthColour (double wavelengthNm)
    {
        double r = 0.0;
        double g = 0.0;
        double b = 0.0;

        if (wavelengthNm >= 380.0 && wavelengthNm < 440.0)
        {
            r = -(wavelengthNm - 440.0) / (440.0 - 380.0);
            b = 1.0;
        }
        else if (wavelengthNm >= 440.0 && wavelengthNm < 490.0)
        {
            g = (wavelengthNm - 440.0) / (490.0 - 440.0);
            b = 1.0;
        }
        else if (wavelengthNm >= 490.0 && wavelengthNm < 510.0)
        {
            g = 1.0;
            b = -(wavelengthNm - 510.0) / (510.0 - 490.0);
        }
        else if (wavelengthNm >= 510.0 && wavelengthNm < 580.0)
        {
            r = (wavelengthNm - 510.0) / (580.0 - 510.0);
            g = 1.0;
        }
        else if (wavelengthNm >= 580.0 && wavelengthNm < 645.0)
        {
            r = 1.0;
            g = -(wavelengthNm - 645.0) / (645.0 - 580.0);
        }
        else if (wavelengthNm >= 645.0 && wavelengthNm <= 750.0)
        {
            r = 1.0;
        }

        double factor = 0.0;
        if (wavelengthNm >= 380.0 && wavelengthNm < 420.0)
            factor = 0.3 + 0.7 * (wavelengthNm - 380.0) / (420.0 - 380.0);
        else if (wavelengthNm >= 420.0 && wavelengthNm < 645.0)
            factor = 1.0;
        else if (wavelengthNm >= 645.0 && wavelengthNm <= 750.0)
            factor = 0.3 + 0.7 * (750.0 - wavelengthNm) / (750.0 - 645.0);

        return juce::Colour::fromFloatRGBA ((float) (r * factor),
                                            (float) (g * factor),
                                            (float) (b * factor),
                                            1.0f);
    }
}

AuroraComponent::AuroraComponent (PartialEngine& e) : engine(e)
{
    // initialize starfield
    for (auto& s : stars)
    {
        s.x         = rng.nextFloat();
        s.y         = rng.nextFloat();
        s.r         = rng.nextFloat() * 1.2f + 0.2f;
        s.baseAlpha = rng.nextFloat() * 0.7f + 0.15f;
        s.twPhase   = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        s.twSpeed   = rng.nextFloat() * 0.02f + 0.005f;
    }
    spectrum.fill(0.0f);
    targets .fill(0.0f);
    particles.reserve(256);
    lastMs = juce::Time::getMillisecondCounter();
    startTimerHz(30);
}

AuroraComponent::~AuroraComponent()
{
    stopTimer();
}

juce::Colour AuroraComponent::bandColour (float t) const
{
    // Reversed so left (low freq) = warm/red, right (high freq) = cool/violet.
    if (palette == Palette::Spectrum) return interpStops(1.0f - t);
    if (palette == Palette::Warm)
    {
        // approximate warm sweep
        const float L = 0.62f + 0.18f * std::sin(t * juce::MathConstants<float>::pi);
        const float H = 30.0f + 50.0f * t;
        const auto base = juce::Colour::fromHSV(H / 360.0f, 0.6f, L, 1.0f);
        return base;
    }
    const float L = 0.55f + 0.35f * (0.5f + 0.5f * std::sin(t * juce::MathConstants<float>::pi));
    return juce::Colour::fromHSV(200.0f / 360.0f, 0.6f, L, 1.0f);
}

void AuroraComponent::resized() {}

void AuroraComponent::timerCallback()
{
    const float W = (float) getWidth();
    const float H = (float) getHeight();
    const juce::int64 nowMs = juce::Time::getMillisecondCounter();
    const float dt = juce::jlimit(0.001f, 0.1f, (float) (nowMs - lastMs) * 0.001f);
    lastMs = nowMs;
    tSec += dt;

    for (auto& s : stars)
        s.twPhase += s.twSpeed;

    // Read the engine's per-voice spectral histogram and convert it
    // into our 96-band aurora target. Engine publishes
    // PartialEngine::AURORA_BANDS bins; we have the same N_BANDS here.
    for (int i = 0; i < N_BANDS; ++i)
    {
        float t = engine.getAuroraBand(i);
        // exaggerate so individual voices read clearly
        t = juce::jmin(1.5f, t * 3.5f);
        // shimmer
        t += 0.025f * (rng.nextFloat() - 0.5f);
        targets[(size_t) i] = juce::jmax(0.0f, t);
    }

    const float smoothK = juce::jmin(1.0f, dt * 6.0f);
    for (int i = 0; i < N_BANDS; ++i)
        spectrum[(size_t) i] += (targets[(size_t) i] - spectrum[(size_t) i]) * smoothK;

    if (W > 0.0f && H > 0.0f)
    {
        const int active = engine.getActiveVoiceCount();
        const float colW = W / (float) N_BANDS;
        if (active > 0)
        {
            const float spawnProb = juce::jmin(1.0f, dt * (float) active * 1.8f);
            if (rng.nextFloat() < spawnProb && particles.size() < 256)
            {
                const int   lane = rng.nextInt(N_BANDS);
                const float ti   = (float) lane / (float) (N_BANDS - 1);
                FloatParticle p;
                p.x      = ((float) lane + 0.5f) * colW + (rng.nextFloat() - 0.5f) * colW * 0.6f;
                p.y      = H * (0.4f + rng.nextFloat() * 0.2f);
                p.vy     = -(20.0f + rng.nextFloat() * 60.0f);
                p.life   = 1.0f;
                p.decay  = 0.4f + rng.nextFloat() * 0.6f;
                p.radius = 1.0f + rng.nextFloat() * 1.8f;
                p.colour = bandColour(ti).interpolatedWith(juce::Colours::white, 0.4f);
                particles.push_back(p);
            }
        }

        for (auto it = particles.begin(); it != particles.end(); )
        {
            it->y    += it->vy * dt;
            it->life -= dt * it->decay;
            if (it->life <= 0.0f) it = particles.erase(it);
            else ++it;
        }
    }

    repaint();
}

void AuroraComponent::paint (juce::Graphics& g)
{
    const float W = (float) getWidth();
    const float H = (float) getHeight();
    if (W <= 0 || H <= 0) return;

    // ---- background ----
    juce::ColourGradient bg(juce::Colour(0xff0b0f16), W * 0.5f, H * 0.0f,
                            juce::Colour(0xff030608), W * 0.5f, H * 1.1f,
                            false);
    g.setGradientFill(bg);
    g.fillRect(getLocalBounds());

    auto area = getLocalBounds().reduced(10, 8);
    auto header = area.removeFromTop(24);
    const int scaleStripH = H < 260.0f
                          ? juce::jlimit(90, 144, (int) std::round(H * 0.46f))
                          : juce::jlimit(150, 244, (int) std::round(H * 0.52f));
    auto scaleStrip = area.removeFromBottom(scaleStripH);
    area.removeFromBottom(8);
    auto map = area;

    // ---- panel labels ----
    {
        g.setColour(juce::Colour(0xffd9dde6));
        g.setFont(juce::Font(juce::FontOptions(11.5f)).boldened());
        g.drawText("AUDIENCE MAP", header.removeFromLeft(140), juce::Justification::centredLeft);

        g.setColour(juce::Colour(0xff707681));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText(engine.getScaleRangeName(), header, juce::Justification::centredLeft);

        const juce::String right = "ACTIVE " + juce::String(engine.getRegisteredSeatCount())
                                 + "   DOMINANT " + engine.getDominantSampleName();
        g.setColour(juce::Colour(0xff8e948f));
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain)));
        g.drawText(right, getWidth() - 240, 9, 220, 16, juce::Justification::centredRight);
    }

    // ---- audience seat grid ----
    {
        g.setColour(juce::Colour(0xff05080c));
        g.fillRoundedRectangle(map.toFloat(), 6.0f);
        g.setColour(juce::Colour(0xff121923));
        g.drawRoundedRectangle(map.toFloat(), 6.0f, 1.0f);

        auto grid = map.reduced(10, 10);
        const int rows = PartialEngine::MAX_ROWS;
        const int cols = PartialEngine::MAX_COLS;
        const float cellW = (float) grid.getWidth()  / (float) cols;
        const float cellH = (float) grid.getHeight() / (float) rows;
        const float seatR = juce::jlimit(1.4f, 4.2f, std::min(cellW, cellH) * 0.33f);
        // Active seats are the hero of the map: render them well above the tiny
        // inactive-dot size so a live audience reads clearly from a distance.
        const float activeBaseR = juce::jlimit(6.0f, 16.0f, std::max(cellW, cellH) * 0.62f);

        g.setColour(juce::Colour(0x15161d25));
        for (int r = 0; r <= rows; ++r)
        {
            const float y = (float) grid.getY() + (float) r * cellH;
            g.drawLine((float) grid.getX(), y, (float) grid.getRight(), y, 1.0f);
        }

        for (int row = 0; row < rows; ++row)
        {
            for (int col = 0; col < cols; ++col)
            {
                const float cx = (float) grid.getX() + ((float) col + 0.5f) * cellW;
                const float cy = (float) grid.getY() + ((float) row + 0.5f) * cellH;

                if (! engine.isSeatActive(row, col))
                {
                    if ((col % 2) == 0 || cellW >= 7.0f)
                    {
                        g.setColour(juce::Colour(0xff0b1017));
                        g.fillEllipse(cx - seatR, cy - seatR, seatR * 2.0f, seatR * 2.0f);
                        g.setColour(juce::Colour(0xff151c25));
                        g.drawEllipse(cx - seatR, cy - seatR, seatR * 2.0f, seatR * 2.0f, 0.7f);
                    }
                    continue;
                }

                const float xNorm = engine.getSeatX(row, col);
                const float amp = juce::jlimit(0.0f, 1.0f, engine.getSeatY(row, col));
                const auto colr = bandColour(xNorm).interpolatedWith(juce::Colour(0xffeff7ff), 0.16f);
                const float activeR = activeBaseR * (0.82f + amp * 0.5f);

                // soft outer glow
                g.setColour(colr.withAlpha(0.10f + amp * 0.18f));
                g.fillEllipse(cx - activeR * 2.4f, cy - activeR * 2.4f,
                              activeR * 4.8f, activeR * 4.8f);
                // glowing core
                g.setColour(colr.withAlpha(0.95f));
                g.fillEllipse(cx - activeR, cy - activeR, activeR * 2.0f, activeR * 2.0f);
                // hot centre
                g.setColour(juce::Colours::white.withAlpha(0.45f));
                g.fillEllipse(cx - activeR * 0.32f, cy - activeR * 0.32f,
                              activeR * 0.64f, activeR * 0.64f);
            }
        }

        // Pitch landmarks
        g.setColour(juce::Colour(0xff3d434d));
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain)));
        g.drawText("LOW PITCH / STAGE", map.getX() + 8, map.getY() + 6, 140, 12, juce::Justification::left);
        g.drawText("MID", map.getCentreX() - 28, map.getY() + 6, 56, 12, juce::Justification::centred);
        g.drawText("HIGH PITCH / STAGE", map.getRight() - 142, map.getY() + 6, 134, 12, juce::Justification::right);
    }

    // ---- bottom spectral scale strip ----
    {
        const int n = engine.getScaleTableSize();
        const bool spectralScale = engine.isSpectralScale();
        g.setColour(juce::Colour(0xff020304));
        g.fillRoundedRectangle(scaleStrip.toFloat(), 5.0f);
        g.setColour(juce::Colour(0xff151b24));
        g.drawRoundedRectangle(scaleStrip.toFloat(), 5.0f, 1.0f);

        auto strip = scaleStrip.reduced(8, 6);
        juce::Rectangle<int> wavelengthWheel;
        if (spectralScale && strip.getWidth() >= 620 && strip.getHeight() >= 124)
        {
            const int wheelSize = juce::jlimit(118, 178, strip.getHeight() - 4);
            auto wheelSlot = strip.removeFromLeft(wheelSize + 12);
            wavelengthWheel = wheelSlot.withTrimmedRight(12).reduced(1);
        }

        auto ruler = strip.removeFromTop(18);
        auto noteAxis = strip.removeFromBottom(spectralScale ? 58 : 18);
        auto lines = strip.reduced(0, 4);

        juce::ColourGradient floorGlow(juce::Colours::transparentBlack,
                                       (float) scaleStrip.getCentreX(), (float) lines.getY(),
                                       juce::Colour(0xff10151a).withAlpha(0.72f),
                                       (float) scaleStrip.getCentreX(), (float) scaleStrip.getBottom(),
                                       false);
        g.setGradientFill(floorGlow);
        g.fillRoundedRectangle(scaleStrip.toFloat(), 5.0f);

        g.setColour(juce::Colour(0xff070a0f));
        g.fillRect(noteAxis);

        auto stepValue = [&] (int step)
        {
            if (spectralScale)
                return std::log2(juce::jmax(1.0, engine.getScaleFrequencyHz(step)));

            return (double) engine.getScaleMidi(step);
        };

        double loValue = 0.0;
        double hiValue = 1.0;
        if (n > 0)
        {
            loValue = hiValue = stepValue(0);
            for (int i = 1; i < n; ++i)
            {
                const double v = stepValue(i);
                loValue = juce::jmin(loValue, v);
                hiValue = juce::jmax(hiValue, v);
            }
        }

        const double valueRange = juce::jmax(0.0001, hiValue - loValue);
        auto tForStep = [&] (int step)
        {
            return (float) ((stepValue(step) - loValue) / valueRange);
        };

        const int loMidi = n > 0 ? engine.getScaleMidi(0) : -1;

        // midiEnergy/stepEnergy are reused members (see header) rather than
        // per-paint stack arrays. Clear them to match the previous semantics:
        // midiEnergy is fully zeroed (every entry is accumulated and read), while
        // stepEnergy only needs its [0, n) range cleared - that is the only range
        // ever read, so stale values beyond n are harmless and we skip zeroing
        // all 8192 floats (32 KB) on every paint.
        midiEnergy.fill(0.0f);
        std::fill_n(stepEnergy.begin(), (size_t) juce::jmin(n, (int) stepEnergy.size()), 0.0f);
        for (int i = 0; i < engine.getMaxVoices(); ++i)
        {
            const float amp = engine.getVoiceAmp(i);
            const int midi = engine.getVoiceMidi(i);
            if (amp > 0.015f && midi >= 0 && midi < (int) midiEnergy.size())
                midiEnergy[(size_t) midi] += amp;

            const int step = engine.getVoiceScaleStep(i);
            if (amp > 0.015f && step >= 0 && step < juce::jmin(n, (int) stepEnergy.size()))
                stepEnergy[(size_t) step] += amp;
        }

        float maxScaleEnergy = 0.0f;
        int dominantMidi = -1;
        for (int i = 0; i < n; ++i)
        {
            const int midi = engine.getScaleMidi(i);
            if (midi < 0 || midi >= (int) midiEnergy.size())
                continue;

            const float e = spectralScale && i < (int) stepEnergy.size()
                ? stepEnergy[(size_t) i]
                : midiEnergy[(size_t) midi];
            if (e > maxScaleEnergy)
            {
                maxScaleEnergy = e;
                dominantMidi = midi;
            }
        }

        auto drawWavelengthWheel = [&] (juce::Rectangle<int> bounds)
        {
            if (bounds.isEmpty())
                return;

            const int stepsPerOctave = juce::jlimit(0, n, engine.getScaleStepsPerOctave());
            if (stepsPerOctave <= 0)
                return;

            g.setColour(juce::Colour(0xff030608).withAlpha(0.84f));
            g.fillRoundedRectangle(bounds.toFloat(), 5.0f);
            g.setColour(juce::Colour(0xff171f2a));
            g.drawRoundedRectangle(bounds.toFloat(), 5.0f, 1.0f);

            g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 7.5f, juce::Font::plain)));
            g.setColour(juce::Colour(0xff69717d));
            g.drawText("WAVELENGTH WHEEL", bounds.getX() + 7, bounds.getY() + 4,
                       bounds.getWidth() - 14, 10, juce::Justification::centredLeft);
            g.drawText("380-750 nm", bounds.getX() + 7, bounds.getBottom() - 13,
                       bounds.getWidth() - 14, 10, juce::Justification::centredRight);

            auto circleArea = bounds.reduced(8, 15);
            const int circleSize = juce::jmin(circleArea.getWidth(), circleArea.getHeight());
            if (circleSize <= 20)
                return;

            circleArea = juce::Rectangle<int>(circleArea.getCentreX() - circleSize / 2,
                                              circleArea.getCentreY() - circleSize / 2,
                                              circleSize, circleSize);

            const float cx = (float) circleArea.getCentreX();
            const float cy = (float) circleArea.getCentreY();
            const float radius = (float) circleSize * 0.5f - 3.0f;

            g.setColour(juce::Colour(0xff111821).withAlpha(0.58f));
            g.fillEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);

            for (int ring = 1; ring <= 3; ++ring)
            {
                const float rr = radius * (float) ring / 3.0f;
                g.setColour(juce::Colour(0xff252d38).withAlpha(ring == 3 ? 0.74f : 0.22f));
                g.drawEllipse(cx - rr, cy - rr, rr * 2.0f, rr * 2.0f, ring == 3 ? 1.1f : 0.6f);
            }

            const auto twoPi = juce::MathConstants<float>::twoPi;
            constexpr double startNm = 380.0;
            constexpr double endNm = 750.0;
            constexpr double nmRange = endNm - startNm;

            for (int i = 0; i < stepsPerOctave; ++i)
            {
                const double nm = engine.getScaleLineWavelengthNm(i);
                if (nm < startNm || nm > endNm)
                    continue;

                float degreeActivity = 0.0f;
                for (int step = i; step < n; step += stepsPerOctave)
                {
                    if (step >= 0 && step < (int) stepEnergy.size())
                        degreeActivity += stepEnergy[(size_t) step];
                }

                const float activeNorm = maxScaleEnergy > 0.0f ? juce::jlimit(0.0f, 1.0f, degreeActivity / maxScaleEnergy)
                                                               : 0.0f;
                const bool active = degreeActivity > 0.015f;
                const float amp = juce::jlimit(0.0f, 1.0f, engine.getScaleLineAmplitude(i));
                const float angle = (float) ((nm - startNm) / nmRange) * twoPi;
                const float endpointX = cx - radius * std::cos(angle);
                const float endpointY = cy + radius * std::sin(angle);
                const auto colour = wavelengthColour(nm);

                g.setColour(colour.withAlpha(active ? 0.92f : 0.44f + amp * 0.32f));
                g.drawLine(cx, cy, endpointX, endpointY, active ? 1.8f + activeNorm * 1.8f : 1.15f + amp * 0.7f);

                if (active)
                {
                    g.setColour(juce::Colours::white.withAlpha(0.58f));
                    g.drawLine(cx, cy, endpointX, endpointY, 2.4f + activeNorm * 1.4f);
                }
            }

            g.setColour(juce::Colour(0xffdce6f4).withAlpha(0.78f));
            g.fillEllipse(cx - 2.0f, cy - 2.0f, 4.0f, 4.0f);
        };

        drawWavelengthWheel(wavelengthWheel);

        auto xForT = [&] (float t)
        {
            return (float) lines.getX() + juce::jlimit(0.0f, 1.0f, t) * (float) lines.getWidth();
        };

        auto drawBeam = [&] (float x, float width, float alpha, juce::Colour colour, float glow)
        {
            const float top = (float) lines.getY();
            const float height = (float) lines.getHeight();

            if (glow > 0.0f)
            {
                for (int layer = 2; layer >= 1; --layer)
                {
                    const float w = width + glow * (float) layer;
                    juce::ColourGradient halo(colour.withAlpha(0.0f), x, top,
                                              colour.withAlpha(alpha * 0.12f / (float) layer), x, top + height * 0.45f,
                                              false);
                    halo.addColour(1.0, colour.withAlpha(0.0f));
                    g.setGradientFill(halo);
                    g.fillRoundedRectangle(x - w * 0.5f, top, w, height, w * 0.5f);
                }
            }

            juce::ColourGradient beam(colour.withAlpha(0.0f), x, top,
                                      colour.withAlpha(alpha), x, top + height * 0.52f,
                                      false);
            beam.addColour(1.0, colour.withAlpha(0.0f));
            g.setGradientFill(beam);
            g.fillRoundedRectangle(x - width * 0.5f, top, width, height, width * 0.5f);
        };

        g.setColour(juce::Colour(0xff222832));
        g.drawLine((float) ruler.getX(), (float) ruler.getBottom() - 1.0f,
                   (float) ruler.getRight(), (float) ruler.getBottom() - 1.0f, 1.0f);

        auto formatHz = [] (double hz)
        {
            return hz >= 1000.0 ? juce::String(hz / 1000.0, 2) + " kHz"
                                : juce::String(hz, 2) + " Hz";
        };

        if (spectralScale)
        {
            const int tickStride = n > 96 ? juce::jmax(1, n / 96) : 1;
            for (int i = 0; i < n; i += tickStride)
            {
                const float x = xForT(tForStep(i));
                g.setColour(juce::Colour(0xff59616d).withAlpha(0.85f));
                g.drawLine(x, (float) ruler.getBottom() - 8.0f, x, (float) ruler.getBottom(), 1.0f);
            }

            g.setColour(juce::Colour(0xff5c636d));
            g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain)));
            g.drawText("Hz", scaleStrip.getX() + 7, ruler.getY(), 20, 10, juce::Justification::left);
        }
        else
        {
            const int minorStep = scaleStrip.getWidth() > 900 ? 10 : 25;
            for (int nm = 400; nm <= 750; nm += minorStep)
            {
                const bool major = (nm % 50) == 0;
                const float t = (float) (nm - 400) / 350.0f;
                const float x = xForT(t);
                const float tickH = major ? 8.0f : 4.0f;
                g.setColour((major ? juce::Colour(0xff59616d) : juce::Colour(0xff343b46)).withAlpha(0.80f));
                g.drawLine(x, (float) ruler.getBottom() - tickH, x, (float) ruler.getBottom(), 1.0f);

                if (major)
                {
                    g.setColour(juce::Colour(0xff8d929c));
                    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 8.5f, juce::Font::plain)));
                    g.drawText(juce::String(nm), (int) (x - 22.0f), ruler.getY(), 44, 10,
                               juce::Justification::centred);
                }
            }

            g.setColour(juce::Colour(0xff5c636d));
            g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain)));
            g.drawText("nm", scaleStrip.getX() + 7, ruler.getY(), 20, 10, juce::Justification::left);
        }

        for (int i = 0; i < n; ++i)
        {
            const int midi = engine.getScaleMidi(i);
            if (midi < 0) continue;
            const float t = tForStep(i);
            const float activity = spectralScale && i < (int) stepEnergy.size()
                                 ? stepEnergy[(size_t) i]
                                 : ((midi >= 0 && midi < (int) midiEnergy.size())
                                        ? midiEnergy[(size_t) midi] : 0.0f);
            const float activeNorm = maxScaleEnergy > 0.0f ? juce::jlimit(0.0f, 1.0f, activity / maxScaleEnergy)
                                                           : 0.0f;
            const auto col = interpStops(1.0f - t);
            const bool active = activity > 0.015f;
            const float alpha = active ? 0.62f + activeNorm * 0.34f : 0.24f;
            const float lineW = active ? 1.8f + activeNorm * 2.6f : 1.25f;
            const float x = xForT(t);

            drawBeam(x, lineW, alpha, col, active ? 10.0f + activeNorm * 20.0f : 2.0f);

            if (! active && scaleStrip.getWidth() > 1100)
            {
                const float ghostOffset = 1.8f + (float) ((midi * 17) % 7);
                drawBeam(x + ghostOffset, 0.65f, alpha * 0.42f, col, 0.0f);
            }
        }

        g.setColour(juce::Colour(0xff242a33));
        g.drawLine((float) noteAxis.getX(), (float) noteAxis.getY(),
                   (float) noteAxis.getRight(), (float) noteAxis.getY(), 1.0f);

        if (spectralScale)
        {
            g.setColour(juce::Colour(0xff5c636d));
            g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain)));
            g.drawText("AUDIO FREQ", scaleStrip.getX() + 8, noteAxis.getY() + 3, 72, 10, juce::Justification::left);

            std::vector<juce::Rectangle<int>> usedLabels;
            const int labelStride = n > 96 ? juce::jmax(1, (n + 95) / 96) : 1;
            const int lanes = juce::jlimit(2, 4, (noteAxis.getHeight() - 6) / 13);
            for (int i = 0; i < n; i += labelStride)
            {
                const float x = xForT(tForStep(i));
                const auto label = formatHz(engine.getScaleFrequencyHz(i));
                const int labelW = juce::jlimit(52, 72, (int) label.length() * 5 + 12);
                juce::Rectangle<int> box((int) std::round(x) - labelW / 2,
                                         noteAxis.getY() + 14 + (i % lanes) * 12,
                                         labelW, 11);

                for (int pass = 0; pass < lanes; ++pass)
                {
                    bool overlaps = false;
                    for (const auto& r : usedLabels)
                        overlaps = overlaps || r.intersects(box);

                    if (! overlaps)
                        break;

                    box.setY(noteAxis.getY() + 14 + ((i + pass + 1) % lanes) * 12);
                }

                usedLabels.push_back(box);

                g.setColour(juce::Colour(0xff05080c).withAlpha(0.82f));
                g.fillRoundedRectangle(box.toFloat(), 3.0f);
                g.setColour(juce::Colour(0xff6b717c));
                g.drawRoundedRectangle(box.toFloat(), 3.0f, 0.6f);
                g.setColour(juce::Colour(0xffdce6f4));
                g.drawText(label, box, juce::Justification::centred);
            }
        }
        else
        {
            for (int i = 0; i < n; ++i)
            {
                const int midi = engine.getScaleMidi(i);
                if (midi < 0 || ((midi - loMidi) % 12) != 0)
                    continue;

                const float x = xForT(tForStep(i));
                g.setColour(juce::Colour(0xff39414c));
                g.drawLine(x, (float) noteAxis.getY(), x, (float) noteAxis.getY() + 4.0f, 1.0f);
                g.setColour(juce::Colour(0xff6b717c));
                g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain)));
                g.drawText(UiText::midiNoteName(midi), (int) (x - 18.0f), noteAxis.getY() + 3, 36, 10,
                           juce::Justification::centred);
            }
        }

        if (! spectralScale && dominantMidi >= 0)
        {
            float t = 0.0f;
            for (int i = 0; i < n; ++i)
                if (engine.getScaleMidi(i) == dominantMidi)
                {
                    t = tForStep(i);
                    break;
                }
            const float x = xForT(t);
            g.setColour(juce::Colour(0xffffc266));
            g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain)).boldened());
            g.drawText("DOM " + UiText::midiNoteName(dominantMidi), (int) (x - 32.0f), lines.getY() + 2, 64, 12,
                       juce::Justification::centred);
            g.drawLine(x, (float) lines.getY() + 15.0f, x, (float) lines.getY() + 21.0f, 1.0f);
        }

        g.setColour(juce::Colour(0xff5c636d));
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain)));
        if (spectralScale)
            g.drawText("SPECTRAL SCALE", scaleStrip.getRight() - 112, noteAxis.getY() + 3, 104, 10, juce::Justification::right);
        else
            g.drawText("SPECTRAL SCALE", scaleStrip.getX() + 8, noteAxis.getY() + 3, 102, 10, juce::Justification::left);
    }

    // ---- sparse per-voice labels ----
    {
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain)));

        for (int i = 0; i < engine.getMaxVoices(); ++i)
        {
            const float a = engine.getVoiceAmp(i);
            if (a < 0.08f || (i % 3) != 0) continue;

            const int row  = engine.getVoiceSeatRow(i);
            const int col  = engine.getVoiceSeatCol(i);
            if (row < 0 || col < 0) continue;

            const float x = 20.0f + engine.getSeatX(row, col) * (W - 40.0f);
            const float y = 42.0f + ((float) row / (float) PartialEngine::MAX_ROWS) * (H - 104.0f);
            const char letter = (char)('A' + juce::jlimit(0, 25, row));
            const juce::String label = juce::String::charToString((juce::juce_wchar) letter)
                                      + juce::String(col);
            g.setColour(juce::Colours::white.withAlpha(0.20f + juce::jlimit(0.0f, 0.5f, a)));
            g.drawText(label, (int)(x - 18), (int)(y - 7), 36, 12, juce::Justification::centred);
        }
    }
}
