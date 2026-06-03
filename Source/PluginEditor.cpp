#include "PluginEditor.h"
#include "UiText.h"
#include <algorithm>
#include <cmath>

#if JUCE_ANDROID
 #include <juce_audio_utils/juce_audio_utils.h>
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace cs
{
    // SpektraSynth "Spektra Performance" palette.
    // Single interactive accent (accBlue = cyan); the other accents are an
    // intentional, harmonious set rather than six unrelated hues. accRed is
    // reserved for panic/warnings. Text tuned for WCAG-AA contrast on bg.
    const juce::Colour bg           { 0xff0b0d12 };
    const juce::Colour bg2          { 0xff0e1218 };
    const juce::Colour panel        { 0xff0e1116 };
    const juce::Colour panel2       { 0xff141a22 };
    const juce::Colour hairline     { 0xff1c2530 };
    const juce::Colour text         { 0xffeef2f7 };
    const juce::Colour text2        { 0xffcdd6e0 };
    const juce::Colour text3        { 0xff8a93a0 };
    const juce::Colour text4        { 0xff5a6470 };
    const juce::Colour accGreen     { 0xff22d3a8 };
    const juce::Colour accBlue      { 0xff5ec8ff };   // primary interactive accent
    const juce::Colour accAmber     { 0xffe8d44d };
    const juce::Colour accViolet    { 0xffb06bff };
    const juce::Colour accTeal      { 0xff22d3a8 };
    const juce::Colour accRed       { 0xffe23d52 };

    // Spectral wavelength ramp (short -> long). Used to colour-code emission
    // lines, element tags and active seats by pitch.
    inline juce::Colour spectral (float t) noexcept
    {
        t = juce::jlimit (0.0f, 1.0f, t);
        static const juce::Colour stops[] = {
            juce::Colour (0xff6a2cf5), juce::Colour (0xff3f6bff),
            juce::Colour (0xff22b8d8), juce::Colour (0xff22d3a8),
            juce::Colour (0xff9be84d), juce::Colour (0xffe8d44d),
            juce::Colour (0xffff9a3d), juce::Colour (0xffff4d5e)
        };
        constexpr int n = (int) (sizeof (stops) / sizeof (stops[0]));
        const float scaled = t * (float) (n - 1);
        const int   i = juce::jlimit (0, n - 2, (int) scaled);
        return stops[i].interpolatedWith (stops[i + 1], scaled - (float) i);
    }
}

static void styleKnob (juce::Slider& s, juce::Colour accent)
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 16);
    s.setColour(juce::Slider::rotarySliderFillColourId,    accent);
    s.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff34374a));
    s.setColour(juce::Slider::thumbColourId,               juce::Colours::white);
    s.setColour(juce::Slider::textBoxTextColourId,         cs::text2);
    s.setColour(juce::Slider::textBoxBackgroundColourId,   cs::bg);
    s.setColour(juce::Slider::textBoxOutlineColourId,      cs::hairline);
}

static void styleLiveKnob (juce::Slider& s, juce::Colour accent)
{
    styleKnob(s, accent);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 58, 16);
}

static void styleLabel (juce::Label& l, const juce::String& txt, juce::Colour col = cs::text3)
{
    l.setText(txt, juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centred);
    l.setColour(juce::Label::textColourId, col);
    l.setFont(juce::Font(juce::FontOptions(11.5f)));
}

static void styleCombo (juce::ComboBox& c)
{
    c.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff111625));
    c.setColour(juce::ComboBox::outlineColourId,    cs::hairline);
    c.setColour(juce::ComboBox::textColourId,       cs::text);
    c.setColour(juce::ComboBox::arrowColourId,      cs::accBlue);
}

static void styleToggle (juce::ToggleButton& b, juce::Colour accent)
{
    b.setColour(juce::ToggleButton::textColourId,         cs::text2);
    b.setColour(juce::ToggleButton::tickColourId,         accent);
    b.setColour(juce::ToggleButton::tickDisabledColourId, cs::hairline);
}

namespace
{
    constexpr std::array<char, 36> kComputerKeys {{
        '1','2','3','4','5','6','7','8','9','0',
        'Q','W','E','R','T','Y','U','I','O','P',
        'A','S','D','F','G','H','J','K','L',
        'Z','X','C','V','B','N','M'
    }};

    bool computerKeyIsDown (char key)
    {
        if (key >= 'A' && key <= 'Z')
            return juce::KeyPress::isKeyCurrentlyDown(key)
                || juce::KeyPress::isKeyCurrentlyDown(key - 'A' + 'a');

        return juce::KeyPress::isKeyCurrentlyDown(key);
    }
}

AudienceEditor::AudienceEditor (AudienceProcessor& p)
    : juce::AudioProcessorEditor(&p), proc(p), libraryRail(p), aurora(p.engine),
      debugPanel(p)
{
    setSize(1280, 820);
    setResizable(true, true);
    setResizeLimits(1100, 680, 2600, 1600);
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);

    addAndMakeVisible(libraryRail);
    addAndMakeVisible(aurora);
    addChildComponent(debugPanel);   // hidden until toggled
    debugToggle.setColour(juce::ToggleButton::textColourId,         cs::text2);
    debugToggle.setColour(juce::ToggleButton::tickColourId,         cs::accBlue);
    debugToggle.setColour(juce::ToggleButton::tickDisabledColourId, cs::hairline);
    debugToggle.onClick = [this]() {
        updatePerformanceVisibility();
    };
    addAndMakeVisible(debugToggle);

    performanceToggle.setColour(juce::ToggleButton::textColourId,         cs::text2);
    performanceToggle.setColour(juce::ToggleButton::tickColourId,         cs::accAmber);
    performanceToggle.setColour(juce::ToggleButton::tickDisabledColourId, cs::hairline);
    performanceToggle.onClick = [this]() { updatePerformanceVisibility(); repaint(); };
    addAndMakeVisible(performanceToggle);

    // ---- left panel (Texture / Global) ----
    styleKnob(pitchSlider,    cs::accBlue);
    styleKnob(layerMixSlider, cs::accBlue);
    styleKnob(wetDrySlider,   cs::accTeal);
    styleKnob(reverbSlider,   cs::accTeal);
    styleKnob(delaySlider,    cs::accViolet);
    styleKnob(tapeDriveSlider, cs::accAmber);
    styleKnob(masterSlider,   cs::accAmber);
    addAndMakeVisible(pitchSlider);
    addAndMakeVisible(layerMixSlider);
    addAndMakeVisible(wetDrySlider);
    addAndMakeVisible(reverbSlider);
    addAndMakeVisible(delaySlider);
    addAndMakeVisible(tapeDriveSlider);
    addAndMakeVisible(masterSlider);
    pitchSlider.setTextValueSuffix(" st");
    layerMixSlider.setTextValueSuffix("");
    wetDrySlider.setTextValueSuffix("");
    reverbSlider.setTextValueSuffix("");
    delaySlider.setTextValueSuffix("");
    tapeDriveSlider.setTextValueSuffix("");
    masterSlider.setTextValueSuffix("");

    styleLabel(pitchLabel,    "PITCH");
    styleLabel(layerMixLabel, "LAYER MIX");
    styleLabel(wetDryLabel,   "WET/DRY");
    styleLabel(reverbLabel,   "REVERB");
    styleLabel(delayLabel,    "DELAY");
    styleLabel(tapeDriveLabel, "TAPE");
    styleLabel(masterLabel,   "MASTER");
    addAndMakeVisible(pitchLabel);
    addAndMakeVisible(layerMixLabel);
    addAndMakeVisible(wetDryLabel);
    addAndMakeVisible(reverbLabel);
    addAndMakeVisible(delayLabel);
    addAndMakeVisible(tapeDriveLabel);
    addAndMakeVisible(masterLabel);

    // ---- right panel (Voice / Spectral) ----
    styleKnob(attackSlider,     cs::accGreen);
    styleKnob(releaseSlider,    cs::accGreen);
    styleKnob(brightnessSlider, cs::accViolet);
    styleKnob(movementSlider,   cs::accGreen);
    styleKnob(grainSizeSlider,  cs::accBlue);
    styleKnob(grainDensitySlider, cs::accGreen);
    styleKnob(pitchSpreadSlider, cs::accViolet);
    styleKnob(positionJitterSlider, cs::accTeal);
    styleKnob(stereoSpreadSlider, cs::accAmber);
    addAndMakeVisible(attackSlider);
    addAndMakeVisible(releaseSlider);
    addAndMakeVisible(brightnessSlider);
    addAndMakeVisible(movementSlider);
    addAndMakeVisible(grainSizeSlider);
    addAndMakeVisible(grainDensitySlider);
    addAndMakeVisible(pitchSpreadSlider);
    addAndMakeVisible(positionJitterSlider);
    addAndMakeVisible(stereoSpreadSlider);
    attackSlider.setTextValueSuffix(" ms");
    releaseSlider.setTextValueSuffix(" ms");
    brightnessSlider.setTextValueSuffix("");
    movementSlider.setTextValueSuffix("");
    grainSizeSlider.setTextValueSuffix(" ms");
    grainDensitySlider.setTextValueSuffix("");
    pitchSpreadSlider.setTextValueSuffix(" st");
    positionJitterSlider.setTextValueSuffix("");
    stereoSpreadSlider.setTextValueSuffix("");

    styleLabel(attackLabel,     "ATTACK");
    styleLabel(releaseLabel,    "RELEASE");
    styleLabel(brightnessLabel, "BRIGHTNESS");
    styleLabel(movementLabel,   "MOVEMENT");
    styleLabel(grainSizeLabel,  "SIZE");
    styleLabel(grainDensityLabel, "DENSITY");
    styleLabel(pitchSpreadLabel,  "PITCH SPREAD");
    styleLabel(positionJitterLabel, "POSITION");
    styleLabel(stereoSpreadLabel, "STEREO");
    addAndMakeVisible(attackLabel);
    addAndMakeVisible(releaseLabel);
    addAndMakeVisible(brightnessLabel);
    addAndMakeVisible(movementLabel);
    addAndMakeVisible(grainSizeLabel);
    addAndMakeVisible(grainDensityLabel);
    addAndMakeVisible(pitchSpreadLabel);
    addAndMakeVisible(positionJitterLabel);
    addAndMakeVisible(stereoSpreadLabel);

    styleToggle(reverseToggle, cs::accViolet);
    addAndMakeVisible(reverseToggle);
    const juce::StringArray grainShapes { "Hann", "Triangle", "Soft Gate", "Pulse" };
    for (int i = 0; i < grainShapes.size(); ++i) grainShapeCombo.addItem(grainShapes[i], i + 1);
    styleCombo(grainShapeCombo);
    styleLabel(grainShapeLabel, "ENV");
    addAndMakeVisible(grainShapeCombo);
    addAndMakeVisible(grainShapeLabel);

    pitchAttach      = std::make_unique<SA>(proc.apvts, "pitch",      pitchSlider);
    layerMixAttach   = std::make_unique<SA>(proc.apvts, "layerMix",   layerMixSlider);
    wetDryAttach     = std::make_unique<SA>(proc.apvts, "wetDry",     wetDrySlider);
    reverbAttach     = std::make_unique<SA>(proc.apvts, "reverb",     reverbSlider);
    delayAttach      = std::make_unique<SA>(proc.apvts, "delay",      delaySlider);
    tapeDriveAttach  = std::make_unique<SA>(proc.apvts, "tapeDrive",  tapeDriveSlider);
    masterAttach     = std::make_unique<SA>(proc.apvts, "master",     masterSlider);
    attackAttach     = std::make_unique<SA>(proc.apvts, "attack",     attackSlider);
    releaseAttach    = std::make_unique<SA>(proc.apvts, "release",    releaseSlider);
    brightnessAttach = std::make_unique<SA>(proc.apvts, "brightness", brightnessSlider);
    movementAttach   = std::make_unique<SA>(proc.apvts, "movement",   movementSlider);
    grainSizeAttach  = std::make_unique<SA>(proc.apvts, "grainSize",  grainSizeSlider);
    grainDensityAttach = std::make_unique<SA>(proc.apvts, "grainDensity", grainDensitySlider);
    pitchSpreadAttach = std::make_unique<SA>(proc.apvts, "pitchSpread", pitchSpreadSlider);
    positionJitterAttach = std::make_unique<SA>(proc.apvts, "positionJitter", positionJitterSlider);
    stereoSpreadAttach = std::make_unique<SA>(proc.apvts, "stereoSpread", stereoSpreadSlider);
    reverseAttach    = std::make_unique<BA>(proc.apvts, "reverseGrains", reverseToggle);
    grainShapeAttach = std::make_unique<CA>(proc.apvts, "grainShape", grainShapeCombo);

    // ---- performance macros + musical mapping ----
    styleLiveKnob(energySlider,      cs::accAmber);
    styleLiveKnob(motionMacroSlider, cs::accGreen);
    styleLiveKnob(toneMacroSlider,   cs::accViolet);
    styleLiveKnob(spaceMacroSlider,  cs::accTeal);
    energySlider     .setTextValueSuffix("");
    motionMacroSlider.setTextValueSuffix("");
    toneMacroSlider  .setTextValueSuffix("");
    spaceMacroSlider .setTextValueSuffix("");

    styleLabel(energyLabel,      "ENERGY", cs::accAmber);
    styleLabel(motionMacroLabel, "MOTION", cs::accGreen);
    styleLabel(toneMacroLabel,   "TONE",   cs::accViolet);
    styleLabel(spaceMacroLabel,  "SPACE",  cs::accTeal);

    addAndMakeVisible(energySlider);
    addAndMakeVisible(motionMacroSlider);
    addAndMakeVisible(toneMacroSlider);
    addAndMakeVisible(spaceMacroSlider);
    addAndMakeVisible(energyLabel);
    addAndMakeVisible(motionMacroLabel);
    addAndMakeVisible(toneMacroLabel);
    addAndMakeVisible(spaceMacroLabel);

    const juce::StringArray engineSources { "Sample Library", "Element Spectral Synth" };
    for (int i = 0; i < engineSources.size(); ++i) engineSourceCombo.addItem(engineSources[i], i + 1);
    styleCombo(engineSourceCombo);
    styleLabel(engineSourceLabel, "ENGINE");
    addAndMakeVisible(engineSourceCombo);
    addAndMakeVisible(engineSourceLabel);

    const juce::StringArray playbackModes { "Sample Player", "Granular" };
    for (int i = 0; i < playbackModes.size(); ++i) samplePlaybackCombo.addItem(playbackModes[i], i + 1);
    styleCombo(samplePlaybackCombo);
    styleLabel(samplePlaybackLabel, "SAMPLE PLAYBACK");
    addAndMakeVisible(samplePlaybackCombo);
    addAndMakeVisible(samplePlaybackLabel);

    const juce::StringArray soundModes { "Choir Cloud", "Glass Harmonics", "Sub Swarm", "Spectral Rain", "Frozen Hall" };
    for (int i = 0; i < soundModes.size(); ++i) soundModeCombo.addItem(soundModes[i], i + 1);
    styleCombo(soundModeCombo);
    styleLabel(soundModeLabel, "SOUND MODE");
    addAndMakeVisible(soundModeCombo);
    addAndMakeVisible(soundModeLabel);

    const juce::StringArray elements { "Hydrogen", "Helium", "Lithium", "Beryllium",
                                       "Boron", "Carbon", "Oxygen", "Fluorine", "Neon",
                                       "Sodium", "Magnesium", "Aluminium", "Silicon", "Phosphorus",
                                       "Sulfur", "Chlorine", "Argon", "Potassium", "Calcium",
                                       "Scandium", "Titanium", "Vanadium", "Chromium", "Manganese",
                                       "Iron", "Cobalt", "Nickel", "Copper", "Zinc" };
    for (int i = 0; i < elements.size(); ++i) spectralElementCombo.addItem(elements[i], i + 1);
    styleCombo(spectralElementCombo);
    styleLabel(spectralElementLabel, "ELEMENT");
    styleKnob(spectralPartialSlider, cs::accAmber);
    spectralPartialSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    spectralPartialSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 34, 18);
    spectralPartialSlider.setTextValueSuffix("");
    spectralPartialSlider.setColour(juce::Slider::trackColourId, cs::accAmber);
    spectralPartialSlider.setColour(juce::Slider::backgroundColourId, cs::hairline);
    styleLabel(spectralPartialLabel, "PARTIAL", cs::accAmber);
    styleToggle(partialSoloToggle, cs::accAmber);
    styleKnob(spectralStretchSlider, cs::accTeal);
    spectralStretchSlider.setTextValueSuffix(" str");
    styleLabel(spectralStretchLabel, "STRETCH");
    const juce::StringArray atomicScaleModes { "Core", "Extended", "Microtonal", "Scientific", "Raw" };
    for (int i = 0; i < atomicScaleModes.size(); ++i) atomicScaleModeCombo.addItem(atomicScaleModes[i], i + 1);
    styleCombo(atomicScaleModeCombo);
    styleLabel(atomicScaleModeLabel, "ATOM SCALE");
    addAndMakeVisible(spectralElementCombo);
    addAndMakeVisible(spectralElementLabel);
    addAndMakeVisible(spectralPartialSlider);
    addAndMakeVisible(spectralPartialLabel);
    addAndMakeVisible(partialSoloToggle);
    addAndMakeVisible(atomicScaleModeCombo);
    addAndMakeVisible(atomicScaleModeLabel);
    addAndMakeVisible(spectralStretchSlider);
    addAndMakeVisible(spectralStretchLabel);

    const juce::StringArray roots { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int i = 0; i < roots.size(); ++i) rootCombo.addItem(roots[i], i + 1);
    const juce::StringArray rootOctaves { "0", "1", "2", "3", "4", "5", "6" };
    for (int i = 0; i < rootOctaves.size(); ++i) rootOctaveCombo.addItem(rootOctaves[i], i + 1);
    const juce::StringArray scales { "Major", "Natural Minor", "Pentatonic", "Dorian", "Lydian", "Harmonic Minor", "Whole Tone",
                                     "Hydrogen Spectrum", "Helium Spectrum", "Lithium Spectrum", "Beryllium Spectrum",
                                     "Boron Spectrum", "Carbon Spectrum", "Oxygen Spectrum", "Fluorine Spectrum", "Neon Spectrum",
                                     "Sodium Spectrum", "Magnesium Spectrum", "Aluminium Spectrum", "Silicon Spectrum",
                                     "Phosphorus Spectrum", "Sulfur Spectrum", "Chlorine Spectrum", "Argon Spectrum",
                                     "Potassium Spectrum", "Calcium Spectrum", "Scandium Spectrum", "Titanium Spectrum",
                                     "Vanadium Spectrum", "Chromium Spectrum", "Manganese Spectrum",
                                     "Iron Spectrum", "Cobalt Spectrum", "Nickel Spectrum", "Copper Spectrum",
                                     "Zinc Spectrum" };
    for (int i = 0; i < scales.size(); ++i) scaleCombo.addItem(scales[i], i + 1);
    const juce::StringArray polyphonyModes { "Normal", "High", "Ultra" };
    for (int i = 0; i < polyphonyModes.size(); ++i) polyphonyCombo.addItem(polyphonyModes[i], i + 1);
    const juce::StringArray audioMidiOutputModes { "Audio Only", "MIDI Only", "Audio + MIDI" };
    for (int i = 0; i < audioMidiOutputModes.size(); ++i) audioMidiOutputModeCombo.addItem(audioMidiOutputModes[i], i + 1);
    const juce::StringArray midiOutputTypes { "Off", "Normal MIDI", "MPE MIDI" };
    for (int i = 0; i < midiOutputTypes.size(); ++i) midiOutputTypeCombo.addItem(midiOutputTypes[i], i + 1);
    const juce::StringArray externalMidiPitchModes { "Direct", "Scale", "Trigger" };
    for (int i = 0; i < externalMidiPitchModes.size(); ++i) externalMidiPitchModeCombo.addItem(externalMidiPitchModes[i], i + 1);
    for (int ch = 1; ch <= 16; ++ch) normalMidiChannelCombo.addItem(juce::String(ch), ch);
    const juce::StringArray bendRanges { "2 st", "12 st", "24 st", "48 st" };
    for (int i = 0; i < bendRanges.size(); ++i) mpeBendRangeCombo.addItem(bendRanges[i], i + 1);
    const juce::StringArray mpePitchModes { "Retrig", "Glide" };
    for (int i = 0; i < mpePitchModes.size(); ++i) mpePitchModeCombo.addItem(mpePitchModes[i], i + 1);
    styleCombo(rootCombo);
    styleCombo(rootOctaveCombo);
    styleCombo(scaleCombo);
    styleCombo(polyphonyCombo);
    styleCombo(audioMidiOutputModeCombo);
    styleCombo(midiOutputTypeCombo);
    styleCombo(externalMidiPitchModeCombo);
    styleCombo(normalMidiChannelCombo);
    styleCombo(mpeBendRangeCombo);
    styleCombo(mpePitchModeCombo);
    styleCombo(midiOutputDeviceCombo);
    styleKnob(octavesSlider, cs::accBlue);
    octavesSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    octavesSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 42, 20);
    octavesSlider.setTextValueSuffix(" oct");
    styleLabel(rootLabel,    "ROOT");
    styleLabel(rootOctaveLabel, "OCT");
    styleLabel(scaleLabel,   "SCALE");
    styleLabel(octavesLabel, "RANGE");
    styleLabel(polyphonyLabel, "POLY");
    styleLabel(audioMidiOutputModeLabel, "OUTPUT MODE");
    styleLabel(midiOutputTypeLabel, "MIDI OUT");
    styleLabel(externalMidiPitchModeLabel, "MIDI IN");
    styleLabel(normalMidiChannelLabel, "CH");
    styleLabel(mpeBendRangeLabel, "BEND");
    styleLabel(mpePitchModeLabel, "MPE PITCH");
    styleLabel(midiOutputDeviceLabel, "MIDI OUTPUT", cs::accBlue);
    midiOutputStatusLabel.setJustificationType(juce::Justification::centredLeft);
    midiOutputStatusLabel.setColour(juce::Label::textColourId, cs::text3);
    midiOutputStatusLabel.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain)));
    midiActivityLabel.setJustificationType(juce::Justification::centredLeft);
    midiActivityLabel.setColour(juce::Label::textColourId, cs::text3);
    midiActivityLabel.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain)));
    midiOutputRefreshBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff182234));
    midiOutputRefreshBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff22304a));
    midiOutputRefreshBtn.setColour(juce::TextButton::textColourOffId, cs::accBlue);
    midiOutputDeviceCombo.onChange = [this]()
    {
        proc.setMidiOutputOptionIndex(midiOutputDeviceCombo.getSelectedItemIndex());
        refreshMidiOutputCombo();
    };
    midiOutputRefreshBtn.onClick = [this]() { refreshMidiOutputCombo(); };
    styleToggle(mpeSetupToggle, cs::accTeal);
    addAndMakeVisible(rootCombo);
    addAndMakeVisible(rootOctaveCombo);
    addAndMakeVisible(scaleCombo);
    addAndMakeVisible(polyphonyCombo);
    addAndMakeVisible(audioMidiOutputModeCombo);
    addAndMakeVisible(midiOutputTypeCombo);
    addAndMakeVisible(externalMidiPitchModeCombo);
    addAndMakeVisible(normalMidiChannelCombo);
    addAndMakeVisible(mpeBendRangeCombo);
    addAndMakeVisible(mpePitchModeCombo);
    addAndMakeVisible(midiOutputDeviceCombo);
    addAndMakeVisible(mpeSetupToggle);
    addAndMakeVisible(midiOutputRefreshBtn);
    addAndMakeVisible(octavesSlider);
    addAndMakeVisible(rootLabel);
    addAndMakeVisible(rootOctaveLabel);
    addAndMakeVisible(scaleLabel);
    addAndMakeVisible(octavesLabel);
    addAndMakeVisible(polyphonyLabel);
    addAndMakeVisible(audioMidiOutputModeLabel);
    addAndMakeVisible(midiOutputTypeLabel);
    addAndMakeVisible(externalMidiPitchModeLabel);
    addAndMakeVisible(normalMidiChannelLabel);
    addAndMakeVisible(mpeBendRangeLabel);
    addAndMakeVisible(mpePitchModeLabel);
    addAndMakeVisible(midiOutputDeviceLabel);
    addAndMakeVisible(midiOutputStatusLabel);
    addAndMakeVisible(midiActivityLabel);

    panicBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff3b1f25));
    panicBtn.setColour(juce::TextButton::textColourOffId, cs::accRed);
    panicBtn.onClick = [this]() { proc.panic(); };
    styleToggle(muteToggle, cs::accRed);
    muteToggle.onClick = [this]() { proc.setMuted(muteToggle.getToggleState()); };
    styleToggle(freezeToggle, cs::accTeal);
    addAndMakeVisible(panicBtn);
    addAndMakeVisible(muteToggle);
    addAndMakeVisible(freezeToggle);

    audioSettingsBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1f2d44));
    audioSettingsBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2a3a55));
    audioSettingsBtn.setColour(juce::TextButton::textColourOffId, cs::accBlue);
    audioSettingsBtn.onClick = [this]() { showAudioSettings(); };
    styleLabel(audioDeviceStatusLabel, getAudioDeviceStatusText(), cs::text3);
    audioDeviceStatusLabel.setJustificationType(juce::Justification::centredLeft);
    audioDeviceStatusLabel.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 8.5f, juce::Font::plain)));
   #if JUCE_ANDROID
    addAndMakeVisible(audioSettingsBtn);
    addAndMakeVisible(audioDeviceStatusLabel);
   #else
    addChildComponent(audioSettingsBtn);
    addChildComponent(audioDeviceStatusLabel);
   #endif

    energyAttach      = std::make_unique<SA>(proc.apvts, "energy",      energySlider);
    motionMacroAttach = std::make_unique<SA>(proc.apvts, "motionMacro", motionMacroSlider);
    toneMacroAttach   = std::make_unique<SA>(proc.apvts, "toneMacro",   toneMacroSlider);
    spaceMacroAttach  = std::make_unique<SA>(proc.apvts, "spaceMacro",  spaceMacroSlider);
    soundModeAttach   = std::make_unique<CA>(proc.apvts, "signatureMode", soundModeCombo);
    engineSourceAttach = std::make_unique<CA>(proc.apvts, "engineSource", engineSourceCombo);
    samplePlaybackAttach = std::make_unique<CA>(proc.apvts, "samplePlaybackMode", samplePlaybackCombo);
    spectralElementAttach = std::make_unique<CA>(proc.apvts, "spectralElement", spectralElementCombo);
    spectralPartialAttach = std::make_unique<SA>(proc.apvts, "spectralPartialCount", spectralPartialSlider);
    partialSoloAttach = std::make_unique<BA>(proc.apvts, "spectralPartialSolo", partialSoloToggle);
    spectralStretchAttach = std::make_unique<SA>(proc.apvts, "spectralStretch", spectralStretchSlider);
    atomicScaleModeAttach = std::make_unique<CA>(proc.apvts, "atomicScaleMode", atomicScaleModeCombo);
    rootAttach        = std::make_unique<CA>(proc.apvts, "scaleRoot",   rootCombo);
    rootOctaveAttach  = std::make_unique<CA>(proc.apvts, "scaleRootOctave", rootOctaveCombo);
    scaleAttach       = std::make_unique<CA>(proc.apvts, "scaleMode",   scaleCombo);
    octavesAttach     = std::make_unique<SA>(proc.apvts, "scaleOctaves", octavesSlider);
    polyphonyAttach   = std::make_unique<CA>(proc.apvts, "polyphonyMode", polyphonyCombo);
    audioMidiOutputModeAttach = std::make_unique<CA>(proc.apvts, "audioMidiOutputMode", audioMidiOutputModeCombo);
    midiOutputTypeAttach = std::make_unique<CA>(proc.apvts, "midiOutputType", midiOutputTypeCombo);
    externalMidiPitchModeAttach = std::make_unique<CA>(proc.apvts, "externalMidiPitchMode", externalMidiPitchModeCombo);
    normalMidiChannelAttach = std::make_unique<CA>(proc.apvts, "normalMidiChannel", normalMidiChannelCombo);
    mpeBendRangeAttach = std::make_unique<CA>(proc.apvts, "mpePitchBendRange", mpeBendRangeCombo);
    mpePitchModeAttach = std::make_unique<CA>(proc.apvts, "mpePitchMode", mpePitchModeCombo);
    mpeSetupAttach = std::make_unique<BA>(proc.apvts, "mpeSendSetupMessages", mpeSetupToggle);
    freezeAttach      = std::make_unique<BA>(proc.apvts, "freeze", freezeToggle);

    // network
    portEditor.setText(juce::String(proc.udpPort));
    portEditor.setInputRestrictions(5, "0123456789");
    portEditor.setColour(juce::TextEditor::backgroundColourId, cs::bg);
    portEditor.setColour(juce::TextEditor::textColourId,       cs::text);
    portEditor.setColour(juce::TextEditor::outlineColourId,    cs::hairline);
    portEditor.setColour(juce::TextEditor::focusedOutlineColourId, cs::accBlue);
    portEditor.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain)));
    addAndMakeVisible(portEditor);

    portApplyBtn.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff1f2d44));
    portApplyBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2a3a55));
    portApplyBtn.setColour(juce::TextButton::textColourOffId,  cs::accBlue);
    portApplyBtn.onClick = [this]()
    {
        const int port = portEditor.getText().getIntValue();
        if (port > 0 && port < 65536) proc.setUdpPort(port);
    };
    addAndMakeVisible(portApplyBtn);

    // simulator
    auto styleSimButton = [] (juce::TextButton& b, juce::Colour accent = cs::text2)
    {
        b.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff20232f));
        b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff292c3a));
        b.setColour(juce::TextButton::textColourOffId,  accent);
    };
    styleSimButton(simAddBtn);
    styleSimButton(simCrowdBtn, cs::accAmber);
    styleSimButton(simRemoveBtn);
    styleSimButton(simClearBtn, cs::accRed);

    simMoveBtn.setColour(juce::ToggleButton::textColourId,         cs::text2);
    simMoveBtn.setColour(juce::ToggleButton::tickColourId,         cs::accGreen);
    simMoveBtn.setColour(juce::ToggleButton::tickDisabledColourId, cs::hairline);

    simAddBtn   .onClick = [this]() { proc.simulator.addRandomSeat(); };
    simCrowdBtn .onClick = [this]() { proc.simulator.addRandomSeats(25); };
    simRemoveBtn.onClick = [this]() { proc.simulator.removeRandomSeat(); };
    simMoveBtn  .onClick = [this]() { proc.simulator.setRandomMovement(simMoveBtn.getToggleState()); };
    simClearBtn .onClick = [this]()
    {
        proc.panic();
        simMoveBtn.setToggleState(false, juce::dontSendNotification);
    };
    addAndMakeVisible(simAddBtn);
    addAndMakeVisible(simCrowdBtn);
    addAndMakeVisible(simRemoveBtn);
    addAndMakeVisible(simMoveBtn);
    addAndMakeVisible(simClearBtn);

    energySlider     .setTooltip("Macro: raises level, saturation, density, and trigger responsiveness where applicable.");
    motionMacroSlider.setTooltip("Macro: adds organic movement; in Granular mode it increases grain spread.");
    toneMacroSlider  .setTooltip("Macro: opens the voice filter and reverb brightness.");
    spaceMacroSlider .setTooltip("Macro: expands reverb and cross-delay.");
    engineSourceCombo.setTooltip("Switches between sample-library playback and the element partial oscillator synth.");
    samplePlaybackCombo.setTooltip("For Sample Library: choose direct sampler playback or the granular engine.");
    soundModeCombo   .setTooltip("Changes the engine personality: cloud, glass, sub, rain, or frozen hall.");
    spectralElementCombo.setTooltip("Element fingerprint used by Element Spectral Synth. Longest wavelength maps to the played root.");
    atomicScaleModeCombo.setTooltip("Builds a playable atomic scale by clustering spectral lines. Raw timbre partials are still preserved.");
    spectralPartialSlider.setTooltip("Selects the raw spectral line to audition when Partial Solo is enabled.");
    partialSoloToggle.setTooltip("Auditions one raw spectral partial at a time. Off preserves the full atomic timbre.");
    spectralStretchSlider.setTooltip("Stretches or compresses the element ratios around the root wavelength.");
    wetDrySlider     .setTooltip("Balances dry voices against the reverb and cross-delay return.");
    tapeDriveSlider  .setTooltip("Adds soft tape-style saturation before the safety limiter.");
    grainSizeSlider  .setTooltip("Base grain length before the selected sound mode reshapes it.");
    grainDensitySlider.setTooltip("Number and overlap of simultaneous grains per active voice.");
    pitchSpreadSlider.setTooltip("Scale-quantized pitch drift for spawned grains.");
    positionJitterSlider.setTooltip("How far new grains wander around the voice playhead.");
    stereoSpreadSlider.setTooltip("Random per-grain stereo placement before the seat pan.");
    reverseToggle    .setTooltip("Lets some grains play backwards for shimmer and smear.");
    grainShapeCombo  .setTooltip("Envelope shape used by each grain.");
    freezeToggle     .setTooltip("Holds active grain clouds and freezes the reverb tail.");
    rootCombo        .setTooltip("Root note for the tonal audience scale maps.");
    rootOctaveCombo  .setTooltip("Root octave. Element spectra preserve their spectral intervals while moving the scale to this root.");
    scaleCombo       .setTooltip("Scale used to quantize audience X movement.");
    octavesSlider    .setTooltip("Number of octaves covered by the X axis.");
    polyphonyCombo   .setTooltip("Voice budget: Normal 256, High 512, Ultra 1024. Unison folds down automatically as the crowd grows.");
    audioMidiOutputModeCombo.setTooltip("Choose whether this plugin renders internal audio, MIDI only, or both.");
    midiOutputTypeCombo.setTooltip("Normal MIDI sends nearest notes; MPE MIDI preserves spectral cents with per-note pitch bend.");
    externalMidiPitchModeCombo.setTooltip("Direct plays normal MIDI notes. Scale quantizes to the selected scale. Trigger maps keys to the visible spectral scale keyboard.");
    midiOutputDeviceCombo.setTooltip("Choose where generated MIDI is sent: the DAW host bus, a virtual MIDI port, or a physical MIDI device.");
    midiOutputRefreshBtn.setTooltip("Rescan system MIDI output devices.");
    normalMidiChannelCombo.setTooltip("Single channel used by Normal MIDI output.");
    mpeBendRangeCombo.setTooltip("Pitch-bend range for MPE member channels. The receiving synth must match this value.");
    mpePitchModeCombo.setTooltip("Retrigger sends a new MPE note for changed degrees; Glide updates pitch bend when possible.");
    mpeSetupToggle.setTooltip("Sends MPE zone and pitch-bend-range setup RPN messages when MPE is enabled.");
    panicBtn         .setTooltip("Immediately clears simulated/live seats and stops voices, delay, and reverb.");
    simClearBtn      .setTooltip("Clears all simulated/live seats and immediately stops voices, delay, and reverb.");
    muteToggle       .setTooltip("Mutes plugin output without changing current seats.");
    performanceToggle.setTooltip("Large stage-readable overlay for live use.");
    debugToggle      .setTooltip("Show raw OSC seat and scale diagnostics.");
    audioSettingsBtn .setTooltip("Open Android audio device settings for sample rate, buffer size, and current bit depth.");

   #if JUCE_ANDROID
    startTimerHz(10);
   #else
    startTimerHz(15);
   #endif
    refreshMidiOutputCombo();
    updatePerformanceVisibility();
    updateOutputModeVisibility();
}

AudienceEditor::~AudienceEditor() { stopTimer(); }

void AudienceEditor::updatePerformanceVisibility()
{
    if (performanceToggle.getToggleState())
        debugPanel.setVisible(false);
    else
        debugPanel.setVisible(debugToggle.getToggleState());
}

void AudienceEditor::refreshMidiOutputCombo()
{
    const auto options = proc.getMidiOutputOptions();
    const bool changed = options != lastMidiOutputOptions;

    if (changed)
    {
        lastMidiOutputOptions = options;
        midiOutputDeviceCombo.clear(juce::dontSendNotification);

        for (int i = 0; i < options.size(); ++i)
            midiOutputDeviceCombo.addItem(options[i], i + 1);
    }

    int selected = proc.getMidiOutputOptionIndex();
    if (selected < 0 || selected >= options.size())
    {
        selected = 0;
        proc.setMidiOutputOptionIndex(selected);
    }

    if (options.size() > 0
        && midiOutputDeviceCombo.getSelectedItemIndex() != selected)
        midiOutputDeviceCombo.setSelectedItemIndex(selected, juce::dontSendNotification);

    midiOutputStatusLabel.setText(proc.getMidiOutputDescription(), juce::dontSendNotification);
}

void AudienceEditor::updateOutputModeVisibility()
{
    auto audioVisible = [] (juce::Component& c) { c.setVisible(true); };
    auto setActive = [] (juce::Component& c, bool active)
    {
        c.setEnabled(active);
        c.setAlpha(active ? 1.0f : 0.36f);
    };

    audioVisible(pitchSlider); audioVisible(layerMixSlider); audioVisible(wetDrySlider); audioVisible(reverbSlider);
    audioVisible(delaySlider); audioVisible(tapeDriveSlider); audioVisible(masterSlider);
    audioVisible(pitchLabel); audioVisible(layerMixLabel); audioVisible(wetDryLabel); audioVisible(reverbLabel);
    audioVisible(delayLabel); audioVisible(tapeDriveLabel); audioVisible(masterLabel);
    audioVisible(attackSlider); audioVisible(releaseSlider); audioVisible(brightnessSlider); audioVisible(movementSlider);
    audioVisible(grainSizeSlider); audioVisible(grainDensitySlider); audioVisible(pitchSpreadSlider);
    audioVisible(positionJitterSlider); audioVisible(stereoSpreadSlider);
    audioVisible(attackLabel); audioVisible(releaseLabel); audioVisible(brightnessLabel); audioVisible(movementLabel);
    audioVisible(grainSizeLabel); audioVisible(grainDensityLabel); audioVisible(pitchSpreadLabel);
    audioVisible(positionJitterLabel); audioVisible(stereoSpreadLabel);
    audioVisible(reverseToggle); audioVisible(grainShapeCombo); audioVisible(grainShapeLabel);
    audioVisible(soundModeCombo); audioVisible(soundModeLabel); audioVisible(polyphonyCombo);
    audioVisible(polyphonyLabel); audioVisible(freezeToggle);
    audioVisible(engineSourceCombo); audioVisible(engineSourceLabel);
    audioVisible(samplePlaybackCombo); audioVisible(samplePlaybackLabel);
    audioVisible(spectralElementCombo); audioVisible(spectralElementLabel);
    audioVisible(spectralPartialSlider); audioVisible(spectralPartialLabel); audioVisible(partialSoloToggle);
    audioVisible(atomicScaleModeCombo); audioVisible(atomicScaleModeLabel);
    audioVisible(spectralStretchSlider); audioVisible(spectralStretchLabel);
    audioVisible(audioMidiOutputModeCombo); audioVisible(audioMidiOutputModeLabel);
    audioVisible(midiOutputTypeCombo); audioVisible(midiOutputTypeLabel);
    audioVisible(externalMidiPitchModeCombo); audioVisible(externalMidiPitchModeLabel);
    audioVisible(normalMidiChannelCombo); audioVisible(normalMidiChannelLabel);
    audioVisible(mpeBendRangeCombo); audioVisible(mpeBendRangeLabel);
    audioVisible(mpePitchModeCombo); audioVisible(mpePitchModeLabel);
    audioVisible(mpeSetupToggle);
    audioVisible(midiOutputDeviceCombo); audioVisible(midiOutputDeviceLabel);
    audioVisible(midiOutputStatusLabel); audioVisible(midiOutputRefreshBtn);
    audioVisible(midiActivityLabel);

   #if JUCE_ANDROID
    audioSettingsBtn.setVisible(true);
    audioDeviceStatusLabel.setVisible(true);
   #else
    audioSettingsBtn.setVisible(false);
    audioDeviceStatusLabel.setVisible(false);
   #endif

    const bool elementSynth = proc.engine.engineSource.load(std::memory_order_relaxed) == 1;
    const bool sampleLibrary = ! elementSynth;
    const bool granular = sampleLibrary
        && proc.engine.samplePlaybackMode.load(std::memory_order_relaxed) == 1;

    scaleCombo.setVisible(! elementSynth);
    scaleLabel.setVisible(! elementSynth);
    samplePlaybackCombo.setVisible(sampleLibrary);
    samplePlaybackLabel.setVisible(sampleLibrary);
    spectralElementCombo.setVisible(elementSynth);
    spectralElementLabel.setVisible(elementSynth);
    partialSoloToggle.setVisible(elementSynth);
    spectralPartialSlider.setVisible(elementSynth);
    spectralPartialLabel.setVisible(elementSynth);
    spectralStretchSlider.setVisible(elementSynth);
    spectralStretchLabel.setVisible(elementSynth);

    setActive(grainSizeSlider, granular);
    setActive(grainSizeLabel, granular);
    setActive(grainDensitySlider, granular);
    setActive(grainDensityLabel, granular);
    setActive(pitchSpreadSlider, granular);
    setActive(pitchSpreadLabel, granular);
    setActive(positionJitterSlider, granular);
    setActive(positionJitterLabel, granular);
    setActive(stereoSpreadSlider, granular);
    setActive(stereoSpreadLabel, granular);
    setActive(reverseToggle, granular);
    setActive(grainShapeCombo, granular);
    setActive(grainShapeLabel, granular);

    const int midiType = (int) proc.apvts.getRawParameterValue("midiOutputType")->load();
    const int outputMode = (int) proc.apvts.getRawParameterValue("audioMidiOutputMode")->load();
    const bool normalMidi = midiType == 1;
    const bool mpeMidi = midiType == 2;
    const bool midiActive = outputMode != 0 && midiType != 0;
    setActive(normalMidiChannelCombo, normalMidi);
    setActive(normalMidiChannelLabel, normalMidi);
    setActive(mpeBendRangeCombo, mpeMidi);
    setActive(mpeBendRangeLabel, mpeMidi);
    setActive(mpePitchModeCombo, mpeMidi);
    setActive(mpePitchModeLabel, mpeMidi);
    setActive(mpeSetupToggle, mpeMidi);
    midiOutputDeviceCombo.setEnabled(true);
    midiOutputDeviceCombo.setAlpha(1.0f);
    midiOutputDeviceLabel.setEnabled(true);
    midiOutputDeviceLabel.setAlpha(1.0f);
    midiOutputRefreshBtn.setEnabled(true);
    midiOutputRefreshBtn.setAlpha(1.0f);
    setActive(midiOutputStatusLabel, midiActive);
    setActive(midiActivityLabel, midiActive);

    libraryRail.setAlpha(1.0f);
}

void AudienceEditor::showAudioSettings()
{
   #if JUCE_ANDROID
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
    {
        auto content = std::make_unique<juce::AudioDeviceSelectorComponent> (
            holder->deviceManager,
            0, 0,
            0, 2,
            false, false,
            true, false);
        content->setSize(600, 330);

        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned(content.release());
        options.dialogTitle = "Audio Settings";
        options.dialogBackgroundColour = cs::panel2;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = false;
        options.resizable = false;
        options.launchAsync();
    }
   #endif
}

juce::String AudienceEditor::getAudioDeviceStatusText() const
{
   #if JUCE_ANDROID
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
    {
        if (auto* device = holder->deviceManager.getCurrentAudioDevice())
        {
            const double sr = device->getCurrentSampleRate();
            const int buffer = device->getCurrentBufferSizeSamples();
            const int bitDepth = device->getCurrentBitDepth();

            const juce::String rate = sr > 0.0
                ? (juce::String(sr / 1000.0, 1) + " kHz")
                : juce::String("rate ?");
            const juce::String depth = bitDepth > 0
                ? (juce::String(bitDepth) + " bit")
                : juce::String("bit ?");
            const juce::String size = buffer > 0
                ? (juce::String(buffer) + " spl")
                : juce::String("buffer ?");

            return rate + " / " + depth + " / " + size;
        }
    }

    return "Audio device unavailable";
   #else
    return {};
   #endif
}

// ---------- paint helpers ----------

void AudienceEditor::paintBrandMark (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float cx = r.getCentreX();
    const float cy = r.getCentreY();
    const float R  = std::min(r.getWidth(), r.getHeight()) * 0.5f - 1.0f;

    juce::ColourGradient ring(cs::accViolet, cx - R, cy - R,
                              cs::accAmber,  cx + R, cy + R, false);
    ring.addColour(0.5, cs::accGreen);
    g.setGradientFill(ring);
    g.drawEllipse(cx - R, cy - R, R * 2.0f, R * 2.0f, 1.5f);

    juce::ColourGradient disc(cs::accViolet, cx - R * 0.4f, cy - R * 0.4f,
                              cs::accAmber,  cx + R * 0.4f, cy + R * 0.4f, false);
    disc.addColour(0.5, cs::accGreen);
    g.setGradientFill(disc);
    g.fillEllipse(cx - R * 0.5f, cy - R * 0.5f, R, R);

    g.setColour(cs::bg.brighter(0.05f));
    const float dotR = R * 0.18f;
    g.fillEllipse(cx - dotR, cy - dotR, dotR * 2.0f, dotR * 2.0f);
}

void AudienceEditor::paintWindowDots (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    const float r = 5.5f;
    const float gap = 6.0f;
    float cx = (float) bounds.getRight() - r;
    const float cy = (float) bounds.getCentreY();

    const juce::Colour cols[3] = { juce::Colour(0xffe25d4f),
                                    juce::Colour(0xffe7c14b),
                                    juce::Colour(0xff64c95c) };
    for (int i = 0; i < 3; ++i)
    {
        g.setColour(cols[i]);
        g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
        cx -= r * 2.0f + gap;
    }
}

void AudienceEditor::paintLivePill (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    const auto rect = bounds.toFloat();
    const bool listening = proc.osc.isRunning();
    g.setColour(juce::Colour(0xcc0a0d18));
    g.fillRoundedRectangle(rect, 14.0f);
    g.setColour(cs::hairline);
    g.drawRoundedRectangle(rect, 14.0f, 1.0f);

    const float dotR = 3.0f;
    const float dotX = rect.getX() + 12.0f;
    const float dotY = rect.getCentreY();
    const auto statusColour = listening ? cs::accGreen : cs::accRed;
    g.setColour(statusColour.withAlpha(0.4f));
    g.fillEllipse(dotX - dotR * 2.0f, dotY - dotR * 2.0f, dotR * 4.0f, dotR * 4.0f);
    g.setColour(statusColour);
    g.fillEllipse(dotX - dotR, dotY - dotR, dotR * 2.0f, dotR * 2.0f);

    g.setColour(cs::text);
    g.setFont(juce::Font(juce::FontOptions(12.5f)));
    g.drawText(listening ? "Live" : "Offline", (int) (dotX + 8.0f), bounds.getY(), 68, bounds.getHeight(),
               juce::Justification::centredLeft);

    const float metaX = dotX + 70.0f;
    g.setColour(cs::text3);
    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain)));

    auto seats    = proc.engine.getRegisteredSeatCount();
    auto voices   = proc.engine.getActiveVoiceCount();
    auto dominant = proc.engine.getDominantSampleName();
    auto libN     = proc.engine.getLibrary().numSamples();

    const juce::String meta = "SEATS  " + juce::String(seats)
                             + "   /   VOICES  " + juce::String(voices)
                             + "   /   LIBRARY  " + juce::String(libN)
                             + "   /   PLAYBACK  " + proc.engine.getSamplePlaybackModeName()
                             + "   /   " + dominant;
    g.drawText(meta, (int) metaX, bounds.getY(), bounds.getRight() - (int) metaX - 8,
               bounds.getHeight(), juce::Justification::centredLeft);
}

void AudienceEditor::paintModulePanel (juce::Graphics& g, juce::Rectangle<int> r)
{
    juce::ColourGradient bg(cs::panel.withAlpha(0.7f),  r.toFloat().getCentre().translated(0.0f, -100.0f),
                            cs::panel2.withAlpha(0.7f), r.toFloat().getCentre().translated(0.0f, (float) r.getHeight() * 0.6f),
                            false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(r.toFloat(), 8.0f);
    g.setColour(cs::hairline);
    g.drawRoundedRectangle(r.toFloat(), 8.0f, 1.0f);

    auto drawTab = [&] (juce::Rectangle<int> tb, const juce::String& label, bool active)
    {
        if (active)
        {
            g.setColour(cs::accBlue.withAlpha(0.16f));
            g.fillRoundedRectangle(tb.toFloat(), 5.0f);
            g.setColour(cs::accBlue);
            g.fillRect((float) tb.getX() + 6.0f, (float) tb.getBottom() - 2.0f, (float) tb.getWidth() - 12.0f, 2.0f);
        }
        g.setColour(active ? cs::text : cs::text3);
        g.setFont(juce::Font(juce::FontOptions(13.0f, active ? juce::Font::bold : juce::Font::plain)));
        g.drawText(label, tb, juce::Justification::centred);
    };
    drawTab(textureTabBounds, "TEXTURE", moduleTab == 0);
    drawTab(voicesTabBounds,  "VOICES",  moduleTab == 1);

    const bool elementSynth = proc.engine.engineSource.load(std::memory_order_relaxed) == 1;
    const juce::String subtitle = moduleTab == 0
        ? (elementSynth ? (proc.engine.getSpectralElementName() + " root "
                           + juce::String(proc.engine.getSpectralElementRootWavelengthNm(), 3) + " nm")
                        : (proc.librariesStatus.isEmpty() ? juce::String("global mix / pitch / space")
                                                          : proc.librariesStatus))
        : juce::String("element partial oscillator bank");
    g.setColour(cs::text3);
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText(subtitle, r.getRight() - 332, r.getY() + 11, 320, 18, juce::Justification::centredRight);
}

void AudienceEditor::paintBedPanel (juce::Graphics& g, juce::Rectangle<int> r)
{
    juce::ColourGradient bg(cs::panel.withAlpha(0.7f),  r.toFloat().getCentre().translated(0.0f, -100.0f),
                            cs::panel2.withAlpha(0.7f), r.toFloat().getCentre().translated(0.0f, (float) r.getHeight() * 0.6f),
                            false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(r.toFloat(), 8.0f);
    g.setColour(cs::hairline);
    g.drawRoundedRectangle(r.toFloat(), 8.0f, 1.0f);

    auto headerArea = r.reduced(12, 8).removeFromTop(30);
    g.setColour(cs::text);
    g.setFont(juce::Font(juce::FontOptions(15.5f, juce::Font::bold)));
    g.drawText("TEXTURE", headerArea.removeFromLeft(132), juce::Justification::centredLeft);

    g.setColour(cs::text3);
    g.setFont(juce::Font(juce::FontOptions(11.5f)));
    const bool elementSynth = proc.engine.engineSource.load(std::memory_order_relaxed) == 1;
    juce::String subtitle = elementSynth
        ? (proc.engine.getSpectralElementName() + " root "
           + juce::String(proc.engine.getSpectralElementRootWavelengthNm(), 3) + " nm")
        : (proc.librariesStatus.isEmpty()
            ? juce::String("global mix / pitch / space")
            : proc.librariesStatus);
    g.drawText(subtitle, headerArea.removeFromLeft(420), juce::Justification::centredLeft);

    auto pill = r.reduced(12, 8).removeFromTop(30).removeFromRight(84);
    g.setColour(cs::panel.withAlpha(0.6f));
    g.fillRoundedRectangle(pill.toFloat(), 999.0f);
    g.setColour(cs::hairline);
    g.drawRoundedRectangle(pill.toFloat(), 999.0f, 1.0f);
    g.setColour(cs::accBlue);
    g.fillEllipse((float) pill.getX() + 10.0f, (float) pill.getCentreY() - 3.0f, 6.0f, 6.0f);
    g.setColour(cs::text2);
    g.setFont(juce::Font(juce::FontOptions(11.5f)));
    g.drawText("global", pill.withTrimmedLeft(22), juce::Justification::centredLeft);

    const int divX = r.getRight() - 12 - 70 - 12;
    g.setColour(cs::hairline);
    g.drawLine((float) divX, (float) r.getY() + 60.0f,
               (float) divX, (float) r.getBottom() - 18.0f, 1.0f);
}

void AudienceEditor::paintParticlePanel (juce::Graphics& g, juce::Rectangle<int> r)
{
    juce::ColourGradient bg(cs::panel.withAlpha(0.7f),  r.toFloat().getCentre().translated(0.0f, -100.0f),
                            cs::panel2.withAlpha(0.7f), r.toFloat().getCentre().translated(0.0f, (float) r.getHeight() * 0.6f),
                            false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(r.toFloat(), 8.0f);
    g.setColour(cs::hairline);
    g.drawRoundedRectangle(r.toFloat(), 8.0f, 1.0f);

    auto headerArea = r.reduced(12, 8).removeFromTop(30);
    g.setColour(cs::text);
    g.setFont(juce::Font(juce::FontOptions(15.5f, juce::Font::bold)));
    g.drawText("VOICES", headerArea.removeFromLeft(132), juce::Justification::centredLeft);

    g.setColour(cs::text3);
    g.setFont(juce::Font(juce::FontOptions(11.5f)));
    const bool elementSynth = proc.engine.engineSource.load(std::memory_order_relaxed) == 1;
    g.drawText(elementSynth
                    ? "element partial oscillator bank"
                    : ("sample library / " + proc.engine.getSamplePlaybackModeName()),
               headerArea.removeFromLeft(360),
               juce::Justification::centredLeft);

    auto pill = r.reduced(12, 8).removeFromTop(30).removeFromRight(94);
    g.setColour(cs::panel.withAlpha(0.6f));
    g.fillRoundedRectangle(pill.toFloat(), 999.0f);
    g.setColour(cs::hairline);
    g.drawRoundedRectangle(pill.toFloat(), 999.0f, 1.0f);
    g.setColour(cs::accGreen);
    g.fillEllipse((float) pill.getX() + 10.0f, (float) pill.getCentreY() - 3.0f, 6.0f, 6.0f);
    g.setColour(cs::text2);
    g.setFont(juce::Font(juce::FontOptions(11.5f)));
    g.drawText("emergent", pill.withTrimmedLeft(22), juce::Justification::centredLeft);
}

void AudienceEditor::paintMacroPanel (juce::Graphics& g, juce::Rectangle<int> r)
{
    juce::ColourGradient bg(cs::panel.withAlpha(0.72f),  r.toFloat().getCentre().translated(0.0f, -70.0f),
                            cs::panel2.withAlpha(0.72f), r.toFloat().getCentre().translated(0.0f, 70.0f),
                            false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(r.toFloat(), 8.0f);
    g.setColour(cs::hairline);
    g.drawRoundedRectangle(r.toFloat(), 8.0f, 1.0f);

    // (Old "LIVE MACROS" / "Performance" section title removed — it overlapped the
    //  ENGINE and SOUND MODE controls that resized() places in this top-left region.)
    g.setColour(cs::text3);
    g.setFont(juce::Font(juce::FontOptions(10.5f)));

    const int outputZoneW = 176;
    const int scaleZoneW = 250;
    const int outputX = r.getRight() - outputZoneW;
    const int scaleX = outputX - scaleZoneW - 16;
    const int macroDividerX = scaleX - 16;
    const int rangeX = r.getX() + 166;
    const int rangeW = juce::jmax(120, macroDividerX - rangeX - 12);
    g.drawText(proc.engine.getScaleRangeName(),
               rangeX, r.getBottom() - 26, rangeW, 14,
               juce::Justification::left);

    g.setColour(cs::hairline);
    g.drawLine((float) macroDividerX, (float) r.getY() + 12.0f,
               (float) macroDividerX, (float) r.getBottom() - 12.0f, 1.0f);
    g.drawLine((float) (outputX - 8), (float) r.getY() + 12.0f,
               (float) (outputX - 8), (float) r.getBottom() - 12.0f, 1.0f);

    if (! midiOutputPanelBounds.isEmpty())
    {
        auto panel = midiOutputPanelBounds.toFloat();
        g.setColour(cs::bg.withAlpha(0.46f));
        g.fillRoundedRectangle(panel, 8.0f);
        g.setColour(cs::hairline);
        g.drawRoundedRectangle(panel, 8.0f, 1.0f);
    }

    const bool muted = proc.isMuted();
    g.setColour(muted ? cs::accRed : cs::accGreen);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).boldened());
    g.drawText(muted ? "OUTPUT MUTED" : "OUTPUT ARMED",
               outputX + 8, r.getY() + 12, outputZoneW - 20, 16,
               juce::Justification::centredRight);

    g.setColour(cs::text4);
    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::plain)));
    const juce::String poly = "LIMIT " + juce::String(proc.engine.getVoiceLimit())
                            + "   U" + juce::String(proc.engine.getAdaptiveUnisonCount());
    g.drawText(poly, outputX + 8, r.getBottom() - 24, outputZoneW - 20, 14,
               juce::Justification::centredRight);
}

void AudienceEditor::paintScaleKeyboard (juce::Graphics& g, juce::Rectangle<int> r)
{
    juce::ColourGradient bg(cs::panel.withAlpha(0.72f),  r.toFloat().getCentre().translated(0.0f, -30.0f),
                            cs::panel2.withAlpha(0.72f), r.toFloat().getCentre().translated(0.0f, 36.0f),
                            false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(r.toFloat(), 8.0f);
    g.setColour(cs::hairline);
    g.drawRoundedRectangle(r.toFloat(), 8.0f, 1.0f);

    auto inner = r.reduced(12, 8);
    auto header = inner.removeFromTop(18);
    const int total = proc.engine.getScaleTableSize();
    const int maxByWidth = juce::jmax(1, inner.getWidth() / 22);
    const int keys = juce::jlimit(1, total, maxByWidth);

    g.setColour(cs::text3);
    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain)));
    g.drawText("SCALE KEYBOARD", header.removeFromLeft(118), juce::Justification::centredLeft);

    g.setColour(cs::text2);
    g.setFont(juce::Font(juce::FontOptions(10.5f)));
    g.drawText(proc.engine.getScaleName(), header.removeFromLeft(180), juce::Justification::centredLeft);

    g.setColour(cs::text4);
    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain)));
    const juce::String rangeText = total <= (int) kComputerKeys.size()
        ? "computer keys cover all steps"
        : "computer keys cover first " + juce::String((int) kComputerKeys.size()) + " / " + juce::String(total);
    g.drawText(rangeText, header, juce::Justification::centredRight);

    auto keyArea = inner.reduced(0, 2);
    const float keyW = (float) keyArea.getWidth() / (float) keys;

    constexpr int kMaxVisualScaleSteps = 8192;
    std::array<float, kMaxVisualScaleSteps> stepEnergy {};
    for (int voice = 0; voice < proc.engine.getMaxVoices(); ++voice)
    {
        const float voiceAmp = proc.engine.getVoiceAmp(voice);
        const int step = proc.engine.getVoiceScaleStep(voice);
        if (voiceAmp > 0.015f && step >= 0 && step < keys && step < kMaxVisualScaleSteps)
            stepEnergy[(size_t) step] += voiceAmp;
    }

    for (int i = 0; i < keys; ++i)
    {
        auto key = juce::Rectangle<float>((float) keyArea.getX() + (float) i * keyW + 1.0f,
                                          (float) keyArea.getY(),
                                          juce::jmax(8.0f, keyW - 2.0f),
                                          (float) keyArea.getHeight());
        const bool computerHeld = i < (int) kComputerKeys.size() && keyboardSlotDown[(size_t) i];
        const bool mouseHeld = mouseKeyboardStep == i && keyboardSlotDown[(size_t) mouseKeyboardSlot];
        const float engineEnergy = i < kMaxVisualScaleSteps ? stepEnergy[(size_t) i] : 0.0f;
        const bool engineHeld = engineEnergy > 0.015f;
        const bool down = computerHeld || mouseHeld || engineHeld;
        const float engineNorm = juce::jlimit(0.0f, 1.0f, engineEnergy);
        const float amp = proc.engine.isSpectralScale()
            ? juce::jlimit(0.15f, 1.0f, proc.engine.getScaleLineAmplitude(i))
            : 0.72f;
        const auto accent = cs::accAmber.interpolatedWith(cs::accBlue,
                                                          total > 1 ? (float) i / (float) (total - 1) : 0.0f);

        g.setColour(down ? accent.withAlpha(0.82f + engineNorm * 0.16f)
                         : juce::Colour(0xff111625).interpolatedWith(accent, 0.08f + amp * 0.30f));
        g.fillRoundedRectangle(key, 5.0f);
        if (proc.engine.isSpectralScale())
        {
            const float barH = juce::jlimit(2.0f, key.getHeight() - 6.0f,
                                            2.0f + amp * (key.getHeight() * 0.32f));
            auto strength = key.reduced(3.0f, 3.0f).removeFromBottom(barH);
            g.setColour(accent.withAlpha(down ? 0.78f : 0.34f + amp * 0.46f));
            g.fillRoundedRectangle(strength, 3.0f);
        }
        if (engineHeld)
        {
            const float glowAlpha = 0.22f + engineNorm * 0.34f;
            g.setColour(juce::Colours::white.withAlpha(glowAlpha));
            g.drawRoundedRectangle(key.reduced(1.0f), 4.0f, 1.4f + engineNorm * 1.0f);
        }
        g.setColour(down ? accent.brighter(0.28f) : cs::hairline);
        g.drawRoundedRectangle(key, 5.0f, 1.0f);

        g.setColour(down ? cs::bg : cs::text);
        g.setFont(juce::Font(juce::FontOptions(10.0f)).boldened());
        g.drawText(keyboardStepLabel(i), key.toNearestInt().withTrimmedBottom(16),
                   juce::Justification::centred);

        g.setColour(down ? cs::bg.withAlpha(0.68f) : cs::text4);
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 8.5f, juce::Font::plain)));
        const juce::String keyName = i < (int) kComputerKeys.size() ? keyboardKeyName(i) : "-";
        g.drawText(keyName, key.toNearestInt().withTrimmedTop((int) key.getHeight() - 15),
                   juce::Justification::centred);
    }
}

void AudienceEditor::paintRibbon (juce::Graphics& g, juce::Rectangle<int> r)
{
    juce::ColourGradient bg(cs::panel.withAlpha(0.7f),  r.toFloat().getCentre().translated(0.0f, -50.0f),
                            cs::panel2.withAlpha(0.7f), r.toFloat().getCentre().translated(0.0f, 50.0f),
                            false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(r.toFloat(), 8.0f);
    g.setColour(cs::hairline);
    g.drawRoundedRectangle(r.toFloat(), 8.0f, 1.0f);

    g.setColour(cs::text4);
    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain)));
    g.drawText("NETWORK",   r.getX() + 22,  r.getY() + 5, 100, 12, juce::Justification::left);
    g.drawText("SIMULATOR", r.getX() + 222, r.getY() + 5, 120, 12, juce::Justification::left);

    g.setColour(cs::hairline);
    g.drawLine((float) (r.getX() + 210), (float) r.getY() + 8.0f,
               (float) (r.getX() + 210), (float) r.getBottom() - 8.0f, 1.0f);
    g.drawLine((float) (r.getRight() - 250), (float) r.getY() + 8.0f,
               (float) (r.getRight() - 250), (float) r.getBottom() - 8.0f, 1.0f);

    const bool listening = proc.osc.isRunning();
    const int statusX = r.getX() + 90;
    const int statusY = r.getY() + 11;
    g.setColour(listening ? cs::accGreen : cs::text4);
    g.fillEllipse((float) statusX, (float) statusY - 3.0f, 6.0f, 6.0f);

    const int counterReserve = 260;
    auto counterArea = juce::Rectangle<int>(r.getRight() - counterReserve + 14,
                                            r.getY(),
                                            counterReserve - 24,
                                            r.getHeight());
    g.setColour(cs::text4);
    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::plain)));
    g.drawText("PARTICIPANTS",    counterArea.getX(),       counterArea.getY() + 14, 110, 12, juce::Justification::left);
    g.drawText("ACTIVE VOICES", counterArea.getX() + 118, counterArea.getY() + 14, 118, 12, juce::Justification::left);

    g.setColour(cs::text);
    g.setFont(juce::Font(juce::FontOptions(22.0f)));
    g.drawText(juce::String(proc.engine.getRegisteredSeatCount()),
               counterArea.getX(), counterArea.getY() + 28, 110, 26, juce::Justification::left);

    g.setColour(cs::accGreen);
    juce::String act = juce::String(proc.engine.getActiveVoiceCount()).paddedLeft('0', 4)
                     + "/" + juce::String(proc.engine.getVoiceLimit());
    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 18.0f, juce::Font::plain)));
    g.drawText(act, counterArea.getX() + 118, counterArea.getY() + 30, 124, 24, juce::Justification::left);
}

void AudienceEditor::paintPerformanceOverlay (juce::Graphics& g, juce::Rectangle<int> r)
{
    auto overlay = r.reduced(22, 24);
    g.setColour(juce::Colour(0xdd060914));
    g.fillRoundedRectangle(overlay.toFloat(), 18.0f);
    g.setColour(cs::accAmber.withAlpha(0.55f));
    g.drawRoundedRectangle(overlay.toFloat(), 18.0f, 1.2f);

    const auto seats  = proc.engine.getRegisteredSeatCount();
    const auto voices = proc.engine.getActiveVoiceCount();
    const auto lib    = proc.currentLibraryName.isEmpty() ? juce::String("(none)") : proc.currentLibraryName;

    g.setColour(cs::text3);
    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain)));
    g.drawText("PERFORMANCE MODE", overlay.getX() + 24, overlay.getY() + 22, 220, 18, juce::Justification::left);

    g.setColour(cs::text);
    g.setFont(juce::Font(juce::FontOptions(34.0f)).boldened());
    g.drawText(lib, overlay.getX() + 24, overlay.getY() + 44, overlay.getWidth() - 48, 44,
               juce::Justification::left);

    g.setColour(cs::text2);
    g.setFont(juce::Font(juce::FontOptions(15.0f)));
    g.drawText(proc.engine.getEngineSourceName() + "  /  "
                + proc.engine.getSignatureModeName() + "  /  "
                + proc.engine.getScaleName() + "  /  " + proc.engine.getScaleRangeName()
                + "  /  " + proc.engine.getPolyphonyModeName()
                + " U" + juce::String(proc.engine.getAdaptiveUnisonCount()),
               overlay.getX() + 26, overlay.getY() + 90, overlay.getWidth() - 52, 24,
               juce::Justification::left);

    auto map = overlay.reduced(24, 0);
    map.setY(overlay.getY() + 126);
    map.setHeight(juce::jmax(76, overlay.getBottom() - 118 - map.getY()));
    g.setColour(juce::Colour(0xaa111625));
    g.fillRoundedRectangle(map.toFloat(), 12.0f);
    g.setColour(cs::hairline);
    g.drawRoundedRectangle(map.toFloat(), 12.0f, 1.0f);
    g.setColour(cs::text3);
    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain)));
    g.drawText("AUDIENCE MAP  A-Z / 0-99", map.getX() + 14, map.getY() + 10, 220, 14,
               juce::Justification::left);

    auto grid = map.reduced(16, 30);
    const float cellW = (float) grid.getWidth()  / (float) PartialEngine::MAX_COLS;
    const float cellH = (float) grid.getHeight() / (float) PartialEngine::MAX_ROWS;
    g.setColour(juce::Colour(0x22343a4c));
    for (int row = 0; row < PartialEngine::MAX_ROWS; ++row)
    {
        const float y = (float) grid.getY() + (float) row * cellH;
        g.drawHorizontalLine((int) y, (float) grid.getX(), (float) grid.getRight());
    }
    for (int row = 0; row < PartialEngine::MAX_ROWS; ++row)
    {
        for (int col = 0; col < PartialEngine::MAX_COLS; ++col)
        {
            if (! proc.engine.isSeatActive(row, col))
                continue;

            const float x = (float) grid.getX() + ((float) col + 0.5f) * cellW;
            const float y = (float) grid.getY() + ((float) row + 0.5f) * cellH;
            const float amp = juce::jlimit(0.0f, 1.0f, proc.engine.getSeatY(row, col));
            const auto colr = cs::accGreen.interpolatedWith(cs::accAmber, proc.engine.getSeatX(row, col));
            const float rDot = 2.0f + amp * 3.0f;
            g.setColour(colr.withAlpha(0.28f));
            g.fillEllipse(x - rDot * 1.8f, y - rDot * 1.8f, rDot * 3.6f, rDot * 3.6f);
            g.setColour(colr);
            g.fillEllipse(x - rDot, y - rDot, rDot * 2.0f, rDot * 2.0f);
        }
    }

    const int boxW = (overlay.getWidth() - 72) / 3;
    const int y = overlay.getBottom() - 106;
    auto drawMetric = [&] (int x, const juce::String& label, const juce::String& value, juce::Colour accent)
    {
        juce::Rectangle<int> box(x, y, boxW, 78);
        g.setColour(juce::Colour(0xaa111625));
        g.fillRoundedRectangle(box.toFloat(), 10.0f);
        g.setColour(accent.withAlpha(0.6f));
        g.drawRoundedRectangle(box.toFloat(), 10.0f, 1.0f);
        g.setColour(cs::text3);
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain)));
        g.drawText(label, box.reduced(16, 10).removeFromTop(14), juce::Justification::left);
        g.setColour(accent);
        g.setFont(juce::Font(juce::FontOptions(30.0f)).boldened());
        g.drawText(value, box.reduced(16, 18), juce::Justification::centredLeft);
    };
    drawMetric(overlay.getX() + 24,               "ACTIVE SEATS",  juce::String(seats),  cs::accBlue);
    drawMetric(overlay.getX() + 24 + boxW + 12, "ACTIVE VOICES",
               juce::String(voices) + "/" + juce::String(proc.engine.getVoiceLimit()),
               cs::accGreen);
    drawMetric(overlay.getX() + 24 + (boxW + 12) * 2, "DOMINANT",
               proc.engine.getDominantSampleName(),
               cs::accAmber);

    if (proc.isMuted())
    {
        g.setColour(cs::accRed);
        g.setFont(juce::Font(juce::FontOptions(20.0f)).boldened());
        g.drawText("MUTED", overlay.getRight() - 130, overlay.getY() + 24, 100, 28,
                   juce::Justification::centredRight);
    }
}

// ---------- main paint / resized ----------

void AudienceEditor::paint (juce::Graphics& g)
{
    juce::ColourGradient bg(cs::bg2, (float) getWidth() * 0.2f, 0.0f,
                            cs::bg,  (float) getWidth() * 0.8f, (float) getHeight(),
                            false);
    g.setGradientFill(bg);
    g.fillAll();

    auto header = getLocalBounds().removeFromTop(40).reduced(10, 0);
    g.setColour(juce::Colour(0xaa070a0e));
    g.fillRect(0, 0, getWidth(), 40);
    g.setColour(cs::hairline);
    g.drawHorizontalLine(39, 0.0f, (float) getWidth());

    paintBrandMark(g, juce::Rectangle<float>((float) header.getX(),
                                              (float) header.getY() + 8.0f, 24.0f, 24.0f));
    g.setColour(cs::text);
    g.setFont(juce::Font(juce::FontOptions(11.5f)).boldened());
    g.drawText("SpektraSynth", header.getX() + 34, header.getY() + 5, 170, 14,
               juce::Justification::left);
    g.setColour(cs::text3);
    g.setFont(juce::Font(juce::FontOptions(9.0f)));
    g.drawText("Audience Spectral Engine", header.getX() + 34, header.getY() + 21, 170, 12,
               juce::Justification::left);

    auto drawPill = [&] (juce::Rectangle<int> r, const juce::String& label, const juce::String& value, juce::Colour accent, bool dot)
    {
        g.setColour(accent.withAlpha(0.08f));
        g.fillRoundedRectangle(r.toFloat(), 999.0f);
        g.setColour(accent.withAlpha(0.34f));
        g.drawRoundedRectangle(r.toFloat(), 999.0f, 1.0f);
        int x = r.getX() + 8;
        if (dot)
        {
            g.setColour(accent);
            g.fillEllipse((float) x, (float) r.getCentreY() - 3.0f, 6.0f, 6.0f);
            x += 12;
        }
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 8.5f, juce::Font::plain)));
        if (label.isNotEmpty())
        {
            g.setColour(cs::text3);
            g.drawText(label, x, r.getY(), 48, r.getHeight(), juce::Justification::centredLeft);
            x += 46;
        }
        g.setColour(cs::text);
        g.drawText(value, x, r.getY(), r.getRight() - x - 8, r.getHeight(), juce::Justification::centredLeft);
    };

    const bool listening = proc.osc.isRunning();
    int sx = header.getX() + 214;
    const int topControlsW = 104 + 76 + 64 + 56 + 8 * 3;
    const int statusRight = getWidth() - 10 - topControlsW - 12;
    auto tryPill = [&] (int width, const juce::String& label, const juce::String& value,
                        juce::Colour accent, bool dot)
    {
        if (sx + width > statusRight)
            return;

        drawPill({ sx, 8, width, 22 }, label, value, accent, dot);
        sx += width + 8;
    };

    tryPill(64,  "",       listening ? "LIVE" : "OFF", listening ? cs::accGreen : cs::accRed, true);
    tryPill(110, "MODE",   proc.engine.engineSource.load(std::memory_order_relaxed) == 1 ? "ELEMENT" : "SAMPLE",
            cs::hairline.brighter(0.5f), false);
    if (proc.engine.engineSource.load(std::memory_order_relaxed) == 0)
        tryPill(114, "PLAY", proc.engine.getSamplePlaybackModeName(), cs::hairline.brighter(0.5f), false);
    tryPill(88,  "SEATS",  juce::String(proc.engine.getRegisteredSeatCount()).paddedLeft('0', 3), cs::hairline.brighter(0.5f), false);
    tryPill(94,  "VOICES",
            juce::String(proc.engine.getActiveVoiceCount()).paddedLeft('0', 3),
            cs::hairline.brighter(0.5f), false);
    tryPill(94,  "LIB",    juce::String(proc.engine.getLibrary().numSamples()), cs::hairline.brighter(0.5f), false);
    tryPill(122, "DOM",    proc.engine.getDominantSampleName(), cs::hairline.brighter(0.5f), false);
    tryPill(104, "UDP",    proc.osc.isRunning() ? juce::String(proc.udpPort) : "busy", proc.osc.isRunning() ? cs::accGreen : cs::accRed, false);

    paintModulePanel(g, bedPanelBounds);
    paintMacroPanel(g, macroPanelBounds);
    paintScaleKeyboard(g, keyboardPanelBounds);
    paintRibbon(g, ribbonBounds);

}

void AudienceEditor::paintOverChildren (juce::Graphics& g)
{
    if (performanceToggle.getToggleState())
        paintPerformanceOverlay(g, aurora.getBounds());
}

void AudienceEditor::resized()
{
    const int pad       = 10;
    const int headerH   = 40;
    const int macroH    = 188;
    const int moduleH   = 210;
    const int keyboardH = 62;
    const int ribbonH   = 44;
    const int gap       = 8;
    const int railWidth = juce::jlimit(180, 220, getWidth() / 8);

    libraryRail.setBounds(pad, pad + headerH + gap,
                          railWidth,
                          getHeight() - (pad + headerH + gap) - pad);

    const int mainX = pad + railWidth + gap;
    const int mainW = getWidth() - mainX - pad;
    const int mainAvailableH = getHeight() - (pad + headerH + gap) - pad;
    const int auroraH = juce::jmax(40, mainAvailableH - macroH - moduleH - keyboardH - ribbonH - gap * 4);

    int y = pad + headerH + gap;
    aurora.setBounds(mainX, y, mainW, auroraH);
    debugPanel.setBounds(mainX, y, mainW, auroraH);
    debugPanel.toFront(false);

    y += auroraH + gap;
    keyboardPanelBounds = { mainX, y, mainW, keyboardH };
    y += keyboardH + gap;

    macroPanelBounds = { mainX, y, mainW, macroH };
    midiOutputPanelBounds = {};

    auto layoutMacro = [&] (juce::Slider& s, juce::Label& l, int x, int yy)
    {
        l.setBounds(x, yy, 64, 12);
        s.setBounds(x - 4, yy + 14, 72, 68);
    };

    const int macroY = macroPanelBounds.getY() + 15;
   #if JUCE_ANDROID
    audioSettingsBtn.setBounds(macroPanelBounds.getX() + 12, macroPanelBounds.getY() + 28, 122, 24);
    audioDeviceStatusLabel.setBounds(macroPanelBounds.getX() + 12, macroPanelBounds.getY() + 52, 142, 16);
    engineSourceLabel.setBounds(macroPanelBounds.getX() + 12, macroPanelBounds.getY() + 66, 104, 12);
    engineSourceCombo.setBounds(macroPanelBounds.getX() + 12, macroPanelBounds.getY() + 82, 132, 24);
    soundModeLabel.setBounds({});
    soundModeCombo.setBounds({});
   #else
    engineSourceLabel.setBounds(macroPanelBounds.getX() + 12, macroPanelBounds.getY() + 12, 104, 12);
    engineSourceCombo.setBounds(macroPanelBounds.getX() + 12, macroPanelBounds.getY() + 28, 132, 24);
    soundModeLabel.setBounds(macroPanelBounds.getX() + 12, macroPanelBounds.getY() + 60, 104, 12);
    soundModeCombo.setBounds(macroPanelBounds.getX() + 12, macroPanelBounds.getY() + 76, 132, 24);
    audioSettingsBtn.setBounds({});
    audioDeviceStatusLabel.setBounds({});
   #endif

    int mx = macroPanelBounds.getX() + 150;
    layoutMacro(energySlider,      energyLabel,      mx + 0 * 76, macroY);
    layoutMacro(motionMacroSlider, motionMacroLabel, mx + 1 * 76, macroY);
    layoutMacro(toneMacroSlider,   toneMacroLabel,   mx + 2 * 76, macroY);
    layoutMacro(spaceMacroSlider,  spaceMacroLabel,  mx + 3 * 76, macroY);

    const int outputZoneW = 176;
    const int scaleZoneW = 300;
    const int outputX = macroPanelBounds.getRight() - outputZoneW;
    const int scaleX = outputX - scaleZoneW - 16;
    rootLabel   .setBounds(scaleX,       macroPanelBounds.getY() + 18, 46, 12);
    rootCombo   .setBounds(scaleX,       macroPanelBounds.getY() + 34, 58, 24);
    rootOctaveLabel.setBounds(scaleX + 66,  macroPanelBounds.getY() + 18, 40, 12);
    rootOctaveCombo.setBounds(scaleX + 66,  macroPanelBounds.getY() + 34, 48, 24);
    scaleLabel  .setBounds(scaleX + 126, macroPanelBounds.getY() + 18, 130, 12);
    scaleCombo  .setBounds(scaleX + 126, macroPanelBounds.getY() + 34, 156, 24);
    octavesLabel.setBounds(scaleX,       macroPanelBounds.getY() + 66, 64, 12);
    octavesSlider.setBounds(scaleX + 64, macroPanelBounds.getY() + 61, 176, 26);
    polyphonyLabel.setBounds(outputX + 8, macroPanelBounds.getY() + 34, 72, 12);
    polyphonyCombo.setBounds(outputX + 8, macroPanelBounds.getY() + 50, 88, 24);
    freezeToggle.setBounds(outputX + 104, macroPanelBounds.getY() + 50, 68, 24);

    midiOutputPanelBounds = { macroPanelBounds.getX() + 10,
                              macroPanelBounds.getBottom() - 72,
                              juce::jmax(300, scaleX - macroPanelBounds.getX() - 26),
                              64 };

    auto midiInner = midiOutputPanelBounds.reduced(10, 7);
    const int labelY = midiInner.getY();
    const int controlY = midiInner.getY() + 17;
    const int statusY = midiInner.getY() + 43;
    constexpr int midiGap = 6;
    const int innerW = midiInner.getWidth();
    const bool compactMidi = innerW < 520;
    const bool showBend = innerW >= 470;
    const bool showRefresh = innerW >= 650;
    const bool showAdvancedMpe = innerW >= 760;
    const int modeW = compactMidi ? 94 : 104;
    const int typeW = compactMidi ? 92 : 98;
    const int inputModeW = compactMidi ? 76 : 88;
    const int channelW = compactMidi ? 42 : 46;
    const int bendW = showBend ? 62 : 0;
    const int pitchModeW = showAdvancedMpe ? 72 : 0;
    const int setupW = showAdvancedMpe ? 58 : 0;
    const int refreshW = showRefresh ? 54 : 0;
    const int fixedW = modeW + typeW + inputModeW + channelW + bendW + pitchModeW + setupW + refreshW
                     + midiGap * (4 + (showBend ? 1 : 0) + (showAdvancedMpe ? 2 : 0) + (showRefresh ? 1 : 0));
    const int deviceW = juce::jmax(compactMidi ? 92 : 160, innerW - fixedW);
    int midiX = midiInner.getX();

    audioMidiOutputModeLabel.setBounds(midiX, labelY, modeW, 12);
    audioMidiOutputModeCombo.setBounds(midiX, controlY, modeW, 24);
    midiX += modeW + midiGap;
    midiOutputTypeLabel.setBounds(midiX, labelY, typeW, 12);
    midiOutputTypeCombo.setBounds(midiX, controlY, typeW, 24);
    midiX += typeW + midiGap;
    externalMidiPitchModeLabel.setBounds(midiX, labelY, inputModeW, 12);
    externalMidiPitchModeCombo.setBounds(midiX, controlY, inputModeW, 24);
    midiX += inputModeW + midiGap;
    midiOutputDeviceLabel.setBounds(midiX, labelY, deviceW, 12);
    midiOutputDeviceCombo.setBounds(midiX, controlY, deviceW, 24);
    midiOutputStatusLabel.setBounds(midiX, statusY, juce::jmax(120, midiOutputPanelBounds.getRight() - midiX - 132), 12);
    midiX += deviceW + midiGap;
    normalMidiChannelLabel.setBounds(midiX, labelY, channelW, 12);
    normalMidiChannelCombo.setBounds(midiX, controlY, channelW, 24);
    midiX += channelW + midiGap;
    if (showBend)
    {
        mpeBendRangeLabel.setBounds(midiX, labelY, bendW, 12);
        mpeBendRangeCombo.setBounds(midiX, controlY, bendW, 24);
        midiX += bendW + midiGap;
    }
    else
    {
        mpeBendRangeLabel.setBounds({});
        mpeBendRangeCombo.setBounds({});
    }

    if (showAdvancedMpe)
    {
        mpePitchModeLabel.setBounds(midiX, labelY, pitchModeW, 12);
        mpePitchModeCombo.setBounds(midiX, controlY, pitchModeW, 24);
        midiX += pitchModeW + midiGap;
        mpeSetupToggle.setBounds(midiX, controlY, setupW, 24);
        midiX += setupW + midiGap;
    }
    else
    {
        mpePitchModeLabel.setBounds({});
        mpePitchModeCombo.setBounds({});
        mpeSetupToggle.setBounds({});
    }

    if (showRefresh)
    {
        midiOutputRefreshBtn.setBounds(midiX, controlY, refreshW, 24);
        midiX += refreshW + midiGap;
    }
    else
    {
        midiOutputRefreshBtn.setBounds({});
    }

    midiActivityLabel.setBounds(midiOutputPanelBounds.getRight() - 132, statusY, 122, 12);

    y += macroH + gap;
    // Bottom module is now tabbed: TEXTURE and VOICES share the full-width panel
    // and only the active tab's controls are laid out (the other is hidden).
    bedPanelBounds      = { mainX, y, mainW, moduleH };
    particlePanelBounds = bedPanelBounds;

    const int tabY = y + 7;
    textureTabBounds = { mainX + 12,       tabY, 104, 24 };
    voicesTabBounds  = { mainX + 12 + 110, tabY, 104, 24 };

    auto layoutKnob = [&] (juce::Slider& s, juce::Label& l, int x, int yy, int w)
    {
        l.setBounds(x, yy, w, 12);
        s.setBounds(x, yy + 14, w, 74);
    };
    auto hideCtl = [] (juce::Component& c) { c.setBounds({}); };

    const int contentY = y + 42;            // below the tab bar
    const int colsX    = mainX + 14;
    const int contentW = mainW - 28;

    if (moduleTab == 0)
    {
        // TEXTURE — 4 columns x 2 rows across the full width.
        const int step  = contentW / 4;
        const int knobW = juce::jlimit(60, 96, step - 8);
        layoutKnob(pitchSlider,     pitchLabel,     colsX + 0 * step, contentY, knobW);
        layoutKnob(layerMixSlider,  layerMixLabel,  colsX + 1 * step, contentY, knobW);
        layoutKnob(wetDrySlider,    wetDryLabel,    colsX + 2 * step, contentY, knobW);
        layoutKnob(reverbSlider,    reverbLabel,    colsX + 3 * step, contentY, knobW);
        layoutKnob(delaySlider,     delayLabel,     colsX + 0 * step, contentY + 88, knobW);
        layoutKnob(tapeDriveSlider, tapeDriveLabel, colsX + 1 * step, contentY + 88, knobW);
        layoutKnob(masterSlider,    masterLabel,    colsX + 2 * step, contentY + 88, knobW);
        layoutKnob(spectralStretchSlider, spectralStretchLabel, colsX + 3 * step, contentY + 88, knobW);

        juce::Component* const voiceCtls[] = { &attackSlider, &attackLabel,
             &releaseSlider, &releaseLabel, &brightnessSlider, &brightnessLabel, &movementSlider, &movementLabel,
             &grainSizeSlider, &grainSizeLabel, &grainDensitySlider, &grainDensityLabel,
             &pitchSpreadSlider, &pitchSpreadLabel, &positionJitterSlider, &positionJitterLabel,
             &stereoSpreadSlider, &stereoSpreadLabel, &grainShapeCombo, &grainShapeLabel, &reverseToggle,
             &samplePlaybackCombo, &samplePlaybackLabel, &spectralElementCombo, &spectralElementLabel,
             &partialSoloToggle, &atomicScaleModeCombo, &atomicScaleModeLabel,
             &spectralPartialSlider, &spectralPartialLabel };
        for (auto* c : voiceCtls) hideCtl(*c);
    }
    else
    {
        // VOICES — 6 columns x 2 rows across the full width.
        const int step  = contentW / 6;
        const int knobW = juce::jlimit(54, 84, step - 8);
        const int comboW = juce::jmin(118, step - 6);
        layoutKnob(attackSlider,       attackLabel,       colsX + 0 * step, contentY, knobW);
        layoutKnob(releaseSlider,      releaseLabel,      colsX + 1 * step, contentY, knobW);
        layoutKnob(brightnessSlider,   brightnessLabel,   colsX + 2 * step, contentY, knobW);
        layoutKnob(movementSlider,     movementLabel,     colsX + 3 * step, contentY, knobW);
        layoutKnob(grainSizeSlider,    grainSizeLabel,    colsX + 4 * step, contentY, knobW);
        layoutKnob(grainDensitySlider, grainDensityLabel, colsX + 5 * step, contentY, knobW);

        const int grainY = contentY + 88;
        layoutKnob(pitchSpreadSlider,    pitchSpreadLabel,    colsX + 0 * step, grainY, knobW);
        layoutKnob(positionJitterSlider, positionJitterLabel, colsX + 1 * step, grainY, knobW);
        layoutKnob(stereoSpreadSlider,   stereoSpreadLabel,   colsX + 2 * step, grainY, knobW);
        grainShapeLabel.setBounds(colsX + 3 * step, grainY, 90, 12);
        grainShapeCombo.setBounds(colsX + 3 * step, grainY + 18, comboW, 24);
        reverseToggle.setBounds(colsX + 3 * step, grainY + 48, 90, 22);
        samplePlaybackLabel.setBounds(colsX + 4 * step, grainY, 120, 12);
        samplePlaybackCombo.setBounds(colsX + 4 * step, grainY + 18, comboW, 24);
        spectralElementLabel.setBounds(colsX + 4 * step, grainY, 100, 12);
        spectralElementCombo.setBounds(colsX + 4 * step, grainY + 18, comboW, 24);
        partialSoloToggle.setBounds(colsX + 4 * step, grainY + 48, 90, 22);
        atomicScaleModeLabel.setBounds(colsX + 5 * step, grainY, 100, 12);
        atomicScaleModeCombo.setBounds(colsX + 5 * step, grainY + 18, comboW, 24);
        spectralPartialLabel.setBounds(colsX + 5 * step, grainY + 47, 92, 12);
        spectralPartialSlider.setBounds(colsX + 5 * step, grainY + 60, comboW, 22);

        juce::Component* const texCtls[] = { &pitchSlider, &pitchLabel,
             &layerMixSlider, &layerMixLabel, &wetDrySlider, &wetDryLabel, &reverbSlider, &reverbLabel,
             &delaySlider, &delayLabel, &tapeDriveSlider, &tapeDriveLabel, &masterSlider, &masterLabel,
             &spectralStretchSlider, &spectralStretchLabel };
        for (auto* c : texCtls) hideCtl(*c);
    }
    y += moduleH + gap;
    ribbonBounds = { mainX, y, mainW, ribbonH };

    const int ribbonControlY = ribbonBounds.getY() + 17;
    portEditor  .setBounds(ribbonBounds.getX() + 70,       ribbonControlY,  70, 22);
    portApplyBtn.setBounds(ribbonBounds.getX() + 148,      ribbonControlY,  54, 22);

    const int topControlY = 9;
    const int topGap = 8;
    const int panicW = 56;
    const int muteW = 64;
    const int debugW = 76;
    const int performanceW = 104;
    int topX = getWidth() - pad - panicW;

    panicBtn.setBounds(topX, topControlY - 1, panicW, 23);
    panicBtn.toFront(false);

    topX -= topGap + muteW;
    muteToggle.setBounds(topX, topControlY, muteW, 22);
    muteToggle.toFront(false);

    topX -= topGap + debugW;
    debugToggle.setBounds(topX, topControlY, debugW, 22);
    debugToggle.toFront(false);

    topX -= topGap + performanceW;
    performanceToggle.setBounds(topX, topControlY, performanceW, 22);
    performanceToggle.toFront(false);

    const int simX = ribbonBounds.getX() + 222;
    const int simY = ribbonBounds.getY() + 17;
    simAddBtn   .setBounds(simX,         simY,  54, 22);
    simCrowdBtn .setBounds(simX + 62,    simY,  72, 22);
    simRemoveBtn.setBounds(simX + 142,   simY,  72, 22);
    simMoveBtn  .setBounds(simX + 222,   simY, 110, 22);
    simClearBtn .setBounds(simX + 340,   simY,  62, 22);
    updateOutputModeVisibility();
}

juce::String AudienceEditor::keyboardKeyName (int slot) const
{
    if (slot < 0 || slot >= (int) kComputerKeys.size())
        return {};
    return juce::String::charToString(kComputerKeys[(size_t) slot]);
}

juce::String AudienceEditor::keyboardStepLabel (int scaleStep) const
{
    const int midi = proc.engine.getScaleMidi(scaleStep);
    if (midi < 0)
        return "-";

    return UiText::midiNoteName(midi);
}

int AudienceEditor::keyboardStepAt (juce::Point<int> p) const
{
    if (! keyboardPanelBounds.contains(p))
        return -1;

    auto inner = keyboardPanelBounds.reduced(12, 8);
    inner.removeFromTop(18);
    auto keyArea = inner.reduced(0, 2);
    if (! keyArea.contains(p))
        return -1;

    const int total = proc.engine.getScaleTableSize();
    if (total <= 0)
        return -1;

    const int keys = juce::jlimit(1, total, juce::jmax(1, keyArea.getWidth() / 22));
    const int step = (int) ((float) (p.x - keyArea.getX()) / (float) keyArea.getWidth() * (float) keys);
    return juce::jlimit(0, keys - 1, step);
}

void AudienceEditor::pressKeyboardStep (int slot, int scaleStep, float velocity)
{
    if (slot < 0 || slot >= PartialEngine::MAX_KEYBOARD_SLOTS || scaleStep < 0)
        return;

    if (keyboardSlotDown[(size_t) slot])
        releaseKeyboardSlot(slot);

    keyboardSlotDown[(size_t) slot] = true;
    proc.engine.setKeyboardStep(slot, scaleStep, velocity, true);
    repaint(keyboardPanelBounds);
}

void AudienceEditor::releaseKeyboardSlot (int slot)
{
    if (slot < 0 || slot >= PartialEngine::MAX_KEYBOARD_SLOTS)
        return;

    if (! keyboardSlotDown[(size_t) slot])
        return;

    keyboardSlotDown[(size_t) slot] = false;
    proc.engine.setKeyboardStep(slot, 0, 0.0f, false);
    repaint(keyboardPanelBounds);
}

void AudienceEditor::releaseAllKeyboardSlots()
{
    for (int slot = 0; slot < PartialEngine::MAX_KEYBOARD_SLOTS; ++slot)
        releaseKeyboardSlot(slot);
    proc.engine.releaseAllKeyboardNotes();
    mouseKeyboardStep = -1;
}

void AudienceEditor::updateComputerKeyboard()
{
    if (! hasKeyboardFocus(true))
        return;

    const int total = proc.engine.getScaleTableSize();
    const int steps = juce::jmin(total, (int) kComputerKeys.size());
    for (int slot = 0; slot < (int) kComputerKeys.size(); ++slot)
    {
        const bool down = slot < steps && computerKeyIsDown(kComputerKeys[(size_t) slot]);
        if (down && ! keyboardSlotDown[(size_t) slot])
            pressKeyboardStep(slot, slot, 1.0f);
        else if (! down && keyboardSlotDown[(size_t) slot])
            releaseKeyboardSlot(slot);
    }
}

bool AudienceEditor::keyStateChanged (bool)
{
    updateComputerKeyboard();
    return true;
}

void AudienceEditor::focusLost (FocusChangeType)
{
    releaseAllKeyboardSlots();
}

void AudienceEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    const auto pos = e.getPosition();
    if (textureTabBounds.contains(pos) || voicesTabBounds.contains(pos))
    {
        const int newTab = voicesTabBounds.contains(pos) ? 1 : 0;
        if (newTab != moduleTab)
        {
            moduleTab = newTab;
            lastVisibilityKey = -1;   // force visibility recompute for the newly shown tab
            resized();
            updateOutputModeVisibility();
            repaint();
        }
        return;
    }

    const int step = keyboardStepAt(e.getPosition());
    if (step >= 0)
    {
        mouseKeyboardStep = step;
        pressKeyboardStep(mouseKeyboardSlot, step, 1.0f);
    }
}

void AudienceEditor::mouseDrag (const juce::MouseEvent& e)
{
    const int step = keyboardStepAt(e.getPosition());
    if (step >= 0 && step != mouseKeyboardStep)
    {
        releaseKeyboardSlot(mouseKeyboardSlot);
        mouseKeyboardStep = step;
        pressKeyboardStep(mouseKeyboardSlot, step, 1.0f);
    }
}

void AudienceEditor::mouseUp (const juce::MouseEvent&)
{
    releaseKeyboardSlot(mouseKeyboardSlot);
    mouseKeyboardStep = -1;
}

void AudienceEditor::mouseExit (const juce::MouseEvent&)
{
    releaseKeyboardSlot(mouseKeyboardSlot);
    mouseKeyboardStep = -1;
}

void AudienceEditor::timerCallback()
{
    updateComputerKeyboard();
    muteToggle.setToggleState(proc.isMuted(), juce::dontSendNotification);
    const int midiType = (int) proc.apvts.getRawParameterValue("midiOutputType")->load();
    if (midiType == 2)
        midiActivityLabel.setText("MIDI " + juce::String(proc.getMidiNotesSent())
                                  + "  MPE " + juce::String(proc.getActiveMpeVoices())
                                  + "/" + juce::String(proc.getAvailableMpeChannels()),
                                  juce::dontSendNotification);
    else
        midiActivityLabel.setText("MIDI " + juce::String(proc.getMidiNotesSent()), juce::dontSendNotification);
    if (++midiOutputRefreshCounter >= 30)
    {
        midiOutputRefreshCounter = 0;
        refreshMidiOutputCombo();
    }
    else
    {
        midiOutputStatusLabel.setText(proc.getMidiOutputDescription(), juce::dontSendNotification);
    }
   #if JUCE_ANDROID
    audioDeviceStatusLabel.setText(getAudioDeviceStatusText(), juce::dontSendNotification);
   #endif
    // Visibility/enabled state only depends on these four mode values; recompute
    // the ~80-component layout pass only when one of them actually changes,
    // instead of on every timer tick.
    const int es  = proc.engine.engineSource.load(std::memory_order_relaxed);
    const int spm = proc.engine.samplePlaybackMode.load(std::memory_order_relaxed);
    const int om  = (int) proc.apvts.getRawParameterValue("audioMidiOutputMode")->load();
    const int visKey = ((es & 3) << 12) | ((spm & 3) << 8) | ((midiType & 15) << 4) | (om & 15);
    if (visKey != lastVisibilityKey)
    {
        lastVisibilityKey = visKey;
        updateOutputModeVisibility();
    }
    repaint();
}
