#include "../Source/PartialEngine.h"
#include "../Source/SampleLibrary.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

// PartialEngine owns several large fixed real-time arrays. Keep test instances
// on the heap so this long Debug test does not reserve every scoped instance in
// one main-thread stack frame.
#define HEAP_PARTIAL_ENGINE(name)                         \
    auto name##Storage = std::make_unique<PartialEngine>(); \
    auto& name = *name##Storage

#ifndef AUDIENCE_SYNTH_SOURCE_SAMPLES_PATH
#define AUDIENCE_SYNTH_SOURCE_SAMPLES_PATH ""
#endif

namespace
{
    constexpr double kSampleRate = 44100.0;
    constexpr int kBlockSize = 256;

    struct Stats
    {
        double sumSquares = 0.0;
        float peak = 0.0f;
        int frames = 0;
        bool finite = true;

        double rms() const
        {
            return frames > 0 ? std::sqrt(sumSquares / (double) (frames * 2)) : 0.0;
        }
    };

    class Runner
    {
    public:
        void expect (bool condition, const std::string& name, const std::string& detail = {})
        {
            if (condition)
            {
                ++passed;
                std::cout << "PASS  " << name << "\n";
                return;
            }

            ++failed;
            std::cout << "FAIL  " << name;
            if (! detail.empty())
                std::cout << "  " << detail;
            std::cout << "\n";
        }

        // B10: record a deliberately-skipped assertion. A skip is neither a
        // pass nor a failure; it is logged with its reason and never affects
        // the exit code. Used only when the sample library is genuinely absent
        // (and even a synthetic stand-in could not be created).
        void skip (const std::string& name, const std::string& reason)
        {
            ++skipped;
            std::cout << "SKIP  " << name << "  (" << reason << ")\n";
        }

        int result() const
        {
            std::cout << "\nSummary: " << passed << " passed, " << failed
                      << " failed, " << skipped << " skipped\n";
            return failed == 0 ? 0 : 1;
        }

    private:
        int passed = 0;
        int failed = 0;
        int skipped = 0;
    };

    juce::File samplesRoot()
    {
        return juce::File(AUDIENCE_SYNTH_SOURCE_SAMPLES_PATH);
    }

    juce::File pianoDreamDir()
    {
        return samplesRoot().getChildFile("Piano Dream");
    }

    // ---- B10: graceful degradation when the sample library is ABSENT -----
    //
    // The bundled "Piano Dream" library is the default fixture and, when it is
    // present, every sample-dependent test below runs against it exactly as
    // before. When it is missing (e.g. a checkout without the large WAV assets)
    // we synthesise a tiny stand-in WAV library into a temp dir so the *code
    // path* (load -> index -> trigger -> render) still exercises. The stand-in
    // covers the full 88-key piano range with short decaying sines named by
    // note (A0.wav .. C8.wav), so SampleLibrary's filename->MIDI parsing,
    // auto-trim and closest-match lookup all behave like a real library for the
    // MIDI notes these tests touch (all >= C2). Pitch/scale mappings are engine
    // logic and do not depend on sample timbre, so the assertions still hold.
    //
    // Producing real audio data (rather than skipping) keeps coverage of the
    // sample render path on sample-less machines; we only fall back to SKIP if
    // even the synthetic library cannot be written.

    // Write one mono 16-bit WAV containing a SUSTAINED tone at `freqHz`.
    // Returns true on success.
    //
    // The tone is deliberately long (3 s) and held near a constant amplitude
    // (only a short attack and a short release fade at the very edges) so it
    // behaves like a real sustaining instrument sample: the sample player keeps
    // feeding audible data for the full duration of every render window used by
    // the tests (the longest is ~0.8 s). A fast-decaying tone would fall silent
    // before those windows finish and would (correctly) trip the engine's
    // voice-amplitude / active-unison assertions, so we keep it sustained. A
    // couple of low harmonics give it a slightly instrument-like spectrum. The
    // long sustain also means SampleLibrary's auto-trim keeps essentially the
    // whole buffer (trimmedLength >> 512).
    bool writeSyntheticWav (const juce::File& file, double freqHz)
    {
        constexpr double sr      = 44100.0;
        constexpr double seconds = 3.0;
        const int        len     = (int) (sr * seconds);
        const int        attack  = (int) (sr * 0.005);   // 5 ms attack
        const int        release = (int) (sr * 0.030);   // 30 ms release fade

        juce::AudioBuffer<float> buffer (1, len);
        auto* data = buffer.getWritePointer (0);
        const double twoPiF = 2.0 * juce::MathConstants<double>::pi * freqHz;
        for (int i = 0; i < len; ++i)
        {
            const double t = (double) i / sr;
            double s = std::sin (twoPiF * t)
                     + 0.30 * std::sin (2.0 * twoPiF * t)
                     + 0.15 * std::sin (3.0 * twoPiF * t);

            double env = 0.35;                            // sustained level
            if (i < attack)                env *= (double) i / (double) attack;
            else if (i > len - release)    env *= (double) (len - i) / (double) release;

            data[i] = (float) (env * s / 1.45);           // normalise harmonics
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

    // Build (once) a synthetic library covering MIDI 21..108 (A0..C8). Returns
    // the directory, or a non-existent File if generation failed.
    juce::File buildSyntheticLibrary()
    {
        static const char* const names[] =
            { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("SpektraSynthTests_SynthSamples");
        dir.createDirectory();

        int written = 0;
        for (int midi = 21; midi <= 108; ++midi)   // full 88-key piano range
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

    // Resolve the directory the sample-dependent tests should load from:
    //   - the real Piano Dream library if it is present, else
    //   - a synthetic stand-in generated into a temp dir (computed once).
    // `usingSynthetic` reports which path was taken (for logging).
    const juce::File& effectiveSampleDir (bool& usingSynthetic)
    {
        static bool       synthetic = false;
        static juce::File resolved = []
        {
            auto real = pianoDreamDir();
            if (real.isDirectory())
                return real;
            return juce::File();
        }();

        if (resolved == juce::File() && ! synthetic)
        {
            resolved  = buildSyntheticLibrary();
            synthetic = (resolved != juce::File());
        }

        usingSynthetic = synthetic;
        return resolved;
    }

    bool nearHz (double actual, double expected, double tolerance = 0.01)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    void configureDryTestEngine (PartialEngine& engine)
    {
        engine.prepare(kSampleRate, kBlockSize);
        engine.attackMs.store(10.0f);
        engine.releaseMs.store(100.0f);
        engine.layerMix.store(1.0f);
        engine.masterGain.store(1.0f);
        engine.energyMacro.store(1.0f);
        engine.motionMacro.store(0.0f);
        engine.toneMacro.store(0.0f);
        engine.spaceMacro.store(0.0f);
        engine.movement.store(0.0f);
        engine.grainDensity.store(0.0f);
        engine.positionJitter.store(0.0f);
        engine.stereoSpread.store(0.0f);
        engine.pitchSpread.store(0.0f);
        engine.reverbAmount.store(0.0f);
        engine.delayAmount.store(0.0f);
        engine.wetDry.store(0.0f);
        engine.tapeDrive.store(0.0f);
        engine.engineSource.store(0);
        engine.samplePlaybackMode.store(0);
        engine.spectralElement.store(1);
        engine.spectralPartialCount.store(28);
        engine.spectralPartialSolo.store(0);
        engine.spectralStretch.store(0.0f);
        engine.atomicScaleMode.store(1);
        engine.signatureMode.store(0);
        engine.freeze.store(0);
        engine.reverseGrains.store(0);
        engine.scaleRootMidi.store(36);
        engine.scaleMode.store(0);
        engine.scaleOctaves.store(4);
    }

    Stats renderSeconds (PartialEngine& engine, double seconds)
    {
        const int total = std::max(1, (int) std::round(seconds * kSampleRate));
        std::vector<float> left((size_t) kBlockSize);
        std::vector<float> right((size_t) kBlockSize);
        Stats stats;

        for (int done = 0; done < total; done += kBlockSize)
        {
            const int n = std::min(kBlockSize, total - done);
            engine.render(left.data(), right.data(), n);

            for (int i = 0; i < n; ++i)
            {
                const float l = left[(size_t) i];
                const float r = right[(size_t) i];
                stats.finite = stats.finite && std::isfinite(l) && std::isfinite(r);
                stats.peak = std::max(stats.peak, std::max(std::abs(l), std::abs(r)));
                stats.sumSquares += (double) l * (double) l + (double) r * (double) r;
            }
            stats.frames += n;
        }

        return stats;
    }

    std::set<int> activeMidis (PartialEngine& engine)
    {
        std::set<int> result;
        for (int i = 0; i < engine.getMaxVoices(); ++i)
        {
            const int midi = engine.getVoiceMidi(i);
            if (midi >= 0 && engine.getVoiceAmp(i) > 0.0001f)
                result.insert(midi);
        }
        return result;
    }

    int strongestMidi (PartialEngine& engine)
    {
        float bestAmp = 0.0f;
        int bestMidi = -1;
        for (int i = 0; i < engine.getMaxVoices(); ++i)
        {
            const float amp = engine.getVoiceAmp(i);
            if (amp > bestAmp)
            {
                bestAmp = amp;
                bestMidi = engine.getVoiceMidi(i);
            }
        }
        return bestMidi;
    }

    float maxVoiceAmp (PartialEngine& engine)
    {
        float bestAmp = 0.0f;
        for (int i = 0; i < engine.getMaxVoices(); ++i)
            bestAmp = std::max(bestAmp, engine.getVoiceAmp(i));
        return bestAmp;
    }

    Stats triggerSeatAndRender (PartialEngine& engine, float x, float y, double seconds,
                                int row = 0, int col = 50)
    {
        engine.setY(row, col, y);
        engine.setX(row, col, x);
        engine.setOn(row, col, true);
        return renderSeconds(engine, seconds);
    }

    void triggerCrowd (PartialEngine& engine, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            const int row = i % PartialEngine::MAX_ROWS;
            const int col = (i / PartialEngine::MAX_ROWS) % PartialEngine::MAX_COLS;
            const float x = (float) (i % 29) / 28.0f;
            const float y = 0.65f + 0.35f * (float) (i % 11) / 10.0f;
            engine.setY(row, col, y);
            engine.setX(row, col, x);
            engine.setOn(row, col, true);
        }
    }

    bool loadPianoDream (PartialEngine& engine)
    {
        bool synthetic = false;
        const auto& dir = effectiveSampleDir(synthetic);
        if (dir == juce::File())
            return false; // no real and no synthetic library available
        return engine.loadSampleLibrary(dir) > 0;
    }
}

int main()
{
    Runner r;

    // B10: the "samples present" assertions stay exactly as before WHEN the
    // bundled library exists (the normal case here). When it is absent we log a
    // SKIP with the offending path instead of failing, and steer the dependent
    // tests onto the synthetic stand-in (validated just below).
    const bool samplesPresent = pianoDreamDir().isDirectory();
    if (samplesPresent)
    {
        r.expect(samplesRoot().isDirectory(), "samples root exists",
                 samplesRoot().getFullPathName().toStdString());
        r.expect(pianoDreamDir().isDirectory(), "Piano Dream library exists",
                 pianoDreamDir().getFullPathName().toStdString());
    }
    else
    {
        r.skip("samples root exists",
               "sample library absent: " + samplesRoot().getFullPathName().toStdString());
        r.skip("Piano Dream library exists",
               "sample library absent: " + pianoDreamDir().getFullPathName().toStdString());
    }

    r.expect(SampleLibrary::parseRootMidiFromName("C4") == 60, "parse C4 -> MIDI 60");
    r.expect(SampleLibrary::parseRootMidiFromName("A4") == 69, "parse A4 -> MIDI 69");
    r.expect(SampleLibrary::parseRootMidiFromName("F#3") == 54, "parse F#3 -> MIDI 54");
    r.expect(SampleLibrary::parseRootMidiFromName("Bb2") == 46, "parse Bb2 -> MIDI 46");

    {
        // Load the real library if present, else the synthetic stand-in. The
        // assertions hold for both: the synthetic set covers >= 40 notes,
        // C4 (MIDI 60), full-length decaying sines and one audio channel.
        bool usingSynthetic = false;
        const auto& dir = effectiveSampleDir(usingSynthetic);

        if (dir == juce::File())
        {
            // Genuinely no samples AND synthetic generation failed: skip the
            // sample-content assertions rather than fail.
            r.skip("sample library loads many notes", "no sample library available (real or synthetic)");
            r.skip("closest sample lookup C4", "no sample library available (real or synthetic)");
            r.skip("loaded sample has playable trimmed length", "no sample library available (real or synthetic)");
            r.skip("loaded sample has audio channels", "no sample library available (real or synthetic)");
        }
        else
        {
            if (usingSynthetic)
                std::cout << "NOTE  sample library absent; using synthetic stand-in at "
                          << dir.getFullPathName().toStdString() << "\n";

            SampleLibrary lib;
            const int loaded = lib.loadFromDirectory(dir);
            r.expect(loaded >= 40, "sample library loads many notes",
                     "loaded=" + std::to_string(loaded));
            r.expect(lib.getSampleForMidi(60) != nullptr, "closest sample lookup C4");
            if (const auto* s = lib.getSampleForMidi(60))
            {
                r.expect(s->trimmedLength > 512, "loaded sample has playable trimmed length",
                         "trimmedLength=" + std::to_string(s->trimmedLength));
                r.expect(s->buffer.getNumChannels() >= 1, "loaded sample has audio channels");
            }
        }
    }

    {
        HEAP_PARTIAL_ENGINE(engine);
        configureDryTestEngine(engine);
        r.expect(loadPianoDream(engine), "engine loads Piano Dream");
        engine.scaleRootMidi.store(36);
        engine.scaleMode.store(0);
        engine.scaleOctaves.store(1);

        const int expected[] = { 36, 38, 40, 41, 43, 45, 47 };
        bool ok = engine.getScaleTableSize() == 7;
        for (int i = 0; i < 7; ++i)
            ok = ok && engine.getScaleMidi(i) == expected[i];
        r.expect(ok, "major scale MIDI table maps C2 octave correctly");
    }

    {
        HEAP_PARTIAL_ENGINE(engine);
        configureDryTestEngine(engine);
        r.expect(loadPianoDream(engine), "engine loads samples for spectral scale tables");
        engine.scaleRootMidi.store(72);
        engine.scaleOctaves.store(1);
        engine.atomicScaleMode.store(4); // Raw: expose every stored spectral line as a scale degree.

        engine.scaleMode.store(7);
        r.expect(engine.getScaleTableSize() == 6, "hydrogen raw spectrum exposes all Max data visible lines");
        r.expect(nearHz(engine.getScaleFrequencyHz(0), 523.251, 0.01)
              && nearHz(engine.getScaleFrequencyHz(1), 706.38, 0.02)
              && nearHz(engine.getScaleFrequencyHz(2), 791.15, 0.02)
              && nearHz(engine.getScaleFrequencyHz(3), 837.20, 0.02),
                 "hydrogen spectrum maps wavelength ratios onto C5");
        engine.scaleRootMidi.store(60);
        r.expect(nearHz(engine.getScaleFrequencyHz(0), 261.626, 0.01)
              && nearHz(engine.getScaleFrequencyHz(1), 353.19, 0.02)
              && nearHz(engine.getScaleFrequencyHz(2), 395.58, 0.02)
              && nearHz(engine.getScaleFrequencyHz(3), 418.60, 0.02),
                 "hydrogen root changes preserve spectral intervals");
        r.expect(engine.getScaleRangeName().contains("Hydrogen Spectrum"),
                 "hydrogen range label names spectral scale");
        engine.scaleRootMidi.store(72);

        engine.scaleMode.store(8);
        r.expect(engine.getScaleTableSize() == 27, "helium raw spectrum exposes all positive Max data visible lines");
        r.expect(nearHz(engine.getScaleFrequencyHz(0), 523.251, 0.01),
                 "helium spectrum maps longest Max-data wavelength onto C5");
        r.expect(engine.getScaleRangeName().contains("Helium Spectrum"),
                 "helium range label names spectral scale");

        engine.scaleMode.store(9);
        r.expect(engine.getScaleTableSize() == 15, "lithium raw spectrum exposes all positive Max data visible lines");
        r.expect(nearHz(engine.getScaleFrequencyHz(0), 523.251, 0.01),
                 "lithium longest wavelength maps onto C5");
        r.expect(engine.getScaleRangeName().contains("Lithium Spectrum"),
                 "lithium range label names spectral scale");

        engine.scaleMode.store(10);
        r.expect(engine.getScaleTableSize() == 67, "beryllium raw spectrum exposes all positive Max data visible lines");
        r.expect(nearHz(engine.getScaleFrequencyHz(0), 523.251, 0.01),
                 "beryllium longest wavelength maps onto C5");
        r.expect(engine.getScaleRangeName().contains("Beryllium Spectrum"),
                 "beryllium range label names spectral scale");

        engine.scaleMode.store(11);
        r.expect(engine.getScaleTableSize() == 74, "boron raw spectrum exposes all positive Max data visible lines");
        r.expect(nearHz(engine.getScaleFrequencyHz(0), 523.251, 0.01),
                 "boron longest wavelength maps onto C5");
        r.expect(engine.getScaleRangeName().contains("Boron Spectrum"),
                 "boron range label names spectral scale");

        engine.scaleMode.store(12);
        r.expect(engine.getScaleTableSize() == 333, "carbon raw spectrum exposes all positive Max data visible lines");
        r.expect(nearHz(engine.getScaleFrequencyHz(0), 523.251, 0.01),
                 "carbon longest wavelength maps onto C5");
        r.expect(engine.getScaleRangeName().contains("Carbon Spectrum"),
                 "carbon range label names spectral scale");

        engine.scaleMode.store(13);
        r.expect(engine.getScaleTableSize() == 268, "oxygen raw spectrum exposes all positive Max data visible lines");
        r.expect(nearHz(engine.getScaleFrequencyHz(0), 523.251, 0.01),
                 "oxygen longest wavelength maps onto C5");
        r.expect(engine.getScaleRangeName().contains("Oxygen Spectrum"),
                 "oxygen range label names spectral scale");

        engine.scaleMode.store(14);
        r.expect(engine.getScaleTableSize() == 94, "fluorine raw spectrum exposes all positive Max data visible lines");
        r.expect(nearHz(engine.getScaleFrequencyHz(0), 523.251, 0.01),
                 "fluorine longest wavelength maps onto C5");
        r.expect(engine.getScaleRangeName().contains("Fluorine Spectrum"),
                 "fluorine range label names spectral scale");

        engine.scaleMode.store(15);
        r.expect(engine.getScaleTableSize() == 1000, "neon raw spectrum exposes all Max data visible lines");
        r.expect(nearHz(engine.getScaleFrequencyHz(0), 523.251, 0.01),
                 "neon longest wavelength maps onto C5");
        r.expect(engine.getScaleRangeName().contains("Neon Spectrum"),
                 "neon range label names spectral scale");

        struct RawScaleExpectation
        {
            int mode;
            int count;
            const char* name;
        };

        const RawScaleExpectation newRawScales[] {
            { 16, 211, "Sodium Spectrum" },
            { 17, 311, "Magnesium Spectrum" },
            { 18, 265, "Aluminium Spectrum" },
            { 19, 450, "Silicon Spectrum" },
            { 20, 151, "Phosphorus Spectrum" },
            { 21, 703, "Sulfur Spectrum" },
            { 22, 232, "Chlorine Spectrum" },
            { 23, 878, "Argon Spectrum" },
            { 24, 134, "Potassium Spectrum" },
            { 25, 248, "Calcium Spectrum" },
            { 26, 916, "Scandium Spectrum" },
            { 27, 1896, "Titanium Spectrum" },
            { 28, 2234, "Vanadium Spectrum" },
            { 29, 2213, "Chromium Spectrum" },
            { 30, 645, "Manganese Spectrum" },
            { 31, 4041, "Iron Spectrum" },
            { 32, 695, "Cobalt Spectrum" },
            { 33, 351, "Nickel Spectrum" },
            { 34, 1004, "Copper Spectrum" },
            { 35, 57, "Zinc Spectrum" },
        };

        for (const auto& e : newRawScales)
        {
            engine.scaleMode.store(e.mode);
            r.expect(engine.getScaleTableSize() == e.count,
                     std::string(e.name) + " raw spectrum exposes all positive stored visible lines");
            r.expect(nearHz(engine.getScaleFrequencyHz(0), 523.251, 0.01),
                     std::string(e.name) + " longest wavelength maps onto C5");
            r.expect(engine.getScaleRangeName().contains(e.name),
                     std::string(e.name) + " range label names spectral scale");
        }
    }

    {
        HEAP_PARTIAL_ENGINE(engine);
        configureDryTestEngine(engine);
        r.expect(loadPianoDream(engine), "engine loads samples for silence test");
        const auto stats = renderSeconds(engine, 0.5);
        r.expect(stats.finite, "silent render is finite");
        r.expect(stats.peak < 1.0e-8f, "no seats render silence",
                 "peak=" + std::to_string(stats.peak));
        r.expect(engine.getActiveVoiceCount() == 0, "no seats keep active voices at zero");
        r.expect(engine.getRegisteredSeatCount() == 0, "no seats keep registered count at zero");
    }

    {
        HEAP_PARTIAL_ENGINE(engine);
        configureDryTestEngine(engine);
        r.expect(loadPianoDream(engine), "engine loads samples for trigger test");
        const auto stats = triggerSeatAndRender(engine, 0.0f, 1.0f, 0.6);
        r.expect(stats.finite, "trigger render is finite");
        r.expect(stats.peak > 0.001f, "seat on produces audible output",
                 "peak=" + std::to_string(stats.peak));
        r.expect(stats.peak <= 1.0001f, "output remains bounded by limiter",
                 "peak=" + std::to_string(stats.peak));
        r.expect(engine.getRegisteredSeatCount() == 1, "registered seat count updates");
        r.expect(engine.getActiveVoiceCount() >= 3, "one seat spawns unison voices",
                 "voices=" + std::to_string(engine.getActiveVoiceCount()));
        r.expect(activeMidis(engine).count(36) == 1, "x=0 maps to root MIDI C2");
        r.expect(engine.getSamplePlaybackModeName() == "Sample Player",
                 "sample library defaults to direct sample player");
        r.expect(engine.getActiveGrainCount() == 0, "direct sample player does not schedule grains",
                 "grains=" + std::to_string(engine.getActiveGrainCount()));
    }

    {
        HEAP_PARTIAL_ENGINE(engine);
        configureDryTestEngine(engine);
        engine.samplePlaybackMode.store(1);
        engine.grainDensity.store(0.8f);
        r.expect(loadPianoDream(engine), "engine loads samples for granular routing test");
        const auto stats = triggerSeatAndRender(engine, 0.0f, 1.0f, 0.6);
        r.expect(stats.finite && stats.peak > 0.001f, "granular sample mode remains audible",
                 "peak=" + std::to_string(stats.peak));
        r.expect(engine.getSamplePlaybackModeName() == "Granular",
                 "sample playback mode reports granular");
        r.expect(engine.getActiveGrainCount() > 0, "granular sample mode schedules grains",
                 "grains=" + std::to_string(engine.getActiveGrainCount()));
    }

    {
        HEAP_PARTIAL_ENGINE(engine);
        configureDryTestEngine(engine);
        engine.engineSource.store(1);
        engine.spectralElement.store(1);
        engine.spectralPartialCount.store(9);
        engine.spectralPartialSolo.store(0);
        engine.scaleRootMidi.store(48);
        engine.scaleMode.store(0);
        engine.scaleOctaves.store(1);

        r.expect(engine.getSpectralElementName() == "Helium", "element synth selects helium dataset");
        r.expect(nearHz(engine.getSpectralElementRootWavelengthNm(), 667.815, 0.001),
                 "helium root wavelength is the longest Max data line");
        r.expect(engine.getSpectralElementLineCount() == 27,
                 "element synth preserves every positive helium raw partial");

        const auto stats = triggerSeatAndRender(engine, 0.0f, 1.0f, 0.45);
        r.expect(stats.finite, "element synth render is finite without samples");
        r.expect(stats.peak > 0.001f, "element synth produces audible output without sample library",
                 "peak=" + std::to_string(stats.peak));
        r.expect(stats.peak <= 1.0001f, "element synth remains bounded by limiter",
                 "peak=" + std::to_string(stats.peak));
        r.expect(engine.getActiveVoiceCount() >= 3, "element synth spawns unison voices");
        r.expect(engine.getDominantSampleName().contains("667.815"),
                 "element synth dominant readout shows root wavelength");
    }

    {
        HEAP_PARTIAL_ENGINE(engine);
        configureDryTestEngine(engine);
        engine.engineSource.store(1);
        engine.spectralElement.store(2);
        engine.spectralPartialSolo.store(1);
        engine.spectralPartialCount.store(27);
        engine.scaleRootMidi.store(48);
        engine.scaleMode.store(0);
        engine.scaleOctaves.store(1);

        r.expect(engine.getSpectralElementName() == "Lithium", "element synth selects lithium dataset");
        r.expect(nearHz(engine.getSpectralElementRootWavelengthNm(), 670.791, 0.001),
                 "lithium root wavelength is 670.791 nm");
        r.expect(engine.getSpectralElementLineCount() == 15,
                 "element synth preserves every positive lithium raw partial");

        const auto stats = triggerSeatAndRender(engine, 0.0f, 1.0f, 0.30);
        r.expect(stats.finite, "lithium partial solo render is finite");
        r.expect(stats.peak > 0.001f, "lithium partial solo is audible");
    }

    {
        HEAP_PARTIAL_ENGINE(engine);
        configureDryTestEngine(engine);
        engine.engineSource.store(1);
        engine.spectralElement.store(3);
        engine.spectralPartialSolo.store(1);
        engine.spectralPartialCount.store(67);
        engine.scaleRootMidi.store(48);
        engine.scaleMode.store(0);
        engine.scaleOctaves.store(1);

        r.expect(engine.getSpectralElementName() == "Beryllium", "element synth selects beryllium dataset");
        r.expect(nearHz(engine.getSpectralElementRootWavelengthNm(), 698.275, 0.001),
                 "beryllium root wavelength is 698.275 nm");
        r.expect(engine.getSpectralElementLineCount() == 67,
                 "element synth preserves every beryllium raw partial");

        const auto stats = triggerSeatAndRender(engine, 0.0f, 1.0f, 0.30);
        r.expect(stats.finite, "beryllium partial solo render is finite");
        r.expect(stats.peak > 0.001f, "beryllium partial solo is audible");
    }

    {
        struct ElementExpectation
        {
            int index;
            const char* name;
            double rootNm;
            int lines;
        };

        const ElementExpectation expectations[] {
            { 4, "Boron", 678.612, 74 },
            { 5, "Carbon", 696.231, 333 },
            { 6, "Oxygen", 691.056, 268 },
            { 7, "Fluorine", 696.635, 94 },
            { 8, "Neon", 699.300, 1000 },
            { 9, "Sodium", 665.150, 211 },
            { 10, "Magnesium", 696.540, 311 },
            { 11, "Aluminium", 692.000, 265 },
            { 12, "Silicon", 699.836, 450 },
            { 13, "Phosphorus", 699.269, 151 },
            { 14, "Sulfur", 699.940, 703 },
            { 15, "Chlorine", 698.189, 232 },
            { 16, "Argon", 699.221, 878 },
            { 17, "Potassium", 696.467, 134 },
            { 18, "Calcium", 694.551, 248 },
            { 19, "Scandium", 697.278, 916 },
            { 20, "Titanium", 699.893, 1896 },
            { 21, "Vanadium", 699.240, 2234 },
            { 22, "Chromium", 699.073, 2213 },
            { 23, "Manganese", 698.996, 645 },
            { 24, "Iron", 699.988, 4041 },
            { 25, "Cobalt", 699.732, 695 },
            { 26, "Nickel", 697.351, 351 },
            { 27, "Copper", 699.656, 1004 },
            { 28, "Zinc", 694.320, 57 },
        };

        for (const auto& e : expectations)
        {
            HEAP_PARTIAL_ENGINE(engine);
            configureDryTestEngine(engine);
            engine.engineSource.store(1);
            engine.spectralElement.store(e.index);
            engine.spectralPartialSolo.store(1);
            engine.spectralPartialCount.store(juce::jmin(e.lines, PartialEngine::MAX_ELEMENT_PARTIALS));
            engine.scaleRootMidi.store(48);
            engine.scaleMode.store(0);
            engine.scaleOctaves.store(1);

            r.expect(engine.getSpectralElementName() == e.name,
                     std::string("element synth selects ") + e.name + " dataset");
            r.expect(nearHz(engine.getSpectralElementRootWavelengthNm(), e.rootNm, 0.001),
                     std::string(e.name) + " root wavelength is preserved");
            r.expect(engine.getSpectralElementLineCount() == e.lines,
                     std::string("element synth preserves every ") + e.name + " raw partial");

            const auto stats = triggerSeatAndRender(engine, 0.0f, 1.0f, 0.25);
            r.expect(stats.finite, std::string(e.name) + " partial solo render is finite");
            r.expect(stats.peak > 0.001f, std::string(e.name) + " partial solo is audible");
        }
    }

    {
        HEAP_PARTIAL_ENGINE(engine);
        configureDryTestEngine(engine);
        engine.engineSource.store(1);
        engine.spectralElement.store(1);
        engine.spectralPartialSolo.store(1);
        engine.spectralPartialCount.store(1);
        engine.scaleRootMidi.store(48);
        engine.scaleMode.store(0);
        engine.scaleOctaves.store(1);

        const auto rootPartialStats = triggerSeatAndRender(engine, 0.0f, 1.0f, 0.35);
        r.expect(rootPartialStats.finite, "partial solo root render is finite");
        r.expect(rootPartialStats.peak > 0.001f, "partial solo root is audible");

        engine.clearAllSeats();
        engine.spectralPartialCount.store(28);
        const auto highPartialStats = triggerSeatAndRender(engine, 0.0f, 1.0f, 0.35);
        r.expect(highPartialStats.finite, "partial solo high line render is finite");
        r.expect(highPartialStats.peak > 0.001f, "partial solo high line is audible");
    }

    {
        HEAP_PARTIAL_ENGINE(engine);
        configureDryTestEngine(engine);
        r.expect(loadPianoDream(engine), "engine loads samples for scale keyboard test");
        engine.setKeyboardStep(0, 2, 1.0f, true);
        const auto stats = renderSeconds(engine, 0.35);
        r.expect(stats.finite && stats.peak > 0.001f, "scale keyboard step produces audible output");
        r.expect(activeMidis(engine).count(40) == 1, "scale keyboard step 2 maps to E2 in C major");
        r.expect(engine.getRegisteredSeatCount() == 0, "scale keyboard does not register as audience seat");
        const float heldAmp = maxVoiceAmp(engine);
        engine.setKeyboardStep(0, 2, 0.0f, false);
        renderSeconds(engine, 0.45);
        r.expect(maxVoiceAmp(engine) < heldAmp * 0.35f, "scale keyboard key release decays mapped note");
    }

    {
        r.expect(nearHz(PartialEngine::midiNoteToFrequencyHz(60), 261.625565, 0.001),
                 "MIDI note 60 maps to C4 frequency");
        r.expect(nearHz(PartialEngine::midiNoteToFrequencyHz(64), 329.627557, 0.001),
                 "MIDI note 64 maps to E4 frequency");
        r.expect(nearHz(PartialEngine::midiNoteToFrequencyHz(67), 391.995436, 0.001),
                 "MIDI note 67 maps to G4 frequency");
    }

    {
        HEAP_PARTIAL_ENGINE(engine);
        configureDryTestEngine(engine);
        r.expect(loadPianoDream(engine), "engine loads samples for realtime MIDI keyboard polyphony test");
        engine.scaleRootMidi.store(36);
        engine.scaleMode.store(0);
        engine.scaleOctaves.store(1);

        engine.processKeyboardStepRealtime(0, 0, 1.0f, true);
        engine.processKeyboardStepRealtime(1, 2, 1.0f, true);
        engine.processKeyboardStepRealtime(2, 4, 1.0f, true);
        renderSeconds(engine, 0.10);

        const auto midis = activeMidis(engine);
        r.expect(midis.count(36) == 1 && midis.count(40) == 1 && midis.count(43) == 1,
                 "realtime MIDI keyboard path keeps held notes polyphonic");
    }

    {
        HEAP_PARTIAL_ENGINE(engine);
        configureDryTestEngine(engine);
        r.expect(loadPianoDream(engine), "engine loads samples for direct MIDI keyboard polyphony test");

        engine.processKeyboardPitchRealtime(0, 60, PartialEngine::midiNoteToFrequencyHz(60), 1.0f, true);
        engine.processKeyboardPitchRealtime(1, 64, PartialEngine::midiNoteToFrequencyHz(64), 1.0f, true);
        engine.processKeyboardPitchRealtime(2, 67, PartialEngine::midiNoteToFrequencyHz(67), 1.0f, true);
        renderSeconds(engine, 0.12);

        auto midis = activeMidis(engine);
        r.expect(midis.count(60) == 1 && midis.count(64) == 1 && midis.count(67) == 1,
                 "direct MIDI keyboard path keeps C4/E4/G4 held as separate voices");

        engine.releaseMs.store(1.0f);
        engine.processKeyboardPitchRealtime(1, 64, 0.0, 0.0f, false);
        renderSeconds(engine, 0.20);
        midis = activeMidis(engine);
        r.expect(midis.count(60) == 1 && midis.count(64) == 0 && midis.count(67) == 1,
                 "releasing E4 leaves C4 and G4 active");

        engine.clearAllVoices();
        r.expect(activeMidis(engine).empty(), "panic-style clear removes direct MIDI keyboard voices");
    }

    {
        HEAP_PARTIAL_ENGINE(engine);
        configureDryTestEngine(engine);
        r.expect(loadPianoDream(engine), "engine loads samples for MIDI keyboard spectral fallback test");
        engine.scaleRootMidi.store(48);
        engine.scaleMode.store(7); // Hydrogen Spectrum
        engine.atomicScaleMode.store(0); // Core
        engine.scaleOctaves.store(1);

        const int step = engine.findNearestScaleStepForMidi(53); // F3 keyboard input.
        r.expect(step >= 0, "incoming MIDI note outside degree-key range finds a spectral scale step");
        r.expect(engine.getScaleMidi(step) == 53, "F3 input maps to Hydrogen's nearest F3 spectral degree");
    }

    {
        HEAP_PARTIAL_ENGINE(engine);
        configureDryTestEngine(engine);
        engine.scaleRootMidi.store(12);
        engine.scaleMode.store(9); // Lithium Spectrum
        engine.atomicScaleMode.store(0); // Core: playable one-octave lithium keyboard.
        engine.scaleOctaves.store(1);

        r.expect(engine.getScaleStepsPerOctave() == 6, "Lithium core scale exposes six playable scale-keyboard degrees");

        const int cStep = engine.findKeyboardScaleStepForMidi(24);  // C1 performance key.
        const int dStep = engine.findKeyboardScaleStepForMidi(26);  // D1 performance key.
        const int fsStep = engine.findKeyboardScaleStepForMidi(30); // F#1 performance key.
        const int gStep = engine.findKeyboardScaleStepForMidi(31);  // G1 performance key.
        const int gsStep = engine.findKeyboardScaleStepForMidi(32); // G#1 performance key.
        const int cTopStep = engine.findKeyboardScaleStepForMidi(36); // C2 performance key.

        r.expect(engine.getScaleMidi(cStep) == 12, "Lithium keyboard C1 maps to displayed C0 degree");
        r.expect(engine.getScaleMidi(dStep) == 14, "Lithium keyboard D1 maps to displayed D0 degree");
        r.expect(engine.getScaleMidi(fsStep) == 18, "Lithium keyboard F#1 maps to displayed F#0 core degree");
        r.expect(engine.getScaleMidi(gStep) == 19, "Lithium keyboard G1 maps to displayed G0 degree");
        r.expect(engine.getScaleMidi(gsStep) == 20, "Lithium keyboard G#1 maps to displayed G#0 degree");
        r.expect(cTopStep == engine.getScaleStepsPerOctave() - 1,
                 "Lithium keyboard C2 maps to the final displayed octave degree");
    }

    {
        HEAP_PARTIAL_ENGINE(engine);
        configureDryTestEngine(engine);
        engine.engineSource.store(1);
        engine.scaleRootMidi.store(12);
        engine.scaleMode.store(9); // Lithium Spectrum
        engine.atomicScaleMode.store(0); // Core: playable one-octave lithium keyboard.
        engine.scaleOctaves.store(1);

        engine.setKeyboardStep(0, engine.findKeyboardScaleStepForMidi(24), 1.0f, true);
        engine.setKeyboardStep(1, engine.findKeyboardScaleStepForMidi(26), 1.0f, true);
        engine.setKeyboardStep(2, engine.findKeyboardScaleStepForMidi(30), 1.0f, true);
        renderSeconds(engine, 0.05);

        const auto midis = activeMidis(engine);
        r.expect(midis.count(12) == 1 && midis.count(14) == 1 && midis.count(18) == 1,
                 "spectral MIDI keyboard mapping keeps multiple held notes polyphonic");
    }

    {
        HEAP_PARTIAL_ENGINE(engine);
        configureDryTestEngine(engine);
        r.expect(loadPianoDream(engine), "engine loads samples for X movement replacement test");
        engine.releaseMs.store(6000.0f);
        engine.minTriggerMs.store(0.0f);
        triggerSeatAndRender(engine, 0.0f, 1.0f, 0.08);

        int maxVoices = engine.getActiveVoiceCount();
        for (int i = 1; i <= 10; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(9));
            engine.setX(0, 50, (float) i / 10.0f);
            renderSeconds(engine, 0.04);
            maxVoices = std::max(maxVoices, engine.getActiveVoiceCount());
        }

        r.expect(maxVoices <= 3,
                 "one moving participant replaces its unison group instead of accumulating voices",
                 "maxVoices=" + std::to_string(maxVoices));
    }

    {
        HEAP_PARTIAL_ENGINE(engine);
        configureDryTestEngine(engine);
        r.expect(loadPianoDream(engine), "engine loads samples for polyphony limit test");

        engine.polyphonyMode.store(0);
        r.expect(engine.getVoiceLimit() == PartialEngine::NORMAL_VOICE_LIMIT,
                 "normal polyphony limit is 256");
        engine.polyphonyMode.store(1);
        r.expect(engine.getVoiceLimit() == PartialEngine::HIGH_VOICE_LIMIT,
                 "high polyphony limit is 512");
        engine.polyphonyMode.store(2);
        r.expect(engine.getVoiceLimit() == PartialEngine::ULTRA_VOICE_LIMIT,
                 "ultra polyphony limit is 1024");
    }

    {
        HEAP_PARTIAL_ENGINE(normalCrowd);
        configureDryTestEngine(normalCrowd);
        normalCrowd.polyphonyMode.store(0);
        r.expect(loadPianoDream(normalCrowd), "engine loads samples for adaptive normal crowd test");
        triggerCrowd(normalCrowd, 130);
        const auto stats = renderSeconds(normalCrowd, 0.4);

        r.expect(stats.finite, "adaptive normal crowd render is finite");
        r.expect(normalCrowd.getAdaptiveUnisonCount() == 1,
                 "normal mode folds dense crowd to one voice per participant",
                 "unison=" + std::to_string(normalCrowd.getAdaptiveUnisonCount()));
        r.expect(normalCrowd.getActiveVoiceCount() <= normalCrowd.getVoiceLimit(),
                 "normal mode never exceeds runtime voice limit",
                 "voices=" + std::to_string(normalCrowd.getActiveVoiceCount())
                 + " limit=" + std::to_string(normalCrowd.getVoiceLimit()));
    }

    {
        HEAP_PARTIAL_ENGINE(highCrowd);
        configureDryTestEngine(highCrowd);
        highCrowd.polyphonyMode.store(1);
        r.expect(loadPianoDream(highCrowd), "engine loads samples for adaptive high crowd test");
        triggerCrowd(highCrowd, 130);
        const auto stats = renderSeconds(highCrowd, 0.4);

        r.expect(stats.finite, "adaptive high crowd render is finite");
        r.expect(highCrowd.getAdaptiveUnisonCount() == 3,
                 "high mode keeps three-voice unison for medium crowd",
                 "unison=" + std::to_string(highCrowd.getAdaptiveUnisonCount()));
        r.expect(highCrowd.getActiveVoiceCount() > 300 && highCrowd.getActiveVoiceCount() <= highCrowd.getVoiceLimit(),
                 "high mode expands audible participant capacity",
                 "voices=" + std::to_string(highCrowd.getActiveVoiceCount())
                 + " limit=" + std::to_string(highCrowd.getVoiceLimit()));
    }

    {
        HEAP_PARTIAL_ENGINE(low);
        configureDryTestEngine(low);
        r.expect(loadPianoDream(low), "engine loads samples for low Y test");
        triggerSeatAndRender(low, 0.45f, 0.15f, 0.8);
        const float lowAmp = maxVoiceAmp(low);

        HEAP_PARTIAL_ENGINE(high);
        configureDryTestEngine(high);
        r.expect(loadPianoDream(high), "engine loads samples for high Y test");
        triggerSeatAndRender(high, 0.45f, 1.0f, 0.8);
        const float highAmp = maxVoiceAmp(high);

        r.expect(highAmp > lowAmp * 3.0f, "Y controls voice amplitude upward",
                 "lowAmp=" + std::to_string(lowAmp)
                 + " highAmp=" + std::to_string(highAmp));
    }

    {
        HEAP_PARTIAL_ENGINE(lowPitch);
        configureDryTestEngine(lowPitch);
        r.expect(loadPianoDream(lowPitch), "engine loads samples for low pitch test");
        lowPitch.scaleOctaves.store(1);
        triggerSeatAndRender(lowPitch, 0.0f, 1.0f, 0.35);

        HEAP_PARTIAL_ENGINE(highPitch);
        configureDryTestEngine(highPitch);
        r.expect(loadPianoDream(highPitch), "engine loads samples for high pitch test");
        highPitch.scaleOctaves.store(1);
        triggerSeatAndRender(highPitch, 0.999f, 1.0f, 0.35);

        r.expect(strongestMidi(lowPitch) == 36, "x=0 selects first scale degree");
        r.expect(strongestMidi(highPitch) == 47, "x near 1 selects last scale degree");
    }

    {
        HEAP_PARTIAL_ENGINE(engine);
        configureDryTestEngine(engine);
        r.expect(loadPianoDream(engine), "engine loads samples for off/release test");
        triggerSeatAndRender(engine, 0.25f, 1.0f, 0.4);
        engine.setOn(0, 50, false);
        renderSeconds(engine, 2.0);
        renderSeconds(engine, 0.1);
        r.expect(engine.getActiveVoiceCount() == 0, "seat off releases voices to zero",
                 "voices=" + std::to_string(engine.getActiveVoiceCount())
                 + " maxAmp=" + std::to_string(maxVoiceAmp(engine)));
        r.expect(engine.getRegisteredSeatCount() == 0, "seat off clears registered seat count",
                 "registered=" + std::to_string(engine.getRegisteredSeatCount()));
    }

    {
        HEAP_PARTIAL_ENGINE(dryRelease);
        configureDryTestEngine(dryRelease);
        dryRelease.samplePlaybackMode.store(1);
        r.expect(loadPianoDream(dryRelease), "engine loads samples for freeze off test");
        triggerSeatAndRender(dryRelease, 0.25f, 1.0f, 0.3);
        dryRelease.setOn(0, 50, false);
        renderSeconds(dryRelease, 2.0);
        const int dryVoices = dryRelease.getActiveVoiceCount();
        const float dryMaxAmp = maxVoiceAmp(dryRelease);

        HEAP_PARTIAL_ENGINE(frozen);
        configureDryTestEngine(frozen);
        frozen.samplePlaybackMode.store(1);
        frozen.freeze.store(1);
        r.expect(loadPianoDream(frozen), "engine loads samples for freeze on test");
        triggerSeatAndRender(frozen, 0.25f, 1.0f, 0.3);
        frozen.setOn(0, 50, false);
        renderSeconds(frozen, 2.0);
        const int frozenVoices = frozen.getActiveVoiceCount();
        const float frozenMaxAmp = maxVoiceAmp(frozen);

        r.expect(dryVoices == 0 && frozenVoices > 0, "freeze sustains released voices",
                 "dryVoices=" + std::to_string(dryVoices)
                 + " dryMaxAmp=" + std::to_string(dryMaxAmp)
                 + " frozenVoices=" + std::to_string(frozenVoices)
                 + " frozenMaxAmp=" + std::to_string(frozenMaxAmp));
    }

    {
        HEAP_PARTIAL_ENGINE(mutedByMaster);
        configureDryTestEngine(mutedByMaster);
        mutedByMaster.masterGain.store(0.0f);
        r.expect(loadPianoDream(mutedByMaster), "engine loads samples for master mute test");
        const auto mutedStats = triggerSeatAndRender(mutedByMaster, 0.5f, 1.0f, 0.7);

        HEAP_PARTIAL_ENGINE(loud);
        configureDryTestEngine(loud);
        loud.masterGain.store(1.0f);
        r.expect(loadPianoDream(loud), "engine loads samples for loud master test");
        const auto loudStats = triggerSeatAndRender(loud, 0.5f, 1.0f, 0.7);

        r.expect(mutedStats.peak < 1.0e-8f, "master gain at zero mutes signal",
                 "peak=" + std::to_string(mutedStats.peak));
        r.expect(loudStats.peak > 0.001f, "master gain above zero passes signal",
                 "peak=" + std::to_string(loudStats.peak));
    }

    {
        HEAP_PARTIAL_ENGINE(engine);
        configureDryTestEngine(engine);
        engine.masterGain.store(3.0f);
        engine.energyMacro.store(1.0f);
        r.expect(loadPianoDream(engine), "engine loads samples for crowd stress test");

        for (int i = 0; i < 60; ++i)
        {
            const int row = i % PartialEngine::MAX_ROWS;
            const int col = (i * 17) % PartialEngine::MAX_COLS;
            const float x = (float) (i % 59) / 58.0f;
            engine.setY(row, col, 1.0f);
            engine.setX(row, col, x);
            engine.setOn(row, col, true);
        }

        const auto stats = renderSeconds(engine, 1.5);
        r.expect(stats.finite, "crowd stress render stays finite");
        r.expect(stats.peak <= 1.0001f, "crowd stress remains limited",
                 "peak=" + std::to_string(stats.peak));
        r.expect(engine.getActiveVoiceCount() <= engine.getVoiceLimit(),
                 "voice allocator stays within runtime voice limit",
                 "voices=" + std::to_string(engine.getActiveVoiceCount()));
        r.expect(engine.getRegisteredSeatCount() == 60, "crowd registered count is correct",
                 "registered=" + std::to_string(engine.getRegisteredSeatCount()));
        r.expect(stats.rms() > 0.01, "crowd stress produces nontrivial signal",
                 "rms=" + std::to_string(stats.rms()));
    }

    {
        HEAP_PARTIAL_ENGINE(engine);
        configureDryTestEngine(engine);
        engine.freeze.store(1);
        engine.reverbAmount.store(1.0f);
        engine.delayAmount.store(1.0f);
        engine.wetDry.store(1.0f);
        r.expect(loadPianoDream(engine), "engine loads samples for hard clear test");
        triggerSeatAndRender(engine, 0.5f, 1.0f, 0.5);
        engine.clearAllSeats();
        const auto stats = renderSeconds(engine, 0.5);

        r.expect(engine.getRegisteredSeatCount() == 0, "hard clear removes all registered seats");
        r.expect(engine.getActiveVoiceCount() == 0, "hard clear removes all active voices");
        r.expect(stats.peak < 1.0e-8f, "hard clear silences freeze/reverb/delay tail",
                 "peak=" + std::to_string(stats.peak));
    }

    {
        HEAP_PARTIAL_ENGINE(engine);
        configureDryTestEngine(engine);
        engine.freeze.store(1);
        engine.reverbAmount.store(1.0f);
        engine.delayAmount.store(1.0f);
        engine.wetDry.store(1.0f);
        r.expect(loadPianoDream(engine), "engine loads samples for requested panic test");
        triggerSeatAndRender(engine, 0.5f, 1.0f, 0.5);
        engine.requestClearAllSeats();
        const auto stats = renderSeconds(engine, 0.5);

        r.expect(engine.getRegisteredSeatCount() == 0, "requested panic removes all registered seats");
        r.expect(engine.getActiveVoiceCount() == 0, "requested panic removes all active voices");
        r.expect(stats.peak < 1.0e-8f, "requested panic silences freeze/reverb/delay tail",
                 "peak=" + std::to_string(stats.peak));
    }

    {
        HEAP_PARTIAL_ENGINE(engine);
        configureDryTestEngine(engine);
        r.expect(loadPianoDream(engine), "engine loads samples for aurora band test");
        triggerSeatAndRender(engine, 0.5f, 1.0f, 0.5);

        float sumBands = 0.0f;
        for (int i = 0; i < PartialEngine::AURORA_BANDS; ++i)
            sumBands += engine.getAuroraBand(i);
        r.expect(sumBands > 0.0f, "aurora/spectral meter receives active voice energy",
                 "sumBands=" + std::to_string(sumBands));
    }

    return r.result();
}
