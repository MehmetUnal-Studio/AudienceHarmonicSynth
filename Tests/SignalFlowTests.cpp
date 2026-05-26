#include "../Source/PartialEngine.h"
#include "../Source/SampleLibrary.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <vector>

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

        int result() const
        {
            std::cout << "\nSummary: " << passed << " passed, " << failed << " failed\n";
            return failed == 0 ? 0 : 1;
        }

    private:
        int passed = 0;
        int failed = 0;
    };

    juce::File samplesRoot()
    {
        return juce::File(AUDIENCE_SYNTH_SOURCE_SAMPLES_PATH);
    }

    juce::File pianoDreamDir()
    {
        return samplesRoot().getChildFile("Piano Dream");
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
        engine.spectralPartialCount.store(9);
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
        return engine.loadSampleLibrary(pianoDreamDir()) > 0;
    }
}

int main()
{
    Runner r;

    r.expect(samplesRoot().isDirectory(), "samples root exists",
             samplesRoot().getFullPathName().toStdString());
    r.expect(pianoDreamDir().isDirectory(), "Piano Dream library exists",
             pianoDreamDir().getFullPathName().toStdString());

    r.expect(SampleLibrary::parseRootMidiFromName("C4") == 60, "parse C4 -> MIDI 60");
    r.expect(SampleLibrary::parseRootMidiFromName("A4") == 69, "parse A4 -> MIDI 69");
    r.expect(SampleLibrary::parseRootMidiFromName("F#3") == 54, "parse F#3 -> MIDI 54");
    r.expect(SampleLibrary::parseRootMidiFromName("Bb2") == 46, "parse Bb2 -> MIDI 46");

    {
        SampleLibrary lib;
        const int loaded = lib.loadFromDirectory(pianoDreamDir());
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

    {
        PartialEngine engine;
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
        PartialEngine engine;
        configureDryTestEngine(engine);
        r.expect(loadPianoDream(engine), "engine loads samples for spectral scale tables");
        engine.scaleRootMidi.store(72);
        engine.scaleOctaves.store(1);
        engine.atomicScaleMode.store(4); // Raw: expose every stored spectral line as a scale degree.

        engine.scaleMode.store(7);
        r.expect(engine.getScaleTableSize() == 7, "hydrogen raw spectrum exposes all stored visible lines");
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
        r.expect(engine.getScaleTableSize() == 9, "helium raw spectrum exposes nine visible filtered lines");
        r.expect(nearHz(engine.getScaleFrequencyHz(0), 523.251, 0.01)
              && nearHz(engine.getScaleFrequencyHz(2), 629.19, 0.02)
              && nearHz(engine.getScaleFrequencyHz(8), 950.68, 0.02),
                 "helium spectrum maps longest red wavelength onto C5");
        r.expect(engine.getScaleRangeName().contains("Helium Spectrum"),
                 "helium range label names spectral scale");

        engine.scaleMode.store(9);
        r.expect(engine.getScaleTableSize() == 6, "lithium raw spectrum exposes six visible lines");
        r.expect(nearHz(engine.getScaleFrequencyHz(0), 523.251, 0.01),
                 "lithium longest wavelength maps onto C5");
        r.expect(engine.getScaleRangeName().contains("Lithium Spectrum"),
                 "lithium range label names spectral scale");

        engine.scaleMode.store(10);
        r.expect(engine.getScaleTableSize() == 27, "beryllium raw spectrum exposes all stored visible lines");
        r.expect(nearHz(engine.getScaleFrequencyHz(0), 523.251, 0.01),
                 "beryllium longest wavelength maps onto C5");
        r.expect(engine.getScaleRangeName().contains("Beryllium Spectrum"),
                 "beryllium range label names spectral scale");

        engine.scaleMode.store(11);
        r.expect(engine.getScaleTableSize() == 26, "boron raw spectrum exposes all stored visible lines");
        r.expect(nearHz(engine.getScaleFrequencyHz(0), 523.251, 0.01),
                 "boron longest wavelength maps onto C5");
        r.expect(engine.getScaleRangeName().contains("Boron Spectrum"),
                 "boron range label names spectral scale");

        engine.scaleMode.store(12);
        r.expect(engine.getScaleTableSize() == 47, "carbon raw spectrum exposes all stored visible lines");
        r.expect(nearHz(engine.getScaleFrequencyHz(0), 523.251, 0.01),
                 "carbon longest wavelength maps onto C5");
        r.expect(engine.getScaleRangeName().contains("Carbon Spectrum"),
                 "carbon range label names spectral scale");

        engine.scaleMode.store(13);
        r.expect(engine.getScaleTableSize() == 37, "oxygen raw spectrum exposes all stored visible lines");
        r.expect(nearHz(engine.getScaleFrequencyHz(0), 523.251, 0.01),
                 "oxygen longest wavelength maps onto C5");
        r.expect(engine.getScaleRangeName().contains("Oxygen Spectrum"),
                 "oxygen range label names spectral scale");

        engine.scaleMode.store(14);
        r.expect(engine.getScaleTableSize() == 29, "fluorine raw spectrum exposes all stored visible lines");
        r.expect(nearHz(engine.getScaleFrequencyHz(0), 523.251, 0.01),
                 "fluorine longest wavelength maps onto C5");
        r.expect(engine.getScaleRangeName().contains("Fluorine Spectrum"),
                 "fluorine range label names spectral scale");

        engine.scaleMode.store(15);
        r.expect(engine.getScaleTableSize() == 54, "neon raw spectrum exposes all stored visible lines");
        r.expect(nearHz(engine.getScaleFrequencyHz(0), 523.251, 0.01),
                 "neon longest wavelength maps onto C5");
        r.expect(engine.getScaleRangeName().contains("Neon Spectrum"),
                 "neon range label names spectral scale");
    }

    {
        PartialEngine engine;
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
        PartialEngine engine;
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
        PartialEngine engine;
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
        PartialEngine engine;
        configureDryTestEngine(engine);
        engine.engineSource.store(1);
        engine.spectralElement.store(1);
        engine.spectralPartialCount.store(9);
        engine.spectralPartialSolo.store(0);
        engine.scaleRootMidi.store(48);
        engine.scaleMode.store(0);
        engine.scaleOctaves.store(1);

        r.expect(engine.getSpectralElementName() == "Helium", "element synth selects helium dataset");
        r.expect(nearHz(engine.getSpectralElementRootWavelengthNm(), 706.519, 0.001),
                 "helium root wavelength is 706.519 nm");
        r.expect(engine.getSpectralElementLineCount() == 9,
                 "element synth preserves every helium raw partial");

        const auto stats = triggerSeatAndRender(engine, 0.0f, 1.0f, 0.45);
        r.expect(stats.finite, "element synth render is finite without samples");
        r.expect(stats.peak > 0.001f, "element synth produces audible output without sample library",
                 "peak=" + std::to_string(stats.peak));
        r.expect(stats.peak <= 1.0001f, "element synth remains bounded by limiter",
                 "peak=" + std::to_string(stats.peak));
        r.expect(engine.getActiveVoiceCount() >= 3, "element synth spawns unison voices");
        r.expect(engine.getDominantSampleName().contains("706.519"),
                 "element synth dominant readout shows root wavelength");
    }

    {
        PartialEngine engine;
        configureDryTestEngine(engine);
        engine.engineSource.store(1);
        engine.spectralElement.store(2);
        engine.spectralPartialSolo.store(1);
        engine.spectralPartialCount.store(6);
        engine.scaleRootMidi.store(48);
        engine.scaleMode.store(0);
        engine.scaleOctaves.store(1);

        r.expect(engine.getSpectralElementName() == "Lithium", "element synth selects lithium dataset");
        r.expect(nearHz(engine.getSpectralElementRootWavelengthNm(), 670.791, 0.001),
                 "lithium root wavelength is 670.791 nm");
        r.expect(engine.getSpectralElementLineCount() == 6,
                 "element synth preserves every lithium raw partial");

        const auto stats = triggerSeatAndRender(engine, 0.0f, 1.0f, 0.30);
        r.expect(stats.finite, "lithium partial solo render is finite");
        r.expect(stats.peak > 0.001f, "lithium partial solo is audible");
    }

    {
        PartialEngine engine;
        configureDryTestEngine(engine);
        engine.engineSource.store(1);
        engine.spectralElement.store(3);
        engine.spectralPartialSolo.store(1);
        engine.spectralPartialCount.store(27);
        engine.scaleRootMidi.store(48);
        engine.scaleMode.store(0);
        engine.scaleOctaves.store(1);

        r.expect(engine.getSpectralElementName() == "Beryllium", "element synth selects beryllium dataset");
        r.expect(nearHz(engine.getSpectralElementRootWavelengthNm(), 698.275, 0.001),
                 "beryllium root wavelength is 698.275 nm");
        r.expect(engine.getSpectralElementLineCount() == 27,
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
            { 4, "Boron", 628.547, 26 },
            { 5, "Carbon", 694.645, 47 },
            { 6, "Oxygen", 689.511, 37 },
            { 7, "Fluorine", 696.635, 29 },
            { 8, "Neon", 688.694, 54 },
        };

        for (const auto& e : expectations)
        {
            PartialEngine engine;
            configureDryTestEngine(engine);
            engine.engineSource.store(1);
            engine.spectralElement.store(e.index);
            engine.spectralPartialSolo.store(1);
            engine.spectralPartialCount.store(e.lines);
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
        PartialEngine engine;
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
        engine.spectralPartialCount.store(9);
        const auto highPartialStats = triggerSeatAndRender(engine, 0.0f, 1.0f, 0.35);
        r.expect(highPartialStats.finite, "partial solo high line render is finite");
        r.expect(highPartialStats.peak > 0.001f, "partial solo high line is audible");
    }

    {
        PartialEngine engine;
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
        PartialEngine engine;
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
        PartialEngine engine;
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
        PartialEngine normalCrowd;
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
        PartialEngine highCrowd;
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
        PartialEngine low;
        configureDryTestEngine(low);
        r.expect(loadPianoDream(low), "engine loads samples for low Y test");
        triggerSeatAndRender(low, 0.45f, 0.15f, 0.8);
        const float lowAmp = maxVoiceAmp(low);

        PartialEngine high;
        configureDryTestEngine(high);
        r.expect(loadPianoDream(high), "engine loads samples for high Y test");
        triggerSeatAndRender(high, 0.45f, 1.0f, 0.8);
        const float highAmp = maxVoiceAmp(high);

        r.expect(highAmp > lowAmp * 3.0f, "Y controls voice amplitude upward",
                 "lowAmp=" + std::to_string(lowAmp)
                 + " highAmp=" + std::to_string(highAmp));
    }

    {
        PartialEngine lowPitch;
        configureDryTestEngine(lowPitch);
        r.expect(loadPianoDream(lowPitch), "engine loads samples for low pitch test");
        lowPitch.scaleOctaves.store(1);
        triggerSeatAndRender(lowPitch, 0.0f, 1.0f, 0.35);

        PartialEngine highPitch;
        configureDryTestEngine(highPitch);
        r.expect(loadPianoDream(highPitch), "engine loads samples for high pitch test");
        highPitch.scaleOctaves.store(1);
        triggerSeatAndRender(highPitch, 0.999f, 1.0f, 0.35);

        r.expect(strongestMidi(lowPitch) == 36, "x=0 selects first scale degree");
        r.expect(strongestMidi(highPitch) == 47, "x near 1 selects last scale degree");
    }

    {
        PartialEngine engine;
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
        PartialEngine dryRelease;
        configureDryTestEngine(dryRelease);
        dryRelease.samplePlaybackMode.store(1);
        r.expect(loadPianoDream(dryRelease), "engine loads samples for freeze off test");
        triggerSeatAndRender(dryRelease, 0.25f, 1.0f, 0.3);
        dryRelease.setOn(0, 50, false);
        renderSeconds(dryRelease, 2.0);
        const int dryVoices = dryRelease.getActiveVoiceCount();
        const float dryMaxAmp = maxVoiceAmp(dryRelease);

        PartialEngine frozen;
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
        PartialEngine mutedByMaster;
        configureDryTestEngine(mutedByMaster);
        mutedByMaster.masterGain.store(0.0f);
        r.expect(loadPianoDream(mutedByMaster), "engine loads samples for master mute test");
        const auto mutedStats = triggerSeatAndRender(mutedByMaster, 0.5f, 1.0f, 0.7);

        PartialEngine loud;
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
        PartialEngine engine;
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
        PartialEngine engine;
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
        PartialEngine engine;
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
        PartialEngine engine;
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
