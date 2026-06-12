#include "../Source/SampleLibrary.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
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

        void note (const std::string& message)
        {
            std::cout << "NOTE  " << message << "\n";
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

    std::string toStd (const juce::String& s) { return s.toStdString(); }

    // Re-derives the minimum semitone distance from a query to any loaded sample
    // *independently* of getSampleForMidi(), so we can verify the lookup really
    // returns a globally-nearest sample rather than trusting the function under
    // test to police itself.
    int minDistanceTo (const SampleLibrary& lib, int midi)
    {
        int best = -1;
        for (int i = 0; i < lib.numSamples(); ++i)
        {
            const auto* s = lib.getSample(i);
            if (s == nullptr) continue;
            const int d = std::abs(s->rootMidi - midi);
            if (best < 0 || d < best) best = d;
        }
        return best;
    }
}

int main()
{
    Runner r;

    // ---------------------------------------------------------------------
    // parseRootMidiFromName
    //
    // Values asserted below are computed directly from the implementation in
    // Source/SampleLibrary.cpp, which uses standard MIDI numbering where
    // C-1 == 0, C4 == 60, A4 == 69 (octave letter o -> base = (o + 1) * 12).
    // ---------------------------------------------------------------------

    // Plain note names.
    r.expect (SampleLibrary::parseRootMidiFromName ("C4") == 60,
              "parseRootMidiFromName: C4 -> 60 (middle C, standard MIDI numbering)",
              "got " + std::to_string (SampleLibrary::parseRootMidiFromName ("C4")));

    r.expect (SampleLibrary::parseRootMidiFromName ("A4") == 69,
              "parseRootMidiFromName: A4 -> 69 (concert A)",
              "got " + std::to_string (SampleLibrary::parseRootMidiFromName ("A4")));

    // Sharp: A in octave 3 = 57, plus one semitone = 58.
    r.expect (SampleLibrary::parseRootMidiFromName ("A#3") == 58,
              "parseRootMidiFromName: A#3 -> 58 (sharp raises one semitone)",
              "got " + std::to_string (SampleLibrary::parseRootMidiFromName ("A#3")));

    // Sharp: F in octave 5 = 77, plus one semitone = 78.
    r.expect (SampleLibrary::parseRootMidiFromName ("F#5") == 78,
              "parseRootMidiFromName: F#5 -> 78 (sharp raises one semitone)",
              "got " + std::to_string (SampleLibrary::parseRootMidiFromName ("F#5")));

    // Case-insensitive: the parser upper-cases input first, so lowercase
    // note names resolve identically.
    r.expect (SampleLibrary::parseRootMidiFromName ("c4") == 60,
              "parseRootMidiFromName: lowercase c4 -> 60 (parser is case-insensitive)",
              "got " + std::to_string (SampleLibrary::parseRootMidiFromName ("c4")));

    // Flat: a trailing 'b' after the note letter (not followed by another
    // letter) lowers one semitone. C3 = 48, flat -> 47.
    r.expect (SampleLibrary::parseRootMidiFromName ("Cb3") == 47,
              "parseRootMidiFromName: Cb3 -> 47 (flat lowers one semitone)",
              "got " + std::to_string (SampleLibrary::parseRootMidiFromName ("Cb3")));

    // Negative octave: explicit '-' before the digits. C-1 -> (-1 + 1) * 12 = 0.
    r.expect (SampleLibrary::parseRootMidiFromName ("C-1") == 0,
              "parseRootMidiFromName: C-1 -> 0 (lowest standard MIDI note)",
              "got " + std::to_string (SampleLibrary::parseRootMidiFromName ("C-1")));

    // Embedded note name: a real-world style filename stem. The parser scans
    // for the first valid note letter that is not preceded by another A-Z
    // letter; '_' is not in A-Z so the 'C' after it qualifies. With C4 == 60,
    // C3 resolves to (3 + 1) * 12 == 48. (The 'A' in "PIANO" is skipped because
    // it is preceded by the letter 'I'.)
    r.expect (SampleLibrary::parseRootMidiFromName ("Piano_C3") == 48,
              "parseRootMidiFromName: Piano_C3 -> 48 (embedded note after underscore, C3)",
              "got " + std::to_string (SampleLibrary::parseRootMidiFromName ("Piano_C3")));

    // Bare numbers are NOT supported: there is no leading note letter, so the
    // parser returns the not-found sentinel -1 (it does NOT treat "60" as a
    // raw MIDI value).
    r.expect (SampleLibrary::parseRootMidiFromName ("60") == -1,
              "parseRootMidiFromName: bare number \"60\" -> -1 (bare numbers unsupported)",
              "got " + std::to_string (SampleLibrary::parseRootMidiFromName ("60")));

    // Unparseable names return the -1 sentinel.
    r.expect (SampleLibrary::parseRootMidiFromName ("drum") == -1,
              "parseRootMidiFromName: \"drum\" -> -1 (no isolated note letter)",
              "got " + std::to_string (SampleLibrary::parseRootMidiFromName ("drum")));

    // A note letter that only appears mid-word (preceded by another letter) is
    // not treated as a root, so this is unparseable too.
    r.expect (SampleLibrary::parseRootMidiFromName ("noise") == -1,
              "parseRootMidiFromName: \"noise\" -> -1 (E is preceded by a letter)",
              "got " + std::to_string (SampleLibrary::parseRootMidiFromName ("noise")));

    r.expect (SampleLibrary::parseRootMidiFromName ("") == -1,
              "parseRootMidiFromName: empty string -> -1",
              "got " + std::to_string (SampleLibrary::parseRootMidiFromName ("")));

    // ---------------------------------------------------------------------
    // getSampleForMidi: empty-library behaviour.
    //
    // Deterministic and asset-independent: load from a freshly-created empty
    // temp directory so the library is guaranteed to hold zero samples.
    // ---------------------------------------------------------------------
    {
        auto emptyDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("AudienceSampleLibraryTests_empty_"
                                           + juce::String (juce::Time::currentTimeMillis()));
        emptyDir.deleteRecursively();
        const bool created = emptyDir.createDirectory().wasOk();

        SampleLibrary lib;
        const int loaded = lib.loadFromDirectory (emptyDir);

        r.expect (created && loaded == 0 && lib.numSamples() == 0,
                  "getSampleForMidi: empty directory loads zero samples",
                  "loaded=" + std::to_string (loaded));
        r.expect (lib.getSampleForMidi (60) == nullptr,
                  "getSampleForMidi: returns nullptr on an empty library");
        r.expect (lib.getSample (0) == nullptr,
                  "getSample: out-of-range index returns nullptr on empty library");
        r.expect (! lib.wasTruncated(),
                  "wasTruncated: clean empty load is not flagged as truncated");

        emptyDir.deleteRecursively();
    }

    // Missing directory: loadFromDirectory must report 0 and leave the lookup
    // empty (graceful, no crash).
    {
        auto missingDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("AudienceSampleLibraryTests_missing_"
                                             + juce::String (juce::Time::currentTimeMillis()));
        missingDir.deleteRecursively(); // ensure it does not exist

        SampleLibrary lib;
        const int loaded = lib.loadFromDirectory (missingDir);
        r.expect (loaded == 0 && lib.numSamples() == 0
                      && lib.getSampleForMidi (60) == nullptr,
                  "getSampleForMidi: missing directory yields empty library + null lookup",
                  "loaded=" + std::to_string (loaded));
        r.expect (lib.getStatus().contains ("not found"),
                  "loadFromDirectory: missing directory reports a 'not found' status",
                  toStd (lib.getStatus()));
    }

    // ---------------------------------------------------------------------
    // getSampleForMidi: exact + nearest match against a real loaded library.
    //
    // Loads the bundled note-name samples via AUDIENCE_SYNTH_SOURCE_SAMPLES_PATH.
    // Expectations are derived from the library's OWN reported rootMidi values
    // (never hardcoded), and "nearest" is cross-checked with an independent
    // minimum-distance computation. If the samples are unavailable (path not
    // defined, missing, or nothing decodes), the block degrades to a NOTE and
    // does not fail the suite.
    // ---------------------------------------------------------------------
    {
        bool exercised = false;

#ifdef AUDIENCE_SYNTH_SOURCE_SAMPLES_PATH
        // Prefer the small, fully note-named "Lyre" set for a predictable
        // spread of root notes; fall back to the Samples root if absent.
        const juce::File samplesRoot { juce::String (AUDIENCE_SYNTH_SOURCE_SAMPLES_PATH) };
        juce::File loadDir = samplesRoot.getChildFile ("Lyre");
        if (! loadDir.isDirectory())
            loadDir = samplesRoot;

        if (loadDir.isDirectory())
        {
            SampleLibrary lib;
            const int loaded = lib.loadFromDirectory (loadDir);
            r.note ("loaded " + std::to_string (loaded)
                    + " sample(s) from " + toStd (loadDir.getFullPathName())
                    + " -> status: " + toStd (lib.getStatus()));

            if (loaded > 0 && lib.numSamples() > 0)
            {
                exercised = true;

                // --- Exact match: query a root that genuinely exists. ---
                const auto* first = lib.getSample (0);
                r.expect (first != nullptr,
                          "getSample: index 0 valid after a non-empty load");

                const int existingMidi = first->rootMidi;
                const auto* exact = lib.getSampleForMidi (existingMidi);
                r.expect (exact != nullptr && exact->rootMidi == existingMidi,
                          "getSampleForMidi: exact root returns a zero-distance sample",
                          "queried " + std::to_string (existingMidi)
                              + ", got "
                              + (exact ? std::to_string (exact->rootMidi) : std::string ("null")));
                r.expect (minDistanceTo (lib, existingMidi) == 0,
                          "getSampleForMidi: exact query confirmed by independent min-distance");

                // --- Nearest match: query far below the lowest root. The
                // returned sample's distance must equal the independently
                // computed global minimum distance for that query. ---
                int lowestMidi = 127;
                for (int i = 0; i < lib.numSamples(); ++i)
                    lowestMidi = std::min (lowestMidi, lib.getSample (i)->rootMidi);

                const int farQuery = lowestMidi - 24; // two octaves below lowest root
                const auto* nearest = lib.getSampleForMidi (farQuery);
                const int expectedDist = minDistanceTo (lib, farQuery);
                r.expect (nearest != nullptr
                              && std::abs (nearest->rootMidi - farQuery) == expectedDist,
                          "getSampleForMidi: far query returns the globally nearest sample",
                          "queried " + std::to_string (farQuery)
                              + ", returned root "
                              + (nearest ? std::to_string (nearest->rootMidi) : std::string ("null"))
                              + ", expected min distance " + std::to_string (expectedDist));
                // Below-range query should resolve to the lowest available root.
                r.expect (nearest != nullptr && nearest->rootMidi == lowestMidi,
                          "getSampleForMidi: query below range clamps to the lowest root",
                          "lowest root " + std::to_string (lowestMidi)
                              + ", got "
                              + (nearest ? std::to_string (nearest->rootMidi) : std::string ("null")));

                // --- Nearest match above the highest root, mirrored. ---
                int highestMidi = 0;
                for (int i = 0; i < lib.numSamples(); ++i)
                    highestMidi = std::max (highestMidi, lib.getSample (i)->rootMidi);

                const int highQuery = highestMidi + 24;
                const auto* nearestHigh = lib.getSampleForMidi (highQuery);
                r.expect (nearestHigh != nullptr && nearestHigh->rootMidi == highestMidi,
                          "getSampleForMidi: query above range clamps to the highest root",
                          "highest root " + std::to_string (highestMidi)
                              + ", got "
                              + (nearestHigh ? std::to_string (nearestHigh->rootMidi)
                                             : std::string ("null")));
            }
        }
#endif

        if (! exercised)
            r.note ("bundled samples unavailable - skipped the load-based "
                    "getSampleForMidi exact/nearest checks (graceful degradation)");
    }

    return r.result();
}
