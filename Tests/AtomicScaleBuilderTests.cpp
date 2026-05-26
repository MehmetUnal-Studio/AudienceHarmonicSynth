#include "../Source/AtomicScaleBuilder.h"

#include <cmath>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace
{
    using Builder = AtomicScaleBuilder;
    using Unit = AtomicScaleBuilder::WavelengthUnit;
    using ScaleMode = AtomicScaleBuilder::ScaleMode;
    using RepMode = AtomicScaleBuilder::RepresentativeMode;

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

    std::vector<Builder::SourceLine> hydrogenAngstromLines()
    {
        return {
            { "H-alpha", "H-alpha", 6562.790, Unit::Unknown, 6500.0 },
            { "H-beta",  "H-beta",  4861.350, Unit::Unknown, 1500.0 },
            { "H-gamma", "H-gamma", 4340.472, Unit::Unknown, 1000.0 },
            { "H-delta", "H-delta", 4101.734, Unit::Unknown, 675.0 },
            { "H-eps",   "H-eps",   3970.075, Unit::Unknown, 255.0 },
            { "H-zeta",  "H-zeta",  3889.064, Unit::Unknown, 195.0 },
            { "H-eta",   "H-eta",   3835.397, Unit::Unknown, 135.0 },
        };
    }

    std::vector<Builder::SourceLine> denseElementLines()
    {
        std::vector<Builder::SourceLine> lines;
        lines.reserve(80);
        for (int i = 0; i < 80; ++i)
        {
            const double nm = 400.0 + (double) i * 3.65;
            const double intensity = 20.0 + (double) ((i * 37) % 500);
            lines.push_back({ "D-" + juce::String(i), "dense", nm, Unit::Nanometer, intensity });
        }
        return lines;
    }

    bool allRawLinesAssignedExactlyOnce (const Builder::Result& result)
    {
        std::set<int> seen;
        int totalAssigned = 0;
        for (const auto& degree : result.scaleDegrees)
        {
            totalAssigned += degree.clusterDensity;
            for (const int sourceIndex : degree.sourceLineIndices)
            {
                if (! seen.insert(sourceIndex).second)
                    return false;
            }
        }

        return totalAssigned == (int) result.rawLines.size()
            && seen.size() == result.rawLines.size();
    }

    bool representativesAreSourceLines (const Builder::Result& result)
    {
        for (const auto& degree : result.scaleDegrees)
        {
            bool found = false;
            for (const auto& id : degree.sourceLineIds)
                found = found || id == degree.representativeLineId;
            if (! found)
                return false;
        }
        return true;
    }

    bool degreeSeparationAtLeast (const Builder::Result& result, double minCents)
    {
        for (size_t i = 0; i < result.scaleDegrees.size(); ++i)
            for (size_t j = i + 1; j < result.scaleDegrees.size(); ++j)
                if (Builder::circularDistanceCents(result.scaleDegrees[i].cents,
                                                   result.scaleDegrees[j].cents) < minCents)
                    return false;

        return true;
    }
}

int main()
{
    Runner r;

    Builder::Options performable;
    performable.elementName = "Hydrogen";
    performable.rootHz = 130.8128;
    performable.scaleMode = ScaleMode::Performable;
    performable.representativeMode = RepMode::Medoid;

    const auto hydrogen = Builder::buildPlayableAtomicScale(hydrogenAngstromLines(), performable);

    r.expect(std::abs(hydrogen.lambdaRefNm - 656.279) < 0.001,
             "angstrom input normalizes to nm and chooses longest wavelength root");
    r.expect(hydrogen.rawLines.size() == 7, "hydrogen keeps all raw lines");
    r.expect(hydrogen.timbrePartials.size() == hydrogen.rawLines.size(),
             "hydrogen timbre partials preserve every raw line");
    r.expect(! hydrogen.scaleDegrees.empty()
          && std::abs(hydrogen.scaleDegrees.front().cents) < 0.001
          && std::abs(hydrogen.scaleDegrees.front().frequencyHz - 130.8128) < 0.001,
             "hydrogen root scale degree stays fixed at C3");
    r.expect(hydrogen.scaleDegrees.size() <= 12,
             "performable hydrogen scale stays within 12 degrees");
    r.expect(degreeSeparationAtLeast(hydrogen, 40.0),
             "performable hydrogen degrees respect 40 cent spacing");
    r.expect(allRawLinesAssignedExactlyOnce(hydrogen),
             "hydrogen assigns every raw line to exactly one scale cluster");
    r.expect(representativesAreSourceLines(hydrogen),
             "medoid representatives are real spectral lines");

    Builder::Options raw = performable;
    raw.scaleMode = ScaleMode::Raw;
    const auto hydrogenRaw = Builder::buildPlayableAtomicScale(hydrogenAngstromLines(), raw);
    r.expect(hydrogenRaw.scaleDegrees.size() == hydrogenRaw.rawLines.size(),
             "raw mode returns one scale degree per raw line");
    r.expect(hydrogenRaw.timbrePartials.size() == hydrogenRaw.rawLines.size(),
             "raw mode still preserves timbre partials");
    r.expect(allRawLinesAssignedExactlyOnce(hydrogenRaw),
             "raw mode assigns each raw line once");

    Builder::Options denseOptions;
    denseOptions.elementName = "Dense";
    denseOptions.scaleMode = ScaleMode::Performable;
    denseOptions.representativeMode = RepMode::Medoid;
    const auto dense = Builder::buildPlayableAtomicScale(denseElementLines(), denseOptions);

    r.expect(dense.rawLines.size() == 80, "dense element stores all raw lines");
    r.expect(dense.timbrePartials.size() == dense.rawLines.size(),
             "dense element preserves all timbre partials");
    r.expect(dense.scaleDegrees.size() <= 12,
             "dense performable scale is reduced to at most 12 degrees");
    r.expect(degreeSeparationAtLeast(dense, 40.0),
             "dense performable scale respects 40 cent spacing");
    r.expect(allRawLinesAssignedExactlyOnce(dense),
             "dense element assigns every raw line to exactly one cluster");

    Builder::Options scientific = denseOptions;
    scientific.scaleMode = ScaleMode::Scientific;
    const auto denseScientific = Builder::buildPlayableAtomicScale(denseElementLines(), scientific);
    r.expect(denseScientific.scaleDegrees.size() <= 48,
             "scientific scale respects 48 degree default cap");
    r.expect(denseScientific.timbrePartials.size() == denseScientific.rawLines.size(),
             "scientific mode still preserves all timbre partials");

    return r.result();
}
