#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FactoryPresets.h"
#include "PluginStateMigration.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>

static_assert (OscFingerRouter::MAX_SOURCES == SeatEventSink::MAX_OSC_SOURCES,
               "OSC parser and MIDI audience capacities must match");
static_assert (OscFingerRouter::MAX_VOICES == MpeMidiOutput::kMaxMidiSources,
               "OSC finger and MIDI output voice capacities must match");
static_assert (std::atomic<std::uint64_t>::is_always_lock_free,
               "Time Field policy publication must stay lock-free on the audio thread");

namespace
{
    constexpr std::uint32_t externalWatchdogAudioStallMs = 250u;
    constexpr std::uint32_t externalWatchdogTimerJitterMs = 50u;

    constexpr std::uint64_t policyFieldMask = 0x0fffu;
    constexpr int policyActiveShift = 12;
    constexpr int policySpreadShift = 24;
    constexpr int policyAdmissionShift = 36;
    constexpr int policyModeShift = 37;
    constexpr std::uint64_t policyValidBit = std::uint64_t { 1 } << 63;

    std::uint64_t packEffectiveTimeFieldPolicy (
        const CrowdTimeField::Config& config) noexcept
    {
        const bool flow = config.mode == CrowdTimeField::Mode::Flow;
        const auto bounded = [] (int value) noexcept
        {
            return static_cast<std::uint64_t>(juce::jlimit(
                0, static_cast<int>(policyFieldMask), value));
        };
        const auto attacks = bounded(flow ? config.flowMaxAttacksPerBlock
                                          : config.maxAttacksPerStep);
        const auto active = bounded(flow ? config.flowMaxActive
                                         : config.maxActive);
        const auto spread = bounded(config.spreadSlots);
        const auto mode = static_cast<std::uint64_t>(juce::jlimit(
            0, 2, static_cast<int>(config.mode)));

        return policyValidBit
             | attacks
             | (active << policyActiveShift)
             | (spread << policySpreadShift)
             | (static_cast<std::uint64_t>(config.attackAdmissionOpen)
                << policyAdmissionShift)
             | (mode << policyModeShift);
    }

    std::uint32_t boundedBlockDurationMs (int numSamples,
                                          double sampleRate) noexcept
    {
        if (numSamples <= 0 || ! std::isfinite(sampleRate)
            || sampleRate <= 0.0)
            return 0u;

        return static_cast<std::uint32_t>(std::ceil(juce::jlimit(
            0.0, 30000.0, 1000.0 * static_cast<double>(numSamples)
                              / sampleRate)));
    }

    class ScopedProcessorSuspension final
    {
    public:
        ScopedProcessorSuspension (juce::AudioProcessor& owner,
                                   std::atomic<bool>& mutationGate,
                                   std::atomic<int>& callbacksInFlight)
            : processor(owner), gate(mutationGate), inFlight(callbacksInFlight),
              didSuspend(! owner.isSuspended()),
              ownsGate(! gate.exchange(true, std::memory_order_seq_cst))
        {
            if (didSuspend)
                processor.suspendProcessing(true);

            // suspendProcessing prevents future wrapper callbacks but is not a
            // join for one that already passed the wrapper check. processBlock
            // increments inFlight before consulting gate, making these two
            // seq_cst atomics a complete entry handshake. Waiting happens only
            // on the message/control thread; the audio callback never waits.
            while (inFlight.load(std::memory_order_seq_cst) != 0)
                juce::Thread::yield();
        }

        ~ScopedProcessorSuspension()
        {
            if (ownsGate)
                gate.store(false, std::memory_order_seq_cst);
            if (didSuspend)
                processor.suspendProcessing(false);
        }

    private:
        juce::AudioProcessor& processor;
        std::atomic<bool>& gate;
        std::atomic<int>& inFlight;
        const bool didSuspend;
        const bool ownsGate;
    };

    class ScopedAudioCallback final
    {
    public:
        explicit ScopedAudioCallback (std::atomic<int>& callbackCount) noexcept
            : count(callbackCount)
        {
            count.fetch_add(1, std::memory_order_seq_cst);
        }

        ~ScopedAudioCallback()
        {
            count.fetch_sub(1, std::memory_order_seq_cst);
        }

    private:
        std::atomic<int>& count;
    };

    float rawParamValue (const std::atomic<float>* param, float fallback = 0.0f) noexcept
    {
        const float value = param != nullptr ? param->load(std::memory_order_relaxed) : fallback;
        return std::isfinite(value) ? value : fallback;
    }

    int rawParamInt (const std::atomic<float>* param, int fallback = 0) noexcept
    {
        const double value = static_cast<double>(
            rawParamValue(param, static_cast<float>(fallback)));
        if (value <= static_cast<double>(std::numeric_limits<int>::lowest()))
            return std::numeric_limits<int>::lowest();
        if (value >= static_cast<double>(std::numeric_limits<int>::max()))
            return std::numeric_limits<int>::max();
        return static_cast<int>(value);
    }

    bool rawParamBool (const std::atomic<float>* param, bool fallback = false) noexcept
    {
        return rawParamValue(param, fallback ? 1.0f : 0.0f) > 0.5f;
    }

    int sourceCapacityFromChoice (int choice) noexcept
    {
        static constexpr int capacities[] { 64, 128, 256 };
        return capacities[juce::jlimit(0, 2, choice)];
    }

    std::uint32_t packConductorPolicy (
        GlobalConductorHub::SnapshotSource source,
        int attackQuota, int voiceQuota) noexcept
    {
        const auto sourceBits = static_cast<std::uint32_t>(source) & 0x3u;
        const auto attackBits = static_cast<std::uint32_t>(
            juce::jlimit(0, 31, attackQuota));
        const auto voiceBits = static_cast<std::uint32_t>(
            juce::jlimit(0, 31, voiceQuota));
        return sourceBits | (attackBits << 2u) | (voiceBits << 7u);
    }

    int boundedStateInt (const juce::var& value, int minimum, int maximum,
                         int fallback) noexcept
    {
        const int safeFallback = juce::jlimit(minimum, maximum, fallback);
        double raw = 0.0;
        if (value.isInt() || value.isInt64()
            || value.isDouble() || value.isBool())
        {
            raw = static_cast<double>(value);
        }
        else if (value.isString())
        {
            // ValueTree XML attributes are restored as strings by JUCE. Parse
            // the complete token strictly: partial values such as "8000x",
            // empty text, overflow and non-finite values must fail closed.
            const auto text = value.toString().trim();
            if (text.isEmpty())
                return safeFallback;

            const char* const begin = text.toRawUTF8();
            char* end = nullptr;
            errno = 0;
            raw = std::strtod(begin, &end);
            if (end == begin || end == nullptr || *end != '\0' || errno == ERANGE)
                return safeFallback;
        }
        else
        {
            return safeFallback;
        }

        if (! std::isfinite(raw))
            return safeFallback;
        if (std::trunc(raw) != raw)
            return safeFallback;

        const double bounded = juce::jlimit(static_cast<double>(minimum),
                                            static_cast<double>(maximum), raw);
        return static_cast<int>(bounded);
    }

}

AudienceProcessor::EffectiveTimeFieldPolicy
AudienceProcessor::getEffectiveTimeFieldPolicy() const noexcept
{
    const auto packed = effectiveTimeFieldPolicyPacked.load(
        std::memory_order_acquire);
    EffectiveTimeFieldPolicy result;
    if ((packed & policyValidBit) == 0)
        return result;

    result.attacksPerStep = static_cast<int>(packed & policyFieldMask);
    result.activeLimit = static_cast<int>(
        (packed >> policyActiveShift) & policyFieldMask);
    result.spreadSlots = static_cast<int>(
        (packed >> policySpreadShift) & policyFieldMask);
    result.admissionOpen = ((packed >> policyAdmissionShift) & 1u) != 0;
    result.mode = static_cast<CrowdTimeField::Mode>(juce::jlimit(
        0, 2, static_cast<int>((packed >> policyModeShift) & 0x3u)));
    return result;
}

AudienceProcessor::AudienceProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createLayout()),
      audienceModel(fingerRouter, nullptr, &sourceQualityController),
      osc(audienceModel, OscBridge::FingerPolicy::finger0Only),
      simulator(audienceModel, MidiAudienceModel::MAX_SOURCES)
{
    cacheParameterPointers();
    // Fresh instances are intentionally light: legacy projects migrate their
    // missing capacity parameter to 256 before their quarantined route opens.
    simulator.setSourceCapacity(sourceCapacityFromChoice(
        CosmicFactoryPresets::sourceCapacity));
    audienceModel.setSourceCapacity(sourceCapacityFromChoice(
        CosmicFactoryPresets::sourceCapacity));
    releaseAllIncomingMidiNotes();
    updatePitchMap();
    mpeOut.reset();
    // The first timer tick applies the Zone A factory route. A host normally
    // restores saved state before that tick, so B-H instances do not all make
    // a short-lived attempt to own UDP 6062 / its virtual MIDI endpoint.
    juce::Timer::startTimerHz(60);
}

AudienceProcessor::~AudienceProcessor()
{
    juce::AsyncUpdater::cancelPendingUpdate();
    juce::Timer::stopTimer();
    juce::HighResolutionTimer::stopTimer();
    unregisterGlobalConductor();
    closeMidiOutput();
    osc.stop();
}

juce::AudioProcessorValueTreeState::ParameterLayout AudienceProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("midiOutputType", 1), "MIDI Format",
        StringArray { "Off", "Notes Only" },
        CosmicFactoryPresets::midiOutputType));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("midiOutputPath", 1), "MIDI Output Path",
        StringArray { "Host Only", "External Only", "Mirror" },
        CosmicFactoryPresets::midiOutputPath));

    StringArray zones { "Any" };
    for (char zone = 'A'; zone <= 'Z'; ++zone)
        zones.add(String::charToString((juce::juce_wchar) zone));
    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("expectedZone", 1), "Expected OSC Zone", zones,
        CosmicFactoryPresets::zonePreset(0).expectedZoneChoice));

    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID("exclusiveUdpPort", 1), "Exclusive UDP Port",
        CosmicFactoryPresets::exclusiveUdpPort != 0));

    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID("safetyGovernorEnabled", 1), "Safety Governor",
        CosmicFactoryPresets::safetyGovernorEnabled != 0));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("normalMidiRoutingMode", 1), "Normal MIDI Routing",
        StringArray { "Single Channel", "Per Source 1-16" },
        CosmicFactoryPresets::normalMidiRoutingMode));

    StringArray midiChannels;
    for (int channel = 1; channel <= 16; ++channel)
        midiChannels.add(String(channel));
    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("normalMidiChannel", 1), "Normal MIDI Channel", midiChannels,
        CosmicFactoryPresets::normalMidiChannel));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("mpeZone", 1), "MPE Zone",
        StringArray { "Lower", "Upper" }, 0));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("mpePitchBendRange", 1), "MPE Pitch Bend Range",
        StringArray { "2 st", "12 st", "24 st", "48 st" }, 0));

    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID("mpeSendSetupMessages", 1), "MPE Send Setup Messages", true));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("mpePitchMode", 1), "MPE Pitch Mode",
        StringArray { "Retrigger", "Glide" }, 0));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("timeMode", 1), "Time Field Mode",
        StringArray { "Flow", "Grid", "Ensemble" },
        CosmicFactoryPresets::timeMode));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("clockSource", 1), "Time Field Clock",
        StringArray { "Host", "Internal" },
        CosmicFactoryPresets::clockSource));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID("internalBpm", 1), "Internal BPM",
        NormalisableRange<float>(40.0f, 240.0f, 0.1f),
        CosmicFactoryPresets::internalBpm));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("gridDivision", 1), "Grid Division",
        StringArray { "1/4", "1/8", "1/16", "1/32" },
        CosmicFactoryPresets::gridDivision));

    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID("maxAttacksPerStep", 1), "Attacks Per Step", 1, 16,
        CosmicFactoryPresets::maxAttacksPerStep));

    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID("maxActiveVoices", 1), "Maximum Active Voices", 1, 16,
        CosmicFactoryPresets::maxActiveVoices));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID("gatePercent", 1), "Gate Length",
        NormalisableRange<float>(5.0f, 100.0f, 1.0f),
        CosmicFactoryPresets::gatePercent));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("temporalSpread", 1), "Temporal Spread",
        StringArray { "1", "2", "4", "8", "16" },
        CosmicFactoryPresets::temporalSpread));

    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID("crowdGovernorEnabled", 1), "Adaptive Crowd Governor",
        CosmicFactoryPresets::crowdGovernorEnabled != 0));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("conductorRole", 1), "Global Conductor Role",
        StringArray { "Off", "Leader", "Follower" },
        CosmicFactoryPresets::zonePreset(0).conductorRoleChoice));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("conductorGroup", 1), "Global Conductor Group",
        StringArray { "1", "2", "3", "4" },
        CosmicFactoryPresets::conductorGroup));

    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID("conductorAttackBudget", 1), "Conductor Attack Budget",
        1, 64, CosmicFactoryPresets::conductorAttackBudget));

    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID("conductorVoiceBudget", 1), "Conductor Voice Budget",
        1, 128, CosmicFactoryPresets::conductorVoiceBudget));

    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID("crowdMacrosEnabled", 1), "Crowd Macros",
        CosmicFactoryPresets::crowdMacrosEnabled != 0));

    StringArray macroChannels;
    for (int channel = 1; channel <= 16; ++channel)
        macroChannels.add("Ch " + String(channel));
    macroChannels.add("Broadcast");
    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("crowdMacroChannel", 1), "Crowd Macro Channel",
        macroChannels, CosmicFactoryPresets::crowdMacroChannel));

    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID("crowdMacroDensityCc", 1), "Crowd Density CC", 0, 127,
        CosmicFactoryPresets::crowdMacroDensityCc));
    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID("crowdMacroCentroidXCc", 1), "Crowd Centroid X CC", 0, 127,
        CosmicFactoryPresets::crowdMacroCentroidXCc));
    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID("crowdMacroCentroidYCc", 1), "Crowd Centroid Y CC", 0, 127,
        CosmicFactoryPresets::crowdMacroCentroidYCc));
    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID("crowdMacroMotionCc", 1), "Crowd Motion CC", 0, 127,
        CosmicFactoryPresets::crowdMacroMotionCc));
    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("crowdMacroRate", 1), "Crowd Macro Rate",
        StringArray { "5 Hz", "10 Hz", "20 Hz", "30 Hz" },
        CosmicFactoryPresets::crowdMacroRate));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("pitchSystem", 1), "Pitch System",
        StringArray { "Tonal", "Atomic" }, CosmicFactoryPresets::pitchSystem));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("scaleRoot", 1), "Root",
        StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" },
        CosmicFactoryPresets::scaleRoot));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("scaleRootOctave", 1), "Root Octave",
        StringArray { "0", "1", "2", "3", "4", "5", "6" },
        CosmicFactoryPresets::scaleRootOctave));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("scaleMode", 1), "Scale",
        StringArray { "Major", "Natural Minor", "Pentatonic", "Dorian",
                      "Lydian", "Harmonic Minor", "Whole Tone" },
        CosmicFactoryPresets::scaleMode));

    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID("scaleOctaves", 1), "Octave Range", 1, 6,
        CosmicFactoryPresets::scaleOctaves));

    StringArray atomicElements;
    for (int index = 0; index < AtomicScaleCatalog::numElements; ++index)
        atomicElements.add(String::fromUTF8(AtomicScaleCatalog::elementName(index))
                           + " (" + String::fromUTF8(AtomicScaleCatalog::elementSymbol(index)) + ")");
    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("spectralElement", 1), "Atomic Element", atomicElements,
        CosmicFactoryPresets::spectralElement));

    StringArray atomicModes;
    for (int index = 0; index < AtomicScaleCatalog::numModes; ++index)
        atomicModes.add(String::fromUTF8(AtomicScaleCatalog::modeName(index)));
    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("atomicScaleMode", 1), "Atomic Scale Mode", atomicModes,
        CosmicFactoryPresets::atomicScaleMode));

    // Append-only parameter evolution: keeping these after every established
    // ID preserves host automation indices in existing Ableton projects.
    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("noteDuration", 1), "Note Duration",
        StringArray { "2n", "4n", "8n", "16n", "32n" },
        CosmicFactoryPresets::noteDuration));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("sourceCapacity", 1), "Source Capacity",
        StringArray { "64 participants - 4 per MIDI channel",
                      "128 participants - 8 per MIDI channel",
                      "256 participants - 16 per MIDI channel" },
        CosmicFactoryPresets::sourceCapacity));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("ensembleSameNoteMode", 1), "Ensemble Same Note",
        StringArray { "Tie", "Retrigger" },
        CosmicFactoryPresets::ensembleSameNoteMode));

    return layout;
}

void AudienceProcessor::cacheParameterPointers()
{
    rawParams.scaleRoot = apvts.getRawParameterValue("scaleRoot");
    rawParams.scaleRootOctave = apvts.getRawParameterValue("scaleRootOctave");
    rawParams.scaleMode = apvts.getRawParameterValue("scaleMode");
    rawParams.scaleOctaves = apvts.getRawParameterValue("scaleOctaves");
    rawParams.pitchSystem = apvts.getRawParameterValue("pitchSystem");
    rawParams.spectralElement = apvts.getRawParameterValue("spectralElement");
    rawParams.atomicScaleMode = apvts.getRawParameterValue("atomicScaleMode");
    rawParams.midiOutputType = apvts.getRawParameterValue("midiOutputType");
    rawParams.midiOutputPath = apvts.getRawParameterValue("midiOutputPath");
    rawParams.expectedZone = apvts.getRawParameterValue("expectedZone");
    rawParams.exclusiveUdpPort = apvts.getRawParameterValue("exclusiveUdpPort");
    rawParams.safetyGovernorEnabled = apvts.getRawParameterValue("safetyGovernorEnabled");
    rawParams.normalMidiRoutingMode = apvts.getRawParameterValue("normalMidiRoutingMode");
    rawParams.normalMidiChannel = apvts.getRawParameterValue("normalMidiChannel");
    rawParams.mpeZone = apvts.getRawParameterValue("mpeZone");
    rawParams.mpePitchBendRange = apvts.getRawParameterValue("mpePitchBendRange");
    rawParams.mpeSendSetupMessages = apvts.getRawParameterValue("mpeSendSetupMessages");
    rawParams.mpePitchMode = apvts.getRawParameterValue("mpePitchMode");
    rawParams.timeMode = apvts.getRawParameterValue("timeMode");
    rawParams.clockSource = apvts.getRawParameterValue("clockSource");
    rawParams.internalBpm = apvts.getRawParameterValue("internalBpm");
    rawParams.gridDivision = apvts.getRawParameterValue("gridDivision");
    rawParams.maxAttacksPerStep = apvts.getRawParameterValue("maxAttacksPerStep");
    rawParams.maxActiveVoices = apvts.getRawParameterValue("maxActiveVoices");
    rawParams.gatePercent = apvts.getRawParameterValue("gatePercent");
    rawParams.temporalSpread = apvts.getRawParameterValue("temporalSpread");
    rawParams.crowdGovernorEnabled = apvts.getRawParameterValue("crowdGovernorEnabled");
    rawParams.conductorRole = apvts.getRawParameterValue("conductorRole");
    rawParams.conductorGroup = apvts.getRawParameterValue("conductorGroup");
    rawParams.conductorAttackBudget = apvts.getRawParameterValue("conductorAttackBudget");
    rawParams.conductorVoiceBudget = apvts.getRawParameterValue("conductorVoiceBudget");
    rawParams.crowdMacrosEnabled = apvts.getRawParameterValue("crowdMacrosEnabled");
    rawParams.crowdMacroChannel = apvts.getRawParameterValue("crowdMacroChannel");
    rawParams.crowdMacroDensityCc = apvts.getRawParameterValue("crowdMacroDensityCc");
    rawParams.crowdMacroCentroidXCc = apvts.getRawParameterValue("crowdMacroCentroidXCc");
    rawParams.crowdMacroCentroidYCc = apvts.getRawParameterValue("crowdMacroCentroidYCc");
    rawParams.crowdMacroMotionCc = apvts.getRawParameterValue("crowdMacroMotionCc");
    rawParams.crowdMacroRate = apvts.getRawParameterValue("crowdMacroRate");
    rawParams.noteDuration = apvts.getRawParameterValue("noteDuration");
    rawParams.sourceCapacity = apvts.getRawParameterValue("sourceCapacity");
    rawParams.ensembleSameNoteMode =
        apvts.getRawParameterValue("ensembleSameNoteMode");
}

void AudienceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Treat the host value as untrusted. Subnormal rates make the external
    // MIDI due-time calculation overflow to infinity and can head-of-line
    // block every later NoteOff in the FIFO.
    currentSampleRate = std::isfinite(sampleRate)
                      ? juce::jlimit(1.0, 768000.0, sampleRate)
                      : 44100.0;
    latestAudioBlockDurationMs.store(
        boundedBlockDurationMs(samplesPerBlock, currentSampleRate),
        std::memory_order_relaxed);
    // Do not cancel an unacknowledged releaseResources reset. A rapid
    // release->prepare sequence can happen before the 2 ms MIDI sender tick;
    // reopening here would let old-epoch NoteOns cross into the new epoch. The
    // sender discards that queue, emits the sweep, then reopens the gate.
    if (! externalTransportResetPending.load(std::memory_order_acquire))
        externalMidiProducerQuarantined.store(false, std::memory_order_release);
    midiInputScratch.ensureSize(realtimeMidiBufferReserveBytes);
    midiRenderScratch.ensureSize(realtimeMidiBufferReserveBytes);
    midiInputScratch.clear();
    midiRenderScratch.clear();
    midiRenderScratchLoanedToHost = false;
    releaseAllIncomingMidiNotes();
    mpeOut.reset();
    mpeOut.markSetupDirty();
    crowdTimeField.reset();
    resetCrowdExpressionMacros();
    adaptiveCrowdGovernor.reset();
    pressureSafetyGovernor.reset();
    safetyOutput = pressureSafetyGovernor.getOutput();
    governorLastUpdateSeconds = 0.0;
    governorControlClockInitialised = false;
    governorLastVoiceLimit = CosmicFactoryPresets::maxActiveVoices;
    safetyLastUpdateSeconds = 0.0;
    safetyLastOscMessages = osc.getValidMessageCount();
    safetyLastDroppedMotion = fingerRouter.getDroppedMotionEventCount();
    safetyClockInitialised = false;
    governorObservedDensity.store(0, std::memory_order_relaxed);
    governorEffectiveAttacks.store(4, std::memory_order_relaxed);
    governorEffectiveActive.store(8, std::memory_order_relaxed);
    governorEffectiveSpread.store(1, std::memory_order_relaxed);
    governorBand.store(0, std::memory_order_relaxed);
    safetyGovernorState.store(0, std::memory_order_relaxed);
    safetyGovernorReasons.store(0, std::memory_order_relaxed);
    safetyIngressRate.store(0.0, std::memory_order_relaxed);
    safetyExternalFifoPressure.store(0.0, std::memory_order_relaxed);
    externalFifoOldestAgeSeconds.store(0.0, std::memory_order_relaxed);
    lastProcessDeadlineRatio.store(0.0, std::memory_order_relaxed);
    requestTimeFieldRehydrate();
    hostTransportStateInitialised = false;
    lastHostTransportPlaying = false;
    // A host can call prepareToPlay again without delivering note-offs from the
    // previous processing epoch. The first new block therefore emits a bounded
    // channel reset and re-arms any OSC fingers that are still held.
    midiOutputRouteChangedPending.store(true, std::memory_order_release);
}

void AudienceProcessor::releaseResources()
{
    // releaseResources may be called from a host-owned processing thread, so it
    // only publishes work. Close the producer gate before publishing the reset,
    // so the high-resolution consumer can discard the complete old-epoch queue
    // without a later block adding a post-snapshot NoteOn. JUCE calls this only
    // after processing has stopped; prepareToPlay explicitly reopens the gate.
    requestExternalMidiReset(externalTransportResetPending);
    midiOutputRouteChangedPending.store(true, std::memory_order_release);
}

void AudienceProcessor::reset()
{
    // VST3 calls reset() from setProcessing(false), independently of
    // releaseResources(). Serialise control-side lifecycle mutations and close
    // the callback entry gate before touching audio-owned schedulers/FIFOs.
    const juce::ScopedLock transactionLock(stateTransactionLock);
    ScopedProcessorSuspension processingGuard(
        *this, controlAudioMutationGate, audioCallbacksInFlight);

    midiInputScratch.clear();
    midiRenderScratch.clear();
    releaseAllIncomingMidiNotes();
    fingerRouter.discardPendingEvents();
    (void) fingerRouter.takeResetRequest();
    mpeOut.reset();
    mpeOut.markSetupDirty();
    crowdTimeField.reset();
    for (auto& state : fingerMidiStates)
        state = {};
    resetCrowdExpressionMacros();
    adaptiveCrowdGovernor.reset();
    pressureSafetyGovernor.reset();
    safetyOutput = pressureSafetyGovernor.getOutput();

    governorLastUpdateSeconds = 0.0;
    governorControlClockInitialised = false;
    governorLastVoiceLimit = CosmicFactoryPresets::maxActiveVoices;
    safetyLastUpdateSeconds = 0.0;
    safetyLastOscMessages = osc.getValidMessageCount();
    safetyLastDroppedMotion = fingerRouter.getDroppedMotionEventCount();
    safetyClockInitialised = false;
    governorObservedDensity.store(0, std::memory_order_relaxed);
    governorEffectiveAttacks.store(4, std::memory_order_relaxed);
    governorEffectiveActive.store(8, std::memory_order_relaxed);
    governorEffectiveSpread.store(1, std::memory_order_relaxed);
    governorBand.store(0, std::memory_order_relaxed);
    safetyGovernorState.store(0, std::memory_order_relaxed);
    safetyGovernorReasons.store(0, std::memory_order_relaxed);
    safetyIngressRate.store(0.0, std::memory_order_relaxed);
    safetyExternalFifoPressure.store(0.0, std::memory_order_relaxed);
    externalFifoOldestAgeSeconds.store(0.0, std::memory_order_relaxed);
    lastProcessDeadlineRatio.store(0.0, std::memory_order_relaxed);
    timeFieldPending.store(0, std::memory_order_relaxed);
    timeFieldActive.store(0, std::memory_order_relaxed);
    timeFieldMerged.store(0, std::memory_order_relaxed);
    timeFieldClockLocked.store(false, std::memory_order_relaxed);
    effectiveTimeFieldPolicyPacked.store(0, std::memory_order_release);

    hostTransportStateInitialised = false;
    lastHostTransportPlaying = false;
    bypassResetEmitted = false;
    requestTimeFieldRehydrate();

    // reset() has no host MidiBuffer and must not perform CoreMIDI I/O. Publish
    // both destinations' reset work: the sender thread sweeps External, while
    // the first resumed processBlock emits the bounded Host sweep.
    requestExternalMidiReset(externalTransportResetPending);
    midiOutputRouteChangedPending.store(true, std::memory_order_release);
}

bool AudienceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono()
        || output == juce::AudioChannelSet::stereo();
}

void AudienceProcessor::updatePitchMap()
{
    pitchMapChangedThisBlock = false;
    const int root = juce::jlimit(0, 11,
        rawParamInt(rawParams.scaleRoot, CosmicFactoryPresets::scaleRoot));
    const int rootOctave = juce::jlimit(0, 6,
        rawParamInt(rawParams.scaleRootOctave, CosmicFactoryPresets::scaleRootOctave));
    const int mode = juce::jlimit(0, MidiPitchMap::numScaleModes - 1,
                                  rawParamInt(rawParams.scaleMode,
                                              CosmicFactoryPresets::scaleMode));
    const int octaves = juce::jlimit(1, 6,
        rawParamInt(rawParams.scaleOctaves, CosmicFactoryPresets::scaleOctaves));
    const int pitchSystem = juce::jlimit(0, 1,
        rawParamInt(rawParams.pitchSystem, CosmicFactoryPresets::pitchSystem));
    const int atomicElement = AtomicScaleCatalog::clampElementIndex(
        rawParamInt(rawParams.spectralElement,
                    CosmicFactoryPresets::spectralElement));
    const int atomicMode = AtomicScaleCatalog::clampModeIndex(
        rawParamInt(rawParams.atomicScaleMode,
                    CosmicFactoryPresets::atomicScaleMode));

    const bool changed = lastPitchSystem >= 0
                      && (root != lastScaleRootPitchClass
                       || rootOctave != lastScaleRootOctave
                       || mode != lastScaleMode
                       || octaves != lastScaleOctaves
                       || pitchSystem != lastPitchSystem
                       || atomicElement != lastAtomicElement
                       || atomicMode != lastAtomicScaleMode);

    if (root != lastScaleRootPitchClass
        || rootOctave != lastScaleRootOctave
        || mode != lastScaleMode
        || octaves != lastScaleOctaves
        || pitchSystem != lastPitchSystem
        || atomicElement != lastAtomicElement
        || atomicMode != lastAtomicScaleMode)
    {
        if (pitchSystem == 1)
        {
            // The catalog is constructed before processing and is immutable.
            // Copying and re-rooting this fixed-capacity map is bounded and
            // allocation-free, including under host parameter automation.
            atomicPitchMap = AtomicScaleCatalog::instance().getMap(atomicElement, atomicMode);
            atomicPitchMap.setRootPitchClassAndOctave(root, rootOctave, octaves);
        }
        else
        {
            pitchMap.configure(root, rootOctave, mode, octaves);
        }
        if (changed)
        {
            pitchMapChangedThisBlock = true;
        }
    }

    lastScaleRootPitchClass = root;
    lastScaleRootOctave = rootOctave;
    lastScaleMode = mode;
    lastScaleOctaves = octaves;
    lastPitchSystem = pitchSystem;
    lastAtomicElement = atomicElement;
    lastAtomicScaleMode = atomicMode;
}

bool AudienceProcessor::processIncomingMidi (const juce::MidiBuffer& input,
                                             int numSamples)
{
#if COSMIC_MIDI_DIAGNOSTICS
    recordIncomingMidiDebugEvents(input);
#endif
    midiInputScratch.clear();

    size_t storedBytes = 0;
    int storedEvents = 0;
    bool copyInput = true;
    bool overflowed = false;
    int activeCount = activeExternalMidiKeys.load(std::memory_order_relaxed);
    const int lastSample = numSamples > 0 ? numSamples - 1 : 0;
    for (const auto metadata : input)
    {
        const auto eventBytes = metadata.numBytes > 0
                              ? (size_t) metadata.numBytes + sizeof(int32_t) + sizeof(uint16_t)
                              : 0;
        if (copyInput && metadata.data != nullptr
            && metadata.numBytes > 0 && metadata.numBytes <= 65535
            && storedEvents < realtimeMidiInputEventLimit
            && eventBytes <= realtimeMidiInputBudgetBytes - storedBytes
            && midiInputScratch.addEvent(metadata.data, metadata.numBytes,
                                         juce::jlimit(0, lastSample,
                                                      metadata.samplePosition)))
        {
            storedBytes += eventBytes;
            ++storedEvents;
        }
        else if (metadata.numBytes > 0)
        {
            // Drop the whole thru block. processBlock clears lifecycle telemetry
            // on this path, so stop scanning here as well. This leaves enough of
            // the preallocated output buffer for the
            // bounded OSC/MIDI reset + retrigger burst, prevents growth, and caps
            // MidiBuffer's sorted-insertion work at a fixed event count.
            copyInput = false;
            overflowed = true;
            midiInputScratch.clear();
            break;
        }

        if (metadata.data == nullptr || metadata.numBytes < 1)
            continue;

        const auto status = metadata.data[0];
        if (status >= 0xf0)
            continue;
        const int channel = (status & 0x0f) + 1;

        const int messageType = status & 0xf0;
        if (messageType == 0xb0 && metadata.numBytes >= 2
            && (metadata.data[1] == 120 || metadata.data[1] == 123))
        {
            const int first = (channel - 1) * midiInputNotes;
            for (int note = 0; note < midiInputNotes; ++note)
            {
                auto& active = incomingMidiKeys[(size_t) (first + note)];
                if (active)
                {
                    active = false;
                    activeCount = juce::jmax(0, activeCount - 1);
                }
            }
            continue;
        }

        if ((messageType != 0x80 && messageType != 0x90) || metadata.numBytes < 3)
            continue;

        const int note = metadata.data[1] & 0x7f;
        if (note < 0 || note >= midiInputNotes)
            continue;

        lastExternalMidiChannel.store(channel, std::memory_order_relaxed);
        lastExternalMidiNote.store(note, std::memory_order_relaxed);
        auto& active = incomingMidiKeys[(size_t) ((channel - 1) * midiInputNotes + note)];
        const bool shouldBeActive = messageType == 0x90 && metadata.data[2] != 0;
        if (shouldBeActive != active)
        {
            active = shouldBeActive;
            activeCount += shouldBeActive ? 1 : -1;
        }
    }

    activeExternalMidiKeys.store(juce::jmax(0, activeCount), std::memory_order_relaxed);
    return overflowed;
}

void AudienceProcessor::releaseAllIncomingMidiNotes() noexcept
{
    incomingMidiKeys.fill(false);
    activeExternalMidiKeys.store(0, std::memory_order_relaxed);
}

void AudienceProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                      juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    ScopedAudioCallback callbackGuard(audioCallbacksInFlight);
    latestAudioBlockDurationMs.store(
        boundedBlockDurationMs(buffer.getNumSamples(), currentSampleRate),
        std::memory_order_relaxed);
    if (controlAudioMutationGate.load(std::memory_order_seq_cst))
    {
        // A control-thread panic/route mutation owns every non-atomic audio
        // structure. Fail silent before touching MIDI ledgers, Time Field,
        // Governors or the external FIFO producer.
        midiMessages.clear();
        buffer.clear();
        return;
    }
    // A normal callback ends the bypass epoch. The reset request published by
    // the first bypass block remains latched independently until its consumer
    // acknowledges it.
    bypassResetEmitted = false;
    const double externalBlockStartTimeMs = juce::Time::getMillisecondCounterHiRes();
    const auto processingStateGeneration = stateRestoreGeneration.load(
        std::memory_order_acquire);
    const auto externalResetGenerationAtBlockStart =
        externalMidiResetRequestGeneration.load(std::memory_order_acquire);

    // APVTS replacement may be reentrant, so emit nothing until one complete
    // parameter tree wins. The external route has a separate generation gate:
    // Host Only can still render in offline/headless hosts without a message
    // pump, while OSC/external MIDI remains fail-closed until its route is ready.
    if (stateRestoreCompletedGeneration.load(std::memory_order_acquire)
        != processingStateGeneration)
    {
        midiMessages.clear();
        buffer.clear();
        lastProcessDeadlineRatio.store(0.0, std::memory_order_relaxed);
        return;
    }

    // Preserve the host input before reclaiming the preallocated output buffer.
    const bool midiInputOverflowed = processIncomingMidi(midiMessages,
                                                          buffer.getNumSamples());

    const bool processingRouteReady =
        stateRouteReadyGeneration.load(std::memory_order_acquire)
            == processingStateGeneration;
    const bool hostStateResetPending =
        pendingHostMidiResetGeneration.load(std::memory_order_acquire)
            == processingStateGeneration;
    if (! processingRouteReady)
    {
        // A worker/headless host can complete its APVTS tree without a JUCE
        // message pump, but the old UDP listener must never drive that new
        // state. Until the matching route is ready, allow only bounded incoming
        // Note On/Off thru to Host/Mirror; suppress OSC, simulator and every
        // external packet. This is allocation- and lock-free on the audio path.
        if (midiRenderScratchLoanedToHost)
        {
            midiMessages.swapWith(midiRenderScratch);
            midiRenderScratchLoanedToHost = false;
        }
        midiRenderScratch.clear();
        midiMessages.clear();
        buffer.clear();

        // State recall can complete on a worker thread in a host with no JUCE
        // message pump. Terminate every note that the previous generation may
        // have emitted to Host/Mirror before allowing note-only input thru.
        // The fixed 16-channel sweep is bounded and allocation-free.
        if (hostStateResetPending)
        {
            mpeOut.emitSafetyReset(midiRenderScratch, 0);
        }

        const int pendingOutputType = juce::jlimit(
            0, 1, rawParamInt(rawParams.midiOutputType,
                              CosmicFactoryPresets::midiOutputType));
        const int pendingOutputPath = juce::jlimit(
            0, 2, rawParamInt(rawParams.midiOutputPath,
                              CosmicFactoryPresets::midiOutputPath));
        if (! midiInputOverflowed && pendingOutputType != 0
            && pendingOutputPath != 1)
        {
            for (const auto metadata : midiInputScratch)
            {
                if (metadata.data == nullptr || metadata.numBytes < 3)
                    continue;
                const auto status = metadata.data[0];
                const auto type = status < 0xf0 ? status & 0xf0 : 0;
                if (type == 0x80 || type == 0x90)
                    midiRenderScratch.addEvent(metadata.data, metadata.numBytes,
                                               metadata.samplePosition);
            }
        }
        if (midiInputOverflowed)
            releaseAllIncomingMidiNotes();

        const bool generationStillCurrent =
            stateRestoreGeneration.load(std::memory_order_acquire)
                    == processingStateGeneration
            && stateRestoreCompletedGeneration.load(std::memory_order_acquire)
                    == processingStateGeneration;
        if (! generationStillCurrent)
            midiRenderScratch.clear();
        else if (hostStateResetPending)
        {
            auto expectedGeneration = processingStateGeneration;
            pendingHostMidiResetGeneration.compare_exchange_strong(
                expectedGeneration, 0, std::memory_order_acq_rel,
                std::memory_order_acquire);
        }
        midiMessages.swapWith(midiRenderScratch);
        midiRenderScratchLoanedToHost = true;
        if (stateRestoreGeneration.load(std::memory_order_acquire)
                != processingStateGeneration
            || stateRestoreCompletedGeneration.load(std::memory_order_acquire)
                != processingStateGeneration)
            midiMessages.clear();
        lastProcessDeadlineRatio.store(0.0, std::memory_order_relaxed);
        return;
    }

    updatePitchMap();
    const bool pitchMapChanged = pitchMapChangedThisBlock;

    // APVTS parameter atomics may change at any point during a callback. Capture
    // one bounded snapshot and use it for change detection, reset decisions and
    // rendering alike. Re-reading the atomics in renderOutgoingMidi used to let
    // a mid-block automation write switch routing without the matching safety
    // reset, corrupting normal-note refcounts or legacy renderer ownership.
    const auto midiConfig = buildMpeConfig();
    auto timeConfig = buildTimeFieldConfig(midiConfig);
    const auto clockFrame = captureTimeFieldClock(buffer.getNumSamples(),
                                                   externalBlockStartTimeMs * 0.001);
    const bool hostTransportStopped = clockFrame.hostPositionAvailable
                                   && hostTransportStateInitialised
                                   && lastHostTransportPlaying
                                   && ! clockFrame.isPlaying;
    if (clockFrame.hostPositionAvailable)
    {
        lastHostTransportPlaying = clockFrame.isPlaying;
        hostTransportStateInitialised = true;
    }
    AdaptiveCrowdGovernor::Config governorConfig;
    governorConfig.voiceLimit = midiConfig.outputType == 2 ? 15 : 16;
    auto governorOutput = adaptiveCrowdGovernor.getOutput();
    constexpr double governorControlPeriodSeconds = 0.1;
    const bool governorClockMovedBack = governorControlClockInitialised
                                     && clockFrame.monotonicSeconds
                                          < governorLastUpdateSeconds;
    const bool governorUpdateDue = ! governorControlClockInitialised
                                || governorClockMovedBack
                                || clockFrame.monotonicSeconds
                                     - governorLastUpdateSeconds
                                       >= governorControlPeriodSeconds
                                || governorConfig.voiceLimit != governorLastVoiceLimit;
    if (governorUpdateDue)
    {
        // Local held tests and +25 Crowd members together form the simulated
        // participant population. Feed that stable population to the musical
        // Governor just as the eight-second live-source window represents
        // connected audience density. The getter is atomic and RT-safe;
        // Safety ingress telemetry intentionally remains real-OSC-only.
        const int representedPopulation = juce::jmax (
            governorRecentSourceCount.load (std::memory_order_acquire),
            simulator.getSimSeatCount());
        governorOutput = adaptiveCrowdGovernor.update(
            governorConfig,
            { audienceModel.getActiveSourceCount(),
              representedPopulation,
              clockFrame.monotonicSeconds });
        governorLastUpdateSeconds = clockFrame.monotonicSeconds;
        governorControlClockInitialised = true;
        governorLastVoiceLimit = governorConfig.voiceLimit;
    }
    governorEffectiveAttacks.store(governorOutput.maxAttacksPerStep,
                                   std::memory_order_relaxed);
    governorObservedDensity.store(governorOutput.observedDensity,
                                  std::memory_order_relaxed);
    governorEffectiveActive.store(governorOutput.maxActive,
                                  std::memory_order_relaxed);
    governorEffectiveSpread.store(governorOutput.spreadSlots,
                                  std::memory_order_relaxed);
    governorBand.store(governorOutput.band, std::memory_order_relaxed);

    const bool governorEnabled = rawParamBool(
        rawParams.crowdGovernorEnabled,
        CosmicFactoryPresets::crowdGovernorEnabled != 0);
    if (governorEnabled && timeConfig.mode != CrowdTimeField::Mode::Flow)
    {
        timeConfig.maxAttacksPerStep = governorOutput.maxAttacksPerStep;
        timeConfig.maxActive = governorOutput.maxActive;
        timeConfig.spreadSlots = governorOutput.spreadSlots;
    }

    constexpr double safetyControlPeriodSeconds = 0.1;
    const bool safetyClockMovedBack = safetyClockInitialised
                                   && clockFrame.monotonicSeconds
                                        < safetyLastUpdateSeconds;
    const bool safetyUpdateDue = ! safetyClockInitialised
                              || safetyClockMovedBack
                              || clockFrame.monotonicSeconds
                                   - safetyLastUpdateSeconds
                                     >= safetyControlPeriodSeconds;
    const bool safetyEnabled = rawParamBool(
        rawParams.safetyGovernorEnabled,
        CosmicFactoryPresets::safetyGovernorEnabled != 0);
    if (safetyUpdateDue)
    {
        const uint32_t oscMessages = osc.getValidMessageCount();
        const uint32_t droppedMotion = fingerRouter.getDroppedMotionEventCount();
        const double elapsed = safetyClockInitialised && ! safetyClockMovedBack
                             ? juce::jmax(1.0e-3,
                                 clockFrame.monotonicSeconds - safetyLastUpdateSeconds)
                             : safetyControlPeriodSeconds;
        const uint32_t messageDelta = oscMessages >= safetyLastOscMessages
                                    ? oscMessages - safetyLastOscMessages
                                    : oscMessages;
        const uint32_t dropDelta = droppedMotion - safetyLastDroppedMotion;

        PressureAwareSafetyGovernor::Input input;
        input.ingressEventsPerSecond = (double) messageDelta / elapsed;
        input.lifecycleQueuePressure = (double) fingerRouter.getLifecycleQueueDepth()
                                     / (double) OscFingerRouter::EVENT_QUEUE_SIZE;
        input.motionDropDelta = (double) dropDelta;
        const int admittedSourceCapacity = sourceCapacityFromChoice(rawParamInt(
            rawParams.sourceCapacity, CosmicFactoryPresets::sourceCapacity));
        // Production is one touch per admitted source. Legacy/direct multi-finger
        // callers can exceed that musical denominator, so saturate the telemetry
        // instead of turning a valid bounded queue into an invalid-input fault.
        input.timeFieldPendingPressure = juce::jlimit(
            0.0, 1.0,
            (double) timeFieldPending.load(std::memory_order_relaxed)
                / (double) juce::jmax(1, admittedSourceCapacity));
        input.externalFifoPressure = (double) externalMidiFifo.getNumReady()
                                   / (double) externalMidiQueueSize;
        input.externalFifoOldestAgeSeconds = externalFifoOldestAgeSeconds.load(
            std::memory_order_relaxed);
        input.processDeadlineRatio = lastProcessDeadlineRatio.load(
            std::memory_order_relaxed);
        input.monotonicSeconds = clockFrame.monotonicSeconds;

        if (safetyEnabled)
            safetyOutput = pressureSafetyGovernor.update({}, input);
        else
        {
            pressureSafetyGovernor.reset();
            safetyOutput = pressureSafetyGovernor.getOutput();
        }

        safetyLastUpdateSeconds = clockFrame.monotonicSeconds;
        safetyLastOscMessages = oscMessages;
        safetyLastDroppedMotion = droppedMotion;
        safetyClockInitialised = true;
        safetyIngressRate.store(input.ingressEventsPerSecond,
                                std::memory_order_relaxed);
        safetyExternalFifoPressure.store(input.externalFifoPressure,
                                         std::memory_order_relaxed);
        safetyGovernorState.store((int) safetyOutput.state,
                                  std::memory_order_relaxed);
        safetyGovernorReasons.store(safetyOutput.reasonBits,
                                    std::memory_order_relaxed);
    }

    if (safetyEnabled)
    {
        timeConfig.maxAttacksPerStep = juce::jmin(
            timeConfig.maxAttacksPerStep, safetyOutput.maxAttacksCeiling);
        timeConfig.maxActive = juce::jmin(
            timeConfig.maxActive, safetyOutput.maxActiveCeiling);
        timeConfig.spreadSlots = juce::jmax(
            timeConfig.spreadSlots, safetyOutput.minSpread);

        if (timeConfig.mode != CrowdTimeField::Mode::Flow)
        {
            timeConfig.attackAdmissionOpen = safetyOutput.admitNewAttacks;
        }
        else if (safetyOutput.state
                 != PressureAwareSafetyGovernor::State::NORMAL)
        {
            // NORMAL keeps Flow's direct legacy behaviour. Under actual
            // pressure, the same scheduler becomes a release-safe admission
            // queue instead of letting the production Flow path bypass the
            // Safety Governor.
            timeConfig.attackAdmissionOpen = safetyOutput.admitNewAttacks;
            timeConfig.flowMaxAttacksPerBlock = safetyOutput.maxAttacksCeiling;
            timeConfig.flowMaxActive = safetyOutput.maxActiveCeiling;
        }
        fingerRouter.setMotionUpdateDivisor(safetyOutput.motionUpdateDivisor);
    }
    else
    {
        timeConfig.attackAdmissionOpen = true;
        fingerRouter.setMotionUpdateDivisor(1);
    }

    const std::uint32_t conductorPolicy = conductorAudioPolicy.load(
        std::memory_order_acquire);
    const bool conductorGlobal = (conductorPolicy & 0x3u)
        == static_cast<std::uint32_t>(GlobalConductorHub::SnapshotSource::Global);
    if (conductorGlobal && timeConfig.mode != CrowdTimeField::Mode::Flow)
    {
        const int attackQuota = juce::jlimit(
            0, 16, static_cast<int>((conductorPolicy >> 2u) & 0x1fu));
        const int voiceQuota = juce::jlimit(
            0, 16, static_cast<int>((conductorPolicy >> 7u) & 0x1fu));
        timeConfig.attackAdmissionOpen = timeConfig.attackAdmissionOpen
                                      && attackQuota > 0 && voiceQuota > 0;
        if (attackQuota > 0)
            timeConfig.maxAttacksPerStep = juce::jmin(
                timeConfig.maxAttacksPerStep, attackQuota);
        if (voiceQuota > 0)
            timeConfig.maxActive = juce::jmin(timeConfig.maxActive, voiceQuota);
    }

    // The operator-armed source-quality gate is the final admission condition.
    // It never drops Off/Cancel or truncates sounding notes; CrowdTimeField
    // retains bounded intent until the message-thread census publishes READY.
    if (! sourceQualityAdmissionOpen.load(std::memory_order_acquire))
        timeConfig.attackAdmissionOpen = false;
    const int midiType = midiConfig.outputType;
    const bool outputEnabled = midiType != 0;
    const auto crowdMacroConfig = buildCrowdMacroRoutingConfig(
        outputEnabled, safetyEnabled);
    const int midiOutputPath = juce::jlimit(
        0, 2, rawParamInt(rawParams.midiOutputPath,
                          CosmicFactoryPresets::midiOutputPath));
    const int routingMode = midiConfig.normalRoutingMode;
    const int normalChannel = midiConfig.normalMidiChannel;
    const int bendRange = MpeMidiOutput::bendRangeFromChoice(
        midiConfig.pitchBendRangeChoice);
    const int setupEnabled = midiConfig.sendSetupMessages ? 1 : 0;
    const int pitchMode = midiConfig.pitchMode;
    const int timeMode = static_cast<int>(timeConfig.mode);
    const int clockSource = static_cast<int>(timeConfig.clockSource);
    const float internalBpm = static_cast<float>(timeConfig.internalBpm);
    const int gridDivision = static_cast<int>(timeConfig.division);
    const float gatePercent = static_cast<float>(timeConfig.gatePercent);
    const bool routeChanged = midiOutputRouteChangedPending.exchange(false, std::memory_order_acq_rel);

    const int previousMidiOutputPath = lastMidiOutputPath;
    const bool outputPathChanged = midiOutputPath != previousMidiOutputPath;
    const bool midiConfigChanged = midiType != lastMidiOutputType
                                || outputPathChanged
                                || routingMode != lastNormalMidiRoutingMode
                                || normalChannel != lastNormalMidiChannel
                                || bendRange != lastMpeBendRange
                                || midiConfig.masterChannel != lastMpeMaster
                                || midiConfig.memberFirst != lastMpeMemberFirst
                                || midiConfig.memberLast != lastMpeMemberLast
                                || setupEnabled != lastMpeSetupEnabled
                                || pitchMode != lastMpePitchMode;
    const bool internalTempoChanged = (clockSource == 1 || lastClockSource == 1)
                                     && std::abs(internalBpm - lastInternalBpm) > 1.0e-4f;
    const bool anyTimeValueChanged = timeMode != lastTimeMode
                                  || clockSource != lastClockSource
                                  || gridDivision != lastGridDivision
                                  || internalTempoChanged
                                  || std::abs(gatePercent - lastGatePercent) > 1.0e-4f;
    const bool timeDomainChanged = anyTimeValueChanged
                                && (timeMode != 0 || lastTimeMode != 0);
    const bool configChanged = midiConfigChanged || timeDomainChanged;
    const bool hadActiveOutput = lastMidiOutputType != 0;
    const bool needsSafetyReset = hostStateResetPending
                               || hostTransportStopped
                               || ((configChanged || routeChanged || pitchMapChanged) && hadActiveOutput)
                               || (midiInputOverflowed && (hadActiveOutput || midiType != 0));

    if (outputPathChanged
        && midiOutputReady.load(std::memory_order_acquire))
    {
        requestExternalMidiReset(externalMidiPanicPending);
    }

    lastMidiOutputType = midiType;
    lastMidiOutputPath = midiOutputPath;
    lastNormalMidiRoutingMode = routingMode;
    lastNormalMidiChannel = normalChannel;
    lastMpeBendRange = bendRange;
    lastMpeMaster = midiConfig.masterChannel;
    lastMpeMemberFirst = midiConfig.memberFirst;
    lastMpeMemberLast = midiConfig.memberLast;
    lastMpeSetupEnabled = setupEnabled;
    lastMpePitchMode = pitchMode;
    lastTimeMode = timeMode;
    lastClockSource = clockSource;
    lastInternalBpm = internalBpm;
    lastGridDivision = gridDivision;
    lastGatePercent = gatePercent;

    audienceModel.setMotionEventForwardingEnabled(timeMode == 0);

    mpeOut.setMemberRange(midiConfig.memberFirst, midiConfig.memberLast);
    if (configChanged || routeChanged || midiInputOverflowed || pitchMapChanged)
    {
        mpeOut.markSetupDirty();
        requestTimeFieldRehydrate();
    }

    if (hostTransportStopped)
    {
        // A DAW Stop is an explicit lifecycle boundary. Do not rehydrate held
        // phones immediately on the stopped transport: they may start again
        // after a fresh On or when the host resumes and the clock domain is
        // adopted again.
        fingerRouter.discardPendingEvents();
        crowdTimeField.reset();
        for (auto& state : fingerMidiStates)
            state = {};
        acknowledgeTimeFieldRehydrate(snapshotTimeFieldRehydrateRequest());
    }

    if (midiRenderScratchLoanedToHost)
    {
        midiMessages.swapWith(midiRenderScratch);
        midiRenderScratchLoanedToHost = false;
    }

    midiRenderScratch.clear();
    midiMessages.clear();
    buffer.clear();

    if (midiInputOverflowed)
        releaseAllIncomingMidiNotes();

    auto& output = midiRenderScratch;
    if (needsSafetyReset)
    {
        mpeOut.emitSafetyReset(output, 0);
    }
    else if (configChanged)
    {
        mpeOut.reset();
    }

    if (outputEnabled)
    {
        for (const auto metadata : midiInputScratch)
        {
            if (metadata.data == nullptr || metadata.numBytes < 3)
                continue;

            const auto status = metadata.data[0];
            const auto messageType = status < 0xf0 ? status & 0xf0 : 0;
            if (messageType == 0x80 || messageType == 0x90)
                output.addEvent(metadata.data, metadata.numBytes,
                                metadata.samplePosition);
        }
    }

    const auto watchdogCancelRenderResult = renderOutgoingMidi(
        output, buffer.getNumSamples(), outputEnabled,
        midiConfig, timeConfig, clockFrame, needsSafetyReset);
    renderCrowdExpressionMacros(
        output, buffer.getNumSamples(), clockFrame.monotonicSeconds,
        crowdMacroConfig,
        configChanged || routeChanged || midiInputOverflowed || pitchMapChanged);
#if COSMIC_MIDI_DIAGNOSTICS
    mpeOut.recordOutgoingMidiDebugEvents(output);
#endif
    // setStateInformation may run concurrently with a host callback. Recheck
    // the generation after rendering and before either destination sees the
    // block. A changed generation makes this block part of the quarantine;
    // the pending route transaction will panic/reset all internal state.
    const bool stateChangedDuringRender =
        stateRestoreGeneration.load(std::memory_order_acquire)
            != processingStateGeneration
        || stateRestoreCompletedGeneration.load(std::memory_order_acquire)
            != processingStateGeneration;

    const bool externalRouteReady =
        stateRouteReadyGeneration.load(std::memory_order_acquire)
            == processingStateGeneration;
    if (! stateChangedDuringRender && externalRouteReady && midiOutputPath != 0)
        queueMidiToExternalOutput(output, externalBlockStartTimeMs,
                                  buffer.getNumSamples(),
                                  externalResetGenerationAtBlockStart);

    // External Only is deliberately fail-closed. An unavailable endpoint must
    // not silently recreate the duplicate Host route this mode is meant to
    // prevent; Venue Preflight exposes the incoherent selection instead.
    if (stateChangedDuringRender)
    {
        output.clear();
    }
    else if (midiOutputPath == 1)
    {
        output.clear();
        if (hostStateResetPending
            || (outputPathChanged && previousMidiOutputPath != 1))
        {
            // This sweep is for the Host destination only. The generated-note
            // scheduler may already own fresh notes queued to the External
            // destination earlier in this block; resetting it here would lose
            // their future deadlines and could strand those notes. Append the
            // bounded host sweep without mutating audio-owned scheduler state.
            for (int channel = 1; channel <= 16; ++channel)
            {
                output.addEvent(
                    juce::MidiMessage::controllerEvent(channel, 123, 0), 0);
                output.addEvent(
                    juce::MidiMessage::controllerEvent(channel, 120, 0), 0);
            }
        }
    }

    if (hostStateResetPending && ! stateChangedDuringRender)
    {
        auto expectedGeneration = processingStateGeneration;
        pendingHostMidiResetGeneration.compare_exchange_strong(
            expectedGeneration, 0, std::memory_order_acq_rel,
            std::memory_order_acquire);
    }

    midiMessages.swapWith(midiRenderScratch);
    midiRenderScratchLoanedToHost = true;

    // Close the final race where restore begins after the pre-publication
    // check but before the host buffer swap. External packets remain queued
    // behind the same generation quarantine and are discarded by Panic.
    const bool finalStateGenerationMatches =
        stateRestoreGeneration.load(std::memory_order_acquire)
                == processingStateGeneration
        && stateRestoreCompletedGeneration.load(std::memory_order_acquire)
                == processingStateGeneration;
    if (! finalStateGenerationMatches)
        midiMessages.clear();
    else
    {
        // Acknowledge only after scheduler rendering and both destination
        // decisions. Reset paths carry the exact publication boundary sampled
        // before their queue discard, so a concurrent later watchdog batch can
        // never be falsely retired by this block.
        acknowledgeWatchdogCancelRenderResult(watchdogCancelRenderResult);
    }

    const double processEndTimeMs = juce::Time::getMillisecondCounterHiRes();
    const double deadlineMs = buffer.getNumSamples() > 0
                            ? 1000.0 * (double) buffer.getNumSamples()
                                / currentSampleRate
                            : 0.0;
    const double measuredRatio = deadlineMs > 0.0
                               ? (processEndTimeMs - externalBlockStartTimeMs)
                                   / deadlineMs
                               : 0.0;
    lastProcessDeadlineRatio.store(
        std::isfinite(measuredRatio)
            ? juce::jlimit(0.0, 1000.0, measuredRatio) : 1000.0,
        std::memory_order_relaxed);
}

void AudienceProcessor::processBlockBypassed (
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    ScopedAudioCallback callbackGuard(audioCallbacksInFlight);
    latestAudioBlockDurationMs.store(
        boundedBlockDurationMs(buffer.getNumSamples(), currentSampleRate),
        std::memory_order_relaxed);

    if (controlAudioMutationGate.load(std::memory_order_seq_cst))
    {
        midiMessages.clear();
        buffer.clear();
        return;
    }

    // Reclaim the preallocated MidiBuffer that the preceding callback loaned to
    // the wrapper. This mirrors processBlock and keeps addEvent allocation-free.
    if (midiRenderScratchLoanedToHost)
    {
        midiMessages.swapWith(midiRenderScratch);
        midiRenderScratchLoanedToHost = false;
    }
    midiRenderScratch.clear();
    midiMessages.clear();
    buffer.clear();

    if (! bypassResetEmitted)
    {
        // Bypass is a fail-closed performance boundary. Emit one Host reset,
        // clear every audio-owned tail, and ask the dedicated sender thread to
        // perform External I/O. Incoming MIDI is deliberately not passed through
        // because doing so would violate External Only routing during bypass.
        mpeOut.emitSafetyReset(midiRenderScratch, 0);
        releaseAllIncomingMidiNotes();
        fingerRouter.discardPendingEvents();
        (void) fingerRouter.takeResetRequest();
        crowdTimeField.reset();
        for (auto& state : fingerMidiStates)
            state = {};
        resetCrowdExpressionMacros();
        timeFieldPending.store(0, std::memory_order_relaxed);
        timeFieldActive.store(0, std::memory_order_relaxed);
        timeFieldMerged.store(0, std::memory_order_relaxed);
        timeFieldClockLocked.store(false, std::memory_order_relaxed);
        requestTimeFieldRehydrate();
        hostTransportStateInitialised = false;
        lastHostTransportPlaying = false;
        requestExternalMidiReset(externalMidiPanicPending);
        midiOutputRouteChangedPending.store(true, std::memory_order_release);
        bypassResetEmitted = true;
    }

    midiMessages.swapWith(midiRenderScratch);
    midiRenderScratchLoanedToHost = true;
    lastProcessDeadlineRatio.store(0.0, std::memory_order_relaxed);
}

MpeMidiOutput::MpeConfig AudienceProcessor::buildMpeConfig() const
{
    MpeMidiOutput::MpeConfig config;
    // v2.6 is deliberately a notes-only product. Keep the legacy MPE APVTS
    // parameters in the state tree so old Ableton sets still recall safely,
    // but never let them select an MPE renderer at runtime.
    config.outputType = juce::jlimit(
        0, 1, rawParamInt(rawParams.midiOutputType,
                          CosmicFactoryPresets::midiOutputType));
    config.normalRoutingMode = juce::jlimit(
        0, 1, rawParamInt(rawParams.normalMidiRoutingMode,
                          CosmicFactoryPresets::normalMidiRoutingMode));
    config.normalMidiChannel = juce::jlimit(
        0, 15, rawParamInt(rawParams.normalMidiChannel,
                           CosmicFactoryPresets::normalMidiChannel));
    config.sendSetupMessages = false;
    return config;
}

CrowdTimeField::Config AudienceProcessor::buildTimeFieldConfig (
    const MpeMidiOutput::MpeConfig& midiConfig) const noexcept
{
    juce::ignoreUnused(midiConfig); // Retained in the signature for source compatibility.
    static constexpr int spreadValues[] { 1, 2, 4, 8, 16 };

    CrowdTimeField::Config config;
    config.mode = static_cast<CrowdTimeField::Mode>(
        juce::jlimit(0, 2, rawParamInt(rawParams.timeMode,
                                      CosmicFactoryPresets::timeMode)));
    config.clockSource = static_cast<CrowdTimeField::ClockSource>(
        juce::jlimit(0, 1, rawParamInt(rawParams.clockSource,
                                      CosmicFactoryPresets::clockSource)));
    config.division = static_cast<CrowdTimeField::Division>(
        juce::jlimit(0, 3, rawParamInt(rawParams.gridDivision,
                                      CosmicFactoryPresets::gridDivision)));
    config.internalBpm = juce::jlimit(40.0, 240.0,
        (double) rawParamValue(rawParams.internalBpm,
                               CosmicFactoryPresets::internalBpm));
    config.maxAttacksPerStep = juce::jlimit(
        1, 16, rawParamInt(rawParams.maxAttacksPerStep,
                           CosmicFactoryPresets::maxAttacksPerStep));
    config.maxActive = juce::jlimit(1, 16,
        rawParamInt(rawParams.maxActiveVoices,
                    CosmicFactoryPresets::maxActiveVoices));
    config.gatePercent = juce::jlimit(
        5.0, 100.0,
        (double) rawParamValue(rawParams.gatePercent,
                               CosmicFactoryPresets::gatePercent));
    config.spreadSlots = spreadValues[juce::jlimit(
        0, 4, rawParamInt(rawParams.temporalSpread,
                          CosmicFactoryPresets::temporalSpread))];
    // Different zone instances commonly reuse the same participant IDs. Fold
    // the stable UDP endpoint into Ensemble's lane phase so those zones do not
    // all attack on the same host tick.
    config.laneSeed = static_cast<std::uint32_t>(getUdpPort());
    return CrowdTimeField::sanitiseConfig(config);
}

AudienceProcessor::CrowdMacroRoutingConfig
AudienceProcessor::buildCrowdMacroRoutingConfig (
    bool outputEnabled, bool safetyEnabled) const noexcept
{
    static constexpr double rateValues[] { 5.0, 10.0, 20.0, 30.0 };

    CrowdMacroRoutingConfig config;
    config.channelChoice = juce::jlimit(
        0, 16, rawParamInt(rawParams.crowdMacroChannel,
                           CosmicFactoryPresets::crowdMacroChannel));
    config.controllers = {
        juce::jlimit(0, 127, rawParamInt(rawParams.crowdMacroDensityCc,
                                        CosmicFactoryPresets::crowdMacroDensityCc)),
        juce::jlimit(0, 127, rawParamInt(rawParams.crowdMacroCentroidXCc,
                                        CosmicFactoryPresets::crowdMacroCentroidXCc)),
        juce::jlimit(0, 127, rawParamInt(rawParams.crowdMacroCentroidYCc,
                                        CosmicFactoryPresets::crowdMacroCentroidYCc)),
        juce::jlimit(0, 127, rawParamInt(rawParams.crowdMacroMotionCc,
                                        CosmicFactoryPresets::crowdMacroMotionCc))
    };
    config.rateChoice = juce::jlimit(
        0, 3, rawParamInt(rawParams.crowdMacroRate,
                          CosmicFactoryPresets::crowdMacroRate));
    config.rateHz = rateValues[config.rateChoice];

    // Notes Only means no musical controller stream. The legacy macro
    // parameters remain state-compatible but are intentionally inert.
    juce::ignoreUnused(outputEnabled, safetyEnabled);
    config.effectiveEnabled = false;
    return config;
}

void AudienceProcessor::renderCrowdExpressionMacros (
    juce::MidiBuffer& midiMessages, int numSamples,
    double monotonicSeconds, const CrowdMacroRoutingConfig& config,
    bool resetBoundary) noexcept
{
    static_assert(CrowdExpressionMacros::kMaxSources
                    == static_cast<std::size_t>(MidiAudienceModel::MAX_SOURCES),
                  "Crowd macro and audience source domains must match");

    const bool mappingChanged = crowdMacroConfigInitialised
        && (config.channelChoice != lastCrowdMacroConfig.channelChoice
            || config.controllers != lastCrowdMacroConfig.controllers
            || config.rateChoice != lastCrowdMacroConfig.rateChoice);
    const bool enableChanged = ! crowdMacroConfigInitialised
        || config.effectiveEnabled != lastCrowdMacroConfig.effectiveEnabled;

    if (resetBoundary || mappingChanged)
    {
        crowdExpressionMacros.reset();
        crowdMacroAnalysisClockInitialised = false;
    }

    const bool finiteClock = std::isfinite(monotonicSeconds);
    const bool clockMovedBack = crowdMacroAnalysisClockInitialised
                             && finiteClock
                             && monotonicSeconds < crowdMacroLastAnalysisSeconds;
    const double analysisPeriodSeconds = 1.0 / config.rateHz;
    const bool periodElapsed = crowdMacroAnalysisClockInitialised
                            && finiteClock
                            && monotonicSeconds - crowdMacroLastAnalysisSeconds
                                 + 1.0e-12 >= analysisPeriodSeconds;
    const bool analysisDue = ! crowdMacroAnalysisClockInitialised
                          || enableChanged || clockMovedBack || periodElapsed;

    crowdMacroEffectiveEnabled.store(config.effectiveEnabled,
                                     std::memory_order_relaxed);
    if (! analysisDue)
        return;

    for (std::size_t index = 0;
         index < CrowdExpressionMacros::kMaxSources; ++index)
    {
        const auto source = audienceModel.getSourceSnapshot(
            static_cast<int>(index));
        auto& destination = crowdMacroInputScratch.sources[index];
        destination.active = source.active;
        destination.x = source.x;
        destination.y = source.y;
    }
    crowdMacroInputScratch.monotonicSeconds = monotonicSeconds;

    CrowdExpressionMacros::Config engineConfig;
    engineConfig.enabled = config.effectiveEnabled;
    engineConfig.rateHz = config.rateHz;
    crowdMacroOutput = crowdExpressionMacros.update(
        engineConfig, crowdMacroInputScratch);

    crowdMacroLastAnalysisSeconds = finiteClock ? monotonicSeconds : 0.0;
    crowdMacroAnalysisClockInitialised = true;
    lastCrowdMacroConfig = config;
    crowdMacroConfigInitialised = true;

    crowdMacroActiveSources.store(
        static_cast<int>(crowdMacroOutput.activeSources),
        std::memory_order_relaxed);
    crowdMacroDensity.store(crowdMacroOutput.density,
                            std::memory_order_relaxed);
    crowdMacroCentroidX.store(crowdMacroOutput.centroidX,
                              std::memory_order_relaxed);
    crowdMacroCentroidY.store(crowdMacroOutput.centroidY,
                              std::memory_order_relaxed);
    crowdMacroMotion.store(crowdMacroOutput.motion,
                           std::memory_order_relaxed);
    crowdMacroDensityCcValue.store(crowdMacroOutput.values.density,
                                   std::memory_order_relaxed);
    crowdMacroCentroidXCcValue.store(crowdMacroOutput.values.centroidX,
                                     std::memory_order_relaxed);
    crowdMacroCentroidYCcValue.store(crowdMacroOutput.values.centroidY,
                                     std::memory_order_relaxed);
    crowdMacroMotionCcValue.store(crowdMacroOutput.values.motion,
                                  std::memory_order_relaxed);

    // v2.6 keeps the retired analysis/state topology readable for old sessions,
    // but there is deliberately no MIDI-emission branch here. Even a future
    // configuration error cannot turn these values into CC messages.
    juce::ignoreUnused(midiMessages, numSamples);
}

void AudienceProcessor::resetCrowdExpressionMacros() noexcept
{
    crowdExpressionMacros.reset();
    crowdMacroInputScratch = {};
    crowdMacroOutput = crowdExpressionMacros.getOutput();
    lastCrowdMacroConfig = {};
    crowdMacroLastAnalysisSeconds = 0.0;
    crowdMacroAnalysisClockInitialised = false;
    crowdMacroConfigInitialised = false;

    crowdMacroEffectiveEnabled.store(false, std::memory_order_relaxed);
    crowdMacroActiveSources.store(0, std::memory_order_relaxed);
    crowdMacroDensity.store(0.0, std::memory_order_relaxed);
    crowdMacroCentroidX.store(0.5, std::memory_order_relaxed);
    crowdMacroCentroidY.store(0.5, std::memory_order_relaxed);
    crowdMacroMotion.store(0.0, std::memory_order_relaxed);
    crowdMacroDensityCcValue.store(0, std::memory_order_relaxed);
    crowdMacroCentroidXCcValue.store(64, std::memory_order_relaxed);
    crowdMacroCentroidYCcValue.store(64, std::memory_order_relaxed);
    crowdMacroMotionCcValue.store(0, std::memory_order_relaxed);
}

CrowdTimeField::ClockFrame AudienceProcessor::captureTimeFieldClock (
    int numSamples, double monotonicSeconds) const noexcept
{
    CrowdTimeField::ClockFrame frame;
    frame.sampleRate = currentSampleRate;
    frame.numSamples = juce::jmax(0, numSamples);
    frame.monotonicSeconds = std::isfinite(monotonicSeconds) ? monotonicSeconds : 0.0;

    if (auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            frame.hostPositionAvailable = true;
            frame.isPlaying = position->getIsPlaying();
            const auto bpm = position->getBpm();
            const auto ppq = position->getPpqPosition();
            if (bpm.hasValue() && std::isfinite(*bpm) && *bpm > 0.0)
            {
                frame.hostBpmValid = true;
                frame.bpm = *bpm;
            }
            if (frame.hostBpmValid && ppq.hasValue() && std::isfinite(*ppq))
            {
                frame.hostValid = true;
                frame.ppqPosition = *ppq;
            }
        }
    }

    return frame;
}

void AudienceProcessor::requestTimeFieldRehydrate() noexcept
{
    timeFieldRehydrateRequestGeneration.fetch_add(1, std::memory_order_release);
}

std::uint64_t AudienceProcessor::snapshotTimeFieldRehydrateRequest() const noexcept
{
    return timeFieldRehydrateRequestGeneration.load(std::memory_order_acquire);
}

bool AudienceProcessor::isTimeFieldRehydratePending (
    std::uint64_t requestTarget) const noexcept
{
    return requestTarget != timeFieldRehydrateAcknowledgedGeneration;
}

void AudienceProcessor::acknowledgeTimeFieldRehydrate (
    std::uint64_t requestTarget) noexcept
{
    // Only the audio owner advances the acknowledgement. If a control thread
    // publishes a newer generation after requestTarget was sampled, equality
    // remains false and the next callback performs the newer rebuild.
    timeFieldRehydrateAcknowledgedGeneration = requestTarget;
}

void AudienceProcessor::rehydrateTimeFieldFromCanonical() noexcept
{
    const int admittedSources = sourceCapacityFromChoice(rawParamInt(
        rawParams.sourceCapacity, CosmicFactoryPresets::sourceCapacity));
    int heldCount = 0;
    for (int voice = 0; voice < CrowdTimeField::kMaxVoices; ++voice)
    {
        const int sourceId = CrowdTimeField::sourceIdForVoice(voice);
        const int finger = voice % CrowdTimeField::kFingersPerSource;
        const auto canonical = sourceId < admittedSources
                             ? audienceModel.getFingerSnapshot(sourceId, finger)
                             : MidiAudienceModel::FingerSnapshot {};
        auto& midiState = fingerMidiStates[(size_t) voice];
        midiState = {};
        midiState.x = canonical.x;
        midiState.y = canonical.y;

        if (canonical.active && heldCount < (int) timeFieldHeldScratch.size())
            timeFieldHeldScratch[(size_t) heldCount++] = { voice, sourceId };
    }

    crowdTimeField.rehydrate(timeFieldHeldScratch.data(), heldCount);
}

AudienceProcessor::WatchdogCancelRenderResult
AudienceProcessor::renderOutgoingMidi (
    juce::MidiBuffer& midiMessages, int numSamples, bool outputEnabled,
    const MpeMidiOutput::MpeConfig& config,
    const CrowdTimeField::Config& timeConfig,
    const CrowdTimeField::ClockFrame& clockFrame,
    bool resetAlreadyEmitted)
{
    return renderTimedOutgoingMidi(midiMessages, numSamples, outputEnabled,
                                   config, timeConfig, clockFrame,
                                   resetAlreadyEmitted);
}

AudienceProcessor::WatchdogCancelRenderResult
AudienceProcessor::renderTimedOutgoingMidi (
    juce::MidiBuffer& midiMessages, int numSamples, bool outputEnabled,
    const MpeMidiOutput::MpeConfig& midiConfig,
    const CrowdTimeField::Config& timeConfig,
    const CrowdTimeField::ClockFrame& clockFrame, bool resetAlreadyEmitted)
{
    const int blockBudget = juce::jlimit(1, midiLifecycleEventBudget,
                                         juce::jmax(1, numSamples));
    CrowdTimeField::Config effectiveTimeConfig = timeConfig;
    if (effectiveTimeConfig.mode == CrowdTimeField::Mode::Flow)
    {
        // Flow remains immediate, but rehydration and hostile bursts must obey
        // the same per-callback semantic budget as the former direct path.
        // This is a soft policy field and never creates a transport reset.
        effectiveTimeConfig.flowMaxAttacksPerBlock = juce::jmin(
            effectiveTimeConfig.flowMaxAttacksPerBlock, blockBudget);
    }
    effectiveTimeFieldPolicyPacked.store(
        packEffectiveTimeFieldPolicy(effectiveTimeConfig),
        std::memory_order_release);
    int midiEventCount = 0;
    WatchdogCancelRenderResult watchdogResult;

    MpeMidiOutput::TimingConfig noteTiming;
    noteTiming.sampleRate = currentSampleRate;
    // Note length follows the host tempo even when Time Field attack starts
    // are in Flow mode or the transport is stopped. Standalone/no-playhead
    // operation falls back to the saved internal tempo.
    noteTiming.bpm = clockFrame.hostBpmValid
                   ? clockFrame.bpm : effectiveTimeConfig.internalBpm;
    noteTiming.noteDuration = static_cast<MpeMidiOutput::NoteDuration>(
        juce::jlimit(0, 4, rawParamInt(rawParams.noteDuration,
                                       CosmicFactoryPresets::noteDuration)));
    // Same-note retriggering is an Ensemble articulation only. Flow and Grid
    // retain historical tied ownership even if host automation leaves the
    // saved Ensemble choice on Retrigger.
    noteTiming.sameNotePolicy =
        effectiveTimeConfig.mode == CrowdTimeField::Mode::Ensemble
        && rawParamInt(rawParams.ensembleSameNoteMode,
                       CosmicFactoryPresets::ensembleSameNoteMode) == 1
            ? MpeMidiOutput::SameNotePolicy::Retrigger
            : MpeMidiOutput::SameNotePolicy::Tie;
    noteDurationBpm.store(noteTiming.bpm, std::memory_order_relaxed);

    auto renderMidiBlock = [&]() noexcept
    {
        // The deadline heap advances exactly once per audio block, including
        // blocks with no new OSC lifecycle events. This is what guarantees
        // sample-accurate Note Off delivery without per-note timers.
        mpeOut.render(midiConfig,
                      midiEventCount > 0 ? midiNoteEventScratch.data() : nullptr,
                      midiEventCount, midiMessages, numSamples, noteTiming);

        if (effectiveTimeConfig.mode == CrowdTimeField::Mode::Grid)
        {
            // Grid owns an admission lease independently from the fixed MIDI
            // tail. Release that lease only after the renderer confirms the
            // semantic voice has no remaining tail (tempo changes and multiple
            // pitch tails therefore cannot make the two state machines drift).
            for (int index = 0; index < mpeOut.getEndedVoiceCount(); ++index)
            {
                const int voiceId = mpeOut.getEndedVoiceId(index);
                if (voiceId < 0 || voiceId >= CrowdTimeField::kMaxVoices
                    || mpeOut.getScheduledNoteCountForVoice(voiceId) != 0)
                    continue;

                if (crowdTimeField.expireAudibleVoice(voiceId))
                {
                    auto& state = fingerMidiStates[(size_t) voiceId];
                    state.active = false;
                    state.pitchKey = -1;
                }
            }

            timeFieldPending.store(crowdTimeField.getPendingCount(),
                                   std::memory_order_relaxed);
            timeFieldActive.store(crowdTimeField.getActiveCount(),
                                  std::memory_order_relaxed);
        }
    };

    auto appendMidi = [this, &midiEventCount, outputEnabled]
                      (const MpeMidiOutput::NoteEvent& event) noexcept
    {
        if (outputEnabled && midiEventCount < (int) midiNoteEventScratch.size())
        {
            midiNoteEventScratch[(size_t) midiEventCount++] = event;
            return true;
        }
        return false;
    };

    auto appendAllOff = [&]() noexcept
    {
        MpeMidiOutput::NoteEvent event;
        event.type = MpeMidiOutput::NoteEvent::AllNotesOff;
        appendMidi(event);
    };

    auto primeAndRehydrate = [&, this] (std::uint64_t requestTarget) noexcept
    {
        crowdTimeField.reset();
        CrowdTimeField::OutputBlock prime;
        crowdTimeField.process(effectiveTimeConfig, clockFrame, nullptr, 0, prime);
        rehydrateTimeFieldFromCanonical();
        acknowledgeTimeFieldRehydrate(requestTarget);
        timeFieldBpm.store(prime.effectiveBpm, std::memory_order_relaxed);
        timeFieldClockLocked.store(prime.clockLocked, std::memory_order_relaxed);
        timeFieldPending.store(crowdTimeField.getPendingCount(), std::memory_order_relaxed);
        timeFieldActive.store(crowdTimeField.getActiveCount(), std::memory_order_relaxed);
        timeFieldMerged.store(crowdTimeField.getMergedCount(), std::memory_order_relaxed);
    };

    const bool routerReset = fingerRouter.takeResetRequest();
    const auto rehydrateRequestTarget = snapshotTimeFieldRehydrateRequest();
    if (routerReset || isTimeFieldRehydratePending(rehydrateRequestTarget))
    {
        // The reset covers exactly the Cancels published before this discard.
        // Loading the counter afterwards would let a concurrently published
        // future batch be acknowledged without ever reaching MIDI output.
        const auto resetAcknowledgedTarget = watchdogCancelsPublished.load(
            std::memory_order_acquire);
        fingerRouter.discardPendingEvents();
        if (! resetAlreadyEmitted)
            appendAllOff();
        primeAndRehydrate(rehydrateRequestTarget);
        renderMidiBlock();
        if (resetAlreadyEmitted || outputEnabled)
            watchdogResult.resetAcknowledgedTarget = resetAcknowledgedTarget;
        return watchdogResult;
    }

    auto resolvePitch = [this] (float x, FingerMidiState& state) noexcept
    {
        if (lastPitchSystem == 1)
        {
            const auto pitch = atomicPitchMap.xToPitch(x);
            state.pitchKey = pitch.step;
            state.frequencyHz = pitch.isValid() ? pitch.frequencyHz : 261.6255653005986;
        }
        else
        {
            const auto pitch = pitchMap.xToPitch(x);
            state.pitchKey = pitch.step;
            state.frequencyHz = pitch.isValid() ? pitch.frequencyHz : 261.6255653005986;
        }
    };

    auto appendFinger = [&] (MpeMidiOutput::NoteEvent::Type type,
                             int sourceId, int finger,
                             const FingerMidiState& state,
                             int sampleOffset) noexcept
    {
        MpeMidiOutput::NoteEvent event;
        event.type = type;
        event.sourceId = CrowdTimeField::voiceIdFor(sourceId, finger);
        event.participantId = sourceId;
        event.sampleOffset = sampleOffset;
        event.frequencyHz = state.frequencyHz;
        event.velocity = state.y;
        event.x = state.x;
        event.y = state.y;
        return appendMidi(event);
    };

    const int drained = fingerRouter.drain(fingerEventScratch.data(), blockBudget);
    const int admittedSources = sourceCapacityFromChoice(rawParamInt(
        rawParams.sourceCapacity, CosmicFactoryPresets::sourceCapacity));
    int inputCount = 0;
    int flowMotionCount = 0;
    for (int index = 0; index < drained; ++index)
    {
        const auto& incoming = fingerEventScratch[(size_t) index];
        const auto type = (OscFingerRouter::Event::Type) incoming.type;
        const int sourceId = (int) incoming.sourceId;
        const int finger = (int) incoming.finger;
        if (sourceId < 0 || sourceId >= admittedSources
            || finger < 0 || finger >= CrowdTimeField::kFingersPerSource)
            continue;

        const int voiceId = CrowdTimeField::voiceIdFor(sourceId, finger);
        if (type == OscFingerRouter::Event::X
            || type == OscFingerRouter::Event::Y)
        {
            if (effectiveTimeConfig.mode != CrowdTimeField::Mode::Flow)
                continue;

            bool alreadyQueued = false;
            for (int motion = 0; motion < flowMotionCount; ++motion)
                alreadyQueued = alreadyQueued
                             || flowMotionVoiceScratch[(size_t) motion] == voiceId;
            if (! alreadyQueued && flowMotionCount < (int) flowMotionVoiceScratch.size())
                flowMotionVoiceScratch[(size_t) flowMotionCount++] = voiceId;
            continue;
        }

        if (type != OscFingerRouter::Event::On
            && type != OscFingerRouter::Event::Off
            && type != OscFingerRouter::Event::Cancel)
            continue;

        auto& event = timeFieldInputScratch[(size_t) inputCount++];
        event.type = type == OscFingerRouter::Event::On
                   ? CrowdTimeField::InputEvent::Type::On
                   : type == OscFingerRouter::Event::Cancel
                       ? CrowdTimeField::InputEvent::Type::Cancel
                       : CrowdTimeField::InputEvent::Type::Off;
        event.voiceId = voiceId;
        event.sourceId = sourceId;
        event.sampleOffset = 0;
    }

    crowdTimeField.process(effectiveTimeConfig, clockFrame,
                           timeFieldInputScratch.data(), inputCount,
                           timeFieldOutputScratch);

    timeFieldBpm.store(timeFieldOutputScratch.effectiveBpm, std::memory_order_relaxed);
    timeFieldClockLocked.store(timeFieldOutputScratch.clockLocked, std::memory_order_relaxed);
    timeFieldPending.store(timeFieldOutputScratch.pendingCount, std::memory_order_relaxed);
    timeFieldActive.store(timeFieldOutputScratch.activeCount, std::memory_order_relaxed);
    timeFieldMerged.store(timeFieldOutputScratch.mergedCount, std::memory_order_relaxed);

    if (timeFieldOutputScratch.resetRequested)
    {
        // A seek/loop/clock-domain reset invalidates the undrained half of the
        // old lifecycle stream too. Rebuild from the canonical finger ledger;
        // events published concurrently after this snapshot remain queued and
        // will be applied in their new order on the following block.
        const auto resetAcknowledgedTarget = watchdogCancelsPublished.load(
            std::memory_order_acquire);
        fingerRouter.discardPendingEvents();
        if (! resetAlreadyEmitted)
            appendAllOff();
        primeAndRehydrate(snapshotTimeFieldRehydrateRequest());
        renderMidiBlock();
        if (resetAlreadyEmitted || outputEnabled)
            watchdogResult.resetAcknowledgedTarget = resetAcknowledgedTarget;
        return watchdogResult;
    }

    for (int index = 0; index < timeFieldOutputScratch.count; ++index)
    {
        const auto& scheduled = timeFieldOutputScratch.events[(size_t) index];
        if (scheduled.voiceId < 0 || scheduled.voiceId >= CrowdTimeField::kMaxVoices
            || scheduled.sourceId < 0 || scheduled.sourceId >= CrowdTimeField::kMaxSources)
            continue;

        const int finger = scheduled.voiceId % CrowdTimeField::kFingersPerSource;
        auto& state = fingerMidiStates[(size_t) scheduled.voiceId];
        const auto canonical = audienceModel.getFingerSnapshot(scheduled.sourceId, finger);
        const int sampleOffset = juce::jlimit(0, juce::jmax(0, numSamples - 1),
                                               scheduled.sampleOffset);

        switch (scheduled.type)
        {
            case CrowdTimeField::OutputEvent::Type::Attack:
                state.x = canonical.x;
                state.y = canonical.y;
                state.active = true;
                resolvePitch(state.x, state);
                appendFinger(MpeMidiOutput::NoteEvent::NoteOn,
                             scheduled.sourceId, finger, state, sampleOffset);
                break;

            case CrowdTimeField::OutputEvent::Type::Release:
                if (state.active)
                {
                    appendFinger(MpeMidiOutput::NoteEvent::NoteOff,
                                 scheduled.sourceId, finger, state, sampleOffset);
                    state.active = false;
                    state.pitchKey = -1;
                }
                break;

            case CrowdTimeField::OutputEvent::Type::Cancel:
                if (appendFinger(MpeMidiOutput::NoteEvent::CancelVoice,
                                 scheduled.sourceId, finger, state,
                                 sampleOffset))
                    ++watchdogResult.processedCount;
                state.active = false;
                state.pitchKey = -1;
                break;

            case CrowdTimeField::OutputEvent::Type::SampleMotion:
                if (state.active)
                {
                    const int previousPitch = state.pitchKey;
                    state.x = canonical.x;
                    state.y = canonical.y;
                    resolvePitch(state.x, state);
                    appendFinger(state.pitchKey != previousPitch
                                     ? MpeMidiOutput::NoteEvent::NoteOn
                                     : MpeMidiOutput::NoteEvent::Expression,
                                 scheduled.sourceId, finger, state, sampleOffset);
                }
                break;
        }
    }

    // Flow forwards motion continuously, unlike Grid/Ensemble which sample
    // canonical position on their own ticks. Lifecycle output always runs
    // first: an Off in this block closes ownership before any expression can
    // be emitted, and a newly admitted Attack already contains the latest
    // position so it does not need a duplicate expression packet.
    if (effectiveTimeConfig.mode == CrowdTimeField::Mode::Flow)
    {
        for (int index = 0; index < flowMotionCount
                            && midiEventCount < (int) midiNoteEventScratch.size(); ++index)
        {
            const int voiceId = flowMotionVoiceScratch[(size_t) index];
            auto& state = fingerMidiStates[(size_t) voiceId];
            if (! state.active)
                continue;

            bool attackedThisBlock = false;
            for (int eventIndex = 0; eventIndex < timeFieldOutputScratch.count; ++eventIndex)
            {
                const auto& scheduled = timeFieldOutputScratch.events[(size_t) eventIndex];
                attackedThisBlock = attackedThisBlock
                                 || (scheduled.voiceId == voiceId
                                     && scheduled.type
                                        == CrowdTimeField::OutputEvent::Type::Attack);
            }
            if (attackedThisBlock)
                continue;

            const int sourceId = CrowdTimeField::sourceIdForVoice(voiceId);
            const int finger = voiceId % CrowdTimeField::kFingersPerSource;
            const auto canonical = audienceModel.getFingerSnapshot(sourceId, finger);
            const int previousPitch = state.pitchKey;
            state.x = canonical.x;
            state.y = canonical.y;
            resolvePitch(state.x, state);
            appendFinger(state.pitchKey != previousPitch
                             ? MpeMidiOutput::NoteEvent::NoteOn
                             : MpeMidiOutput::NoteEvent::Expression,
                         sourceId, finger, state, 0);
        }
    }

    renderMidiBlock();
    return watchdogResult;
}

void AudienceProcessor::acknowledgeWatchdogCancelRenderResult (
    const WatchdogCancelRenderResult& result) noexcept
{
    if (result.resetAcknowledgedTarget > 0)
    {
        auto completed = watchdogCancelsProcessed.load(
            std::memory_order_relaxed);
        while (completed < result.resetAcknowledgedTarget
               && ! watchdogCancelsProcessed.compare_exchange_weak(
                    completed, result.resetAcknowledgedTarget,
                    std::memory_order_release, std::memory_order_relaxed))
        {
        }
    }
    else if (result.processedCount > 0)
    {
        watchdogCancelsProcessed.fetch_add(
            static_cast<std::uint64_t>(result.processedCount),
            std::memory_order_release);
    }
}

void AudienceProcessor::recordIncomingMidiDebugEvents (const juce::MidiBuffer& midiMessages) noexcept
{
#if COSMIC_MIDI_DIAGNOSTICS
    int visited = 0;
    for (const auto metadata : midiMessages)
    {
        if (visited++ >= realtimeMidiInputEventLimit)
            break;

        const int size = metadata.numBytes;
        if (size <= 0 || size > 3)
            continue;

        const auto sequence = incomingMidiDebugWriteCounter.fetch_add(1, std::memory_order_relaxed) + 1;
        auto& slot = incomingMidiDebugEvents[(size_t) ((sequence - 1) % midiDebugEventQueueSize)];
        slot.sequence.store(0, std::memory_order_release);
        slot.sampleOffset.store(metadata.samplePosition, std::memory_order_relaxed);
        slot.size.store(size, std::memory_order_relaxed);
        const auto* bytes = metadata.data;
        slot.byte0.store(size > 0 ? bytes[0] : 0, std::memory_order_relaxed);
        slot.byte1.store(size > 1 ? bytes[1] : 0, std::memory_order_relaxed);
        slot.byte2.store(size > 2 ? bytes[2] : 0, std::memory_order_relaxed);
        slot.sequence.store(sequence, std::memory_order_release);
    }
#else
    juce::ignoreUnused(midiMessages);
#endif
}

void AudienceProcessor::queueMidiToExternalOutput (const juce::MidiBuffer& midiMessages,
                                                   double blockStartTimeMs,
                                                   int numSamples,
                                                   std::uint32_t blockResetGeneration) noexcept
{
    if (! midiOutputReady.load(std::memory_order_acquire)
        || isExternalMidiResetPending()
        || externalMidiResetRequestGeneration.load(std::memory_order_acquire)
            != blockResetGeneration
        || externalMidiResetAcknowledgedGeneration.load(std::memory_order_acquire)
            != blockResetGeneration)
        return;

    const double safeBlockStart = std::isfinite(blockStartTimeMs)
                                ? blockStartTimeMs : juce::Time::getMillisecondCounterHiRes();
    const double millisecondsPerSample = 1000.0 / currentSampleRate;
    const int lastSample = juce::jmax(0, numSamples - 1);

    for (const auto metadata : midiMessages)
    {
        if (isExternalMidiResetPending())
            return;

        const int size = metadata.numBytes;
        if (size <= 0 || size > 3)
            continue;

        int start1, size1, start2, size2;
        externalMidiFifo.prepareToWrite(1, start1, size1, start2, size2);
        if (size1 <= 0 && size2 <= 0)
        {
            externalMidiDropped.fetch_add(1, std::memory_order_relaxed);
            // Stop this producer before publishing the panic. The timer consumer
            // can then discard the complete pre-overflow queue, send one ordered
            // reset sweep, and only afterwards re-open the producer gate.
            requestExternalMidiReset(externalMidiPanicPending);
            return;
        }

        const int slotIndex = size1 > 0 ? start1 : start2;
        auto& packed = externalMidiEvents[(size_t) slotIndex];
        packed.size = (juce::uint8) size;
        const auto* bytes = metadata.data;
        for (int i = 0; i < size; ++i)
            packed.data[i] = bytes[i];
        const int sampleOffset = juce::jlimit(0, lastSample, metadata.samplePosition);
        packed.dueTimeMs = safeBlockStart + (double) sampleOffset * millisecondsPerSample;
        packed.resetGeneration = blockResetGeneration;
        externalMidiFifo.finishedWrite(1);

        // A reset may be published after the FIFO reservation but before its
        // commit. The stamped event is now visible, but it belongs to the old
        // generation and the consumer will discard it without sending. Stop
        // adding the remainder of this pre-boundary block immediately.
        if (externalMidiResetRequestGeneration.load(std::memory_order_acquire)
                != blockResetGeneration)
            return;
    }
}

void AudienceProcessor::requestExternalMidiReset (
    std::atomic<bool>& resetFlag) noexcept
{
    // Publish the generation first. Even if the sender is between its flag
    // exchange and its quarantine clear, generation != acknowledgement keeps
    // both the audio producer and sender drain fail-closed.
    externalMidiResetRequestGeneration.fetch_add(1, std::memory_order_acq_rel);
    externalMidiProducerQuarantined.store(true, std::memory_order_release);
    resetFlag.store(true, std::memory_order_release);
}

void AudienceProcessor::serviceExternalWatchdogFallback (
    std::uint32_t now, int expiredLiveTouches) noexcept
{
    auto clearWatchdogFallback = [this]() noexcept
    {
        externalWatchdogCancelPending = false;
        externalWatchdogResetRequested = false;
        externalWatchdogCancelDrainBlocks = 1;
    };

    // A reset acknowledged after this batch was armed has already performed
    // the physical 16-channel sweep. Retire the latch before admitting any new
    // expiries so it cannot fire later against a newly opened endpoint.
    const auto resetAckGeneration =
        externalMidiResetAcknowledgedGeneration.load(std::memory_order_acquire);
    if (externalWatchdogCancelPending
        && resetAckGeneration != externalWatchdogResetAckGeneration)
    {
        clearWatchdogFallback();
    }
    else if (externalWatchdogCancelPending
             && watchdogCancelsProcessed.load(std::memory_order_acquire)
                    >= externalWatchdogCancelTargetCount)
    {
        clearWatchdogFallback();
    }

    if (expiredLiveTouches > 0)
    {
        // Cumulative publication/processing counts preserve a 256-source
        // watchdog burst across the 64-event per-block realtime budget. The
        // first timestamp is retained so repeated expiries cannot defer the
        // fail-safe forever; queue depth makes the grace period block-aware.
        if (! externalWatchdogCancelPending)
        {
            externalWatchdogCancelSinceMs = now;
            externalWatchdogCancelDrainBlocks = 1;
            externalWatchdogResetAckGeneration = resetAckGeneration;
        }
        externalWatchdogCancelPending = true;
        externalWatchdogCancelTargetCount =
            watchdogCancelsPublished.fetch_add(
                static_cast<std::uint64_t>(expiredLiveTouches),
                std::memory_order_acq_rel)
            + static_cast<std::uint64_t>(expiredLiveTouches);
        const auto queueDepth = static_cast<std::uint32_t>(juce::jmax(
            0, fingerRouter.getLifecycleQueueDepth()));
        const auto drainBlocks = 1u
            + (queueDepth + static_cast<std::uint32_t>(midiLifecycleEventBudget)
                    - 1u)
                / static_cast<std::uint32_t>(midiLifecycleEventBudget);
        externalWatchdogCancelDrainBlocks = juce::jmax(
            externalWatchdogCancelDrainBlocks, drainBlocks);
    }

    if (externalWatchdogCancelPending
        && watchdogCancelsProcessed.load(std::memory_order_acquire)
            >= externalWatchdogCancelTargetCount)
    {
        clearWatchdogFallback();
    }
    else if (externalWatchdogCancelPending)
    {
        const auto latestBlockMs = latestAudioBlockDurationMs.load(
            std::memory_order_relaxed);
        const auto blockAwareStallMs = static_cast<std::uint32_t>(
            juce::jmin<std::uint64_t>(60000u,
                static_cast<std::uint64_t>(latestBlockMs)
                    * externalWatchdogCancelDrainBlocks
                    + externalWatchdogTimerJitterMs));
        const auto stallThresholdMs = juce::jmax(
            externalWatchdogAudioStallMs, blockAwareStallMs);
        if (static_cast<std::uint32_t>(
                now - externalWatchdogCancelSinceMs) >= stallThresholdMs)
        {
            // With a running host the per-voice Cancel reaches the duration
            // scheduler on the next block. If the host has stopped calling us,
            // however, an external CoreMIDI endpoint can still be sustaining
            // the last packet it received. The independent high-resolution
            // sender therefore performs one bounded 16-channel sweep. Host-only
            // output needs no fallback because the downstream host graph is
            // stopped too.
            const bool externalEndpointOpen =
                midiOutputReady.load(std::memory_order_acquire)
                && midiOutputOptionIndex.load(std::memory_order_relaxed) > 0;
            if (externalEndpointOpen && ! externalWatchdogResetRequested)
            {
                requestExternalMidiReset(externalMidiPanicPending);
                externalWatchdogResetRequested = true;
            }
            else if (! externalEndpointOpen)
            {
                // No external endpoint can be sustaining this batch. Host MIDI
                // will consume the queued Cancels when its graph resumes. Do
                // not forge an audio-side processed count: the same queued
                // Cancels must not pre-acknowledge a later watchdog batch.
                clearWatchdogFallback();
            }
        }
    }
}

bool AudienceProcessor::isExternalMidiResetPending() const noexcept
{
    return externalMidiPanicPending.load(std::memory_order_acquire)
        || externalTransportResetPending.load(std::memory_order_acquire)
        || externalMidiProducerQuarantined.load(std::memory_order_acquire)
        || externalMidiResetRequestGeneration.load(std::memory_order_acquire)
            != externalMidiResetAcknowledgedGeneration.load(
                std::memory_order_acquire);
}

void AudienceProcessor::acknowledgeExternalMidiReset() noexcept
{
    const auto requested = externalMidiResetRequestGeneration.load(
        std::memory_order_acquire);
    externalMidiResetAcknowledgedGeneration.store(requested,
                                                   std::memory_order_release);
    // A newer request racing this store remains closed by the generation
    // mismatch even if it has not yet republished the boolean quarantine.
    externalMidiProducerQuarantined.store(false, std::memory_order_release);
}

void AudienceProcessor::drainExternalMidiOutputQueue()
{
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    for (;;)
    {
        // releaseResources() only publishes a reset because hosts may call it
        // from a processing-owned thread. A high-resolution sender callback
        // may already have passed its top-level flag exchange at that exact
        // moment. Recheck the producer gate before every dequeue/send so no
        // old-epoch NoteOn can drain ahead of the next ordered reset sweep.
        if (isExternalMidiResetPending()
            || externalMidiPanicPending.load(std::memory_order_acquire)
            || externalTransportResetPending.load(std::memory_order_acquire))
            return;

        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        externalMidiFifo.prepareToRead(1, start1, size1, start2, size2);
        if (size1 <= 0 && size2 <= 0)
        {
            externalFifoOldestAgeSeconds.store(0.0, std::memory_order_relaxed);
            return;
        }

        const int slotIndex = size1 > 0 ? start1 : start2;
        const auto& event = externalMidiEvents[(size_t) slotIndex];
        const auto acceptedGeneration =
            externalMidiResetAcknowledgedGeneration.load(std::memory_order_acquire);
        if (event.resetGeneration != acceptedGeneration)
        {
            // A producer reservation may finish just after a reset sweep. Its
            // generation stamp makes that old NoteOn harmless and prevents it
            // from blocking newer due-time events behind it.
            externalMidiFifo.finishedRead(1);
            continue;
        }
        externalFifoOldestAgeSeconds.store(
            std::isfinite(event.dueTimeMs)
                ? juce::jmax(0.0, (nowMs - event.dueTimeMs) * 0.001)
                : 86400.0,
            std::memory_order_relaxed);
        if (event.dueTimeMs > nowMs)
            return;

        // Close the narrower race where a transport/reset boundary is
        // published after prepareToRead() but before CoreMIDI is called. The
        // event remains in the FIFO and is discarded by the reset branch.
        if (isExternalMidiResetPending()
            || externalMidiPanicPending.load(std::memory_order_acquire)
            || externalTransportResetPending.load(std::memory_order_acquire))
            return;

        if (midiOutput != nullptr && event.size > 0)
            midiOutput->sendMessageNow(juce::MidiMessage(event.data, (int) event.size));
        externalMidiFifo.finishedRead(1);
    }
}

void AudienceProcessor::discardExternalMidiOutputQueue() noexcept
{
    const int available = externalMidiFifo.getNumReady();
    if (available <= 0)
    {
        externalFifoOldestAgeSeconds.store(0.0, std::memory_order_relaxed);
        return;
    }

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    externalMidiFifo.prepareToRead(available, start1, size1, start2, size2);
    externalMidiFifo.finishedRead(size1 + size2);
    externalFifoOldestAgeSeconds.store(0.0, std::memory_order_relaxed);
}

void AudienceProcessor::sendExternalResetSweep()
{
    if (midiOutput == nullptr)
        return;

    for (int channel = 1; channel <= 16; ++channel)
    {
        midiOutput->sendMessageNow(juce::MidiMessage::allNotesOff(channel));
        midiOutput->sendMessageNow(juce::MidiMessage::allSoundOff(channel));
    }
}

void AudienceProcessor::unregisterGlobalConductor() noexcept
{
    if (conductorHandle.isValid())
        GlobalConductorHub::shared().unregisterInstance(conductorHandle);
    conductorHandle = {};
    conductorRegisteredPort = 0;
    conductorRegisteredRoleChoice = -1;
    conductorRegisteredGroup = -1;
    conductorRegisteredZoneChoice = -1;
    conductorAudioPolicy.store(packConductorPolicy(
        GlobalConductorHub::SnapshotSource::LocalFallback,
        governorEffectiveAttacks.load(std::memory_order_relaxed),
        governorEffectiveActive.load(std::memory_order_relaxed)),
        std::memory_order_release);
    conductorActiveZones.store(0, std::memory_order_relaxed);
    conductorLeaderPort.store(0, std::memory_order_relaxed);
}

void AudienceProcessor::deactivateExternalMidiForRestoreLocked()
{
    if (midiOutput == nullptr
        && ! midiOutputReady.load (std::memory_order_acquire))
        return;

    // State restore is already an allocation-heavy host control callback, not
    // an audio callback. Publish the producer quarantine before joining the
    // sender so an in-flight audio block cannot enqueue onto the retired
    // endpoint after this boundary.
    requestExternalMidiReset (externalMidiPanicPending);
    juce::HighResolutionTimer::stopTimer();
    sendImmediateExternalAllNotesOffOnly();
    midiOutputRouteRevision.fetch_add (1, std::memory_order_release);
    midiOutput.reset();
    midiOutputReady.store (false, std::memory_order_release);
    midiOutputRouteChangedPending.store (true, std::memory_order_release);

    // Keep midiOutputOptionIndex / route kind / stable identifier untouched.
    // They describe logical saved state; the complete pending generation will
    // replace them when its message-thread route is applied.
}

bool AudienceProcessor::deactivatePendingOscForRestoreLocked()
{
    const auto generation = pendingOscDeactivationGeneration;
    if (generation == 0)
        return false;

    const auto currentGeneration = stateRestoreGeneration.load (
        std::memory_order_acquire);
    if (generation != currentGeneration
        || stateRouteReadyGeneration.load (std::memory_order_acquire)
            == generation)
    {
        pendingOscDeactivationGeneration = 0;
        return false;
    }

    // OscBridge exposes message-thread UI state as well as owning the receiver.
    // Its actual stop therefore happens only here/on the Timer message thread,
    // never on a worker that happened to call setStateInformation.
    osc.stop();
    oscStatus = "VALID RESTORE PENDING / ROUTE FAIL-CLOSED";
    pendingOscDeactivationGeneration = 0;
    return true;
}

void AudienceProcessor::refreshGlobalConductor()
{
    const int roleChoice = juce::jlimit(
        0, 2, rawParamInt(
            rawParams.conductorRole,
            CosmicFactoryPresets::zonePreset(0).conductorRoleChoice));
    const int group = juce::jlimit(
        0, 3, rawParamInt(rawParams.conductorGroup,
                          CosmicFactoryPresets::conductorGroup));
    const int port = getUdpPort();
    const int zoneChoice = juce::jlimit(
        0, 26, rawParamInt(
            rawParams.expectedZone,
            CosmicFactoryPresets::zonePreset(0).expectedZoneChoice));

    // Conductor identity is part of the same venue route as OSC. A restored
    // duplicate port may retain its exact saved settings, but it must not join
    // the global allocation hub until it actually owns and receives that UDP
    // endpoint with the matching zone filter.
    if (! osc.isReceiving()
        || osc.getCurrentPort() != port
        || osc.getExpectedZone() != zoneChoice - 1)
    {
        unregisterGlobalConductor();
        conductorRegistrationStatus.store (
            static_cast<int> (
                GlobalConductorHub::RegistrationStatus::InvalidPort),
            std::memory_order_relaxed);
        return;
    }

    if (conductorHandle.isValid()
        && conductorRegisteredPort == port
        && conductorRegisteredRoleChoice == roleChoice
        && conductorRegisteredGroup == group
        && conductorRegisteredZoneChoice == zoneChoice)
        return;

    unregisterGlobalConductor();
    const auto role = roleChoice == 1 ? GlobalConductorHub::Role::Leader
                    : roleChoice == 2 ? GlobalConductorHub::Role::Follower
                                      : GlobalConductorHub::Role::Off;
    GlobalConductorHub::Registration registration;
    registration.udpPort = port;
    registration.zoneKey = static_cast<std::uint32_t>(zoneChoice);
    registration.role = role;
    registration.groupKey = static_cast<std::uint32_t>(group + 1);
    const auto result = GlobalConductorHub::shared().registerInstance(registration);
    conductorRegistrationStatus.store((int) result.status,
                                      std::memory_order_relaxed);
    if (result.status == GlobalConductorHub::RegistrationStatus::Registered)
    {
        conductorHandle = result.handle;
        conductorRegisteredPort = port;
        conductorRegisteredRoleChoice = roleChoice;
        conductorRegisteredGroup = group;
        conductorRegisteredZoneChoice = zoneChoice;
    }
}

bool AudienceProcessor::freshRouteAssignmentIsEligibleLocked (
    bool requireUnclaimedOsc)
{
    if (! freshRouteAssignmentEligible
        || stateMutationOwnerActive
        || routeSnapshotApplyInProgress
        || stateRestoreGeneration.load (std::memory_order_acquire) != 1u
        || stateRestoreCompletedGeneration.load (std::memory_order_acquire) != 1u
        || stateRouteReadyGeneration.load (std::memory_order_acquire) != 0u
        || (requireUnclaimedOsc && osc.isRunning())
        || getUdpPort() != CosmicFactoryPresets::firstUdpPort
        || rawParamInt (rawParams.expectedZone, -1)
            != CosmicFactoryPresets::zonePreset (0).expectedZoneChoice
        || ! rawParamBool (rawParams.exclusiveUdpPort, false)
        || rawParamInt (rawParams.conductorRole, -1)
            != CosmicFactoryPresets::zonePreset (0).conductorRoleChoice
        || rawParamInt (rawParams.midiOutputPath, -1)
            != CosmicFactoryPresets::midiOutputPath
        || midiOutputOptionIndex.load (std::memory_order_relaxed)
            != CosmicFactoryPresets::midiOutputOption)
        return false;

    const juce::ScopedLock pendingLock (pendingStateLock);
    return pendingStateApply.load (std::memory_order_acquire)
        && pendingStateGeneration == 1u
        && pendingUdpPort == CosmicFactoryPresets::firstUdpPort
        && pendingMidiOutputOption == CosmicFactoryPresets::midiOutputOption
        && pendingMidiOutputRouteKind == CosmicFactoryPresets::midiOutputRouteKind
        && pendingMidiOutputDeviceIdentifier.isEmpty();
}

void AudienceProcessor::disableFreshRouteAssignmentLocked() noexcept
{
    freshRouteAssignmentEligible = false;
    freshRouteAssignmentState.store (
        static_cast<int> (FreshRouteAssignmentState::preserved),
        std::memory_order_release);
}

bool AudienceProcessor::handleFreshRouteAssignment()
{
    auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    jassert (manager != nullptr && manager->isThisTheMessageThread());
    if (manager == nullptr || ! manager->isThisTheMessageThread())
        return false;

    // setStateInformation publishes this intent before it starts parsing. Do
    // not let the default Zone A snapshot escape while a worker may still be
    // decoding the authoritative saved route.
    if (hostStateRestoreIntentCount.load (std::memory_order_acquire) != 0u)
        return true;

    const auto now = juce::Time::getMillisecondCounter();
    std::uint32_t expectedGeneration = 0;
    int claimedPresetIndex = -1;
    bool assignmentPublished = false;
    bool stateBuildFailed = false;
    std::uint32_t assignmentGeneration = 0;
    {
        const juce::ScopedLock transactionLock (stateTransactionLock);
        if (hostStateRestoreIntentCount.load (std::memory_order_acquire) != 0u)
            return true;

        if (! freshRouteAssignmentEligible)
            return false;

        if (! freshRouteAssignmentIsEligibleLocked())
        {
            disableFreshRouteAssignmentLocked();
            return false;
        }

        // Exhaustion is deliberately stable. A user can free a route and use
        // RETRY AUTO; dozens of overflow instances must not rescan eight ports
        // every second on Ableton's message thread.
        if (getFreshRouteAssignmentState()
            == FreshRouteAssignmentState::exhausted)
            return true;

        expectedGeneration = stateRestoreGeneration.load (
            std::memory_order_acquire);
        lastFreshRouteAssignmentAttemptMs = now;
        freshRouteAssignmentState.store (
            static_cast<int> (FreshRouteAssignmentState::pending),
            std::memory_order_release);

        // Keep the transaction lock for the short A-H bind scan. This makes a
        // concurrent getStateInformation snapshot observe either the complete
        // pre-assignment state or the immutable assigned snapshot, never the
        // old Zone A tree paired with a newly retained B-H OSC claim.
        for (int index = 0; index < CosmicFactoryPresets::count; ++index)
        {
            const auto candidate = CosmicFactoryPresets::zonePreset (index);
            osc.setExpectedZone (candidate.expectedZoneChoice - 1);
            const bool started = osc.start (
                candidate.udpPort, OscBridge::PortPolicy::exclusive);
            const bool retainedExclusiveClaim = started
                                             && osc.isRunning()
                                             && osc.isReceiving()
                                             && osc.isExclusive()
                                             && osc.getCurrentPort()
                                                    == candidate.udpPort;
            if (retainedExclusiveClaim)
            {
                claimedPresetIndex = index;
                break;
            }
        }

        if (claimedPresetIndex < 0)
        {
            // A failed in-process exclusive registration can still leave an
            // unregistered SharedPort handle behind. Release it and restore
            // the canonical pending Zone A identity for coherent diagnostics.
            osc.stop();
            osc.setExpectedZone (
                rawParamInt (rawParams.expectedZone,
                             CosmicFactoryPresets::zonePreset (0)
                                 .expectedZoneChoice) - 1);
            freshRouteAssignmentState.store (
                static_cast<int> (FreshRouteAssignmentState::exhausted),
                std::memory_order_release);
            oscStatus = "NO FREE UDP PORT / "
                      + juce::String (CosmicFactoryPresets::firstUdpPort)
                      + "-" + juce::String (CosmicFactoryPresets::lastUdpPort)
                      + " / FAIL-CLOSED";
            return true;
        }

        const auto preset = CosmicFactoryPresets::zonePreset (
            claimedPresetIndex);
        if (freshRouteAssignmentEligible
            && hostStateRestoreIntentCount.load (std::memory_order_acquire) == 0u
            && stateRestoreGeneration.load (std::memory_order_acquire)
                == expectedGeneration
            && freshRouteAssignmentIsEligibleLocked (false))
        {
            // Become the sole APVTS mutation owner before taking the snapshot.
            // A concurrent worker restore can then publish only a newer queued
            // generation and will win when drainPendingApvtsStateQueue runs.
            juce::HighResolutionTimer::stopTimer();
            stateMutationOwnerActive = true;

            auto assignedState = apvts.copyState();
            auto expectedZoneNode = CosmicStateMigration::findParameterNode (
                assignedState, "expectedZone");
            auto conductorRoleNode = CosmicStateMigration::findParameterNode (
                assignedState, "conductorRole");

            if (expectedZoneNode.isValid() && conductorRoleNode.isValid())
            {
                expectedZoneNode.setProperty (
                    "value", (float) preset.expectedZoneChoice, nullptr);
                conductorRoleNode.setProperty (
                    "value", (float) preset.conductorRoleChoice, nullptr);
                assignedState.setProperty ("udpPort", preset.udpPort, nullptr);
                assignedState.setProperty (
                    "midiOutputOption", CosmicFactoryPresets::midiOutputOption,
                    nullptr);
                assignedState.setProperty (
                    "midiOutputRouteKind",
                    CosmicFactoryPresets::midiOutputRouteKind, nullptr);
                assignedState.setProperty (
                    "midiOutputDeviceIdentifier", {}, nullptr);
                assignedState.setProperty (
                    "cosmicMicrowaveSchema",
                    CosmicStateMigration::currentSchema, nullptr);

                assignmentGeneration =
                    stateRestoreGeneration.fetch_add (
                        1, std::memory_order_acq_rel) + 1;
                pendingRuntimeResetGeneration = assignmentGeneration;
                pendingFactoryRuntimeGeneration = 0;
                pendingHostMidiResetGeneration.store (
                    assignmentGeneration, std::memory_order_release);
                {
                    const juce::ScopedLock pendingLock (pendingStateLock);
                    pendingStateApply.store (true, std::memory_order_release);
                    pendingStateGeneration = assignmentGeneration;
                    pendingUdpPort = preset.udpPort;
                    pendingMidiOutputOption =
                        CosmicFactoryPresets::midiOutputOption;
                    pendingMidiOutputRouteKind =
                        CosmicFactoryPresets::midiOutputRouteKind;
                    pendingMidiOutputDeviceIdentifier.clear();
                }
                stateSnapshotOverride = assignedState.createCopy();
                stateSnapshotOverrideInProgress = true;
                stateSnapshotOverrideGeneration = assignmentGeneration;
                pendingApvtsState = assignedState.createCopy();
                pendingApvtsGeneration = assignmentGeneration;

                // Keep the winning OSC claim alive. Once APVTS publishes the
                // matching zone, setUdpPortInternal takes its same-port fast
                // path and never creates a probe/release race.
                udpPort.store (preset.udpPort, std::memory_order_relaxed);
                lastExclusiveUdpPort = true;
                lastExpectedZoneChoice = preset.expectedZoneChoice;
                lastOscRetryMs = now;
                oscStatus = osc.oscStatus();
                freshRouteAssignmentEligible = false;
                freshRouteAssignmentState.store (
                    static_cast<int> (FreshRouteAssignmentState::assigned),
                    std::memory_order_release);
                assignmentPublished = true;
            }
            else
            {
                stateMutationOwnerActive = false;
                freshRouteAssignmentState.store (
                    static_cast<int> (FreshRouteAssignmentState::exhausted),
                    std::memory_order_release);
                oscStatus = "AUTO ASSIGN STATE ERROR / FAIL-CLOSED";
                stateBuildFailed = true;
            }
        }
    }

    if (! assignmentPublished)
    {
        osc.stop();
        osc.setExpectedZone (
            rawParamInt (rawParams.expectedZone,
                         CosmicFactoryPresets::zonePreset (0)
                             .expectedZoneChoice) - 1);
        return stateBuildFailed
            || hostStateRestoreIntentCount.load (
                   std::memory_order_acquire) != 0u;
    }

    // APVTS replacement and its host/listener notifications must never happen
    // while stateTransactionLock is held. The existing latest-wins drain also
    // guarantees that a reentrant saved-state restore supersedes this choice.
    drainPendingApvtsStateQueue();
    requestTimeFieldRehydrate();
    midiOutputRouteChangedPending.store (true, std::memory_order_release);

    // Intent alone is deliberately non-destructive: an invalid blob must not
    // release this retained claim and let a sibling steal its route. A valid
    // restore publishes a newer generation and a matching deactivation token.
    const bool restorePending = hostStateRestoreIntentCount.load (
        std::memory_order_acquire) != 0u;
    if (stateRestoreGeneration.load (std::memory_order_acquire)
            != assignmentGeneration)
    {
        const juce::ScopedLock transactionLock (stateTransactionLock);
        deactivatePendingOscForRestoreLocked();
    }
    if (restorePending)
        return true;
    return false;
}

void AudienceProcessor::retryFreshRouteAssignment()
{
    auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    jassert (manager != nullptr && manager->isThisTheMessageThread());
    if (manager == nullptr || ! manager->isThisTheMessageThread())
        return;

    bool shouldRetry = false;
    {
        const juce::ScopedLock transactionLock (stateTransactionLock);
        shouldRetry = freshRouteAssignmentEligible
                   && getFreshRouteAssignmentState()
                        == FreshRouteAssignmentState::exhausted;
        if (shouldRetry)
        {
            lastFreshRouteAssignmentAttemptMs = 0;
            freshRouteAssignmentState.store (
                static_cast<int> (FreshRouteAssignmentState::pending),
                std::memory_order_release);
        }
    }

    if (shouldRetry)
        timerCallback();
}

bool AudienceProcessor::applyPendingRouteSnapshotLocked()
{
    // The APVTS owner may have completed one tree but still need to inspect its
    // latest-wins queue. Do not expose that intermediate generation's route.
    if (stateMutationOwnerActive)
        return false;

    int restoredPort = CosmicFactoryPresets::firstUdpPort;
    int restoredOutput = CosmicFactoryPresets::midiOutputOption;
    int restoredRouteKind = CosmicFactoryPresets::midiOutputRouteKind;
    std::uint32_t restoredGeneration = 0;
    juce::String restoredDeviceIdentifier;
    bool applyState = false;
    {
        const juce::ScopedLock lock(pendingStateLock);
        const auto currentGeneration = stateRestoreGeneration.load(
            std::memory_order_acquire);
        const auto completedGeneration = stateRestoreCompletedGeneration.load(
            std::memory_order_acquire);
        applyState = pendingStateApply.load(std::memory_order_acquire)
                  && pendingStateGeneration == currentGeneration
                  && completedGeneration == currentGeneration;
        if (applyState)
        {
            pendingStateApply.store(false, std::memory_order_release);
            restoredPort = pendingUdpPort;
            restoredOutput = pendingMidiOutputOption;
            restoredRouteKind = pendingMidiOutputRouteKind;
            restoredGeneration = pendingStateGeneration;
            restoredDeviceIdentifier = pendingMidiOutputDeviceIdentifier;
        }
    }

    if (! applyState)
        return false;

    // The full model reset is generation-tagged and consumed under the same
    // transaction lock immediately before the matching route is opened. This
    // prevents a timer/manual-route race from publishing Route Ready first.
    performPendingRuntimeResetLocked(restoredGeneration);
    routeSnapshotApplyInProgress = true;
    setUdpPortInternal(restoredPort, false);
    restoreMidiOutputRoute(restoredRouteKind, restoredDeviceIdentifier,
                           restoredOutput);
    routeSnapshotApplyInProgress = false;
    if (stateRestoreGeneration.load(std::memory_order_acquire)
            == restoredGeneration
        && stateRestoreCompletedGeneration.load(std::memory_order_acquire)
            == restoredGeneration)
    {
        stateRouteReadyGeneration.store(restoredGeneration,
                                        std::memory_order_release);
        if (pendingOscDeactivationGeneration == restoredGeneration)
            pendingOscDeactivationGeneration = 0;
        if (stateSnapshotOverrideInProgress
            && stateSnapshotOverrideGeneration == restoredGeneration)
        {
            stateSnapshotOverrideInProgress = false;
            stateSnapshotOverrideGeneration = 0;
            stateSnapshotOverride = {};
        }
    }
    return true;
}

void AudienceProcessor::handleAsyncUpdate()
{
    auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    jassert (manager != nullptr && manager->isThisTheMessageThread());
    if (manager == nullptr || ! manager->isThisTheMessageThread())
        return;

    {
        const juce::ScopedLock transactionLock (stateTransactionLock);
        deactivatePendingOscForRestoreLocked();
    }

    // The APVTS owner may still be finishing on a worker, in which case this
    // call only leaves the old route fail-closed. setStateInformation posts a
    // second update after completion so the final route is applied promptly.
    timerCallback();
}

SourceQualityController::ExternalCounters
AudienceProcessor::getSourceQualityExternalCounters() const noexcept
{
    return {
        audienceModel.getCapacityDroppedEventCount(),
        fingerRouter.getDroppedMotionEventCount(),
        fingerRouter.getDroppedLifecycleEventCount()
    };
}

void AudienceProcessor::startSourceQualityCheck()
{
    const auto now = juce::Time::getMillisecondCounter();
    sourceQualityController.arm(now, getSourceQualityExternalCounters());
    sourceQualityAdmissionOpen.store(false, std::memory_order_release);
    sourceQualityUpdateClockInitialised = false;
    updateSourceQualityController(now);
}

void AudienceProcessor::stopSourceQualityCheck()
{
    sourceQualityController.disarm();
    sourceQualityAdmissionOpen.store(true, std::memory_order_release);
    sourceQualityUpdateClockInitialised = false;
}

void AudienceProcessor::restartSourceQualityEpoch (std::uint32_t nowMs) noexcept
{
    sourceQualityController.restartEpoch(nowMs,
                                         getSourceQualityExternalCounters());
    const auto output = sourceQualityController.getOutput();
    sourceQualityAdmissionOpen.store(output.admissionOpen,
                                     std::memory_order_release);
    sourceQualityUpdateClockInitialised = false;
}

void AudienceProcessor::updateSourceQualityController (
    std::uint32_t nowMs) noexcept
{
    constexpr std::uint32_t updatePeriodMs = 100u;
    const auto elapsed = static_cast<std::uint32_t>(
        nowMs - sourceQualityLastUpdateMs);
    const bool clockInvalid = sourceQualityUpdateClockInitialised
                           && elapsed >= 0x80000000u;
    if (sourceQualityUpdateClockInitialised && ! clockInvalid
        && elapsed < updatePeriodMs)
        return;

    SourceQualityController::Config config;
    config.expectedSources = audienceModel.getSourceCapacity();
    config.simulatorActive = simulator.getSimSeatCount() > 0;
    const auto output = sourceQualityController.update(
        config, getSourceQualityExternalCounters(), nowMs);
    sourceQualityAdmissionOpen.store(output.admissionOpen,
                                     std::memory_order_release);
    sourceQualityLastUpdateMs = nowMs;
    sourceQualityUpdateClockInitialised = true;
}

void AudienceProcessor::timerCallback()
{
    const std::uint32_t now = juce::Time::getMillisecondCounter();

    // A valid worker restore posts an AsyncUpdater handoff. This message-timer
    // check is the fallback for hosts that coalesce or delay async messages;
    // an unparsed/invalid restore intent never owns a deactivation token.
    {
        const juce::ScopedLock transactionLock (stateTransactionLock);
        deactivatePendingOscForRestoreLocked();
    }

    const int requestedSourceCapacity = sourceCapacityFromChoice(rawParamInt(
        rawParams.sourceCapacity, CosmicFactoryPresets::sourceCapacity));
    const int previousSourceCapacity = audienceModel.getSourceCapacity();
    if (requestedSourceCapacity != previousSourceCapacity)
    {
        // These APIs take the control-producer lock and can retire hundreds of
        // canonical slots, so they must never run in processBlock. Shrink lets
        // the simulator publish its ordered Offs while the old admission range
        // is still valid; expansion opens the model before the simulator.
        if (requestedSourceCapacity < previousSourceCapacity)
        {
            simulator.setSourceCapacity(requestedSourceCapacity);
            audienceModel.setSourceCapacity(requestedSourceCapacity);
        }
        else
        {
            audienceModel.setSourceCapacity(requestedSourceCapacity);
            simulator.setSourceCapacity(requestedSourceCapacity);
        }

        // The next realtime boundary emits one bounded channel reset and
        // rebuilds Time Field state only from the still-admitted canonical
        // sources. This also cancels fixed-duration tails owned by retired IDs.
        midiOutputRouteChangedPending.store(true, std::memory_order_release);
        restartSourceQualityEpoch(now);
    }

    // Processor-owned timer runs for the full plug-in lifetime, including when
    // the editor is closed. Three missing 1 Hz phone heartbeats synthesize one
    // ordered Cancel, preventing a disconnected client from holding a tail.
    // Expiry publishes one bounded Cancel lifecycle per stale source/finger.
    // The audio scheduler releases only that semantic voice immediately; all
    // other crowd notes retain their selected musical duration.
    const int expiredLiveTouches = audienceModel.expireStaleLiveTouches();
    serviceExternalWatchdogFallback(now, expiredLiveTouches);
    governorRecentSourceCount.store(
        audienceModel.getRecentLiveSourceCount(), std::memory_order_release);
    updateSourceQualityController(now);

    // A fresh instance claims one complete A-H route before the ordinary
    // pending snapshot is allowed to open Zone A. Exhaustion deliberately
    // returns early so no fallback receiver, virtual endpoint or Conductor
    // registration can escape the generation quarantine.
    if (handleFreshRouteAssignment())
        return;

    // Close the tiny race where restore intent is published immediately after
    // handleFreshRouteAssignment's final check but before the ordinary route
    // snapshot phase. Intent quarantines publication but is non-destructive;
    // only a parsed valid generation receives the deactivation token handled
    // above.
    if (hostStateRestoreIntentCount.load (std::memory_order_acquire) != 0u)
        return;

    // Apply one complete restored routing snapshot before deriving OSC or
    // Conductor identity. Otherwise a restored role/group could be registered
    // for one timer tick against the previous UDP port.
    const juce::ScopedLock transactionLock(stateTransactionLock);
    if (hostStateRestoreIntentCount.load (std::memory_order_acquire) != 0u)
        return;
    applyPendingRouteSnapshotLocked();

    // A host state arriving after the route transaction keeps the generation
    // quarantined. Keep this check, route maintenance and Global Conductor
    // publication under one transaction lock so a stale timer tick can never
    // overwrite the pending port/zone snapshot of a newer host generation.
    if (stateRestoreCompletedGeneration.load(std::memory_order_acquire)
            != stateRestoreGeneration.load(std::memory_order_acquire)
        || stateRouteReadyGeneration.load(std::memory_order_acquire)
        != stateRestoreGeneration.load(std::memory_order_acquire))
        return;

    const int expectedZoneChoice = juce::jlimit(
        0, 26, rawParamInt(
            rawParams.expectedZone,
            CosmicFactoryPresets::zonePreset(0).expectedZoneChoice));
    const bool exclusivePort = rawParamBool(
        rawParams.exclusiveUdpPort, CosmicFactoryPresets::exclusiveUdpPort != 0);
    maintainPhysicalMidiOutputRouteLocked(now);
    maintainVirtualMidiOutputRouteLocked(now);
    const bool retryDue = ! osc.isReceiving()
                       && static_cast<std::uint32_t>(now - lastOscRetryMs) >= 1000u;
    if (expectedZoneChoice != lastExpectedZoneChoice
        || exclusivePort != lastExclusiveUdpPort || retryDue)
    {
        // This is route maintenance, not a user edit. Keep fresh-assignment
        // provenance intact while applying APVTS policy/zone automation or a
        // bounded ownership retry on the message thread.
        ScopedProcessorSuspension processingGuard (
            *this, controlAudioMutationGate, audioCallbacksInFlight);
        setUdpPortInternal (getUdpPort());
        commitExplicitRouteEditLocked();
    }

    refreshGlobalConductor();
    if (conductorHandle.isValid())
    {
        auto& hub = GlobalConductorHub::shared();
        hub.publishZoneDensity(conductorHandle,
                               governorObservedDensity.load(std::memory_order_relaxed),
                               now);
        if (conductorRegisteredRoleChoice == 1)
        {
            hub.publishLeaderBudgets(
                conductorHandle,
                juce::jlimit(1, 64,
                    rawParamInt(rawParams.conductorAttackBudget,
                                CosmicFactoryPresets::conductorAttackBudget)),
                juce::jlimit(1, 128,
                    rawParamInt(rawParams.conductorVoiceBudget,
                                CosmicFactoryPresets::conductorVoiceBudget)),
                now);
            hub.allocationTick(conductorHandle, now);
        }

        GlobalConductorHub::LocalPolicy local;
        local.attackQuota = governorEffectiveAttacks.load(std::memory_order_relaxed);
        local.voiceQuota = governorEffectiveActive.load(std::memory_order_relaxed);
        const auto snapshot = hub.readAudioSnapshot(conductorHandle, now, local);
        conductorActiveZones.store(snapshot.activeZoneCount, std::memory_order_relaxed);
        conductorLeaderPort.store(snapshot.leaderUdpPort, std::memory_order_relaxed);
        conductorAudioPolicy.store(packConductorPolicy(
            snapshot.source, snapshot.attackQuota, snapshot.voiceQuota),
            std::memory_order_release);
    }
}

void AudienceProcessor::maintainPhysicalMidiOutputRouteLocked (
    std::uint32_t nowMs)
{
    // CoreMIDI enumeration and endpoint open/close stay on the message timer.
    // One scan per second is responsive enough for show recovery without
    // polling the OS at the processor timer's 60 Hz rate.
    if (static_cast<std::uint32_t>(nowMs - lastMidiHotplugScanMs) < 1000u)
        return;
    lastMidiHotplugScanMs = nowMs;

    int routeKind = 0;
    int legacyOption = 0;
    juce::String deviceIdentifier;
    {
        const juce::ScopedLock lock(pendingStateLock);
        routeKind = midiOutputRouteKind;
        legacyOption = midiOutputOptionIndex.load(std::memory_order_relaxed);
        deviceIdentifier = midiOutputDeviceIdentifier;
    }

    if (routeKind != 2 || deviceIdentifier.isEmpty())
        return;

    const auto devices = juce::MidiOutput::getAvailableDevices();
    int resolvedIndex = -1;
    for (int index = 0; index < devices.size(); ++index)
    {
        if (devices[index].identifier == deviceIdentifier)
        {
            resolvedIndex = index;
            break;
        }
    }

    const bool ready = midiOutputReady.load(std::memory_order_acquire)
                    && midiOutput != nullptr;
    if (resolvedIndex >= 0)
    {
        const int resolvedOption = resolvedIndex + 2;
        if (! ready)
        {
            // The stable identifier, not the old list index, chooses the
            // endpoint. Failed opens remain fail-closed and are retried on the
            // next bounded scan without falling back to Host output.
            restoreMidiOutputRoute(2, deviceIdentifier, resolvedOption);
            commitExplicitRouteEditLocked();
        }
        else if (legacyOption != resolvedOption)
        {
            // Hotplug can reorder CoreMIDI's numeric list. Keep the legacy
            // compatibility field current without reopening the live route.
            midiOutputOptionIndex.store(resolvedOption,
                                        std::memory_order_relaxed);
            midiOutputRouteRevision.fetch_add(1, std::memory_order_release);
            commitExplicitRouteEditLocked();
        }
        return;
    }

    if (ready)
    {
        // The selected destination disappeared. Close it immediately, but
        // restore its logical kind and identifier so project state remains
        // stable and the exact same endpoint can reconnect automatically.
        restoreMidiOutputRoute(2, deviceIdentifier,
                               juce::jmax(2, legacyOption));
        commitExplicitRouteEditLocked();
    }
}

void AudienceProcessor::maintainVirtualMidiOutputRouteLocked (
    std::uint32_t nowMs)
{
    if (! osc.isReceiving()
        || midiOutputReady.load (std::memory_order_acquire)
        || midiOutput != nullptr
        || static_cast<std::uint32_t> (nowMs - lastVirtualMidiRetryMs) < 1000u)
        return;

    int routeKind = 0;
    {
        const juce::ScopedLock lock (pendingStateLock);
        routeKind = midiOutputRouteKind;
    }
    if (routeKind != CosmicFactoryPresets::midiOutputRouteKind
        || midiOutputOptionIndex.load (std::memory_order_relaxed)
            != CosmicFactoryPresets::midiOutputOption)
        return;

    // CoreMIDI can release a just-destroyed virtual source slightly after OSC
    // ownership becomes free. Retry at 1 Hz and keep External Only fail-closed
    // until the exact "Cosmic Microwave <port> Out" endpoint exists.
    lastVirtualMidiRetryMs = nowMs;
    setMidiOutputOptionIndexInternal (CosmicFactoryPresets::midiOutputOption);
}

void AudienceProcessor::hiResTimerCallback()
{
    if (stateRestoreCompletedGeneration.load(std::memory_order_acquire)
            != stateRestoreGeneration.load(std::memory_order_acquire)
        || stateRouteReadyGeneration.load(std::memory_order_acquire)
            != stateRestoreGeneration.load(std::memory_order_acquire))
        return;

    const bool panicPending = externalMidiPanicPending.exchange(false, std::memory_order_acq_rel);
    const bool transportResetPending = externalTransportResetPending.exchange(false, std::memory_order_acq_rel);
    if (panicPending || transportResetPending || isExternalMidiResetPending())
    {
        // Panic/transport boundaries invalidate every queued packet, including
        // future-offset events. Discard before all-off so no pre-boundary NoteOn
        // can be emitted on a later timer tick after the reset.
        discardExternalMidiOutputQueue();
        sendExternalResetSweep();
        acknowledgeExternalMidiReset();
        // Publish the retrigger only after reopening the gate. Otherwise an
        // audio block could consume this flag while still quarantined, clear
        // the held-finger retrigger, and leave the reset endpoint silent.
        midiOutputRouteChangedPending.store(true, std::memory_order_release);
        return;
    }

    drainExternalMidiOutputQueue();
}

juce::StringArray AudienceProcessor::getMidiOutputOptions() const
{
    juce::StringArray options;
    options.add("Host MIDI Output");
    options.add("Virtual: " + getVirtualMidiPortName());
    for (const auto& device : juce::MidiOutput::getAvailableDevices())
        options.add(device.name);
    return options;
}

juce::String AudienceProcessor::getVirtualMidiPortName() const
{
    return "Cosmic Microwave " + juce::String(getUdpPort()) + " Out";
}

juce::String AudienceProcessor::getAtomicElementName (int index) const
{
    return juce::String::fromUTF8(AtomicScaleCatalog::elementName(index));
}

juce::String AudienceProcessor::getAtomicElementSymbol (int index) const
{
    return juce::String::fromUTF8(AtomicScaleCatalog::elementSymbol(index));
}

juce::String AudienceProcessor::getAtomicModeName (int index) const
{
    return juce::String::fromUTF8(AtomicScaleCatalog::modeName(index));
}

int AudienceProcessor::getSelectedAtomicDegreeCount() const noexcept
{
    const int element = rawParamInt(rawParams.spectralElement,
                                    CosmicFactoryPresets::spectralElement);
    const int mode = rawParamInt(rawParams.atomicScaleMode,
                                 CosmicFactoryPresets::atomicScaleMode);
    return AtomicScaleCatalog::instance().getDegreeCount(element, mode);
}

double AudienceProcessor::getSelectedAtomicReferenceWavelengthNm() const noexcept
{
    const int element = rawParamInt(rawParams.spectralElement,
                                    CosmicFactoryPresets::spectralElement);
    const int mode = rawParamInt(rawParams.atomicScaleMode,
                                 CosmicFactoryPresets::atomicScaleMode);
    return AtomicScaleCatalog::instance().getReferenceWavelengthNm(element, mode);
}

juce::String AudienceProcessor::getMidiOutputStatus() const
{
    return midiOutputStatus;
}

int AudienceProcessor::getMidiOutputPath() const noexcept
{
    return juce::jlimit(0, 2,
        rawParamInt(rawParams.midiOutputPath,
                    CosmicFactoryPresets::midiOutputPath));
}

int AudienceProcessor::getNumFactoryPresets() noexcept
{
    return CosmicFactoryPresets::count;
}

juce::String AudienceProcessor::getFactoryPresetName (int index)
{
    const auto preset = CosmicFactoryPresets::zonePreset(index);
    const auto zone = juce::String::charToString(
        static_cast<juce::juce_wchar>('A' + preset.index));
    return "ZONE " + zone + "  /  UDP " + juce::String(preset.udpPort)
         + (preset.index == 0 ? "  /  LEADER" : "  /  FOLLOWER");
}

void AudienceProcessor::drainPendingApvtsStateQueue()
{
    for (;;)
    {
        juce::ValueTree stateToApply;
        std::uint32_t generationToApply = 0;
        {
            const juce::ScopedLock lock(stateTransactionLock);
            jassert(stateMutationOwnerActive);
            if (! pendingApvtsState.isValid())
            {
                stateMutationOwnerActive = false;
                return;
            }

            stateToApply = pendingApvtsState;
            generationToApply = pendingApvtsGeneration;
            pendingApvtsState = {};
            pendingApvtsGeneration = 0;
        }

        // replaceState synchronously notifies APVTS/host listeners. Never hold
        // a state lock here: a listener may save state on another thread or
        // re-enter setStateInformation on this one. Reentrant restores only
        // publish a newer complete tree, which this outermost owner reapplies
        // after the older callback stack has fully unwound.
        apvts.replaceState(stateToApply);

        {
            const juce::ScopedLock lock(stateTransactionLock);
            if (stateRestoreGeneration.load(std::memory_order_acquire)
                == generationToApply)
            {
                stateRestoreCompletedGeneration.store(generationToApply,
                                                      std::memory_order_release);
                // Once APVTS is complete, subsequent automation must be saved
                // from the live tree. The still-pending route is preserved by
                // its separate fields until the message-thread phase is ready.
                if (stateSnapshotOverrideInProgress
                    && stateSnapshotOverrideGeneration == generationToApply)
                {
                    stateSnapshotOverrideInProgress = false;
                    stateSnapshotOverrideGeneration = 0;
                    stateSnapshotOverride = {};
                }
            }
        }
    }
}

void AudienceProcessor::applyFactoryPreset (int index)
{
    auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();
    jassert(messageManager != nullptr
            && messageManager->isThisTheMessageThread());
    if (messageManager == nullptr || ! messageManager->isThisTheMessageThread())
        return;

    {
        const juce::ScopedLock transactionLock (stateTransactionLock);
        disableFreshRouteAssignmentLocked();
    }

    const auto preset = CosmicFactoryPresets::zonePreset(index);
    struct ParameterSetting
    {
        const char* id;
        float value;
    };
    const std::array<ParameterSetting, 37> settings {{
        { "midiOutputType", (float) CosmicFactoryPresets::midiOutputType },
        { "midiOutputPath", (float) CosmicFactoryPresets::midiOutputPath },
        { "expectedZone", (float) preset.expectedZoneChoice },
        { "exclusiveUdpPort", (float) CosmicFactoryPresets::exclusiveUdpPort },
        { "safetyGovernorEnabled", (float) CosmicFactoryPresets::safetyGovernorEnabled },
        { "normalMidiRoutingMode", (float) CosmicFactoryPresets::normalMidiRoutingMode },
        { "normalMidiChannel", (float) CosmicFactoryPresets::normalMidiChannel },
        { "timeMode", (float) CosmicFactoryPresets::timeMode },
        { "clockSource", (float) CosmicFactoryPresets::clockSource },
        { "internalBpm", CosmicFactoryPresets::internalBpm },
        { "gridDivision", (float) CosmicFactoryPresets::gridDivision },
        { "maxAttacksPerStep", (float) CosmicFactoryPresets::maxAttacksPerStep },
        { "maxActiveVoices", (float) CosmicFactoryPresets::maxActiveVoices },
        { "gatePercent", CosmicFactoryPresets::gatePercent },
        { "temporalSpread", (float) CosmicFactoryPresets::temporalSpread },
        { "crowdGovernorEnabled", (float) CosmicFactoryPresets::crowdGovernorEnabled },
        { "noteDuration", (float) CosmicFactoryPresets::noteDuration },
        { "sourceCapacity", (float) CosmicFactoryPresets::sourceCapacity },
        { "ensembleSameNoteMode", (float) CosmicFactoryPresets::ensembleSameNoteMode },
        { "conductorRole", (float) preset.conductorRoleChoice },
        { "conductorGroup", (float) CosmicFactoryPresets::conductorGroup },
        { "conductorAttackBudget", (float) CosmicFactoryPresets::conductorAttackBudget },
        { "conductorVoiceBudget", (float) CosmicFactoryPresets::conductorVoiceBudget },
        { "crowdMacrosEnabled", (float) CosmicFactoryPresets::crowdMacrosEnabled },
        { "crowdMacroChannel", (float) CosmicFactoryPresets::crowdMacroChannel },
        { "crowdMacroDensityCc", (float) CosmicFactoryPresets::crowdMacroDensityCc },
        { "crowdMacroCentroidXCc", (float) CosmicFactoryPresets::crowdMacroCentroidXCc },
        { "crowdMacroCentroidYCc", (float) CosmicFactoryPresets::crowdMacroCentroidYCc },
        { "crowdMacroMotionCc", (float) CosmicFactoryPresets::crowdMacroMotionCc },
        { "crowdMacroRate", (float) CosmicFactoryPresets::crowdMacroRate },
        { "pitchSystem", (float) CosmicFactoryPresets::pitchSystem },
        { "scaleRoot", (float) CosmicFactoryPresets::scaleRoot },
        { "scaleRootOctave", (float) CosmicFactoryPresets::scaleRootOctave },
        { "scaleMode", (float) CosmicFactoryPresets::scaleMode },
        { "scaleOctaves", (float) CosmicFactoryPresets::scaleOctaves },
        { "spectralElement", (float) CosmicFactoryPresets::spectralElement },
        { "atomicScaleMode", (float) CosmicFactoryPresets::atomicScaleMode }
    }};

    juce::ValueTree presetState;
    {
        const juce::ScopedLock lock(stateTransactionLock);
        presetState = stateSnapshotOverrideInProgress
                   && stateSnapshotOverride.isValid()
                    ? stateSnapshotOverride.createCopy()
                    : apvts.copyState();
    }
    for (const auto& setting : settings)
        if (auto parameter = CosmicStateMigration::findParameterNode(
                presetState, setting.id); parameter.isValid())
            parameter.setProperty("value", setting.value, nullptr);
    presetState.setProperty("udpPort", preset.udpPort, nullptr);
    presetState.setProperty(
        "midiOutputOption", CosmicFactoryPresets::midiOutputOption, nullptr);
    presetState.setProperty(
        "midiOutputRouteKind", CosmicFactoryPresets::midiOutputRouteKind, nullptr);
    presetState.setProperty("midiOutputDeviceIdentifier", {}, nullptr);
    presetState.setProperty(
        "cosmicMicrowaveSchema", CosmicStateMigration::currentSchema, nullptr);

    std::uint32_t presetGeneration = 0;
    bool ownsMutation = false;
    {
        const juce::ScopedLock transactionLock(stateTransactionLock);
        if (! stateMutationOwnerActive)
        {
            // Join an external sender callback that may already have passed its
            // generation check before publishing the new boundary.
            juce::HighResolutionTimer::stopTimer();
            stateMutationOwnerActive = true;
            ownsMutation = true;
        }
        const juce::ScopedLock pendingLock(pendingStateLock);
        presetGeneration = stateRestoreGeneration.fetch_add(
            1, std::memory_order_acq_rel) + 1;
        pendingRuntimeResetGeneration = presetGeneration;
        pendingFactoryRuntimeGeneration = presetGeneration;
        pendingHostMidiResetGeneration.store(presetGeneration,
                                             std::memory_order_release);
        pendingStateApply.store(true, std::memory_order_release);
        pendingStateGeneration = presetGeneration;
        pendingUdpPort = preset.udpPort;
        pendingMidiOutputOption = CosmicFactoryPresets::midiOutputOption;
        pendingMidiOutputRouteKind = CosmicFactoryPresets::midiOutputRouteKind;
        pendingMidiOutputDeviceIdentifier.clear();
        stateSnapshotOverride = presetState.createCopy();
        stateSnapshotOverrideInProgress = true;
        stateSnapshotOverrideGeneration = presetGeneration;

        if (! ownsMutation)
        {
            // A state callback is already mutating APVTS. Queue the entire
            // preset as the newest authoritative snapshot; its outer owner will
            // reapply it after the current listener stack unwinds.
            pendingApvtsState = presetState.createCopy();
            pendingApvtsGeneration = presetGeneration;
        }

        // The outer owner has joined the high-resolution sender. Silence the
        // currently open endpoint immediately; the complete model reset waits
        // until the winning APVTS generation is known.
        if (ownsMutation)
            sendImmediateExternalAllNotesOffOnly();
    }

    // Preset recall is a hard performance boundary: silence every prior route
    // before changing its pitch, timing, UDP identity or destination.
    if (! ownsMutation)
        return;

    auto setParameter = [this, presetGeneration] (
        const char* id, float denormalizedValue)
    {
        auto generationStillOwned = [this, presetGeneration]
        {
            return stateRestoreGeneration.load(std::memory_order_acquire)
                == presetGeneration;
        };

        if (! generationStillOwned())
            return false;

        if (auto* parameter = apvts.getParameter(id))
        {
            const float normalized = parameter->convertTo0to1(denormalizedValue);
            if (std::isfinite(normalized)
                && std::abs(parameter->getValue() - normalized) > 1.0e-7f)
            {
                parameter->beginChangeGesture();
                if (generationStillOwned())
                    parameter->setValueNotifyingHost(normalized);
                parameter->endChangeGesture();
            }
        }
        return generationStillOwned();
    };

    for (const auto& setting : settings)
    {
        if (! setParameter(setting.id, setting.value))
            break;
    }

    {
        const juce::ScopedLock lock(stateTransactionLock);
        if (stateRestoreGeneration.load(std::memory_order_acquire)
            == presetGeneration)
        {
            stateRestoreCompletedGeneration.store(presetGeneration,
                                                  std::memory_order_release);
            if (stateSnapshotOverrideInProgress
                && stateSnapshotOverrideGeneration == presetGeneration)
            {
                stateSnapshotOverrideInProgress = false;
                stateSnapshotOverrideGeneration = 0;
                stateSnapshotOverride = {};
            }
        }
    }

    // A setStateInformation call made by one of the synchronous parameter
    // listeners is now waiting in the latest-wins queue. Drain it before any
    // route is opened, then apply exactly the final generation's route.
    drainPendingApvtsStateQueue();
    requestTimeFieldRehydrate();
    midiOutputRouteChangedPending.store(true, std::memory_order_release);
    timerCallback();
}

int AudienceProcessor::getMatchingFactoryPresetIndex() const noexcept
{
    auto isInt = [] (const std::atomic<float>* parameter, int expected) noexcept
    {
        return rawParamInt(parameter, expected) == expected;
    };
    auto isFloat = [] (const std::atomic<float>* parameter, float expected) noexcept
    {
        return std::abs(rawParamValue(parameter, expected) - expected) <= 1.0e-4f;
    };

    const bool commonMatches =
        isInt(rawParams.midiOutputType, CosmicFactoryPresets::midiOutputType)
        && isInt(rawParams.midiOutputPath, CosmicFactoryPresets::midiOutputPath)
        && isInt(rawParams.exclusiveUdpPort, CosmicFactoryPresets::exclusiveUdpPort)
        && isInt(rawParams.safetyGovernorEnabled, CosmicFactoryPresets::safetyGovernorEnabled)
        && isInt(rawParams.normalMidiRoutingMode, CosmicFactoryPresets::normalMidiRoutingMode)
        && isInt(rawParams.normalMidiChannel, CosmicFactoryPresets::normalMidiChannel)
        && isInt(rawParams.timeMode, CosmicFactoryPresets::timeMode)
        && isInt(rawParams.clockSource, CosmicFactoryPresets::clockSource)
        && isFloat(rawParams.internalBpm, CosmicFactoryPresets::internalBpm)
        && isInt(rawParams.gridDivision, CosmicFactoryPresets::gridDivision)
        && isInt(rawParams.maxAttacksPerStep, CosmicFactoryPresets::maxAttacksPerStep)
        && isInt(rawParams.maxActiveVoices, CosmicFactoryPresets::maxActiveVoices)
        && isFloat(rawParams.gatePercent, CosmicFactoryPresets::gatePercent)
        && isInt(rawParams.temporalSpread, CosmicFactoryPresets::temporalSpread)
        && isInt(rawParams.crowdGovernorEnabled, CosmicFactoryPresets::crowdGovernorEnabled)
        && isInt(rawParams.noteDuration, CosmicFactoryPresets::noteDuration)
        && isInt(rawParams.sourceCapacity, CosmicFactoryPresets::sourceCapacity)
        && isInt(rawParams.ensembleSameNoteMode,
                 CosmicFactoryPresets::ensembleSameNoteMode)
        && isInt(rawParams.conductorGroup, CosmicFactoryPresets::conductorGroup)
        && isInt(rawParams.conductorAttackBudget, CosmicFactoryPresets::conductorAttackBudget)
        && isInt(rawParams.conductorVoiceBudget, CosmicFactoryPresets::conductorVoiceBudget)
        // Notes Only keeps the retired Crowd Macro parameters solely so old
        // projects and automation lanes remain readable. They are hidden and
        // cannot affect output, so legacy values must not make an otherwise
        // exact Zone A-H setup appear as CUSTOM in the factory-preset UI.
        && isInt(rawParams.pitchSystem, CosmicFactoryPresets::pitchSystem)
        && isInt(rawParams.scaleRoot, CosmicFactoryPresets::scaleRoot)
        && isInt(rawParams.scaleRootOctave, CosmicFactoryPresets::scaleRootOctave)
        && isInt(rawParams.scaleMode, CosmicFactoryPresets::scaleMode)
        && isInt(rawParams.scaleOctaves, CosmicFactoryPresets::scaleOctaves)
        && isInt(rawParams.spectralElement, CosmicFactoryPresets::spectralElement)
        && isInt(rawParams.atomicScaleMode, CosmicFactoryPresets::atomicScaleMode)
        && midiOutputOptionIndex.load(std::memory_order_relaxed)
            == CosmicFactoryPresets::midiOutputOption;
    if (! commonMatches)
        return -1;

    for (int index = 0; index < CosmicFactoryPresets::count; ++index)
    {
        const auto preset = CosmicFactoryPresets::zonePreset(index);
        if (getUdpPort() == preset.udpPort
            && isInt(rawParams.expectedZone, preset.expectedZoneChoice)
            && isInt(rawParams.conductorRole, preset.conductorRoleChoice))
            return index;
    }
    return -1;
}

juce::String AudienceProcessor::getMidiOutputDescription() const
{
    const int path = getMidiOutputPath();
    if (path == 0)
        return "HOST ONLY | external endpoint is isolated";
    if (path == 1)
        return midiOutputOptionIndex.load(std::memory_order_relaxed) <= 0
             ? "EXTERNAL ONLY | no endpoint selected (fail-closed)"
             : midiOutputReady.load(std::memory_order_acquire)
                 ? "EXTERNAL ONLY | " + midiOutputStatus
                 : "EXTERNAL ONLY | selected endpoint unavailable (fail-closed)";
    return midiOutputOptionIndex.load(std::memory_order_relaxed) <= 0
         ? "MIRROR | Host only until an external endpoint is selected"
         : midiOutputReady.load(std::memory_order_acquire)
             ? "MIRROR | Host + " + midiOutputStatus
             : "MIRROR | Host only; selected external endpoint unavailable";
}

int AudienceProcessor::getResolvedMidiOutputOptionIndex()
{
    const juce::ScopedLock transactionLock(stateTransactionLock);
    int routeKind = 0;
    juce::String deviceIdentifier;
    {
        const juce::ScopedLock lock(pendingStateLock);
        routeKind = midiOutputRouteKind;
        deviceIdentifier = midiOutputDeviceIdentifier;
    }

    if (routeKind == 0)
        return 0;
    if (routeKind == 1)
        return 1;

    if (routeKind == 2 && deviceIdentifier.isNotEmpty())
    {
        const auto devices = juce::MidiOutput::getAvailableDevices();
        for (int i = 0; i < devices.size(); ++i)
            if (devices[i].identifier == deviceIdentifier)
                return i + 2;
    }

    // Do not let a stale numeric device index highlight an unrelated endpoint.
    return -1;
}

void AudienceProcessor::commitExplicitRouteEditLocked()
{
    if (routeSnapshotApplyInProgress || stateMutationOwnerActive)
        return;

    const auto generation = stateRestoreGeneration.load(std::memory_order_acquire);
    if (stateRestoreCompletedGeneration.load(std::memory_order_acquire)
            != generation
        || pendingRuntimeResetGeneration == generation
        || pendingFactoryRuntimeGeneration == generation)
        return;

    const juce::ScopedLock lock(pendingStateLock);
    if (pendingStateApply.load(std::memory_order_acquire)
        && pendingStateGeneration == generation)
        return;
    pendingStateApply.store(false, std::memory_order_release);
    pendingStateGeneration = generation;
    pendingUdpPort = getUdpPort();
    pendingMidiOutputOption = midiOutputOptionIndex.load(
        std::memory_order_relaxed);
    pendingMidiOutputRouteKind = midiOutputRouteKind;
    pendingMidiOutputDeviceIdentifier = midiOutputDeviceIdentifier;
    stateRouteReadyGeneration.store(generation, std::memory_order_release);

    if (stateSnapshotOverrideInProgress
        && stateSnapshotOverrideGeneration == generation)
    {
        stateSnapshotOverrideInProgress = false;
        stateSnapshotOverrideGeneration = 0;
        stateSnapshotOverride = {};
    }
}

void AudienceProcessor::setMidiOutputOptionIndex (int index)
{
    const juce::ScopedLock transactionLock(stateTransactionLock);
    disableFreshRouteAssignmentLocked();
    const auto generation = stateRestoreGeneration.load(std::memory_order_acquire);
    auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    const bool onMessageThread = manager != nullptr
                              && manager->isThisTheMessageThread();
    bool routeBoundaryPending = false;
    {
        const juce::ScopedLock lock(pendingStateLock);
        routeBoundaryPending = pendingStateApply.load(std::memory_order_acquire)
                            && pendingStateGeneration == generation;
    }
    const bool runtimeBoundaryPending =
        pendingRuntimeResetGeneration == generation
        || pendingFactoryRuntimeGeneration == generation;

    auto mergeSelectionIntoPendingRoute = [this, index, generation]
    {
        int deferredOption = 0;
        int deferredKind = 0;
        juce::String deferredIdentifier;
        if (index == CosmicFactoryPresets::midiOutputOption)
        {
            deferredOption = CosmicFactoryPresets::midiOutputOption;
            deferredKind = CosmicFactoryPresets::midiOutputRouteKind;
        }
        else if (index > CosmicFactoryPresets::midiOutputOption)
        {
            const auto devices = juce::MidiOutput::getAvailableDevices();
            const int deviceIndex = index - 2;
            if (deviceIndex >= 0 && deviceIndex < devices.size())
            {
                deferredOption = index;
                deferredKind = 2;
                deferredIdentifier = devices[deviceIndex].identifier;
            }
        }

        {
            const juce::ScopedLock lock(pendingStateLock);
            pendingMidiOutputOption = deferredOption;
            pendingMidiOutputRouteKind = deferredKind;
            pendingMidiOutputDeviceIdentifier = deferredIdentifier;
            pendingStateGeneration = generation;
            pendingStateApply.store(true, std::memory_order_release);
        }
        if (stateSnapshotOverrideInProgress
            && stateSnapshotOverride.isValid())
        {
            auto updatedSnapshot = stateSnapshotOverride.createCopy();
            updatedSnapshot.setProperty("midiOutputOption", deferredOption, nullptr);
            updatedSnapshot.setProperty("midiOutputRouteKind", deferredKind, nullptr);
            updatedSnapshot.setProperty("midiOutputDeviceIdentifier",
                                        deferredIdentifier, nullptr);
            stateSnapshotOverride = std::move(updatedSnapshot);
        }
    };

    // CoreMIDI endpoint mutation and JUCE's processor suspension flag are
    // message/control-thread operations. A worker request only updates the
    // coherent pending snapshot; the processor timer applies it safely.
    if (stateMutationOwnerActive || ! onMessageThread)
    {
        mergeSelectionIntoPendingRoute();
        return;
    }

    if (routeBoundaryPending)
    {
        // The user edit is the winning field of the still-pending snapshot.
        // Open exactly that merged route; never expose the older restored
        // endpoint for one transient timer/audio window first.
        mergeSelectionIntoPendingRoute();
        applyPendingRouteSnapshotLocked();
        return;
    }
    if (runtimeBoundaryPending)
        performPendingRuntimeResetLocked(generation);

    if (index == CosmicFactoryPresets::midiOutputOption
        && ! osc.isReceiving())
        withholdVirtualMidiOutputForUnavailableUdp();
    else
        setMidiOutputOptionIndexInternal(index);
    commitExplicitRouteEditLocked();
}

void AudienceProcessor::setMidiOutputOptionIndexInternal (int index)
{
    midiOutputRouteRevision.fetch_add(1, std::memory_order_release);
    juce::HighResolutionTimer::stopTimer();
    ScopedProcessorSuspension processingGuard(
        *this, controlAudioMutationGate, audioCallbacksInFlight);
    discardExternalMidiOutputQueue();
    sendImmediateAllNotesOffToExternal(true);
    externalMidiFifo.reset();
    externalMidiPanicPending.store(false, std::memory_order_release);
    externalTransportResetPending.store(false, std::memory_order_release);
    acknowledgeExternalMidiReset();
    midiOutput.reset();
    midiOutputReady.store(false, std::memory_order_release);
    midiOutputOptionIndex.store(juce::jmax(0, index), std::memory_order_relaxed);
    midiOutputRouteChangedPending.store(true, std::memory_order_release);

    if (index <= 0)
    {
        midiOutputOptionIndex.store(0, std::memory_order_relaxed);
        midiOutputStatus = "Host MIDI Output";
        const juce::ScopedLock lock(pendingStateLock);
        midiOutputRouteKind = 0;
        midiOutputDeviceIdentifier.clear();
        return;
    }

    if (index == 1)
    {
        const auto portName = getVirtualMidiPortName();
        midiOutput = juce::MidiOutput::createNewDevice(portName);
        if (midiOutput != nullptr)
        {
            midiOutputReady.store(true, std::memory_order_release);
            midiOutputStatus = "Virtual port: " + portName;
            const juce::ScopedLock lock(pendingStateLock);
            midiOutputRouteKind = CosmicFactoryPresets::midiOutputRouteKind;
            midiOutputDeviceIdentifier.clear();
            juce::HighResolutionTimer::startTimer(2);
        }
        else
        {
            midiOutputStatus = "Virtual MIDI port unavailable";
            const juce::ScopedLock lock(pendingStateLock);
            midiOutputRouteKind = CosmicFactoryPresets::midiOutputRouteKind;
            midiOutputDeviceIdentifier.clear();
        }
        return;
    }

    const auto devices = juce::MidiOutput::getAvailableDevices();
    const int deviceIndex = index - 2;
    if (deviceIndex >= 0 && deviceIndex < devices.size())
    {
        midiOutput = juce::MidiOutput::openDevice(devices[deviceIndex].identifier);
        if (midiOutput != nullptr)
        {
            midiOutputReady.store(true, std::memory_order_release);
            midiOutputStatus = "MIDI Output: " + devices[deviceIndex].name;
            const juce::ScopedLock lock(pendingStateLock);
            midiOutputRouteKind = 2;
            midiOutputDeviceIdentifier = devices[deviceIndex].identifier;
            juce::HighResolutionTimer::startTimer(2);
        }
        else
        {
            midiOutputStatus = "Failed to open MIDI output";
            const juce::ScopedLock lock(pendingStateLock);
            // Keep the logical selection so a temporarily disconnected device
            // can recover on a later recall/rescan. Runtime output remains
            // fail-closed because midiOutput is null and Ready is false.
            midiOutputRouteKind = 2;
            midiOutputDeviceIdentifier = devices[deviceIndex].identifier;
        }
    }
    else
    {
        midiOutputStatus = "MIDI output device not found";
        midiOutputOptionIndex.store(0, std::memory_order_relaxed);
        const juce::ScopedLock lock(pendingStateLock);
        midiOutputRouteKind = 0;
        midiOutputDeviceIdentifier.clear();
    }
}

void AudienceProcessor::restoreMidiOutputRoute (int routeKind,
                                                 const juce::String& deviceIdentifier,
                                                 int legacyOptionIndex)
{
    if (routeKind < 0)
    {
        if (legacyOptionIndex == CosmicFactoryPresets::midiOutputOption)
        {
            restoreMidiOutputRoute (
                CosmicFactoryPresets::midiOutputRouteKind, {},
                legacyOptionIndex);
            return;
        }
        setMidiOutputOptionIndexInternal(legacyOptionIndex);
        return;
    }

    if (routeKind == 0)
    {
        setMidiOutputOptionIndexInternal(0);
        return;
    }

    if (routeKind == 1)
    {
        // A saved route keeps its exact logical virtual destination, but it
        // must not expose an endpoint while this instance does not own the
        // matching UDP listener. The 1 Hz route maintenance path reopens both
        // halves coherently after ownership becomes available.
        if (! osc.isReceiving())
        {
            withholdVirtualMidiOutputForUnavailableUdp();
            return;
        }

        const auto expectedStatus = "Virtual port: " + getVirtualMidiPortName();
        if (midiOutputOptionIndex.load (std::memory_order_relaxed)
                == CosmicFactoryPresets::midiOutputOption
            && midiOutputReady.load (std::memory_order_acquire)
            && midiOutput != nullptr
            && midiOutputStatus == expectedStatus)
            return;

        setMidiOutputOptionIndexInternal(CosmicFactoryPresets::midiOutputOption);
        lastVirtualMidiRetryMs = juce::Time::getMillisecondCounter();
        return;
    }

    if (routeKind == 2 && deviceIdentifier.isNotEmpty())
    {
        const auto devices = juce::MidiOutput::getAvailableDevices();
        for (int i = 0; i < devices.size(); ++i)
        {
            if (devices[i].identifier == deviceIdentifier)
            {
                setMidiOutputOptionIndexInternal(i + 2);
                return;
            }
        }

        // The saved physical endpoint is currently absent. Close any previous
        // endpoint but retain its stable identifier and logical route kind in
        // project state; a future reconnect can then resolve it automatically.
        setMidiOutputOptionIndexInternal(0);
        midiOutputOptionIndex.store(juce::jmax(2, legacyOptionIndex),
                                    std::memory_order_relaxed);
        {
            const juce::ScopedLock lock(pendingStateLock);
            midiOutputRouteKind = 2;
            midiOutputDeviceIdentifier = deviceIdentifier;
        }
        midiOutputStatus = "Saved MIDI output unavailable: " + deviceIdentifier;
        return;
    }

    setMidiOutputOptionIndexInternal(0);
    midiOutputStatus = "Saved MIDI output is unavailable; using Host MIDI Output";
}

void AudienceProcessor::withholdVirtualMidiOutputForUnavailableUdp()
{
    bool alreadyWithheld = midiOutput == nullptr
                        && ! midiOutputReady.load (std::memory_order_acquire)
                        && midiOutputOptionIndex.load (
                               std::memory_order_relaxed)
                            == CosmicFactoryPresets::midiOutputOption;
    {
        const juce::ScopedLock lock (pendingStateLock);
        alreadyWithheld = alreadyWithheld
                       && midiOutputRouteKind
                            == CosmicFactoryPresets::midiOutputRouteKind
                       && midiOutputDeviceIdentifier.isEmpty();
    }

    // A failed exclusive OSC retry reaches this path once per second. Once
    // the virtual endpoint is already closed, keep that fail-closed logical
    // selection without publishing another route revision/reset sweep.
    if (alreadyWithheld)
    {
        midiOutputStatus = "Virtual MIDI port withheld: UDP "
                         + juce::String (getUdpPort())
                         + " ownership unavailable";
        lastVirtualMidiRetryMs = juce::Time::getMillisecondCounter();
        return;
    }

    setMidiOutputOptionIndexInternal (0);

    // setMidiOutputOptionIndexInternal(0) closes the runtime endpoint safely.
    // Restore the saved logical choice so state/UI and the bounded retry path
    // still know that this is a virtual route, not a Host-output fallback.
    midiOutputOptionIndex.store (CosmicFactoryPresets::midiOutputOption,
                                 std::memory_order_relaxed);
    {
        const juce::ScopedLock lock (pendingStateLock);
        midiOutputRouteKind = CosmicFactoryPresets::midiOutputRouteKind;
        midiOutputDeviceIdentifier.clear();
    }
    midiOutputStatus = "Virtual MIDI port withheld: UDP "
                     + juce::String (getUdpPort())
                     + " ownership unavailable";
    lastVirtualMidiRetryMs = juce::Time::getMillisecondCounter();
}

void AudienceProcessor::sendImmediateAllNotesOffToExternal (bool processingAlreadySuspended)
{
    if (midiOutput == nullptr || midiOutputOptionIndex.load(std::memory_order_relaxed) == 0)
        return;

    const bool shouldResume = ! processingAlreadySuspended && ! isSuspended();
    if (shouldResume)
        suspendProcessing(true);

    // A fixed channel sweep is the immediate panic primitive. Enumerating every
    // semantic source/finger voice can enqueue thousands of duplicate NoteOffs
    // ahead of the actual CC120/123 messages on a slow physical MIDI link.
    for (int channel = 1; channel <= 16; ++channel)
    {
        midiOutput->sendMessageNow(juce::MidiMessage::allNotesOff(channel));
        midiOutput->sendMessageNow(juce::MidiMessage::allSoundOff(channel));
    }

    // mpeOut is owned exclusively by processBlock. suspendProcessing() stops
    // future callbacks but is not a join barrier for a callback that may
    // already be running, so mutating the bounded deadline heap here would be
    // a data race. Callers publish midiOutputRouteChangedPending and the next
    // audio boundary clears the scheduler while emitting the Host/Mirror
    // safety sweep.

    if (shouldResume)
        suspendProcessing(false);
}

void AudienceProcessor::sendImmediateExternalAllNotesOffOnly()
{
    // Used by a worker-thread state boundary after the high-resolution sender
    // has been joined and while route I/O is serialised. Do not touch mpeOut,
    // Simulator, APVTS, JUCE's suspension flag, or any audio-thread-owned state.
    if (midiOutput == nullptr
        || midiOutputOptionIndex.load(std::memory_order_relaxed) == 0)
        return;

    for (int channel = 1; channel <= 16; ++channel)
    {
        midiOutput->sendMessageNow(juce::MidiMessage::allNotesOff(channel));
        midiOutput->sendMessageNow(juce::MidiMessage::allSoundOff(channel));
    }
}

void AudienceProcessor::closeMidiOutput()
{
    midiOutputRouteRevision.fetch_add(1, std::memory_order_release);
    juce::HighResolutionTimer::stopTimer();
    sendImmediateAllNotesOffToExternal();
    midiOutput.reset();
    midiOutputReady.store(false, std::memory_order_release);
    midiOutputOptionIndex.store(0, std::memory_order_relaxed);
    midiOutputRouteChangedPending.store(true, std::memory_order_release);
    midiOutputStatus = "Host MIDI Output";
    const juce::ScopedLock lock(pendingStateLock);
    midiOutputRouteKind = 0;
    midiOutputDeviceIdentifier.clear();
}

void AudienceProcessor::setUdpPort (int port)
{
    const juce::ScopedLock transactionLock(stateTransactionLock);
    disableFreshRouteAssignmentLocked();
    const auto generation = stateRestoreGeneration.load(std::memory_order_acquire);
    auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    const bool onMessageThread = manager != nullptr
                              && manager->isThisTheMessageThread();
    bool routeBoundaryPending = false;
    {
        const juce::ScopedLock lock(pendingStateLock);
        routeBoundaryPending = pendingStateApply.load(std::memory_order_acquire)
                            && pendingStateGeneration == generation;
    }
    const bool runtimeBoundaryPending =
        pendingRuntimeResetGeneration == generation
        || pendingFactoryRuntimeGeneration == generation;

    auto mergePortIntoPendingRoute = [this, port, generation]
    {
        const int deferredPort = juce::jlimit(1, 65535, port);
        {
            const juce::ScopedLock lock(pendingStateLock);
            pendingUdpPort = deferredPort;
            pendingStateGeneration = generation;
            pendingStateApply.store(true, std::memory_order_release);
        }
        if (stateSnapshotOverrideInProgress
            && stateSnapshotOverride.isValid())
        {
            auto updatedSnapshot = stateSnapshotOverride.createCopy();
            updatedSnapshot.setProperty("udpPort", deferredPort, nullptr);
            stateSnapshotOverride = std::move(updatedSnapshot);
        }
    };

    // OSC/Simulator teardown and JUCE's processor suspension flag are
    // message/control-thread operations. A worker request only updates the
    // coherent pending snapshot; the processor timer applies it safely.
    if (stateMutationOwnerActive || ! onMessageThread)
    {
        mergePortIntoPendingRoute();
        return;
    }

    if (routeBoundaryPending)
    {
        mergePortIntoPendingRoute();
        applyPendingRouteSnapshotLocked();
        return;
    }
    if (runtimeBoundaryPending)
        performPendingRuntimeResetLocked(generation);

    // Keep the audio callback quiescent for the complete OSC-port / virtual-
    // endpoint identity mutation. Otherwise a fresh packet could be rendered
    // between the old listener closing and the new endpoint becoming coherent.
    ScopedProcessorSuspension processingGuard(
        *this, controlAudioMutationGate, audioCallbacksInFlight);
    setUdpPortInternal(port);
    commitExplicitRouteEditLocked();
}

void AudienceProcessor::setUdpPortInternal (int port, bool reopenVirtualEndpoint)
{
    const int safePort = juce::jlimit(1, 65535, port);
    const int previousPort = getUdpPort();
    const bool virtualRouteSelected =
        midiOutputOptionIndex.load (std::memory_order_relaxed)
            == CosmicFactoryPresets::midiOutputOption;
    const bool exclusive = rawParamBool(
        rawParams.exclusiveUdpPort, CosmicFactoryPresets::exclusiveUdpPort != 0);
    const auto portPolicy = exclusive ? OscBridge::PortPolicy::exclusive
                                      : OscBridge::PortPolicy::shared;
    const int expectedZoneChoice = juce::jlimit(
        0, 26, rawParamInt(
            rawParams.expectedZone,
            CosmicFactoryPresets::zonePreset(0).expectedZoneChoice));
    const int expectedZone = expectedZoneChoice - 1;
    const bool expectedZoneChanged = expectedZone != osc.getExpectedZone();
    if (safePort == previousPort && osc.isReceiving()
        && osc.isExclusive() == exclusive && ! expectedZoneChanged)
    {
        if (reopenVirtualEndpoint && virtualRouteSelected
            && (! midiOutputReady.load (std::memory_order_acquire)
                || midiOutput == nullptr))
        {
            setMidiOutputOptionIndexInternal (
                CosmicFactoryPresets::midiOutputOption);
            lastVirtualMidiRetryMs = juce::Time::getMillisecondCounter();
        }
        oscStatus = osc.oscStatus();
        lastExclusiveUdpPort = exclusive;
        lastExpectedZoneChoice = expectedZoneChoice;
        lastOscRetryMs = juce::Time::getMillisecondCounter();
        return;
    }

    // The virtual endpoint identity is derived only from the UDP port. A
    // same-port zone/policy change or a 1 Hz ownership retry must not tear down
    // the endpoint and disconnect Ableton from its stable source.
    const bool refreshVirtualPort = reopenVirtualEndpoint
                                 && virtualRouteSelected
                                 && (safePort != previousPort
                                     || ! midiOutputReady.load (
                                            std::memory_order_acquire)
                                     || midiOutput == nullptr);
    const bool needsReset = osc.isReceiving() || safePort != previousPort
                         || expectedZoneChanged;
    osc.stop();
    if (needsReset)
        panic();
    udpPort.store(safePort, std::memory_order_relaxed);
    osc.setExpectedZone(expectedZone);
    osc.start(safePort, portPolicy);
    oscStatus = osc.oscStatus();
    lastExclusiveUdpPort = exclusive;
    lastExpectedZoneChoice = expectedZoneChoice;
    lastOscRetryMs = juce::Time::getMillisecondCounter();

    if (reopenVirtualEndpoint && virtualRouteSelected && ! osc.isReceiving())
        withholdVirtualMidiOutputForUnavailableUdp();
    else if (refreshVirtualPort)
    {
        setMidiOutputOptionIndexInternal (
            CosmicFactoryPresets::midiOutputOption);
        lastVirtualMidiRetryMs = juce::Time::getMillisecondCounter();
    }
}

void AudienceProcessor::performPendingRuntimeResetLocked (
    std::uint32_t generation)
{
    if (pendingRuntimeResetGeneration == generation)
    {
        panic();
        if (pendingRuntimeResetGeneration == generation)
            pendingRuntimeResetGeneration = 0;
    }

    // Simulator profile is not an APVTS/host-state property. Factory preset
    // recall deliberately restores its reproducible Human baseline, including
    // when that preset was queued by a reentrant parameter callback.
    if (pendingFactoryRuntimeGeneration == generation)
    {
        simulator.setRandomMovement(false);
        simulator.setProfile(Simulator::Profile::human);
        pendingFactoryRuntimeGeneration = 0;
    }
}

void AudienceProcessor::panic()
{
    // Panic mutates the simulator/model/time-field and the current CoreMIDI
    // endpoint. Serialise it with route/state publication; the audio thread
    // never takes this lock and no APVTS/host callback is invoked below.
    const juce::ScopedLock transactionLock(stateTransactionLock);
    const auto generation = stateRestoreGeneration.load(std::memory_order_acquire);
    if (pendingRuntimeResetGeneration == generation)
        pendingRuntimeResetGeneration = 0;
    juce::HighResolutionTimer::stopTimer();
    ScopedProcessorSuspension processingGuard(
        *this, controlAudioMutationGate, audioCallbacksInFlight);
    simulator.clearSilently();
    releaseAllIncomingMidiNotes();
    audienceModel.clear();
    restartSourceQualityEpoch(juce::Time::getMillisecondCounter());
    crowdTimeField.reset();
    resetCrowdExpressionMacros();
    adaptiveCrowdGovernor.reset();
    pressureSafetyGovernor.reset();
    safetyOutput = pressureSafetyGovernor.getOutput();
    governorLastUpdateSeconds = 0.0;
    governorControlClockInitialised = false;
    governorLastVoiceLimit = CosmicFactoryPresets::maxActiveVoices;
    safetyLastUpdateSeconds = 0.0;
    safetyLastOscMessages = osc.getValidMessageCount();
    safetyLastDroppedMotion = fingerRouter.getDroppedMotionEventCount();
    safetyClockInitialised = false;
    governorRecentSourceCount.store(0, std::memory_order_release);
    governorObservedDensity.store(0, std::memory_order_relaxed);
    governorEffectiveAttacks.store(4, std::memory_order_relaxed);
    governorEffectiveActive.store(8, std::memory_order_relaxed);
    governorEffectiveSpread.store(1, std::memory_order_relaxed);
    governorBand.store(0, std::memory_order_relaxed);
    safetyGovernorState.store(0, std::memory_order_relaxed);
    safetyGovernorReasons.store(0, std::memory_order_relaxed);
    safetyIngressRate.store(0.0, std::memory_order_relaxed);
    safetyExternalFifoPressure.store(0.0, std::memory_order_relaxed);
    externalFifoOldestAgeSeconds.store(0.0, std::memory_order_relaxed);
    lastProcessDeadlineRatio.store(0.0, std::memory_order_relaxed);
    requestTimeFieldRehydrate();
    // Drop packets produced before the panic while the audio producer is
    // quiescent. Otherwise an old NoteOn could be drained after the immediate
    // all-off sweep and recreate a stuck external note.
    externalMidiFifo.reset();
    externalMidiPanicPending.store(false, std::memory_order_release);
    externalTransportResetPending.store(false, std::memory_order_release);
    acknowledgeExternalMidiReset();
    sendImmediateAllNotesOffToExternal(true);
    // The external endpoint was reset immediately; Host/Mirror can only be
    // reset by returning MIDI from the next audio callback.
    midiOutputRouteChangedPending.store(true, std::memory_order_release);
    if (midiOutput != nullptr && midiOutputOptionIndex.load(std::memory_order_relaxed) > 0)
        juce::HighResolutionTimer::startTimer(2);
}

void AudienceProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    juce::ValueTree state;
    {
        const juce::ScopedLock transactionLock(stateTransactionLock);
        if (stateSnapshotOverrideInProgress
            && stateSnapshotOverride.isValid())
        {
            // Published snapshots are immutable. Retain the ValueTree handle
            // under the short lock, then perform XML allocation outside it.
            state = stateSnapshotOverride;
        }
        else
        {
            state = apvts.copyState();
            const juce::ScopedLock lock(pendingStateLock);
            const bool pendingSnapshot =
                pendingStateApply.load(std::memory_order_acquire)
                || stateRouteReadyGeneration.load(std::memory_order_acquire)
                    != stateRestoreGeneration.load(std::memory_order_acquire);
            state.setProperty("udpPort",
                              pendingSnapshot ? pendingUdpPort : getUdpPort(), nullptr);
            state.setProperty("midiOutputOption",
                              pendingSnapshot
                                ? pendingMidiOutputOption
                                : midiOutputOptionIndex.load(std::memory_order_relaxed),
                              nullptr);
            state.setProperty("midiOutputRouteKind",
                              pendingSnapshot ? pendingMidiOutputRouteKind
                                              : midiOutputRouteKind,
                              nullptr);
            state.setProperty("midiOutputDeviceIdentifier",
                              pendingSnapshot ? pendingMidiOutputDeviceIdentifier
                                              : midiOutputDeviceIdentifier,
                              nullptr);
        }
    }
    state = state.createCopy();
    state.setProperty("cosmicMicrowaveSchema", CosmicStateMigration::currentSchema, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destination);
}

void AudienceProcessor::setStateInformation (const void* data, int size)
{
    // Publish intent before any XML allocation/parsing. A fresh instance's
    // first timer then stays quarantined until this callback either publishes
    // a complete valid generation or rejects the blob and clears the intent.
    {
        // Linearise restore intent against the timer's route-commit lock. One
        // side wins completely: either this intent is visible before any
        // pending OSC/CoreMIDI route opens, or the already-committing timer
        // finishes before this restore begins parsing.
        const juce::ScopedLock transactionLock (stateTransactionLock);
        hostStateRestoreIntentCount.fetch_add (1, std::memory_order_acq_rel);
    }
    auto finishRestoreIntent = [this]
    {
        const auto previous = hostStateRestoreIntentCount.fetch_sub (
            1, std::memory_order_acq_rel);
        jassert (previous != 0u);
        juce::ignoreUnused (previous);
    };

    auto xml = getXmlFromBinary(data, size);
    if (xml == nullptr)
    {
        finishRestoreIntent();
        return;
    }

    auto state = juce::ValueTree::fromXml(*xml);
    if (! state.isValid())
    {
        finishRestoreIntent();
        return;
    }

    CosmicStateMigration::migrate(state);

    const int restoredUdpPort = boundedStateInt(
        state.getProperty("udpPort", CosmicFactoryPresets::firstUdpPort),
        1, 65535, CosmicFactoryPresets::firstUdpPort);
    const int restoredMidiOutputOption = boundedStateInt(
        state.getProperty("midiOutputOption",
                          CosmicFactoryPresets::midiOutputOption),
        0, std::numeric_limits<int>::max(),
        CosmicFactoryPresets::midiOutputOption);
    const int restoredMidiOutputRouteKind = boundedStateInt(
        state.getProperty("midiOutputRouteKind", -1), -1, 2, -1);
    const auto restoredMidiOutputDeviceIdentifier =
        state.getProperty("midiOutputDeviceIdentifier").toString();

    state.setProperty("udpPort", restoredUdpPort, nullptr);
    state.setProperty("midiOutputOption", restoredMidiOutputOption, nullptr);
    state.setProperty("midiOutputRouteKind", restoredMidiOutputRouteKind, nullptr);
    state.setProperty("midiOutputDeviceIdentifier",
                      restoredMidiOutputDeviceIdentifier, nullptr);

    bool ownsMutation = false;
    {
        const juce::ScopedLock transactionLock(stateTransactionLock);
        // A valid host snapshot is authoritative even when it contains the
        // same Zone A defaults as a fresh instance. Duplicated/restored
        // Ableton tracks therefore keep their exact saved port and fail
        // closed on conflict instead of being shifted to another zone.
        disableFreshRouteAssignmentLocked();
        if (! stateMutationOwnerActive)
        {
            // Join a sender callback that may already have passed the old
            // generation gate, then become the sole APVTS mutation owner.
            juce::HighResolutionTimer::stopTimer();
            stateMutationOwnerActive = true;
            ownsMutation = true;
        }
        const juce::ScopedLock lock(pendingStateLock);
        // Publish the generation before APVTS changes so the audio and
        // external sender threads immediately fail closed. The complete
        // immutable snapshot also makes a synchronous getState request
        // independent of APVTS's one-parameter-at-a-time notifications.
        const auto restoreGeneration = stateRestoreGeneration.fetch_add(
            1, std::memory_order_acq_rel) + 1;
        pendingRuntimeResetGeneration = restoreGeneration;
        pendingFactoryRuntimeGeneration = 0;
        pendingHostMidiResetGeneration.store(restoreGeneration,
                                             std::memory_order_release);
        pendingUdpPort = restoredUdpPort;
        pendingMidiOutputOption = restoredMidiOutputOption;
        pendingMidiOutputRouteKind = restoredMidiOutputRouteKind;
        pendingMidiOutputDeviceIdentifier = restoredMidiOutputDeviceIdentifier;
        pendingStateGeneration = restoreGeneration;
        pendingStateApply.store(true, std::memory_order_release);
        stateSnapshotOverride = state.createCopy();
        stateSnapshotOverrideInProgress = true;
        stateSnapshotOverrideGeneration = restoreGeneration;
        pendingApvtsState = state.createCopy();
        pendingApvtsGeneration = restoreGeneration;

        // Only a fully decoded, migrated snapshot may retire the previous
        // route. CoreMIDI and the process-local Conductor contain no UI state,
        // so they can be made fail-closed on this host control thread. The
        // OSC receiver's UI-visible fields are handed to the message thread.
        pendingOscDeactivationGeneration = restoreGeneration;
        deactivateExternalMidiForRestoreLocked();
        unregisterGlobalConductor();
        conductorRegistrationStatus.store (
            static_cast<int> (
                GlobalConductorHub::RegistrationStatus::InvalidPort),
            std::memory_order_relaxed);

    }

    juce::AsyncUpdater::triggerAsyncUpdate();

    if (! ownsMutation)
    {
        finishRestoreIntent();
        return;
    }

    auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    const bool onMessageThread = manager != nullptr
                              && manager->isThisTheMessageThread();

    drainPendingApvtsStateQueue();
    finishRestoreIntent();
    juce::AsyncUpdater::triggerAsyncUpdate();

    // Ableton and Standalone normally restore on the message thread, where
    // the matching route can be opened immediately. Worker/headless restores
    // use the posted AsyncUpdater handoff; Timer remains its bounded fallback.
    if (onMessageThread)
        timerCallback();

}

juce::String AudienceProcessor::getOutgoingMidiDebugText (int maxEvents) const
{
    juce::String text;
    text << "outgoing MIDI\n";
    text << "destination : " << getMidiOutputStatus() << "\n";
    text << "notes sent  : " << getMidiNotesSent() << "\n";
    text << "active notes: " << getTimeFieldActive() << "\n";
    const auto latest = mpeOut.getOutgoingDebugLatest();
    const int count = juce::jlimit(0, MpeMidiOutput::kDebugEventQueueSize,
                                   juce::jmin(maxEvents, (int) latest));
    for (uint32_t sequence = latest - (uint32_t) count + 1; count > 0 && sequence <= latest; ++sequence)
    {
        MpeMidiOutput::OutgoingDebugEvent event;
        if (! mpeOut.readOutgoingDebugSlot(sequence, event))
            continue;
        text << juce::String(sequence).paddedLeft(' ', 5) << "  ["
             << juce::String::toHexString(event.b0 & 0xff).paddedLeft('0', 2);
        if (event.size > 1) text << " " << juce::String::toHexString(event.b1 & 0xff).paddedLeft('0', 2);
        if (event.size > 2) text << " " << juce::String::toHexString(event.b2 & 0xff).paddedLeft('0', 2);
        text << "]\n";
    }
    return text;
}

juce::String AudienceProcessor::getIncomingMidiDebugText (int maxEvents) const
{
    juce::String text;
    text << "incoming MIDI / note-only thru\n";
    text << "active keys : " << getActiveExternalMidiKeys() << "\n";
#if COSMIC_MIDI_DIAGNOSTICS
    const auto latest = incomingMidiDebugWriteCounter.load(std::memory_order_acquire);
    const int count = juce::jlimit(0, midiDebugEventQueueSize,
                                   juce::jmin(maxEvents, (int) latest));
    for (uint32_t sequence = latest - (uint32_t) count + 1; count > 0 && sequence <= latest; ++sequence)
    {
        const auto& event = incomingMidiDebugEvents[(size_t) ((sequence - 1) % midiDebugEventQueueSize)];
        if (event.sequence.load(std::memory_order_acquire) != sequence)
            continue;
        const int eventSize = event.size.load(std::memory_order_relaxed);
        text << juce::String(sequence).paddedLeft(' ', 5) << "  ["
             << juce::String::toHexString(event.byte0.load(std::memory_order_relaxed) & 0xff).paddedLeft('0', 2);
        if (eventSize > 1) text << " " << juce::String::toHexString(event.byte1.load(std::memory_order_relaxed) & 0xff).paddedLeft('0', 2);
        if (eventSize > 2) text << " " << juce::String::toHexString(event.byte2.load(std::memory_order_relaxed) & 0xff).paddedLeft('0', 2);
        text << "]\n";
    }
#else
    juce::ignoreUnused(maxEvents);
    text << "packet diagnostics disabled in this build\n";
#endif
    return text;
}

juce::String AudienceProcessor::getMidiStateDebugText() const
{
    juce::String text;
    text << "Cosmic Microwave MIDI router\n";
    text << "OSC            : " << osc.oscStatus() << "\n";
    text << "UDP            : " << getUdpPort() << "\n";
    text << "source capacity: " << audienceModel.getSourceCapacity() << "\n";
    text << "active sources : " << audienceModel.getActiveSourceCount() << "\n";
    text << "active touches : " << audienceModel.getActiveFingerCount() << "\n";
    text << "capacity drops : "
         << (int) audienceModel.getCapacityDroppedEventCount() << "\n";
    text << "MIDI output    : " << getMidiOutputStatus() << "\n";
    text << "dropped output : " << (int) externalMidiDropped.load(std::memory_order_relaxed) << "\n";
    text << "scheduled notes: " << mpeOut.getScheduledNoteCount() << "\n";
    text << "physical notes : " << mpeOut.getPhysicalNoteCount() << "\n";
    text << "deadline offs  : " << (juce::int64) mpeOut.getDeadlineReleaseCount() << "\n";
    text << "retrigger merge: " << (juce::int64) mpeOut.getCoalescedRetriggerCount() << "\n";
    text << "hard retrigger : " << (juce::int64) mpeOut.getHardRetriggerCount() << "\n";
    text << "capacity steals: " << (juce::int64) mpeOut.getCapacityStealCount()
         << " (global " << (juce::int64) mpeOut.getGlobalLimitStealCount()
         << ", channel " << (juce::int64) mpeOut.getChannelLimitStealCount()
         << ", source " << (juce::int64) mpeOut.getSourceLimitStealCount()
         << ")\n";
    text << "safety resets  : " << (juce::int64) mpeOut.getSafetyResetCount() << "\n";
    return text;
}

juce::String AudienceProcessor::getMidiDebugReportText() const
{
    return getMidiStateDebugText() + "\n"
         + getIncomingMidiDebugText(96) + "\n"
         + getOutgoingMidiDebugText(96);
}

juce::AudioProcessorEditor* AudienceProcessor::createEditor()
{
    return new AudienceEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudienceProcessor();
}
