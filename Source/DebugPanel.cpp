#include "DebugPanel.h"

namespace
{
    const juce::Colour kBg       { 0xee0a0d18 };
    const juce::Colour kHairline { 0xff272a37 };
    const juce::Colour kAccent   { 0xff5fbcff };
    const juce::Colour kText     { 0xfff1f2f6 };
    const juce::Colour kText3    { 0xff7c7d8a };
}

DebugPanel::DebugPanel (AudienceProcessor& p) : proc(p)
{
    auto styleEditor = [] (juce::TextEditor& e)
    {
        e.setMultiLine(true, false);
        e.setReadOnly(true);
        e.setScrollbarsShown(true);
        e.setCaretVisible(false);
        e.setColour(juce::TextEditor::backgroundColourId,     juce::Colours::transparentBlack);
        e.setColour(juce::TextEditor::outlineColourId,        juce::Colours::transparentBlack);
        e.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        e.setColour(juce::TextEditor::textColourId,           kText);
        e.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                                  11.5f, juce::Font::plain)));
    };
    styleEditor(seatsView);
    styleEditor(scaleView);
    styleEditor(octaveView);
    styleEditor(midiView);
    addAndMakeVisible(seatsView);
    addAndMakeVisible(scaleView);
    addAndMakeVisible(octaveView);
    addAndMakeVisible(midiView);

    startTimerHz(20);
}

DebugPanel::~DebugPanel() { stopTimer(); }

void DebugPanel::timerCallback()
{
    if (! isShowing())
        return;

    seatsView.setText(proc.engine.getActiveSeatsSnapshot(24), juce::dontSendNotification);

    juce::String s;
    s << "scale range : " << proc.engine.getScaleRangeName() << "\n";
    s << "library     : " << proc.currentLibraryName << "  ("
                          << proc.engine.getLibrary().numSamples() << " samples)\n";
    s << "UDP         : " << proc.oscStatus << "\n";
    s << "voices/seats: "
      << proc.engine.getActiveVoiceCount() << " voices / "
      << proc.engine.getRegisteredSeatCount() << " seats\n";
    s << "----------------------------------------\n";

    const int n = proc.engine.getScaleTableSize();
    if (n > 0)
    {
        s << "X mapping -> MIDI step (X=0 -> first, X=1 -> last):\n";
        static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        for (int i = 0; i < n; ++i)
        {
            const int m   = proc.engine.getScaleMidi(i);
            const float x = (float) i / (float) juce::jmax(1, n - 1);
            const int   oct = m / 12 - 1;
            s << "  X=" << juce::String(x, 3).paddedRight(' ', 6)
              << "  midi=" << juce::String(m).paddedRight(' ', 4)
              << "  " << names[((m % 12) + 12) % 12] << oct << "\n";
        }
    }
    scaleView.setText(s, juce::dontSendNotification);
    octaveView.setText(proc.engine.getScaleOneOctaveDebugText(), juce::dontSendNotification);
    midiView.setText(proc.getOutgoingMidiDebugText(96), juce::dontSendNotification);
}

void DebugPanel::visibilityChanged()
{
    if (isVisible()) startTimerHz(20);
    else             stopTimer();
}

void DebugPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(kBg);
    g.fillRoundedRectangle(r, 18.0f);
    g.setColour(kHairline);
    g.drawRoundedRectangle(r, 18.0f, 1.0f);

    g.setColour(kAccent);
    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                              10.0f, juce::Font::plain)));
    g.drawText("MIDI CONSOLE", 18, 14, 220, 14, juce::Justification::left);

    g.setColour(kText);
    g.setFont(juce::Font(juce::FontOptions(15.5f)));
    g.drawText("outgoing MIDI / MPE", 18, 30, 260, 22, juce::Justification::left);

    g.setColour(kText3);
    g.setFont(juce::Font(juce::FontOptions(10.5f)));
    g.drawText("host / virtual port / MPE stream", 18, 56, 320, 14, juce::Justification::left);

    g.setColour(kText);
    g.setFont(juce::Font(juce::FontOptions(15.5f)));
    const int bottomH = juce::jlimit(150, 260, getHeight() / 3);
    const int topBottom = getHeight() - bottomH;
    const int thirdW = getWidth() / 3;
    g.drawText("scale + status", thirdW + 18, 30, 240, 22, juce::Justification::left);
    g.drawText("root octave", thirdW * 2 + 18, 30, 240, 22, juce::Justification::left);
    g.drawText("active seats", 18, topBottom + 14, 240, 22, juce::Justification::left);

    g.setColour(kText3);
    g.setFont(juce::Font(juce::FontOptions(10.5f)));
    g.drawText("row / col / X / Y / note", 18, topBottom + 38, 320, 14, juce::Justification::left);

    g.setColour(kHairline);
    g.drawLine((float) thirdW, 30.0f,
               (float) thirdW, (float) topBottom - 12.0f, 1.0f);
    g.drawLine((float) thirdW * 2.0f, 30.0f,
               (float) thirdW * 2.0f, (float) topBottom - 12.0f, 1.0f);
    g.drawLine(18.0f, (float) topBottom,
               (float) getWidth() - 18.0f, (float) topBottom, 1.0f);
}

void DebugPanel::resized()
{
    const int bottomH = juce::jlimit(150, 260, getHeight() / 3);
    const int topBottom = getHeight() - bottomH;
    const int thirdW = getWidth() / 3;
    midiView.setBounds(18, 76, thirdW - 28, topBottom - 90);
    scaleView.setBounds(thirdW + 18, 56, thirdW - 36, topBottom - 70);
    octaveView.setBounds(thirdW * 2 + 18, 56, getWidth() - thirdW * 2 - 36, topBottom - 70);
    seatsView.setBounds(18, topBottom + 58, getWidth() - 36, getHeight() - topBottom - 72);
}
