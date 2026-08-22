#include "CrowdLfoGate.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double kTwoPi = 6.283185307179586476925286766559;
    constexpr double kMaximumClockMagnitude = 1.0e12;

    double finiteOr (double value, double fallback) noexcept
    {
        return std::isfinite(value) ? value : fallback;
    }
}

CrowdLfoGate::CrowdLfoGate() noexcept
{
    reset();
}

CrowdLfoGate::Config CrowdLfoGate::sanitiseConfig (const Config& input) noexcept
{
    Config result = input;

    switch (input.waveform)
    {
        case Waveform::Sine:
        case Waveform::Triangle:
        case Waveform::Square:
        case Waveform::SawUp:
        case Waveform::SawDown:
            break;
        default:
            result.waveform = Waveform::Sine;
            break;
    }

    switch (input.rateMode)
    {
        case RateMode::Hertz:
        case RateMode::Sync:
            break;
        default:
            result.rateMode = RateMode::Hertz;
            break;
    }

    switch (input.syncDivision)
    {
        case SyncDivision::EightBars:
        case SyncDivision::FourBars:
        case SyncDivision::TwoBars:
        case SyncDivision::OneBar:
        case SyncDivision::Half:
        case SyncDivision::Quarter:
        case SyncDivision::Eighth:
        case SyncDivision::Sixteenth:
        case SyncDivision::ThirtySecond:
        case SyncDivision::SixtyFourth:
            break;
        default:
            result.syncDivision = SyncDivision::Quarter;
            break;
    }

    result.rateHz = std::max(0.01, std::min(20.0,
                             finiteOr(input.rateHz, 1.0)));
    return result;
}

double CrowdLfoGate::divisionQuarterNotes (SyncDivision division) noexcept
{
    switch (division)
    {
        case SyncDivision::EightBars:    return 32.0;
        case SyncDivision::FourBars:     return 16.0;
        case SyncDivision::TwoBars:      return 8.0;
        case SyncDivision::OneBar:       return 4.0;
        case SyncDivision::Half:         return 2.0;
        case SyncDivision::Quarter:      return 1.0;
        case SyncDivision::Eighth:       return 0.5;
        case SyncDivision::Sixteenth:    return 0.25;
        case SyncDivision::ThirtySecond: return 0.125;
        case SyncDivision::SixtyFourth:  return 0.0625;
        default:                         return 1.0;
    }
}

double CrowdLfoGate::positiveFraction (double value) noexcept
{
    if (! std::isfinite(value))
        return 0.0;

    double result = value - std::floor(value);
    if (result >= 1.0)
        result = 0.0;
    if (result < 0.0)
        result += 1.0;
    return result;
}

double CrowdLfoGate::waveformValue (Waveform waveform, double phase) noexcept
{
    const double p = positiveFraction(phase);
    switch (waveform)
    {
        case Waveform::Sine:
            return 0.5 + 0.5 * std::sin(kTwoPi * p);
        case Waveform::Triangle:
            return 1.0 - 2.0 * std::abs(p - 0.5);
        case Waveform::Square:
            return p < 0.5 ? 1.0 : 0.0;
        case Waveform::SawUp:
            return p;
        case Waveform::SawDown:
            return 1.0 - p;
        default:
            return 0.5 + 0.5 * std::sin(kTwoPi * p);
    }
}

bool CrowdLfoGate::validHostClock (const ClockFrame& frame) noexcept
{
    if (! frame.hostValid || ! frame.isPlaying
        || ! std::isfinite(frame.sampleRate) || frame.sampleRate <= 0.0
        || ! std::isfinite(frame.bpm) || frame.bpm <= 0.0
        || frame.bpm > 1000.0
        || ! std::isfinite(frame.ppqPosition)
        || std::abs(frame.ppqPosition) > kMaximumClockMagnitude)
        return false;

    const double sampleRate = std::max(1.0, frame.sampleRate);
    const int sampleCount = std::max(0, frame.numSamples);
    const double ppqEnd = frame.ppqPosition
                        + static_cast<double>(sampleCount) * frame.bpm
                            / (60.0 * sampleRate);
    return std::isfinite(ppqEnd)
        && std::abs(ppqEnd) <= kMaximumClockMagnitude;
}

bool CrowdLfoGate::gateStateAtPhase (Waveform waveform, double phase) noexcept
{
    const double p = positiveFraction(phase);
    // These are the exact >= 0.5 domains of waveformValue(). Keeping the binary
    // comparator analytic avoids a trigonometric call per sample and removes
    // libm rounding ambiguity at sine's zero crossings.
    switch (waveform)
    {
        case Waveform::Sine:     return p <= 0.5;
        case Waveform::Triangle: return p >= 0.25 && p <= 0.75;
        case Waveform::Square:   return p < 0.5;
        case Waveform::SawUp:    return p >= 0.5;
        case Waveform::SawDown:  return p <= 0.5;
        default:                 return p <= 0.5;
    }
}

void CrowdLfoGate::setGate (bool shouldOpen, int sampleOffset,
                            OutputBlock& output) noexcept
{
    if (shouldOpen == gateOpen_)
        return;

    gateOpen_ = shouldOpen;
    if (output.count >= kMaxTransitionsPerBlock)
    {
        output.overflowed = true;
        output.resetRequested = true;
        return;
    }

    output.transitions[static_cast<std::size_t>(output.count++)]
        = { shouldOpen ? Transition::Type::Open : Transition::Type::Close,
            std::max(0, sampleOffset) };
}

void CrowdLfoGate::process (const Config& unsafeConfig,
                            const ClockFrame& unsafeFrame,
                            OutputBlock& output) noexcept
{
    output = {};
    const Config config = sanitiseConfig(unsafeConfig);

    const double sampleRate = std::max(1.0,
        std::min(768000.0, finiteOr(unsafeFrame.sampleRate, 44100.0)));
    const int numSamples = std::max(0, unsafeFrame.numSamples);
    const double division = divisionQuarterNotes(config.syncDivision);
    const bool hostPrimary = config.rateMode == RateMode::Sync
                          && validHostClock(unsafeFrame);
    const double fallbackBpm = std::max(20.0, std::min(400.0,
        finiteOr(unsafeFrame.fallbackBpm, 120.0)));
    const double monotonicSeconds = std::max(-kMaximumClockMagnitude,
        std::min(kMaximumClockMagnitude,
                 finiteOr(unsafeFrame.monotonicSeconds, 0.0)));

    double baseCycles = 0.0;
    double phaseIncrement = 0.0;
    if (hostPrimary)
    {
        baseCycles = unsafeFrame.ppqPosition / division;
        phaseIncrement = unsafeFrame.bpm / (60.0 * sampleRate * division);
        output.effectiveRateHz = unsafeFrame.bpm / (60.0 * division);
        output.hostLocked = true;
    }
    else if (config.rateMode == RateMode::Sync)
    {
        output.effectiveRateHz = fallbackBpm / (60.0 * division);
        baseCycles = monotonicSeconds * output.effectiveRateHz;
        phaseIncrement = output.effectiveRateHz / sampleRate;
    }
    else
    {
        output.effectiveRateHz = config.rateHz;
        baseCycles = monotonicSeconds * config.rateHz;
        phaseIncrement = config.rateHz / sampleRate;
    }

    output.active = config.enabled;
    output.phaseAtStart = positiveFraction(baseCycles);
    output.valueAtStart = config.enabled
                        ? waveformValue(config.waveform, output.phaseAtStart) : 1.0;

    // A zero-sample call still applies a parameter/transport boundary at offset
    // zero, but never advances the absolute clock or creates a negative offset.
    const int evaluations = std::max(1, numSamples);
    double phase = output.phaseAtStart;
    for (int sample = 0; sample < evaluations; ++sample)
    {
        if (sample > 0)
            phase = positiveFraction(phase + phaseIncrement);

        if (! config.enabled)
            setGate(true, sample, output);
        else
            setGate(gateStateAtPhase(config.waveform, phase), sample, output);
    }

    output.phaseAtEnd = positiveFraction(baseCycles
                                          + static_cast<double>(numSamples)
                                              * phaseIncrement);
    output.valueAtEnd = config.enabled
                      ? waveformValue(config.waveform, output.phaseAtEnd) : 1.0;
    output.gateOpen = gateOpen_;

    // Never expose a lifecycle prefix after capacity loss: callers can perform
    // one ordered release sweep, then use final gateOpen to choose re-pending.
    if (output.resetRequested)
        output.count = 0;
}

void CrowdLfoGate::reset() noexcept
{
    gateOpen_ = true;
}
