#include "LibraryRail.h"

namespace
{
    const juce::Colour kBg          { 0xff06090d };
    const juce::Colour kPanel       { 0xff10151c };
    const juce::Colour kRowHover    { 0xff18202a };
    const juce::Colour kRowSelected { 0xff112a2d };
    const juce::Colour kHairline    { 0xff222a33 };
    const juce::Colour kText        { 0xfff1f2f6 };
    const juce::Colour kText2       { 0xffb8bac6 };
    const juce::Colour kText3       { 0xff7c7d8a };
    const juce::Colour kAccent      { 0xff5fd7d0 };

    // Element->scaleMode mapping anchor.
    //
    // The "scaleMode" choice parameter lists the musical scales first, then one
    // "<Element> Spectrum" entry per spectral element, in element order. So
    // selecting spectral element N must select scaleMode (firstSpectrumIndex + N),
    // where firstSpectrumIndex is the position of the first (Hydrogen) spectrum.
    //
    // The authoritative way to find that index is to look up the anchor entry by
    // name (kSpectralScaleAnchorName) - that stays correct even if scales are
    // reordered or inserted. kBaseMusicalScaleCount is the documented expected
    // value (number of non-spectral scales that precede the spectra) and is used
    // only as a fallback if the parameter is missing or not a choice parameter,
    // replacing what used to be a bare, unexplained literal 7.
    const juce::String kSpectralScaleAnchorName { "Hydrogen Spectrum" };
    constexpr int      kBaseMusicalScaleCount = 7;
}

LibraryRail::LibraryRail (AudienceProcessor& p) : proc(p)
{
    listbox.setModel(this);
    listbox.setRowHeight(44);
    listbox.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    listbox.setColour(juce::ListBox::outlineColourId,    juce::Colours::transparentBlack);
    addAndMakeVisible(listbox);

    auto styleTab = [] (juce::TextButton& b)
    {
        b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff10151c));
        b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff18242a));
        b.setColour(juce::TextButton::textColourOffId, kText3);
        b.setColour(juce::TextButton::textColourOnId, kText);
    };

    styleTab(samplesTabBtn);
    styleTab(elementsTabBtn);
    samplesTabBtn.setClickingTogglesState(false);
    elementsTabBtn.setClickingTogglesState(false);
    samplesTabBtn.onClick = [this]()
    {
        railMode = RailMode::Samples;
        searchBox.setTextToShowWhenEmpty("Search libraries...", kText3);
        refresh();
    };
    elementsTabBtn.onClick = [this]()
    {
        railMode = RailMode::Elements;
        searchBox.setTextToShowWhenEmpty("Search elements...", kText3);
        refresh();
    };
    addAndMakeVisible(samplesTabBtn);
    addAndMakeVisible(elementsTabBtn);

    searchBox.setTextToShowWhenEmpty("Search libraries...", kText3);
    searchBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff05080c));
    searchBox.setColour(juce::TextEditor::textColourId,       kText);
    searchBox.setColour(juce::TextEditor::outlineColourId,    kHairline);
    searchBox.setColour(juce::TextEditor::focusedOutlineColourId, kAccent);
    searchBox.onTextChange = [this]() { refresh(); };
    addAndMakeVisible(searchBox);

    rescanBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff20283a));
    rescanBtn.setColour(juce::TextButton::textColourOffId, kAccent);
    rescanBtn.onClick = [this]()
    {
        proc.rescanLibraryRoot();
        refresh();
        repaint();
    };
    addAndMakeVisible(rescanBtn);

    samplePreview.setMultiLine(true, false);
    samplePreview.setReadOnly(true);
    samplePreview.setScrollbarsShown(true);
    samplePreview.setCaretVisible(false);
    samplePreview.setColour(juce::TextEditor::backgroundColourId,     juce::Colours::transparentBlack);
    samplePreview.setColour(juce::TextEditor::outlineColourId,        juce::Colours::transparentBlack);
    samplePreview.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    samplePreview.setColour(juce::TextEditor::textColourId,           kText3);
    samplePreview.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                                         10.5f, juce::Font::plain)));
    addAndMakeVisible(samplePreview);

    refresh();
}

LibraryRail::~LibraryRail() = default;

void LibraryRail::refresh()
{
    const auto query = searchBox.getText().trim().toLowerCase();
    libraryNames.clear();
    sampleCounts.clear();
    elementNames.clear();
    elementIndices.clear();

    if (railMode == RailMode::Samples)
    {
        const auto allLibraries = proc.getAvailableLibraries();
        for (const auto& name : allLibraries)
        {
            if (query.isNotEmpty() && ! name.toLowerCase().contains(query))
                continue;

            libraryNames.add(name);
            sampleCounts.add(countSamplesForLibrary(name));
        }

        selectedRow = libraryNames.indexOf(proc.currentLibraryName);
    }
    else if (auto* elements = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter("spectralElement")))
    {
        const int selectedElement = (int) proc.apvts.getRawParameterValue("spectralElement")->load();
        for (int i = 0; i < elements->choices.size(); ++i)
        {
            const auto name = elements->choices[i];
            if (query.isNotEmpty() && ! name.toLowerCase().contains(query))
                continue;

            elementNames.add(name);
            elementIndices.add(i);
        }

        selectedRow = elementIndices.indexOf(selectedElement);
    }

    listbox.updateContent();
    if (selectedRow >= 0)
        listbox.selectRow(selectedRow, true, true);

    juce::String preview;
    if (railMode == RailMode::Samples)
    {
        const auto& lib = proc.engine.getLibrary();
        const int   n   = lib.numSamples();
        for (int i = 0; i < n; ++i)
        {
            if (auto* s = lib.getSample(i))
                preview << s->rootNote << "   " << s->displayName << "\n";
        }
    }
    else
    {
        preview << proc.engine.getSpectralElementName() << " Spectrum\n";
        preview << "root " << juce::String(proc.engine.getSpectralElementRootWavelengthNm(), 3) << " nm\n";
        preview << proc.engine.getScaleRangeName() << "\n";
        preview << "raw lines " << juce::String(proc.engine.getSpectralElementLineCount()) << "\n";
    }

    samplePreview.setText(preview, juce::dontSendNotification);
    samplesTabBtn .setToggleState(railMode == RailMode::Samples, juce::dontSendNotification);
    elementsTabBtn.setToggleState(railMode == RailMode::Elements, juce::dontSendNotification);
    rescanBtn.setVisible(railMode == RailMode::Samples);
    resized();
}

int LibraryRail::getNumRows()
{
    return railMode == RailMode::Samples ? libraryNames.size() : elementNames.size();
}

int LibraryRail::countSamplesForLibrary (const juce::String& name) const
{
    const auto dir = proc.libraryRoot.getChildFile(name);
    int count = 0;
    if (dir.isDirectory())
    {
        juce::Array<juce::File> files;
        dir.findChildFiles(files, juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac");
        for (auto& f : files)
            if (! f.getFileName().startsWith("._")) ++count;
    }
    return count;
}

void LibraryRail::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (railMode == RailMode::Elements
        && row >= 0
        && row < elementIndices.size())
        selected = elementIndices[row] == (int) proc.apvts.getRawParameterValue("spectralElement")->load();

    auto bounds = juce::Rectangle<float>(2.0f, 2.0f, (float) w - 4.0f, (float) h - 4.0f);
    g.setColour(selected ? kRowSelected : juce::Colours::transparentBlack);
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(selected ? kAccent.withAlpha(0.6f) : kHairline);
    g.drawRoundedRectangle(bounds, 4.0f, selected ? 1.0f : 0.5f);

    const auto rowCount = railMode == RailMode::Samples ? libraryNames.size() : elementNames.size();
    if (row < 0 || row >= rowCount) return;
    const auto name = railMode == RailMode::Samples ? libraryNames[row] : elementNames[row];

    g.setColour(selected ? kText : kText2);
    g.setFont(juce::Font(juce::FontOptions(11.5f)));
    g.drawText(name, (int) bounds.getX() + 14, (int) bounds.getY() + 4,
               w - 28, 14, juce::Justification::centredLeft);

    const int count = (row >= 0 && row < sampleCounts.size()) ? sampleCounts[row] : 0;
    const auto subtitle = railMode == RailMode::Samples
        ? (juce::String(count) + " samples")
        : (selected
            ? ("root " + juce::String(proc.engine.getSpectralElementRootWavelengthNm(), 3) + " nm")
            : juce::String("element spectrum"));

    g.setColour(kText3);
    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                            10.0f, juce::Font::plain)));
    g.drawText(subtitle,
               (int) bounds.getX() + 14, (int) bounds.getY() + 19,
               w - 28, 14, juce::Justification::centredLeft);
}

void LibraryRail::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (railMode == RailMode::Samples)
    {
        if (row < 0 || row >= libraryNames.size()) return;
        setChoiceParameter("engineSource", 0);
        proc.setCurrentLibrary(libraryNames[row]);
    }
    else
    {
        if (row < 0 || row >= elementIndices.size()) return;
        const int element = elementIndices[row];
        setChoiceParameter("engineSource", 1);
        setChoiceParameter("spectralElement", element);
        setChoiceParameter("scaleMode", spectralScaleStartIndex() + element);
    }

    selectedRow = row;
    refresh();
}

void LibraryRail::setChoiceParameter (const juce::String& parameterId, int choiceIndex)
{
    if (auto* param = proc.apvts.getParameter(parameterId))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1((float) choiceIndex));
        param->endChangeGesture();
    }
}

int LibraryRail::spectralScaleStartIndex() const
{
    if (auto* scales = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter("scaleMode")))
    {
        const int anchor = scales->choices.indexOf(kSpectralScaleAnchorName);

        // If the anchor entry is missing the list is malformed; fall back to the
        // documented base-scale count rather than silently mapping every element
        // to index 0 (which would mis-select the first musical scale).
        if (anchor < 0)
        {
            jassertfalse;
            return kBaseMusicalScaleCount;
        }

        // The spectra are expected to start exactly after the base musical scales.
        // A mismatch means the scale list and this UI mapping have drifted apart;
        // flag it in debug builds while still honouring the live list at runtime.
        jassert (anchor == kBaseMusicalScaleCount);
        return anchor;
    }

    return kBaseMusicalScaleCount;
}

void LibraryRail::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    juce::ColourGradient bg(kPanel,  r.getCentre().translated(0, -200),
                            kBg,     r.getCentre().translated(0, 200), false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(kHairline);
    g.drawRoundedRectangle(r, 8.0f, 1.0f);

    g.setColour(kText3);
    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                            10.0f, juce::Font::plain)));
    g.drawText("LIBRARY", 12, 10, 120, 14, juce::Justification::left);

    g.setColour(kText);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).boldened());
    g.drawText(railMode == RailMode::Samples ? "Sample Libraries" : "Element Spectra",
               12, 28, 150, 18, juce::Justification::left);

    g.setColour(kHairline);
    g.drawLine(12.0f, 108.0f, (float) getWidth() - 12.0f, 108.0f, 1.0f);

    // active library banner near the bottom of the rail
    const int yBanner = getHeight() - 130;
    g.setColour(kText3);
    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                            10.0f, juce::Font::plain)));
    g.drawText("ACTIVE PATCH", 12, yBanner, 160, 14, juce::Justification::left);

    g.setColour(kText);
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawText(proc.currentLibraryName.isEmpty() ? juce::String("(none)") : proc.currentLibraryName,
               12, yBanner + 16, getWidth() - 24, 18, juce::Justification::left);

    g.setColour(kText3);
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText(proc.librariesStatus, 12, yBanner + 36, getWidth() - 24, 16,
               juce::Justification::left);

    g.setColour(kHairline);
    g.drawLine(12.0f, (float)(yBanner + 56), (float) getWidth() - 12.0f, (float)(yBanner + 56), 1.0f);
}

void LibraryRail::resized()
{
    const int tabW = (getWidth() - 28) / 2;
    samplesTabBtn.setBounds(12, 50, tabW, 22);
    elementsTabBtn.setBounds(16 + tabW, 50, getWidth() - 28 - tabW, 22);

    searchBox.setBounds(12, 78, railMode == RailMode::Samples ? getWidth() - 78 : getWidth() - 24, 24);
    rescanBtn.setBounds(getWidth() - 60, 78, 48, 24);
    listbox.setBounds(8, 114, getWidth() - 16, getHeight() - 258);
    samplePreview.setBounds(12, getHeight() - 66, getWidth() - 24, 54);
}
