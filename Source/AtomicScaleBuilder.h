#pragma once

#include <juce_core/juce_core.h>
#include <vector>

class AtomicScaleBuilder
{
public:
    enum class WavelengthUnit
    {
        Unknown,
        Angstrom,
        Nanometer
    };

    enum class ScaleMode
    {
        Melodic,
        Performable,
        Microtonal,
        Scientific,
        Raw
    };

    enum class RepresentativeMode
    {
        Strongest,
        Medoid,
        WeightedMean
    };

    struct SourceLine
    {
        juce::String id;
        juce::String label;
        double wavelength = 0.0;
        WavelengthUnit wavelengthUnit = WavelengthUnit::Unknown;
        double intensity = 0.0;
    };

    struct Options
    {
        juce::String elementName;
        double rootHz = 130.8128; // C3
        ScaleMode scaleMode = ScaleMode::Performable;
        int maxScaleDegrees = 0; // 0 means use scaleMode default.
        double minSeparationCents = -1.0; // < 0 means use scaleMode default.
        RepresentativeMode representativeMode = RepresentativeMode::Medoid;
        bool alwaysIncludeRoot = true;
    };

    struct RawLine
    {
        int sourceIndex = -1;
        juce::String id;
        juce::String label;
        double wavelengthNm = 0.0;
        double intensity = 0.0;
        double ratio = 1.0;
        double cents = 0.0;
        double salience = 0.0;
        double timbreAmp = 0.0;
    };

    struct TimbrePartial
    {
        int sourceLineIndex = -1;
        juce::String sourceLineId;
        double ratio = 1.0;
        double amp = 0.0;
        double wavelengthNm = 0.0;
        double intensity = 0.0;
    };

    struct ScaleDegree
    {
        int degreeIndex = 0;
        double cents = 0.0;
        double frequencyHz = 0.0;
        double velocity = 0.0;
        juce::String representativeLineId;
        double representativeWavelengthNm = 0.0;
        double representativeIntensity = 0.0;
        double clusterTotalIntensity = 0.0;
        double clusterMaxIntensity = 0.0;
        int clusterDensity = 0;
        double clusterSpreadCents = 0.0;
        std::vector<juce::String> sourceLineIds;
        std::vector<int> sourceLineIndices;
    };

    struct Result
    {
        juce::String elementName;
        double rootHz = 130.8128;
        double lambdaRefNm = 0.0;
        std::vector<RawLine> rawLines;
        std::vector<TimbrePartial> timbrePartials;
        std::vector<ScaleDegree> scaleDegrees;
    };

    static Result buildPlayableAtomicScale (const std::vector<SourceLine>& lines);
    static Result buildPlayableAtomicScale (const std::vector<SourceLine>& lines,
                                            const Options& options);

    static double normalizeWavelengthNm (double wavelength, WavelengthUnit unit) noexcept;
    static double circularDistanceCents (double a, double b) noexcept;
    static double normalizedCents (double cents) noexcept;
    static int defaultMaxScaleDegrees (ScaleMode mode) noexcept;
    static double defaultMinSeparationCents (ScaleMode mode) noexcept;
};
