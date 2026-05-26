#pragma once

#include <array>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "MidiProcessor.h"

class AudienceMidiGeneratorEditor final : public juce::AudioProcessorEditor,
                                          private juce::Slider::Listener,
                                          private juce::Timer
{
public:
    explicit AudienceMidiGeneratorEditor (AudienceMidiProcessor&);
    ~AudienceMidiGeneratorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    struct NotePulseEvent
    {
        int midi = 60;
        int velocity = 96;
        double timeMs = 0.0;
    };

    struct TrailDot
    {
        float x = 0.0f;
        float y = 0.0f;
        float radius = 5.0f;
        float alpha = 1.0f;
    };

    enum class VenueShape { Stadium = 0, Grid, Scatter };

    void timerCallback() override;
    void sliderValueChanged (juce::Slider*) override;

    void styleControls();
    void refreshMidiOutputCombo();
    void refreshRangeLabel();
    void captureNewMidiEvents();
    void updateDebugVisibility();

    int getIntParam (const juce::String& id) const;
    bool getBoolParam (const juce::String& id) const;
    void setIntParam (const juce::String& id, int value);
    void setBoolParam (const juce::String& id, bool value);
    int currentScaleMask() const;
    bool isPitchClassInScale (int pitchClass) const;
    void setCustomMask (int mask);
    void setRemap (int inputPc, int outputPc);
    void resetRemap();

    juce::Point<float> projectSeat (int row, int col, juce::Rectangle<float> venue) const;
    void drawShell (juce::Graphics&);
    void drawTitleBar (juce::Graphics&);
    void drawHeader (juce::Graphics&);
    void drawVenue (juce::Graphics&);
    void drawNotePulse (juce::Graphics&);
    void drawCrowdBar (juce::Graphics&);
    void drawScaleCard (juce::Graphics&);
    void drawIOCard (juce::Graphics&);
    void drawRangeCard (juce::Graphics&);
    void drawLogs (juce::Graphics&);

    AudienceMidiProcessor& proc;

    juce::Slider channelSlider, rangeLowSlider, rangeHighSlider, transposeSlider;
    juce::ComboBox midiOutputCombo, rootCombo, scaleCombo, correctionCombo;
    juce::TextEditor udpEditor, incomingMonitor, outgoingMonitor;
    juce::ToggleButton scaleEnableButton { "Scale On" };
    juce::ToggleButton moveButton { "Random Move" };
    juce::TextButton applyUdpButton { "Apply" };
    juce::TextButton panicButton { "Panic" };
    juce::TextButton addButton { "+ Seat" };
    juce::TextButton crowdButton { "+25 Crowd" };
    juce::TextButton removeButton { "Remove" };
    juce::TextButton clearButton { "Clear" };
    juce::TextButton resetRemapButton { "Reset Remap" };
    juce::TextButton debugToggleButton { "Show Debug" };

    juce::Rectangle<int> titleBar, header, mainShell, leftColumn, rightColumn;
    juce::Rectangle<int> venuePanel, notePulsePanel, crowdPanel;
    juce::Rectangle<int> scaleCard, ioCard, rangeCard, logPanel, incomingLogPanel, outgoingLogPanel;
    std::array<juce::Rectangle<int>, 3> venueTabBounds {};
    std::array<juce::Rectangle<int>, 12> rootKeyBounds {};
    std::array<juce::Rectangle<int>, 12> pitchPadBounds {};
    std::array<std::array<juce::Rectangle<int>, 12>, 12> remapCellBounds {};

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SA> channelAttach, rangeLowAttach, rangeHighAttach, transposeAttach;
    std::unique_ptr<CA> rootAttach, scaleAttach, correctionAttach;
    std::unique_ptr<BA> scaleEnableAttach;

    juce::StringArray lastMidiOutputOptions;
    std::vector<NotePulseEvent> noteEvents;
    std::vector<TrailDot> trails;
    uint32_t lastSeenOutputSerial = 0;
    int midiOutputRefreshCounter = 0;
    int hotPc = -1;
    double hotPcUntilMs = 0.0;
    int hoveredRemapColumn = -1;
    VenueShape venueShape = VenueShape::Stadium;
    bool updatingRangeSliders = false;
    bool debugLogsVisible = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudienceMidiGeneratorEditor)
};
