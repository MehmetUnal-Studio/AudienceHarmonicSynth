#pragma once

#include <array>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "AuroraComponent.h"
#include "LibraryRail.h"
#include "DebugPanel.h"

class AudienceEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AudienceEditor (AudienceProcessor&);
    ~AudienceEditor() override;

    void paint   (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void paintBrandMark (juce::Graphics&, juce::Rectangle<float>);
    void paintWindowDots (juce::Graphics&, juce::Rectangle<int>);
    void paintBedPanel  (juce::Graphics&, juce::Rectangle<int>);
    void paintParticlePanel (juce::Graphics&, juce::Rectangle<int>);
    void paintMacroPanel (juce::Graphics&, juce::Rectangle<int>);
    void paintScaleKeyboard (juce::Graphics&, juce::Rectangle<int>);
    void paintRibbon    (juce::Graphics&, juce::Rectangle<int>);
    void paintLivePill  (juce::Graphics&, juce::Rectangle<int>);
    void paintPerformanceOverlay (juce::Graphics&, juce::Rectangle<int>);
    void updatePerformanceVisibility();
    void updateOutputModeVisibility();
    void refreshMidiOutputCombo();
    void showAudioSettings();
    juce::String getAudioDeviceStatusText() const;
    void updateComputerKeyboard();
    void pressKeyboardStep (int slot, int scaleStep, float velocity);
    void releaseKeyboardSlot (int slot);
    void releaseAllKeyboardSlots();
    int  keyboardStepAt (juce::Point<int>) const;
    juce::String keyboardStepLabel (int scaleStep) const;
    juce::String keyboardKeyName (int slot) const;

    AudienceProcessor& proc;

    LibraryRail     libraryRail;
    AuroraComponent aurora;
    DebugPanel      debugPanel;
    juce::ToggleButton debugToggle { "Debug" };
    juce::ToggleButton performanceToggle { "Performance" };
    juce::TooltipWindow tooltipWindow { this, 700 };

    // bed controls (sample layer mix + global FX)
    juce::Slider pitchSlider, layerMixSlider, wetDrySlider, reverbSlider, delaySlider, tapeDriveSlider, masterSlider;
    juce::Label  pitchLabel,  layerMixLabel,  wetDryLabel,  reverbLabel,  delayLabel,  tapeDriveLabel,  masterLabel;

    // voice / texture controls
    juce::Slider attackSlider, releaseSlider, brightnessSlider, movementSlider;
    juce::Slider grainSizeSlider, grainDensitySlider, pitchSpreadSlider, positionJitterSlider, stereoSpreadSlider;
    juce::Label  attackLabel,  releaseLabel,  brightnessLabel,  movementLabel;
    juce::Label  grainSizeLabel, grainDensityLabel, pitchSpreadLabel, positionJitterLabel, stereoSpreadLabel;
    juce::ToggleButton reverseToggle { "Reverse" };
    juce::ComboBox grainShapeCombo;
    juce::Label grainShapeLabel;

    // performance macros + scale
    juce::Slider energySlider, motionMacroSlider, toneMacroSlider, spaceMacroSlider;
    juce::Label  energyLabel,  motionMacroLabel,  toneMacroLabel,  spaceMacroLabel;
    juce::ComboBox soundModeCombo;
    juce::Label soundModeLabel;
    juce::ComboBox engineSourceCombo, samplePlaybackCombo, spectralElementCombo, atomicScaleModeCombo;
    juce::Label engineSourceLabel, samplePlaybackLabel, spectralElementLabel, atomicScaleModeLabel;
    juce::Slider spectralPartialSlider, spectralStretchSlider;
    juce::Label spectralPartialLabel, spectralStretchLabel;
    juce::ComboBox rootCombo, rootOctaveCombo, scaleCombo;
    juce::ComboBox polyphonyCombo;
    juce::ComboBox audioMidiOutputModeCombo, midiOutputTypeCombo, externalMidiPitchModeCombo;
    juce::ComboBox normalMidiChannelCombo, mpeBendRangeCombo, mpePitchModeCombo;
    juce::ComboBox midiOutputDeviceCombo;
    juce::Slider octavesSlider;
    juce::Label rootLabel, rootOctaveLabel, scaleLabel, octavesLabel, polyphonyLabel;
    juce::Label audioMidiOutputModeLabel, midiOutputTypeLabel, externalMidiPitchModeLabel;
    juce::Label normalMidiChannelLabel, mpeBendRangeLabel, mpePitchModeLabel;
    juce::Label midiOutputDeviceLabel, midiOutputStatusLabel, midiActivityLabel;
    juce::ToggleButton mpeSetupToggle { "Setup" };
    juce::TextButton midiOutputRefreshBtn { "Rescan" };
    juce::TextButton panicBtn { "Panic" };
    juce::ToggleButton muteToggle { "Mute" };
    juce::ToggleButton freezeToggle { "Freeze" };
    juce::ToggleButton partialSoloToggle { "Solo" };
    juce::TextButton audioSettingsBtn { "Audio Setup" };
    juce::Label audioDeviceStatusLabel;

    // network
    juce::TextEditor portEditor;
    juce::TextButton portApplyBtn { "Apply" };

    // simulator
    juce::TextButton   simAddBtn    { "+ Add" };
    juce::TextButton   simCrowdBtn  { "+25 Crowd" };
    juce::TextButton   simRemoveBtn { "Remove" };
    juce::ToggleButton simMoveBtn   { "Random Movement" };
    juce::TextButton   simClearBtn  { "Clear All" };

    juce::Rectangle<int> bedPanelBounds, particlePanelBounds, macroPanelBounds, keyboardPanelBounds, ribbonBounds;
    juce::Rectangle<int> midiOutputPanelBounds;

    static constexpr int computerKeyboardSlots = 36;
    static constexpr int mouseKeyboardSlot = computerKeyboardSlots;
    std::array<bool, PartialEngine::MAX_KEYBOARD_SLOTS> keyboardSlotDown {};
    int mouseKeyboardStep = -1;
    juce::StringArray lastMidiOutputOptions;
    int midiOutputRefreshCounter = 0;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SA> pitchAttach, layerMixAttach, wetDryAttach, reverbAttach, delayAttach, tapeDriveAttach, masterAttach;
    std::unique_ptr<SA> attackAttach, releaseAttach, brightnessAttach, movementAttach;
    std::unique_ptr<SA> grainSizeAttach, grainDensityAttach, pitchSpreadAttach, positionJitterAttach, stereoSpreadAttach;
    std::unique_ptr<SA> energyAttach, motionMacroAttach, toneMacroAttach, spaceMacroAttach, octavesAttach;
    std::unique_ptr<SA> spectralPartialAttach, spectralStretchAttach;
    std::unique_ptr<CA> soundModeAttach, engineSourceAttach, samplePlaybackAttach, spectralElementAttach, atomicScaleModeAttach;
    std::unique_ptr<CA> rootAttach, rootOctaveAttach, scaleAttach, grainShapeAttach, polyphonyAttach;
    std::unique_ptr<CA> audioMidiOutputModeAttach, midiOutputTypeAttach, externalMidiPitchModeAttach;
    std::unique_ptr<CA> normalMidiChannelAttach, mpeBendRangeAttach, mpePitchModeAttach;
    std::unique_ptr<BA> reverseAttach, freezeAttach, partialSoloAttach, mpeSetupAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudienceEditor)
};
