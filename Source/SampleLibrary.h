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

    static int parseRootMidiFromName (const juce::String& name);

    // Auto-trim parameters (applied during loadFromDirectory).
    static constexpr float kTrimThreshold   = 0.0015f;  // approx -56 dB
    static constexpr float kTrimTailMs      = 60.0f;    // keep this much past last audible
    static constexpr float kEndFadeMs       = 25.0f;    // fade out at the trim point

private:
    static void autoTrim (Sample& s);

    std::vector<Sample> samples;
    juce::AudioFormatManager formatManager;
    juce::String status;
};
