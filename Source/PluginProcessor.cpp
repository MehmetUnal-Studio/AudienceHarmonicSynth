#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <limits>

#ifndef AUDIENCE_SYNTH_SOURCE_SAMPLES_PATH
#define AUDIENCE_SYNTH_SOURCE_SAMPLES_PATH ""
#endif

namespace
{
    std::atomic<int> nextSynthInstanceId { 1 };

    juce::File getBundledSamplesDirectory()
    {
        return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
            .getParentDirectory()
            .getParentDirectory()
            .getChildFile("Resources/Samples");
    }

    float rawParamValue (const std::atomic<float>* param, float fallback = 0.0f) noexcept
    {
        return param != nullptr ? param->load(std::memory_order_relaxed) : fallback;
    }

    int rawParamInt (const std::atomic<float>* param, int fallback = 0) noexcept
    {
        return (int) rawParamValue(param, (float) fallback);
    }

    bool rawParamBool (const std::atomic<float>* param, bool fallback = false) noexcept
    {
        return rawParamValue(param, fallback ? 1.0f : 0.0f) > 0.5f;
    }
}

AudienceProcessor::AudienceProcessor()
    : juce::AudioProcessor (BusesProperties()
	                              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
	      apvts (*this, nullptr, "PARAMS", createLayout()),
	      seatRouter (engine),
	      osc (seatRouter),
	      simulator (seatRouter)
{
    instanceId = nextSynthInstanceId.fetch_add(1, std::memory_order_relaxed);
    cacheParameterPointers();
    midiKeyToKeyboardSlot.fill(-1);
    keyboardSlotToMidiKey.fill(-1);
    for (auto& key : keyboardDebugKeys)
        key.store(-1, std::memory_order_relaxed);
    resetMidiOutputState();
    setUdpPort(udpPort);
    startTimerHz(60);

    const std::initializer_list<juce::File> rootCandidates {
        getBundledSamplesDirectory(),
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Audience Harmonic Synth/Samples"),
        juce::File(AUDIENCE_SYNTH_SOURCE_SAMPLES_PATH)
    };
    for (auto& c : rootCandidates)
        if (c.isDirectory()) { libraryRoot = c; break; }

    rescanLibraryRoot();

    // pick the first available library (or fall back to the root itself if it
    // still has loose .wav files lying around - backward compatibility)
    const auto libs = getAvailableLibraries();
    if (! libs.isEmpty())
        setCurrentLibrary(libs[0]);
    else if (libraryRoot.isDirectory())
        setSampleDirectory(libraryRoot);
}

void AudienceProcessor::rescanLibraryRoot()
{
    if (libraryRoot.isDirectory())
        return;

    const std::initializer_list<juce::File> rootCandidates {
        getBundledSamplesDirectory(),
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Audience Harmonic Synth/Samples"),
        juce::File(AUDIENCE_SYNTH_SOURCE_SAMPLES_PATH)
    };

    for (auto& c : rootCandidates)
    {
        if (c.isDirectory())
        {
            libraryRoot = c;
            return;
        }
    }

    librariesStatus = "Samples folder not found";
}

juce::StringArray AudienceProcessor::getAvailableLibraries() const
{
    juce::StringArray names;
    if (! libraryRoot.isDirectory()) return names;
    juce::Array<juce::File> subs;
    libraryRoot.findChildFiles(subs, juce::File::findDirectories, false);
    for (auto& d : subs)
    {
        if (d.getFileName().startsWith(".")) continue;
        names.add(d.getFileName());
    }
    names.sortNatural();
    return names;
}

void AudienceProcessor::setCurrentLibrary (const juce::String& libraryName)
{
    const auto dir = libraryRoot.getChildFile(libraryName);
    if (! dir.isDirectory()) return;
    currentLibraryName = libraryName;
    setSampleDirectory(dir);
}

AudienceProcessor::~AudienceProcessor()
{
    stopTimer();
    sendImmediateAllNotesOffToExternal();
    closeMidiOutput();
    osc.stop();
}

juce::AudioProcessorValueTreeState::ParameterLayout AudienceProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat>(
        ParameterID ("pitch", 1), "Pitch (semitones)",
        NormalisableRange<float>(-12.0f, 12.0f, 0.01f), 0.0f));

    layout.add (std::make_unique<AudioParameterFloat>(
        ParameterID ("layerMix", 1), "Layer Mix",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.7f));

    layout.add (std::make_unique<AudioParameterFloat>(
        ParameterID ("attack", 1), "Attack",
        NormalisableRange<float>(10.0f, 3000.0f, 1.0f, 0.4f), 800.0f));

    layout.add (std::make_unique<AudioParameterFloat>(
        ParameterID ("release", 1), "Release",
        NormalisableRange<float>(100.0f, 6000.0f, 1.0f, 0.4f), 2500.0f));

    layout.add (std::make_unique<AudioParameterFloat>(
        ParameterID ("brightness", 1), "Brightness",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.6f));

    layout.add (std::make_unique<AudioParameterFloat>(
        ParameterID ("movement", 1), "Movement (Grain)",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.45f));

    layout.add (std::make_unique<AudioParameterFloat>(
        ParameterID ("reverb", 1), "Reverb",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.35f));

    layout.add (std::make_unique<AudioParameterFloat>(
        ParameterID ("delay", 1), "Delay",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.25f));

    layout.add (std::make_unique<AudioParameterFloat>(
        ParameterID ("master", 1), "Master",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.7f));

    layout.add (std::make_unique<AudioParameterFloat>(
        ParameterID ("energy", 1), "Energy",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));

    layout.add (std::make_unique<AudioParameterFloat>(
        ParameterID ("motionMacro", 1), "Motion",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));

    layout.add (std::make_unique<AudioParameterFloat>(
        ParameterID ("toneMacro", 1), "Tone",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));

    layout.add (std::make_unique<AudioParameterFloat>(
        ParameterID ("spaceMacro", 1), "Space",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));

    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID ("signatureMode", 1), "Signature Mode",
        StringArray { "Choir Cloud", "Glass Harmonics", "Sub Swarm", "Spectral Rain", "Frozen Hall" }, 0));

    layout.add (std::make_unique<AudioParameterFloat>(
        ParameterID ("grainSize", 1), "Grain Size",
        NormalisableRange<float>(40.0f, 800.0f, 1.0f, 0.55f), 260.0f));

    layout.add (std::make_unique<AudioParameterFloat>(
        ParameterID ("grainDensity", 1), "Grain Density",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.55f));

    layout.add (std::make_unique<AudioParameterFloat>(
        ParameterID ("pitchSpread", 1), "Pitch Spread",
        NormalisableRange<float>(0.0f, 12.0f, 0.01f, 0.45f), 0.0f));

    layout.add (std::make_unique<AudioParameterFloat>(
        ParameterID ("positionJitter", 1), "Position Jitter",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.35f));

    layout.add (std::make_unique<AudioParameterFloat>(
        ParameterID ("stereoSpread", 1), "Stereo Spread",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.45f));

    layout.add (std::make_unique<AudioParameterBool>(
        ParameterID ("reverseGrains", 1), "Reverse Grains", false));

    layout.add (std::make_unique<AudioParameterBool>(
        ParameterID ("freeze", 1), "Freeze", false));

    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID ("grainShape", 1), "Grain Envelope",
        StringArray { "Hann", "Triangle", "Soft Gate", "Pulse" }, 0));

    layout.add (std::make_unique<AudioParameterFloat>(
        ParameterID ("wetDry", 1), "Wet Dry",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.85f));

    layout.add (std::make_unique<AudioParameterFloat>(
        ParameterID ("tapeDrive", 1), "Tape Drive",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID ("polyphonyMode", 1), "Polyphony Mode",
        StringArray { "Normal", "High", "Ultra" }, 0));

    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID ("engineSource", 1), "Sound Engine",
        StringArray { "Sample Library", "Element Spectral Synth" }, 0));

    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID ("samplePlaybackMode", 1), "Sample Playback",
        StringArray { "Sample Player", "Granular" }, 0));

    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID ("audioMidiOutputMode", 1), "Audio MIDI Output Mode",
        StringArray { "Audio Only", "MIDI Only", "Audio + MIDI" }, 0));

    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID ("midiOutputType", 1), "MIDI Output Type",
        StringArray { "Off", "Normal MIDI", "MPE MIDI" }, 0));

    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID ("externalMidiPitchMode", 1), "External MIDI Pitch Mode",
        StringArray { "Direct MIDI Pitch", "Quantize To Current Scale", "Use As Trigger For Audience Pitch" }, 0));

    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID ("mpeZone", 1), "MPE Zone",
        StringArray { "Lower" }, 0));

    layout.add (std::make_unique<AudioParameterInt>(
        ParameterID ("normalMidiChannel", 1), "Normal MIDI Channel", 1, 16, 1));

    layout.add (std::make_unique<AudioParameterInt>(
        ParameterID ("mpeMasterChannel", 1), "MPE Master Channel", 1, 16, 1));

    layout.add (std::make_unique<AudioParameterInt>(
        ParameterID ("mpeMemberFirstChannel", 1), "MPE First Member Channel", 2, 16, 2));

    layout.add (std::make_unique<AudioParameterInt>(
        ParameterID ("mpeMemberLastChannel", 1), "MPE Last Member Channel", 2, 16, 16));

    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID ("mpePitchBendRange", 1), "MPE Pitch Bend Range",
        StringArray { "2 st", "12 st", "24 st", "48 st" }, 3));

    layout.add (std::make_unique<AudioParameterBool>(
        ParameterID ("mpeSendSetupMessages", 1), "MPE Send Setup Messages", true));

    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID ("mpePitchMode", 1), "MPE Pitch Mode",
        StringArray { "Retrigger", "Glide" }, 0));

    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID ("spectralElement", 1), "Element",
        StringArray { "Hydrogen", "Helium", "Lithium", "Beryllium",
                      "Boron", "Carbon", "Oxygen", "Fluorine", "Neon",
                      "Sodium", "Magnesium", "Aluminium", "Silicon", "Phosphorus",
                      "Sulfur", "Chlorine", "Argon", "Potassium", "Calcium",
                      "Scandium", "Titanium", "Vanadium", "Chromium", "Manganese",
                      "Iron", "Cobalt", "Nickel", "Copper", "Zinc" }, 1));

    layout.add (std::make_unique<AudioParameterInt>(
        ParameterID ("spectralPartialCount", 1), "Element Partial", 1, PartialEngine::MAX_ELEMENT_PARTIALS, 1));

    layout.add (std::make_unique<AudioParameterBool>(
        ParameterID ("spectralPartialSolo", 1), "Partial Solo", false));

    layout.add (std::make_unique<AudioParameterFloat>(
        ParameterID ("spectralStretch", 1), "Spectral Stretch",
        NormalisableRange<float>(-0.35f, 0.35f, 0.001f), 0.0f));

    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID ("atomicScaleMode", 1), "Atomic Scale Mode",
        StringArray { "Core", "Extended", "Microtonal", "Scientific", "Raw" }, 1));

    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID ("scaleRoot", 1), "Root",
        StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));

    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID ("scaleRootOctave", 1), "Root Octave",
        StringArray { "0", "1", "2", "3", "4", "5", "6" }, 2));

    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID ("scaleMode", 1), "Scale",
        StringArray { "Major", "Natural Minor", "Pentatonic", "Dorian", "Lydian", "Harmonic Minor", "Whole Tone",
                      "Hydrogen Spectrum", "Helium Spectrum", "Lithium Spectrum", "Beryllium Spectrum",
                      "Boron Spectrum", "Carbon Spectrum", "Oxygen Spectrum", "Fluorine Spectrum", "Neon Spectrum",
                      "Sodium Spectrum", "Magnesium Spectrum", "Aluminium Spectrum", "Silicon Spectrum",
                      "Phosphorus Spectrum", "Sulfur Spectrum", "Chlorine Spectrum", "Argon Spectrum",
                      "Potassium Spectrum", "Calcium Spectrum", "Scandium Spectrum", "Titanium Spectrum",
                      "Vanadium Spectrum", "Chromium Spectrum", "Manganese Spectrum",
                      "Iron Spectrum", "Cobalt Spectrum", "Nickel Spectrum", "Copper Spectrum",
                      "Zinc Spectrum" }, 0));

    layout.add (std::make_unique<AudioParameterInt>(
        ParameterID ("scaleOctaves", 1), "Octaves", 1, 6, 4));

    return layout;
}

void AudienceProcessor::cacheParameterPointers()
{
    rawParams.pitch = apvts.getRawParameterValue("pitch");
    rawParams.layerMix = apvts.getRawParameterValue("layerMix");
    rawParams.attack = apvts.getRawParameterValue("attack");
    rawParams.release = apvts.getRawParameterValue("release");
    rawParams.brightness = apvts.getRawParameterValue("brightness");
    rawParams.movement = apvts.getRawParameterValue("movement");
    rawParams.reverb = apvts.getRawParameterValue("reverb");
    rawParams.delay = apvts.getRawParameterValue("delay");
    rawParams.master = apvts.getRawParameterValue("master");
    rawParams.energy = apvts.getRawParameterValue("energy");
    rawParams.motionMacro = apvts.getRawParameterValue("motionMacro");
    rawParams.toneMacro = apvts.getRawParameterValue("toneMacro");
    rawParams.spaceMacro = apvts.getRawParameterValue("spaceMacro");
    rawParams.signatureMode = apvts.getRawParameterValue("signatureMode");
    rawParams.grainSize = apvts.getRawParameterValue("grainSize");
    rawParams.grainDensity = apvts.getRawParameterValue("grainDensity");
    rawParams.pitchSpread = apvts.getRawParameterValue("pitchSpread");
    rawParams.positionJitter = apvts.getRawParameterValue("positionJitter");
    rawParams.stereoSpread = apvts.getRawParameterValue("stereoSpread");
    rawParams.reverseGrains = apvts.getRawParameterValue("reverseGrains");
    rawParams.freeze = apvts.getRawParameterValue("freeze");
    rawParams.grainShape = apvts.getRawParameterValue("grainShape");
    rawParams.wetDry = apvts.getRawParameterValue("wetDry");
    rawParams.tapeDrive = apvts.getRawParameterValue("tapeDrive");
    rawParams.polyphonyMode = apvts.getRawParameterValue("polyphonyMode");
    rawParams.scaleRoot = apvts.getRawParameterValue("scaleRoot");
    rawParams.scaleRootOctave = apvts.getRawParameterValue("scaleRootOctave");
    rawParams.scaleMode = apvts.getRawParameterValue("scaleMode");
    rawParams.scaleOctaves = apvts.getRawParameterValue("scaleOctaves");
    rawParams.engineSource = apvts.getRawParameterValue("engineSource");
    rawParams.samplePlaybackMode = apvts.getRawParameterValue("samplePlaybackMode");
    rawParams.spectralElement = apvts.getRawParameterValue("spectralElement");
    rawParams.spectralPartialCount = apvts.getRawParameterValue("spectralPartialCount");
    rawParams.spectralPartialSolo = apvts.getRawParameterValue("spectralPartialSolo");
    rawParams.spectralStretch = apvts.getRawParameterValue("spectralStretch");
    rawParams.atomicScaleMode = apvts.getRawParameterValue("atomicScaleMode");
    rawParams.audioMidiOutputMode = apvts.getRawParameterValue("audioMidiOutputMode");
    rawParams.midiOutputType = apvts.getRawParameterValue("midiOutputType");
    rawParams.normalMidiChannel = apvts.getRawParameterValue("normalMidiChannel");
    rawParams.mpeMasterChannel = apvts.getRawParameterValue("mpeMasterChannel");
    rawParams.mpeMemberFirstChannel = apvts.getRawParameterValue("mpeMemberFirstChannel");
    rawParams.mpeMemberLastChannel = apvts.getRawParameterValue("mpeMemberLastChannel");
    rawParams.mpePitchBendRange = apvts.getRawParameterValue("mpePitchBendRange");
    rawParams.mpeSendSetupMessages = apvts.getRawParameterValue("mpeSendSetupMessages");
    rawParams.mpePitchMode = apvts.getRawParameterValue("mpePitchMode");
    rawParams.externalMidiPitchMode = apvts.getRawParameterValue("externalMidiPitchMode");
}

void AudienceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = juce::jmax(1.0, sampleRate);
    engine.prepare(sampleRate, samplesPerBlock);
    monoScratch.setSize(2, juce::jmax(samplesPerBlock, realtimeScratchBlockSize), false, false, true);
    midiRenderScratch.ensureSize(realtimeMidiBufferReserveBytes);
    midiRenderScratchLoanedToHost = false;
    resetMidiOutputState();
    mpeSetupDirty = true;
}

void AudienceProcessor::releaseResources() {}

bool AudienceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void AudienceProcessor::pullParams()
{
    const int rootPitchClass = rawParamInt(rawParams.scaleRoot);
    const int rootOctave = rawParamInt(rawParams.scaleRootOctave, 2);
    const int rootMidi = (rootOctave + 1) * 12 + rootPitchClass;
    const int scaleMode = rawParamInt(rawParams.scaleMode);
    const int octaves = rawParamInt(rawParams.scaleOctaves, 4);
    const int engineSource = rawParamInt(rawParams.engineSource);
    const int samplePlaybackMode = rawParamInt(rawParams.samplePlaybackMode);
    const int spectralElement = rawParamInt(rawParams.spectralElement, 1);
    const int atomicScaleMode = rawParamInt(rawParams.atomicScaleMode, 1);
    const float energy = rawParamValue(rawParams.energy, 0.5f);
    const float motion = rawParamValue(rawParams.motionMacro, 0.5f);
    const float tone = rawParamValue(rawParams.toneMacro, 0.5f);
    const float space = rawParamValue(rawParams.spaceMacro, 0.5f);

    engine.pitchSemitones.store(rawParamValue(rawParams.pitch));
    engine.layerMix      .store(rawParamValue(rawParams.layerMix, 0.7f));
    engine.attackMs      .store(rawParamValue(rawParams.attack, 800.0f));
    engine.releaseMs     .store(rawParamValue(rawParams.release, 2500.0f));
    engine.brightness    .store(rawParamValue(rawParams.brightness, 0.6f));
    engine.movement      .store(rawParamValue(rawParams.movement, 0.45f));
    engine.reverbAmount  .store(rawParamValue(rawParams.reverb, 0.35f));
    engine.delayAmount   .store(rawParamValue(rawParams.delay, 0.25f));
    engine.masterGain    .store(rawParamValue(rawParams.master, 0.7f));
    engine.energyMacro   .store(energy);
    engine.motionMacro   .store(motion);
    engine.toneMacro     .store(tone);
    engine.spaceMacro    .store(space);
    engine.signatureMode .store(rawParamInt(rawParams.signatureMode));
    engine.grainSizeMs   .store(rawParamValue(rawParams.grainSize, 260.0f));
    engine.grainDensity  .store(rawParamValue(rawParams.grainDensity, 0.55f));
    engine.pitchSpread   .store(rawParamValue(rawParams.pitchSpread));
    engine.positionJitter.store(rawParamValue(rawParams.positionJitter));
    engine.stereoSpread  .store(rawParamValue(rawParams.stereoSpread, 0.5f));
    engine.reverseGrains .store(rawParamBool(rawParams.reverseGrains) ? 1 : 0);
    engine.freeze        .store(rawParamBool(rawParams.freeze) ? 1 : 0);
    engine.grainShape    .store(rawParamInt(rawParams.grainShape));
    engine.wetDry        .store(rawParamValue(rawParams.wetDry, 0.5f));
    engine.tapeDrive     .store(rawParamValue(rawParams.tapeDrive));
    engine.polyphonyMode .store(rawParamInt(rawParams.polyphonyMode));
    engine.engineSource  .store(engineSource);
    engine.samplePlaybackMode.store(samplePlaybackMode);
    engine.spectralElement.store(spectralElement);
    engine.spectralPartialCount.store(rawParamInt(rawParams.spectralPartialCount, 1));
    engine.spectralPartialSolo.store(rawParamBool(rawParams.spectralPartialSolo) ? 1 : 0);
    engine.spectralStretch.store(rawParamValue(rawParams.spectralStretch));
    engine.atomicScaleMode.store(atomicScaleMode);
    engine.scaleRootMidi .store(rootMidi);
    engine.scaleMode     .store(scaleMode);
    engine.scaleOctaves  .store(octaves);

    const bool scaleChanged = lastScaleRoot >= 0
                           && (lastScaleRoot != rootMidi
                            || lastScaleMode != scaleMode
                            || lastScaleOctaves != octaves
                            || lastEngineSource != engineSource
                            || lastSamplePlaybackMode != samplePlaybackMode
                            || lastSpectralElement != spectralElement
                            || lastAtomicScaleMode != atomicScaleMode);

    if (scaleChanged)
    {
        releaseAllMidiKeyboardNotes();
        engine.requestRetuneActiveSeats();
    }

    lastScaleRoot = rootMidi;
    lastScaleMode = scaleMode;
    lastScaleOctaves = octaves;
    lastEngineSource = engineSource;
    lastSamplePlaybackMode = samplePlaybackMode;
    lastSpectralElement = spectralElement;
    lastAtomicScaleMode = atomicScaleMode;
}

void AudienceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    pullParams();
    processIncomingMidiKeyboard(midiMessages);

    const int outputMode = rawParamInt(rawParams.audioMidiOutputMode);
    const int midiType = rawParamInt(rawParams.midiOutputType);
    const bool renderAudio = outputMode != 1 && ! muted.load();
    const bool renderMidi = outputMode != 0 && midiType != 0;
    const int bendRange = bendRangeFromChoice(rawParamInt(rawParams.mpePitchBendRange, 3));
    const int mpeFirst = juce::jlimit(2, 16, rawParamInt(rawParams.mpeMemberFirstChannel, 2));
    const int mpeLast = juce::jlimit(mpeFirst, 16, rawParamInt(rawParams.mpeMemberLastChannel, 16));
    const int setupEnabled = rawParamBool(rawParams.mpeSendSetupMessages, true) ? 1 : 0;

    const bool midiConfigChanged = outputMode != lastAudioMidiOutputMode
        || midiType != lastMidiOutputType
        || bendRange != lastMpeBendRange
        || mpeFirst != lastMpeMemberFirst
        || mpeLast != lastMpeMemberLast
        || setupEnabled != lastMpeSetupEnabled;
    const bool needsSafetyAllOff = midiConfigChanged
        && (lastAudioMidiOutputMode != 0 && lastMidiOutputType != 0);

    lastAudioMidiOutputMode = outputMode;
    lastMidiOutputType = midiType;
    lastMpeBendRange = bendRange;
    lastMpeMemberFirst = mpeFirst;
    lastMpeMemberLast = mpeLast;
    lastMpeSetupEnabled = setupEnabled;

    if (midiConfigChanged)
        mpeSetupDirty = true;

    if (midiRenderScratchLoanedToHost)
    {
        midiMessages.swapWith(midiRenderScratch);
        midiRenderScratchLoanedToHost = false;
    }

    midiRenderScratch.clear();
    buffer.clear();
    midiMessages.clear();

    auto& outputMidi = midiRenderScratch;
    if (needsSafetyAllOff)
    {
        sendMidiResetMessages(outputMidi, 0);
        resetMidiOutputState();
    }
    else if (midiConfigChanged)
    {
        resetMidiOutputState();
    }

    if (renderAudio && buffer.getNumChannels() < 2)
    {
        const int n = buffer.getNumSamples();
        auto* mono = buffer.getWritePointer(0);
        int rendered = 0;
        const int scratchSamples = monoScratch.getNumSamples();
        while (rendered < n && scratchSamples > 0)
        {
            const int chunk = juce::jmin(scratchSamples, n - rendered);
            engine.render(monoScratch.getWritePointer(0), monoScratch.getWritePointer(1), chunk);
            for (int i = 0; i < chunk; ++i)
                mono[rendered + i] = 0.5f * (monoScratch.getSample(0, i) + monoScratch.getSample(1, i));
            rendered += chunk;
        }
    }
    else if (renderAudio)
    {
        engine.render(buffer.getWritePointer(0),
                      buffer.getWritePointer(1),
                      buffer.getNumSamples());
    }
    else
    {
        engine.processControlEvents(buffer.getNumSamples());
    }

    if (renderMidi)
        renderOutgoingMidi(outputMidi, buffer.getNumSamples());
    else
    {
        (void) engine.drainMidiSourceEvents(midiSourceScratch.data(), (int) midiSourceScratch.size());
        resetMidiOutputState();
    }

    recordOutgoingMidiDebugEvents(outputMidi);
    queueMidiToExternalOutput(outputMidi);
    midiMessages.swapWith(midiRenderScratch);
    midiRenderScratchLoanedToHost = true;
}

void AudienceProcessor::processIncomingMidiKeyboard (const juce::MidiBuffer& midiMessages)
{
    recordIncomingMidiDebugEvents(midiMessages);

    enum PitchMode
    {
        directMidiPitch = 0,
        quantizeToCurrentScale = 1,
        triggerAudiencePitch = 2
    };

    const int pitchMode = juce::jlimit(0, 2, rawParamInt(rawParams.externalMidiPitchMode));
    externalMidiPitchModeSnapshot.store(pitchMode, std::memory_order_relaxed);
    const int totalSteps = engine.getScaleTableSize();
    const int rootMidi = engine.scaleRootMidi.load(std::memory_order_relaxed);
    const bool mpeOutputActive = rawParamInt(rawParams.midiOutputType) == 2;
    const int mpeFirst = juce::jlimit(2, 16, rawParamInt(rawParams.mpeMemberFirstChannel, 2));
    const int mpeLast = juce::jlimit(mpeFirst, 16, rawParamInt(rawParams.mpeMemberLastChannel, 16));

    auto midiKeyIndex = [] (int channel, int note) noexcept
    {
        if (channel < 1 || channel > 16 || note < 0 || note >= 128)
            return -1;

        return (channel - 1) * 128 + note;
    };

    auto refreshActiveKeyCount = [this] () noexcept
    {
        int count = 0;
        for (const auto key : keyboardSlotToMidiKey)
            if (key >= 0)
                ++count;

        activeExternalMidiKeys.store(count, std::memory_order_relaxed);
    };

    auto releaseSlot = [this] (int slot)
    {
        if (slot < 0 || slot >= PartialEngine::MAX_KEYBOARD_SLOTS)
            return;

        const int oldKey = keyboardSlotToMidiKey[(size_t) slot];
        if (oldKey >= 0 && oldKey < (int) midiKeyToKeyboardSlot.size())
            midiKeyToKeyboardSlot[(size_t) oldKey] = -1;

        engine.processKeyboardPitchRealtime(slot, 0, 0.0, 0.0f, false);
        keyboardSlotToMidiKey[(size_t) slot] = -1;
        keyboardDebugKeys[(size_t) slot].store(-1, std::memory_order_relaxed);
    };

    auto releaseNote = [&] (int channel, int note)
    {
        const int key = midiKeyIndex(channel, note);
        if (key < 0)
            return;

        const int slot = midiKeyToKeyboardSlot[(size_t) key];
        if (slot < 0 || slot >= PartialEngine::MAX_KEYBOARD_SLOTS)
            return;

        releaseSlot(slot);
        refreshActiveKeyCount();
    };

    auto allocateSlot = [&] (int key)
    {
        if (key >= 0 && key < (int) midiKeyToKeyboardSlot.size())
        {
            const int existing = midiKeyToKeyboardSlot[(size_t) key];
            if (existing >= 0 && existing < PartialEngine::MAX_KEYBOARD_SLOTS)
                return existing;
        }

        for (int slot = 0; slot < PartialEngine::MAX_KEYBOARD_SLOTS; ++slot)
            if (keyboardSlotToMidiKey[(size_t) slot] < 0)
                return slot;

        const int stolen = juce::jlimit(0, PartialEngine::MAX_KEYBOARD_SLOTS - 1,
                                        key % PartialEngine::MAX_KEYBOARD_SLOTS);
        releaseSlot(stolen);
        return stolen;
    };

    std::array<int, midiInputKeyCount> pendingAction {};
    std::array<float, midiInputKeyCount> pendingVelocity {};
    std::array<unsigned char, midiInputKeyCount> pendingSawNoteOff {};
    std::array<unsigned char, midiInputKeyCount> pendingForcedOff {};

    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();
        const int channel = msg.getChannel();

        if (mpeOutputActive && channel >= mpeFirst && channel <= mpeLast)
            continue;

        if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            if (channel >= 1 && channel <= 16)
            {
                const int firstKey = (channel - 1) * 128;
                const int lastKey = firstKey + 127;
                for (int key = firstKey; key <= lastKey; ++key)
                {
                    if (midiKeyToKeyboardSlot[(size_t) key] >= 0)
                    {
                        pendingAction[(size_t) key] = -1;
                        pendingForcedOff[(size_t) key] = 1;
                    }
                }
            }
            continue;
        }

        if (msg.isNoteOff(true))
        {
            lastExternalMidiChannel.store(channel, std::memory_order_relaxed);
            lastExternalMidiNote.store(msg.getNoteNumber(), std::memory_order_relaxed);
            const int key = midiKeyIndex(channel, msg.getNoteNumber());
            if (key >= 0)
            {
                pendingAction[(size_t) key] = -1;
                pendingSawNoteOff[(size_t) key] = 1;
            }
            continue;
        }

        if (! msg.isNoteOn(false))
            continue;

        const int note = msg.getNoteNumber();
        const int key = midiKeyIndex(channel, note);
        if (key < 0)
            continue;

        lastExternalMidiChannel.store(channel, std::memory_order_relaxed);
        lastExternalMidiNote.store(note, std::memory_order_relaxed);

        pendingAction[(size_t) key] = 1;
        pendingVelocity[(size_t) key] = juce::jlimit(0.0f, 1.0f, msg.getFloatVelocity());
    }

    for (int key = 0; key < midiInputKeyCount; ++key)
    {
        const int action = pendingAction[(size_t) key];
        if (action == 0)
            continue;

        const int channel = key / 128 + 1;
        const int note = key % 128;

        if (action < 0)
        {
            releaseNote(channel, note);
            continue;
        }

        const int existingSlot = midiKeyToKeyboardSlot[(size_t) key];
        const bool alreadyActive = existingSlot >= 0 && existingSlot < PartialEngine::MAX_KEYBOARD_SLOTS;
        if (alreadyActive && pendingSawNoteOff[(size_t) key] != 0 && pendingForcedOff[(size_t) key] == 0)
        {
            refreshActiveKeyCount();
            continue;
        }

        releaseNote(channel, note);
        const int slot = allocateSlot(key);
        const float velocity = pendingVelocity[(size_t) key];

        midiKeyToKeyboardSlot[(size_t) key] = slot;
        keyboardSlotToMidiKey[(size_t) slot] = key;
        keyboardDebugKeys[(size_t) slot].store(key, std::memory_order_relaxed);

        if (pitchMode == directMidiPitch)
        {
            engine.processKeyboardPitchRealtime(slot, note,
                                                PartialEngine::midiNoteToFrequencyHz(note),
                                                velocity,
                                                true);
        }
        else
        {
            if (totalSteps <= 0)
            {
                releaseSlot(slot);
                refreshActiveKeyCount();
                continue;
            }

            int step = -1;
            if (pitchMode == quantizeToCurrentScale)
            {
                step = engine.findNearestScaleStepForMidi(note);
            }
            else
            {
                step = engine.isSpectralScale() ? engine.findKeyboardScaleStepForMidi(note)
                                                : note - rootMidi;
                if (step < 0 || step >= totalSteps)
                    step = engine.findNearestScaleStepForMidi(note);
            }

            if (step < 0 || step >= totalSteps)
            {
                releaseSlot(slot);
                refreshActiveKeyCount();
                continue;
            }

            engine.processKeyboardStepRealtime(slot, step, velocity, true);
        }

        refreshActiveKeyCount();
    }
}

void AudienceProcessor::releaseAllMidiKeyboardNotes()
{
    for (int slot = 0; slot < PartialEngine::MAX_KEYBOARD_SLOTS; ++slot)
    {
        if (keyboardSlotToMidiKey[(size_t) slot] >= 0)
            engine.processKeyboardPitchRealtime(slot, 0, 0.0, 0.0f, false);
        keyboardSlotToMidiKey[(size_t) slot] = -1;
    }

    midiKeyToKeyboardSlot.fill(-1);
    for (auto& key : keyboardDebugKeys)
        key.store(-1, std::memory_order_relaxed);
    activeExternalMidiKeys.store(0, std::memory_order_relaxed);
}

int AudienceProcessor::bendRangeFromChoice (int choice) noexcept
{
    static constexpr int ranges[] { 2, 12, 24, 48 };
    return ranges[juce::jlimit(0, 3, choice)];
}

int AudienceProcessor::velocityFromUnit (float value) noexcept
{
    return juce::jlimit(1, 127, (int) std::round(juce::jlimit(0.0f, 1.0f, value) * 127.0f));
}

int AudienceProcessor::pressureFromUnit (float value) noexcept
{
    return juce::jlimit(0, 127, (int) std::round(juce::jlimit(0.0f, 1.0f, value) * 127.0f));
}

juce::String AudienceProcessor::getExternalMidiPitchModeName() const
{
    switch (juce::jlimit(0, 2, externalMidiPitchModeSnapshot.load(std::memory_order_relaxed)))
    {
        case 1:  return "Quantize To Current Scale";
        case 2:  return "Use As Trigger For Audience Pitch";
        default: return "Direct MIDI Pitch";
    }
}

void AudienceProcessor::resetMidiOutputState() noexcept
{
    for (auto& state : midiOutVoices)
        state = {};

    for (auto& state : midiVoiceDebug)
    {
        state.active.store(0, std::memory_order_relaxed);
        state.sourceId.store(-1, std::memory_order_relaxed);
        state.channel.store(0, std::memory_order_relaxed);
        state.note.store(-1, std::memory_order_relaxed);
        state.pitchBend.store(8192, std::memory_order_relaxed);
        state.age.store(0, std::memory_order_relaxed);
    }

    mpeChannelOwner.fill(-1);
    midiVoiceAgeCounter = 0;
    midiNotesSent.store(0, std::memory_order_relaxed);
    activeMpeVoices.store(0, std::memory_order_relaxed);
    availableMpeChannels.store(juce::jmax(0, lastMpeMemberLast - lastMpeMemberFirst + 1),
                               std::memory_order_relaxed);
}

void AudienceProcessor::sendPitchBendRangeRpn (juce::MidiBuffer& midiMessages, int sampleOffset,
                                               int channel, int semitones)
{
    midiMessages.addEvent(juce::MidiMessage::controllerEvent(channel, 101, 0), sampleOffset);
    midiMessages.addEvent(juce::MidiMessage::controllerEvent(channel, 100, 0), sampleOffset);
    midiMessages.addEvent(juce::MidiMessage::controllerEvent(channel, 6, juce::jlimit(0, 127, semitones)), sampleOffset);
    midiMessages.addEvent(juce::MidiMessage::controllerEvent(channel, 38, 0), sampleOffset);
    midiMessages.addEvent(juce::MidiMessage::controllerEvent(channel, 101, 127), sampleOffset);
    midiMessages.addEvent(juce::MidiMessage::controllerEvent(channel, 100, 127), sampleOffset);
}

void AudienceProcessor::sendMpeSetupIfNeeded (juce::MidiBuffer& midiMessages, int sampleOffset)
{
    if (! mpeSetupDirty)
        return;

    if (rawParamInt(rawParams.midiOutputType) != 2
        || ! rawParamBool(rawParams.mpeSendSetupMessages, true))
    {
        mpeSetupDirty = false;
        return;
    }

    const int master = juce::jlimit(1, 16, rawParamInt(rawParams.mpeMasterChannel, 1));
    const int first = juce::jlimit(2, 16, rawParamInt(rawParams.mpeMemberFirstChannel, 2));
    const int last = juce::jlimit(first, 16, rawParamInt(rawParams.mpeMemberLastChannel, 16));
    const int bendRange = bendRangeFromChoice(rawParamInt(rawParams.mpePitchBendRange, 3));

    const int memberCount = juce::jlimit(0, 15, last - first + 1);
    midiMessages.addEvent(juce::MidiMessage::controllerEvent(master, 101, 0), sampleOffset);
    midiMessages.addEvent(juce::MidiMessage::controllerEvent(master, 100, 6), sampleOffset);
    midiMessages.addEvent(juce::MidiMessage::controllerEvent(master, 6, memberCount), sampleOffset);
    midiMessages.addEvent(juce::MidiMessage::controllerEvent(master, 38, 0), sampleOffset);
    midiMessages.addEvent(juce::MidiMessage::controllerEvent(master, 101, 127), sampleOffset);
    midiMessages.addEvent(juce::MidiMessage::controllerEvent(master, 100, 127), sampleOffset);

    for (int ch = first; ch <= last; ++ch)
        sendPitchBendRangeRpn(midiMessages, sampleOffset, ch, bendRange);

    mpeSetupDirty = false;
}

void AudienceProcessor::sendAllMidiNotesOff (juce::MidiBuffer& midiMessages, int sampleOffset)
{
    for (int ch = 1; ch <= 16; ++ch)
    {
        midiMessages.addEvent(juce::MidiMessage::controllerEvent(ch, 123, 0), sampleOffset);
        midiMessages.addEvent(juce::MidiMessage::controllerEvent(ch, 120, 0), sampleOffset);
        midiMessages.addEvent(juce::MidiMessage::channelPressureChange(ch, 0), sampleOffset);
        midiMessages.addEvent(juce::MidiMessage::pitchWheel(ch, 8192), sampleOffset);
    }
}

void AudienceProcessor::sendMidiResetMessages (juce::MidiBuffer& midiMessages, int sampleOffset)
{
    for (const auto& state : midiOutVoices)
    {
        if (! state.active || state.note < 0)
            continue;

        const int ch = juce::jlimit(1, 16, state.channel);
        midiMessages.addEvent(juce::MidiMessage::noteOff(ch, state.note), sampleOffset);
    }

    sendAllMidiNotesOff(midiMessages, sampleOffset);
}

void AudienceProcessor::releaseMpeChannelForSource (int sourceId) noexcept
{
    for (int ch = 1; ch <= 16; ++ch)
        if (mpeChannelOwner[(size_t) ch] == sourceId)
            mpeChannelOwner[(size_t) ch] = -1;

    int active = 0;
    for (const auto& state : midiOutVoices)
        if (state.active && state.channel >= lastMpeMemberFirst && state.channel <= lastMpeMemberLast)
            ++active;

    activeMpeVoices.store(active, std::memory_order_relaxed);
    availableMpeChannels.store(juce::jmax(0, lastMpeMemberLast - lastMpeMemberFirst + 1 - active),
                               std::memory_order_relaxed);
}

void AudienceProcessor::sendNoteOffForSource (int sourceId, juce::MidiBuffer& midiMessages, int sampleOffset)
{
    if (sourceId < 0 || sourceId >= (int) midiOutVoices.size())
        return;

    auto& state = midiOutVoices[(size_t) sourceId];
    if (! state.active || state.note < 0)
        return;

    const int ch = juce::jlimit(1, 16, state.channel);
    midiMessages.addEvent(juce::MidiMessage::noteOff(ch, state.note), sampleOffset);
    midiMessages.addEvent(juce::MidiMessage::channelPressureChange(ch, 0), sampleOffset);

    if (rawParamInt(rawParams.midiOutputType) == 2)
    {
        midiMessages.addEvent(juce::MidiMessage::pitchWheel(ch, 8192), sampleOffset);
        state.active = false;
        releaseMpeChannelForSource(sourceId);
    }

    state = {};
    auto& debug = midiVoiceDebug[(size_t) sourceId];
    debug.active.store(0, std::memory_order_relaxed);
    debug.sourceId.store(sourceId, std::memory_order_relaxed);
    debug.channel.store(0, std::memory_order_relaxed);
    debug.note.store(-1, std::memory_order_relaxed);
    debug.pitchBend.store(8192, std::memory_order_relaxed);
    debug.age.store(0, std::memory_order_relaxed);
}

int AudienceProcessor::allocateMpeChannelForSource (int sourceId, juce::MidiBuffer& midiMessages, int sampleOffset)
{
    if (sourceId >= 0 && sourceId < (int) midiOutVoices.size())
    {
        const auto& state = midiOutVoices[(size_t) sourceId];
        if (state.active && state.channel >= lastMpeMemberFirst && state.channel <= lastMpeMemberLast)
            return state.channel;
    }

    for (int ch = lastMpeMemberFirst; ch <= lastMpeMemberLast; ++ch)
    {
        if (mpeChannelOwner[(size_t) ch] < 0)
        {
            mpeChannelOwner[(size_t) ch] = sourceId;
            return ch;
        }
    }

    int oldestSource = -1;
    uint32_t oldestAge = std::numeric_limits<uint32_t>::max();
    for (const auto& state : midiOutVoices)
    {
        if (! state.active || state.channel < lastMpeMemberFirst || state.channel > lastMpeMemberLast)
            continue;

        if (state.age < oldestAge)
        {
            oldestAge = state.age;
            oldestSource = state.sourceId;
        }
    }

    if (oldestSource >= 0)
    {
        const int stolenChannel = midiOutVoices[(size_t) oldestSource].channel;
        sendNoteOffForSource(oldestSource, midiMessages, sampleOffset);
        mpeChannelOwner[(size_t) stolenChannel] = sourceId;
        return stolenChannel;
    }

    return lastMpeMemberFirst;
}

void AudienceProcessor::sendExpressionForSource (int sourceId, const PartialEngine::MidiSourceEvent& event,
                                                 juce::MidiBuffer& midiMessages, int sampleOffset, bool force)
{
    if (sourceId < 0 || sourceId >= (int) midiOutVoices.size())
        return;

    auto& state = midiOutVoices[(size_t) sourceId];
    if (! state.active)
        return;

    const int type = rawParamInt(rawParams.midiOutputType);
    const int ch = juce::jlimit(1, 16, state.channel);
    const int pressure = pressureFromUnit(event.y);
    const int timbre = pressureFromUnit(juce::jlimit(0.0f, 1.0f,
        event.x * 0.68f + rawParamValue(rawParams.motionMacro, 0.5f) * 0.32f));
    const int expression = pressureFromUnit(juce::jlimit(0.0f, 1.0f,
        event.y * 0.70f + rawParamValue(rawParams.energy, 0.5f) * 0.30f));

    if (type == 2)
    {
        const int bendRange = bendRangeFromChoice(rawParamInt(rawParams.mpePitchBendRange, 3));
        const auto pitch = convertFrequencyToMidiPitch(event.frequencyHz, bendRange);
        if (force || std::abs(pitch.pitchBend14Bit - state.pitchBend) > 1)
        {
            midiMessages.addEvent(juce::MidiMessage::pitchWheel(ch, pitch.pitchBend14Bit), sampleOffset);
            state.pitchBend = pitch.pitchBend14Bit;
            state.frequencyHz = pitch.targetFrequencyHz;
            midiVoiceDebug[(size_t) sourceId].pitchBend.store(state.pitchBend, std::memory_order_relaxed);
        }

        if (force || std::abs(pressure - state.pressure) > 1)
        {
            midiMessages.addEvent(juce::MidiMessage::channelPressureChange(ch, pressure), sampleOffset);
            state.pressure = pressure;
        }
    }

    if (force || std::abs(timbre - state.timbre) > 1)
    {
        midiMessages.addEvent(juce::MidiMessage::controllerEvent(ch, 74, timbre), sampleOffset);
        state.timbre = timbre;
    }

    if (force || std::abs(expression - state.expression) > 1)
    {
        midiMessages.addEvent(juce::MidiMessage::controllerEvent(ch, 11, expression), sampleOffset);
        state.expression = expression;
    }
}

void AudienceProcessor::handleMidiSourceEvent (const PartialEngine::MidiSourceEvent& event,
                                               juce::MidiBuffer& midiMessages, int sampleOffset)
{
    if (event.type == PartialEngine::MidiSourceEvent::AllNotesOff)
    {
        sendMidiResetMessages(midiMessages, sampleOffset);
        resetMidiOutputState();
        return;
    }

    const int sourceId = event.sourceId;
    if (sourceId < 0 || sourceId >= (int) midiOutVoices.size())
        return;

    if (event.type == PartialEngine::MidiSourceEvent::NoteOff)
    {
        sendNoteOffForSource(sourceId, midiMessages, sampleOffset);
        return;
    }

    if (event.type == PartialEngine::MidiSourceEvent::Expression)
    {
        sendExpressionForSource(sourceId, event, midiMessages, sampleOffset, false);
        return;
    }

    const int outputType = rawParamInt(rawParams.midiOutputType);
    const int bendRange = bendRangeFromChoice(rawParamInt(rawParams.mpePitchBendRange, 3));
    const auto pitch = convertFrequencyToMidiPitch(event.frequencyHz, bendRange);
    const int velocity = velocityFromUnit(event.velocity);

    auto& state = midiOutVoices[(size_t) sourceId];

    if (outputType == 2
        && state.active
        && state.note == pitch.noteNumber
        && std::abs(pitch.pitchBend14Bit - state.pitchBend) <= 1)
    {
        sendExpressionForSource(sourceId, event, midiMessages, sampleOffset, true);
        return;
    }

    if (outputType == 2
        && rawParamInt(rawParams.mpePitchMode) == 1
        && state.active
        && state.note == pitch.noteNumber)
    {
        sendExpressionForSource(sourceId, event, midiMessages, sampleOffset, true);
        return;
    }

    sendNoteOffForSource(sourceId, midiMessages, sampleOffset);

    state.active = true;
    state.sourceId = sourceId;
    state.note = outputType == 2 ? pitch.noteNumber : pitch.noteNumber;
    state.frequencyHz = pitch.targetFrequencyHz;
    state.age = ++midiVoiceAgeCounter;

    if (outputType == 2)
    {
        sendMpeSetupIfNeeded(midiMessages, sampleOffset);
        const int ch = allocateMpeChannelForSource(sourceId, midiMessages, sampleOffset);
        state.channel = ch;
        state.pitchBend = pitch.pitchBend14Bit;
        mpeChannelOwner[(size_t) ch] = sourceId;

        midiMessages.addEvent(juce::MidiMessage::pitchWheel(ch, pitch.pitchBend14Bit), sampleOffset);
        const int timbre = pressureFromUnit(juce::jlimit(0.0f, 1.0f,
            event.x * 0.68f + rawParamValue(rawParams.motionMacro, 0.5f) * 0.32f));
        const int expression = pressureFromUnit(juce::jlimit(0.0f, 1.0f,
            event.y * 0.70f + rawParamValue(rawParams.energy, 0.5f) * 0.30f));
        const int pressure = pressureFromUnit(event.y);
        midiMessages.addEvent(juce::MidiMessage::controllerEvent(ch, 74, timbre), sampleOffset);
        midiMessages.addEvent(juce::MidiMessage::controllerEvent(ch, 11, expression), sampleOffset);
        midiMessages.addEvent(juce::MidiMessage::noteOn(ch, pitch.noteNumber, (juce::uint8) velocity), sampleOffset);
        midiMessages.addEvent(juce::MidiMessage::channelPressureChange(ch, pressure), sampleOffset);
        state.pressure = pressure;
        state.timbre = timbre;
        state.expression = expression;
        auto& debug = midiVoiceDebug[(size_t) sourceId];
        debug.sourceId.store(sourceId, std::memory_order_relaxed);
        debug.channel.store(ch, std::memory_order_relaxed);
        debug.note.store(state.note, std::memory_order_relaxed);
        debug.pitchBend.store(state.pitchBend, std::memory_order_relaxed);
        debug.age.store((int) state.age, std::memory_order_relaxed);
        debug.active.store(1, std::memory_order_release);
        int active = 0;
        for (const auto& voiceState : midiOutVoices)
            if (voiceState.active && voiceState.channel >= lastMpeMemberFirst && voiceState.channel <= lastMpeMemberLast)
                ++active;
        activeMpeVoices.store(active, std::memory_order_relaxed);
        availableMpeChannels.store(juce::jmax(0, lastMpeMemberLast - lastMpeMemberFirst + 1 - active),
                                   std::memory_order_relaxed);
    }
    else
    {
        const int ch = juce::jlimit(1, 16, rawParamInt(rawParams.normalMidiChannel, 1));
        state.channel = ch;
        state.pitchBend = 8192;
        midiMessages.addEvent(juce::MidiMessage::noteOn(ch, state.note, (juce::uint8) velocity), sampleOffset);
        sendExpressionForSource(sourceId, event, midiMessages, sampleOffset, true);
        auto& debug = midiVoiceDebug[(size_t) sourceId];
        debug.sourceId.store(sourceId, std::memory_order_relaxed);
        debug.channel.store(ch, std::memory_order_relaxed);
        debug.note.store(state.note, std::memory_order_relaxed);
        debug.pitchBend.store(state.pitchBend, std::memory_order_relaxed);
        debug.age.store((int) state.age, std::memory_order_relaxed);
        debug.active.store(1, std::memory_order_release);
    }

    midiNotesSent.fetch_add(1, std::memory_order_relaxed);
}

void AudienceProcessor::renderOutgoingMidi (juce::MidiBuffer& midiMessages, int numSamples)
{
    const int outputType = rawParamInt(rawParams.midiOutputType);
    if (outputType == 2)
        sendMpeSetupIfNeeded(midiMessages, 0);

    const int maxEvents = (int) midiSourceScratch.size();
    const int count = engine.drainMidiSourceEvents(midiSourceScratch.data(), maxEvents);
    const int sampleOffset = juce::jlimit(0, juce::jmax(0, numSamples - 1), 0);
    for (int i = 0; i < count; ++i)
        handleMidiSourceEvent(midiSourceScratch[(size_t) i], midiMessages, sampleOffset);
}

void AudienceProcessor::recordIncomingMidiDebugEvents (const juce::MidiBuffer& midiMessages) noexcept
{
    if (midiMessages.isEmpty())
        return;

    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();
        const int rawSize = message.getRawDataSize();
        if (rawSize <= 0 || rawSize > 3)
            continue;

        const auto seq = incomingMidiDebugWriteCounter.fetch_add(1, std::memory_order_relaxed) + 1;
        auto& slot = incomingMidiDebugEvents[(size_t) ((seq - 1) % midiDebugEventQueueSize)];
        slot.sequence.store(0, std::memory_order_release);
        slot.sampleOffset.store(metadata.samplePosition, std::memory_order_relaxed);
        slot.size.store(rawSize, std::memory_order_relaxed);

        const auto* raw = message.getRawData();
        slot.byte0.store(rawSize > 0 ? raw[0] : 0, std::memory_order_relaxed);
        slot.byte1.store(rawSize > 1 ? raw[1] : 0, std::memory_order_relaxed);
        slot.byte2.store(rawSize > 2 ? raw[2] : 0, std::memory_order_relaxed);
        slot.sequence.store(seq, std::memory_order_release);
    }
}

void AudienceProcessor::recordOutgoingMidiDebugEvents (const juce::MidiBuffer& midiMessages) noexcept
{
    if (midiMessages.isEmpty())
        return;

    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();
        const int rawSize = message.getRawDataSize();
        if (rawSize <= 0 || rawSize > 3)
            continue;

        const auto seq = midiDebugWriteCounter.fetch_add(1, std::memory_order_relaxed) + 1;
        auto& slot = midiDebugEvents[(size_t) ((seq - 1) % midiDebugEventQueueSize)];
        slot.sequence.store(0, std::memory_order_release);
        slot.sampleOffset.store(metadata.samplePosition, std::memory_order_relaxed);
        slot.size.store(rawSize, std::memory_order_relaxed);

        const auto* raw = message.getRawData();
        slot.byte0.store(rawSize > 0 ? raw[0] : 0, std::memory_order_relaxed);
        slot.byte1.store(rawSize > 1 ? raw[1] : 0, std::memory_order_relaxed);
        slot.byte2.store(rawSize > 2 ? raw[2] : 0, std::memory_order_relaxed);
        slot.sequence.store(seq, std::memory_order_release);
    }
}

void AudienceProcessor::queueMidiToExternalOutput (const juce::MidiBuffer& midiMessages) noexcept
{
    if (midiOutputOptionIndex.load(std::memory_order_relaxed) <= 0 || midiMessages.isEmpty())
        return;

    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();
        const int rawSize = message.getRawDataSize();
        if (rawSize <= 0 || rawSize > 3)
            continue;

        int s1, sz1, s2, sz2;
        externalMidiFifo.prepareToWrite(1, s1, sz1, s2, sz2);
        if (sz1 <= 0 && sz2 <= 0)
        {
            externalMidiDropped.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        const int slot = sz1 > 0 ? s1 : s2;
        auto& packed = externalMidiEvents[(size_t) slot];
        packed.size = (juce::uint8) rawSize;
        const auto* raw = message.getRawData();
        for (int i = 0; i < rawSize; ++i)
            packed.data[i] = raw[i];

        externalMidiFifo.finishedWrite(1);
    }
}

void AudienceProcessor::drainExternalMidiOutputQueue()
{
    if (midiOutput == nullptr)
        return;

    const int available = externalMidiFifo.getNumReady();
    if (available <= 0)
        return;

    int s1, sz1, s2, sz2;
    externalMidiFifo.prepareToRead(available, s1, sz1, s2, sz2);

    auto sendRange = [this] (int start, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            const auto& event = externalMidiEvents[(size_t) (start + i)];
            if (event.size > 0)
                midiOutput->sendMessageNow(juce::MidiMessage(event.data, (int) event.size));
        }
    };

    sendRange(s1, sz1);
    sendRange(s2, sz2);
    externalMidiFifo.finishedRead(sz1 + sz2);
}

void AudienceProcessor::timerCallback()
{
    drainExternalMidiOutputQueue();
}

juce::String AudienceProcessor::getOutgoingMidiDebugText (int maxEvents) const
{
    auto noteName = [] (int midi)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#",
                                       "G", "G#", "A", "A#", "B" };
        return juce::String(names[((midi % 12) + 12) % 12]) + juce::String(midi / 12 - 1);
    };

    auto ccName = [] (int cc)
    {
        switch (cc)
        {
            case 6:   return "Data MSB";
            case 11:  return "Expression";
            case 38:  return "Data LSB";
            case 74:  return "Timbre";
            case 100: return "RPN LSB";
            case 101: return "RPN MSB";
            case 120: return "All Sound Off";
            case 123: return "All Notes Off";
            default:  return "CC";
        }
    };

    const int outputMode = rawParamInt(rawParams.audioMidiOutputMode);
    const int midiType = rawParamInt(rawParams.midiOutputType);
    const int bendRange = bendRangeFromChoice(rawParamInt(rawParams.mpePitchBendRange, 3));
    static const char* outputModeNames[] { "Audio Only", "MIDI Only", "Audio + MIDI" };
    static const char* midiTypeNames[] { "Off", "Normal MIDI", "MPE MIDI" };

    juce::String s;
    s << "outgoing MIDI / MPE\n";
    s << "mode        : " << outputModeNames[juce::jlimit(0, 2, outputMode)]
      << " / " << midiTypeNames[juce::jlimit(0, 2, midiType)] << "\n";
    s << "destination : " << getMidiOutputStatus() << "\n";
    s << "MPE bend    : " << bendRange << " st"
      << "   active " << getActiveMpeVoices()
      << " / available " << getAvailableMpeChannels() << "\n";
    s << "----------------------------------------------\n";

    const auto latest = midiDebugWriteCounter.load(std::memory_order_acquire);
    const int count = juce::jlimit(0, midiDebugEventQueueSize,
                                   juce::jmin(maxEvents, (int) latest));
    if (count <= 0)
    {
        s << "(no outgoing MIDI captured yet)\n";
        return s;
    }

    const uint32_t firstSeq = latest - (uint32_t) count + 1;
    for (uint32_t seq = firstSeq; seq <= latest; ++seq)
    {
        const auto& slot = midiDebugEvents[(size_t) ((seq - 1) % midiDebugEventQueueSize)];
        const auto storedSeq = slot.sequence.load(std::memory_order_acquire);
        if (storedSeq != seq)
            continue;

        const int sample = slot.sampleOffset.load(std::memory_order_relaxed);
        const int size = slot.size.load(std::memory_order_relaxed);
        const int b0 = slot.byte0.load(std::memory_order_relaxed) & 0xff;
        const int b1 = slot.byte1.load(std::memory_order_relaxed) & 0xff;
        const int b2 = slot.byte2.load(std::memory_order_relaxed) & 0xff;
        if (size <= 0)
            continue;

        const int status = b0 & 0xf0;
        const int channel = (b0 & 0x0f) + 1;
        s << juce::String(seq).paddedLeft(' ', 5) << "  +"
          << juce::String(sample).paddedLeft(' ', 4) << "  ch "
          << juce::String(channel).paddedLeft(' ', 2) << "  ";

        if (status == 0x90 && b2 > 0)
        {
            s << "Note On   " << noteName(b1).paddedRight(' ', 4)
              << " note " << juce::String(b1).paddedLeft(' ', 3)
              << " vel " << juce::String(b2).paddedLeft(' ', 3);
        }
        else if (status == 0x80 || (status == 0x90 && b2 == 0))
        {
            s << "Note Off  " << noteName(b1).paddedRight(' ', 4)
              << " note " << juce::String(b1).paddedLeft(' ', 3);
        }
        else if (status == 0xb0)
        {
            s << juce::String(ccName(b1)).paddedRight(' ', 13)
              << " " << juce::String(b1).paddedLeft(' ', 3)
              << " = " << juce::String(b2).paddedLeft(' ', 3);
        }
        else if (status == 0xd0)
        {
            s << "Pressure  " << juce::String(b1).paddedLeft(' ', 3);
        }
        else if (status == 0xe0)
        {
            const int bend = b1 + (b2 << 7);
            const double cents = ((double) bend - 8192.0) / 8192.0
                               * (double) bendRange * 100.0;
            s << "PitchBend " << juce::String(bend).paddedLeft(' ', 5)
              << "  " << (cents >= 0.0 ? "+" : "")
              << juce::String(cents, 1) << " ct";
        }
        else
        {
            s << "Raw";
        }

        s << "   [";
        s << juce::String::toHexString(b0).paddedLeft('0', 2);
        if (size > 1) s << " " << juce::String::toHexString(b1).paddedLeft('0', 2);
        if (size > 2) s << " " << juce::String::toHexString(b2).paddedLeft('0', 2);
        s << "]\n";
    }

    return s;
}

juce::String AudienceProcessor::getIncomingMidiDebugText (int maxEvents) const
{
    auto noteName = [] (int midi)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#",
                                       "G", "G#", "A", "A#", "B" };
        return juce::String(names[((midi % 12) + 12) % 12]) + juce::String(midi / 12 - 1);
    };

    auto ccName = [] (int cc)
    {
        switch (cc)
        {
            case 11:  return "Expression";
            case 64:  return "Sustain";
            case 74:  return "Timbre";
            case 120: return "All Sound Off";
            case 123: return "All Notes Off";
            default:  return "CC";
        }
    };

    juce::String s;
    s << "incoming MIDI\n";
    s << "input mode  : " << getExternalMidiPitchModeName() << "\n";
    s << "active keys : " << getActiveExternalMidiKeys()
      << "   last ch " << getLastExternalMidiChannel()
      << " note " << getLastExternalMidiNote() << "\n";
    if (rawParamInt(rawParams.midiOutputType) == 2)
    {
        const int first = juce::jlimit(2, 16, rawParamInt(rawParams.mpeMemberFirstChannel, 2));
        const int last = juce::jlimit(first, 16, rawParamInt(rawParams.mpeMemberLastChannel, 16));
        s << "guard       : ignoring local MPE member input ch "
          << first << "-" << last << "\n";
    }
    s << "----------------------------------------------\n";

    const auto latest = incomingMidiDebugWriteCounter.load(std::memory_order_acquire);
    const int count = juce::jlimit(0, midiDebugEventQueueSize,
                                   juce::jmin(maxEvents, (int) latest));
    if (count <= 0)
    {
        s << "(no incoming MIDI captured yet)\n";
        return s;
    }

    const uint32_t firstSeq = latest - (uint32_t) count + 1;
    for (uint32_t seq = firstSeq; seq <= latest; ++seq)
    {
        const auto& slot = incomingMidiDebugEvents[(size_t) ((seq - 1) % midiDebugEventQueueSize)];
        const auto storedSeq = slot.sequence.load(std::memory_order_acquire);
        if (storedSeq != seq)
            continue;

        const int sample = slot.sampleOffset.load(std::memory_order_relaxed);
        const int size = slot.size.load(std::memory_order_relaxed);
        const int b0 = slot.byte0.load(std::memory_order_relaxed) & 0xff;
        const int b1 = slot.byte1.load(std::memory_order_relaxed) & 0xff;
        const int b2 = slot.byte2.load(std::memory_order_relaxed) & 0xff;
        if (size <= 0)
            continue;

        const int status = b0 & 0xf0;
        const int channel = (b0 & 0x0f) + 1;
        s << juce::String(seq).paddedLeft(' ', 5) << "  +"
          << juce::String(sample).paddedLeft(' ', 4) << "  ch "
          << juce::String(channel).paddedLeft(' ', 2) << "  ";

        if (status == 0x90 && b2 > 0)
        {
            s << "Note On   " << noteName(b1).paddedRight(' ', 4)
              << " note " << juce::String(b1).paddedLeft(' ', 3)
              << " vel " << juce::String(b2).paddedLeft(' ', 3);
        }
        else if (status == 0x80 || (status == 0x90 && b2 == 0))
        {
            s << "Note Off  " << noteName(b1).paddedRight(' ', 4)
              << " note " << juce::String(b1).paddedLeft(' ', 3);
        }
        else if (status == 0xb0)
        {
            s << juce::String(ccName(b1)).paddedRight(' ', 13)
              << " " << juce::String(b1).paddedLeft(' ', 3)
              << " = " << juce::String(b2).paddedLeft(' ', 3);
        }
        else if (status == 0xd0)
        {
            s << "Pressure  " << juce::String(b1).paddedLeft(' ', 3);
        }
        else if (status == 0xe0)
        {
            const int bend = b1 + (b2 << 7);
            s << "PitchBend " << juce::String(bend).paddedLeft(' ', 5);
        }
        else
        {
            s << "Raw";
        }

        s << "   [";
        s << juce::String::toHexString(b0).paddedLeft('0', 2);
        if (size > 1) s << " " << juce::String::toHexString(b1).paddedLeft('0', 2);
        if (size > 2) s << " " << juce::String::toHexString(b2).paddedLeft('0', 2);
        s << "]\n";
    }

    return s;
}

juce::String AudienceProcessor::getMidiStateDebugText() const
{
    auto noteName = [] (int midi)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#",
                                       "G", "G#", "A", "A#", "B" };
        if (midi < 0)
            return juce::String("-");
        return juce::String(names[((midi % 12) + 12) % 12]) + juce::String(midi / 12 - 1);
    };

    juce::String s;
    s << "debug report\n";
    s << "--------------------------------------------------\n";
    s << "external MIDI mode : " << getExternalMidiPitchModeName() << "\n";
    s << "active input keys  : " << getActiveExternalMidiKeys() << "\n";
    s << "engine voices      : " << engine.getActiveVoiceCount()
      << " / " << engine.getVoiceLimit() << "\n";
    s << "MPE voices         : " << getActiveMpeVoices()
      << " active / " << getAvailableMpeChannels() << " available\n";
    s << "MIDI output        : " << getMidiOutputStatus() << "\n";
    s << "\nactive keyboard slots\n";

    int listed = 0;
    for (int slot = 0; slot < PartialEngine::MAX_KEYBOARD_SLOTS; ++slot)
    {
        const int key = keyboardDebugKeys[(size_t) slot].load(std::memory_order_relaxed);
        if (key < 0)
            continue;

        const int ch = key / 128 + 1;
        const int note = key % 128;
        s << "  slot " << juce::String(slot).paddedLeft(' ', 2)
          << "  input ch " << juce::String(ch).paddedLeft(' ', 2)
          << "  " << noteName(note).paddedRight(' ', 4)
          << " note " << note << "\n";
        if (++listed >= 24)
        {
            s << "  ...\n";
            break;
        }
    }
    if (listed == 0)
        s << "  (none)\n";

    s << "\nactive MIDI/MPE output voices\n";
    listed = 0;
    for (const auto& voice : midiVoiceDebug)
    {
        if (voice.active.load(std::memory_order_acquire) == 0)
            continue;

        const int sourceId = voice.sourceId.load(std::memory_order_relaxed);
        const int ch = voice.channel.load(std::memory_order_relaxed);
        const int note = voice.note.load(std::memory_order_relaxed);
        const int bend = voice.pitchBend.load(std::memory_order_relaxed);
        const int age = voice.age.load(std::memory_order_relaxed);
        s << "  src " << juce::String(sourceId).paddedLeft(' ', 4)
          << "  ch " << juce::String(ch).paddedLeft(' ', 2)
          << "  " << noteName(note).paddedRight(' ', 4)
          << " note " << juce::String(note).paddedLeft(' ', 3)
          << "  pb " << juce::String(bend).paddedLeft(' ', 5)
          << "  age " << age << "\n";
        if (++listed >= 32)
        {
            s << "  ...\n";
            break;
        }
    }
    if (listed == 0)
        s << "  (none)\n";

    return s;
}

juce::String AudienceProcessor::getMidiDebugReportText() const
{
    juce::String s;
    s << getMidiStateDebugText() << "\n";
    s << getIncomingMidiDebugText(96) << "\n";
    s << getOutgoingMidiDebugText(96);
    return s;
}

juce::StringArray AudienceProcessor::getMidiOutputOptions() const
{
    juce::StringArray options;
    options.add("Host MIDI Output");
    options.add("Virtual: Audience Harmonic Synth MIDI Out");

    for (const auto& device : juce::MidiOutput::getAvailableDevices())
        options.add(device.name);

    return options;
}

juce::String AudienceProcessor::getMidiOutputStatus() const
{
    return midiOutputStatus;
}

juce::String AudienceProcessor::getMidiOutputDescription() const
{
    if (midiOutputOptionIndex.load(std::memory_order_relaxed) == 0)
        return juce::String("Host MIDI Output")
             + (wrapperType == wrapperType_VST3 ? " | route from this plugin track in Ableton" : " | plugin MIDI bus");

    return midiOutputStatus + " | host MIDI output remains available";
}

void AudienceProcessor::setMidiOutputOptionIndex (int index)
{
    drainExternalMidiOutputQueue();
    sendImmediateAllNotesOffToExternal();
    midiOutput.reset();
    midiOutputOptionIndex.store(juce::jmax(0, index), std::memory_order_relaxed);

    if (midiOutputOptionIndex.load(std::memory_order_relaxed) == 0)
    {
        midiOutputStatus = "Host MIDI Output";
        return;
    }

    if (midiOutputOptionIndex.load(std::memory_order_relaxed) == 1)
    {
        const juce::String portName = instanceId <= 1
            ? "Audience Harmonic Synth MIDI Out"
            : "Audience Harmonic Synth MIDI Out " + juce::String(instanceId);

        midiOutput = juce::MidiOutput::createNewDevice(portName);
        if (midiOutput != nullptr)
        {
            midiOutputStatus = "Virtual port: " + portName;
        }
        else
        {
            midiOutputStatus = "Virtual MIDI port unavailable";
            midiOutputOptionIndex.store(0, std::memory_order_relaxed);
        }
        return;
    }

    const auto devices = juce::MidiOutput::getAvailableDevices();
    const int deviceIndex = midiOutputOptionIndex.load(std::memory_order_relaxed) - 2;
    if (deviceIndex >= 0 && deviceIndex < devices.size())
    {
        midiOutput = juce::MidiOutput::openDevice(devices[deviceIndex].identifier);
        if (midiOutput != nullptr)
        {
            midiOutputStatus = "MIDI Output: " + devices[deviceIndex].name;
        }
        else
        {
            midiOutputStatus = "Failed to open MIDI output";
            midiOutputOptionIndex.store(0, std::memory_order_relaxed);
        }
    }
    else
    {
        midiOutputStatus = "MIDI output device not found";
        midiOutputOptionIndex.store(0, std::memory_order_relaxed);
    }
}

void AudienceProcessor::sendImmediateAllNotesOffToExternal()
{
    if (midiOutput == nullptr || midiOutputOptionIndex.load(std::memory_order_relaxed) == 0)
        return;

    for (const auto& state : midiOutVoices)
    {
        if (! state.active || state.note < 0)
            continue;

        const int ch = juce::jlimit(1, 16, state.channel);
        midiOutput->sendMessageNow(juce::MidiMessage::noteOff(ch, state.note));
    }

    for (int ch = 1; ch <= 16; ++ch)
    {
        midiOutput->sendMessageNow(juce::MidiMessage::channelPressureChange(ch, 0));
        midiOutput->sendMessageNow(juce::MidiMessage::pitchWheel(ch, 8192));
        midiOutput->sendMessageNow(juce::MidiMessage::allNotesOff(ch));
        midiOutput->sendMessageNow(juce::MidiMessage::allSoundOff(ch));
    }

    resetMidiOutputState();
}

void AudienceProcessor::closeMidiOutput()
{
    sendImmediateAllNotesOffToExternal();
    midiOutput.reset();
    midiOutputOptionIndex.store(0, std::memory_order_relaxed);
    midiOutputStatus = "Host MIDI Output";
}

void AudienceProcessor::setSampleDirectory (const juce::File& dir)
{
    sampleDir = dir;
    suspendProcessing(true);
    engine.loadSampleLibrary(dir);
    suspendProcessing(false);
    librariesStatus = engine.getLibrary().getStatus();
}

void AudienceProcessor::setUdpPort (int port)
{
    udpPort = port;
    const bool ok = osc.start(port);
    oscStatus = ok ? ("Listening on UDP " + juce::String(port))
                   : ("FAILED to bind UDP " + juce::String(port) + " - port busy?");
}

void AudienceProcessor::panic()
{
    simulator.clearSilently();
    releaseAllMidiKeyboardNotes();
    engine.requestClearAllSeats();
    sendImmediateAllNotesOffToExternal();
}

void AudienceProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty("udpPort", udpPort, nullptr);
    state.setProperty("currentLibrary", currentLibraryName, nullptr);
    state.setProperty("midiOutputOption", midiOutputOptionIndex.load(std::memory_order_relaxed), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void AudienceProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
    {
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.isValid())
        {
            apvts.replaceState(state);
            const int port = (int) state.getProperty("udpPort", 6060);
            setUdpPort(port);
            setMidiOutputOptionIndex((int) state.getProperty("midiOutputOption", 0));

            const auto lib = state.getProperty("currentLibrary").toString();
            if (lib.isNotEmpty())
                setCurrentLibrary(lib);
        }
    }
}

juce::AudioProcessorEditor* AudienceProcessor::createEditor()
{
    return new AudienceEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudienceProcessor();
}
