#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <limits>

// AUTHORITATIVE capacity guard. MpeMidiOutput.h/.cpp are kept free of
// PartialEngine.h so the lightweight MPE test target stays decoupled, which
// means MpeMidiOutput::kMaxMidiSources is a hand-maintained mirror of the real
// PartialEngine seat + keyboard count. This static_assert (here, where both
// PartialEngine.h and MpeMidiOutput.h are visible) breaks the build if those
// two ever drift again.
static_assert (MpeMidiOutput::kMaxMidiSources == PartialEngine::MAX_SEATS + PartialEngine::MAX_KEYBOARD_SLOTS,
               "MpeMidiOutput seat capacity must match PartialEngine seat+keyboard count");

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

    // B8: the MPE zone fully determines the channel layout. Maps the mpeZone choice
    // index to its legal MPE master + member-channel range. This is the single
    // source of truth shared by buildMpeConfig() (which feeds MpeMidiOutput) and the
    // processBlock change-detection (which decides when to re-send setup / all-off),
    // so the two can never disagree about which channels a zone uses.
    struct MpeZoneChannels { int master; int memberFirst; int memberLast; };

    MpeZoneChannels zoneChannels (int zoneIndex) noexcept
    {
        // Upper (1): master 16, members 1..15. Anything else -> Lower (0): master 1,
        // members 2..16 (the historical default; byte-identical to prior behaviour).
        if (zoneIndex == 1)
            return { 16, 1, 15 };
        return { 1, 2, 16 };
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
    mpeOut.reset();
    setUdpPort(udpPort);
    startTimerHz(60);

    const std::initializer_list<juce::File> rootCandidates {
        getBundledSamplesDirectory(),
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("SpektraSynth/Samples"),
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
            .getChildFile("SpektraSynth/Samples"),
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
        StringArray { "Lower", "Upper" }, 0));   // 0 = Lower (default), 1 = Upper.
        // The zone now fully OWNS the MPE channel layout (master + member range);
        // see buildMpeConfig(). Lower keeps the historical master1/members2-16,
        // Upper uses master16/members1-15.

    {
        // Choice (not Int) so the editor's ComboBoxAttachment indexes correctly.
        // 16 choices keep the same normalised 0..1 mapping as the old Int(1..16),
        // so existing sessions recall the same channel.
        juce::StringArray midiChannelChoices;
        for (int ch = 1; ch <= 16; ++ch)
            midiChannelChoices.add (juce::String (ch));
        layout.add (std::make_unique<AudioParameterChoice>(
            ParameterID ("normalMidiChannel", 1), "Normal MIDI Channel", midiChannelChoices, 0));
    }

    layout.add (std::make_unique<AudioParameterInt>(
        ParameterID ("mpeMasterChannel", 1), "MPE Master Channel", 1, 16, 1));

    layout.add (std::make_unique<AudioParameterInt>(
        ParameterID ("mpeMemberFirstChannel", 1), "MPE First Member Channel", 2, 16, 2));

    layout.add (std::make_unique<AudioParameterInt>(
        ParameterID ("mpeMemberLastChannel", 1), "MPE Last Member Channel", 2, 16, 16));

    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID ("mpePitchBendRange", 1), "MPE Pitch Bend Range",
        StringArray { "2 st", "12 st", "24 st", "48 st" }, 0));   // default 2 st:
        // spectral degrees are emitted as nearest 12-TET note + bend, and that
        // offset is always <= +/-50 cents. 2 st (the universal MPE/synth default)
        // gives ample range AND is interpreted correctly by receivers that don't
        // adopt our bend-range RPN, so the microtonal scale survives. Wider ranges
        // are only needed for large Glide-mode pitch slides.

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
        ParameterID ("spectralPartialCount", 1), "Element Partial", 1, PartialEngine::MAX_ELEMENT_PARTIALS, PartialEngine::MAX_ELEMENT_PARTIALS));

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
    rawParams.mpeZone = apvts.getRawParameterValue("mpeZone");
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
    mpeOut.reset();
    mpeOut.markSetupDirty();
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
    engine.spectralPartialCount.store(rawParamInt(rawParams.spectralPartialCount, PartialEngine::MAX_ELEMENT_PARTIALS));
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
    const int bendRange = MpeMidiOutput::bendRangeFromChoice(rawParamInt(rawParams.mpePitchBendRange, 3));
    // B8: the member range (and master) are now DERIVED from the MPE zone, so the
    // change-detection tracks the ZONE-derived channels. Switching Lower<->Upper
    // changes both the master and the member first/last, which flips
    // midiConfigChanged -> the existing safety path (emitSafetyReset + reset on the
    // OLD channels, then markSetupDirty so the MCM is re-sent on the NEW master and
    // allocation moves to the NEW member range) fires exactly as for any other
    // config change.
    const auto zone = zoneChannels(rawParamInt(rawParams.mpeZone, 0));
    const int mpeMaster = juce::jlimit(1, 16, zone.master);
    const int mpeFirst = juce::jlimit(1, 16, zone.memberFirst);
    const int mpeLast = juce::jlimit(mpeFirst, 16, zone.memberLast);
    const int setupEnabled = rawParamBool(rawParams.mpeSendSetupMessages, true) ? 1 : 0;

    const bool midiConfigChanged = outputMode != lastAudioMidiOutputMode
        || midiType != lastMidiOutputType
        || bendRange != lastMpeBendRange
        || mpeMaster != lastMpeMaster
        || mpeFirst != lastMpeMemberFirst
        || mpeLast != lastMpeMemberLast
        || setupEnabled != lastMpeSetupEnabled;
    const bool needsSafetyAllOff = midiConfigChanged
        && (lastAudioMidiOutputMode != 0 && lastMidiOutputType != 0);

    lastAudioMidiOutputMode = outputMode;
    lastMidiOutputType = midiType;
    lastMpeBendRange = bendRange;
    lastMpeMaster = mpeMaster;
    lastMpeMemberFirst = mpeFirst;
    lastMpeMemberLast = mpeLast;
    lastMpeSetupEnabled = setupEnabled;

    // Mirror the just-computed member range into the MPE output engine before any
    // reset below, matching the old ordering where lastMpeMemberFirst/Last were
    // assigned ahead of resetMidiOutputState (so availableMpeChannels is correct).
    mpeOut.setMemberRange(mpeFirst, mpeLast);

    if (midiConfigChanged)
        mpeOut.markSetupDirty();

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
        mpeOut.emitSafetyReset(outputMidi, 0);
        mpeOut.reset();
    }
    else if (midiConfigChanged)
    {
        mpeOut.reset();
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
        mpeOut.reset();
    }

    mpeOut.recordOutgoingMidiDebugEvents(outputMidi);
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
    // B8: derive the member range we guard against from the active MPE zone (the
    // zone owns the layout) so the "ignore local MPE member input" range matches
    // what we actually emit on (Lower: 2..16, Upper: 1..15).
    const auto incomingZone = zoneChannels(rawParamInt(rawParams.mpeZone, 0));
    const int mpeFirst = juce::jlimit(1, 16, incomingZone.memberFirst);
    const int mpeLast = juce::jlimit(mpeFirst, 16, incomingZone.memberLast);

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
                pendingAction[(size_t) key] = -1;
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
        // Feedback / redundant-trigger guard: a key that is already sounding must NOT be
        // retriggered by another Note On unless it was force-released. When our MPE output
        // is monitored or looped back (virtual port also open as input, or a host routing
        // out -> in), our own notes echo back as repeated Note Ons; retriggering on them
        // produced a Note Off/On storm that destabilised polyphonic per-note pitch bends
        // (the receiver collapsed to 12-TET). Holding a key is a single press, so any
        // further Note On with no intervening Note Off is redundant -> keep the voice.
        // Real re-strikes still work: they send Note Off first, which frees the slot.
        if (alreadyActive && pendingForcedOff[(size_t) key] == 0)
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

juce::String AudienceProcessor::getExternalMidiPitchModeName() const
{
    switch (juce::jlimit(0, 2, externalMidiPitchModeSnapshot.load(std::memory_order_relaxed)))
    {
        case 1:  return "Quantize To Current Scale";
        case 2:  return "Use As Trigger For Audience Pitch";
        default: return "Direct MIDI Pitch";
    }
}

MpeMidiOutput::MpeConfig AudienceProcessor::buildMpeConfig() const
{
    MpeMidiOutput::MpeConfig config;
    config.outputType           = rawParamInt(rawParams.midiOutputType);

    // B8: the MPE zone now fully OWNS the channel layout. We DERIVE the master and
    // member-channel range from the mpeZone choice index instead of reading the
    // manual mpeMasterChannel / mpeMemberFirstChannel / mpeMemberLastChannel params
    // (those stay in the APVTS layout purely for session compatibility, but are no
    // longer consulted here):
    //   Lower (0): master 1,  members 2..16  (15 members) - historical default,
    //              byte-identical to the prior behaviour.
    //   Upper (1): master 16, members 1..15  (15 members).
    // The derived member range never includes the master channel in either zone.
    const auto zone = zoneChannels(rawParamInt(rawParams.mpeZone, 0));
    config.masterChannel        = zone.master;
    config.memberFirst          = zone.memberFirst;
    config.memberLast           = zone.memberLast;

    config.pitchBendRangeChoice = rawParamInt(rawParams.mpePitchBendRange, 3);
    config.normalMidiChannel    = rawParamInt(rawParams.normalMidiChannel, 0);
    config.sendSetupMessages    = rawParamBool(rawParams.mpeSendSetupMessages, true);
    config.pitchMode            = rawParamInt(rawParams.mpePitchMode);
    config.motionMacro          = rawParamValue(rawParams.motionMacro, 0.5f);
    config.energy               = rawParamValue(rawParams.energy, 0.5f);
    return config;
}

void AudienceProcessor::renderOutgoingMidi (juce::MidiBuffer& midiMessages, int numSamples)
{
    const auto config = buildMpeConfig();

    const int maxEvents = (int) midiSourceScratch.size();
    const int count = engine.drainMidiSourceEvents(midiSourceScratch.data(), maxEvents);

    // Convert PartialEngine::MidiSourceEvent -> MpeMidiOutput::NoteEvent (trivial
    // field copy + event-type enum mapping). midiNoteEventScratch mirrors the
    // size of midiSourceScratch so the conversion is allocation-free.
    for (int i = 0; i < count; ++i)
    {
        const auto& src = midiSourceScratch[(size_t) i];
        auto& dst = midiNoteEventScratch[(size_t) i];
        switch (src.type)
        {
            case PartialEngine::MidiSourceEvent::NoteOff:     dst.type = MpeMidiOutput::NoteEvent::NoteOff; break;
            case PartialEngine::MidiSourceEvent::Expression:  dst.type = MpeMidiOutput::NoteEvent::Expression; break;
            case PartialEngine::MidiSourceEvent::AllNotesOff: dst.type = MpeMidiOutput::NoteEvent::AllNotesOff; break;
            case PartialEngine::MidiSourceEvent::NoteOn:
            default:                                          dst.type = MpeMidiOutput::NoteEvent::NoteOn; break;
        }
        dst.sourceId    = src.sourceId;
        dst.frequencyHz = src.frequencyHz;
        dst.velocity    = src.velocity;
        dst.x           = src.x;
        dst.y           = src.y;
    }

    mpeOut.render(config, midiNoteEventScratch.data(), count, midiMessages, numSamples);
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

    if (pendingStateApply.exchange(false, std::memory_order_acquire))
    {
        setUdpPort(pendingUdpPort);
        setMidiOutputOptionIndex(pendingMidiOutputOption);
        if (pendingLibraryName.isNotEmpty())
            setCurrentLibrary(pendingLibraryName);
    }
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
    const int bendRange = MpeMidiOutput::bendRangeFromChoice(rawParamInt(rawParams.mpePitchBendRange, 3));
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

    const auto latest = mpeOut.getOutgoingDebugLatest();
    const int count = juce::jlimit(0, MpeMidiOutput::kDebugEventQueueSize,
                                   juce::jmin(maxEvents, (int) latest));
    if (count <= 0)
    {
        s << "(no outgoing MIDI captured yet)\n";
        return s;
    }

    const uint32_t firstSeq = latest - (uint32_t) count + 1;
    for (uint32_t seq = firstSeq; seq <= latest; ++seq)
    {
        MpeMidiOutput::OutgoingDebugEvent slot;
        if (! mpeOut.readOutgoingDebugSlot(seq, slot))
            continue;

        const int sample = slot.sampleOffset;
        const int size = slot.size;
        const int b0 = slot.b0 & 0xff;
        const int b1 = slot.b1 & 0xff;
        const int b2 = slot.b2 & 0xff;
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
        // B8: the guarded member range follows the active MPE zone (Lower 2..16,
        // Upper 1..15), matching the channels processIncomingMidiKeyboard ignores.
        const auto zone = zoneChannels(rawParamInt(rawParams.mpeZone, 0));
        const int first = juce::jlimit(1, 16, zone.memberFirst);
        const int last = juce::jlimit(first, 16, zone.memberLast);
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
    // B7: surface the external-MIDI FIFO overflow counter so a full
    // host-output queue (dropped outgoing messages) is visible in the report.
    s << "MIDI out dropped   : "
      << (int) externalMidiDropped.load(std::memory_order_relaxed) << "\n";
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
    for (int voiceIndex = 0; voiceIndex < mpeOut.getVoiceDebugCount(); ++voiceIndex)
    {
        const auto voice = mpeOut.getVoiceDebugSnapshot(voiceIndex);
        if (voice.active == 0)
            continue;

        const int sourceId = voice.sourceId;
        const int ch = voice.channel;
        const int note = voice.note;
        const int bend = voice.pitchBend;
        const int age = voice.age;
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
    options.add("Virtual: SpektraSynth MIDI Out");

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
            ? "SpektraSynth MIDI Out"
            : "SpektraSynth MIDI Out " + juce::String(instanceId);

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

    for (const auto& noteOff : mpeOut.getActiveNoteOffs())
        midiOutput->sendMessageNow(juce::MidiMessage::noteOff(noteOff.channel, noteOff.note));

    for (int ch = 1; ch <= 16; ++ch)
    {
        midiOutput->sendMessageNow(juce::MidiMessage::channelPressureChange(ch, 0));
        midiOutput->sendMessageNow(juce::MidiMessage::pitchWheel(ch, 8192));
        midiOutput->sendMessageNow(juce::MidiMessage::allNotesOff(ch));
        midiOutput->sendMessageNow(juce::MidiMessage::allSoundOff(ch));
    }

    mpeOut.reset();
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
    osc.start(port);
    // B25: take the UI-visible status straight from OscBridge::oscStatus() so the
    // shared-UDP-port "PORT FULL" condition (when the 16-client cap is hit and this
    // instance receives no OSC) becomes visible in the DebugPanel, instead of the
    // old text which only distinguished bound vs. bind-failed. The bridge string
    // already covers Listening / FAILED-to-bind / PORT FULL; connection logic is
    // unchanged.
    oscStatus = osc.oscStatus();
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

            // Defer socket bind / MIDI device open / sample-library disk I/O to the
            // message-thread timer. The host may call this on a background thread or
            // before prepareToPlay, where doing that work inline can deadlock or race.
            pendingUdpPort          = (int) state.getProperty("udpPort", 6060);
            pendingMidiOutputOption = (int) state.getProperty("midiOutputOption", 0);
            pendingLibraryName      = state.getProperty("currentLibrary").toString();
            pendingStateApply.store(true, std::memory_order_release);
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
