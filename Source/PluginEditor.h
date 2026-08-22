#pragma once

#include <memory>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class SourceActivityMap;

// MIDI-only editor for Cosmic Microwave. The plug-in intentionally keeps a
// silent audio shell for host compatibility, but every visible control here is
// part of the OSC -> MIDI path.
class AudienceEditor final : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit AudienceEditor (AudienceProcessor&);
    ~AudienceEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    void applyUdpPortFromEditor();
    void restoreUdpPortEditor();
    void refreshMidiOutputCombo();
    void updateModeVisibility();
    void updateLiveText();

    static void addChoiceItems (juce::ComboBox&, const juce::StringArray&);
    static void styleLabel (juce::Label&, const juce::String&,
                            juce::Justification = juce::Justification::centredLeft);
    static void styleCombo (juce::ComboBox&);
    static void styleButton (juce::Button&, bool destructive = false);

    AudienceProcessor& proc;
    std::unique_ptr<juce::LookAndFeel_V4> lookAndFeel;
    std::unique_ptr<SourceActivityMap> sourceMap;
    juce::TooltipWindow tooltipWindow { this, 650 };

    // Header telemetry
    juce::Label activeSourcesValue;
    juce::Label activeFingersValue;
    juce::Label notesSentValue;
    juce::Label mpeVoicesValue;

    // OSC input card
    juce::Label portLabel;
    juce::TextEditor portEditor;
    juce::TextButton portApplyButton { "Apply" };
    juce::Label oscStatusLabel;
    juce::Label oscPathLabel;

    // Routing summary card
    juce::Label routingSummaryLabel;
    juce::Label routingDetailLabel;
    juce::Label zoneStatusLabel;

    // Simulator card
    juce::TextButton simAddButton { "+ Source" };
    juce::TextButton simCrowdButton { "+ 25" };
    juce::TextButton simRemoveButton { "Remove" };
    juce::TextButton simClearButton { "Clear" };
    juce::ToggleButton simMoveButton { "Random movement" };

    // Pitch mapping card
    juce::Label rootLabel, rootOctaveLabel, scaleLabel, octavesLabel;
    juce::Label atomicElementLabel, atomicModeLabel;
    juce::ComboBox pitchSystemCombo, rootCombo, rootOctaveCombo, scaleCombo;
    juce::ComboBox atomicElementCombo, atomicModeCombo;
    juce::Slider octavesSlider;

    // MIDI mode / channel routing card
    juce::Label midiTypeLabel, normalRoutingLabel, normalChannelLabel;
    juce::Label mpeZoneLabel, mpeBendRangeLabel, mpePitchModeLabel;
    juce::ComboBox midiTypeCombo, normalRoutingCombo, normalChannelCombo;
    juce::ComboBox mpeZoneCombo, mpeBendRangeCombo, mpePitchModeCombo;
    juce::ToggleButton mpeSetupButton { "Send MPE setup" };

    // MIDI destination card
    juce::Label destinationLabel;
    juce::ComboBox destinationCombo;
    juce::TextButton rescanButton { "Rescan" };
    juce::Label destinationStatusLabel;
    juce::Label destinationDetailLabel;
    juce::TextButton panicButton { "PANIC" };

    juce::Rectangle<int> oscCardBounds;
    juce::Rectangle<int> routingCardBounds;
    juce::Rectangle<int> simulatorCardBounds;
    juce::Rectangle<int> mapCardBounds;
    juce::Rectangle<int> pitchCardBounds;
    juce::Rectangle<int> midiCardBounds;
    juce::Rectangle<int> destinationCardBounds;

    bool updatingPortEditor = false;
    bool portEditorDirty = false;
    bool refreshingDestination = false;
    bool destinationRouteUnresolved = false;
    int lastUdpPort = -1;
    int lastVisibilityKey = -1;
    uint32_t lastMidiOutputRouteRevision = 0;
    juce::StringArray midiOutputOptions;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<ComboAttachment> rootAttachment;
    std::unique_ptr<ComboAttachment> rootOctaveAttachment;
    std::unique_ptr<ComboAttachment> scaleAttachment;
    std::unique_ptr<ComboAttachment> pitchSystemAttachment;
    std::unique_ptr<ComboAttachment> atomicElementAttachment;
    std::unique_ptr<ComboAttachment> atomicModeAttachment;
    std::unique_ptr<SliderAttachment> octavesAttachment;
    std::unique_ptr<ComboAttachment> midiTypeAttachment;
    std::unique_ptr<ComboAttachment> normalRoutingAttachment;
    std::unique_ptr<ComboAttachment> normalChannelAttachment;
    std::unique_ptr<ComboAttachment> mpeZoneAttachment;
    std::unique_ptr<ComboAttachment> mpeBendRangeAttachment;
    std::unique_ptr<ComboAttachment> mpePitchModeAttachment;
    std::unique_ptr<ButtonAttachment> mpeSetupAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudienceEditor)
};
