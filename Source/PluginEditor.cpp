#include "PluginEditor.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace cm
{
    const juce::Colour background   { 0xff070a0f };
    const juce::Colour header       { 0xff0a0f16 };
    const juce::Colour card         { 0xff0e141c };
    const juce::Colour cardRaised   { 0xff151d27 };
    const juce::Colour line         { 0xff26313d };
    const juce::Colour lineSoft     { 0xff1a232d };
    const juce::Colour text         { 0xffedf3f8 };
    const juce::Colour textMuted    { 0xff9eacba };
    const juce::Colour textDim      { 0xff697786 };
    const juce::Colour cyan         { 0xff58d6ff };
    const juce::Colour green        { 0xff42d6a5 };
    const juce::Colour violet       { 0xff9b8cff };
    const juce::Colour amber        { 0xffefbd5c };
    const juce::Colour red          { 0xffff647a };

    static juce::Colour channelColour (int channel) noexcept
    {
        const auto position = (float) juce::jlimit (0, 15, channel - 1) / 15.0f;
        return cyan.interpolatedWith (violet, position);
    }

    static void drawCard (juce::Graphics& g, juce::Rectangle<int> bounds,
                          const juce::String& title, const juce::String& tag = {})
    {
        if (bounds.isEmpty())
            return;

        auto b = bounds.toFloat();
        juce::ColourGradient surface (cardRaised.withAlpha (0.50f), b.getX(), b.getY(),
                                      card, b.getX(), b.getBottom(), false);
        g.setGradientFill (surface);
        g.fillRoundedRectangle (b, 11.0f);
        g.setColour (line);
        g.drawRoundedRectangle (b.reduced (0.5f), 11.0f, 1.0f);

        g.setColour (lineSoft);
        g.fillRect (bounds.reduced (14, 0).withY (bounds.getY() + 34).withHeight (1));

        g.setColour (textMuted.withAlpha (0.82f));
        g.setFont (juce::Font (juce::FontOptions (10.8f).withStyle ("bold")));
        g.drawText (title, bounds.reduced (15, 0).removeFromTop (35),
                    juce::Justification::centredLeft, false);

        if (tag.isNotEmpty())
        {
            auto tagBounds = bounds.reduced (15, 0).removeFromTop (35).removeFromRight (140);
            g.setColour (cyan.withAlpha (0.78f));
            g.setFont (juce::Font (juce::FontOptions (9.0f).withStyle ("bold")));
            g.drawText (tag, tagBounds, juce::Justification::centredRight, false);
        }
    }

    class LookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel()
        {
            setColour (juce::PopupMenu::backgroundColourId, cardRaised);
            setColour (juce::PopupMenu::textColourId, text);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, cyan.withAlpha (0.16f));
            setColour (juce::PopupMenu::highlightedTextColourId, text);
        }

        juce::Font getComboBoxFont (juce::ComboBox&) override
        {
            return juce::Font (juce::FontOptions (11.5f));
        }

        void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
        {
            // Keep compact Atomic ROOT/OCTAVE fields readable. JUCE's default
            // 34 px text reservation left only ~10 px at minimum width.
            label.setBounds (8, 1, juce::jmax (10, box.getWidth() - 26), box.getHeight() - 2);
            label.setFont (getComboBoxFont (box));
        }

        void drawComboBox (juce::Graphics& g, int width, int height,
                           bool isButtonDown, int buttonX, int buttonY,
                           int buttonW, int buttonH, juce::ComboBox& box) override
        {
            auto bounds = juce::Rectangle<float> (0.5f, 0.5f,
                                                  (float) width - 1.0f,
                                                  (float) height - 1.0f);
            const auto focused = box.hasKeyboardFocus (true);
            const auto enabledAlpha = box.isEnabled() ? 1.0f : 0.34f;
            const auto outline = focused || isButtonDown
                                   ? box.findColour (juce::ComboBox::focusedOutlineColourId)
                                   : box.findColour (juce::ComboBox::outlineColourId);

            g.setColour (box.findColour (juce::ComboBox::backgroundColourId)
                             .withMultipliedAlpha (enabledAlpha));
            g.fillRoundedRectangle (bounds, 6.0f);
            g.setColour (outline.withAlpha ((focused ? 0.95f : 0.78f) * enabledAlpha));
            g.drawRoundedRectangle (bounds, 6.0f, focused ? 1.2f : 0.8f);

            const auto centreX = (float) buttonX + (float) buttonW * 0.5f;
            const auto centreY = (float) buttonY + (float) buttonH * 0.5f;
            juce::Path chevron;
            chevron.startNewSubPath (centreX - 4.0f, centreY - 1.5f);
            chevron.lineTo (centreX, centreY + 2.5f);
            chevron.lineTo (centreX + 4.0f, centreY - 1.5f);
            g.setColour (box.findColour (juce::ComboBox::arrowColourId)
                             .withAlpha (0.88f * enabledAlpha));
            g.strokePath (chevron, juce::PathStrokeType (1.5f,
                                                        juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
        }

        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
        {
            return juce::Font (juce::FontOptions (juce::jlimit (10.5f, 12.0f,
                                                                (float) buttonHeight * 0.40f))
                                    .withStyle ("bold"));
        }

        void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                   const juce::Colour& backgroundColour,
                                   bool highlighted, bool down) override
        {
            auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
            const auto radius = button.getComponentID() == "governorPill"
                                  ? bounds.getHeight() * 0.5f : 6.0f;
            const auto accent = button.findColour (juce::TextButton::buttonOnColourId).withAlpha (1.0f);
            const auto enabledAlpha = button.isEnabled() ? 1.0f : 0.34f;
            auto fill = backgroundColour.withMultipliedAlpha (enabledAlpha);
            if (down)
                fill = fill.interpolatedWith (accent, 0.20f);
            else if (highlighted)
                fill = fill.interpolatedWith (accent, 0.09f);

            g.setColour (fill);
            g.fillRoundedRectangle (bounds, radius);
            g.setColour ((highlighted || button.hasKeyboardFocus (true))
                           ? accent.withAlpha (0.72f * enabledAlpha)
                           : line.withAlpha (0.88f * enabledAlpha));
            g.drawRoundedRectangle (bounds, radius,
                                    button.hasKeyboardFocus (true) ? 1.2f : 0.8f);
        }

        void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                               bool highlighted, bool) override
        {
            const auto boxSize = 14.0f;
            const auto box = juce::Rectangle<float> (2.0f,
                                                     ((float) button.getHeight() - boxSize) * 0.5f,
                                                     boxSize, boxSize);
            const auto accent = button.findColour (juce::ToggleButton::tickColourId);

            g.setColour (button.getToggleState() ? accent : cardRaised);
            g.fillRoundedRectangle (box, 4.0f);
            g.setColour ((highlighted || button.hasKeyboardFocus (true))
                           ? accent.withAlpha (0.90f) : line);
            g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.0f);

            if (button.getToggleState())
            {
                juce::Path tick;
                tick.startNewSubPath (box.getX() + 3.5f, box.getCentreY());
                tick.lineTo (box.getX() + 6.0f, box.getBottom() - 3.5f);
                tick.lineTo (box.getRight() - 3.0f, box.getY() + 3.5f);
                g.setColour (background);
                g.strokePath (tick, juce::PathStrokeType (1.7f,
                                                          juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));
            }

            g.setColour (button.findColour (juce::ToggleButton::textColourId));
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawFittedText (button.getButtonText(), 24, 0,
                              button.getWidth() - 24, button.getHeight(),
                              juce::Justification::centredLeft, 1);
        }

        void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                               float sliderPos, float minSliderPos, float maxSliderPos,
                               juce::Slider::SliderStyle style, juce::Slider& slider) override
        {
            if (style != juce::Slider::LinearHorizontal)
            {
                juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                                        minSliderPos, maxSliderPos, style, slider);
                return;
            }

            const auto centreY = (float) y + (float) height * 0.5f;
            auto track = juce::Rectangle<float> ((float) x, centreY - 2.0f,
                                                 (float) width, 4.0f);
            const auto enabledAlpha = slider.isEnabled() ? 1.0f : 0.30f;
            g.setColour (slider.findColour (juce::Slider::backgroundColourId)
                               .withMultipliedAlpha (enabledAlpha));
            g.fillRoundedRectangle (track, 2.0f);

            auto fill = track.withWidth (juce::jmax (0.0f, sliderPos - track.getX()));
            g.setColour (slider.findColour (juce::Slider::trackColourId)
                               .withMultipliedAlpha (enabledAlpha));
            g.fillRoundedRectangle (fill, 2.0f);

            g.setColour (slider.findColour (juce::Slider::thumbColourId)
                               .withMultipliedAlpha (enabledAlpha));
            g.fillEllipse (sliderPos - 5.0f, centreY - 5.0f, 10.0f, 10.0f);
            g.setColour (cyan.withAlpha (0.85f * enabledAlpha));
            g.drawEllipse (sliderPos - 5.0f, centreY - 5.0f, 10.0f, 10.0f, 1.0f);
        }
    };

    static int choiceValue (juce::AudioProcessorValueTreeState& state,
                            const juce::String& parameterId) noexcept
    {
        if (auto* value = state.getRawParameterValue (parameterId))
            return (int) std::lround (value->load (std::memory_order_relaxed));
        return 0;
    }

    static juce::String zonesFromMask (juce::uint32 mask)
    {
        if (mask == 0)
            return "Waiting for zone data";

        juce::StringArray zones;
        for (int i = 0; i < 26; ++i)
            if ((mask & (1u << (juce::uint32) i)) != 0)
                zones.add (juce::String::charToString ((juce::juce_wchar) ('A' + i)));

        if (zones.size() > 1)
            return "MIXED ZONES  /  " + zones.joinIntoString (" + ");

        return "ZONE " + zones[0];
    }

    static int zoneCount (juce::uint32 mask) noexcept
    {
        int count = 0;
        for (int i = 0; i < 26; ++i)
            if ((mask & (1u << (juce::uint32) i)) != 0)
                ++count;
        return count;
    }

    static juce::String compactCount (int value)
    {
        if (value < 1000)
            return juce::String (value);
        return juce::String ((double) value / 1000.0, 1) + "k";
    }
}

//==============================================================================
// The source map deliberately reads only MidiAudienceModel snapshots. It never
// reaches into the OSC receiver or MIDI endpoint, so painting remains a cheap,
// side-effect-free operation on the message thread.
class SourceActivityMap final : public juce::Component
{
public:
    explicit SourceActivityMap (MidiAudienceModel& modelToUse)
        : model (modelToUse)
    {
        setTitle ("Source activity map");
        setDescription ("Two hundred and fifty-six OSC source IDs grouped by their stable MIDI channel assignment.");
        setInterceptsMouseClicks (false, false);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        cm::drawCard (g, bounds, "SOURCE MATRIX", "256 SOURCES / 16 CH");

        auto content = bounds.reduced (14);
        content.removeFromTop (30);

        auto subtitle = content.removeFromTop (19);
        g.setColour (cm::textMuted);
        g.setFont (juce::Font (juce::FontOptions (10.5f)));
        g.drawText ("ID-locked source / MIDI channel map",
                    subtitle, juce::Justification::centredLeft, true);

        content.removeFromTop (4);
        auto channelHeader = content.removeFromTop (22);
        auto grid = content.reduced (0, 3);
        if (grid.getWidth() < 160 || grid.getHeight() < 128)
            return;

        const float columnWidth = (float) grid.getWidth() / 16.0f;
        const float rowHeight = (float) grid.getHeight() / 16.0f;
        const float gap = juce::jlimit (1.0f, 2.6f, std::min (columnWidth, rowHeight) * 0.14f);

        // Quiet four-channel dividers make a dense 16x16 grid easy to scan
        // without turning every inactive source into a coloured outline.
        g.setColour (cm::line.withAlpha (0.62f));
        for (int group = 1; group < 4; ++group)
        {
            const auto x = (float) grid.getX() + columnWidth * (float) group * 4.0f;
            g.fillRect (x - 0.5f, (float) channelHeader.getY() + 3.0f,
                        1.0f, (float) grid.getBottom() - (float) channelHeader.getY() - 3.0f);
        }
        for (int group = 1; group < 4; ++group)
        {
            const auto y = (float) grid.getY() + rowHeight * (float) group * 4.0f;
            g.fillRect ((float) grid.getX(), y - 0.5f,
                        (float) grid.getWidth(), 1.0f);
        }

        for (int channelIndex = 0; channelIndex < 16; ++channelIndex)
        {
            const int midiChannel = channelIndex + 1;
            auto heading = juce::Rectangle<float> ((float) grid.getX() + columnWidth * (float) channelIndex,
                                                   (float) channelHeader.getY(), columnWidth,
                                                   (float) channelHeader.getHeight());
            const auto colour = cm::channelColour (midiChannel);
            g.setColour (colour.withAlpha (0.90f));
            g.setFont (juce::Font (juce::FontOptions (9.0f).withStyle ("bold")));
            const auto channelText = columnWidth >= 32.0f ? "CH " + juce::String (midiChannel)
                                                          : juce::String (midiChannel);
            g.drawText (channelText, heading.reduced (1.0f, 0.0f),
                        juce::Justification::centred, false);

            for (int slot = 0; slot < 16; ++slot)
            {
                // OSC source ids are [0,255], while the documented musical map
                // is 1->ch1 ... 17->ch1. Source 0 therefore occupies the final
                // ch16 cell after ids 16,32,...240.
                const int oneBasedId = midiChannel + slot * 16;
                const int sourceId = oneBasedId == 256 ? 0 : oneBasedId;
                const auto snapshot = model.getSourceSnapshot (sourceId);

                auto cell = juce::Rectangle<float> (
                    (float) grid.getX() + columnWidth * (float) channelIndex,
                    (float) grid.getY() + rowHeight * (float) slot,
                    columnWidth, rowHeight).reduced (gap * 0.5f);

                const bool active = snapshot.active;
                g.setColour (active ? colour.withAlpha (0.23f)
                                    : cm::cardRaised.withAlpha (0.58f));
                g.fillRoundedRectangle (cell, juce::jmin (3.5f, rowHeight * 0.22f));

                if (active)
                {
                    g.setColour (colour.withAlpha (0.92f));
                    g.drawRoundedRectangle (cell, juce::jmin (3.5f, rowHeight * 0.22f), 0.9f);

                    const float px = cell.getX() + juce::jlimit (0.12f, 0.88f, snapshot.x) * cell.getWidth();
                    const float py = cell.getY() + juce::jlimit (0.18f, 0.82f, 1.0f - snapshot.y) * cell.getHeight();
                    const float dot = juce::jlimit (2.0f, 4.2f,
                                                   1.7f + 0.25f * (float) snapshot.activeFingerCount);
                    g.setColour (cm::text.withAlpha (0.96f));
                    g.fillEllipse (px - dot * 0.5f, py - dot * 0.5f, dot, dot);

                    if (cell.getWidth() >= 17.0f && cell.getHeight() >= 11.0f)
                    {
                        g.setColour (cm::text.withAlpha (0.90f));
                        g.setFont (juce::Font (juce::FontOptions (6.6f).withStyle ("bold")));
                        g.drawText (juce::String (sourceId), cell.reduced (2.0f),
                                    juce::Justification::bottomLeft, false);
                    }
                }
            }
        }
    }

private:
    MidiAudienceModel& model;
};

//==============================================================================
void AudienceEditor::addChoiceItems (juce::ComboBox& combo, const juce::StringArray& items)
{
    for (int index = 0; index < items.size(); ++index)
        combo.addItem (items[index], index + 1);
}

void AudienceEditor::styleLabel (juce::Label& label, const juce::String& text,
                                 juce::Justification justification)
{
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (justification);
    label.setColour (juce::Label::textColourId, cm::textMuted);
    label.setFont (juce::Font (juce::FontOptions (10.5f)));
    label.setMinimumHorizontalScale (0.78f);
}

void AudienceEditor::styleCombo (juce::ComboBox& combo)
{
    combo.setColour (juce::ComboBox::backgroundColourId, cm::cardRaised);
    combo.setColour (juce::ComboBox::outlineColourId, cm::line);
    combo.setColour (juce::ComboBox::textColourId, cm::text);
    combo.setColour (juce::ComboBox::arrowColourId, cm::cyan);
    combo.setColour (juce::ComboBox::focusedOutlineColourId, cm::cyan);
}

void AudienceEditor::styleButton (juce::Button& button, bool destructive)
{
    const auto accent = destructive ? cm::red : cm::cyan;
    button.setColour (juce::TextButton::buttonColourId, cm::cardRaised);
    button.setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.24f));
    button.setColour (juce::TextButton::textColourOffId, destructive ? cm::red : cm::text);
    button.setColour (juce::TextButton::textColourOnId, cm::text);
}

AudienceEditor::AudienceEditor (AudienceProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse), proc (processorToUse)
{
    lookAndFeel = std::make_unique<cm::LookAndFeel>();
    setLookAndFeel (lookAndFeel.get());

    setSize (1120, 640);
    setResizable (true, true);
    setResizeLimits (900, 560, 2200, 1300);
    setOpaque (true);
    const auto versionText = "v" + juce::String (JucePlugin_VersionString);
    setTitle ("Cosmic Microwave " + versionText + " OSC to MIDI router");
    setDescription ("MIDI-only control surface for zone OSC input, source routing, pitch mapping, Time Field scheduling and MIDI output.");

    versionLabel.setText (versionText, juce::dontSendNotification);
    versionLabel.setJustificationType (juce::Justification::centred);
    versionLabel.setColour (juce::Label::textColourId, cm::cyan.withAlpha (0.88f));
    versionLabel.setFont (juce::Font (juce::FontOptions (9.0f).withStyle ("bold")));
    versionLabel.setTitle ("Cosmic Microwave version");
    versionLabel.setDescription ("Cosmic Microwave version "
                                 + juce::String (JucePlugin_VersionString));
    addAndMakeVisible (versionLabel);

    sourceMap = std::make_unique<SourceActivityMap> (proc.audienceModel);
    addAndMakeVisible (*sourceMap);

    auto styleMetric = [this] (juce::Label& value, juce::Label& caption,
                               const juce::String& captionText,
                               const juce::String& title)
    {
        addAndMakeVisible (value);
        value.setJustificationType (juce::Justification::centred);
        value.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        value.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        value.setColour (juce::Label::textColourId, cm::text);
        value.setFont (juce::Font (juce::FontOptions (16.5f).withStyle ("bold")));
        value.setTitle (title);

        addAndMakeVisible (caption);
        caption.setText (captionText, juce::dontSendNotification);
        caption.setJustificationType (juce::Justification::centred);
        caption.setColour (juce::Label::textColourId, cm::textDim);
        caption.setFont (juce::Font (juce::FontOptions (8.2f).withStyle ("bold")));
        caption.setMinimumHorizontalScale (0.76f);
    };
    styleMetric (activeSourcesValue, activeSourcesCaption, "SOURCES", "Active OSC sources");
    styleMetric (activeFingersValue, activeFingersCaption, "TOUCHES", "Active touches");
    styleMetric (notesSentValue, notesSentCaption, "MIDI NOTES", "MIDI notes sent");
    styleMetric (mpeVoicesValue, mpeVoicesCaption, "MPE VOICES", "Active MPE voices");

    // OSC input ---------------------------------------------------------------
    styleLabel (portLabel, "UDP PORT");
    addAndMakeVisible (portLabel);

    portEditor.setInputRestrictions (5, "0123456789");
    portEditor.setJustification (juce::Justification::centred);
    portEditor.setColour (juce::TextEditor::backgroundColourId, cm::cardRaised);
    portEditor.setColour (juce::TextEditor::textColourId, cm::text);
    portEditor.setColour (juce::TextEditor::outlineColourId, cm::line);
    portEditor.setColour (juce::TextEditor::focusedOutlineColourId, cm::cyan);
    portEditor.setTitle ("UDP listen port");
    portEditor.setDescription ("UDP port for this Cosmic Microwave instance. Enter a number from 1 to 65535 and press Return or Apply.");
    portEditor.setTooltip ("Each zone uses its own UDP port, for example Zone A 6060 and Zone B 6061.");
    portEditor.onTextChange = [this]
    {
        if (! updatingPortEditor)
        {
            portEditorDirty = portEditor.getText().trim() != juce::String (proc.getUdpPort());
            portApplyButton.setEnabled (portEditorDirty);
            portEditor.setDescription ("UDP port for this Cosmic Microwave instance. Enter a number from 1 to 65535 and press Return or Apply.");
            oscPathLabel.setText ("/cs/{zone}/{source}/finger0  |  /u  /v  /on  |  0-1",
                                  juce::dontSendNotification);
            oscPathLabel.setColour (juce::Label::textColourId, cm::textDim);
            oscPathLabel.setTitle ("OSC address format");
            oscPathLabel.setDescription ("Single-touch OSC contract. Only finger0 is accepted; u and v are normalized from zero to one, on 1 starts a touch, and on 0 releases it. A live touch is safety-released after three seconds without valid updates.");
            repaint (oscCardBounds);
        }
    };
    portEditor.onReturnKey = [this] { applyUdpPortFromEditor(); };
    portEditor.onEscapeKey = [this] { restoreUdpPortEditor(); };
    addAndMakeVisible (portEditor);

    styleButton (portApplyButton);
    portApplyButton.setTitle ("Apply UDP port");
    portApplyButton.setTooltip ("Restart OSC listening on the entered UDP port.");
    portApplyButton.onClick = [this] { applyUdpPortFromEditor(); };
    addAndMakeVisible (portApplyButton);

    styleLabel (oscStatusLabel, {}, juce::Justification::centredLeft);
    oscStatusLabel.setFont (juce::Font (juce::FontOptions (11.5f).withStyle ("bold")));
    oscStatusLabel.setTitle ("OSC receiver status");
    addAndMakeVisible (oscStatusLabel);

    styleLabel (oscPathLabel, "/cs/{zone}/{source}/finger0  |  /u  /v  /on  |  0-1");
    oscPathLabel.setColour (juce::Label::textColourId, cm::textDim);
    oscPathLabel.setFont (juce::Font (juce::FontOptions (9.0f)));
    oscPathLabel.setTitle ("OSC address format");
    oscPathLabel.setDescription ("Single-touch OSC contract. Only finger0 is accepted; u and v are normalized from zero to one, on 1 starts a touch, and on 0 releases it. A live touch is safety-released after three seconds without valid updates.");
    oscPathLabel.setTooltip (oscPathLabel.getDescription());
    addAndMakeVisible (oscPathLabel);

    // Routing summary ---------------------------------------------------------
    styleLabel (routingSummaryLabel, {}, juce::Justification::centredLeft);
    routingSummaryLabel.setColour (juce::Label::textColourId, cm::text);
    routingSummaryLabel.setFont (juce::Font (juce::FontOptions (15.0f).withStyle ("bold")));
    routingSummaryLabel.setTitle ("Current source routing mode");
    addAndMakeVisible (routingSummaryLabel);

    styleLabel (routingDetailLabel, {}, juce::Justification::centredLeft);
    routingDetailLabel.setTitle ("Routing detail");
    addAndMakeVisible (routingDetailLabel);

    styleLabel (zoneStatusLabel, {}, juce::Justification::centredLeft);
    zoneStatusLabel.setColour (juce::Label::textColourId, cm::green);
    zoneStatusLabel.setTitle ("Observed OSC zones");
    addAndMakeVisible (zoneStatusLabel);

    // Simulator ---------------------------------------------------------------
    for (auto* button : { &simAddButton, &simCrowdButton, &simRemoveButton, &simClearButton })
    {
        styleButton (*button);
        addAndMakeVisible (*button);
    }
    simAddButton.setTitle ("Add simulated source");
    simCrowdButton.setTitle ("Add twenty-five simulated sources");
    simRemoveButton.setTitle ("Remove simulated source");
    simClearButton.setTitle ("Clear all simulated sources");
    simAddButton.onClick = [this] { proc.simulator.addRandomSeat(); };
    simCrowdButton.onClick = [this] { proc.simulator.addRandomSeats (25); };
    simRemoveButton.onClick = [this] { proc.simulator.removeRandomSeat(); };
    simClearButton.onClick = [this] { proc.simulator.clear(); };

    simMoveButton.setColour (juce::ToggleButton::textColourId, cm::textMuted);
    simMoveButton.setColour (juce::ToggleButton::tickColourId, cm::green);
    simMoveButton.setColour (juce::ToggleButton::tickDisabledColourId, cm::line);
    simMoveButton.setTitle ("Random simulator movement");
    simMoveButton.setTooltip ("Continuously move active simulator sources across the pitch and expression axes.");
    simMoveButton.onClick = [this]
    {
        proc.simulator.setRandomMovement (simMoveButton.getToggleState());
    };
    addAndMakeVisible (simMoveButton);

    // Pitch mapping -----------------------------------------------------------
    addChoiceItems (pitchSystemCombo, { "Tonal", "Atomic" });
    addChoiceItems (rootCombo, { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" });
    addChoiceItems (rootOctaveCombo, { "0", "1", "2", "3", "4", "5", "6" });
    addChoiceItems (scaleCombo, { "Major", "Natural Minor", "Pentatonic", "Dorian",
                                  "Lydian", "Harmonic Minor", "Whole Tone" });
    juce::StringArray atomicElements;
    for (int index = 0; index < AtomicScaleCatalog::numElements; ++index)
        atomicElements.add (proc.getAtomicElementSymbol (index) + "  "
                            + proc.getAtomicElementName (index));
    addChoiceItems (atomicElementCombo, atomicElements);

    juce::StringArray atomicModes;
    for (int index = 0; index < AtomicScaleCatalog::numModes; ++index)
        atomicModes.add (proc.getAtomicModeName (index));
    addChoiceItems (atomicModeCombo, atomicModes);

    for (auto* combo : { &pitchSystemCombo, &rootCombo, &rootOctaveCombo, &scaleCombo,
                         &atomicElementCombo, &atomicModeCombo })
    {
        styleCombo (*combo);
        addAndMakeVisible (*combo);
    }
    pitchSystemCombo.setTitle ("Pitch system");
    pitchSystemCombo.setDescription ("Choose equal-tempered tonal maps or exact element-derived Atomic Scale maps.");
    rootCombo.setTitle ("Scale root note");
    rootOctaveCombo.setTitle ("Scale root octave");
    scaleCombo.setTitle ("Scale mode");
    atomicElementCombo.setTitle ("Atomic element");
    atomicElementCombo.setDescription ("Element spectrum used to derive the pitch degrees.");
    atomicModeCombo.setTitle ("Atomic scale density");
    atomicModeCombo.setDescription ("Select the number and spacing of element-derived pitch degrees.");

    octavesSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    octavesSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 34, 24);
    octavesSlider.setColour (juce::Slider::trackColourId, cm::cyan);
    octavesSlider.setColour (juce::Slider::backgroundColourId, cm::line);
    octavesSlider.setColour (juce::Slider::thumbColourId, cm::text);
    octavesSlider.setColour (juce::Slider::textBoxTextColourId, cm::text);
    octavesSlider.setColour (juce::Slider::textBoxBackgroundColourId, cm::cardRaised);
    octavesSlider.setColour (juce::Slider::textBoxOutlineColourId, cm::line);
    octavesSlider.setTitle ("Scale octave span");
    octavesSlider.setDescription ("Number of octaves used to map horizontal OSC position to MIDI pitch.");
    addAndMakeVisible (octavesSlider);

    styleLabel (rootLabel, "ROOT");
    styleLabel (rootOctaveLabel, "OCTAVE");
    styleLabel (scaleLabel, "SCALE");
    styleLabel (atomicElementLabel, "ELEMENT");
    styleLabel (atomicModeLabel, "DENSITY");
    styleLabel (octavesLabel, "RANGE");
    for (auto* label : { &rootLabel, &rootOctaveLabel, &scaleLabel, &atomicElementLabel,
                         &atomicModeLabel, &octavesLabel })
        addAndMakeVisible (*label);

    pitchSystemAttachment = std::make_unique<ComboAttachment> (proc.apvts, "pitchSystem", pitchSystemCombo);
    rootAttachment = std::make_unique<ComboAttachment> (proc.apvts, "scaleRoot", rootCombo);
    rootOctaveAttachment = std::make_unique<ComboAttachment> (proc.apvts, "scaleRootOctave", rootOctaveCombo);
    scaleAttachment = std::make_unique<ComboAttachment> (proc.apvts, "scaleMode", scaleCombo);
    atomicElementAttachment = std::make_unique<ComboAttachment> (proc.apvts, "spectralElement", atomicElementCombo);
    atomicModeAttachment = std::make_unique<ComboAttachment> (proc.apvts, "atomicScaleMode", atomicModeCombo);
    octavesAttachment = std::make_unique<SliderAttachment> (proc.apvts, "scaleOctaves", octavesSlider);

    pitchSystemCombo.onChange = [this] { updateModeVisibility(); updateLiveText(); };
    atomicElementCombo.onChange = [this] { updateLiveText(); repaint (pitchCardBounds); };
    atomicModeCombo.onChange = [this] { updateLiveText(); repaint (pitchCardBounds); };

    // Time Field -------------------------------------------------------------
    addChoiceItems (timeModeCombo, { "Flow", "Grid", "Ensemble" });
    addChoiceItems (clockSourceCombo, { "Host", "Internal" });
    addChoiceItems (gridDivisionCombo, { "1/4", "1/8", "1/16", "1/32" });
    addChoiceItems (temporalSpreadCombo, { "1", "2", "4", "8", "16" });

    for (auto* combo : { &timeModeCombo, &clockSourceCombo,
                         &gridDivisionCombo, &temporalSpreadCombo })
    {
        styleCombo (*combo);
        addAndMakeVisible (*combo);
    }

    timeModeCombo.setTitle ("Time Field mode");
    timeModeCombo.setDescription ("Flow passes attacks freely, Grid quantizes them, and Ensemble distributes them through the temporal field.");
    clockSourceCombo.setTitle ("Time Field clock source");
    clockSourceCombo.setDescription ("Follow the host transport tempo or use Cosmic Microwave's internal clock.");
    gridDivisionCombo.setTitle ("Time Field grid division");
    gridDivisionCombo.setDescription ("Temporal scheduling grid, from quarter notes to thirty-second notes.");
    temporalSpreadCombo.setTitle ("Time Field temporal spread");
    temporalSpreadCombo.setDescription ("Number of grid steps over which scheduled attacks may be distributed.");

    auto styleTimeSlider = [this] (juce::Slider& slider, const juce::String& title,
                                   const juce::String& description, const juce::String& suffix,
                                   int decimalPlaces)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 24);
        slider.setColour (juce::Slider::trackColourId, cm::violet);
        slider.setColour (juce::Slider::backgroundColourId, cm::line);
        slider.setColour (juce::Slider::thumbColourId, cm::text);
        slider.setColour (juce::Slider::textBoxTextColourId, cm::text);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, cm::cardRaised);
        slider.setColour (juce::Slider::textBoxOutlineColourId, cm::line);
        slider.setTextValueSuffix (suffix);
        slider.setNumDecimalPlacesToDisplay (decimalPlaces);
        slider.setTitle (title);
        slider.setDescription (description);
        addAndMakeVisible (slider);
    };
    styleTimeSlider (internalBpmSlider, "Internal tempo",
                     "Internal Time Field tempo in beats per minute.", {}, 1);
    styleTimeSlider (maxAttacksSlider, "Maximum attacks per step",
                     "Maximum new MIDI attacks emitted on one Time Field grid step.", {}, 0);
    styleTimeSlider (maxActiveVoicesSlider, "Maximum active voices",
                     "Maximum simultaneous scheduled MIDI voices.", {}, 0);
    styleTimeSlider (gatePercentSlider, "Gate length",
                     "Scheduled note gate as a percentage of the selected grid division.", " %", 0);

    styleLabel (timeModeLabel, "MODE");
    styleLabel (clockSourceLabel, "CLOCK");
    styleLabel (internalBpmLabel, "INTERNAL BPM");
    styleLabel (gridDivisionLabel, "DIVISION");
    styleLabel (maxAttacksLabel, "ATTACKS / STEP");
    styleLabel (maxActiveVoicesLabel, "ACTIVE LIMIT");
    styleLabel (gatePercentLabel, "GATE");
    styleLabel (temporalSpreadLabel, "SPREAD / STEPS");
    for (auto* label : { &timeModeLabel, &clockSourceLabel, &internalBpmLabel,
                         &gridDivisionLabel, &maxAttacksLabel, &maxActiveVoicesLabel,
                         &gatePercentLabel, &temporalSpreadLabel })
        addAndMakeVisible (*label);

    styleLabel (timeStatusLabel, {}, juce::Justification::centredLeft);
    timeStatusLabel.setColour (juce::Label::textColourId, cm::green);
    timeStatusLabel.setFont (juce::Font (juce::FontOptions (10.5f).withStyle ("bold")));
    timeStatusLabel.setMinimumHorizontalScale (0.78f);
    timeStatusLabel.setTitle ("Time Field live status");
    timeStatusLabel.setDescription ("Clock, tempo, grid division, pending attacks, active voices and merged-attack telemetry.");
    addAndMakeVisible (timeStatusLabel);

    styleLabel (timeTelemetryLabel, {}, juce::Justification::centredLeft);
    timeTelemetryLabel.setColour (juce::Label::textColourId, cm::textMuted);
    timeTelemetryLabel.setFont (juce::Font (juce::FontOptions (9.6f).withStyle ("bold")));
    timeTelemetryLabel.setMinimumHorizontalScale (0.72f);
    timeTelemetryLabel.setTitle ("Time Field load");
    timeTelemetryLabel.setDescription ("Pending attacks, active scheduled voices and merged same-step attacks.");
    addAndMakeVisible (timeTelemetryLabel);

    styleButton (governorModeButton);
    governorModeButton.setComponentID ("governorPill");
    governorModeButton.setClickingTogglesState (true);
    governorModeButton.setColour (juce::TextButton::textColourOnId, cm::cyan);
    governorModeButton.setTitle ("Adaptive Crowd Governor");
    governorModeButton.setDescription ("Manual uses the saved Time Field limits. Adaptive derives attacks, active voices and temporal spread from the observed crowd density.");
    governorModeButton.setTooltip (governorModeButton.getDescription());
    addAndMakeVisible (governorModeButton);

    auto styleGovernorValue = [this] (juce::Label& value, const juce::String& title)
    {
        value.setJustificationType (juce::Justification::centred);
        value.setColour (juce::Label::backgroundColourId, cm::cardRaised);
        value.setColour (juce::Label::outlineColourId, cm::line);
        value.setColour (juce::Label::textColourId, cm::cyan);
        value.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("bold")));
        value.setTitle (title);
        value.setInterceptsMouseClicks (false, false);
        value.setWantsKeyboardFocus (false);
        addAndMakeVisible (value);
    };
    styleGovernorValue (governorAttacksValue, "Effective attacks per step");
    styleGovernorValue (governorActiveVoicesValue, "Effective active-voice limit");
    styleGovernorValue (governorSpreadValue, "Effective temporal spread");

    timeModeAttachment = std::make_unique<ComboAttachment> (proc.apvts, "timeMode", timeModeCombo);
    clockSourceAttachment = std::make_unique<ComboAttachment> (proc.apvts, "clockSource", clockSourceCombo);
    internalBpmAttachment = std::make_unique<SliderAttachment> (proc.apvts, "internalBpm", internalBpmSlider);
    gridDivisionAttachment = std::make_unique<ComboAttachment> (proc.apvts, "gridDivision", gridDivisionCombo);
    maxAttacksAttachment = std::make_unique<SliderAttachment> (proc.apvts, "maxAttacksPerStep", maxAttacksSlider);
    maxActiveVoicesAttachment = std::make_unique<SliderAttachment> (proc.apvts, "maxActiveVoices", maxActiveVoicesSlider);
    gatePercentAttachment = std::make_unique<SliderAttachment> (proc.apvts, "gatePercent", gatePercentSlider);
    temporalSpreadAttachment = std::make_unique<ComboAttachment> (proc.apvts, "temporalSpread", temporalSpreadCombo);
    governorModeAttachment = std::make_unique<ButtonAttachment> (proc.apvts, "crowdGovernorEnabled", governorModeButton);

    timeModeCombo.onChange = [this] { updateModeVisibility(); updateLiveText(); };
    clockSourceCombo.onChange = [this] { updateModeVisibility(); updateLiveText(); };
    gridDivisionCombo.onChange = [this] { updateLiveText(); };
    governorModeButton.onClick = [this] { updateModeVisibility(); updateLiveText(); };

    // MIDI routing ------------------------------------------------------------
    addChoiceItems (midiTypeCombo, { "Off", "Normal MIDI", "MPE MIDI" });
    addChoiceItems (normalRoutingCombo, { "Single channel", "Per source 1-16" });
    juce::StringArray channels;
    for (int channel = 1; channel <= 16; ++channel)
        channels.add ("Channel " + juce::String (channel));
    addChoiceItems (normalChannelCombo, channels);
    addChoiceItems (mpeZoneCombo, { "Lower (M1 / Ch2-16)", "Upper (M16 / Ch1-15)" });
    addChoiceItems (mpeBendRangeCombo, { "+/-2 st", "+/-12 st", "+/-24 st", "+/-48 st" });
    addChoiceItems (mpePitchModeCombo, { "Retrigger", "Glide" });

    for (auto* combo : { &midiTypeCombo, &normalRoutingCombo, &normalChannelCombo,
                         &mpeZoneCombo, &mpeBendRangeCombo, &mpePitchModeCombo })
    {
        styleCombo (*combo);
        addAndMakeVisible (*combo);
    }
    midiTypeCombo.setTitle ("MIDI output protocol");
    normalRoutingCombo.setTitle ("Normal MIDI source routing");
    normalChannelCombo.setTitle ("Fixed normal MIDI channel");
    mpeZoneCombo.setTitle ("MPE zone");
    mpeBendRangeCombo.setTitle ("MPE pitch bend range");
    mpePitchModeCombo.setTitle ("MPE pitch motion mode");

    styleLabel (midiTypeLabel, "OUTPUT");
    styleLabel (normalRoutingLabel, "SOURCE ROUTING");
    styleLabel (normalChannelLabel, "FIXED CHANNEL");
    styleLabel (mpeZoneLabel, "ZONE");
    styleLabel (mpeBendRangeLabel, "BEND RANGE");
    styleLabel (mpePitchModeLabel, "PITCH MOTION");
    for (auto* label : { &midiTypeLabel, &normalRoutingLabel, &normalChannelLabel,
                         &mpeZoneLabel, &mpeBendRangeLabel, &mpePitchModeLabel })
        addAndMakeVisible (*label);

    mpeSetupButton.setColour (juce::ToggleButton::textColourId, cm::textMuted);
    mpeSetupButton.setColour (juce::ToggleButton::tickColourId, cm::violet);
    mpeSetupButton.setColour (juce::ToggleButton::tickDisabledColourId, cm::line);
    mpeSetupButton.setTitle ("Send MPE setup messages");
    mpeSetupButton.setTooltip ("Send MPE zone configuration and pitch-bend-range RPN messages to the destination.");
    addAndMakeVisible (mpeSetupButton);

    midiTypeAttachment = std::make_unique<ComboAttachment> (proc.apvts, "midiOutputType", midiTypeCombo);
    normalRoutingAttachment = std::make_unique<ComboAttachment> (proc.apvts, "normalMidiRoutingMode", normalRoutingCombo);
    normalChannelAttachment = std::make_unique<ComboAttachment> (proc.apvts, "normalMidiChannel", normalChannelCombo);
    mpeZoneAttachment = std::make_unique<ComboAttachment> (proc.apvts, "mpeZone", mpeZoneCombo);
    mpeBendRangeAttachment = std::make_unique<ComboAttachment> (proc.apvts, "mpePitchBendRange", mpeBendRangeCombo);
    mpePitchModeAttachment = std::make_unique<ComboAttachment> (proc.apvts, "mpePitchMode", mpePitchModeCombo);
    mpeSetupAttachment = std::make_unique<ButtonAttachment> (proc.apvts, "mpeSendSetupMessages", mpeSetupButton);

    midiTypeCombo.onChange = [this] { updateModeVisibility(); updateLiveText(); };
    normalRoutingCombo.onChange = [this] { updateModeVisibility(); updateLiveText(); };
    mpeZoneCombo.onChange = [this] { updateLiveText(); };

    // MIDI destination --------------------------------------------------------
    styleLabel (destinationLabel, "DESTINATION");
    addAndMakeVisible (destinationLabel);
    styleCombo (destinationCombo);
    destinationCombo.setTitle ("MIDI output destination");
    destinationCombo.setDescription ("Send MIDI to the host bus, a virtual port or an available hardware output.");
    destinationCombo.onChange = [this]
    {
        if (! refreshingDestination && destinationCombo.getSelectedItemIndex() >= 0)
        {
            proc.setMidiOutputOptionIndex (destinationCombo.getSelectedItemIndex());
            updateLiveText();
        }
    };
    addAndMakeVisible (destinationCombo);

    styleButton (rescanButton);
    rescanButton.setTitle ("Rescan MIDI destinations");
    rescanButton.setTooltip ("Refresh virtual and hardware MIDI output choices.");
    rescanButton.onClick = [this] { refreshMidiOutputCombo(); };
    addAndMakeVisible (rescanButton);

    styleLabel (destinationStatusLabel, {}, juce::Justification::centredLeft);
    destinationStatusLabel.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("bold")));
    destinationStatusLabel.setTitle ("MIDI destination status");
    addAndMakeVisible (destinationStatusLabel);

    styleLabel (destinationDetailLabel, {}, juce::Justification::centredLeft);
    destinationDetailLabel.setColour (juce::Label::textColourId, cm::textDim);
    destinationDetailLabel.setFont (juce::Font (juce::FontOptions (9.5f)));
    destinationDetailLabel.setTitle ("MIDI destination detail");
    addAndMakeVisible (destinationDetailLabel);

    styleButton (panicButton, true);
    panicButton.setTitle ("MIDI panic");
    panicButton.setDescription ("Send note-off and all-sound-off messages on all MIDI channels.");
    panicButton.setTooltip ("Release every active note on the host and external MIDI outputs.");
    panicButton.onClick = [this] { proc.panic(); updateLiveText(); };
    addAndMakeVisible (panicButton);

    restoreUdpPortEditor();
    refreshMidiOutputCombo();
    updateModeVisibility();
    updateLiveText();
    startTimerHz (8);
}

AudienceEditor::~AudienceEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void AudienceEditor::paint (juce::Graphics& g)
{
    juce::ColourGradient bodyGradient (cm::background.brighter (0.018f), 0.0f, 64.0f,
                                       cm::background, 0.0f, (float) getHeight(), false);
    g.setGradientFill (bodyGradient);
    g.fillRect (getLocalBounds());

    auto headerBounds = getLocalBounds().removeFromTop (64);
    juce::ColourGradient headerGradient (cm::header.brighter (0.035f),
                                         (float) headerBounds.getX(), 0.0f,
                                         cm::header, (float) headerBounds.getRight(), 0.0f, false);
    headerGradient.addColour (0.48, juce::Colour (0xff101925));
    g.setGradientFill (headerGradient);
    g.fillRect (headerBounds);
    g.setColour (cm::lineSoft);
    g.fillRect (headerBounds.removeFromBottom (1));

    auto brand = juce::Rectangle<int> (18, 12, 36, 36).toFloat();
    g.setColour (cm::cyan.withAlpha (0.075f));
    g.fillEllipse (brand.expanded (1.0f));
    g.setColour (cm::cyan.withAlpha (0.88f));
    g.drawEllipse (brand.reduced (3.0f), 1.25f);
    g.setColour (cm::violet.withAlpha (0.84f));
    g.drawEllipse (brand.reduced (9.5f), 1.15f);
    g.setColour (cm::text);
    g.fillEllipse (brand.getCentreX() - 2.5f, brand.getCentreY() - 2.5f, 5.0f, 5.0f);
    g.setColour (cm::cyan);
    g.fillEllipse (brand.getRight() - 7.0f, brand.getY() + 7.0f, 3.5f, 3.5f);

    g.setColour (cm::text);
    g.setFont (juce::Font (juce::FontOptions (20.0f).withStyle ("bold")));
    g.drawText ("COSMIC MICROWAVE", 66, 9, 260, 25, juce::Justification::centredLeft, false);
    g.setColour (cm::textDim);
    g.setFont (juce::Font (juce::FontOptions (9.5f).withStyle ("bold")));
    g.drawText ("OSC / MIDI ROUTING", 67, 35, 218, 14,
                juce::Justification::centredLeft, false);

    auto badge = juce::Rectangle<float> (303.0f, 20.0f, 67.0f, 20.0f);
    g.setColour (cm::green.withAlpha (0.07f));
    g.fillRoundedRectangle (badge, 10.0f);
    g.setColour (cm::green);
    g.fillEllipse (badge.getX() + 9.0f, badge.getCentreY() - 2.0f, 4.0f, 4.0f);
    g.setFont (juce::Font (juce::FontOptions (8.5f).withStyle ("bold")));
    g.drawText ("MIDI ONLY", badge.toNearestInt().withTrimmedLeft (9),
                juce::Justification::centred, false);

    const std::array<juce::Label*, 4> metricValues {{ &activeSourcesValue, &activeFingersValue,
                                                      &notesSentValue, &mpeVoicesValue }};
    const std::array<juce::Label*, 4> metricCaptions {{ &activeSourcesCaption, &activeFingersCaption,
                                                        &notesSentCaption, &mpeVoicesCaption }};
    for (size_t index = 0; index < metricValues.size(); ++index)
    {
        const auto metricBounds = metricValues[index]->getBounds()
                                      .getUnion (metricCaptions[index]->getBounds()).toFloat();
        g.setColour (cm::cardRaised.withAlpha (0.58f));
        g.fillRoundedRectangle (metricBounds, 7.0f);
        g.setColour (cm::lineSoft.withAlpha (0.92f));
        g.drawRoundedRectangle (metricBounds.reduced (0.5f), 7.0f, 0.8f);

        const auto accent = cm::cyan.interpolatedWith (cm::violet,
                                                        (float) index / 3.0f);
        g.setColour (accent.withAlpha (0.72f));
        g.fillRoundedRectangle (metricBounds.withHeight (1.4f).reduced (8.0f, 0.0f), 0.7f);
    }

    cm::drawCard (g, oscCardBounds, "OSC INPUT", "ZONE / FINGER0");
    cm::drawCard (g, routingCardBounds, "SOURCE ROUTING", "ID-LOCKED");
    cm::drawCard (g, simulatorCardBounds, "SIMULATOR", "LOCAL TEST");
    cm::drawCard (g, pitchCardBounds, "PITCH MAPPING");
    cm::drawCard (g, timeCardBounds, "TIME FIELD");
    cm::drawCard (g, midiCardBounds, "MIDI ROUTING", "NORMAL / MPE");
    cm::drawCard (g, destinationCardBounds, "MIDI OUTPUT");

    if (! timeStatusLabel.getBounds().isEmpty() && ! timeTelemetryLabel.getBounds().isEmpty())
    {
        const auto liveBounds = timeStatusLabel.getBounds()
                                    .getUnion (timeTelemetryLabel.getBounds())
                                    .toFloat().expanded (5.0f, 2.0f);
        g.setColour (cm::background.withAlpha (0.44f));
        g.fillRoundedRectangle (liveBounds, 6.0f);
        g.setColour (cm::lineSoft.withAlpha (0.86f));
        g.drawRoundedRectangle (liveBounds.reduced (0.5f), 6.0f, 0.8f);
    }
}

void AudienceEditor::resized()
{
    auto area = getLocalBounds();
    auto headerArea = area.removeFromTop (64);
    versionLabel.setBounds (303, 42, 67, 12);

    auto metrics = headerArea.reduced (14, 12).removeFromRight (juce::jmin (500, getWidth() - 395));
    constexpr int metricGap = 7;
    const int metricWidth = (metrics.getWidth() - metricGap * 3) / 4;
    const std::array<juce::Label*, 4> metricValues {{ &activeSourcesValue, &activeFingersValue,
                                                      &notesSentValue, &mpeVoicesValue }};
    const std::array<juce::Label*, 4> metricCaptions {{ &activeSourcesCaption, &activeFingersCaption,
                                                        &notesSentCaption, &mpeVoicesCaption }};
    for (size_t index = 0; index < metricValues.size(); ++index)
    {
        auto metric = metrics.removeFromLeft (metricWidth);
        metricValues[index]->setBounds (metric.removeFromTop (26));
        metricCaptions[index]->setBounds (metric);
        metrics.removeFromLeft (metricGap);
    }

    area.reduce (14, 12);
    const int topHeight = juce::jlimit (120, 142, (int) std::round ((double) area.getHeight() * 0.25));
    auto top = area.removeFromTop (topHeight);
    area.removeFromTop (8);

    const int cardGap = 10;
    const int oscWidth = (int) std::round ((double) (top.getWidth() - cardGap * 2) * 0.33);
    const int routingWidth = (int) std::round ((double) (top.getWidth() - cardGap * 2) * 0.38);
    oscCardBounds = top.removeFromLeft (oscWidth);
    top.removeFromLeft (cardGap);
    routingCardBounds = top.removeFromLeft (routingWidth);
    top.removeFromLeft (cardGap);
    simulatorCardBounds = top;

    const int sidebarWidth = juce::jlimit (348, 430, (int) std::round ((double) area.getWidth() * 0.38));
    auto sidebar = area.removeFromRight (sidebarWidth);
    area.removeFromRight (11);
    const int timeWidth = juce::jlimit (260, 304,
                                        (int) std::round ((double) area.getWidth() * 0.44));
    timeCardBounds = area.removeFromRight (timeWidth);
    area.removeFromRight (9);
    mapCardBounds = area;
    if (sourceMap != nullptr)
        sourceMap->setBounds (mapCardBounds);

    const int pitchHeight = juce::jlimit (90, 126, (int) std::round ((double) sidebar.getHeight() * 0.26));
    const int desiredDestinationHeight = juce::jlimit (114, 138, (int) std::round ((double) sidebar.getHeight() * 0.34));
    const int destinationHeight = juce::jmin (desiredDestinationHeight,
                                               sidebar.getHeight() - pitchHeight - 14 - 126);
    pitchCardBounds = sidebar.removeFromTop (pitchHeight);
    sidebar.removeFromTop (7);
    destinationCardBounds = sidebar.removeFromBottom (destinationHeight);
    sidebar.removeFromBottom (7);
    midiCardBounds = sidebar;

    // OSC input card
    {
        auto inner = oscCardBounds.reduced (14);
        inner.removeFromTop (31);
        auto portRow = inner.removeFromTop (28);
        portLabel.setBounds (portRow.removeFromLeft (58));
        portRow.removeFromLeft (4);
        portApplyButton.setBounds (portRow.removeFromRight (62));
        portRow.removeFromRight (6);
        portEditor.setBounds (portRow.removeFromLeft (86));
        inner.removeFromTop (1);
        oscStatusLabel.setBounds (inner.removeFromTop (17));
        oscPathLabel.setBounds (inner);
    }

    // Routing summary card
    {
        auto inner = routingCardBounds.reduced (14);
        inner.removeFromTop (31);
        routingSummaryLabel.setBounds (inner.removeFromTop (25));
        routingDetailLabel.setBounds (inner.removeFromTop (23));
        zoneStatusLabel.setBounds (inner.removeFromTop (21));
    }

    // Simulator card
    {
        auto inner = simulatorCardBounds.reduced (14);
        inner.removeFromTop (34);
        auto row = inner.removeFromTop (28);
        const std::array<int, 4> weights {{ 82, 54, 72, 54 }};
        const int totalWeight = 262;
        const int available = row.getWidth() - 18;
        std::array<juce::Button*, 4> buttons {{ &simAddButton, &simCrowdButton, &simRemoveButton, &simClearButton }};
        for (size_t index = 0; index < buttons.size(); ++index)
        {
            const int width = index + 1 == buttons.size()
                                  ? row.getWidth()
                                  : juce::jmax (42, available * weights[index] / totalWeight);
            buttons[index]->setBounds (row.removeFromLeft (width));
            if (index + 1 != buttons.size())
                row.removeFromLeft (6);
        }
        inner.removeFromTop (5);
        simMoveButton.setBounds (inner.removeFromTop (25));
    }

    // Time Field: a dedicated two-column scheduling surface. Keeping it beside
    // the source matrix gives all eight parameters full-height controls even at
    // the 900x560 minimum editor size.
    {
        governorModeButton.setBounds (timeCardBounds.getRight() - 109,
                                      timeCardBounds.getY() + 6, 96, 24);
        auto inner = timeCardBounds.reduced (13);
        inner.removeFromTop (27);
        timeStatusLabel.setBounds (inner.removeFromTop (21));
        timeTelemetryLabel.setBounds (inner.removeFromTop (17));
        inner.removeFromTop (3);

        auto layoutPair = [] (juce::Rectangle<int> row,
                              juce::Label& leftLabel, juce::Component& leftControl,
                              juce::Label& rightLabel, juce::Component& rightControl)
        {
            constexpr int gap = 7;
            auto labels = row.removeFromTop (12);
            auto controls = row;
            const int leftWidth = (labels.getWidth() - gap) / 2;
            leftLabel.setBounds (labels.removeFromLeft (leftWidth));
            labels.removeFromLeft (gap);
            rightLabel.setBounds (labels);
            leftControl.setBounds (controls.removeFromLeft (leftWidth));
            controls.removeFromLeft (gap);
            rightControl.setBounds (controls);
        };

        const int rowGap = 3;
        const int rowHeight = juce::jlimit (39, 47,
                                            juce::jmax (1, (inner.getHeight() - rowGap * 3) / 4));
        const int controlsHeight = rowHeight * 4 + rowGap * 3;
        inner.removeFromTop (juce::jmax (0, (inner.getHeight() - controlsHeight) / 2));
        layoutPair (inner.removeFromTop (rowHeight),
                    timeModeLabel, timeModeCombo, clockSourceLabel, clockSourceCombo);
        inner.removeFromTop (rowGap);

        auto clockRow = inner.removeFromTop (rowHeight);
        const int selectedMode = timeModeCombo.getSelectedItemIndex();
        const bool timedMode = selectedMode >= 0
                                 ? selectedMode != 0
                                 : cm::choiceValue (proc.apvts, "timeMode") != 0;
        const int selectedClock = clockSourceCombo.getSelectedItemIndex();
        const bool internalClock = selectedClock >= 0
                                     ? selectedClock == 1
                                     : cm::choiceValue (proc.apvts, "clockSource") == 1;
        if (timedMode && internalClock)
        {
            layoutPair (clockRow, internalBpmLabel, internalBpmSlider,
                        gridDivisionLabel, gridDivisionCombo);
        }
        else
        {
            auto label = clockRow.removeFromTop (12);
            gridDivisionLabel.setBounds (label);
            gridDivisionCombo.setBounds (clockRow);
            internalBpmLabel.setBounds ({});
            internalBpmSlider.setBounds ({});
        }
        inner.removeFromTop (rowGap);

        layoutPair (inner.removeFromTop (rowHeight),
                    maxAttacksLabel, maxAttacksSlider,
                    maxActiveVoicesLabel, maxActiveVoicesSlider);
        inner.removeFromTop (rowGap);
        layoutPair (inner.removeFromTop (rowHeight),
                    gatePercentLabel, gatePercentSlider,
                    temporalSpreadLabel, temporalSpreadCombo);

        // Adaptive readouts replace only the three governed policy controls.
        // Their APVTS-attached manual controls keep their values off-screen, so
        // enabling the Governor never overwrites automation or session state.
        governorAttacksValue.setBounds (maxAttacksSlider.getBounds());
        governorActiveVoicesValue.setBounds (maxActiveVoicesSlider.getBounds());
        governorSpreadValue.setBounds (temporalSpreadCombo.getBounds());
    }

    // Pitch mapping card: the system selector lives in the card header; the
    // content row adapts between four tonal fields and five Atomic fields.
    {
        pitchSystemCombo.setBounds (pitchCardBounds.getRight() - 112,
                                    pitchCardBounds.getY() + 6, 99, 24);
        auto inner = pitchCardBounds.reduced (13);
        inner.removeFromTop (27);
        auto labelRow = inner.removeFromTop (13);
        auto controlRow = inner.removeFromTop (juce::jmin (28, inner.getHeight()));
        const int gap = 6;
        const bool atomic = cm::choiceValue (proc.apvts, "pitchSystem") == 1;
        if (atomic)
        {
            const int rootWidth = 44;
            const int octaveWidth = 48;
            const int rangeWidth = 56;
            const int flexible = juce::jmax (96, labelRow.getWidth()
                                                  - rootWidth - octaveWidth - rangeWidth - gap * 4);
            const int elementWidth = (int) std::round ((double) flexible * 0.55);
            const std::array<int, 5> widths {{ rootWidth, octaveWidth, elementWidth,
                                               flexible - elementWidth, rangeWidth }};
            std::array<juce::Label*, 5> labels {{ &rootLabel, &rootOctaveLabel,
                                                  &atomicElementLabel, &atomicModeLabel,
                                                  &octavesLabel }};
            std::array<juce::Component*, 5> controls {{ &rootCombo, &rootOctaveCombo,
                                                         &atomicElementCombo, &atomicModeCombo,
                                                         &octavesSlider }};
            for (size_t index = 0; index < controls.size(); ++index)
            {
                const int width = index + 1 == controls.size() ? labelRow.getWidth() : widths[index];
                labels[index]->setBounds (labelRow.removeFromLeft (width));
                controls[index]->setBounds (controlRow.removeFromLeft (width));
                if (index + 1 != controls.size())
                {
                    labelRow.removeFromLeft (gap);
                    controlRow.removeFromLeft (gap);
                }
            }
        }
        else
        {
            const int fieldWidth = (inner.getWidth() - gap * 3) / 4;
            std::array<juce::Label*, 4> labels {{ &rootLabel, &rootOctaveLabel,
                                                  &scaleLabel, &octavesLabel }};
            std::array<juce::Component*, 4> controls {{ &rootCombo, &rootOctaveCombo,
                                                         &scaleCombo, &octavesSlider }};
            for (size_t index = 0; index < controls.size(); ++index)
            {
                const int width = index + 1 == controls.size() ? labelRow.getWidth() : fieldWidth;
                labels[index]->setBounds (labelRow.removeFromLeft (width));
                controls[index]->setBounds (controlRow.removeFromLeft (width));
                if (index + 1 != controls.size())
                {
                    labelRow.removeFromLeft (gap);
                    controlRow.removeFromLeft (gap);
                }
            }
        }
    }

    // MIDI routing card: protocol on row one, mode-specific controls on row two.
    {
        auto inner = midiCardBounds.reduced (13);
        inner.removeFromTop (26);
        const int availableHeight = inner.getHeight();
        const int controlHeight = juce::jlimit (24, 30, (availableHeight - 26) / 2);

        auto firstLabels = inner.removeFromTop (12);
        auto firstControls = inner.removeFromTop (controlHeight);
        midiTypeLabel.setBounds (firstLabels.removeFromLeft (juce::jmin (160, firstLabels.getWidth() / 2)));
        midiTypeCombo.setBounds (firstControls.removeFromLeft (juce::jmin (160, firstControls.getWidth() / 2)));
        mpeSetupButton.setBounds (firstControls.reduced (8, 0));

        inner.removeFromTop (2);
        auto secondLabels = inner.removeFromTop (12);
        auto secondControls = inner.removeFromTop (controlHeight);
        const int mode = cm::choiceValue (proc.apvts, "midiOutputType");

        if (mode == 1)
        {
            const bool fixed = cm::choiceValue (proc.apvts, "normalMidiRoutingMode") == 0;
            const int leftWidth = fixed ? (int) std::round ((double) secondControls.getWidth() * 0.62)
                                        : secondControls.getWidth();
            normalRoutingLabel.setBounds (secondLabels.removeFromLeft (leftWidth));
            normalRoutingCombo.setBounds (secondControls.removeFromLeft (leftWidth));
            if (fixed)
            {
                secondLabels.removeFromLeft (7);
                secondControls.removeFromLeft (7);
                normalChannelLabel.setBounds (secondLabels);
                normalChannelCombo.setBounds (secondControls);
            }
        }
        else if (mode == 2)
        {
            const int gap = 6;
            const int width = (secondControls.getWidth() - gap * 2) / 3;
            std::array<juce::Label*, 3> labels {{ &mpeZoneLabel, &mpeBendRangeLabel, &mpePitchModeLabel }};
            std::array<juce::Component*, 3> controls {{ &mpeZoneCombo, &mpeBendRangeCombo, &mpePitchModeCombo }};
            for (size_t index = 0; index < controls.size(); ++index)
            {
                const int fieldWidth = index + 1 == controls.size() ? secondControls.getWidth() : width;
                labels[index]->setBounds (secondLabels.removeFromLeft (fieldWidth));
                controls[index]->setBounds (secondControls.removeFromLeft (fieldWidth));
                if (index + 1 != controls.size())
                {
                    secondLabels.removeFromLeft (gap);
                    secondControls.removeFromLeft (gap);
                }
            }
        }
    }

    // MIDI destination card
    {
        auto inner = destinationCardBounds.reduced (13);
        inner.removeFromTop (27);
        destinationLabel.setBounds (inner.removeFromTop (13));
        auto row = inner.removeFromTop (juce::jmin (28, inner.getHeight()));
        panicButton.setBounds (row.removeFromRight (64));
        row.removeFromRight (6);
        rescanButton.setBounds (row.removeFromRight (62));
        row.removeFromRight (6);
        destinationCombo.setBounds (row);
        inner.removeFromTop (2);
        destinationStatusLabel.setBounds (inner.removeFromTop (juce::jmin (18, inner.getHeight())));
        const bool showDetail = inner.getHeight() >= 14;
        destinationDetailLabel.setVisible (showDetail);
        destinationDetailLabel.setBounds (showDetail ? inner.removeFromTop (18)
                                                     : juce::Rectangle<int>());
    }
}

void AudienceEditor::applyUdpPortFromEditor()
{
    const auto text = portEditor.getText().trim();
    const bool digitsOnly = text.isNotEmpty() && text.containsOnly ("0123456789");
    const int requestedPort = digitsOnly ? text.getIntValue() : 0;
    if (! digitsOnly || requestedPort < 1 || requestedPort > 65535)
    {
        portEditor.setColour (juce::TextEditor::outlineColourId, cm::red);
        portEditor.setDescription ("Invalid UDP port. Enter a number from 1 to 65535.");
        oscPathLabel.setText ("Invalid port (enter 1-65535)", juce::dontSendNotification);
        oscPathLabel.setColour (juce::Label::textColourId, cm::red);
        oscPathLabel.setTitle ("UDP port validation error");
        oscPathLabel.setDescription ("The UDP port is invalid. Enter a number from 1 to 65535.");
        portEditor.grabKeyboardFocus();
        portEditor.selectAll();
        repaint (oscCardBounds);
        return;
    }

    proc.setUdpPort (requestedPort);
    portEditorDirty = false;
    portEditor.setColour (juce::TextEditor::outlineColourId, cm::line);
    lastUdpPort = requestedPort;
    restoreUdpPortEditor();

    // The virtual endpoint name includes the port. This scan is intentionally
    // tied to an explicit user action, never paint/resized/timer.
    refreshMidiOutputCombo();
    updateLiveText();
}

void AudienceEditor::restoreUdpPortEditor()
{
    updatingPortEditor = true;
    lastUdpPort = proc.getUdpPort();
    portEditor.setText (juce::String (lastUdpPort), false);
    updatingPortEditor = false;
    portEditorDirty = false;
    portApplyButton.setEnabled (false);
    portEditor.setColour (juce::TextEditor::outlineColourId, cm::line);
    portEditor.setDescription ("UDP port for this Cosmic Microwave instance. Enter a number from 1 to 65535 and press Return or Apply.");
    oscPathLabel.setText ("/cs/{zone}/{source}/finger0  |  /u  /v  /on  |  0-1",
                          juce::dontSendNotification);
    oscPathLabel.setColour (juce::Label::textColourId, cm::textDim);
    oscPathLabel.setTitle ("OSC address format");
    oscPathLabel.setDescription ("Single-touch OSC contract. Only finger0 is accepted; u and v are normalized from zero to one, on 1 starts a touch, and on 0 releases it. A live touch is safety-released after three seconds without valid updates.");
    oscPathLabel.setTooltip (oscPathLabel.getDescription());
    repaint (oscCardBounds);
}

void AudienceEditor::refreshMidiOutputCombo()
{
    const auto options = proc.getMidiOutputOptions();
    const int requestedIndex = proc.getResolvedMidiOutputOptionIndex();

    refreshingDestination = true;
    destinationCombo.clear (juce::dontSendNotification);
    midiOutputOptions = options;
    for (int index = 0; index < midiOutputOptions.size(); ++index)
        destinationCombo.addItem (midiOutputOptions[index], index + 1);
    if (requestedIndex >= 0 && requestedIndex < midiOutputOptions.size())
    {
        destinationRouteUnresolved = false;
        destinationCombo.setSelectedItemIndex (requestedIndex, juce::dontSendNotification);
    }
    else
    {
        destinationRouteUnresolved = true;
        destinationCombo.setSelectedItemIndex (-1, juce::dontSendNotification);
        destinationCombo.setText ("Unavailable: " + proc.getMidiOutputStatus(),
                                  juce::dontSendNotification);
    }
    lastMidiOutputRouteRevision = proc.getMidiOutputRouteRevision();
    refreshingDestination = false;
}

void AudienceEditor::updateModeVisibility()
{
    const int midiType = cm::choiceValue (proc.apvts, "midiOutputType");
    const bool atomicPitch = cm::choiceValue (proc.apvts, "pitchSystem") == 1;
    const int selectedTimeMode = timeModeCombo.getSelectedItemIndex();
    const int timeMode = selectedTimeMode >= 0
                           ? selectedTimeMode
                           : cm::choiceValue (proc.apvts, "timeMode");
    const bool timed = timeMode != 0;
    const int selectedClock = clockSourceCombo.getSelectedItemIndex();
    const bool internalClock = selectedClock >= 0
                                 ? selectedClock == 1
                                 : cm::choiceValue (proc.apvts, "clockSource") == 1;
    const bool adaptive = cm::choiceValue (proc.apvts, "crowdGovernorEnabled") != 0;
    const bool normal = midiType == 1;
    const bool mpe = midiType == 2;
    const bool fixedChannel = normal
                           && cm::choiceValue (proc.apvts, "normalMidiRoutingMode") == 0;
    const int visibilityKey = (adaptive ? 100000 : 0)
                            + timeMode * 10000
                            + (internalClock ? 1000 : 0)
                            + (atomicPitch ? 100 : 0)
                            + midiType * 10 + (fixedChannel ? 1 : 0);

    scaleLabel.setVisible (! atomicPitch);
    scaleCombo.setVisible (! atomicPitch);
    atomicElementLabel.setVisible (atomicPitch);
    atomicElementCombo.setVisible (atomicPitch);
    atomicModeLabel.setVisible (atomicPitch);
    atomicModeCombo.setVisible (atomicPitch);

    internalBpmLabel.setVisible (timed && internalClock);
    internalBpmSlider.setVisible (timed && internalClock);

    governorModeButton.setButtonText (adaptive ? "ADAPTIVE" : "MANUAL");
    governorModeButton.setColour (juce::TextButton::textColourOffId,
                                  adaptive ? cm::cyan : cm::textMuted);

    // Flow is intentionally direct. Keep the timing configuration visible as
    // a stable layout, but make it unmistakably unavailable until Grid or
    // Ensemble is selected (and remove disabled controls from keyboard use).
    for (auto* component : { static_cast<juce::Component*> (&clockSourceCombo),
                             static_cast<juce::Component*> (&gridDivisionCombo),
                             static_cast<juce::Component*> (&maxAttacksSlider),
                             static_cast<juce::Component*> (&maxActiveVoicesSlider),
                             static_cast<juce::Component*> (&gatePercentSlider),
                             static_cast<juce::Component*> (&temporalSpreadCombo),
                             static_cast<juce::Component*> (&clockSourceLabel),
                             static_cast<juce::Component*> (&gridDivisionLabel),
                             static_cast<juce::Component*> (&maxAttacksLabel),
                             static_cast<juce::Component*> (&maxActiveVoicesLabel),
                             static_cast<juce::Component*> (&gatePercentLabel),
                             static_cast<juce::Component*> (&temporalSpreadLabel) })
        component->setEnabled (timed);

    maxAttacksSlider.setVisible (! adaptive);
    maxActiveVoicesSlider.setVisible (! adaptive);
    temporalSpreadCombo.setVisible (! adaptive);
    governorAttacksValue.setVisible (adaptive);
    governorActiveVoicesValue.setVisible (adaptive);
    governorSpreadValue.setVisible (adaptive);
    governorAttacksValue.setEnabled (timed);
    governorActiveVoicesValue.setEnabled (timed);
    governorSpreadValue.setEnabled (timed);

    normalRoutingLabel.setVisible (normal);
    normalRoutingCombo.setVisible (normal);
    normalChannelLabel.setVisible (fixedChannel);
    normalChannelCombo.setVisible (fixedChannel);

    mpeZoneLabel.setVisible (mpe);
    mpeZoneCombo.setVisible (mpe);
    mpeBendRangeLabel.setVisible (mpe);
    mpeBendRangeCombo.setVisible (mpe);
    mpePitchModeLabel.setVisible (mpe);
    mpePitchModeCombo.setVisible (mpe);
    mpeSetupButton.setVisible (mpe);

    if (lastVisibilityKey != visibilityKey)
    {
        lastVisibilityKey = visibilityKey;
        resized();
        repaint (timeCardBounds.getUnion (pitchCardBounds).getUnion (midiCardBounds));
    }
}

void AudienceEditor::updateLiveText()
{
    const int activeSources = proc.audienceModel.getActiveSourceCount();
    const int activeFingers = proc.audienceModel.getActiveFingerCount();
    const int notesSent = proc.getMidiNotesSent();
    const int mpeVoices = proc.getActiveMpeVoices();

    activeSourcesValue.setText (juce::String (activeSources), juce::dontSendNotification);
    activeFingersValue.setText (juce::String (activeFingers), juce::dontSendNotification);
    notesSentValue.setText (cm::compactCount (notesSent), juce::dontSendNotification);
    mpeVoicesValue.setText (juce::String (mpeVoices), juce::dontSendNotification);
    activeSourcesValue.setDescription (juce::String (activeSources) + " active OSC sources.");
    activeFingersValue.setDescription (juce::String (activeFingers) + " active finger0 touches.");
    notesSentValue.setDescription (juce::String (notesSent) + " MIDI note attacks sent.");
    mpeVoicesValue.setDescription (juce::String (mpeVoices) + " active MPE voices.");

    if (cm::choiceValue (proc.apvts, "pitchSystem") == 1)
    {
        const int element = cm::choiceValue (proc.apvts, "spectralElement");
        const int mode = cm::choiceValue (proc.apvts, "atomicScaleMode");
        const auto referenceNm = proc.getSelectedAtomicReferenceWavelengthNm();
        const auto detail = proc.getAtomicElementName (element) + " ("
                          + proc.getAtomicElementSymbol (element) + ") · "
                          + proc.getAtomicModeName (mode) + " · "
                          + juce::String (proc.getSelectedAtomicDegreeCount()) + " degrees · "
                          + juce::String (referenceNm, 3) + " nm reference. "
                            "MPE uses exact frequency; Normal MIDI uses the nearest note.";
        pitchSystemCombo.setTooltip (detail);
        pitchSystemCombo.setDescription (detail);
    }
    else
    {
        pitchSystemCombo.setTooltip ("Seven fixed 12-TET tonal maps. Horizontal OSC position selects the scale step.");
        pitchSystemCombo.setDescription (pitchSystemCombo.getTooltip());
    }

    static constexpr const char* divisions[] { "1/4", "1/8", "1/16", "1/32" };
    const int selectedTimeClock = clockSourceCombo.getSelectedItemIndex();
    const bool hostClock = selectedTimeClock >= 0
                             ? selectedTimeClock == 0
                             : cm::choiceValue (proc.apvts, "clockSource") == 0;
    const int selectedDivision = gridDivisionCombo.getSelectedItemIndex();
    const int divisionIndex = juce::jlimit (0, 3,
                                            selectedDivision >= 0
                                              ? selectedDivision
                                              : cm::choiceValue (proc.apvts, "gridDivision"));
    const int selectedMode = timeModeCombo.getSelectedItemIndex();
    const int liveTimeMode = selectedMode >= 0
                               ? selectedMode
                               : cm::choiceValue (proc.apvts, "timeMode");
    const bool governorAdaptive = cm::choiceValue (proc.apvts, "crowdGovernorEnabled") != 0;
    const int observedCrowd = juce::jmax (0, proc.getGovernorObservedDensity());
    const int effectiveAttacks = juce::jmax (1, proc.getGovernorEffectiveAttacksPerStep());
    const int effectiveActiveLimit = juce::jmax (1, proc.getGovernorEffectiveActiveVoices());
    const int effectiveSpread = juce::jmax (1, proc.getGovernorEffectiveSpreadSlots());
    const auto observedSourceWord = observedCrowd == 1 ? " source" : " sources";
    const auto spreadStepWord = effectiveSpread == 1 ? " grid step" : " grid steps";
    const bool clockLocked = proc.getTimeFieldClockLocked();
    const int pending = proc.getTimeFieldPending();
    const int scheduledActive = proc.getTimeFieldActive();
    const auto merged = proc.getTimeFieldMerged();
    const auto bpm = juce::String (proc.getTimeFieldBpm(), 0);
    const auto grid = juce::String (divisions[divisionIndex]);

    juce::String primaryTimeStatus;
    juce::String clockDescription;
    if (liveTimeMode == 0)
    {
        primaryTimeStatus = "FLOW  /  DIRECT";
        clockDescription = "Flow mode sends touch attacks directly. Timing controls are bypassed.";
    }
    else if (hostClock && clockLocked)
    {
        primaryTimeStatus = "HOST LOCK  /  " + bpm + " BPM  /  " + grid;
        clockDescription = "Time Field is locked to the running host transport at "
                         + bpm + " BPM on the " + grid + " grid.";
    }
    else if (hostClock)
    {
        // Host transport may be stopped or absent in Standalone. Scheduling is
        // still active: every instance shares the monotonic-seconds fallback.
        primaryTimeStatus = "FREE CLOCK  /  " + bpm + " BPM  /  " + grid;
        clockDescription = "Host transport is stopped or unavailable. Time Field remains active on the shared monotonic clock at "
                         + bpm + " BPM on the " + grid + " grid.";
    }
    else
    {
        primaryTimeStatus = "INTERNAL  /  " + bpm + " BPM  /  " + grid;
        clockDescription = "Time Field is running from its internal clock at "
                         + bpm + " BPM on the " + grid + " grid.";
    }

    timeStatusLabel.setText (primaryTimeStatus, juce::dontSendNotification);
    timeStatusLabel.setTooltip (clockDescription);
    timeStatusLabel.setDescription (clockDescription);
    timeStatusLabel.setColour (juce::Label::textColourId,
                               liveTimeMode != 0 && hostClock && ! clockLocked
                                 ? cm::amber : cm::green);

    governorAttacksValue.setText ("AUTO  /  " + juce::String (effectiveAttacks),
                                  juce::dontSendNotification);
    governorActiveVoicesValue.setText ("AUTO  /  " + juce::String (effectiveActiveLimit),
                                       juce::dontSendNotification);
    governorSpreadValue.setText ("AUTO  /  " + juce::String (effectiveSpread),
                                 juce::dontSendNotification);
    governorAttacksValue.setDescription (
        "Adaptive Crowd Governor currently allows " + juce::String (effectiveAttacks)
        + " new attacks per grid step for an observed crowd density of "
        + juce::String (observedCrowd) + observedSourceWord + ".");
    governorActiveVoicesValue.setDescription (
        "Adaptive Crowd Governor currently allows " + juce::String (effectiveActiveLimit)
        + " simultaneous scheduled voices for an observed crowd density of "
        + juce::String (observedCrowd) + observedSourceWord + ".");
    governorSpreadValue.setDescription (
        "Adaptive Crowd Governor currently distributes attacks across "
        + juce::String (effectiveSpread) + spreadStepWord + " for "
        + juce::String (observedCrowd) + observedSourceWord + ".");
    const auto governorDescription = governorAdaptive
        ? (liveTimeMode == 0
             ? "Adaptive Crowd Governor is prepared but bypassed in Flow mode. Select Grid or Ensemble to apply crowd-aware limits."
             : "Adaptive Crowd Governor is active and derives attacks, active voices and temporal spread from observed crowd density.")
        : "Manual uses the saved attacks, active-voice and temporal-spread limits.";
    governorModeButton.setDescription (governorDescription);
    governorModeButton.setTooltip (governorDescription);

    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    if (merged < lastTimeFieldMerged)
        mergeActivityUntilMs = 0.0;
    else if (merged > lastTimeFieldMerged)
        mergeActivityUntilMs = nowMs + 1400.0;
    lastTimeFieldMerged = merged;

    // Adaptive telemetry is authoritative only while the Governor is selected.
    // Manual mode continues to reflect the saved/automated controls, including
    // the fifteen-member MPE ceiling.
    const int manualActiveLimit = cm::choiceValue (proc.apvts, "midiOutputType") == 2
                                ? juce::jmin (15, juce::jmax (
                                      1, (int) std::lround (maxActiveVoicesSlider.getValue())))
                                : juce::jmax (
                                      1, (int) std::lround (maxActiveVoicesSlider.getValue()));
    const int activeLimit = governorAdaptive ? effectiveActiveLimit : manualActiveLimit;
    const int attacksPerStep = governorAdaptive
                                 ? effectiveAttacks
                                 : juce::jmax (1, (int) std::lround (maxAttacksSlider.getValue()));
    const int spreadSteps = governorAdaptive
                              ? effectiveSpread
                              : juce::jmax (1, temporalSpreadCombo.getText().getIntValue());
    const int oneSpreadCapacity = attacksPerStep * spreadSteps;
    const bool atActiveLimit = liveTimeMode != 0 && pending > 0
                            && scheduledActive >= activeLimit;
    const bool queuePressure = liveTimeMode != 0 && pending > oneSpreadCapacity;
    const bool highLoad = atActiveLimit || queuePressure;
    const bool mergeActivity = liveTimeMode != 0 && nowMs < mergeActivityUntilMs;
    const auto queueState = "PENDING " + juce::String (pending)
                          + "  /  ACTIVE " + juce::String (scheduledActive)
                          + "  /  MERGED " + juce::String (merged);
    auto loadDescription = liveTimeMode == 0
                         ? "Direct signal path with " + juce::String (scheduledActive)
                             + " active touches."
                         : queueState
                             + ". Merged counts same-step attack collisions combined safely; it is not a dropped-note count."
                             + (atActiveLimit
                                  ? " The active-voice limit is currently full and attacks remain queued."
                                  : queuePressure
                                      ? " High load: the pending queue exceeds one selected temporal-spread window."
                                  : mergeActivity
                                      ? " Crowd attacks were consolidated during the latest scheduling window."
                                      : " Scheduler load is within the selected limits.");
    if (governorAdaptive)
        loadDescription += " Adaptive Crowd Governor sees an observed crowd density of "
                         + juce::String (observedCrowd)
                         + observedSourceWord + " and currently applies "
                         + juce::String (effectiveAttacks) + " attacks per step, "
                         + juce::String (effectiveActiveLimit) + " active voices and "
                         + juce::String (effectiveSpread) + spreadStepWord + "."
                         + (liveTimeMode == 0 ? " These limits are bypassed in Flow mode." : "");
    timeTelemetryLabel.setText (liveTimeMode == 0
                                  ? governorAdaptive
                                      ? "GOVERNOR BYPASS  /  CROWD " + juce::String (observedCrowd)
                                          + "  /  ACTIVE " + juce::String (scheduledActive)
                                      : "ATTACKS PASS THROUGH  /  ACTIVE "
                                          + juce::String (scheduledActive)
                                  : governorAdaptive
                                      ? "ADAPT  /  CROWD " + juce::String (observedCrowd)
                                          + "  /  P" + juce::String (pending)
                                          + "  A" + juce::String (scheduledActive)
                                          + "  M" + juce::String (merged)
                                  : highLoad
                                      ? "HIGH LOAD  /  P" + juce::String (pending)
                                          + "  /  A" + juce::String (scheduledActive)
                                          + "  /  M" + juce::String (merged)
                                      : queueState,
                                juce::dontSendNotification);
    timeTelemetryLabel.setTooltip (loadDescription);
    timeTelemetryLabel.setDescription (loadDescription);
    timeTelemetryLabel.setColour (juce::Label::textColourId,
                                  liveTimeMode == 0 && governorAdaptive ? cm::textDim
                                  : highLoad || mergeActivity ? cm::amber
                                  : governorAdaptive ? cm::cyan
                                  : pending > 0 ? cm::violet : cm::textMuted);

    const bool listening = proc.osc.isRunning() && proc.osc.isReceiving();
    const auto messageCount = proc.osc.getValidMessageCount();
    const auto malformedCount = proc.osc.getMalformedDatagramCount();
    const auto age = proc.osc.getLastValidMessageAgeMs();
    juce::String oscText;
    juce::String oscDetail;
    juce::Colour oscColour;
    if (! listening)
    {
        oscDetail = proc.osc.oscStatus().isNotEmpty() ? proc.osc.oscStatus() : "OSC receiver stopped";
        oscText = "ERR / " + oscDetail;
        oscColour = cm::red;
    }
    else if (messageCount == 0 && malformedCount > 0)
    {
        oscText = "BAD OSC / " + juce::String ((int) malformedCount);
        oscDetail = "Listening on UDP " + juce::String (proc.getUdpPort())
                  + " | " + juce::String (malformedCount)
                  + " malformed OSC datagrams were rejected; no valid message has arrived";
        oscColour = cm::amber;
    }
    else if (messageCount == 0)
    {
        oscText = "WAIT / UDP " + juce::String (proc.getUdpPort());
        oscDetail = "Listening on UDP " + juce::String (proc.getUdpPort())
                  + " and waiting for valid OSC data";
        oscColour = cm::amber;
    }
    else if (age <= 1500u)
    {
        const auto messageSummary = messageCount < 1000u
                                  ? juce::String ((int) messageCount)
                                  : juce::String ((double) messageCount / 1000.0, 1) + "k";
        oscText = "RX / " + messageSummary + " MSG";
        oscDetail = "Receiving on UDP " + juce::String (proc.getUdpPort())
                  + " | " + juce::String (messageCount) + " valid messages"
                  + (malformedCount > 0
                       ? " | " + juce::String (malformedCount)
                           + " malformed datagrams safely rejected"
                       : juce::String());
        oscColour = cm::green;
    }
    else
    {
        oscText = "STALE / " + juce::String ((double) age / 1000.0, 1) + " s";
        oscDetail = "Listening on UDP " + juce::String (proc.getUdpPort())
                  + " | last valid message "
                  + juce::String ((double) age / 1000.0, 1) + " seconds ago"
                  + (malformedCount > 0
                       ? " | " + juce::String (malformedCount)
                           + " malformed datagrams safely rejected"
                       : juce::String());
        oscColour = cm::textMuted;
    }
    oscStatusLabel.setText (juce::String::fromUTF8 ("\xe2\x80\xa2  ") + oscText,
                            juce::dontSendNotification);
    oscStatusLabel.setColour (juce::Label::textColourId, oscColour);
    const auto oscSafetyDetail = oscDetail
                               + ". Live touches are safety-released after 3 seconds without valid OSC updates.";
    oscStatusLabel.setTooltip (oscSafetyDetail);
    oscStatusLabel.setDescription (oscSafetyDetail);

    const int midiType = cm::choiceValue (proc.apvts, "midiOutputType");
    if (midiType == 1)
    {
        const int routingMode = cm::choiceValue (proc.apvts, "normalMidiRoutingMode");
        routingSummaryLabel.setText (routingMode == 1 ? "Source ID  ->  MIDI Ch 1-16"
                                                       : "All sources  ->  Fixed channel",
                                     juce::dontSendNotification);
        if (routingMode == 1)
        {
            routingDetailLabel.setText ("1:1  /  ...  /  16:16  /  17:1  /  0:16  /  note pairs stay together",
                                        juce::dontSendNotification);
        }
        else
        {
            const int channel = cm::choiceValue (proc.apvts, "normalMidiChannel") + 1;
            routingDetailLabel.setText ("Every source/finger0 uses MIDI Channel " + juce::String (channel),
                                        juce::dontSendNotification);
        }
    }
    else if (midiType == 2)
    {
        const bool upper = cm::choiceValue (proc.apvts, "mpeZone") == 1;
        routingSummaryLabel.setText ("Source finger0  ->  MPE voices", juce::dontSendNotification);
        routingDetailLabel.setText (upper ? "Upper zone  /  master 16  /  members 1-15"
                                               : "Lower zone  /  master 1  /  members 2-16",
                                    juce::dontSendNotification);
    }
    else
    {
        routingSummaryLabel.setText ("MIDI output is off", juce::dontSendNotification);
        routingDetailLabel.setText ("OSC monitoring remains active", juce::dontSendNotification);
    }

    const auto observedZones = proc.osc.getObservedZoneMask();
    const int observedZoneCount = cm::zoneCount (observedZones);
    zoneStatusLabel.setText (cm::zonesFromMask (observedZones)
                             + "  /  " + juce::String (activeSources) + " sources  /  "
                             + juce::String (activeFingers) + " touches",
                             juce::dontSendNotification);
    const bool oscFresh = listening && messageCount > 0 && age <= 1500u;
    zoneStatusLabel.setColour (juce::Label::textColourId,
                               observedZoneCount > 1 ? cm::amber
                               : observedZones == 0 ? cm::textDim
                               : oscFresh ? cm::green : cm::textMuted);
    const auto zoneDescription = observedZoneCount > 1
                               ? "Mixed-zone input detected on this UDP port. Route one zone per Cosmic Microwave instance."
                               : observedZoneCount == 1
                                   ? "One OSC zone is observed on this UDP port."
                                   : "No valid OSC zone has been observed yet.";
    zoneStatusLabel.setTooltip (zoneDescription);
    zoneStatusLabel.setDescription (zoneDescription);

    simMoveButton.setToggleState (proc.simulator.isRandomMovementOn(), juce::dontSendNotification);

    const auto currentDestinationStatus = proc.getMidiOutputStatus();
    if (proc.getMidiOutputRouteRevision() != lastMidiOutputRouteRevision)
        refreshMidiOutputCombo();
    destinationStatusLabel.setText (juce::String::fromUTF8 ("\xe2\x80\xa2  ")
                                     + (destinationRouteUnresolved
                                          ? "Unavailable saved MIDI output"
                                          : currentDestinationStatus),
                                    juce::dontSendNotification);
    const bool destinationOkay = ! destinationRouteUnresolved
                              && ! currentDestinationStatus.containsIgnoreCase ("failed")
                              && ! currentDestinationStatus.containsIgnoreCase ("unavailable")
                              && ! currentDestinationStatus.containsIgnoreCase ("error");
    destinationStatusLabel.setColour (juce::Label::textColourId,
                                      destinationOkay ? cm::green : cm::red);
    destinationDetailLabel.setText (proc.getMidiOutputDescription(), juce::dontSendNotification);
}

void AudienceEditor::timerCallback()
{
    // State recall may replace the port while the editor remains open. Do not
    // trample an in-progress edit; Escape always restores immediately.
    const int statePort = proc.getUdpPort();
    if (! portEditorDirty && statePort != lastUdpPort)
    {
        restoreUdpPortEditor();
        refreshMidiOutputCombo();
    }

    updateModeVisibility();
    updateLiveText();
    sourceMap->repaint();
}
