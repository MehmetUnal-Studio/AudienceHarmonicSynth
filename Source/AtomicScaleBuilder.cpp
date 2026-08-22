#include "AtomicScaleBuilder.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    constexpr double kOctaveCents = 1200.0;
    constexpr double kTwoPi = 6.28318530717958647692;

    double signedCircularDeltaCents (double cents, double reference) noexcept
    {
        double d = AtomicScaleBuilder::normalizedCents(cents - reference);
        if (d > 600.0)
            d -= kOctaveCents;
        return d;
    }

    double weightForLine (const AtomicScaleBuilder::RawLine& line) noexcept
    {
        return line.salience > 0.0 ? line.salience : 1.0;
    }

    bool meaningfullyDifferent (double a, double b) noexcept
    {
        return std::abs(a - b) > 1.0e-12;
    }
}

double AtomicScaleBuilder::normalizeWavelengthNm (double wavelength, WavelengthUnit unit) noexcept
{
    if (! std::isfinite(wavelength) || wavelength <= 0.0)
        return 0.0;

    if (unit == WavelengthUnit::Angstrom)
        return wavelength * 0.1;

    if (unit == WavelengthUnit::Nanometer)
        return wavelength;

    // Most public atomic datasets use Angstroms. If unit metadata is absent,
    // values above 1000 are safely interpreted as Angstroms.
    return wavelength > 1000.0 ? wavelength * 0.1 : wavelength;
}

double AtomicScaleBuilder::normalizedCents (double cents) noexcept
{
    if (! std::isfinite(cents))
        return 0.0;

    double c = std::fmod(cents, kOctaveCents);
    if (c < 0.0)
        c += kOctaveCents;

    if (std::abs(c - kOctaveCents) < 1.0e-9)
        return 0.0;

    return c;
}

double AtomicScaleBuilder::circularDistanceCents (double a, double b) noexcept
{
    if (! std::isfinite(a) || ! std::isfinite(b))
        return std::numeric_limits<double>::infinity();

    const double d = std::abs(normalizedCents(a - b));
    return std::min(d, kOctaveCents - d);
}

int AtomicScaleBuilder::defaultMaxScaleDegrees (ScaleMode mode) noexcept
{
    switch (mode)
    {
        case ScaleMode::Melodic:     return 7;
        case ScaleMode::Performable: return 12;
        case ScaleMode::Microtonal:  return 24;
        case ScaleMode::Scientific:  return 48;
        case ScaleMode::Raw:         return std::numeric_limits<int>::max();
    }

    return 12;
}

double AtomicScaleBuilder::defaultMinSeparationCents (ScaleMode mode) noexcept
{
    switch (mode)
    {
        case ScaleMode::Melodic:     return 80.0;
        case ScaleMode::Performable: return 40.0;
        case ScaleMode::Microtonal:  return 20.0;
        case ScaleMode::Scientific:  return 10.0;
        case ScaleMode::Raw:         return 0.0;
    }

    return 40.0;
}

AtomicScaleBuilder::Result AtomicScaleBuilder::buildPlayableAtomicScale (
    const std::vector<SourceLine>& lines)
{
    return buildPlayableAtomicScale(lines, Options {});
}

AtomicScaleBuilder::Result AtomicScaleBuilder::buildPlayableAtomicScale (
    const std::vector<SourceLine>& lines,
    const Options& options)
{
    Result result;
    result.elementName = options.elementName;
    result.rootHz = std::isfinite(options.rootHz) && options.rootHz > 0.0
                  ? options.rootHz : 130.8128;

    std::vector<RawLine> raw;
    raw.reserve(lines.size());

    double lambdaRefNm = 0.0;
    double maxIntensity = 0.0;
    double maxLogIntensity = 0.0;
    int rootRawIndex = -1;

    for (int i = 0; i < (int) lines.size(); ++i)
    {
        const auto& src = lines[(size_t) i];
        const double nm = normalizeWavelengthNm(src.wavelength, src.wavelengthUnit);
        if (! std::isfinite(nm) || nm <= 0.0)
            continue;

        const double intensity = src.intensity;
        if (! std::isfinite(intensity) || intensity <= 0.0)
            continue;

        RawLine line;
        line.sourceIndex = i;
        line.id = src.id.isNotEmpty() ? src.id : ("line-" + juce::String(i + 1));
        line.label = src.label;
        line.wavelengthNm = nm;
        line.intensity = intensity;
        raw.push_back(line);

        if (nm > lambdaRefNm)
        {
            lambdaRefNm = nm;
            rootRawIndex = (int) raw.size() - 1;
        }

        maxIntensity = std::max(maxIntensity, line.intensity);
        maxLogIntensity = std::max(maxLogIntensity, std::log1p(line.intensity));
    }

    if (raw.empty() || lambdaRefNm <= 0.0)
        return result;

    for (auto& line : raw)
    {
        line.ratio = lambdaRefNm / line.wavelengthNm;
        line.cents = normalizedCents(1200.0 * std::log2(line.ratio));
        if (line.sourceIndex == raw[(size_t) rootRawIndex].sourceIndex)
            line.cents = 0.0;

        const double logIntensity = std::log1p(line.intensity);
        line.salience = maxLogIntensity > 0.0 ? logIntensity / maxLogIntensity : 0.0;
        line.timbreAmp = maxIntensity > 0.0 ? line.intensity / maxIntensity : 0.0;
    }

    result.lambdaRefNm = lambdaRefNm;
    result.rawLines = raw;

    result.timbrePartials.reserve(result.rawLines.size());
    for (const auto& line : result.rawLines)
    {
        result.timbrePartials.push_back({
            line.sourceIndex,
            line.id,
            line.ratio,
            line.timbreAmp,
            line.wavelengthNm,
            line.intensity
        });
    }

    std::vector<int> selected;

    if (options.scaleMode == ScaleMode::Raw)
    {
        selected.reserve(result.rawLines.size());
        for (int i = 0; i < (int) result.rawLines.size(); ++i)
            selected.push_back(i);
    }
    else
    {
        const int maxDegrees = options.maxScaleDegrees > 0
            ? options.maxScaleDegrees
            : defaultMaxScaleDegrees(options.scaleMode);
        const double minSep = std::isfinite(options.minSeparationCents)
                           && options.minSeparationCents >= 0.0
            ? options.minSeparationCents
            : defaultMinSeparationCents(options.scaleMode);

        if (options.alwaysIncludeRoot && rootRawIndex >= 0)
            selected.push_back(rootRawIndex);

        std::vector<int> candidates;
        candidates.reserve(result.rawLines.size());
        for (int i = 0; i < (int) result.rawLines.size(); ++i)
            if (i != rootRawIndex)
                candidates.push_back(i);

        std::sort(candidates.begin(), candidates.end(), [&] (int a, int b) {
            const auto& A = result.rawLines[(size_t) a];
            const auto& B = result.rawLines[(size_t) b];
            if (meaningfullyDifferent(A.salience, B.salience)) return A.salience > B.salience;
            if (meaningfullyDifferent(A.intensity, B.intensity)) return A.intensity > B.intensity;
            if (meaningfullyDifferent(A.cents, B.cents)) return A.cents < B.cents;
            return A.wavelengthNm > B.wavelengthNm;
        });

        for (const int candidate : candidates)
        {
            if ((int) selected.size() >= maxDegrees)
                break;

            double nearest = std::numeric_limits<double>::max();
            for (const int s : selected)
                nearest = std::min(nearest, circularDistanceCents(result.rawLines[(size_t) candidate].cents,
                                                                  result.rawLines[(size_t) s].cents));

            if (selected.empty() || nearest >= minSep)
                selected.push_back(candidate);
        }

        if (selected.empty() && ! candidates.empty())
            selected.push_back(candidates.front());
    }

    if (selected.empty())
        return result;

    std::vector<std::vector<int>> clusters(selected.size());
    if (options.scaleMode == ScaleMode::Raw)
    {
        for (int i = 0; i < (int) selected.size(); ++i)
            clusters[(size_t) i].push_back(selected[(size_t) i]);
    }
    else
    {
        for (int i = 0; i < (int) result.rawLines.size(); ++i)
        {
            int bestCluster = 0;
            double bestDistance = std::numeric_limits<double>::max();
            for (int c = 0; c < (int) selected.size(); ++c)
            {
                const double d = circularDistanceCents(result.rawLines[(size_t) i].cents,
                                                       result.rawLines[(size_t) selected[(size_t) c]].cents);
                if (d < bestDistance)
                {
                    bestDistance = d;
                    bestCluster = c;
                }
            }
            clusters[(size_t) bestCluster].push_back(i);
        }
    }

    std::vector<ScaleDegree> degrees;
    degrees.reserve(selected.size());

    for (int c = 0; c < (int) selected.size(); ++c)
    {
        const auto& cluster = clusters[(size_t) c];
        if (cluster.empty())
            continue;

        const bool rootCluster = options.alwaysIncludeRoot
                              && rootRawIndex >= 0
                              && std::find(cluster.begin(), cluster.end(), rootRawIndex) != cluster.end();

        double clusterTotal = 0.0;
        double clusterMax = 0.0;
        for (const int idx : cluster)
        {
            const auto& line = result.rawLines[(size_t) idx];
            clusterTotal += line.intensity;
            clusterMax = std::max(clusterMax, line.intensity);
        }

        int representative = cluster.front();
        double degreeCents = result.rawLines[(size_t) representative].cents;

        if (rootCluster)
        {
            // The longest wavelength is the musical root. Keep it fixed at 0
            // cents so the element root reliably maps to the chosen root note.
            representative = rootRawIndex;
            degreeCents = 0.0;
        }
        else if (options.representativeMode == RepresentativeMode::Strongest)
        {
            representative = *std::max_element(cluster.begin(), cluster.end(), [&] (int a, int b) {
                const auto& A = result.rawLines[(size_t) a];
                const auto& B = result.rawLines[(size_t) b];
                if (meaningfullyDifferent(A.intensity, B.intensity)) return A.intensity < B.intensity;
                return A.sourceIndex > B.sourceIndex;
            });
            degreeCents = result.rawLines[(size_t) representative].cents;
        }
        else if (options.representativeMode == RepresentativeMode::WeightedMean)
        {
            double x = 0.0;
            double y = 0.0;
            for (const int idx : cluster)
            {
                const auto& line = result.rawLines[(size_t) idx];
                const double weight = weightForLine(line);
                const double angle = line.cents / kOctaveCents * kTwoPi;
                x += std::cos(angle) * weight;
                y += std::sin(angle) * weight;
            }
            degreeCents = normalizedCents(std::atan2(y, x) / kTwoPi * kOctaveCents);

            double best = std::numeric_limits<double>::max();
            for (const int idx : cluster)
            {
                const double d = circularDistanceCents(result.rawLines[(size_t) idx].cents, degreeCents);
                if (d < best)
                {
                    best = d;
                    representative = idx;
                }
            }
        }
        else // Medoid: actual source line with smallest weighted circular distance to the cluster.
        {
            double bestScore = std::numeric_limits<double>::max();
            for (const int idx : cluster)
            {
                const auto& candidate = result.rawLines[(size_t) idx];
                double weightedDistance = 0.0;
                double weightSum = 0.0;
                for (const int otherIdx : cluster)
                {
                    const auto& other = result.rawLines[(size_t) otherIdx];
                    const double weight = weightForLine(other);
                    weightedDistance += circularDistanceCents(candidate.cents, other.cents) * weight;
                    weightSum += weight;
                }
                const double score = weightSum > 0.0 ? weightedDistance / weightSum : weightedDistance;
                if (score < bestScore
                    || (std::abs(score - bestScore) < 1.0e-9
                        && candidate.salience > result.rawLines[(size_t) representative].salience))
                {
                    bestScore = score;
                    representative = idx;
                }
            }
            degreeCents = result.rawLines[(size_t) representative].cents;
        }

        if (options.scaleMode != ScaleMode::Raw)
        {
            const double minSep = std::isfinite(options.minSeparationCents)
                               && options.minSeparationCents >= 0.0
                ? options.minSeparationCents
                : defaultMinSeparationCents(options.scaleMode);

            bool representativeWouldBreakSpacing = false;
            for (int other = 0; other < (int) selected.size(); ++other)
            {
                if (other == c)
                    continue;

                if (circularDistanceCents(degreeCents,
                                          result.rawLines[(size_t) selected[(size_t) other]].cents)
                    < minSep - 1.0e-9)
                {
                    representativeWouldBreakSpacing = true;
                    break;
                }
            }

            if (representativeWouldBreakSpacing)
            {
                // Keep the playable scale constraints stronger than the
                // representative heuristic. The shifted line remains traceable
                // in the cluster and timbre partial list.
                representative = selected[(size_t) c];
                degreeCents = result.rawLines[(size_t) representative].cents;
            }
        }

        const auto& rep = result.rawLines[(size_t) representative];
        double spreadNum = 0.0;
        double spreadDen = 0.0;
        ScaleDegree degree;
        degree.cents = degreeCents;
        degree.frequencyHz = result.rootHz * std::pow(2.0, degreeCents / kOctaveCents);
        degree.representativeLineId = rep.id;
        degree.representativeWavelengthNm = rep.wavelengthNm;
        degree.representativeIntensity = rep.intensity;
        degree.clusterTotalIntensity = clusterTotal;
        degree.clusterMaxIntensity = clusterMax;
        degree.clusterDensity = (int) cluster.size();

        for (const int idx : cluster)
        {
            const auto& line = result.rawLines[(size_t) idx];
            degree.sourceLineIds.push_back(line.id);
            degree.sourceLineIndices.push_back(line.sourceIndex);
            const double weight = weightForLine(line);
            const double d = signedCircularDeltaCents(line.cents, degreeCents);
            spreadNum += d * d * weight;
            spreadDen += weight;
        }

        degree.clusterSpreadCents = spreadDen > 0.0 ? std::sqrt(spreadNum / spreadDen) : 0.0;
        degrees.push_back(std::move(degree));
    }

    double maxClusterTotal = 0.0;
    for (const auto& degree : degrees)
        maxClusterTotal = std::max(maxClusterTotal, degree.clusterTotalIntensity);

    for (auto& degree : degrees)
        degree.velocity = maxClusterTotal > 0.0 ? degree.clusterTotalIntensity / maxClusterTotal : 0.0;

    std::sort(degrees.begin(), degrees.end(), [] (const auto& a, const auto& b) {
        if (meaningfullyDifferent(a.cents, b.cents)) return a.cents < b.cents;
        return a.representativeWavelengthNm > b.representativeWavelengthNm;
    });

    for (int i = 0; i < (int) degrees.size(); ++i)
    {
        degrees[(size_t) i].degreeIndex = i;
        degrees[(size_t) i].frequencyHz = result.rootHz * std::pow(2.0, degrees[(size_t) i].cents / kOctaveCents);
    }

    result.scaleDegrees = std::move(degrees);
    return result;
}
