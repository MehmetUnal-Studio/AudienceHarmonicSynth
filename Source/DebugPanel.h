#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

/*
    DebugPanel

    Toggleable overlay for MIDI/MPE diagnostics. Activates / deactivates
    from the ribbon toggle.
*/
class DebugPanel : public juce::Component, private juce::Timer
{
public:
    explicit DebugPanel (AudienceProcessor& p);
    ~DebugPanel() override;

    void paint   (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    void timerCallback() override;

    AudienceProcessor& proc;
    juce::TextEditor   scaleView;
    juce::TextEditor   octaveView;
    juce::TextEditor   midiView;
    juce::TextEditor   reportView;
    juce::TextButton   copyReportButton { "Copy Report" };
    juce::String       lastReport;
};
