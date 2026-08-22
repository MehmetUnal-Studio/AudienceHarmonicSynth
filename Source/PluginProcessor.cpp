#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PluginStateMigration.h"

#include <cmath>
#include <limits>

static_assert (OscFingerRouter::MAX_SOURCES == SeatEventSink::MAX_OSC_SOURCES,
               "OSC parser and MIDI audience capacities must match");
static_assert (OscFingerRouter::MAX_VOICES == MpeMidiOutput::kMaxMidiSources,
               "OSC finger and MIDI output voice capacities must match");

namespace
{
    class ScopedProcessorSuspension final
    {
    public:
        explicit ScopedProcessorSuspension (juce::AudioProcessor& owner)
            : processor(owner), didSuspend(! owner.isSuspended())
        {
            if (didSuspend)
                processor.suspendProcessing(true);
        }

        ~ScopedProcessorSuspension()
        {
            if (didSuspend)
                processor.suspendProcessing(false);
        }

    private:
        juce::AudioProcessor& processor;
        const bool didSuspend;
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

    int boundedStateInt (const juce::var& value, int minimum, int maximum,
                         int fallback) noexcept
    {
        const int safeFallback = juce::jlimit(minimum, maximum, fallback);
        if (! (value.isInt() || value.isInt64()
               || value.isDouble() || value.isBool()))
            return safeFallback;

        const double raw = static_cast<double>(value);
        if (! std::isfinite(raw))
            return safeFallback;

        const double bounded = juce::jlimit(static_cast<double>(minimum),
                                            static_cast<double>(maximum), raw);
        return static_cast<int>(bounded);
    }

    struct MpeZoneChannels
    {
        int master;
        int memberFirst;
        int memberLast;
    };

    MpeZoneChannels zoneChannels (int zoneIndex) noexcept
    {
        return zoneIndex == 1 ? MpeZoneChannels { 16, 1, 15 }
                              : MpeZoneChannels { 1, 2, 16 };
    }

}

AudienceProcessor::AudienceProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createLayout()),
      audienceModel(fingerRouter),
      osc(audienceModel, OscBridge::FingerPolicy::finger0Only),
      simulator(audienceModel, MidiAudienceModel::MAX_SOURCES)
{
    cacheParameterPointers();
    releaseAllIncomingMidiNotes();
    updatePitchMap();
    mpeOut.reset();
    setUdpPort(getUdpPort());
    juce::Timer::startTimerHz(60);
}

AudienceProcessor::~AudienceProcessor()
{
    juce::Timer::stopTimer();
    juce::HighResolutionTimer::stopTimer();
    closeMidiOutput();
    osc.stop();
}

juce::AudioProcessorValueTreeState::ParameterLayout AudienceProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("midiOutputType", 1), "MIDI Format",
        StringArray { "Off", "Normal MIDI", "MPE MIDI" }, 1));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("normalMidiRoutingMode", 1), "Normal MIDI Routing",
        StringArray { "Single Channel", "Per Source 1-16" }, 1));

    StringArray midiChannels;
    for (int channel = 1; channel <= 16; ++channel)
        midiChannels.add(String(channel));
    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("normalMidiChannel", 1), "Normal MIDI Channel", midiChannels, 0));

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
        StringArray { "Flow", "Grid", "Ensemble" }, 2));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("clockSource", 1), "Time Field Clock",
        StringArray { "Host", "Internal" }, 0));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID("internalBpm", 1), "Internal BPM",
        NormalisableRange<float>(40.0f, 240.0f, 0.1f), 120.0f));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("gridDivision", 1), "Grid Division",
        StringArray { "1/4", "1/8", "1/16", "1/32" }, 2));

    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID("maxAttacksPerStep", 1), "Attacks Per Step", 1, 16, 4));

    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID("maxActiveVoices", 1), "Maximum Active Voices", 1, 16, 16));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID("gatePercent", 1), "Gate Length",
        NormalisableRange<float>(5.0f, 100.0f, 1.0f), 70.0f));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("temporalSpread", 1), "Temporal Spread",
        StringArray { "1", "2", "4", "8", "16" }, 2));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("pitchSystem", 1), "Pitch System",
        StringArray { "Tonal", "Atomic" }, 1));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("scaleRoot", 1), "Root",
        StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("scaleRootOctave", 1), "Root Octave",
        StringArray { "0", "1", "2", "3", "4", "5", "6" }, 2));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("scaleMode", 1), "Scale",
        StringArray { "Major", "Natural Minor", "Pentatonic", "Dorian",
                      "Lydian", "Harmonic Minor", "Whole Tone" }, 0));

    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID("scaleOctaves", 1), "Octave Range", 1, 6, 4));

    StringArray atomicElements;
    for (int index = 0; index < AtomicScaleCatalog::numElements; ++index)
        atomicElements.add(String::fromUTF8(AtomicScaleCatalog::elementName(index))
                           + " (" + String::fromUTF8(AtomicScaleCatalog::elementSymbol(index)) + ")");
    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("spectralElement", 1), "Atomic Element", atomicElements, 1));

    StringArray atomicModes;
    for (int index = 0; index < AtomicScaleCatalog::numModes; ++index)
        atomicModes.add(String::fromUTF8(AtomicScaleCatalog::modeName(index)));
    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("atomicScaleMode", 1), "Atomic Scale Mode", atomicModes, 1));

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
}

void AudienceProcessor::prepareToPlay (double sampleRate, int)
{
    // Treat the host value as untrusted. Subnormal rates make the external
    // MIDI due-time calculation overflow to infinity and can head-of-line
    // block every later NoteOff in the FIFO.
    currentSampleRate = std::isfinite(sampleRate)
                      ? juce::jlimit(1.0, 768000.0, sampleRate)
                      : 44100.0;
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
    timeFieldRehydratePending = true;
    retriggerFingerMidi = true;
    fingerRetriggerCursor = 0;
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
    externalMidiProducerQuarantined.store(true, std::memory_order_release);
    externalTransportResetPending.store(true, std::memory_order_release);
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
    const int root = juce::jlimit(0, 11, rawParamInt(rawParams.scaleRoot));
    const int rootOctave = juce::jlimit(0, 6, rawParamInt(rawParams.scaleRootOctave, 2));
    const int mode = juce::jlimit(0, MidiPitchMap::numScaleModes - 1,
                                  rawParamInt(rawParams.scaleMode));
    const int octaves = juce::jlimit(1, 6, rawParamInt(rawParams.scaleOctaves, 4));
    const int pitchSystem = juce::jlimit(0, 1, rawParamInt(rawParams.pitchSystem, 1));
    const int atomicElement = AtomicScaleCatalog::clampElementIndex(
        rawParamInt(rawParams.spectralElement, 1));
    const int atomicMode = AtomicScaleCatalog::clampModeIndex(
        rawParamInt(rawParams.atomicScaleMode, 1));

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
            retriggerFingerMidi = true;
            fingerRetriggerCursor = 0;
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
            // bounded OSC/MPE reset + retrigger burst, prevents growth, and caps
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
    const double externalBlockStartTimeMs = juce::Time::getMillisecondCounterHiRes();

    // Preserve the host input before reclaiming the preallocated output buffer.
    const bool midiInputOverflowed = processIncomingMidi(midiMessages,
                                                          buffer.getNumSamples());
    updatePitchMap();
    const bool pitchMapChanged = pitchMapChangedThisBlock;

    // APVTS parameter atomics may change at any point during a callback. Capture
    // one bounded snapshot and use it for change detection, reset decisions and
    // rendering alike. Re-reading the atomics in renderOutgoingMidi used to let
    // a mid-block automation write switch routing without the matching safety
    // reset, corrupting normal-note refcounts or MPE channel ownership.
    const auto midiConfig = buildMpeConfig();
    const auto timeConfig = buildTimeFieldConfig(midiConfig);
    const int midiType = midiConfig.outputType;
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
    const int maxAttacks = timeConfig.maxAttacksPerStep;
    const int maxActive = timeConfig.maxActive;
    const float gatePercent = static_cast<float>(timeConfig.gatePercent);
    const int temporalSpread = timeConfig.spreadSlots <= 1 ? 0
                             : timeConfig.spreadSlots <= 2 ? 1
                             : timeConfig.spreadSlots <= 4 ? 2
                             : timeConfig.spreadSlots <= 8 ? 3 : 4;
    const bool routeChanged = midiOutputRouteChangedPending.exchange(false, std::memory_order_acq_rel);

    const bool midiConfigChanged = midiType != lastMidiOutputType
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
                                  || maxAttacks != lastMaxAttacksPerStep
                                  || maxActive != lastMaxActiveVoices
                                  || temporalSpread != lastTemporalSpread
                                  || internalTempoChanged
                                  || std::abs(gatePercent - lastGatePercent) > 1.0e-4f;
    const bool timeDomainChanged = anyTimeValueChanged
                                && (timeMode != 0 || lastTimeMode != 0);
    const bool configChanged = midiConfigChanged || timeDomainChanged;
    const bool hadActiveOutput = lastMidiOutputType != 0;
    const bool needsSafetyReset = ((configChanged || routeChanged || pitchMapChanged) && hadActiveOutput)
                               || (midiInputOverflowed && (hadActiveOutput || midiType != 0));

    lastMidiOutputType = midiType;
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
    lastMaxAttacksPerStep = maxAttacks;
    lastMaxActiveVoices = maxActive;
    lastGatePercent = gatePercent;
    lastTemporalSpread = temporalSpread;

    audienceModel.setMotionEventForwardingEnabled(timeMode == 0);

    mpeOut.setMemberRange(midiConfig.memberFirst, midiConfig.memberLast);
    if (configChanged || routeChanged || midiInputOverflowed || pitchMapChanged)
    {
        mpeOut.markSetupDirty();
        if (timeMode == 0)
        {
            retriggerFingerMidi = true;
            fingerRetriggerCursor = 0;
            timeFieldRehydratePending = true;
        }
        else
        {
            retriggerFingerMidi = false;
            fingerRetriggerCursor = 0;
            timeFieldRehydratePending = true;
        }
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
        mpeOut.reset();
    }
    else if (configChanged)
    {
        mpeOut.reset();
    }

    const bool outputEnabled = midiType != 0;
    if (outputEnabled)
    {
        for (const auto metadata : midiInputScratch)
            output.addEvent(metadata.data, metadata.numBytes, metadata.samplePosition);
    }

    const auto clockFrame = captureTimeFieldClock(buffer.getNumSamples(),
                                                   externalBlockStartTimeMs * 0.001);
    renderOutgoingMidi(output, buffer.getNumSamples(), outputEnabled,
                       midiConfig, timeConfig, clockFrame, needsSafetyReset);
#if COSMIC_MIDI_DIAGNOSTICS
    mpeOut.recordOutgoingMidiDebugEvents(output);
#endif
    queueMidiToExternalOutput(output, externalBlockStartTimeMs, buffer.getNumSamples());

    midiMessages.swapWith(midiRenderScratch);
    midiRenderScratchLoanedToHost = true;
}

MpeMidiOutput::MpeConfig AudienceProcessor::buildMpeConfig() const
{
    MpeMidiOutput::MpeConfig config;
    config.outputType = juce::jlimit(0, 2, rawParamInt(rawParams.midiOutputType, 1));
    const auto zone = zoneChannels(rawParamInt(rawParams.mpeZone));
    config.masterChannel = zone.master;
    config.memberFirst = zone.memberFirst;
    config.memberLast = zone.memberLast;
    config.pitchBendRangeChoice = juce::jlimit(
        0, 3, rawParamInt(rawParams.mpePitchBendRange));
    config.normalRoutingMode = juce::jlimit(
        0, 1, rawParamInt(rawParams.normalMidiRoutingMode, 1));
    config.normalMidiChannel = juce::jlimit(
        0, 15, rawParamInt(rawParams.normalMidiChannel));
    config.sendSetupMessages = rawParamBool(rawParams.mpeSendSetupMessages, true);
    config.pitchMode = juce::jlimit(0, 1, rawParamInt(rawParams.mpePitchMode));
    return config;
}

CrowdTimeField::Config AudienceProcessor::buildTimeFieldConfig (
    const MpeMidiOutput::MpeConfig& midiConfig) const noexcept
{
    static constexpr int spreadValues[] { 1, 2, 4, 8, 16 };

    CrowdTimeField::Config config;
    config.mode = static_cast<CrowdTimeField::Mode>(
        juce::jlimit(0, 2, rawParamInt(rawParams.timeMode, 2)));
    config.clockSource = static_cast<CrowdTimeField::ClockSource>(
        juce::jlimit(0, 1, rawParamInt(rawParams.clockSource)));
    config.division = static_cast<CrowdTimeField::Division>(
        juce::jlimit(0, 3, rawParamInt(rawParams.gridDivision, 2)));
    config.internalBpm = juce::jlimit(40.0, 240.0,
                                      (double) rawParamValue(rawParams.internalBpm, 120.0f));
    config.maxAttacksPerStep = juce::jlimit(1, 16,
                                            rawParamInt(rawParams.maxAttacksPerStep, 4));
    config.maxActive = juce::jlimit(1, 16,
                                    rawParamInt(rawParams.maxActiveVoices, 16));
    if (midiConfig.outputType == 2)
        config.maxActive = juce::jmin(config.maxActive, 15);
    config.gatePercent = juce::jlimit(5.0, 100.0,
                                      (double) rawParamValue(rawParams.gatePercent, 70.0f));
    config.spreadSlots = spreadValues[juce::jlimit(0, 4,
                                                   rawParamInt(rawParams.temporalSpread, 2))];
    // Different zone instances commonly reuse the same participant IDs. Fold
    // the stable UDP endpoint into Ensemble's lane phase so those zones do not
    // all attack on the same host tick.
    config.laneSeed = static_cast<std::uint32_t>(getUdpPort());
    return CrowdTimeField::sanitiseConfig(config);
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
            const auto bpm = position->getBpm();
            const auto ppq = position->getPpqPosition();
            if (bpm.hasValue() && ppq.hasValue()
                && std::isfinite(*bpm) && *bpm > 0.0
                && std::isfinite(*ppq))
            {
                frame.hostValid = true;
                frame.bpm = *bpm;
                frame.ppqPosition = *ppq;
                frame.isPlaying = position->getIsPlaying();
            }
        }
    }

    return frame;
}

void AudienceProcessor::rehydrateTimeFieldFromCanonical() noexcept
{
    int heldCount = 0;
    for (int voice = 0; voice < CrowdTimeField::kMaxVoices; ++voice)
    {
        const int sourceId = CrowdTimeField::sourceIdForVoice(voice);
        const int finger = voice % CrowdTimeField::kFingersPerSource;
        const auto canonical = audienceModel.getFingerSnapshot(sourceId, finger);
        auto& midiState = fingerMidiStates[(size_t) voice];
        midiState = {};
        midiState.x = canonical.x;
        midiState.y = canonical.y;

        if (canonical.active && heldCount < (int) timeFieldHeldScratch.size())
            timeFieldHeldScratch[(size_t) heldCount++] = { voice, sourceId };
    }

    crowdTimeField.rehydrate(timeFieldHeldScratch.data(), heldCount);
    timeFieldRehydratePending = false;
}

void AudienceProcessor::renderOutgoingMidi (juce::MidiBuffer& midiMessages,
                                            int numSamples,
                                            bool outputEnabled,
                                            const MpeMidiOutput::MpeConfig& config,
                                            const CrowdTimeField::Config& timeConfig,
                                            const CrowdTimeField::ClockFrame& clockFrame,
                                            bool resetAlreadyEmitted)
{
    if (timeConfig.mode != CrowdTimeField::Mode::Flow)
    {
        renderTimedOutgoingMidi(midiMessages, numSamples, outputEnabled,
                                config, timeConfig, clockFrame,
                                resetAlreadyEmitted);
        return;
    }

    if (timeFieldRehydratePending)
    {
        crowdTimeField.reset();
        timeFieldRehydratePending = false;
    }
    timeFieldPending.store(0, std::memory_order_relaxed);
    timeFieldActive.store(audienceModel.getActiveFingerCount(), std::memory_order_relaxed);
    timeFieldMerged.store(0, std::memory_order_relaxed);
    const bool hostLocked = clockFrame.hostValid && clockFrame.isPlaying;
    const bool useHost = timeConfig.clockSource == CrowdTimeField::ClockSource::Host;
    timeFieldClockLocked.store(useHost ? hostLocked : true, std::memory_order_relaxed);
    timeFieldBpm.store(useHost && hostLocked ? clockFrame.bpm : timeConfig.internalBpm,
                       std::memory_order_relaxed);

    // Bound semantic work by both the fixed scratch capacity and the current
    // audio deadline. Tiny host blocks (including pluginval's 1-sample case)
    // must not be asked to emit a 64-voice MPE burst.
    const int blockLifecycleBudget = juce::jlimit(1, midiLifecycleEventBudget,
                                                   juce::jmax(1, numSamples));
    int noteEventCount = 0;

    auto appendEvent = [this, &noteEventCount, outputEnabled]
                       (const MpeMidiOutput::NoteEvent& event) noexcept
    {
        if (outputEnabled && noteEventCount < (int) midiNoteEventScratch.size())
            midiNoteEventScratch[(size_t) noteEventCount++] = event;
    };

    const bool reset = fingerRouter.takeResetRequest();
    if (reset)
    {
        fingerRouter.discardPendingEvents();
        bool hasHeldFinger = false;
        for (int voice = 0; voice < (int) fingerMidiStates.size(); ++voice)
        {
            const int sourceId = voice / OscFingerRouter::MAX_FINGERS;
            const int finger = voice % OscFingerRouter::MAX_FINGERS;
            const auto canonical = audienceModel.getFingerSnapshot(sourceId, finger);
            auto& state = fingerMidiStates[(size_t) voice];
            state = {};
            state.x = canonical.x;
            state.y = canonical.y;
            state.active = canonical.active;
            hasHeldFinger = hasHeldFinger || canonical.active;
        }
        // A reset is a transport boundary, not deletion of authoritative OSC
        // state. Rehydrate every held finger from the lock-free control ledger
        // on following blocks. Events published after discard remain queued and
        // will be applied normally, so clear/on races cannot lose a touch.
        retriggerFingerMidi = hasHeldFinger;
        fingerRetriggerCursor = 0;

        if (! resetAlreadyEmitted)
        {
            MpeMidiOutput::NoteEvent allOff;
            allOff.type = MpeMidiOutput::NoteEvent::AllNotesOff;
            appendEvent(allOff);
        }
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
                             int sampleOffset = 0) noexcept
    {
        MpeMidiOutput::NoteEvent event;
        event.type = type;
        event.sourceId = sourceId * OscFingerRouter::MAX_FINGERS + finger;
        event.participantId = sourceId;
        event.sampleOffset = sampleOffset;
        event.frequencyHz = state.frequencyHz;
        event.velocity = state.y;
        event.x = state.x;
        event.y = state.y;
        appendEvent(event);
    };

    if (! reset)
    {
        const int count = fingerRouter.drain(fingerEventScratch.data(),
                                             blockLifecycleBudget);
        for (int i = 0; i < count; ++i)
        {
            const auto& event = fingerEventScratch[(size_t) i];
            const int sourceId = (int) event.sourceId;
            const int finger = (int) event.finger;
            if (sourceId < 0 || sourceId >= OscFingerRouter::MAX_SOURCES
                || finger < 0 || finger >= OscFingerRouter::MAX_FINGERS)
                continue;

            auto& state = fingerMidiStates[(size_t) (sourceId * OscFingerRouter::MAX_FINGERS + finger)];
            switch ((OscFingerRouter::Event::Type) event.type)
            {
                case OscFingerRouter::Event::X:
                {
                    state.x = juce::jlimit(0.0f, 1.0f, event.value);
                    if (state.active)
                    {
                        const int previousPitch = state.pitchKey;
                        resolvePitch(state.x, state);
                        appendFinger(state.pitchKey != previousPitch
                                         ? MpeMidiOutput::NoteEvent::NoteOn
                                         : MpeMidiOutput::NoteEvent::Expression,
                                     sourceId, finger, state);
                    }
                    break;
                }

                case OscFingerRouter::Event::Y:
                    state.y = juce::jlimit(0.0f, 1.0f, event.value);
                    if (state.active)
                        appendFinger(MpeMidiOutput::NoteEvent::Expression, sourceId, finger, state);
                    break;

                case OscFingerRouter::Event::On:
                    if (! state.active)
                    {
                        state.active = true;
                        resolvePitch(state.x, state);
                        appendFinger(MpeMidiOutput::NoteEvent::NoteOn, sourceId, finger, state);
                    }
                    break;

                case OscFingerRouter::Event::Off:
                    if (state.active)
                    {
                        appendFinger(MpeMidiOutput::NoteEvent::NoteOff, sourceId, finger, state);
                        state.active = false;
                        state.pitchKey = -1;
                    }
                    break;
            }
        }
    }

    // MPE voice stealing and same-offset MidiBuffer insertion both become
    // expensive for huge bursts. Keep one shared semantic budget for new OSC
    // lifecycle work and held-finger rehydration. The router retains the rest
    // in FIFO order for following callbacks, so NoteOn/Off order is preserved.
    if (outputEnabled && retriggerFingerMidi && ! reset
        && noteEventCount < blockLifecycleBudget)
    {
        const int remainingBudget = blockLifecycleBudget - noteEventCount;
        int emitted = 0;
        for (; fingerRetriggerCursor < (int) fingerMidiStates.size(); ++fingerRetriggerCursor)
        {
            auto& state = fingerMidiStates[(size_t) fingerRetriggerCursor];
            if (! state.active)
                continue;

            resolvePitch(state.x, state);
            appendFinger(MpeMidiOutput::NoteEvent::NoteOn,
                         fingerRetriggerCursor / OscFingerRouter::MAX_FINGERS,
                         fingerRetriggerCursor % OscFingerRouter::MAX_FINGERS,
                         state);
            if (++emitted >= remainingBudget)
            {
                ++fingerRetriggerCursor;
                break;
            }
        }

        if (fingerRetriggerCursor >= (int) fingerMidiStates.size())
        {
            retriggerFingerMidi = false;
            fingerRetriggerCursor = 0;
        }
    }

    if (outputEnabled && noteEventCount > 0)
        mpeOut.render(config, midiNoteEventScratch.data(), noteEventCount,
                      midiMessages, numSamples);
}

void AudienceProcessor::renderTimedOutgoingMidi (
    juce::MidiBuffer& midiMessages, int numSamples, bool outputEnabled,
    const MpeMidiOutput::MpeConfig& midiConfig,
    const CrowdTimeField::Config& timeConfig,
    const CrowdTimeField::ClockFrame& clockFrame, bool resetAlreadyEmitted)
{
    const int blockBudget = juce::jlimit(1, midiLifecycleEventBudget,
                                         juce::jmax(1, numSamples));
    int midiEventCount = 0;

    auto appendMidi = [this, &midiEventCount, outputEnabled]
                      (const MpeMidiOutput::NoteEvent& event) noexcept
    {
        if (outputEnabled && midiEventCount < (int) midiNoteEventScratch.size())
            midiNoteEventScratch[(size_t) midiEventCount++] = event;
    };

    auto appendAllOff = [&]() noexcept
    {
        MpeMidiOutput::NoteEvent event;
        event.type = MpeMidiOutput::NoteEvent::AllNotesOff;
        appendMidi(event);
    };

    auto primeAndRehydrate = [&]() noexcept
    {
        crowdTimeField.reset();
        CrowdTimeField::OutputBlock prime;
        crowdTimeField.process(timeConfig, clockFrame, nullptr, 0, prime);
        rehydrateTimeFieldFromCanonical();
        timeFieldBpm.store(prime.effectiveBpm, std::memory_order_relaxed);
        timeFieldClockLocked.store(prime.clockLocked, std::memory_order_relaxed);
        timeFieldPending.store(crowdTimeField.getPendingCount(), std::memory_order_relaxed);
        timeFieldActive.store(crowdTimeField.getActiveCount(), std::memory_order_relaxed);
        timeFieldMerged.store(crowdTimeField.getMergedCount(), std::memory_order_relaxed);
    };

    const bool routerReset = fingerRouter.takeResetRequest();
    if (routerReset || timeFieldRehydratePending)
    {
        fingerRouter.discardPendingEvents();
        if (! resetAlreadyEmitted)
            appendAllOff();
        primeAndRehydrate();
        if (midiEventCount > 0)
            mpeOut.render(midiConfig, midiNoteEventScratch.data(), midiEventCount,
                          midiMessages, numSamples);
        return;
    }

    const int drained = fingerRouter.drain(fingerEventScratch.data(), blockBudget);
    int inputCount = 0;
    for (int index = 0; index < drained; ++index)
    {
        const auto& incoming = fingerEventScratch[(size_t) index];
        const auto type = (OscFingerRouter::Event::Type) incoming.type;
        if (type != OscFingerRouter::Event::On && type != OscFingerRouter::Event::Off)
            continue;

        const int sourceId = (int) incoming.sourceId;
        const int finger = (int) incoming.finger;
        if (sourceId < 0 || sourceId >= CrowdTimeField::kMaxSources
            || finger < 0 || finger >= CrowdTimeField::kFingersPerSource)
            continue;

        auto& event = timeFieldInputScratch[(size_t) inputCount++];
        event.type = type == OscFingerRouter::Event::On
                   ? CrowdTimeField::InputEvent::Type::On
                   : CrowdTimeField::InputEvent::Type::Off;
        event.voiceId = CrowdTimeField::voiceIdFor(sourceId, finger);
        event.sourceId = sourceId;
        event.sampleOffset = 0;
    }

    crowdTimeField.process(timeConfig, clockFrame,
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
        fingerRouter.discardPendingEvents();
        if (! resetAlreadyEmitted)
            appendAllOff();
        primeAndRehydrate();
        if (midiEventCount > 0)
            mpeOut.render(midiConfig, midiNoteEventScratch.data(), midiEventCount,
                          midiMessages, numSamples);
        return;
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
        appendMidi(event);
    };

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

    if (midiEventCount > 0)
        mpeOut.render(midiConfig, midiNoteEventScratch.data(), midiEventCount,
                      midiMessages, numSamples);
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
                                                   int numSamples) noexcept
{
    if (midiOutputOptionIndex.load(std::memory_order_relaxed) <= 0
        || externalMidiProducerQuarantined.load(std::memory_order_acquire))
        return;

    const double safeBlockStart = std::isfinite(blockStartTimeMs)
                                ? blockStartTimeMs : juce::Time::getMillisecondCounterHiRes();
    const double millisecondsPerSample = 1000.0 / currentSampleRate;
    const int lastSample = juce::jmax(0, numSamples - 1);

    for (const auto metadata : midiMessages)
    {
        if (externalMidiProducerQuarantined.load(std::memory_order_acquire))
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
            externalMidiProducerQuarantined.store(true, std::memory_order_release);
            externalMidiPanicPending.store(true, std::memory_order_release);
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
        externalMidiFifo.finishedWrite(1);
    }
}

void AudienceProcessor::drainExternalMidiOutputQueue()
{
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    for (;;)
    {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        externalMidiFifo.prepareToRead(1, start1, size1, start2, size2);
        if (size1 <= 0 && size2 <= 0)
            return;

        const int slotIndex = size1 > 0 ? start1 : start2;
        const auto& event = externalMidiEvents[(size_t) slotIndex];
        if (event.dueTimeMs > nowMs)
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
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    externalMidiFifo.prepareToRead(available, start1, size1, start2, size2);
    externalMidiFifo.finishedRead(size1 + size2);
}

void AudienceProcessor::sendExternalResetSweep()
{
    if (midiOutput == nullptr)
        return;

    for (int channel = 1; channel <= 16; ++channel)
    {
        midiOutput->sendMessageNow(juce::MidiMessage::channelPressureChange(channel, 0));
        midiOutput->sendMessageNow(juce::MidiMessage::pitchWheel(channel, 8192));
        midiOutput->sendMessageNow(juce::MidiMessage::allNotesOff(channel));
        midiOutput->sendMessageNow(juce::MidiMessage::allSoundOff(channel));
    }
}

void AudienceProcessor::timerCallback()
{
    // Processor-owned timer runs for the full plug-in lifetime, including when
    // the editor is closed. Three missing 1 Hz phone heartbeats synthesize one
    // ordered Off, preventing a disconnected client from holding a note forever.
    audienceModel.expireStaleLiveTouches();

    int restoredPort = 6060;
    int restoredOutput = 0;
    int restoredRouteKind = -1;
    juce::String restoredDeviceIdentifier;
    bool applyState = false;
    {
        const juce::ScopedLock lock(pendingStateLock);
        applyState = pendingStateApply.exchange(false, std::memory_order_acquire);
        if (applyState)
        {
            restoredPort = pendingUdpPort;
            restoredOutput = pendingMidiOutputOption;
            restoredRouteKind = pendingMidiOutputRouteKind;
            restoredDeviceIdentifier = pendingMidiOutputDeviceIdentifier;
        }
    }

    if (applyState)
    {
        setUdpPort(restoredPort);
        restoreMidiOutputRoute(restoredRouteKind, restoredDeviceIdentifier, restoredOutput);
    }
}

void AudienceProcessor::hiResTimerCallback()
{
    const bool panicPending = externalMidiPanicPending.exchange(false, std::memory_order_acq_rel);
    const bool transportResetPending = externalTransportResetPending.exchange(false, std::memory_order_acq_rel);
    if (panicPending || transportResetPending)
    {
        // Panic/transport boundaries invalidate every queued packet, including
        // future-offset events. Discard before all-off so no pre-boundary NoteOn
        // can be emitted on a later timer tick after the reset.
        discardExternalMidiOutputQueue();
        sendExternalResetSweep();
        externalMidiProducerQuarantined.store(false, std::memory_order_release);
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
    const int element = rawParamInt(rawParams.spectralElement, 1);
    const int mode = rawParamInt(rawParams.atomicScaleMode, 1);
    return AtomicScaleCatalog::instance().getDegreeCount(element, mode);
}

double AudienceProcessor::getSelectedAtomicReferenceWavelengthNm() const noexcept
{
    const int element = rawParamInt(rawParams.spectralElement, 1);
    const int mode = rawParamInt(rawParams.atomicScaleMode, 1);
    return AtomicScaleCatalog::instance().getReferenceWavelengthNm(element, mode);
}

juce::String AudienceProcessor::getMidiOutputStatus() const
{
    return midiOutputStatus;
}

juce::String AudienceProcessor::getMidiOutputDescription() const
{
    if (midiOutputOptionIndex.load(std::memory_order_relaxed) == 0)
        return "Host MIDI Output | use the port output for channel-separated Ableton routing";
    return midiOutputStatus + " | host MIDI output also remains available";
}

int AudienceProcessor::getResolvedMidiOutputOptionIndex()
{
    int routeKind = 0;
    juce::String deviceIdentifier;
    {
        const juce::ScopedLock lock(pendingStateLock);
        routeKind = midiOutputRouteKind;
        deviceIdentifier = midiOutputDeviceIdentifier;
    }

    if (routeKind == 0)
    {
        midiOutputOptionIndex.store(0, std::memory_order_relaxed);
        return 0;
    }
    if (routeKind == 1)
    {
        midiOutputOptionIndex.store(1, std::memory_order_relaxed);
        return 1;
    }

    if (routeKind == 2 && deviceIdentifier.isNotEmpty())
    {
        const auto devices = juce::MidiOutput::getAvailableDevices();
        for (int i = 0; i < devices.size(); ++i)
            if (devices[i].identifier == deviceIdentifier)
            {
                midiOutputOptionIndex.store(i + 2, std::memory_order_relaxed);
                return i + 2;
            }
    }

    // Do not let a stale numeric device index highlight an unrelated endpoint.
    return -1;
}

void AudienceProcessor::setMidiOutputOptionIndex (int index)
{
    midiOutputRouteRevision.fetch_add(1, std::memory_order_release);
    juce::HighResolutionTimer::stopTimer();
    ScopedProcessorSuspension processingGuard(*this);
    discardExternalMidiOutputQueue();
    sendImmediateAllNotesOffToExternal(true);
    externalMidiFifo.reset();
    externalMidiPanicPending.store(false, std::memory_order_release);
    externalMidiProducerQuarantined.store(false, std::memory_order_release);
    externalTransportResetPending.store(false, std::memory_order_release);
    midiOutput.reset();
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
            midiOutputStatus = "Virtual port: " + portName;
            const juce::ScopedLock lock(pendingStateLock);
            midiOutputRouteKind = 1;
            midiOutputDeviceIdentifier.clear();
            juce::HighResolutionTimer::startTimer(2);
        }
        else
        {
            midiOutputStatus = "Virtual MIDI port unavailable";
            midiOutputOptionIndex.store(0, std::memory_order_relaxed);
            const juce::ScopedLock lock(pendingStateLock);
            midiOutputRouteKind = 0;
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
            midiOutputStatus = "MIDI Output: " + devices[deviceIndex].name;
            const juce::ScopedLock lock(pendingStateLock);
            midiOutputRouteKind = 2;
            midiOutputDeviceIdentifier = devices[deviceIndex].identifier;
            juce::HighResolutionTimer::startTimer(2);
        }
        else
        {
            midiOutputStatus = "Failed to open MIDI output";
            midiOutputOptionIndex.store(0, std::memory_order_relaxed);
            const juce::ScopedLock lock(pendingStateLock);
            midiOutputRouteKind = 0;
            midiOutputDeviceIdentifier.clear();
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
        setMidiOutputOptionIndex(legacyOptionIndex);
        return;
    }

    if (routeKind == 0)
    {
        setMidiOutputOptionIndex(0);
        return;
    }

    if (routeKind == 1)
    {
        setMidiOutputOptionIndex(1);
        return;
    }

    if (routeKind == 2 && deviceIdentifier.isNotEmpty())
    {
        const auto devices = juce::MidiOutput::getAvailableDevices();
        for (int i = 0; i < devices.size(); ++i)
        {
            if (devices[i].identifier == deviceIdentifier)
            {
                setMidiOutputOptionIndex(i + 2);
                return;
            }
        }
    }

    setMidiOutputOptionIndex(0);
    midiOutputStatus = "Saved MIDI output is unavailable; using Host MIDI Output";
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
        midiOutput->sendMessageNow(juce::MidiMessage::channelPressureChange(channel, 0));
        midiOutput->sendMessageNow(juce::MidiMessage::pitchWheel(channel, 8192));
        midiOutput->sendMessageNow(juce::MidiMessage::allNotesOff(channel));
        midiOutput->sendMessageNow(juce::MidiMessage::allSoundOff(channel));
    }
    mpeOut.reset();

    if (shouldResume)
        suspendProcessing(false);
}

void AudienceProcessor::closeMidiOutput()
{
    midiOutputRouteRevision.fetch_add(1, std::memory_order_release);
    juce::HighResolutionTimer::stopTimer();
    sendImmediateAllNotesOffToExternal();
    midiOutput.reset();
    midiOutputOptionIndex.store(0, std::memory_order_relaxed);
    midiOutputStatus = "Host MIDI Output";
    const juce::ScopedLock lock(pendingStateLock);
    midiOutputRouteKind = 0;
    midiOutputDeviceIdentifier.clear();
}

void AudienceProcessor::setUdpPort (int port)
{
    const int safePort = juce::jlimit(1, 65535, port);
    const int previousPort = getUdpPort();
    if (safePort == previousPort && osc.isReceiving())
    {
        oscStatus = osc.oscStatus();
        return;
    }

    const bool reopenVirtualPort = midiOutputOptionIndex.load(std::memory_order_relaxed) == 1;
    const bool needsReset = osc.isRunning() || safePort != previousPort;
    osc.stop();
    if (needsReset)
        panic();
    udpPort.store(safePort, std::memory_order_relaxed);
    osc.start(safePort);
    oscStatus = osc.oscStatus();

    if (reopenVirtualPort)
        setMidiOutputOptionIndex(1);
}

void AudienceProcessor::panic()
{
    juce::HighResolutionTimer::stopTimer();
    ScopedProcessorSuspension processingGuard(*this);
    simulator.clearSilently();
    releaseAllIncomingMidiNotes();
    audienceModel.clear();
    crowdTimeField.reset();
    timeFieldRehydratePending = true;
    // Drop packets produced before the panic while the audio producer is
    // quiescent. Otherwise an old NoteOn could be drained after the immediate
    // all-off sweep and recreate a stuck external note.
    externalMidiFifo.reset();
    externalMidiPanicPending.store(false, std::memory_order_release);
    externalMidiProducerQuarantined.store(false, std::memory_order_release);
    externalTransportResetPending.store(false, std::memory_order_release);
    sendImmediateAllNotesOffToExternal(true);
    if (midiOutput != nullptr && midiOutputOptionIndex.load(std::memory_order_relaxed) > 0)
        juce::HighResolutionTimer::startTimer(2);
}

void AudienceProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    auto state = apvts.copyState();
    state.setProperty("udpPort", getUdpPort(), nullptr);
    state.setProperty("midiOutputOption", midiOutputOptionIndex.load(std::memory_order_relaxed), nullptr);
    {
        const juce::ScopedLock lock(pendingStateLock);
        state.setProperty("midiOutputRouteKind", midiOutputRouteKind, nullptr);
        state.setProperty("midiOutputDeviceIdentifier", midiOutputDeviceIdentifier, nullptr);
    }
    state.setProperty("cosmicMicrowaveSchema", CosmicStateMigration::currentSchema, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destination);
}

void AudienceProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
    {
        auto state = juce::ValueTree::fromXml(*xml);
        if (! state.isValid())
            return;

        CosmicStateMigration::migrate(state);

        const juce::ScopedLock lock(pendingStateLock);
        apvts.replaceState(state);
        pendingUdpPort = boundedStateInt(state.getProperty("udpPort", 6060),
                                         1, 65535, 6060);
        pendingMidiOutputOption = boundedStateInt(
            state.getProperty("midiOutputOption", 0),
            0, std::numeric_limits<int>::max(), 0);
        pendingMidiOutputRouteKind = boundedStateInt(
            state.getProperty("midiOutputRouteKind", -1), -1, 2, -1);
        pendingMidiOutputDeviceIdentifier = state.getProperty("midiOutputDeviceIdentifier").toString();
        pendingStateApply.store(true, std::memory_order_release);
    }
}

juce::String AudienceProcessor::getOutgoingMidiDebugText (int maxEvents) const
{
    juce::String text;
    text << "outgoing MIDI\n";
    text << "destination : " << getMidiOutputStatus() << "\n";
    text << "notes sent  : " << getMidiNotesSent() << "\n";
    text << "active MPE  : " << getActiveMpeVoices() << "\n";
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
    text << "incoming MIDI / thru\n";
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
    text << "active sources : " << audienceModel.getActiveSourceCount() << "\n";
    text << "active touches : " << audienceModel.getActiveFingerCount() << "\n";
    text << "MIDI output    : " << getMidiOutputStatus() << "\n";
    text << "dropped output : " << (int) externalMidiDropped.load(std::memory_order_relaxed) << "\n";
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
