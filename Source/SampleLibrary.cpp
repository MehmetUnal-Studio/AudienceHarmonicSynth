#include "SampleLibrary.h"
#include <cmath>
#include <limits>

SampleLibrary::SampleLibrary()
{
    formatManager.registerBasicFormats();
}

int SampleLibrary::parseRootMidiFromName (const juce::String& nameRaw)
{
    const auto str = nameRaw.toUpperCase();
    const int  n   = str.length();

    auto isNoteLetter = [] (juce::juce_wchar c) { return c >= 'A' && c <= 'G'; };

    for (int i = 0; i < n; ++i)
    {
        if (! isNoteLetter(str[i]))                             continue;
        if (i > 0 && (str[i - 1] >= 'A' && str[i - 1] <= 'Z'))  continue;

        const juce::juce_wchar letter = str[i];
        int semi = 0;
        switch (letter)
        {
            case 'C': semi = 0;  break;
            case 'D': semi = 2;  break;
            case 'E': semi = 4;  break;
            case 'F': semi = 5;  break;
            case 'G': semi = 7;  break;
            case 'A': semi = 9;  break;
            case 'B': semi = 11; break;
        }
        int j = i + 1;

        if (j < n && str[j] == '#')      { semi += 1; ++j; }
        else if (j < n && str[j] == 'B'
                 && (j + 1 >= n || str[j + 1] < 'A' || str[j + 1] > 'Z'))
                                         { semi -= 1; ++j; }

        bool neg = false;
        if (j < n && str[j] == '-')      { neg = true; ++j; }
        if (j >= n || str[j] < '0' || str[j] > '9') continue;

        int oct = 0;
        while (j < n && str[j] >= '0' && str[j] <= '9')
            oct = oct * 10 + (int)(str[j++] - '0');
        if (neg) oct = -oct;

        // standard MIDI numbering: C-1 = 0, A4 = 69, middle-C C4 = 60
        return (oct + 1) * 12 + semi;
    }
    return -1;
}

void SampleLibrary::autoTrim (Sample& s)
{
    const int len  = s.buffer.getNumSamples();
    const int nCh  = s.buffer.getNumChannels();
    if (len <= 0 || nCh <= 0) { s.trimmedLength = 0; return; }

    // walk backwards to find the last audible sample
    int lastAudible = -1;
    for (int i = len - 1; i >= 0; --i)
    {
        float peak = 0.0f;
        for (int c = 0; c < nCh; ++c)
            peak = juce::jmax(peak, std::abs(s.buffer.getSample(c, i)));
        if (peak > kTrimThreshold) { lastAudible = i; break; }
    }
    if (lastAudible < 0) { s.trimmedLength = 0; return; }

    const int tailSamples = (int) (kTrimTailMs * 0.001 * s.fileSampleRate);
    int trim = juce::jlimit(0, len, lastAudible + 1 + tailSamples);

    // apply a short fade-out at the trim point so the cut is silent
    const int fadeSamples = (int) (kEndFadeMs * 0.001 * s.fileSampleRate);
    if (trim > fadeSamples + 64)
    {
        const int fadeStart = trim - fadeSamples;
        for (int i = 0; i < fadeSamples; ++i)
        {
            const float g   = 1.0f - (float) i / (float) fadeSamples;
            const int   idx = fadeStart + i;
            for (int c = 0; c < nCh; ++c)
                s.buffer.setSample(c, idx, s.buffer.getSample(c, idx) * g);
        }
    }

    s.trimmedLength = trim;
}

int SampleLibrary::loadFromDirectory (const juce::File& dir)
{
    samples.clear();

    if (! dir.exists() || ! dir.isDirectory())
    {
        status = "Samples folder not found: " + dir.getFullPathName();
        return 0;
    }

    juce::Array<juce::File> files;
    dir.findChildFiles(files, juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac");
    files.sort();

    int loaded = 0, skipped = 0;
    for (auto& f : files)
    {
        if (f.getFileName().startsWith("._")) { ++skipped; continue; }

        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor(f));
        if (! reader)                                  { ++skipped; continue; }
        if (reader->lengthInSamples <= 0)              { ++skipped; continue; }
        if (reader->lengthInSamples > (juce::int64) std::numeric_limits<int>::max())
                                                        { ++skipped; continue; }

        Sample s;
        s.displayName    = f.getFileNameWithoutExtension();
        s.filename       = f.getFileName();
        s.fileSampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;

        const int numCh = (int) juce::jmin((juce::uint32) 2, reader->numChannels);
        if (numCh <= 0)                                 { ++skipped; continue; }
        const int len   = (int) reader->lengthInSamples;
        s.buffer.setSize(numCh, len);
        reader->read(&s.buffer, 0, len, 0, true, numCh > 1);

        const int midi = parseRootMidiFromName(s.displayName);
        s.rootMidi = (midi >= 0 && midi < 128) ? midi : 60;       // fallback C4
        s.rootFreq = 440.0 * std::pow(2.0, (s.rootMidi - 69) / 12.0);
        {
            static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
            const int oct = s.rootMidi / 12 - 1;
            s.rootNote = juce::String(names[s.rootMidi % 12]) + juce::String(oct);
        }

        autoTrim(s);
        if (s.trimmedLength <= 0)                      { ++skipped; continue; }

        samples.push_back(std::move(s));
        ++loaded;
    }

    status = "Loaded " + juce::String(loaded) + " sample"
           + (loaded == 1 ? "" : "s")
           + (skipped > 0 ? " (" + juce::String(skipped) + " skipped)" : "");
    return loaded;
}

const SampleLibrary::Sample* SampleLibrary::getSample (int idx) const noexcept
{
    if (idx < 0 || idx >= (int) samples.size()) return nullptr;
    return &samples[(size_t) idx];
}

const SampleLibrary::Sample* SampleLibrary::getSampleForMidi (int midi) const noexcept
{
    if (samples.empty()) return nullptr;
    const Sample* best = nullptr;
    int bestDist = 1024;
    for (const auto& s : samples)
    {
        const int d = std::abs(s.rootMidi - midi);
        if (d < bestDist) { bestDist = d; best = &s; }
    }
    return best;
}
