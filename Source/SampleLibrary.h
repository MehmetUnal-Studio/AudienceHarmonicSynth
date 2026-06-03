#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>

/*
    SampleLibrary

    Scans a directory for .wav files, parses each one's root note from
    the filename (e.g. "C2.wav" -> C2 -> MIDI 36), runs auto-trim to skip
    the silent tail of each sample, and provides a "closest MIDI match"
    lookup.

    Designed to grow: drop more .wav files in the directory and call
    loadFromDirectory() again to re-index.
*/
class SampleLibrary
{
public:
    struct Sample
    {
        juce::String              displayName;
        juce::String              filename;
        juce::AudioBuffer<float>  buffer;          // raw audio
        double                    fileSampleRate = 44100.0;
        int                       trimmedLength  = 0;   // playback ends here
        int                       rootMidi       = 60;  // MIDI note number (C4 default)
        double                    rootFreq       = 440.0;
        juce::String              rootNote;
    };

    SampleLibrary();
    ~SampleLibrary() = default;

    int loadFromDirectory (const juce::File& dir);

    int numSamples() const noexcept { return (int) samples.size(); }
    const Sample* getSample (int idx) const noexcept;

    // Closest MIDI match (by semitone distance).
    const Sample* getSampleForMidi (int midi) const noexcept;

    const juce::String& getStatus() const noexcept { return status; }

    // True when the most recent loadFromDirectory() hit a safety cap and
    // stopped early (i.e. not every eligible file was loaded). Exposed so the
    // UI / debug panel can flag the truncation distinctly from a clean load (B17).
    bool wasTruncated() const noexcept { return truncated; }

    static int parseRootMidiFromName (const juce::String& name);

    // Auto-trim parameters (applied during loadFromDirectory).
    static constexpr float kTrimThreshold   = 0.0015f;  // approx -56 dB
    static constexpr float kTrimTailMs      = 60.0f;    // keep this much past last audible
    static constexpr float kEndFadeMs       = 25.0f;    // fade out at the trim point

    // ---- Sample-load safety budget (B17) --------------------------------
    // Guards against pathological directories (e.g. thousands of long files)
    // that would spike RAM and stall the audio thread while processing is
    // suspended around a re-index. These caps are deliberately GENEROUS: they
    // sit far above any realistic instrument library so a normal load is never
    // truncated, while still bounding worst-case memory.
    //
    //  - kMaxSampleCount: hard ceiling on the number of loaded samples. Real
    //    single-instrument libraries here top out around 50 files; even a fully
    //    chromatic 10-octave set with 8 velocity layers (~960 files) stays well
    //    under this, so 4096 cannot truncate a sane library.
    //
    //  - kMaxTotalBytes: ceiling on DECODED (float PCM) audio held in RAM,
    //    summed across loaded buffers. The largest bundled library (Piano Dream,
    //    ~30 MB of 16-bit WAV) decodes to well under ~100 MB of float; 2 GiB is
    //    ~20x that, so a normal library never approaches the cap, yet a runaway
    //    directory is stopped before it can exhaust memory.
    static constexpr int           kMaxSampleCount = 4096;
    static constexpr juce::uint64  kMaxTotalBytes  = (juce::uint64) 2 * 1024 * 1024 * 1024; // 2 GiB decoded

private:
    static void autoTrim (Sample& s);

    std::vector<Sample> samples;
    juce::AudioFormatManager formatManager;
    juce::String status;
    bool         truncated = false;
};
