#include "CrowdTimeField.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace
{
    constexpr double kBeatEpsilon = 1.0e-9;
    constexpr double kMaxBeatMagnitude = 1.0e12;
    constexpr std::int64_t kMaxExactStepIndex = 4503599627370495LL; // 2^52 - 1
    constexpr int kMaxTimelineIterations = CrowdTimeField::kMaxInputEventsPerBlock
                                          + 4 * CrowdTimeField::kMaxOutputEvents + 1;

    double finiteOr (double value, double fallback) noexcept
    {
        return std::isfinite(value) ? value : fallback;
    }

    int positiveModulo (std::int64_t value, int modulus) noexcept
    {
        const auto m = static_cast<std::int64_t>(modulus);
        const auto result = ((value % m) + m) % m;
        return static_cast<int>(result);
    }

    std::int64_t firstStepAtOrAfter (double beat, double stepBeats) noexcept
    {
        const double scaled = beat / stepBeats;
        const double nearest = std::round(scaled);
        double result = std::abs(scaled - nearest) <= kBeatEpsilon
                      ? nearest : std::ceil(scaled);

        const double lo = static_cast<double>(-kMaxExactStepIndex);
        const double hi = static_cast<double>( kMaxExactStepIndex);
        result = std::max(lo, std::min(hi, result));
        return static_cast<std::int64_t>(result);
    }

    bool beatWithinDomain (double beat) noexcept
    {
        return std::isfinite(beat) && std::abs(beat) <= kMaxBeatMagnitude;
    }

    double pendingLifetimeBeats (const CrowdTimeField::Config& config) noexcept
    {
        if (config.mode != CrowdTimeField::Mode::Ensemble)
            return 1.0;
        return std::max(1.0, CrowdTimeField::divisionQuarterNotes(config.division)
                              * static_cast<double>(config.spreadSlots));
    }

    std::uint32_t mixLaneSeed (std::uint32_t value) noexcept
    {
        if (value == 0)
            return 0;
        value ^= value >> 16;
        value *= 0x7feb352dU;
        value ^= value >> 15;
        value *= 0x846ca68bU;
        value ^= value >> 16;
        return value;
    }

    int laneForSource (const CrowdTimeField::Config& config, int sourceId) noexcept
    {
        const int seedOffset = static_cast<int>(mixLaneSeed(config.laneSeed)
                                                % static_cast<std::uint32_t>(config.spreadSlots));
        return (sourceId + seedOffset) % config.spreadSlots;
    }

    void saturatingAdd (std::uint32_t& destination, std::uint32_t amount) noexcept
    {
        const auto room = std::numeric_limits<std::uint32_t>::max() - destination;
        destination += std::min(room, amount);
    }
}

struct CrowdTimeField::EventCollector
{
    struct TaggedEvent
    {
        OutputEvent event {};
        std::uint32_t sequence = 0;
    };

    std::array<TaggedEvent, kMaxOutputEvents> releases {};
    std::array<TaggedEvent, kMaxOutputEvents> attacks {};
    std::array<TaggedEvent, kMaxOutputEvents> motions {};
    int releaseCount = 0;
    int attackCount = 0;
    int motionCount = 0;
    int reservedReleaseCount = 0;
    std::uint32_t nextSequence = 0;
    std::uint32_t droppedMotions = 0;
    bool mandatoryOverflow = false;

    int committedAndReserved() const noexcept
    {
        return releaseCount + attackCount + reservedReleaseCount;
    }

    void push (OutputEvent::Type type, int voiceId, int sourceId,
               int sampleOffset) noexcept
    {
        TaggedEvent tagged;
        tagged.event = { type, voiceId, sourceId, sampleOffset };
        tagged.sequence = nextSequence++;

        if (type == OutputEvent::Type::Release)
        {
            if (releaseCount < kMaxOutputEvents)
                releases[static_cast<std::size_t>(releaseCount++)] = tagged;
            else
                mandatoryOverflow = true;
            return;
        }

        if (type == OutputEvent::Type::Attack)
        {
            if (attackCount < kMaxOutputEvents)
                attacks[static_cast<std::size_t>(attackCount++)] = tagged;
            else
                mandatoryOverflow = true;
            return;
        }

        if (motionCount < kMaxOutputEvents)
            motions[static_cast<std::size_t>(motionCount++)] = tagged;
        else
            ++droppedMotions;
    }

    bool writeTo (OutputBlock& output) noexcept
    {
        if (mandatoryOverflow || releaseCount + attackCount > kMaxOutputEvents)
            return false;

        std::array<std::uint32_t, kMaxOutputEvents> sequences {};
        int written = 0;

        const auto append = [&] (const TaggedEvent& tagged) noexcept
        {
            output.events[static_cast<std::size_t>(written)] = tagged.event;
            sequences[static_cast<std::size_t>(written)] = tagged.sequence;
            ++written;
        };

        // Capacity is selected by semantic priority. Mandatory releases are
        // retained before attacks; SampleMotion is best-effort telemetry.
        for (int i = 0; i < releaseCount; ++i)
            append(releases[static_cast<std::size_t>(i)]);
        for (int i = 0; i < attackCount; ++i)
            append(attacks[static_cast<std::size_t>(i)]);

        const int motionRoom = kMaxOutputEvents - written;
        const int keptMotions = std::min(motionRoom, motionCount);
        for (int i = 0; i < keptMotions; ++i)
            append(motions[static_cast<std::size_t>(i)]);
        droppedMotions += static_cast<std::uint32_t>(motionCount - keptMotions);

        // Restore chronological FIFO order after priority-based capacity
        // selection. This preserves Flow On->Off at one offset and guarantees an
        // Ensemble gate Release precedes its same-tick retrigger.
        for (int i = 1; i < written; ++i)
        {
            const auto event = output.events[static_cast<std::size_t>(i)];
            const auto sequence = sequences[static_cast<std::size_t>(i)];
            int j = i;
            while (j > 0)
            {
                const auto previousIndex = static_cast<std::size_t>(j - 1);
                const bool comesBefore = event.sampleOffset
                                           < output.events[previousIndex].sampleOffset
                                      || (event.sampleOffset
                                            == output.events[previousIndex].sampleOffset
                                          && sequence < sequences[previousIndex]);
                if (! comesBefore)
                    break;

                output.events[static_cast<std::size_t>(j)] = output.events[previousIndex];
                sequences[static_cast<std::size_t>(j)] = sequences[previousIndex];
                --j;
            }
            output.events[static_cast<std::size_t>(j)] = event;
            sequences[static_cast<std::size_t>(j)] = sequence;
        }

        output.count = written;
        return true;
    }
};

CrowdTimeField::CrowdTimeField() noexcept
{
    reset();
}

CrowdTimeField::Config CrowdTimeField::sanitiseConfig (const Config& input) noexcept
{
    Config result = input;

    switch (input.mode)
    {
        case Mode::Flow:
        case Mode::Grid:
        case Mode::Ensemble:
            break;
        default:
            result.mode = Mode::Flow;
            break;
    }

    switch (input.clockSource)
    {
        case ClockSource::Host:
        case ClockSource::Internal:
            break;
        default:
            result.clockSource = ClockSource::Host;
            break;
    }

    switch (input.division)
    {
        case Division::Quarter:
        case Division::Eighth:
        case Division::Sixteenth:
        case Division::ThirtySecond:
            break;
        default:
            result.division = Division::Sixteenth;
            break;
    }

    result.internalBpm = std::max(40.0, std::min(240.0,
                                  finiteOr(input.internalBpm, 120.0)));
    result.maxAttacksPerStep = std::max(1, std::min(16, input.maxAttacksPerStep));
    result.maxActive = std::max(1, std::min(16, input.maxActive));
    result.gatePercent = std::max(5.0, std::min(100.0,
                                finiteOr(input.gatePercent, 70.0)));

    const int slots = input.spreadSlots;
    result.spreadSlots = slots <= 1 ? 1
                       : slots <= 2 ? 2
                       : slots <= 4 ? 4
                       : slots <= 8 ? 8 : 16;
    return result;
}

double CrowdTimeField::divisionQuarterNotes (Division division) noexcept
{
    switch (division)
    {
        case Division::Quarter:       return 1.0;
        case Division::Eighth:        return 0.5;
        case Division::Sixteenth:     return 0.25;
        case Division::ThirtySecond:  return 0.125;
        default:                      return 0.25;
    }
}

bool CrowdTimeField::validIdentity (int voiceId, int sourceId) noexcept
{
    return voiceId >= 0 && voiceId < kMaxVoices
        && sourceId >= 0 && sourceId < kMaxSources
        && sourceIdForVoice(voiceId) == sourceId;
}

int CrowdTimeField::clampedOffset (int sampleOffset, int numSamples) noexcept
{
    const int maximum = std::max(0, numSamples - 1);
    return std::max(0, std::min(maximum, sampleOffset));
}

CrowdTimeField::ResolvedClock CrowdTimeField::resolveClock (const Config& config,
                                                             const ClockFrame& frame) noexcept
{
    ResolvedClock result;
    result.sampleRate = std::max(1.0, finiteOr(frame.sampleRate, 44100.0));
    result.numSamples = std::max(0, frame.numSamples);
    result.monotonicSeconds = finiteOr(frame.monotonicSeconds, 0.0);

    const double durationSeconds = static_cast<double>(result.numSamples)
                                 / result.sampleRate;
    const double hostBpm = std::max(1.0, std::min(1000.0,
                                   finiteOr(frame.bpm, 120.0)));
    const double hostBeatEnd = frame.ppqPosition
                             + durationSeconds * hostBpm / 60.0;

    const bool hostUsable = config.clockSource == ClockSource::Host
                         && frame.hostValid && frame.isPlaying
                         && std::isfinite(frame.bpm) && frame.bpm > 0.0
                         && beatWithinDomain(frame.ppqPosition)
                         && beatWithinDomain(hostBeatEnd)
                         && (result.numSamples <= 0
                             || hostBeatEnd > frame.ppqPosition);

    if (hostUsable)
    {
        result.hostPrimary = true;
        result.clockLocked = true;
        result.running = true;
        result.bpm = hostBpm;
        result.beatStart = frame.ppqPosition;
        result.beatEnd = hostBeatEnd;
    }
    else
    {
        // Absolute monotonic phase is intentionally not accumulated per block:
        // separate instances supplied the same timestamp land on the same grid.
        result.hostPrimary = false;
        result.clockLocked = config.clockSource == ClockSource::Internal;
        result.running = true;
        result.bpm = config.internalBpm;
        const double fallbackBeatStart = result.monotonicSeconds * result.bpm / 60.0;
        const double fallbackBeatEnd = fallbackBeatStart
                                     + durationSeconds * result.bpm / 60.0;
        result.validDomain = beatWithinDomain(fallbackBeatStart)
                          && beatWithinDomain(fallbackBeatEnd)
                          && (result.numSamples <= 0
                              || fallbackBeatEnd > fallbackBeatStart);
        if (result.validDomain)
        {
            result.beatStart = fallbackBeatStart;
            result.beatEnd = fallbackBeatEnd;
        }
    }
    return result;
}

bool CrowdTimeField::configsEqual (const Config& a, const Config& b) noexcept
{
    return a.mode == b.mode
        && a.clockSource == b.clockSource
        && a.division == b.division
        && (a.clockSource == ClockSource::Host
            || std::abs(a.internalBpm - b.internalBpm) <= 1.0e-9)
        && a.maxAttacksPerStep == b.maxAttacksPerStep
        && a.maxActive == b.maxActive
        && std::abs(a.gatePercent - b.gatePercent) <= 1.0e-9
        && a.spreadSlots == b.spreadSlots
        && a.laneSeed == b.laneSeed;
}

void CrowdTimeField::clearVoiceState() noexcept
{
    for (auto& voice : voices_)
        voice = VoiceState {};

    timedActiveCount_ = 0;
    pendingCount_ = 0;
    activeCount_ = 0;
    nextGateEndBeat_ = std::numeric_limits<double>::infinity();
    nextPendingExpiryBeat_ = std::numeric_limits<double>::infinity();
    pendingAnchorDeferred_ = false;
}

void CrowdTimeField::reset() noexcept
{
    clearVoiceState();
    config_ = {};
    domainInitialised_ = false;
    lastHostPrimary_ = false;
    lastRunning_ = true;
    lastSampleRate_ = 44100.0;
    lastBpm_ = 120.0;
    lastBeatStart_ = 0.0;
    lastBeatEnd_ = 0.0;
    lastMonotonicSeconds_ = 0.0;
    lastNumSamples_ = 0;
    nextStepIndex_ = 0;
    fairCursor_ = -1;
    mergedCount_ = 0;
    droppedMotionCount_ = 0;
}

void CrowdTimeField::updateClockHistory (const ResolvedClock& clock) noexcept
{
    lastHostPrimary_ = clock.hostPrimary;
    lastRunning_ = clock.running;
    lastSampleRate_ = clock.sampleRate;
    lastBpm_ = clock.bpm;
    lastBeatStart_ = clock.beatStart;
    lastBeatEnd_ = clock.beatEnd;
    lastMonotonicSeconds_ = clock.monotonicSeconds;
    lastNumSamples_ = clock.numSamples;
}

void CrowdTimeField::adoptDomain (const Config& config,
                                  const ResolvedClock& clock) noexcept
{
    config_ = config;
    domainInitialised_ = true;
    nextStepIndex_ = firstStepAtOrAfter(clock.beatStart,
                                        divisionQuarterNotes(config.division));
    updateClockHistory(clock);
}

bool CrowdTimeField::domainChanged (const Config& config,
                                    const ResolvedClock& clock) const noexcept
{
    if (! domainInitialised_)
        return false;

    if (! configsEqual(config_, config)
        || clock.hostPrimary != lastHostPrimary_
        || clock.running != lastRunning_
        || std::abs(clock.sampleRate - lastSampleRate_)
             > std::max(1.0, lastSampleRate_) * 1.0e-9
        || (! clock.hostPrimary && std::abs(clock.bpm - lastBpm_) > 1.0e-7))
        return true;

    if (clock.hostPrimary)
    {
        const double beatsPerSample = clock.bpm / (60.0 * clock.sampleRate);
        const double previousDuration = static_cast<double>(lastNumSamples_)
                                      / std::max(1.0, lastSampleRate_);
        const double tempoRampAllowance = previousDuration
                                        * std::abs(clock.bpm - lastBpm_) / 60.0;
        const double tolerance = std::max(1.0e-7, 2.0 * beatsPerSample)
                               + tempoRampAllowance;
        return std::abs(clock.beatStart - lastBeatEnd_) > tolerance;
    }

    // A monotonic clock may jitter with callback scheduling, so only backwards
    // motion or a clearly missing callback is treated as a transport boundary.
    const double expectedSeconds = static_cast<double>(lastNumSamples_)
                                 / std::max(1.0, lastSampleRate_);
    const double actualSeconds = clock.monotonicSeconds - lastMonotonicSeconds_;
    const double toleranceSeconds = std::max(0.050, expectedSeconds * 4.0);
    return actualSeconds < -1.0e-9
        || std::abs(actualSeconds - expectedSeconds) > toleranceSeconds;
}

int CrowdTimeField::rehydrate (const HeldVoice* held, int count) noexcept
{
    clearVoiceState();
    fairCursor_ = -1;

    if (held == nullptr || count <= 0)
        return 0;

    count = std::min(count, kMaxVoices);

    const bool deferAnchor = ! domainInitialised_;
    const double anchorBeat = deferAnchor ? 0.0 : lastBeatEnd_;
    const double lifetimeBeats = deferAnchor ? 0.0 : pendingLifetimeBeats(config_);
    int accepted = 0;
    for (int i = 0; i < count; ++i)
    {
        const auto& item = held[i];
        if (! validIdentity(item.voiceId, item.sourceId))
            continue;

        auto& voice = voices_[static_cast<std::size_t>(item.voiceId)];
        if (voice.held)
            continue;

        voice.held = true;
        voice.pending = true;
        voice.sourceId = item.sourceId;
        voice.pendingSinceBeat = anchorBeat;
        voice.pendingDeadlineBeat = deferAnchor
                                  ? std::numeric_limits<double>::infinity()
                                  : anchorBeat + lifetimeBeats;
        ++accepted;
    }

    pendingCount_ = accepted;
    pendingAnchorDeferred_ = deferAnchor && accepted > 0;
    nextPendingExpiryBeat_ = accepted > 0 && ! deferAnchor
                           ? anchorBeat + lifetimeBeats
                           : std::numeric_limits<double>::infinity();
    return accepted;
}

void CrowdTimeField::processFlow (const ResolvedClock& clock,
                                  const InputEvent* inputs, int inputCount,
                                  EventCollector& collector) noexcept
{
    for (int i = 0; i < inputCount; ++i)
    {
        const auto& event = inputs[i];
        if (! validIdentity(event.voiceId, event.sourceId))
            continue;
        if (event.type != InputEvent::Type::On
            && event.type != InputEvent::Type::Off)
            continue;

        const int offset = clampedOffset(event.sampleOffset, clock.numSamples);
        auto& voice = voices_[static_cast<std::size_t>(event.voiceId)];
        voice.sourceId = event.sourceId;

        if (event.type == InputEvent::Type::On)
        {
            if (voice.held)
                continue;

            voice.held = true;
            if (! voice.sounding && ! voice.pending)
            {
                voice.sounding = true;
                ++activeCount_;
                collector.push(OutputEvent::Type::Attack, event.voiceId,
                               event.sourceId, offset);
            }
            continue;
        }

        if (! voice.held && ! voice.sounding && ! voice.pending)
            continue;

        voice.held = false;
        clearPending(event.voiceId);
        if (voice.sounding)
            stopVoice(event.voiceId, offset, collector);
    }

    // Rehydration is intentionally bounded and comes after fresh lifecycle
    // input, so an Off at this block's start can cancel a stale retrigger.
    int room = kMaxOutputEvents - collector.releaseCount - collector.attackCount;
    if (room <= 0 || pendingCount_ <= 0)
        return;

    int cursor = fairCursor_;
    for (int scanned = 0; scanned < kMaxVoices && room > 0; ++scanned)
    {
        const int voiceId = (cursor + 1 + scanned) % kMaxVoices;
        auto& voice = voices_[static_cast<std::size_t>(voiceId)];
        if (! voice.pending || ! voice.held || voice.sounding)
            continue;

        clearPending(voiceId);
        voice.sounding = true;
        ++activeCount_;
        collector.push(OutputEvent::Type::Attack, voiceId, voice.sourceId, 0);
        fairCursor_ = voiceId;
        --room;
    }
}

void CrowdTimeField::handleTimedInput (const Config& config,
                                       const InputEvent& event, int offset,
                                       double eventBeat,
                                       EventCollector& collector) noexcept
{
    if (! validIdentity(event.voiceId, event.sourceId))
        return;
    if (event.type != InputEvent::Type::On
        && event.type != InputEvent::Type::Off)
        return;

    auto& voice = voices_[static_cast<std::size_t>(event.voiceId)];
    voice.sourceId = event.sourceId;

    if (event.type == InputEvent::Type::On)
    {
        if (voice.held)
            return;

        voice.held = true;
        if (voice.sounding)
        {
            // A Grid short tap that is touched again before its minimum gate
            // becomes an ordinary held note. Ensemble always keeps fixed gates.
            if (config.mode == Mode::Grid)
            {
                voice.gateEndBeat = std::numeric_limits<double>::infinity();
                refreshNextGateEnd();
            }
            return;
        }

        if (! voice.pending)
            setPending(event.voiceId, eventBeat, pendingLifetimeBeats(config));
        return;
    }

    if (! voice.held && ! voice.sounding && ! voice.pending)
        return;

    voice.held = false;
    if (config.mode == Mode::Grid && voice.sounding)
        stopVoice(event.voiceId, offset, collector);
    // A pending Off remains as a one-shot for at least one beat; Ensemble extends
    // that lifetime to a full spread-lane cycle. This lets a tap shorter than the
    // quantisation wait produce a safe minimum gate. Ensemble voices already
    // sounding retain their fixed gate.
}

void CrowdTimeField::stopVoice (int voiceId, int offset,
                                EventCollector& collector) noexcept
{
    auto& voice = voices_[static_cast<std::size_t>(voiceId)];
    if (! voice.sounding)
        return;

    if (voice.releaseReservedThisBlock)
    {
        voice.releaseReservedThisBlock = false;
        if (collector.reservedReleaseCount > 0)
            --collector.reservedReleaseCount;
    }
    collector.push(OutputEvent::Type::Release, voiceId, voice.sourceId, offset);
    voice.sounding = false;
    voice.gateEndBeat = std::numeric_limits<double>::infinity();
    removeTimedActive(voiceId);
    if (activeCount_ > 0)
        --activeCount_;
}

void CrowdTimeField::setPending (int voiceId, double sinceBeat,
                                 double lifetimeBeats) noexcept
{
    auto& voice = voices_[static_cast<std::size_t>(voiceId)];
    if (! voice.pending)
    {
        voice.pending = true;
        ++pendingCount_;
    }
    voice.pendingSinceBeat = sinceBeat;
    voice.pendingDeadlineBeat = sinceBeat + lifetimeBeats;
    nextPendingExpiryBeat_ = std::min(nextPendingExpiryBeat_,
                                      voice.pendingDeadlineBeat);
}

void CrowdTimeField::clearPending (int voiceId) noexcept
{
    auto& voice = voices_[static_cast<std::size_t>(voiceId)];
    if (! voice.pending)
        return;

    voice.pending = false;
    voice.pendingDeadlineBeat = std::numeric_limits<double>::infinity();
    if (pendingCount_ > 0)
        --pendingCount_;
    if (pendingCount_ == 0)
        nextPendingExpiryBeat_ = std::numeric_limits<double>::infinity();
}

void CrowdTimeField::addTimedActive (int voiceId) noexcept
{
    for (int i = 0; i < timedActiveCount_; ++i)
        if (timedActiveVoiceIds_[static_cast<std::size_t>(i)] == voiceId)
            return;

    if (timedActiveCount_ < static_cast<int>(timedActiveVoiceIds_.size()))
        timedActiveVoiceIds_[static_cast<std::size_t>(timedActiveCount_++)] = voiceId;
}

void CrowdTimeField::removeTimedActive (int voiceId) noexcept
{
    for (int i = 0; i < timedActiveCount_; ++i)
    {
        if (timedActiveVoiceIds_[static_cast<std::size_t>(i)] != voiceId)
            continue;

        --timedActiveCount_;
        timedActiveVoiceIds_[static_cast<std::size_t>(i)]
            = timedActiveVoiceIds_[static_cast<std::size_t>(timedActiveCount_)];
        refreshNextGateEnd();
        return;
    }
}

void CrowdTimeField::refreshNextGateEnd() noexcept
{
    nextGateEndBeat_ = std::numeric_limits<double>::infinity();
    for (int i = 0; i < timedActiveCount_; ++i)
    {
        const int voiceId = timedActiveVoiceIds_[static_cast<std::size_t>(i)];
        nextGateEndBeat_ = std::min(nextGateEndBeat_,
            voices_[static_cast<std::size_t>(voiceId)].gateEndBeat);
    }
}

void CrowdTimeField::refreshNextPendingExpiry() noexcept
{
    nextPendingExpiryBeat_ = std::numeric_limits<double>::infinity();
    if (pendingCount_ <= 0)
        return;

    for (const auto& voice : voices_)
        if (voice.pending)
            nextPendingExpiryBeat_ = std::min(nextPendingExpiryBeat_,
                                               voice.pendingDeadlineBeat);
}

void CrowdTimeField::processGateReleases (const Config& config,
                                          double releaseBeat, int offset,
                                          EventCollector& collector) noexcept
{
    for (int activeIndex = 0; activeIndex < timedActiveCount_;)
    {
        const int voiceId = timedActiveVoiceIds_[static_cast<std::size_t>(activeIndex)];
        auto& voice = voices_[static_cast<std::size_t>(voiceId)];
        if (! voice.sounding || voice.gateEndBeat > releaseBeat + kBeatEpsilon)
        {
            ++activeIndex;
            continue;
        }

        const double actualReleaseBeat = voice.gateEndBeat;
        stopVoice(voiceId, offset, collector);
        if (config.mode == Mode::Ensemble && voice.held)
            setPending(voiceId, actualReleaseBeat, pendingLifetimeBeats(config));
        // stopVoice removes this entry by swapping in the final active id, so do
        // not increment activeIndex after a release.
    }
}

void CrowdTimeField::incrementMerged() noexcept
{
    if (mergedCount_ != std::numeric_limits<std::uint32_t>::max())
        ++mergedCount_;
}

void CrowdTimeField::processPendingExpiry (double expiryBeat) noexcept
{
    for (int voiceId = 0; voiceId < kMaxVoices; ++voiceId)
    {
        auto& voice = voices_[static_cast<std::size_t>(voiceId)];
        if (! voice.pending
            || voice.pendingDeadlineBeat > expiryBeat + kBeatEpsilon)
            continue;

        incrementMerged();
        if (voice.held)
        {
            // Continued pressure is coalesced into one fresh request instead of
            // disappearing; a short tap has no such requeue and expires here.
            const double lifetimeBeats = std::max(kBeatEpsilon,
                voice.pendingDeadlineBeat - voice.pendingSinceBeat);
            voice.pendingSinceBeat = expiryBeat;
            voice.pendingDeadlineBeat = expiryBeat + lifetimeBeats;
        }
        else
        {
            clearPending(voiceId);
        }
    }

    refreshNextPendingExpiry();
}

void CrowdTimeField::startAttack (const Config& config, int voiceId,
                                  std::int64_t stepIndex, double attackBeat,
                                  int offset, EventCollector& collector) noexcept
{
    auto& voice = voices_[static_cast<std::size_t>(voiceId)];
    if (voice.sounding || ! voice.pending)
        return;

    clearPending(voiceId);
    voice.sounding = true;
    voice.lastAttackStep = stepIndex;
    ++activeCount_;
    addTimedActive(voiceId);
    voice.releaseReservedThisBlock = true;
    ++collector.reservedReleaseCount;

    const bool fixedGate = config.mode == Mode::Ensemble || ! voice.held;
    voice.gateEndBeat = fixedGate
                      ? attackBeat + divisionQuarterNotes(config.division)
                                   * config.gatePercent / 100.0
                      : std::numeric_limits<double>::infinity();
    nextGateEndBeat_ = std::min(nextGateEndBeat_, voice.gateEndBeat);

    collector.push(OutputEvent::Type::Attack, voiceId, voice.sourceId, offset);
}

void CrowdTimeField::processGridTick (const Config& config,
                                      std::int64_t stepIndex,
                                      double tickBeat, int offset,
                                      EventCollector& collector) noexcept
{
    const int attackLimit = std::min(config.maxAttacksPerStep,
                                     std::max(0, config.maxActive - activeCount_));
    const int activeLane = positiveModulo(stepIndex, config.spreadSlots);
    int selected = 0;
    int cursor = fairCursor_;

    for (int scanned = 0; pendingCount_ > 0
                           && scanned < kMaxVoices
                           && selected < attackLimit; ++scanned)
    {
        const int voiceId = (cursor + 1 + scanned) % kMaxVoices;
        const auto& candidate = voices_[static_cast<std::size_t>(voiceId)];
        if (! candidate.pending)
            continue;
        if (config.mode == Mode::Ensemble
            && laneForSource(config, candidate.sourceId) != activeLane)
            continue;
        if (collector.committedAndReserved() + 2 > kMaxOutputEvents)
            break;

        startAttack(config, voiceId, stepIndex, tickBeat, offset, collector);
        fairCursor_ = voiceId;
        ++selected;
    }

    // Timed modes refresh current expression/pitch from the processor's latest
    // snapshot once per base grid boundary. Attack already samples it, so a
    // second SampleMotion for the same voice/tick would be redundant.
    for (int activeIndex = 0; activeIndex < timedActiveCount_; ++activeIndex)
    {
        const int voiceId = timedActiveVoiceIds_[static_cast<std::size_t>(activeIndex)];
        const auto& voice = voices_[static_cast<std::size_t>(voiceId)];
        if (voice.sounding && voice.held && voice.lastAttackStep != stepIndex)
            collector.push(OutputEvent::Type::SampleMotion, voiceId,
                           voice.sourceId, offset);
    }
}

void CrowdTimeField::processTimed (const Config& config,
                                   const ResolvedClock& clock,
                                   const InputEvent* inputs, int inputCount,
                                   EventCollector& collector) noexcept
{
    const double beatPerSample = clock.running
                               ? clock.bpm / (60.0 * clock.sampleRate) : 0.0;
    const double halfSampleBeat = beatPerSample * 0.5 + kBeatEpsilon;
    const double stepBeats = divisionQuarterNotes(config.division);
    int inputIndex = 0;

    // Reserve one mandatory Release for every sounding timed voice. A future
    // Off or fixed-gate expiry can then never be displaced by later attacks in
    // a large/offline block. Unused reservations cost no output slots at write.
    for (int i = 0; i < timedActiveCount_; ++i)
    {
        auto& voice = voices_[static_cast<std::size_t>(
            timedActiveVoiceIds_[static_cast<std::size_t>(i)])];
        voice.releaseReservedThisBlock = true;
        ++collector.reservedReleaseCount;
    }

    // A boundary that rounded a fraction behind this block belongs at sample 0;
    // anything older is skipped (normally only possible after first adoption).
    const auto firstCurrentStep = firstStepAtOrAfter(clock.beatStart - halfSampleBeat,
                                                     stepBeats);
    if (nextStepIndex_ < firstCurrentStep)
        nextStepIndex_ = firstCurrentStep;

    int timelineIterations = 0;
    for (;;)
    {
        if (++timelineIterations > kMaxTimelineIterations)
        {
            collector.mandatoryOverflow = true;
            return;
        }

        enum class Due : std::uint8_t { None, Input, Gate, Tick, Expiry };
        Due due = Due::None;
        double dueBeat = std::numeric_limits<double>::infinity();
        int duePriority = 99;

        const auto consider = [&] (Due candidate, double beat, int priority) noexcept
        {
            if (! std::isfinite(beat))
                return;
            if (beat > clock.beatEnd + kBeatEpsilon)
                return;
            // The audio block is half-open. A zero-sample block accepts input
            // state but never manufactures a clock event.
            if (candidate != Due::Input
                && (clock.numSamples <= 0
                    || beat >= clock.beatEnd - kBeatEpsilon))
                return;

            if (beat < dueBeat - kBeatEpsilon
                || (std::abs(beat - dueBeat) <= kBeatEpsilon
                    && priority < duePriority))
            {
                due = candidate;
                dueBeat = beat;
                duePriority = priority;
            }
        };

        if (inputIndex < inputCount)
        {
            const int offset = clampedOffset(inputs[inputIndex].sampleOffset,
                                             clock.numSamples);
            const double eventBeat = clock.beatStart
                                   + static_cast<double>(offset) * beatPerSample;
            consider(Due::Input, eventBeat, 0);
        }

        if (clock.running)
        {
            if (timedActiveCount_ > 0)
                consider(Due::Gate, nextGateEndBeat_, 1);
            consider(Due::Tick, static_cast<double>(nextStepIndex_) * stepBeats, 2);
            if (pendingCount_ > 0)
                consider(Due::Expiry, nextPendingExpiryBeat_, 3);
        }

        if (due == Due::None)
            break;

        const double offsetSamples = beatPerSample > 0.0
                                   ? (dueBeat - clock.beatStart) / beatPerSample
                                   : 0.0;
        const int roundedOffset = offsetSamples <= 0.0 ? 0
                                : offsetSamples >= static_cast<double>(clock.numSamples)
                                    ? std::max(0, clock.numSamples - 1)
                                    : static_cast<int>(std::llround(offsetSamples));
        const int offset = clampedOffset(roundedOffset, clock.numSamples);

        switch (due)
        {
            case Due::Input:
                handleTimedInput(config, inputs[inputIndex],
                                 clampedOffset(inputs[inputIndex].sampleOffset,
                                               clock.numSamples),
                                 dueBeat, collector);
                ++inputIndex;
                break;

            case Due::Gate:
                processGateReleases(config, dueBeat, offset, collector);
                break;

            case Due::Tick:
                processGridTick(config, nextStepIndex_, dueBeat, offset, collector);
                if (nextStepIndex_ >= kMaxExactStepIndex)
                {
                    collector.mandatoryOverflow = true;
                    return;
                }
                ++nextStepIndex_;
                break;

            case Due::Expiry:
                processPendingExpiry(dueBeat);
                break;

            case Due::None:
                break;
        }
    }

    for (int i = 0; i < timedActiveCount_; ++i)
        voices_[static_cast<std::size_t>(
            timedActiveVoiceIds_[static_cast<std::size_t>(i)])]
                .releaseReservedThisBlock = false;
    collector.reservedReleaseCount = 0;
}

void CrowdTimeField::process (const Config& requestedConfig,
                              const ClockFrame& frame,
                              const InputEvent* inputs, int inputCount,
                              OutputBlock& output) noexcept
{
    output = {};
    const Config config = sanitiseConfig(requestedConfig);
    const ResolvedClock clock = resolveClock(config, frame);
    inputCount = inputs != nullptr ? std::max(0, inputCount) : 0;

    const auto fillStatus = [&] () noexcept
    {
        output.clockLocked = clock.clockLocked;
        output.running = clock.running;
        output.effectiveBpm = clock.bpm;
        output.pendingCount = pendingCount_;
        output.activeCount = activeCount_;
        output.mergedCount = mergedCount_;
        output.droppedMotionCount = droppedMotionCount_;
    };

    // Timed modes cannot make lifecycle progress on an unrepresentable or
    // non-advancing beat domain. Fail closed instead of clamping phase and
    // leaving a pending voice stuck forever. Flow does not consume clock phase.
    if (config.mode != Mode::Flow && ! clock.validDomain)
    {
        clearVoiceState();
        domainInitialised_ = false;
        fairCursor_ = -1;
        output.resetRequested = true;
        output.overflowed = true;
        fillStatus();
        return;
    }

    if (! domainInitialised_)
    {
        adoptDomain(config, clock);
    }
    else if (domainChanged(config, clock))
    {
        clearVoiceState();
        fairCursor_ = -1;
        adoptDomain(config, clock);
        output.resetRequested = true;
        fillStatus();
        return;
    }

    if (pendingAnchorDeferred_)
    {
        const double lifetimeBeats = pendingLifetimeBeats(config);
        for (auto& voice : voices_)
            if (voice.pending)
            {
                voice.pendingSinceBeat = clock.beatStart;
                voice.pendingDeadlineBeat = clock.beatStart + lifetimeBeats;
            }
        nextPendingExpiryBeat_ = pendingCount_ > 0
                               ? clock.beatStart + lifetimeBeats
                               : std::numeric_limits<double>::infinity();
        pendingAnchorDeferred_ = false;
    }

    if (inputCount > kMaxInputEventsPerBlock)
    {
        clearVoiceState();
        fairCursor_ = -1;
        adoptDomain(config, clock);
        output.resetRequested = true;
        output.overflowed = true;
        fillStatus();
        return;
    }

    // FIFO order is part of lifecycle correctness. Rejecting a malformed block
    // is safer than manufacturing an On/Off order that could leave a note stuck.
    int previousOffset = -1;
    for (int i = 0; i < inputCount; ++i)
    {
        if (! validIdentity(inputs[i].voiceId, inputs[i].sourceId)
            || (inputs[i].type != InputEvent::Type::On
                && inputs[i].type != InputEvent::Type::Off))
            continue;

        const int offset = clampedOffset(inputs[i].sampleOffset, clock.numSamples);
        if (offset < previousOffset)
        {
            clearVoiceState();
            fairCursor_ = -1;
            adoptDomain(config, clock);
            output.resetRequested = true;
            output.overflowed = true;
            fillStatus();
            return;
        }
        previousOffset = offset;
    }

    EventCollector collector;
    if (config.mode == Mode::Flow)
        processFlow(clock, inputs, inputCount, collector);
    else
        processTimed(config, clock, inputs, inputCount, collector);

    updateClockHistory(clock);

    if (! collector.writeTo(output))
    {
        // More mandatory lifecycle transitions than the fixed block contract can
        // represent: request the caller's bounded all-notes-off instead of ever
        // dropping a Release and risking a stuck note.
        clearVoiceState();
        fairCursor_ = -1;
        output = {};
        output.resetRequested = true;
        output.overflowed = true;
        fillStatus();
        return;
    }

    saturatingAdd(droppedMotionCount_, collector.droppedMotions);
    fillStatus();
}
