#pragma once

#include <array>
#include <memory>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class SourceActivityMap;
class SegmentedChoice;
class PitchSpectrumDisplay;

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
    void updateConsoleTelemetry();
    void showPage (bool showConsole);
    void setPerformControlsVisible (bool shouldBeVisible);
    void setConsoleControlsVisible (bool shouldBeVisible);

    static void addChoiceItems (juce::ComboBox&, const juce::StringArray&);
    static void styleLabel (juce::Label&, const juce::String&,
                            juce::Justification = juce::Justification::centredLeft);
    static void styleCombo (juce::ComboBox&);
    static void styleButton (juce::Button&, bool destructive = false);

    AudienceProcessor& proc;
    std::unique_ptr<juce::LookAndFeel_V4> lookAndFeel;
    std::unique_ptr<SourceActivityMap> sourceMap;
    juce::TooltipWindow tooltipWindow { this, 650 };

    // Accessible navigation. The Perform page remains the musical control
    // surface; Show Console is a deliberately separate venue-safety view.
    juce::TextButton performTabButton { "PERFORM" };
    juce::TextButton showConsoleTabButton { "SHOW CONSOLE" };

    // Header telemetry
    juce::Label versionLabel;
    juce::Label activeSourcesValue;
    juce::Label activeFingersValue;
    juce::Label notesSentValue;
    juce::Label activeNotesValue;
    juce::Label activeSourcesCaption;
    juce::Label activeFingersCaption;
    juce::Label notesSentCaption;
    juce::Label activeNotesCaption;

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
    juce::TextButton simAddButton { "+1 Held" };
    juce::TextButton simCrowdButton { "+25 Crowd" };
    juce::TextButton simRemoveButton { "Remove 1" };
    juce::TextButton simClearButton { "Clear" };
    juce::ComboBox simProfileCombo;
    juce::Label simStatusLabel;
    juce::ToggleButton simMoveButton { "Move active U/V" };

    // Pitch mapping card
    juce::Label rootLabel, rootOctaveLabel, scaleLabel, octavesLabel;
    juce::Label atomicElementLabel, atomicModeLabel;
    juce::ComboBox pitchSystemCombo, rootCombo, rootOctaveCombo, scaleCombo;
    juce::ComboBox atomicElementCombo, atomicModeCombo;
    juce::Slider octavesSlider;

    // Temporal note scheduling
    juce::Label timeStatusLabel;
    juce::Label timeTelemetryLabel;
    juce::TextButton governorModeButton { "ADAPTIVE" };
    juce::Label timeModeLabel, clockSourceLabel, internalBpmLabel, gridDivisionLabel;
    juce::Label noteDurationLabel, ensembleSameNoteLabel;
    juce::Label maxAttacksLabel, maxActiveVoicesLabel, gatePercentLabel, temporalSpreadLabel;
    juce::Label governorAttacksValue, governorActiveVoicesValue, governorSpreadValue;
    juce::ComboBox timeModeCombo, clockSourceCombo, gridDivisionCombo, temporalSpreadCombo;
    juce::ComboBox noteDurationCombo, ensembleSameNoteCombo;
    juce::Slider internalBpmSlider, maxAttacksSlider, maxActiveVoicesSlider, gatePercentSlider;

    // MIDI mode / channel routing card
    juce::Label midiTypeLabel, normalRoutingLabel, normalChannelLabel, sourceCapacityLabel;
    juce::ComboBox midiTypeCombo, normalRoutingCombo, normalChannelCombo, sourceCapacityCombo;

    // MIDI destination card
    juce::Label destinationLabel;
    juce::ComboBox destinationCombo;
    juce::TextButton rescanButton { "Rescan" };
    juce::Label destinationStatusLabel;
    juce::Label destinationDetailLabel;
    juce::TextButton panicButton { "PANIC" };

    // Show Console: routing contract
    juce::Label outputPathLabel, expectedZoneLabel;
    juce::ComboBox outputPathCombo, expectedZoneCombo;
    juce::ToggleButton exclusivePortButton { "Exclusive UDP ownership" };
    juce::Label routeConsoleStatusLabel;
    juce::Label factoryPresetLabel;
    juce::ComboBox factoryPresetCombo;
    juce::TextButton autoAssignRetryButton { "RETRY AUTO" };
    juce::Label factoryPresetStatusLabel;

    // Show Console: pressure-aware safety governor + telemetry
    juce::ToggleButton safetyGovernorButton { "Safety Governor enabled" };
    juce::Label safetyStateLabel, safetyReasonLabel;
    juce::Label safetyIngressLabel, safetyDeadlineLabel;
    juce::Label safetyFifoLabel, safetyQueueLabel;

    // Show Console: venue preflight. Every row includes a text state so status
    // is never communicated by colour alone.
    juce::Label preflightSummaryLabel;
    std::array<juce::Label, 8> preflightRows;

    // Show Console: process-local Global Conductor
    juce::Label conductorRoleLabel, conductorGroupLabel;
    juce::Label conductorAttackBudgetLabel, conductorVoiceBudgetLabel;
    juce::ComboBox conductorRoleCombo, conductorGroupCombo;
    juce::Slider conductorAttackBudgetSlider, conductorVoiceBudgetSlider;
    juce::Label conductorStatusLabel, conductorQuotaLabel;

    // Show Console: crowd-expression CC macros
    juce::ToggleButton crowdMacrosButton { "Crowd Expression macros" };
    juce::Label macroChannelLabel, macroRateLabel;
    juce::Label macroDensityCcLabel, macroCentroidXCcLabel;
    juce::Label macroCentroidYCcLabel, macroMotionCcLabel;
    juce::ComboBox macroChannelCombo, macroRateCombo;
    juce::Slider macroDensityCcSlider, macroCentroidXCcSlider;
    juce::Slider macroCentroidYCcSlider, macroMotionCcSlider;
    juce::Label macroStatusLabel;

    // Show Console: runtime-only 64/128/256 live-source signal census.
    juce::TextButton sourceQualityButton { "START 64 CHECK" };
    juce::Label sourceQualityGateLabel;
    juce::Label sourceQualityCoverageLabel;
    juce::Label sourceQualityTimingLabel;
    juce::Label sourceQualityFaultLabel;

    // Show Console: Capture/Replay Chaos Lab is intentionally an external tool
    // so file and UDP I/O can never enter the plug-in audio callback.
    juce::Label chaosTitleLabel, chaosBodyLabel, chaosCommandLabel;

    juce::Rectangle<int> oscCardBounds;
    juce::Rectangle<int> routingCardBounds;
    juce::Rectangle<int> simulatorCardBounds;
    juce::Rectangle<int> mapCardBounds;
    juce::Rectangle<int> timeCardBounds;
    juce::Rectangle<int> pitchCardBounds;
    juce::Rectangle<int> midiCardBounds;
    juce::Rectangle<int> destinationCardBounds;
    juce::Rectangle<int> safetyCardBounds;
    juce::Rectangle<int> preflightCardBounds;
    juce::Rectangle<int> conductorCardBounds;
    juce::Rectangle<int> routeConsoleCardBounds;
    juce::Rectangle<int> macrosCardBounds;
    juce::Rectangle<int> chaosCardBounds;

    // Presentation-only proxies. The hidden ComboBoxes remain the APVTS
    // attachment owners; these focusable segmented controls mirror them so
    // automation/state recall keeps the exact existing parameter semantics.
    std::unique_ptr<SegmentedChoice> timeModeSegments;
    std::unique_ptr<SegmentedChoice> clockSourceSegments;
    std::unique_ptr<SegmentedChoice> pitchSystemSegments;
    std::unique_ptr<PitchSpectrumDisplay> pitchSpectrumDisplay;

    bool showConsolePage = false;
    bool updatingPortEditor = false;
    bool portEditorDirty = false;
    bool refreshingDestination = false;
    bool refreshingFactoryPreset = false;
    bool destinationRouteUnresolved = false;
    int lastUdpPort = -1;
    int lastVisibilityKey = -1;
    uint32_t lastTimeFieldMerged = 0;
    double mergeActivityUntilMs = 0.0;
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
    std::unique_ptr<ComboAttachment> timeModeAttachment;
    std::unique_ptr<ComboAttachment> clockSourceAttachment;
    std::unique_ptr<SliderAttachment> internalBpmAttachment;
    std::unique_ptr<ComboAttachment> gridDivisionAttachment;
    std::unique_ptr<ComboAttachment> noteDurationAttachment;
    std::unique_ptr<ComboAttachment> ensembleSameNoteAttachment;
    std::unique_ptr<SliderAttachment> maxAttacksAttachment;
    std::unique_ptr<SliderAttachment> maxActiveVoicesAttachment;
    std::unique_ptr<SliderAttachment> gatePercentAttachment;
    std::unique_ptr<ComboAttachment> temporalSpreadAttachment;
    std::unique_ptr<ButtonAttachment> governorModeAttachment;
    std::unique_ptr<ComboAttachment> midiTypeAttachment;
    std::unique_ptr<ComboAttachment> normalRoutingAttachment;
    std::unique_ptr<ComboAttachment> normalChannelAttachment;
    std::unique_ptr<ComboAttachment> sourceCapacityAttachment;
    std::unique_ptr<ComboAttachment> outputPathAttachment;
    std::unique_ptr<ComboAttachment> expectedZoneAttachment;
    std::unique_ptr<ButtonAttachment> exclusivePortAttachment;
    std::unique_ptr<ButtonAttachment> safetyGovernorAttachment;
    std::unique_ptr<ComboAttachment> conductorRoleAttachment;
    std::unique_ptr<ComboAttachment> conductorGroupAttachment;
    std::unique_ptr<SliderAttachment> conductorAttackBudgetAttachment;
    std::unique_ptr<SliderAttachment> conductorVoiceBudgetAttachment;
    std::unique_ptr<ButtonAttachment> crowdMacrosAttachment;
    std::unique_ptr<ComboAttachment> macroChannelAttachment;
    std::unique_ptr<ComboAttachment> macroRateAttachment;
    std::unique_ptr<SliderAttachment> macroDensityCcAttachment;
    std::unique_ptr<SliderAttachment> macroCentroidXCcAttachment;
    std::unique_ptr<SliderAttachment> macroCentroidYCcAttachment;
    std::unique_ptr<SliderAttachment> macroMotionCcAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudienceEditor)
};
