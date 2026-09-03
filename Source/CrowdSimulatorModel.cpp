#include "CrowdSimulatorModel.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    constexpr double twoPi = 6.283185307179586476925286766559;
    constexpr double pi = 3.1415926535897932384626433832795;

    int clampedCapacity (int value) noexcept
    {
        return std::max (1, std::min (CrowdSimulatorModel::maxParticipants, value));
    }

    int roundedMilliseconds (double value, int fallback) noexcept
    {
        if (! std::isfinite (value))
            return fallback;

        const double bounded = std::max (1.0,
            std::min (value, static_cast<double> (std::numeric_limits<int>::max())));
        return static_cast<int> (std::lround (bounded));
    }
}

CrowdSimulatorModel::CrowdSimulatorModel (int capacity,
                                          std::uint64_t seed) noexcept
    : sourceCapacity (clampedCapacity (capacity))
{
    setSeed (seed);
}

void CrowdSimulatorModel::setProfile (Profile newProfile) noexcept
{
    switch (newProfile)
    {
        case Profile::human:
        case Profile::dense:
        case Profile::stress:
            profile = newProfile;
            break;
        default:
            profile = Profile::human;
            break;
    }

    resetMotionBudget();
}

void CrowdSimulatorModel::setSeed (std::uint64_t newSeed) noexcept
{
    globalSeed = newSeed != 0 ? newSeed : defaultSeed;
    managementRng = mix64 (globalSeed ^ UINT64_C (0xd6e8feb86659fd93));
    if (managementRng == 0)
        managementRng = 1;

    for (auto& participant : participants)
        if (participant.occupied)
            initialiseParticipantRng (participant);
}

CrowdSimulatorModel::MutationResult
CrowdSimulatorModel::addHeldParticipant (EventBuffer& output) noexcept
{
    return addParticipant (true, output);
}

CrowdSimulatorModel::MutationResult
CrowdSimulatorModel::addCrowdParticipant (EventBuffer& output) noexcept
{
    return addParticipant (false, output);
}

CrowdSimulatorModel::MutationResult
CrowdSimulatorModel::addParticipant (bool held, EventBuffer& output) noexcept
{
    const int sourceId = findFirstFreeSource();
    if (sourceId < 0)
        return {};

    auto& participant = participants[static_cast<std::size_t> (sourceId)];
    participant = {};
    participant.occupied = true;
    participant.held = held;
    participant.sourceId = sourceId;
    initialiseParticipantRng (participant);
    participant.row = static_cast<int> (
        nextRandom (participant.lifecycleRng) % 26u);

    ++population;
    std::size_t eventCount = 0;

    if (held)
    {
        ++heldPopulation;
        activate (participant, output, eventCount);
    }
    else
    {
        ++crowdPopulation;
        participant.lifecycleRemainingMs = sampleInitialDelayMs (participant);
    }

    return { sourceId, eventCount };
}

CrowdSimulatorModel::MutationResult
CrowdSimulatorModel::removeOneParticipant (EventBuffer& output) noexcept
{
    const int sourceId = chooseOccupiedSource();
    if (sourceId < 0)
        return {};

    auto& participant = participants[static_cast<std::size_t> (sourceId)];
    std::size_t eventCount = 0;
    if (participant.active)
        deactivate (participant, output, eventCount);

    if (participant.held)
        --heldPopulation;
    else
        --crowdPopulation;
    --population;
    participant = {};
    return { sourceId, eventCount };
}

std::size_t CrowdSimulatorModel::advance (bool movementEnabled,
                                          EventBuffer& output) noexcept
{
    std::size_t eventCount = 0;
    std::array<bool, maxParticipants> motionDue {};

    for (int sourceId = 0; sourceId < sourceCapacity; ++sourceId)
    {
        auto& participant = participants[static_cast<std::size_t> (sourceId)];
        if (! participant.occupied)
            continue;

        bool activatedThisTick = false;
        if (! participant.held)
        {
            participant.lifecycleRemainingMs -= tickIntervalMs;
            if (participant.lifecycleRemainingMs <= 0)
            {
                if (participant.active)
                    deactivate (participant, output, eventCount);
                else
                {
                    activate (participant, output, eventCount);
                    activatedThisTick = true;
                }
            }
        }

        if (! participant.active || ! movementEnabled || activatedThisTick)
            continue;

        participant.physicsRemainingMs -= tickIntervalMs;
        if (participant.physicsRemainingMs <= 0)
        {
            // Physics stays on the measured 50 ms grid even when the phone-like
            // transport cadence skips a sample. One step at most per callback:
            // a delayed GUI timer is never converted into a catch-up burst.
            participant.physicsRemainingMs += coordinateIntervalMs;
            advanceMotion (participant);
        }

        participant.motionRemainingMs -= tickIntervalMs;
        if (participant.motionRemainingMs <= 0)
        {
            participant.motionRemainingMs += sampleMotionIntervalMs (participant);
            motionDue[static_cast<std::size_t> (sourceId)] = true;
        }
    }

    const int frameLimit = motionFrameLimitPerSecond (profile);
    const std::int64_t creditIncrement =
        static_cast<std::int64_t> (frameLimit) * tickIntervalMs;
    const std::int64_t maximumCredit =
        static_cast<std::int64_t> (frameLimit) * coordinateIntervalMs;
    motionCreditMilliFrames = std::min (maximumCredit,
        motionCreditMilliFrames + creditIncrement);

    int availableFrames = static_cast<int> (motionCreditMilliFrames / 1000);
    int emittedFrames = 0;
    int lastEmittedSource = -1;

    for (int scan = 0; scan < sourceCapacity && emittedFrames < availableFrames; ++scan)
    {
        const int sourceId = (motionRoundRobinSource + scan) % sourceCapacity;
        if (! motionDue[static_cast<std::size_t> (sourceId)])
            continue;

        const auto& participant = participants[static_cast<std::size_t> (sourceId)];
        appendEvent (output, eventCount, EventType::x, participant,
                     static_cast<float> (participant.emittedX));
        appendEvent (output, eventCount, EventType::y, participant,
                     static_cast<float> (participant.emittedY));
        ++emittedFrames;
        lastEmittedSource = sourceId;
    }

    motionCreditMilliFrames -= static_cast<std::int64_t> (emittedFrames) * 1000;
    if (lastEmittedSource >= 0)
        motionRoundRobinSource = (lastEmittedSource + 1) % sourceCapacity;

    return eventCount;
}

std::size_t CrowdSimulatorModel::clear (EventBuffer& output) noexcept
{
    std::size_t eventCount = 0;
    for (auto& participant : participants)
        if (participant.occupied && participant.active)
            appendEvent (output, eventCount, EventType::off, participant);

    clearSilently();
    return eventCount;
}

void CrowdSimulatorModel::clearSilently() noexcept
{
    participants = {};
    population = 0;
    heldPopulation = 0;
    crowdPopulation = 0;
    activePopulation = 0;
    activeCrowdPopulation = 0;
    motionRoundRobinSource = 0;
    managementRng = mix64 (globalSeed ^ UINT64_C (0xd6e8feb86659fd93));
    if (managementRng == 0)
        managementRng = 1;
    resetMotionBudget();
}

std::size_t CrowdSimulatorModel::setSourceCapacity (
    int newCapacity, EventBuffer& output) noexcept
{
    newCapacity = clampedCapacity (newCapacity);
    if (newCapacity == sourceCapacity)
        return 0;

    std::size_t eventCount = 0;
    if (newCapacity < sourceCapacity)
    {
        // Ascending source order makes retirement deterministic and keeps the
        // maximum output bounded to one Off per physical participant.
        for (int sourceId = newCapacity; sourceId < sourceCapacity; ++sourceId)
        {
            auto& participant = participants[static_cast<std::size_t> (sourceId)];
            if (! participant.occupied)
                continue;

            if (participant.active)
            {
                appendEvent (output, eventCount, EventType::off, participant);
                --activePopulation;
                if (! participant.held)
                    --activeCrowdPopulation;
            }

            if (participant.held)
                --heldPopulation;
            else
                --crowdPopulation;
            --population;
            participant = {};
        }
    }

    sourceCapacity = newCapacity;
    motionRoundRobinSource %= sourceCapacity;
    return eventCount;
}

CrowdSimulatorModel::ParticipantSnapshot
CrowdSimulatorModel::getParticipantSnapshot (int sourceId) const noexcept
{
    if (sourceId < 0 || sourceId >= sourceCapacity)
        return {};

    const auto& participant = participants[static_cast<std::size_t> (sourceId)];
    return { participant.occupied,
             participant.active,
             participant.held,
             participant.row,
             static_cast<float> (participant.emittedX),
             static_cast<float> (participant.emittedY),
             participant.lifecycleRemainingMs };
}

const char* CrowdSimulatorModel::profileName (Profile value) noexcept
{
    switch (value)
    {
        case Profile::human:  return "Human";
        case Profile::dense:  return "Dense";
        case Profile::stress: return "Stress";
        default:              return "Human";
    }
}

int CrowdSimulatorModel::motionFrameLimitPerSecond (Profile value) noexcept
{
    switch (value)
    {
        case Profile::human:  return 500;  // 1,000 scalar U/V events/s
        case Profile::dense:  return 1000; // 2,000 scalar U/V events/s
        case Profile::stress: return 2000; // bounded intentional load
        default:              return 500;
    }
}

void CrowdSimulatorModel::activate (Participant& participant,
                                    EventBuffer& output,
                                    std::size_t& eventCount) noexcept
{
    if (participant.active)
        return;

    const auto initialCoordinate = [&participant] (double home,
                                                   double radius) noexcept
    {
        // A person repeatedly works around a local hand position, with a rare
        // whole-glass start. Homes are source-stable; starts remain per-gesture.
        if (uniform01 (participant.motionRng) < 0.035)
            return uniform01 (participant.motionRng);
        const double value = home + normal01 (participant.motionRng) * radius * 0.42;
        return std::max (0.02, std::min (0.98, value));
    };
    participant.x = initialCoordinate (participant.motionTraits.homeX,
                                       participant.motionTraits.radiusX);
    participant.y = initialCoordinate (participant.motionTraits.homeY,
                                       participant.motionTraits.radiusY);
    participant.emittedX = quantizeCoordinate (participant.x);
    participant.emittedY = quantizeCoordinate (participant.y);
    participant.speed = 0.0;
    participant.heading = uniform01 (participant.motionRng) * twoPi - pi;
    participant.targetHeading = participant.heading;
    participant.targetSpeed = 0.0;
    participant.paused = false;
    participant.staticTap = false;
    participant.axisLocked = false;
    participant.pauseFramesRemaining = 0;
    participant.physicsRemainingMs = coordinateIntervalMs;
    participant.motionRemainingMs = participant.held
        ? coordinateIntervalMs : sampleMotionIntervalMs (participant);
    participant.gestureClass = chooseGestureClass (participant);
    if (profile == Profile::human && ! participant.held
        && participant.gestureClass == GestureClass::tap)
        participant.staticTap = uniform01 (participant.motionRng) < 0.70;
    chooseMotionStyle (participant, true);
    chooseMotionSegment (participant, true);
    participant.lifecycleRemainingMs = participant.held
        ? std::numeric_limits<int>::max()
        : sampleGestureDurationMs (participant, participant.gestureClass);
    participant.active = true;
    ++activePopulation;
    if (! participant.held)
        ++activeCrowdPopulation;

    // Production protocol order is U, V, On for one finger.
    appendEvent (output, eventCount, EventType::x, participant,
                 static_cast<float> (participant.emittedX));
    appendEvent (output, eventCount, EventType::y, participant,
                 static_cast<float> (participant.emittedY));
    appendEvent (output, eventCount, EventType::on, participant, 1.0f);
}

void CrowdSimulatorModel::deactivate (Participant& participant,
                                      EventBuffer& output,
                                      std::size_t& eventCount) noexcept
{
    if (! participant.active)
        return;

    appendEvent (output, eventCount, EventType::off, participant);
    participant.active = false;
    participant.speed = 0.0;
    participant.targetSpeed = 0.0;
    --activePopulation;
    if (! participant.held)
    {
        --activeCrowdPopulation;
        participant.lifecycleRemainingMs = sampleIdleDurationMs (participant);
    }
}

void CrowdSimulatorModel::advanceMotion (Participant& participant) noexcept
{
    if (profile == Profile::human && participant.staticTap)
    {
        participant.paused = true;
        participant.speed = 0.0;
        participant.targetSpeed = 0.0;
        // Seventy percent of phone taps in the ten-person reference are a
        // press/release at one coordinate, not a miniature random walk.
        participant.emittedX = quantizeCoordinate (participant.x);
        participant.emittedY = quantizeCoordinate (participant.y);
        return;
    }

    if (profile == Profile::human)
    {
        participant.styleBoutRemainingMs -= coordinateIntervalMs;
        if (participant.styleBoutRemainingMs <= 0)
            chooseMotionStyle (participant, false);

        if (participant.pauseFramesRemaining > 0)
        {
            participant.paused = true;
            --participant.pauseFramesRemaining;
            participant.speed = 0.0;
            participant.targetSpeed = 0.0;
            return;
        }

        const bool resumingFromPause = participant.paused;
        const double pauseChance = std::min (0.24,
            0.052 * participant.motionTraits.pauseScale);
        if (uniform01 (participant.motionRng) < pauseChance)
        {
            participant.paused = true;
            participant.pauseFramesRemaining = samplePauseRunFrames (participant) - 1;
            participant.speed = 0.0;
            participant.targetSpeed = 0.0;
            return;
        }
        participant.paused = false;
        if (resumingFromPause)
            participant.motionSegmentRemainingMs = 0;
    }

    participant.motionSegmentRemainingMs -= coordinateIntervalMs;
    if (participant.motionSegmentRemainingMs <= 0)
        chooseMotionSegment (participant, false);

    double angleDelta = wrapAngle (participant.targetHeading - participant.heading);
    const double maximumTurn = profile == Profile::human ? 0.50 : 0.42;
    angleDelta = std::max (-maximumTurn, std::min (maximumTurn, angleDelta));
    participant.heading = wrapAngle (participant.heading
        + angleDelta * (profile == Profile::human ? 0.45 : 0.72));

    const double speedSmoothing = participant.paused ? 0.72
        : (profile == Profile::human ? 0.68 : 0.48);
    participant.speed += (participant.targetSpeed - participant.speed) * speedSmoothing;
    if (participant.paused && participant.speed < 0.025)
        participant.speed = 0.0;

    // Human paths are persistent but contain occasional decisive turns. This
    // measured irregularity avoids the simulator's old smooth random-walk look.
    const double decisiveTurnChance = profile == Profile::human ? 0.12 : 0.085;
    if (! participant.paused
        && uniform01 (participant.motionRng) < decisiveTurnChance)
    {
        const double sign = uniform01 (participant.motionRng) < 0.5 ? -1.0 : 1.0;
        participant.heading = wrapAngle (participant.heading + sign
            * ((profile == Profile::human ? 0.58 * pi : 1.60)
               + uniform01 (participant.motionRng)
                 * (profile == Profile::human ? 0.45 * pi : 1.05)));
        participant.targetHeading = participant.heading;
    }

    double frameSpeed = participant.speed;
    if (! participant.paused)
    {
        participant.heading = wrapAngle (participant.heading
            + normal01 (participant.motionRng) * 0.025);
        // Micro acceleration is applied to this 50 ms frame only. Keeping it
        // outside the smoothed base speed lowers artificial lag-1 repetition
        // while retaining a coherent longer movement arc.
        frameSpeed *= std::max (0.38,
            std::min (1.85, 1.0 + normal01 (participant.motionRng)
                * (profile == Profile::human ? 0.34 : 0.20)));
    }

    const double speedLimit = profile == Profile::human ? 6.0 : 4.8;
    participant.speed = std::max (0.0, std::min (speedLimit, participant.speed));
    frameSpeed = std::max (0.0, std::min (speedLimit, frameSpeed));
    double xAspect = profile == Profile::human ? 1.15 : 1.0;
    double yAspect = profile == Profile::human ? 0.85 : 1.0;
    if (profile == Profile::human
        && participant.motionTraits.axisPreference == AxisPreference::vertical)
    {
        // Vertical-persona paths must remain visibly vertical even though the
        // population as a whole retains the phone capture's wider U spread.
        xAspect = 0.70;
        yAspect = 1.18;
    }
    double vx = std::cos (participant.heading) * frameSpeed * xAspect;
    double vy = std::sin (participant.heading) * frameSpeed * yAspect;

    if (profile == Profile::human)
    {
        // A weak source-stable home pull prevents every user from painting the
        // same full-screen random walk while still allowing rare edge visits.
        vx += 0.35 * ((participant.motionTraits.homeX - participant.x)
            / std::max (0.04, participant.motionTraits.radiusX))
            * participant.motionTraits.homePull;
        vy += 0.35 * ((participant.motionTraits.homeY - participant.y)
            / std::max (0.04, participant.motionTraits.radiusY))
            * participant.motionTraits.homePull;
    }

    // Soft steering before the hard reflection keeps paths away from the exact
    // 0/1 rails while still allowing the reference's full coordinate range.
    const double softBoundary = profile == Profile::human ? 0.055 : 0.06;
    if (participant.x < softBoundary && vx < 0.0)
    {
        vx = -vx;
        participant.heading = wrapAngle (pi - participant.heading);
        participant.targetHeading = wrapAngle (pi - participant.targetHeading);
    }
    else if (participant.x > 1.0 - softBoundary && vx > 0.0)
    {
        vx = -vx;
        participant.heading = wrapAngle (pi - participant.heading);
        participant.targetHeading = wrapAngle (pi - participant.targetHeading);
    }
    if (participant.y < softBoundary && vy < 0.0)
    {
        vy = -vy;
        participant.heading = wrapAngle (-participant.heading);
        participant.targetHeading = wrapAngle (-participant.targetHeading);
    }
    else if (participant.y > 1.0 - softBoundary && vy > 0.0)
    {
        vy = -vy;
        participant.heading = wrapAngle (-participant.heading);
        participant.targetHeading = wrapAngle (-participant.targetHeading);
    }

    participant.x += vx * 0.05;
    participant.y += vy * 0.05;

    if (participant.x < 0.0)
    {
        participant.x = -participant.x;
        participant.heading = wrapAngle (pi - participant.heading);
    }
    else if (participant.x > 1.0)
    {
        participant.x = 2.0 - participant.x;
        participant.heading = wrapAngle (pi - participant.heading);
    }
    if (participant.y < 0.0)
    {
        participant.y = -participant.y;
        participant.heading = wrapAngle (-participant.heading);
    }
    else if (participant.y > 1.0)
    {
        participant.y = 2.0 - participant.y;
        participant.heading = wrapAngle (-participant.heading);
    }

    if (! finiteUnit (participant.x) || ! finiteUnit (participant.y)
        || ! std::isfinite (participant.speed)
        || ! std::isfinite (participant.heading))
    {
        participant.x = 0.5;
        participant.y = 0.5;
        participant.speed = 0.0;
        participant.heading = 0.0;
        participant.targetHeading = 0.0;
        participant.targetSpeed = 0.0;
    }

    const double coordinateMinimum = profile == Profile::human ? 0.06 : 0.0;
    const double coordinateMaximum = profile == Profile::human ? 0.94 : 1.0;
    participant.x = std::max (coordinateMinimum,
                              std::min (coordinateMaximum, participant.x));
    participant.y = std::max (coordinateMinimum,
                              std::min (coordinateMaximum, participant.y));
    participant.emittedX = quantizeCoordinate (participant.x);
    participant.emittedY = quantizeCoordinate (participant.y);
}

void CrowdSimulatorModel::chooseMotionSegment (Participant& participant,
                                               bool initial) noexcept
{
    if (profile == Profile::human)
    {
        participant.paused = false;

        double styleScale = participant.motionTraits.speedScale;
        switch (participant.motionStyle)
        {
            case MotionStyle::still:      styleScale *= 0.88; break;
            case MotionStyle::gentle:     styleScale *= 0.96; break;
            case MotionStyle::ordinary:  styleScale *= 1.00; break;
            case MotionStyle::expressive: styleScale *= 1.06; break;
        }

        const double tapScale = participant.gestureClass == GestureClass::tap
                              ? 1.35 : 1.0;
        participant.targetSpeed = boundedLogNormal (participant.motionRng,
            1.33 * styleScale * participant.gestureSpeedScale * tapScale,
            0.72, 0.015, 6.0);

        const double lockChance = participant.motionTraits.axisPreference
                                   == AxisPreference::vertical
            ? std::max (0.90, participant.motionTraits.axisLockChance)
            : participant.motionTraits.axisLockChance;
        participant.axisLocked = participant.motionTraits.axisPreference
                                  != AxisPreference::free
            && uniform01 (participant.motionRng) < lockChance;

        if (participant.axisLocked)
        {
            const bool positive = uniform01 (participant.motionRng) < 0.5;
            const double base = participant.motionTraits.axisPreference
                                  == AxisPreference::horizontal
                ? (positive ? 0.0 : pi)
                : (positive ? 0.5 * pi : -0.5 * pi);
            participant.targetHeading = wrapAngle (base
                + normal01 (participant.motionRng) * 0.09);
        }
        else
        {
            participant.targetHeading = wrapAngle (participant.heading
                + normal01 (participant.motionRng) * 0.26);
        }

        const double dx = participant.motionTraits.homeX - participant.x;
        const double dy = participant.motionTraits.homeY - participant.y;
        const double normalisedDistance = std::hypot (
            dx / std::max (0.04, participant.motionTraits.radiusX),
            dy / std::max (0.04, participant.motionTraits.radiusY));
        if (normalisedDistance > 0.55 && std::hypot (dx, dy) > 0.01)
        {
            const double homeHeading = std::atan2 (dy, dx);
            const double blend = std::min (0.65,
                (normalisedDistance - 0.55) * 0.25);
            participant.targetHeading = wrapAngle (participant.targetHeading
                + wrapAngle (homeHeading - participant.targetHeading) * blend);
        }

        participant.motionSegmentRemainingMs = roundedMilliseconds (
            boundedLogNormal (participant.motionRng,
                              initial ? 210.0 : 285.0,
                              0.55, 100.0, 950.0),
            initial ? 210 : 285);
        return;
    }

    // A permanently-held participant has a deliberately narrower 5-13%
    // long-run pause envelope. The Real10 population aggregate is 23.72%
    // because it also includes lifecycle-driven static taps.
    const double pauseChance = initial
        ? (participant.gestureClass == GestureClass::tap ? 0.10 : 0.04)
        : 0.16;
    participant.paused = uniform01 (participant.motionRng) < pauseChance;
    if (participant.paused)
    {
        participant.targetSpeed = 0.0;
        participant.motionSegmentRemainingMs = roundedMilliseconds (
            boundedLogNormal (participant.motionRng, 190.0, 0.40, 100.0, 400.0),
            190);
        return;
    }

    participant.targetSpeed = boundedLogNormal (participant.motionRng,
                                                  1.15, 0.62, 0.10, 4.8);
    participant.targetHeading = wrapAngle (participant.heading
        + normal01 (participant.motionRng) * 0.45);
    participant.motionSegmentRemainingMs = roundedMilliseconds (
        boundedLogNormal (participant.motionRng, 300.0, 0.45, 125.0, 800.0),
        300);
}

void CrowdSimulatorModel::chooseMotionStyle (Participant& participant,
                                             bool initial) noexcept
{
    if (initial)
    {
        participant.motionStyle = participant.motionTraits.baseStyle;
        participant.gestureSpeedScale = boundedLogNormal (
            participant.motionRng, 1.0, 0.24, 0.62, 1.60);
    }
    else
    {
        const double choice = uniform01 (participant.motionRng);
        int style = static_cast<int> (participant.motionTraits.baseStyle);
        if (choice >= 0.72)
            style += choice < 0.86 ? -1 : 1;
        style = std::max (static_cast<int> (MotionStyle::still),
                          std::min (static_cast<int> (MotionStyle::expressive),
                                    style));
        participant.motionStyle = static_cast<MotionStyle> (style);
    }

    participant.styleBoutRemainingMs = roundedMilliseconds (
        boundedLogNormal (participant.motionRng, 2400.0, 0.55, 700.0, 7000.0),
        2400);
}

int CrowdSimulatorModel::sampleMotionIntervalMs (Participant& participant) noexcept
{
    if (profile != Profile::human || participant.held)
        return coordinateIntervalMs;

    // The plugin's timer is a deterministic 25 ms grid, so the observed phone
    // jitter is represented as skipped 50 ms frames rather than sub-tick noise.
    const double value = uniform01 (participant.cadenceRng);
    if (value < 0.732) return 50;
    if (value < 0.978) return 100;
    if (value < 0.999) return 150;
    return 200;
}

int CrowdSimulatorModel::samplePauseRunFrames (Participant& participant) noexcept
{
    const double value = uniform01 (participant.motionRng);
    if (value < 0.78)
        return 1;
    if (value < 0.92)
        return 2 + static_cast<int> (nextRandom (participant.motionRng) % 2u);
    if (value < 0.99)
        return 4 + static_cast<int> (nextRandom (participant.motionRng) % 8u);
    return 12 + static_cast<int> (nextRandom (participant.motionRng) % 29u);
}

CrowdSimulatorModel::GestureClass
CrowdSimulatorModel::chooseGestureClass (Participant& participant) noexcept
{
    const double value = uniform01 (participant.lifecycleRng);
    switch (profile)
    {
        case Profile::human:
        {
            // Direct per-gesture weights keep the observable mixture stable;
            // source personas independently shape idle time and duration.
            if (value < 0.608) return GestureClass::tap;
            if (value < 0.923) return GestureClass::shortDrag;
            if (value < 0.993) return GestureClass::longDrag;
            return GestureClass::extendedDrag;
        }
        case Profile::dense:
            if (value < 0.48) return GestureClass::tap;
            if (value < 0.72) return GestureClass::shortDrag;
            if (value < 0.94) return GestureClass::longDrag;
            return GestureClass::extendedDrag;
        case Profile::stress:
            if (value < 0.25) return GestureClass::tap;
            if (value < 0.50) return GestureClass::shortDrag;
            if (value < 0.85) return GestureClass::longDrag;
            return GestureClass::extendedDrag;
        default:
            return GestureClass::tap;
    }
}

int CrowdSimulatorModel::sampleGestureDurationMs (Participant& participant,
                                                   GestureClass gesture) noexcept
{
    double value = 120.0;
    if (profile == Profile::human)
    {
        switch (gesture)
        {
            case GestureClass::tap:
                value = boundedLogNormal (participant.lifecycleRng,
                                          100.0, 0.50, 30.0, 250.0);
                break;
            case GestureClass::shortDrag:
                value = boundedLogNormal (participant.lifecycleRng,
                                          565.0, 0.64, 250.0, 2000.0);
                break;
            case GestureClass::longDrag:
                value = boundedLogNormal (participant.lifecycleRng,
                                          3100.0, 0.55, 2000.0, 9000.0);
                break;
            case GestureClass::extendedDrag:
                value = boundedLogNormal (participant.lifecycleRng,
                                          15000.0, 0.30, 9000.0, 20000.0);
                break;
        }
        value *= participant.lifecycleTraits.durationScale;
        switch (gesture)
        {
            case GestureClass::tap:          value = std::min (250.0, value); break;
            case GestureClass::shortDrag:    value = std::max (250.0, std::min (2000.0, value)); break;
            case GestureClass::longDrag:     value = std::max (2000.0, std::min (9000.0, value)); break;
            case GestureClass::extendedDrag: value = std::max (9000.0, std::min (20000.0, value)); break;
        }
    }
    else
    {
        switch (gesture)
        {
            case GestureClass::tap:
                value = boundedLogNormal (participant.lifecycleRng,
                                          105.0, 0.22, 70.0, 300.0);
                break;
            case GestureClass::shortDrag:
                value = boundedLogNormal (participant.lifecycleRng,
                                          900.0, 0.60, 250.0, 2500.0);
                break;
            case GestureClass::longDrag:
                value = boundedLogNormal (participant.lifecycleRng,
                                          5200.0, 0.42, 2000.0, 9000.0);
                break;
            case GestureClass::extendedDrag:
                value = boundedLogNormal (participant.lifecycleRng,
                                          15000.0, 0.36, 9000.0, 25000.0);
                break;
        }
        value *= profile == Profile::dense ? 1.10 : 1.20;
    }

    return roundedMilliseconds (value, 120);
}

int CrowdSimulatorModel::sampleIdleDurationMs (Participant& participant) noexcept
{
    const bool afterTap = participant.gestureClass == GestureClass::tap;
    double value = 600.0;

    switch (profile)
    {
        case Profile::human:
            if (uniform01 (participant.lifecycleRng) < 0.015)
            {
                value = boundedLogNormal (participant.lifecycleRng,
                                          3500.0, 0.65, 1500.0, 10000.0);
            }
            else if (afterTap && uniform01 (participant.lifecycleRng) < 0.96)
            {
                // A log-uniform quick gap matches rapid repeated phone taps
                // without hard-coding one metronomic interval.
                const double low = std::log (35.0);
                const double high = std::log (260.0);
                value = std::exp (low + uniform01 (participant.lifecycleRng)
                                      * (high - low));
            }
            else
            {
                value = boundedLogNormal (participant.lifecycleRng,
                                          550.0, 0.72, 225.0, 2000.0);
            }
            value *= participant.lifecycleTraits.idleScale;
            break;
        case Profile::dense:
            value = afterTap
                ? boundedLogNormal (participant.lifecycleRng,
                                    70.0, 0.50, 25.0, 250.0)
                : boundedLogNormal (participant.lifecycleRng,
                                    300.0, 0.48, 100.0, 900.0);
            break;
        case Profile::stress:
            value = afterTap
                ? boundedLogNormal (participant.lifecycleRng,
                                    45.0, 0.40, 25.0, 100.0)
                : boundedLogNormal (participant.lifecycleRng,
                                    100.0, 0.50, 25.0, 300.0);
            break;
    }

    return roundedMilliseconds (value, afterTap ? 110 : 600);
}

int CrowdSimulatorModel::sampleInitialDelayMs (Participant& participant) noexcept
{
    int maximumMs = 2000;
    if (profile == Profile::dense)
        maximumMs = 1000;
    else if (profile == Profile::stress)
        maximumMs = 250;

    const int slots = std::max (1, maximumMs / tickIntervalMs);
    return tickIntervalMs * (1 + static_cast<int> (
        nextRandom (participant.lifecycleRng) % static_cast<std::uint64_t> (slots)));
}

int CrowdSimulatorModel::findFirstFreeSource() const noexcept
{
    for (int sourceId = 0; sourceId < sourceCapacity; ++sourceId)
        if (! participants[static_cast<std::size_t> (sourceId)].occupied)
            return sourceId;
    return -1;
}

int CrowdSimulatorModel::chooseOccupiedSource() noexcept
{
    if (population <= 0)
        return -1;

    const int ordinal = static_cast<int> (
        nextRandom (managementRng) % static_cast<std::uint64_t> (population));
    int seen = 0;
    for (int sourceId = 0; sourceId < sourceCapacity; ++sourceId)
    {
        if (! participants[static_cast<std::size_t> (sourceId)].occupied)
            continue;
        if (seen == ordinal)
            return sourceId;
        ++seen;
    }
    return -1;
}

void CrowdSimulatorModel::initialiseParticipantRng (Participant& participant) noexcept
{
    const auto source = static_cast<std::uint64_t> (participant.sourceId + 1);
    participant.lifecycleRng = mix64 (globalSeed
        ^ (source * UINT64_C (0x9e3779b97f4a7c15))
        ^ UINT64_C (0xa0761d6478bd642f));
    participant.motionRng = mix64 (globalSeed
        ^ (source * UINT64_C (0xbf58476d1ce4e5b9))
        ^ UINT64_C (0xe7037ed1a0b428db));
    participant.cadenceRng = mix64 (globalSeed
        ^ (source * UINT64_C (0x94d049bb133111eb))
        ^ UINT64_C (0x8ebc6af09c88c6e3));
    if (participant.lifecycleRng == 0) participant.lifecycleRng = 1;
    if (participant.motionRng == 0) participant.motionRng = 1;
    if (participant.cadenceRng == 0) participant.cadenceRng = 1;

    // Trait streams are deliberately local: sampling a persona never consumes
    // future lifecycle, motion or cadence choices.
    auto lifecycleTraitRng = mix64 (globalSeed
        ^ (source * UINT64_C (0xd1342543de82ef95))
        ^ UINT64_C (0x9e6c63d0676a9a99));
    const double lifecyclePersona = uniform01 (lifecycleTraitRng);
    if (lifecyclePersona < 0.30)
    {
        participant.lifecycleTraits.persona = LifecyclePersona::tapper;
        participant.lifecycleTraits.idleScale = 0.55;
    }
    else if (lifecyclePersona < 0.50)
    {
        participant.lifecycleTraits.persona = LifecyclePersona::explorer;
        participant.lifecycleTraits.idleScale = 0.85;
    }
    else if (lifecyclePersona < 0.85)
    {
        participant.lifecycleTraits.persona = LifecyclePersona::ordinary;
        participant.lifecycleTraits.idleScale = 1.0;
    }
    else
    {
        participant.lifecycleTraits.persona = LifecyclePersona::intermittent;
        participant.lifecycleTraits.idleScale = 1.8;
    }
    participant.lifecycleTraits.durationScale = boundedLogNormal (
        lifecycleTraitRng, 1.0, 0.08, 0.86, 1.16);

    auto motionTraitRng = mix64 (globalSeed
        ^ (source * UINT64_C (0x369dea0f31a53f85))
        ^ UINT64_C (0xdb4f0b9175ae2165));
    const int styleOffset = static_cast<int> (mix64 (globalSeed
        ^ UINT64_C (0x13198a2e03707344)) % 10u);
    const int styleBucket = (participant.sourceId * 3 + styleOffset) % 10;
    double minimumSpeed = 0.86, maximumSpeed = 1.28;
    double minimumPause = 0.75, maximumPause = 1.35;
    if (styleBucket < 2)
    {
        participant.motionTraits.baseStyle = MotionStyle::still;
        minimumSpeed = 0.22; maximumSpeed = 0.48;
        minimumPause = 2.8; maximumPause = 4.6;
    }
    else if (styleBucket < 6)
    {
        participant.motionTraits.baseStyle = MotionStyle::gentle;
        minimumSpeed = 0.48; maximumSpeed = 0.82;
        minimumPause = 1.5; maximumPause = 2.5;
    }
    else if (styleBucket < 8)
    {
        participant.motionTraits.baseStyle = MotionStyle::ordinary;
    }
    else
    {
        participant.motionTraits.baseStyle = MotionStyle::expressive;
        minimumSpeed = 1.45; maximumSpeed = 2.15;
        minimumPause = 0.38; maximumPause = 0.78;
    }
    participant.motionTraits.speedScale = minimumSpeed
        + uniform01 (motionTraitRng) * (maximumSpeed - minimumSpeed);
    participant.motionTraits.pauseScale = minimumPause
        + uniform01 (motionTraitRng) * (maximumPause - minimumPause);

    // A low-discrepancy source permutation gives every consecutive group of
    // ten an exact 50/30/20 horizontal/vertical/free axis mix while the global
    // seed rotates which IDs receive each preference.
    const int axisOffset = static_cast<int> (mix64 (globalSeed
        ^ UINT64_C (0x243f6a8885a308d3)) % 10u);
    const int axisBucket = (participant.sourceId * 7 + axisOffset) % 10;
    participant.motionTraits.axisPreference = axisBucket < 5
        ? AxisPreference::horizontal
        : (axisBucket < 8 ? AxisPreference::vertical : AxisPreference::free);
    participant.motionTraits.axisLockChance = 0.70
        + uniform01 (motionTraitRng) * 0.15;
    participant.motionTraits.homeX = 0.14 + uniform01 (motionTraitRng) * 0.72;
    participant.motionTraits.homeY = 0.20 + uniform01 (motionTraitRng) * 0.48;

    double minimumRadius = 0.10, maximumRadius = 0.22;
    switch (participant.motionTraits.baseStyle)
    {
        case MotionStyle::still:
            minimumRadius = 0.08; maximumRadius = 0.16; break;
        case MotionStyle::gentle:
            minimumRadius = 0.15; maximumRadius = 0.28; break;
        case MotionStyle::ordinary:
            minimumRadius = 0.25; maximumRadius = 0.42; break;
        case MotionStyle::expressive:
            minimumRadius = 0.34; maximumRadius = 0.55; break;
    }
    participant.motionTraits.radiusX = minimumRadius
        + uniform01 (motionTraitRng) * (maximumRadius - minimumRadius);
    participant.motionTraits.radiusX *= 0.58;
    participant.motionTraits.radiusY = participant.motionTraits.radiusX
        * (0.58 + uniform01 (motionTraitRng) * 0.18);
    participant.motionTraits.homePull = 0.22
        + uniform01 (motionTraitRng) * 0.20;
}

void CrowdSimulatorModel::resetMotionBudget() noexcept
{
    motionCreditMilliFrames = 0;
    motionRoundRobinSource = 0;
}

std::uint64_t CrowdSimulatorModel::mix64 (std::uint64_t value) noexcept
{
    value += UINT64_C (0x9e3779b97f4a7c15);
    value = (value ^ (value >> 30)) * UINT64_C (0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C (0x94d049bb133111eb);
    return value ^ (value >> 31);
}

std::uint64_t CrowdSimulatorModel::nextRandom (std::uint64_t& state) noexcept
{
    // xorshift64*: small per-source state, deterministic on every supported ABI.
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    return state * UINT64_C (2685821657736338717);
}

double CrowdSimulatorModel::uniform01 (std::uint64_t& state) noexcept
{
    return static_cast<double> (nextRandom (state) >> 11)
         * (1.0 / 9007199254740992.0);
}

double CrowdSimulatorModel::normal01 (std::uint64_t& state) noexcept
{
    const double unitA = std::max (1.0e-12, uniform01 (state));
    const double unitB = uniform01 (state);
    const double result = std::sqrt (-2.0 * std::log (unitA))
                        * std::cos (twoPi * unitB);
    return std::isfinite (result) ? result : 0.0;
}

double CrowdSimulatorModel::boundedLogNormal (std::uint64_t& state,
                                               double median,
                                               double sigma,
                                               double minimum,
                                               double maximum) noexcept
{
    const double candidate = median * std::exp (normal01 (state) * sigma);
    if (! std::isfinite (candidate))
        return median;
    return std::max (minimum, std::min (maximum, candidate));
}

double CrowdSimulatorModel::wrapAngle (double radians) noexcept
{
    if (! std::isfinite (radians))
        return 0.0;
    while (radians > pi) radians -= twoPi;
    while (radians < -pi) radians += twoPi;
    return radians;
}

double CrowdSimulatorModel::quantizeCoordinate (double value) noexcept
{
    if (! finiteUnit (value))
        value = 0.5;
    value = std::max (0.0, std::min (1.0, value));
    return std::round (value * 100.0) * 0.01;
}

bool CrowdSimulatorModel::finiteUnit (double value) noexcept
{
    return std::isfinite (value) && value >= 0.0 && value <= 1.0;
}

void CrowdSimulatorModel::appendEvent (EventBuffer& output,
                                       std::size_t& eventCount,
                                       EventType type,
                                       const Participant& participant,
                                       float value) noexcept
{
    if (eventCount >= output.size())
        return;
    output[eventCount++] = { type, participant.row, participant.sourceId, value };
}
