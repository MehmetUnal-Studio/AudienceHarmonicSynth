#include "../Source/PartialEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#ifndef AUDIENCE_SYNTH_SOURCE_SAMPLES_PATH
#define AUDIENCE_SYNTH_SOURCE_SAMPLES_PATH ""
#endif

namespace
{
    constexpr double kSampleRate = 48000.0;

    struct Metrics
    {
        bool finite = true;
        float peak = 0.0f;
        int frames = 0;
        int activeVoices = 0;
        double wallSeconds = 0.0;
        double worstBlockMs = 0.0;
    };

    juce::File pianoDreamDir()
    {
        return juce::File(AUDIENCE_SYNTH_SOURCE_SAMPLES_PATH).getChildFile("Piano Dream");
    }

    void configureEngine (PartialEngine& engine, int blockSize)
    {
        engine.prepare(kSampleRate, blockSize);
        engine.attackMs.store(8.0f);
        engine.releaseMs.store(120.0f);
        engine.layerMix.store(1.0f);
        engine.masterGain.store(1.0f);
        engine.energyMacro.store(1.0f);
        engine.motionMacro.store(0.65f);
        engine.toneMacro.store(0.55f);
        engine.spaceMacro.store(0.35f);
        engine.movement.store(0.7f);
        engine.grainDensity.store(0.85f);
        engine.positionJitter.store(0.6f);
        engine.stereoSpread.store(0.85f);
        engine.pitchSpread.store(4.0f);
        engine.reverbAmount.store(0.35f);
        engine.delayAmount.store(0.25f);
        engine.wetDry.store(0.85f);
        engine.tapeDrive.store(0.2f);
        engine.samplePlaybackMode.store(1);
        engine.scaleRootMidi.store(36);
        engine.scaleMode.store(0);
        engine.scaleOctaves.store(4);
    }

    void triggerCrowd (PartialEngine& engine, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            const int row = i % PartialEngine::MAX_ROWS;
            const int col = (i / PartialEngine::MAX_ROWS) % PartialEngine::MAX_COLS;
            const float x = (float) (i % 127) / 126.0f;
            const float y = 0.55f + 0.45f * (float) (i % 17) / 16.0f;
            engine.setY(row, col, y);
            engine.setX(row, col, x);
            engine.setOn(row, col, true);
        }
    }

    Metrics renderScenario (PartialEngine& engine, int blockSize, double audioSeconds)
    {
        const int total = std::max(1, (int) std::round(audioSeconds * kSampleRate));
        std::vector<float> left((size_t) blockSize);
        std::vector<float> right((size_t) blockSize);
        Metrics metrics;

        const auto wallStart = std::chrono::steady_clock::now();
        for (int done = 0; done < total; done += blockSize)
        {
            const int n = std::min(blockSize, total - done);
            const auto blockStart = std::chrono::steady_clock::now();
            engine.render(left.data(), right.data(), n);
            const auto blockEnd = std::chrono::steady_clock::now();

            metrics.worstBlockMs = std::max(metrics.worstBlockMs,
                                            std::chrono::duration<double, std::milli>(blockEnd - blockStart).count());

            for (int i = 0; i < n; ++i)
            {
                const float l = left[(size_t) i];
                const float r = right[(size_t) i];
                metrics.finite = metrics.finite && std::isfinite(l) && std::isfinite(r);
                metrics.peak = std::max(metrics.peak, std::max(std::abs(l), std::abs(r)));
            }
            metrics.frames += n;
        }

        metrics.wallSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - wallStart).count();
        metrics.activeVoices = engine.getActiveVoiceCount();
        return metrics;
    }

    bool runScenario (const char* name, int polyMode, int participants, int blockSize,
                      double audioSeconds, int grainShape, int signatureMode)
    {
        PartialEngine engine;
        configureEngine(engine, blockSize);
        engine.polyphonyMode.store(polyMode);
        engine.grainShape.store(grainShape);
        engine.signatureMode.store(signatureMode);

        if (engine.loadSampleLibrary(pianoDreamDir()) <= 0)
        {
            std::cout << "FAIL  " << name << "  samples unavailable\n";
            return false;
        }

        triggerCrowd(engine, participants);
        const auto metrics = renderScenario(engine, blockSize, audioSeconds);
        const double realtimeRatio = metrics.wallSeconds > 0.0 ? audioSeconds / metrics.wallSeconds : 999.0;
        const bool ok = metrics.finite
                     && metrics.peak <= 1.0001f
                     && metrics.activeVoices <= engine.getVoiceLimit()
                     && metrics.wallSeconds < audioSeconds * 50.0;

        std::cout << (ok ? "PASS  " : "FAIL  ") << name
                  << "  participants=" << participants
                  << " voices=" << metrics.activeVoices << "/" << engine.getVoiceLimit()
                  << " unison=" << engine.getAdaptiveUnisonCount()
                  << " block=" << blockSize
                  << " realtime=" << realtimeRatio << "x"
                  << " worstBlockMs=" << metrics.worstBlockMs
                  << " peak=" << metrics.peak
                  << "\n";
        return ok;
    }
}

int main()
{
    int failed = 0;
    failed += runScenario("Normal 60 participants", 0, 60, 128, 0.25, 0, 0) ? 0 : 1;
    failed += runScenario("High 130 pulse envelope", 1, 130, 128, 0.20, 3, 3) ? 0 : 1;
    failed += runScenario("Ultra 512 participants", 2, 512, 256, 0.12, 0, 1) ? 0 : 1;
    failed += runScenario("Ultra 1024 participants", 2, 1024, 256, 0.08, 0, 2) ? 0 : 1;

    std::cout << "\nSummary: " << (failed == 0 ? "ok" : "failed") << "\n";
    return failed == 0 ? 0 : 1;
}
