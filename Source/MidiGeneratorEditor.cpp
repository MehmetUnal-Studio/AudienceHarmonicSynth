#include "MidiGeneratorEditor.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    constexpr const char* degrees[] = { "R", "b2", "2", "b3", "3", "4", "b5", "5", "b6", "6", "b7", "7" };

    float srgbTransfer (float c)
    {
        c = juce::jlimit(0.0f, 1.0f, c);
        return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    }

    juce::Colour oklch (float L, float C, float H, float alpha = 1.0f)
    {
        const float h = H * juce::MathConstants<float>::pi / 180.0f;
        const float a = C * std::cos(h);
        const float b = C * std::sin(h);

        const float l_ = L + 0.3963377774f * a + 0.2158037573f * b;
        const float m_ = L - 0.1055613458f * a - 0.0638541728f * b;
        const float s_ = L - 0.0894841775f * a - 1.2914855480f * b;

        const float l = l_ * l_ * l_;
        const float m = m_ * m_ * m_;
        const float s = s_ * s_ * s_;

        const float r = +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
        const float g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
        const float bl = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;

        return juce::Colour::fromFloatRGBA(srgbTransfer(r), srgbTransfer(g), srgbTransfer(bl),
                                           juce::jlimit(0.0f, 1.0f, alpha));
    }

    juce::Colour bg()            { return oklch(0.16f, 0.005f, 70.0f); }
    juce::Colour bgDeep()        { return oklch(0.12f, 0.006f, 70.0f); }
    juce::Colour surface1()      { return oklch(0.20f, 0.006f, 70.0f); }
    juce::Colour surface2()      { return oklch(0.24f, 0.007f, 70.0f); }
    juce::Colour surface3()      { return oklch(0.28f, 0.008f, 70.0f); }
    juce::Colour border()        { return oklch(0.32f, 0.008f, 70.0f); }
    juce::Colour text()          { return oklch(0.95f, 0.005f, 80.0f); }
    juce::Colour textDim()       { return oklch(0.70f, 0.008f, 80.0f); }
    juce::Colour textMute()      { return oklch(0.52f, 0.010f, 80.0f); }
    juce::Colour accent()        { return oklch(0.82f, 0.18f, 95.0f); }
    juce::Colour accent2()       { return oklch(0.78f, 0.16f, 145.0f); }
    juce::Colour hot()           { return oklch(0.72f, 0.22f, 25.0f); }
    juce::Colour darkOnAccent()  { return oklch(0.18f, 0.02f, 95.0f); }

    juce::Font sans (float size, int style = juce::Font::plain)
    {
        return juce::Font(juce::FontOptions("Geist", size, style));
    }

    juce::Font mono (float size, int style = juce::Font::plain)
    {
        return juce::Font(juce::FontOptions("JetBrains Mono", size, style));
    }

    void drawRoundPanel (juce::Graphics& g, juce::Rectangle<int> r, float radius = 14.0f)
    {
        g.setColour(surface1());
        g.fillRoundedRectangle(r.toFloat(), radius);
        g.setColour(border());
        g.drawRoundedRectangle(r.toFloat().reduced(0.5f), radius, 1.0f);
    }

    void drawEyebrow (juce::Graphics& g, juce::String textToDraw, juce::Rectangle<int> r,
                      juce::Colour colour = textMute())
    {
        g.setColour(colour);
        g.setFont(mono(10.0f, juce::Font::bold));
        g.drawFittedText(textToDraw.toUpperCase(), r, juce::Justification::centredLeft, 1);
    }

    void styleCombo (juce::ComboBox& combo)
    {
        combo.setColour(juce::ComboBox::backgroundColourId, bgDeep());
        combo.setColour(juce::ComboBox::outlineColourId, border());
        combo.setColour(juce::ComboBox::textColourId, text());
        combo.setColour(juce::ComboBox::arrowColourId, textDim());
    }

    void styleSlider (juce::Slider& slider)
    {
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 20);
        slider.setColour(juce::Slider::trackColourId, accent());
        slider.setColour(juce::Slider::backgroundColourId, surface3());
        slider.setColour(juce::Slider::thumbColourId, text());
        slider.setColour(juce::Slider::textBoxTextColourId, text());
        slider.setColour(juce::Slider::textBoxBackgroundColourId, bgDeep());
        slider.setColour(juce::Slider::textBoxOutlineColourId, border());
    }

    void styleButton (juce::Button& button, juce::Colour colour = text())
    {
        button.setColour(juce::TextButton::buttonColourId, surface2());
        button.setColour(juce::TextButton::buttonOnColourId, surface3());
        button.setColour(juce::TextButton::textColourOffId, colour);
        button.setColour(juce::TextButton::textColourOnId, colour);
    }

    juce::String midiName (int midi)
    {
        return juce::String(noteNames[((midi % 12) + 12) % 12]) + juce::String(midi / 12 - 1);
    }
}

AudienceMidiGeneratorEditor::AudienceMidiGeneratorEditor (AudienceMidiProcessor& p)
    : juce::AudioProcessorEditor(&p), proc(p)
{
    setSize(1320, 820);
    setResizable(true, true);
    setResizeLimits(1040, 760, 1800, 1280);

    std::initializer_list<juce::Component*> components {
        &channelSlider, &rangeLowSlider, &rangeHighSlider, &transposeSlider,
        &midiOutputCombo, &rootCombo, &scaleCombo, &correctionCombo,
        &udpEditor, &incomingMonitor, &outgoingMonitor, &scaleEnableButton,
        &moveButton, &applyUdpButton, &panicButton, &addButton, &crowdButton,
        &removeButton, &clearButton, &resetRemapButton, &debugToggleButton
    };

    for (auto* c : components)
        addAndMakeVisible(*c);

    rootCombo.addItemList(MidiScaleModule::rootNames(), 1);
    scaleCombo.addItemList(MidiScaleModule::scaleNames(), 1);
    correctionCombo.addItemList(MidiScaleModule::correctionModeNames(), 1);
    refreshMidiOutputCombo();
    midiOutputCombo.onChange = [this] { proc.setMidiOutputOptionIndex(midiOutputCombo.getSelectedItemIndex()); };

    channelAttach = std::make_unique<SA>(proc.apvts, "channel", channelSlider);
    rootAttach = std::make_unique<CA>(proc.apvts, "root", rootCombo);
    scaleAttach = std::make_unique<CA>(proc.apvts, "scaleMode", scaleCombo);
    correctionAttach = std::make_unique<CA>(proc.apvts, "midiScaleCorrection", correctionCombo);
    rangeLowAttach = std::make_unique<SA>(proc.apvts, "rangeLowOctave", rangeLowSlider);
    rangeHighAttach = std::make_unique<SA>(proc.apvts, "rangeHighOctave", rangeHighSlider);
    transposeAttach = std::make_unique<SA>(proc.apvts, "transpose", transposeSlider);
    scaleEnableAttach = std::make_unique<BA>(proc.apvts, "midiScaleEnabled", scaleEnableButton);

    rangeLowSlider.addListener(this);
    rangeHighSlider.addListener(this);

    udpEditor.setText(juce::String(proc.udpPort), false);
    udpEditor.setInputRestrictions(5, "0123456789");
    applyUdpButton.onClick = [this]
    {
        proc.setUdpPort(udpEditor.getText().getIntValue());
        repaint();
    };

    panicButton.onClick = [this] { proc.panic(); repaint(); };
    debugToggleButton.onClick = [this]
    {
        debugLogsVisible = ! debugLogsVisible;
        updateDebugVisibility();
        resized();
        repaint();
    };
    addButton.onClick = [this] { proc.simulator.addRandomSeat(); };
    crowdButton.onClick = [this] { proc.simulator.addRandomSeats(25); };
    removeButton.onClick = [this] { proc.simulator.removeRandomSeat(); };
    clearButton.onClick = [this] { proc.panic(); };
    moveButton.onClick = [this] { proc.simulator.setRandomMovement(moveButton.getToggleState()); };
    resetRemapButton.onClick = [this] { resetRemap(); };

    styleControls();
    refreshRangeLabel();
    updateDebugVisibility();
    startTimerHz(30);
}

AudienceMidiGeneratorEditor::~AudienceMidiGeneratorEditor()
{
    rangeLowSlider.removeListener(this);
    rangeHighSlider.removeListener(this);
    stopTimer();
}

void AudienceMidiGeneratorEditor::styleControls()
{
    for (auto* s : { &channelSlider, &rangeLowSlider, &rangeHighSlider, &transposeSlider })
        styleSlider(*s);
    rangeLowSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

    for (auto* c : { &midiOutputCombo, &rootCombo, &scaleCombo, &correctionCombo })
        styleCombo(*c);

    for (auto* b : { &applyUdpButton, &addButton, &crowdButton, &removeButton, &clearButton,
                     &resetRemapButton, &debugToggleButton })
        styleButton(*b);

    styleButton(panicButton, hot());
    styleButton(scaleEnableButton, accent());
    styleButton(moveButton, accent2());

    for (auto* ed : { &incomingMonitor, &outgoingMonitor })
    {
        ed->setMultiLine(true);
        ed->setReadOnly(true);
        ed->setScrollbarsShown(true);
        ed->setCaretVisible(false);
        ed->setColour(juce::TextEditor::backgroundColourId, bg());
        ed->setColour(juce::TextEditor::textColourId, textDim());
        ed->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        ed->setFont(mono(11.0f));
    }

    udpEditor.setColour(juce::TextEditor::backgroundColourId, bgDeep());
    udpEditor.setColour(juce::TextEditor::textColourId, text());
    udpEditor.setColour(juce::TextEditor::outlineColourId, border());
}

void AudienceMidiGeneratorEditor::updateDebugVisibility()
{
    debugToggleButton.setButtonText(debugLogsVisible ? "Hide Debug" : "Show Debug");
    incomingMonitor.setVisible(debugLogsVisible);
    outgoingMonitor.setVisible(debugLogsVisible);

    if (debugLogsVisible)
    {
        incomingMonitor.setText(proc.getIncomingMonitorText(), juce::dontSendNotification);
        outgoingMonitor.setText(proc.getOutgoingMonitorText(), juce::dontSendNotification);
    }
}

int AudienceMidiGeneratorEditor::getIntParam (const juce::String& id) const
{
    if (auto* value = proc.apvts.getRawParameterValue(id))
        return (int) value->load();
    return 0;
}

bool AudienceMidiGeneratorEditor::getBoolParam (const juce::String& id) const
{
    if (auto* value = proc.apvts.getRawParameterValue(id))
        return value->load() > 0.5f;
    return false;
}

void AudienceMidiGeneratorEditor::setIntParam (const juce::String& id, int value)
{
    if (auto* param = proc.apvts.getParameter(id))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1((float) value));
        param->endChangeGesture();
    }
}

void AudienceMidiGeneratorEditor::setBoolParam (const juce::String& id, bool value)
{
    if (auto* param = proc.apvts.getParameter(id))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost(value ? 1.0f : 0.0f);
        param->endChangeGesture();
    }
}

int AudienceMidiGeneratorEditor::currentScaleMask() const
{
    const int scaleIndex = juce::jlimit(0, MidiScaleModule::numScaleTypes - 1, getIntParam("scaleMode"));
    const int customMask = juce::jlimit(1, 4095, getIntParam("midiScaleCustomMask"));
    return MidiScaleModule::maskForScaleType((MidiScaleModule::ScaleType) scaleIndex, customMask);
}

bool AudienceMidiGeneratorEditor::isPitchClassInScale (int pitchClass) const
{
    const int root = juce::jlimit(0, 11, getIntParam("root"));
    const int rel = (pitchClass - root + 12) % 12;
    return (currentScaleMask() & (1 << rel)) != 0;
}

void AudienceMidiGeneratorEditor::setCustomMask (int mask)
{
    setIntParam("midiScaleCustomMask", juce::jlimit(1, 4095, mask));
    setIntParam("scaleMode", MidiScaleModule::numScaleTypes - 1);
}

void AudienceMidiGeneratorEditor::setRemap (int inputPc, int outputPc)
{
    inputPc = juce::jlimit(0, 11, inputPc);
    outputPc = juce::jlimit(0, 11, outputPc);
    const int current = getIntParam("midiScaleRemap" + juce::String(inputPc));
    setIntParam("midiScaleRemap" + juce::String(inputPc), current == outputPc ? inputPc : outputPc);
}

void AudienceMidiGeneratorEditor::resetRemap()
{
    for (int pc = 0; pc < 12; ++pc)
        setIntParam("midiScaleRemap" + juce::String(pc), pc);
}

void AudienceMidiGeneratorEditor::refreshMidiOutputCombo()
{
    const auto options = proc.getMidiOutputOptions();
    if (options != lastMidiOutputOptions || midiOutputCombo.getNumItems() != options.size())
    {
        lastMidiOutputOptions = options;
        midiOutputCombo.clear(juce::dontSendNotification);
        for (int i = 0; i < options.size(); ++i)
            midiOutputCombo.addItem(options[i], i + 1);
    }

    midiOutputCombo.setSelectedItemIndex(juce::jlimit(0, juce::jmax(0, options.size() - 1),
                                                      proc.getMidiOutputOptionIndex()),
                                         juce::dontSendNotification);
    midiOutputCombo.setTooltip(proc.getMidiOutputDescription());
}

void AudienceMidiGeneratorEditor::refreshRangeLabel()
{
    repaint(rangeCard);
}

void AudienceMidiGeneratorEditor::sliderValueChanged (juce::Slider* slider)
{
    if (slider != &rangeLowSlider && slider != &rangeHighSlider)
        return;

    if (updatingRangeSliders)
        return;

    const juce::ScopedValueSetter<bool> guard(updatingRangeSliders, true);
    const int low = (int) rangeLowSlider.getValue();
    const int high = (int) rangeHighSlider.getValue();

    if (slider == &rangeLowSlider && low >= high)
        rangeHighSlider.setValue(juce::jlimit(1, 8, low + 1), juce::sendNotificationSync);
    else if (slider == &rangeHighSlider && high <= low)
        rangeLowSlider.setValue(juce::jlimit(0, 7, high - 1), juce::sendNotificationSync);

    refreshRangeLabel();
}

void AudienceMidiGeneratorEditor::captureNewMidiEvents()
{
    auto events = proc.getOutgoingMonitorEvents();
    std::sort(events.begin(), events.end(), [] (const auto& a, const auto& b) { return a.serial < b.serial; });

    const double now = juce::Time::getMillisecondCounterHiRes();
    for (const auto& e : events)
    {
        if (e.serial == 0 || e.serial <= lastSeenOutputSerial)
            continue;

        lastSeenOutputSerial = juce::jmax(lastSeenOutputSerial, e.serial);
        if (e.type == 1)
        {
            noteEvents.push_back({ e.data1, e.data2, now });
            if (noteEvents.size() > 96)
                noteEvents.erase(noteEvents.begin(), noteEvents.begin() + (int) noteEvents.size() - 96);

            hotPc = e.data1 % 12;
            hotPcUntilMs = now + 360.0;
        }
    }
}

void AudienceMidiGeneratorEditor::timerCallback()
{
    captureNewMidiEvents();
    if (debugLogsVisible)
    {
        incomingMonitor.setText(proc.getIncomingMonitorText(), juce::dontSendNotification);
        outgoingMonitor.setText(proc.getOutgoingMonitorText(), juce::dontSendNotification);
    }

    if (++midiOutputRefreshCounter >= 30)
    {
        midiOutputRefreshCounter = 0;
        refreshMidiOutputCombo();
    }

    const double now = juce::Time::getMillisecondCounterHiRes();
    noteEvents.erase(std::remove_if(noteEvents.begin(), noteEvents.end(),
                                    [now] (const auto& e) { return now - e.timeMs > 6500.0; }),
                     noteEvents.end());

    for (auto& t : trails)
    {
        t.alpha *= 0.86f;
        t.radius += 0.6f;
    }
    trails.erase(std::remove_if(trails.begin(), trails.end(), [] (const auto& t) { return t.alpha < 0.04f; }),
                 trails.end());

    repaint();
}

void AudienceMidiGeneratorEditor::resized()
{
    auto area = getLocalBounds();
    if (area.getWidth() > 1480)
        area = area.withSizeKeepingCentre(1480, area.getHeight());
    area.reduce(10, 10);

    titleBar = area.removeFromTop(28);
    header = area.removeFromTop(76);

    if (debugLogsVisible)
    {
        const int logHeight = juce::jlimit(150, 206, area.getHeight() / 3);
        logPanel = area.removeFromBottom(logHeight);
        area.removeFromBottom(8);
    }
    else
    {
        logPanel = {};
    }

    mainShell = area.reduced(0, 8);

    const int leftW = (int) std::round((float) mainShell.getWidth() * 0.59f);
    leftColumn = mainShell.withWidth(leftW);
    rightColumn = mainShell.withTrimmedLeft(leftW);

    auto leftInner = leftColumn.reduced(22, 18);
    const int crowdH = 58;
    const int noteH = juce::jlimit(118, 150, leftInner.getHeight() / 4);
    crowdPanel = leftInner.removeFromBottom(crowdH);
    leftInner.removeFromBottom(12);
    notePulsePanel = leftInner.removeFromBottom(noteH);
    leftInner.removeFromBottom(12);
    venuePanel = leftInner;

    auto rightInner = rightColumn.reduced(22, 18);
    const int rightH = rightInner.getHeight();
    const int gap = rightH < 620 ? 8 : 10;
    const int rangeH = juce::jlimit(68, 92, rightH / 8);
    const int ioH = juce::jlimit(136, 158, rightH / 4);
    int scaleH = rightH - ioH - rangeH - gap * 2;
    scaleH = juce::jlimit(350, 400, scaleH);

    scaleCard = rightInner.removeFromTop(scaleH);
    rightInner.removeFromTop(gap);
    ioCard = rightInner.removeFromTop(juce::jmin(ioH, rightInner.getHeight()));
    rightInner.removeFromTop(gap);
    rangeCard = rightInner.removeFromTop(juce::jmin(rangeH, rightInner.getHeight()));

    if (debugLogsVisible && ! logPanel.isEmpty())
    {
        const int logW = (logPanel.getWidth() - 1) / 2;
        incomingLogPanel = logPanel.withWidth(logW);
        outgoingLogPanel = logPanel.withTrimmedLeft(logW + 1);
    }
    else
    {
        incomingLogPanel = {};
        outgoingLogPanel = {};
    }

    debugToggleButton.setBounds(header.getRight() - 226, header.getY() + 23, 92, 32);
    panicButton.setBounds(header.getRight() - 120, header.getY() + 23, 94, 32);

    const auto scaleInner = scaleCard.reduced(18);
    resetRemapButton.setBounds(scaleInner.getRight() - 96, scaleInner.getY() + 8, 92, 26);
    const int controlsY = scaleInner.getY() + 108;
    if (scaleInner.getWidth() < 520)
    {
        const int half = (scaleInner.getWidth() - 10) / 2;
        scaleCombo.setBounds(scaleInner.getX(), controlsY, half, 28);
        transposeSlider.setBounds(scaleCombo.getRight() + 10, controlsY, half, 28);
        scaleEnableButton.setBounds(scaleInner.getX(), controlsY + 40, 104, 28);
        correctionCombo.setBounds(scaleEnableButton.getRight() + 10, controlsY + 40,
                                  scaleInner.getRight() - scaleEnableButton.getRight() - 10, 28);
    }
    else
    {
        const int controlW = juce::jmax(120, (scaleInner.getWidth() - 24) / 4);
        scaleCombo.setBounds(scaleInner.getX(), controlsY, controlW + 16, 28);
        transposeSlider.setBounds(scaleCombo.getRight() + 10, controlsY, controlW + 4, 28);
        scaleEnableButton.setBounds(transposeSlider.getRight() + 10, controlsY, 78, 28);
        correctionCombo.setBounds(scaleEnableButton.getRight() + 10, controlsY,
                                  juce::jmax(96, scaleInner.getRight() - scaleEnableButton.getRight() - 10), 28);
    }

    auto ioInner = ioCard.reduced(16);
    udpEditor.setBounds(ioInner.getX(), ioInner.getY() + 30, 96, 28);
    applyUdpButton.setBounds(ioInner.getX() + 104, ioInner.getY() + 30, 66, 28);
    channelSlider.setBounds(ioInner.getX() + 196, ioInner.getY() + 30, ioInner.getWidth() - 196, 28);
    midiOutputCombo.setBounds(ioInner.getX(), ioInner.getY() + 82, ioInner.getWidth(), 30);

    auto rangeInner = rangeCard.reduced(16);
    rangeLowSlider.setBounds(rangeInner.getX(), rangeInner.getY() + 30, rangeInner.getWidth(), 24);
    rangeHighSlider.setBounds(rangeInner.getX(), rangeInner.getY() + 30, rangeInner.getWidth(), 24);

    int chipX = crowdPanel.getX() + 14;
    const int chipY = crowdPanel.getY() + 18;
    addButton.setBounds(chipX, chipY, 78, 30); chipX += 86;
    crowdButton.setBounds(chipX, chipY, 104, 30); chipX += 112;
    removeButton.setBounds(chipX, chipY, 86, 30); chipX += 94;
    clearButton.setBounds(chipX, chipY, 70, 30); chipX += 82;
    moveButton.setBounds(chipX, chipY, juce::jmin(126, juce::jmax(96, crowdPanel.getRight() - chipX - 128)), 30);

    if (debugLogsVisible)
    {
        incomingMonitor.setBounds(incomingLogPanel.reduced(20).withTrimmedTop(44));
        outgoingMonitor.setBounds(outgoingLogPanel.reduced(20).withTrimmedTop(44));
    }
    else
    {
        incomingMonitor.setBounds({});
        outgoingMonitor.setBounds({});
    }
}

juce::Point<float> AudienceMidiGeneratorEditor::projectSeat (int row, int col, juce::Rectangle<float> venue) const
{
    const float xNorm = juce::jlimit(0.0f, 1.0f, (float) col / (float) (SeatEventSink::MAX_COLS - 1));
    const float yNorm = juce::jlimit(0.0f, 1.0f, (float) row / (float) (SeatEventSink::MAX_ROWS - 1));

    if (venueShape == VenueShape::Grid)
        return { venue.getX() + 28.0f + xNorm * (venue.getWidth() - 56.0f),
                 venue.getY() + 48.0f + yNorm * (venue.getHeight() - 96.0f) };

    if (venueShape == VenueShape::Scatter)
    {
        const float jitter = std::sin((float) (row * 17 + col * 31)) * 0.035f;
        return { venue.getX() + 28.0f + juce::jlimit(0.0f, 1.0f, xNorm + jitter) * (venue.getWidth() - 56.0f),
                 venue.getY() + 48.0f + juce::jlimit(0.0f, 1.0f, yNorm - jitter) * (venue.getHeight() - 96.0f) };
    }

    const float rowCurve = std::sin((yNorm - 0.5f) * juce::MathConstants<float>::pi);
    const float widthScale = 0.62f + yNorm * 0.32f;
    const float center = venue.getCentreX();
    return { center + (xNorm - 0.5f) * venue.getWidth() * widthScale,
             venue.getY() + 46.0f + yNorm * (venue.getHeight() - 112.0f) - rowCurve * 18.0f };
}

void AudienceMidiGeneratorEditor::paint (juce::Graphics& g)
{
    g.fillAll(bg());
    drawTitleBar(g);
    drawHeader(g);
    drawShell(g);
    drawVenue(g);
    drawNotePulse(g);
    drawCrowdBar(g);
    drawScaleCard(g);
    drawIOCard(g);
    drawRangeCard(g);
    if (debugLogsVisible)
        drawLogs(g);
}

void AudienceMidiGeneratorEditor::drawTitleBar (juce::Graphics& g)
{
    g.setColour(bgDeep());
    g.fillRect(titleBar);
    const int y = titleBar.getCentreY() - 5;
    for (int i = 0; i < 3; ++i)
    {
        g.setColour(i == 0 ? hot() : (i == 1 ? accent() : accent2()));
        g.fillEllipse((float) titleBar.getX() + 14.0f + (float) i * 16.0f, (float) y, 10.0f, 10.0f);
    }
    g.setColour(textMute());
    g.setFont(mono(10.0f));
    g.drawText("AUDIENCE MIDI GENERATOR / LIVE UDP MIDI", titleBar.reduced(66, 0), juce::Justification::centredLeft);
}

void AudienceMidiGeneratorEditor::drawHeader (juce::Graphics& g)
{
    g.setColour(bg());
    g.fillRect(header);
    g.setColour(border());
    g.drawHorizontalLine(header.getBottom() - 1, (float) header.getX(), (float) header.getRight());

    auto mark = juce::Rectangle<int>(header.getX() + 26, header.getY() + 22, 40, 40);
    g.setColour(accent());
    g.fillRoundedRectangle(mark.toFloat(), 9.0f);
    g.setColour(bg().withAlpha(0.38f));
    g.fillRoundedRectangle(mark.reduced(7).toFloat(), 4.0f);

    g.setColour(text());
    g.setFont(sans(24.0f, juce::Font::bold));
    g.drawText("SpektraSynth MIDI Generator", header.getX() + 82, header.getY() + 18, 360, 28, juce::Justification::centredLeft);
    g.setColour(textDim());
    g.setFont(sans(13.0f));
    g.drawText("UDP audience seats -> scale, remap, channel and MIDI output", header.getX() + 82, header.getY() + 48, 460, 18, juce::Justification::centredLeft);

    auto pill = [&] (juce::Rectangle<int> r, juce::String label, juce::Colour dot)
    {
        g.setColour(surface1());
        g.fillRoundedRectangle(r.toFloat(), 999.0f);
        g.setColour(border());
        g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 999.0f, 1.0f);
        g.setColour(dot);
        g.fillEllipse((float) r.getX() + 13.0f, (float) r.getCentreY() - 3.0f, 6.0f, 6.0f);
        g.setColour(textDim());
        g.setFont(mono(11.0f, juce::Font::bold));
        g.drawText(label, r.withTrimmedLeft(28), juce::Justification::centredLeft);
    };

    pill({ header.getRight() - 470, header.getY() + 26, 118, 30 }, "UDP :" + juce::String(proc.udpPort), accent2());
    pill({ header.getRight() - 342, header.getY() + 26, 120, 30 }, juce::String(proc.engine.getRegisteredSeatCount()) + " ACTIVE", accent());
}

void AudienceMidiGeneratorEditor::drawShell (juce::Graphics& g)
{
    drawRoundPanel(g, mainShell, 14.0f);
    g.setColour(border());
    g.drawVerticalLine(leftColumn.getRight(), (float) mainShell.getY(), (float) mainShell.getBottom());
}

void AudienceMidiGeneratorEditor::drawVenue (juce::Graphics& g)
{
    drawEyebrow(g, "Audience Venue", venuePanel.withHeight(18), accent());

    const int tabY = venuePanel.getY();
    const char* tabs[] = { "STADIUM", "GRID", "SCATTER" };
    for (int i = 0; i < 3; ++i)
    {
        venueTabBounds[(size_t) i] = { venuePanel.getRight() - 238 + i * 78, tabY, 72, 24 };
        const bool active = (int) venueShape == i;
        g.setColour(active ? surface2() : juce::Colours::transparentBlack);
        g.fillRoundedRectangle(venueTabBounds[(size_t) i].toFloat(), 6.0f);
        g.setColour(active ? text() : textMute());
        g.setFont(mono(10.0f, juce::Font::bold));
        g.drawText(tabs[i], venueTabBounds[(size_t) i], juce::Justification::centred);
    }

    auto r = venuePanel.withTrimmedTop(30).toFloat();
    g.setGradientFill(juce::ColourGradient(surface1(), r.getX(), r.getY(), bg(), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle(r, 14.0f);
    g.setColour(border());
    g.drawRoundedRectangle(r.reduced(0.5f), 14.0f, 1.0f);

    g.setColour(surface3().withAlpha(0.35f));
    for (int x = (int) r.getX() + 12; x < (int) r.getRight(); x += 24)
        for (int y = (int) r.getY() + 12; y < (int) r.getBottom(); y += 24)
            g.fillEllipse((float) x, (float) y, 1.2f, 1.2f);

    g.setColour(textMute());
    g.setFont(mono(10.0f, juce::Font::bold));
    g.drawText("X -> NOTE  /  Y -> VELOCITY", r.toNearestInt().reduced(16).withHeight(16), juce::Justification::centredLeft);
    g.drawText(juce::String(proc.engine.getRegisteredSeatCount()) + " SEATS", r.toNearestInt().reduced(16).withHeight(16), juce::Justification::centredRight);

    for (int rr = 0; rr < 10; ++rr)
        for (int cc = 0; cc < 18; ++cc)
        {
            const auto p = projectSeat(rr * 2, cc * 5, r);
            g.setColour(bgDeep());
            g.fillRoundedRectangle({ p.x - 5.0f, p.y - 5.0f, 10.0f, 10.0f }, 3.0f);
            g.setColour(border().withAlpha(0.55f));
            g.drawRoundedRectangle({ p.x - 5.0f, p.y - 5.0f, 10.0f, 10.0f }, 3.0f, 1.0f);
        }

    for (int row = 0; row < SeatEventSink::MAX_ROWS; ++row)
        for (int col = 0; col < SeatEventSink::MAX_COLS; ++col)
        {
            if (! proc.engine.isSeatActive(row, col))
                continue;

            const auto p = projectSeat(row, col, r);
            const float vel = proc.engine.getSeatY(row, col);
            g.setColour(oklch(0.65f + vel * 0.18f, 0.18f, 95.0f));
            g.fillRoundedRectangle({ p.x - 6.0f, p.y - 6.0f, 12.0f, 12.0f }, 3.0f);
            g.setColour(accent());
            g.drawRoundedRectangle({ p.x - 6.0f, p.y - 6.0f, 12.0f, 12.0f }, 3.0f, 1.2f);

            const int note = proc.engine.getSeatMidi(row, col);
            if (note >= 0)
            {
                g.setFont(mono(7.0f, juce::Font::bold));
                g.setColour(text());
                g.drawText(midiName(note), juce::Rectangle<float>(p.x - 16.0f, p.y - 19.0f, 32.0f, 10.0f),
                           juce::Justification::centred);
            }
        }

    g.setColour(textMute());
    g.setFont(mono(9.0f, juce::Font::bold));
    g.drawText("STAGE", r.toNearestInt().withTrimmedTop((int) r.getHeight() - 28), juce::Justification::centred);
}

void AudienceMidiGeneratorEditor::drawNotePulse (juce::Graphics& g)
{
    drawRoundPanel(g, notePulsePanel, 14.0f);
    auto head = notePulsePanel.reduced(12).removeFromTop(24);
    g.setColour(text());
    g.setFont(sans(12.0f, juce::Font::bold));
    g.drawText("Note Pulse", head, juce::Justification::centredLeft);
    g.setColour(textMute());
    g.setFont(mono(10.0f, juce::Font::bold));
    g.drawText("LAST 6 S", head, juce::Justification::centredRight);

    auto r = notePulsePanel.reduced(12).withTrimmedTop(30).toFloat();
    g.setColour(bgDeep());
    g.fillRoundedRectangle(r, 8.0f);

    const int low = (getIntParam("rangeLowOctave") + 1) * 12;
    const int high = (getIntParam("rangeHighOctave") + 1) * 12 + 11;
    const int span = juce::jmax(1, high - low);
    for (int midi = low; midi <= high; ++midi)
    {
        const float y = r.getBottom() - (float) (midi - low) / (float) span * r.getHeight();
        if (midi % 12 == getIntParam("root"))
        {
            g.setColour(accent().withAlpha(0.10f));
            g.fillRect(r.getX(), y - 2.0f, r.getWidth(), 4.0f);
        }
        else if (isPitchClassInScale(midi % 12))
        {
            g.setColour(surface2().withAlpha(0.35f));
            g.drawHorizontalLine((int) y, r.getX(), r.getRight());
        }
    }

    const double now = juce::Time::getMillisecondCounterHiRes();
    for (const auto& e : noteEvents)
    {
        const double age = now - e.timeMs;
        if (age < 0.0 || age > 6000.0)
            continue;

        const float x = r.getRight() - (float) (age / 6000.0) * r.getWidth();
        const float y = r.getBottom() - (float) (e.midi - low) / (float) span * r.getHeight();
        const float alpha = 1.0f - (float) (age / 6000.0) * 0.65f;
        const float rad = age < 220.0 ? 7.0f : 4.0f;
        g.setColour(accent().withAlpha(alpha));
        g.fillEllipse(x - rad, y - rad, rad * 2.0f, rad * 2.0f);
    }

    g.setGradientFill(juce::ColourGradient(accent().withAlpha(0.0f), r.getRight() - 28.0f, r.getY(),
                                           accent().withAlpha(0.18f), r.getRight(), r.getY(), false));
    g.fillRect(r.getRight() - 28.0f, r.getY(), 28.0f, r.getHeight());
}

void AudienceMidiGeneratorEditor::drawCrowdBar (juce::Graphics& g)
{
    drawRoundPanel(g, crowdPanel, 14.0f);
    g.setFont(mono(28.0f, juce::Font::bold));
    g.setColour(text());
    g.drawText(juce::String(proc.engine.getRegisteredSeatCount()), crowdPanel.getRight() - 130, crowdPanel.getY() + 13, 72, 34,
               juce::Justification::centredRight);
    g.setFont(mono(10.0f, juce::Font::bold));
    g.setColour(textMute());
    g.drawText("SEATS", crowdPanel.getRight() - 54, crowdPanel.getY() + 27, 44, 14, juce::Justification::centredLeft);
}

void AudienceMidiGeneratorEditor::drawScaleCard (juce::Graphics& g)
{
    drawRoundPanel(g, scaleCard, 14.0f);
    auto r = scaleCard.reduced(18);
    drawEyebrow(g, "Scale", r.withHeight(14), accent());
    g.setColour(text());
    g.setFont(sans(22.0f, juce::Font::bold));
    const int root = getIntParam("root");
    const auto scaleName = MidiScaleModule::scaleNames()[juce::jlimit(0, MidiScaleModule::numScaleTypes - 1, getIntParam("scaleMode"))];
    g.drawText(juce::String(noteNames[root]) + " " + scaleName, r.withTrimmedTop(18).withHeight(28), juce::Justification::centredLeft);

    auto piano = juce::Rectangle<int>(r.getX(), r.getY() + 52, r.getWidth(), 44);
    g.setColour(bgDeep());
    g.fillRoundedRectangle(piano.toFloat(), 6.0f);
    const int whiteIdx[] = { 0, 2, 4, 5, 7, 9, 11 };
    const float whiteW = (float) piano.getWidth() / 7.0f;
    for (int i = 0; i < 7; ++i)
    {
        const int pc = whiteIdx[i];
        auto key = juce::Rectangle<float>((float) piano.getX() + (float) i * whiteW + 2.0f,
                                          (float) piano.getY() + 4.0f,
                                          whiteW - 4.0f, (float) piano.getHeight() - 8.0f);
        rootKeyBounds[(size_t) pc] = key.toNearestInt();
        g.setColour(root == pc ? accent() : surface2());
        g.fillRoundedRectangle(key, 4.0f);
        g.setColour(root == pc ? darkOnAccent() : textDim());
        g.setFont(mono(9.0f, juce::Font::bold));
        g.drawText(noteNames[pc], key.toNearestInt().withTrimmedTop((int) key.getHeight() - 16),
                   juce::Justification::centred);
    }
    const std::array<std::pair<int, float>, 5> blackKeys { { {1, 0.7f}, {3, 1.7f}, {6, 3.7f}, {8, 4.7f}, {10, 5.7f} } };
    for (auto [pc, pos] : blackKeys)
    {
        auto key = juce::Rectangle<float>((float) piano.getX() + pos * whiteW,
                                          (float) piano.getY() + 4.0f,
                                          whiteW * 0.56f, (float) piano.getHeight() * 0.58f);
        rootKeyBounds[(size_t) pc] = key.toNearestInt();
        g.setColour(root == pc ? accent() : bg());
        g.fillRoundedRectangle(key, 4.0f);
        g.setColour(root == pc ? darkOnAccent() : textDim());
        g.setFont(mono(8.0f, juce::Font::bold));
        g.drawText(noteNames[pc], key.toNearestInt(), juce::Justification::centred);
    }

    drawEyebrow(g, "Preset", { scaleCombo.getX(), scaleCombo.getY() - 17, 90, 12 });
    drawEyebrow(g, "Transpose", { transposeSlider.getX(), transposeSlider.getY() - 17, 100, 12 });
    drawEyebrow(g, "Fold", { scaleEnableButton.getX(), scaleEnableButton.getY() - 17, 80, 12 });
    drawEyebrow(g, "Correction", { correctionCombo.getX(), correctionCombo.getY() - 17, 120, 12 });

    const int stripHeight = scaleCard.getHeight() < 380 ? 42 : 48;
    const int controlsBottom = juce::jmax(juce::jmax(scaleCombo.getBottom(), transposeSlider.getBottom()),
                                          juce::jmax(scaleEnableButton.getBottom(), correctionCombo.getBottom()));
    auto strip = juce::Rectangle<int>(r.getX(), controlsBottom + 18, r.getWidth(), stripHeight);
    const int gap = 4;
    const int padW = (strip.getWidth() - gap * 11) / 12;
    for (int pc = 0; pc < 12; ++pc)
    {
        auto pad = juce::Rectangle<int>(strip.getX() + pc * (padW + gap), strip.getY(), padW, strip.getHeight());
        pitchPadBounds[(size_t) pc] = pad;
        const bool inScale = isPitchClassInScale(pc);
        const int rel = (pc - root + 12) % 12;
        g.setColour(inScale ? accent() : bgDeep());
        g.fillRoundedRectangle(pad.toFloat(), 6.0f);
        g.setColour(inScale ? juce::Colours::transparentBlack : border());
        g.drawRoundedRectangle(pad.toFloat().reduced(0.5f), 6.0f, 1.0f);
        g.setColour(inScale ? darkOnAccent() : textMute());
        g.setFont(mono(10.0f, juce::Font::bold));
        g.drawText(noteNames[pc], pad.withTrimmedBottom(24), juce::Justification::centred);
        g.setFont(mono(9.0f));
        g.drawText(inScale ? degrees[rel] : juce::String("."), pad.withTrimmedTop(23), juce::Justification::centred);
        if (pc == root)
        {
            g.setColour(inScale ? darkOnAccent() : accent());
            g.fillEllipse((float) pad.getCentreX() - 2.0f, (float) pad.getBottom() - 7.0f, 4.0f, 4.0f);
        }
    }

    auto matrix = juce::Rectangle<int>(r.getX(), strip.getBottom() + 14, r.getWidth(),
                                       juce::jmax(66, r.getBottom() - strip.getBottom() - 14));
    g.setColour(bgDeep());
    g.fillRoundedRectangle(matrix.toFloat(), 10.0f);
    g.setColour(border());
    g.drawRoundedRectangle(matrix.toFloat().reduced(0.5f), 10.0f, 1.0f);
    drawEyebrow(g, "OUT", matrix.withWidth(18).withTrimmedTop(48));
    const int cell = juce::jmax(6, juce::jmin(20, juce::jmin((matrix.getWidth() - 56) / 12,
                                                             (matrix.getHeight() - 26) / 12)));
    const int size = juce::jmax(5, cell - 4);
    const int gridX = matrix.getX() + 34;
    const int gridY = matrix.getY() + 8;
    for (int input = 0; input < 12; ++input)
    {
        const int mapped = getIntParam("midiScaleRemap" + juce::String(input));
        for (int out = 0; out < 12; ++out)
        {
            const int row = 11 - out;
            auto c = juce::Rectangle<int>(gridX + input * cell, gridY + row * cell, size, size).expanded(3);
            remapCellBounds[(size_t) out][(size_t) input] = c;
            const bool selected = mapped == out;
            const bool diag = input == out;
            const bool hotCol = (hotPc == input && juce::Time::getMillisecondCounterHiRes() < hotPcUntilMs) || hoveredRemapColumn == input;
            g.setColour(selected ? (hotCol ? oklch(0.92f, 0.20f, 95.0f) : accent())
                                  : (diag ? surface3() : surface1()));
            g.fillRoundedRectangle(c.toFloat().reduced(2.0f), 3.0f);
            if (hotCol)
            {
                g.setColour(accent().withAlpha(0.45f));
                g.drawRect(gridX + input * cell, gridY, size + 6, cell * 12, 1);
            }
        }
        g.setColour((hotPc == input && juce::Time::getMillisecondCounterHiRes() < hotPcUntilMs) ? accent() : textMute());
        g.setFont(mono(8.0f, juce::Font::bold));
        g.drawText(noteNames[input], gridX + input * cell - 3, gridY + cell * 12 + 4, cell + 2, 12,
                   juce::Justification::centred);
    }
    drawEyebrow(g, "IN", { gridX + cell * 12 - 26, gridY + cell * 12 + 20, 28, 12 });
}

void AudienceMidiGeneratorEditor::drawIOCard (juce::Graphics& g)
{
    drawRoundPanel(g, ioCard, 14.0f);
    auto r = ioCard.reduced(16);
    drawEyebrow(g, "I/O", r.withHeight(14), accent());
    drawEyebrow(g, "UDP Port", { udpEditor.getX(), udpEditor.getY() - 17, 90, 12 });
    drawEyebrow(g, "MIDI Channel", { channelSlider.getX(), channelSlider.getY() - 17, 120, 12 });
    drawEyebrow(g, "MIDI Output", { midiOutputCombo.getX(), midiOutputCombo.getY() - 17, 120, 12 });

    auto footer = r.withTop(midiOutputCombo.getBottom() + 8);
    if (footer.getHeight() >= 18)
    {
        g.setColour(border().withAlpha(0.75f));
        g.drawHorizontalLine(footer.getY(), (float) footer.getX(), (float) footer.getRight());
        g.setFont(mono(10.0f));
        g.setColour(accent2());
        g.drawText("* UDP " + juce::String(proc.udpPort) + " listening",
                   footer.withHeight(16).translated(0, 5), juce::Justification::centredLeft);

        if (footer.getHeight() >= 34)
            g.drawText("* " + proc.getMidiOutputStatus(),
                       footer.withHeight(16).translated(0, 21), juce::Justification::centredLeft);
    }
}

void AudienceMidiGeneratorEditor::drawRangeCard (juce::Graphics& g)
{
    if (rangeCard.isEmpty())
        return;

    drawRoundPanel(g, rangeCard, 14.0f);
    auto r = rangeCard.reduced(16);
    const int low = getIntParam("rangeLowOctave");
    const int high = getIntParam("rangeHighOctave");
    drawEyebrow(g, "Scaler Range", r.withHeight(14), accent());
    g.setColour(accent());
    g.setFont(mono(12.0f, juce::Font::bold));
    g.drawText(juce::String(low) + " -> " + juce::String(high) + " oct", r.withHeight(16), juce::Justification::centredRight);

    if (rangeCard.getHeight() < 86)
        return;

    auto ticks = r.withTop(rangeLowSlider.getBottom() + 8).withHeight(16);
    for (int i = 0; i <= 8; ++i)
    {
        const int x = ticks.getX() + juce::roundToInt((float) i / 8.0f * (float) ticks.getWidth());
        g.setColour(i >= low && i <= high ? text() : textMute());
        g.setFont(mono(10.0f));
        g.drawText(juce::String(i), x - 6, ticks.getY(), 12, 14, juce::Justification::centred);
    }
}

void AudienceMidiGeneratorEditor::drawLogs (juce::Graphics& g)
{
    if (logPanel.isEmpty())
        return;

    g.setColour(border());
    g.fillRect(logPanel);
    g.setColour(bg());
    g.fillRect(incomingLogPanel);
    g.fillRect(outgoingLogPanel);

    drawEyebrow(g, "Incoming UDP", incomingLogPanel.reduced(20).withHeight(18), accent2());
    drawEyebrow(g, "Outgoing MIDI", outgoingLogPanel.reduced(20).withHeight(18), accent());
}

void AudienceMidiGeneratorEditor::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < 3; ++i)
        if (venueTabBounds[(size_t) i].contains(e.getPosition()))
        {
            venueShape = (VenueShape) i;
            repaint();
            return;
        }

    for (int pc = 0; pc < 12; ++pc)
        if (rootKeyBounds[(size_t) pc].contains(e.getPosition()))
        {
            setIntParam("root", pc);
            return;
        }

    for (int pc = 0; pc < 12; ++pc)
        if (pitchPadBounds[(size_t) pc].contains(e.getPosition()))
        {
            const int root = getIntParam("root");
            const int rel = (pc - root + 12) % 12;
            int mask = currentScaleMask();
            const int bit = 1 << rel;
            if ((mask & bit) != 0 && (mask & ~bit) != 0)
                mask &= ~bit;
            else
                mask |= bit;
            setCustomMask(mask);
            return;
        }

    for (int out = 0; out < 12; ++out)
        for (int input = 0; input < 12; ++input)
            if (remapCellBounds[(size_t) out][(size_t) input].contains(e.getPosition()))
            {
                setRemap(input, out);
                return;
            }
}

void AudienceMidiGeneratorEditor::mouseMove (const juce::MouseEvent& e)
{
    int newHover = -1;
    for (int input = 0; input < 12; ++input)
        for (int out = 0; out < 12; ++out)
            if (remapCellBounds[(size_t) out][(size_t) input].contains(e.getPosition()))
                newHover = input;

    if (newHover != hoveredRemapColumn)
    {
        hoveredRemapColumn = newHover;
        repaint(scaleCard);
    }
}

void AudienceMidiGeneratorEditor::mouseExit (const juce::MouseEvent&)
{
    hoveredRemapColumn = -1;
    repaint(scaleCard);
}
