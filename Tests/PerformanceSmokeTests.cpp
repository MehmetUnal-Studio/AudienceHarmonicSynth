#include "../Source/PartialEngine.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
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

    // ---- B10: graceful degradation when the sample library is ABSENT -----
    //
    // These perf smoke scenarios need a loaded sample library to render through
    // the sample voice path. When the bundled "Piano Dream" library is present
    // (the normal case here) every scenario runs against it unchanged. When it
    // is missing we synthesise a tiny stand-in library (short decaying sines,
    // named A0.wav..C8.wav) into a temp dir so the render/perf path still runs;
    // the perf bounds asserted below (finite, peak, voice cap, wall-clock) do
    // not depend on the sample timbre. Only if even the synthetic library
    // cannot be written do we SKIP the affected scenario instead of failing it.

    // Sustained 3 s tone (see SignalFlowTests for the full rationale): a long,
    // near-constant-amplitude sample with a couple of low harmonics so the
    // sample voice keeps feeding audible, bounded data for the whole render.
    bool writeSyntheticWav (const juce::File& file, double freqHz)
    {
        constexpr double sr      = 44100.0;
        constexpr double seconds = 3.0;
        const int        len     = (int) (sr * seconds);
        const int        attack  = (int) (sr * 0.005);
        const int        release = (int) (sr * 0.030);

        juce::AudioBuffer<float> buffer (1, len);
        auto* data = buffer.getWritePointer (0);
        const double twoPiF = 2.0 * juce::MathConstants<double>::pi * freqHz;
        for (int i = 0; i < len; ++i)
        {
            const double t = (double) i / sr;
            double s = std::sin (twoPiF * t)
                     + 0.30 * std::sin (2.0 * twoPiF * t)
                     + 0.15 * std::sin (3.0 * twoPiF * t);

            double env = 0.35;
            if (i < attack)               env *= (double) i / (double) attack;
            else if (i > len - release)   env *= (double) (len - i) / (double) release;

            data[i] = (float) (env * s / 1.45);
        }

        file.deleteFile();
        std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
        if (stream == nullptr)
            return false;

        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            format.createWriterFor (stream.get(), sr, 1, 16, {}, 0));
        if (writer == nullptr)
            return false;

        stream.release(); // writer now owns the stream
        return writer->writeFromAudioSampleBuffer (buffer, 0, len);
    }

    juce::File buildSyntheticLibrary()
    {
        static const char* const names[] =
            { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("SpektraSynthPerf_SynthSamples");
        dir.createDirectory();

        int written = 0;
        for (int midi = 21; midi <= 108; ++midi)
        {
            const int oct = midi / 12 - 1;
            const juce::String noteName =
                juce::String (names[midi % 12]) + juce::String (oct);
            const double freq = 440.0 * std::pow (2.0, (midi - 69) / 12.0);
            if (writeSyntheticWav (dir.getChildFile (noteName + ".wav"), freq))
                ++written;
        }

        return written > 0 ? dir : juce::File();
    }

    // Directory the scenarios load from: real Piano Dream if present, else a
    // synthetic stand-in (computed once). `usingSynthetic` reports the choice.
    const juce::File& effectiveSampleDir (bool& usingSynthetic)
    {
        static bool       synthetic = false;
        static juce::File resolved = []
        {
            auto real = pianoDreamDir();
            return real.isDirectory() ? real : juce::File();
        }();

        if (resolved == juce::File() && ! synthetic)
        {
            resolved  = buildSyntheticLibrary();
            synthetic = (resolved != juce::File());
        }

        usingSynthetic = synthetic;
        return resolved;
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

    enum class ScenarioResult { Pass, Fail, Skip };

    ScenarioResult runScenario (const char* name, int polyMode, int participants, int blockSize,
                                double audioSeconds, int grainShape, int signatureMode)
    {
        PartialEngine engine;
        configureEngine(engine, blockSize);
        engine.polyphonyMode.store(polyMode);
        engine.grainShape.store(grainShape);
        engine.signatureMode.store(signatureMode);

        // B10: load the bundled library if present, else a synthetic stand-in.
        bool usingSynthetic = false;
        const auto& sampleDir = effectiveSampleDir(usingSynthetic);

        if (sampleDir == juce::File())
        {
            // No real library AND synthetic generation failed: this scenario
            // cannot run. SKIP it (not a failure) with a clear reason.
            std::cout << "SKIP  " << name
                      << "  (no sample library available - real or synthetic)\n";
            return ScenarioResult::Skip;
        }

        if (engine.loadSampleLibrary(sampleDir) <= 0)
        {
            // The resolved directory exists but produced no usable samples.
            std::cout << "SKIP  " << name
                      << "  (sample library at " << sampleDir.getFullPathName().toStdString()
                      << " loaded no samples)\n";
            return ScenarioResult::Skip;
        }

        if (usingSynthetic)
            std::cout << "NOTE  " << name
                      << "  using synthetic stand-in sample library\n";

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
        return ok ? ScenarioResult::Pass : ScenarioResult::Fail;
    }
}

int main()
{
    int failed = 0;
    int skipped = 0;

    auto tally = [&] (ScenarioResult res)
    {
        if (res == ScenarioResult::Fail) ++failed;
        else if (res == ScenarioResult::Skip) ++skipped;
    };

    tally(runScenario("Normal 60 participants", 0, 60, 128, 0.25, 0, 0));
    tally(runScenario("High 130 pulse envelope", 1, 130, 128, 0.20, 3, 3));
    tally(runScenario("Ultra 512 participants", 2, 512, 256, 0.12, 0, 1));
    tally(runScenario("Ultra 1024 participants", 2, 1024, 256, 0.08, 0, 2));

    std::cout << "\nSummary: " << (failed == 0 ? "ok" : "failed")
              << " (" << skipped << " skipped)\n";
    return failed == 0 ? 0 : 1;
}
