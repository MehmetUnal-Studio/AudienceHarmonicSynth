#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

/*
    DebugPanel

    Toggleable overlay that shows incoming OSC data per-seat in real time -
    like a synth console. Activates / deactivates from the ribbon toggle.
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
    juce::TextEditor   seatsView;
    juce::TextEditor   scaleView;
    juce::TextEditor   octaveView;
    juce::TextEditor   midiView;
};
