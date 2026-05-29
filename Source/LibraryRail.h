#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

/*
    LibraryRail

    The left-side patch browser. Lists every subdirectory found in the
    plugin's Samples/ root - each subdirectory is a "library" containing
    its own set of pitched sample files. Click an entry to hot-swap the
    current library; the engine clears its voices and reloads samples.

    Below the list: the active library's name, sample count, and a quick
    scrollable preview of the sample names that were loaded.
*/
class LibraryRail : public juce::Component, private juce::ListBoxModel
{
public:
    explicit LibraryRail (AudienceProcessor& proc);
    ~LibraryRail() override;

    void paint   (juce::Graphics&) override;
    void resized() override;

    // call this when the processor's library list changes
    void refresh();

private:
    enum class RailMode
    {
        Samples,
        Elements
    };

    int  getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    int  countSamplesForLibrary (const juce::String& name) const;
    void setChoiceParameter (const juce::String& parameterId, int choiceIndex);
    int  spectralScaleStartIndex() const;

    AudienceProcessor& proc;
    RailMode           railMode = RailMode::Samples;
    juce::StringArray  libraryNames;
    juce::Array<int>   sampleCounts;
    juce::StringArray  elementNames;
    juce::Array<int>   elementIndices;
    int                selectedRow = -1;

    juce::ListBox      listbox;
    juce::TextEditor   searchBox;
    juce::TextButton   samplesTabBtn  { "Samples" };
    juce::TextButton   elementsTabBtn { "Elements" };
    juce::TextButton   rescanBtn { "Rescan" };
    juce::TextEditor   samplePreview;
};
