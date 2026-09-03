#include "PluginEditor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

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
    const juce::Colour textLow      { 0xff4a5462 };
    const juce::Colour cyan         { 0xff58d6ff };
    const juce::Colour green        { 0xff42d6a5 };
    const juce::Colour violet       { 0xff9b8cff };
    const juce::Colour amber        { 0xffefbd5c };
    const juce::Colour red          { 0xffff647a };
    const juce::Colour selectedInk  { 0xff06202e };
    const juce::Colour matrixIdle   { 0xff0c1119 };
    const juce::Colour spectrumBg   { 0xff05080c };

    static juce::Colour channelColour (int channel) noexcept
    {
        const auto index = juce::jlimit (0, 15, channel - 1);
        const auto hue = std::fmod (205.0f + (float) index * 21.0f, 360.0f) / 360.0f;
        return juce::Colour::fromHSV (hue, 0.60f, 0.82f, 1.0f);
    }

    static void drawCard (juce::Graphics& g, juce::Rectangle<int> bounds,
                          const juce::String& title, const juce::String& tag = {})
    {
        if (bounds.isEmpty())
            return;

        auto b = bounds.toFloat();
        g.setColour (card);
        g.fillRoundedRectangle (b, 11.0f);
        g.setColour (lineSoft);
        g.drawRoundedRectangle (b.reduced (0.5f), 11.0f, 1.0f);

        g.setColour (lineSoft);
        g.fillRect (bounds.withY (bounds.getY() + 34).withHeight (1));

        g.setColour (textMuted.withAlpha (0.82f));
        g.setFont (juce::Font (juce::FontOptions (10.8f).withStyle ("bold")));
        g.drawText (title, bounds.reduced (15, 0).removeFromTop (35),
                    juce::Justification::centredLeft, false);

        if (tag.isNotEmpty())
        {
            auto tagBounds = bounds.reduced (15, 0).removeFromTop (35).removeFromRight (190);
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
            if (button.getComponentID() == "segmentButton")
            {
                auto fill = button.getToggleState() ? cyan : juce::Colours::transparentBlack;
                if (! button.isEnabled())
                    fill = fill.withMultipliedAlpha (0.30f);
                else if (down)
                    fill = fill.interpolatedWith (text, 0.12f);
                else if (highlighted && ! button.getToggleState())
                    fill = cardRaised.withAlpha (0.72f);

                if (! fill.isTransparent())
                {
                    g.setColour (fill);
                    g.fillRect (button.getLocalBounds());
                }

                if (button.hasKeyboardFocus (true))
                {
                    g.setColour ((button.getToggleState() ? selectedInk : cyan).withAlpha (0.96f));
                    g.drawRoundedRectangle (bounds.reduced (1.5f), 3.5f, 1.4f);
                }
                return;
            }

            if (button.getComponentID() == "pageTab")
            {
                auto fill = button.getToggleState()
                    ? cyan.withAlpha (0.12f) : card.withAlpha (0.70f);
                if (down)
                    fill = fill.brighter (0.08f);
                else if (highlighted)
                    fill = fill.brighter (0.04f);
                g.setColour (fill.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.34f));
                g.fillRoundedRectangle (bounds, 6.0f);
                g.setColour ((button.getToggleState() ? cyan : line)
                                 .withAlpha (button.hasKeyboardFocus (true) ? 0.95f : 0.72f));
                g.drawRoundedRectangle (bounds, 6.0f,
                                        button.hasKeyboardFocus (true) ? 1.2f : 0.8f);
                return;
            }

            if (button.getComponentID() == "panicButton")
            {
                auto fill = juce::Colour (0xff1c1016);
                if (down)
                    fill = red.withAlpha (0.70f);
                else if (highlighted)
                    fill = fill.interpolatedWith (red, 0.10f);
                g.setColour (fill);
                g.fillRoundedRectangle (bounds, 6.0f);
                g.setColour (red.withAlpha (button.hasKeyboardFocus (true) ? 0.96f : 0.72f));
                g.drawRoundedRectangle (bounds, 6.0f,
                                        button.hasKeyboardFocus (true) ? 1.2f : 0.8f);
                return;
            }

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
            g.fillEllipse (sliderPos - 6.5f, centreY - 6.5f, 13.0f, 13.0f);
            g.setColour (background.withAlpha (0.92f * enabledAlpha));
            g.drawEllipse (sliderPos - 6.5f, centreY - 6.5f, 13.0f, 13.0f, 1.5f);
        }

        void drawLabel (juce::Graphics& g, juce::Label& label) override
        {
            const auto bounds = label.getLocalBounds().toFloat().reduced (0.5f);
            const auto backgroundColour = label.findColour (juce::Label::backgroundColourId);
            const auto outlineColour = label.findColour (juce::Label::outlineColourId);

            if (! backgroundColour.isTransparent())
            {
                g.setColour (backgroundColour);
                g.fillRoundedRectangle (bounds, 5.0f);
            }

            if (! outlineColour.isTransparent())
            {
                g.setColour (outlineColour);
                g.drawRoundedRectangle (bounds, 5.0f, 0.8f);
            }

            if (! label.isBeingEdited())
            {
                auto textArea = label.getBorderSize().subtractedFrom (label.getLocalBounds());
                g.setColour (label.findColour (juce::Label::textColourId)
                                  .withMultipliedAlpha (label.isEnabled() ? 1.0f : 0.34f));
                g.setFont (label.getFont());
                g.drawFittedText (label.getText(), textArea, label.getJustificationType(),
                                  juce::jmax (1, (int) ((float) textArea.getHeight()
                                                       / juce::jmax (1.0f, label.getFont().getHeight()))),
                                  label.getMinimumHorizontalScale());
            }
        }

        void fillTextEditorBackground (juce::Graphics& g, int width, int height,
                                       juce::TextEditor& editor) override
        {
            g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
            g.fillRoundedRectangle (juce::Rectangle<float> (0.5f, 0.5f,
                                                            (float) width - 1.0f,
                                                            (float) height - 1.0f), 6.0f);
        }

        void drawTextEditorOutline (juce::Graphics& g, int width, int height,
                                    juce::TextEditor& editor) override
        {
            if (! editor.isEnabled())
                return;

            const auto focused = editor.hasKeyboardFocus (true);
            g.setColour (editor.findColour (focused ? juce::TextEditor::focusedOutlineColourId
                                                    : juce::TextEditor::outlineColourId));
            g.drawRoundedRectangle (juce::Rectangle<float> (0.5f, 0.5f,
                                                            (float) width - 1.0f,
                                                            (float) height - 1.0f),
                                    6.0f, focused ? 1.2f : 0.8f);
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
// Focusable proxy for an APVTS-attached ComboBox. Keeping the ComboBox as the
// parameter owner preserves JUCE's gestures, automation updates and choice
// indices while presenting the compact segmented language from the reference.
class SegmentedChoice final : public juce::Component,
                              private juce::ComboBox::Listener
{
public:
    SegmentedChoice (juce::ComboBox& targetToUse, const juce::StringArray& labels,
                     const juce::String& accessibleTitle)
        : target (targetToUse)
    {
        setTitle (accessibleTitle);
        setDescription ("Choose " + accessibleTitle.toLowerCase());
        setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);

        for (int index = 0; index < labels.size(); ++index)
        {
            auto button = std::make_unique<juce::TextButton> (labels[index]);
            button->setComponentID ("segmentButton");
            button->setRadioGroupId (1, juce::dontSendNotification);
            button->setClickingTogglesState (false);
            button->setColour (juce::TextButton::buttonColourId,
                               juce::Colours::transparentBlack);
            button->setColour (juce::TextButton::buttonOnColourId, cm::cyan);
            button->setColour (juce::TextButton::textColourOffId, cm::textMuted);
            button->setColour (juce::TextButton::textColourOnId, cm::selectedInk);
            button->setTitle (accessibleTitle + ": " + labels[index]);
            button->setDescription ("Select " + labels[index] + " for "
                                    + accessibleTitle.toLowerCase());
            button->setTooltip (button->getDescription());
            button->setWantsKeyboardFocus (true);
            button->onClick = [this, index]
            {
                target.setSelectedItemIndex (index, juce::sendNotificationSync);
                syncFromTarget();
            };
            addAndMakeVisible (*button);
            buttons.push_back (std::move (button));
        }

        target.addListener (this);
        syncFromTarget();
    }

    ~SegmentedChoice() override
    {
        target.removeListener (this);
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (cm::card.withMultipliedAlpha (isEnabled() ? 1.0f : 0.34f));
        g.fillRoundedRectangle (bounds, 7.0f);
        g.setColour (cm::line.withMultipliedAlpha (isEnabled() ? 1.0f : 0.34f));
        g.drawRoundedRectangle (bounds, 7.0f, 0.9f);

        if (buttons.size() > 1)
        {
            const auto segmentWidth = (float) getWidth() / (float) buttons.size();
            for (size_t index = 1; index < buttons.size(); ++index)
                g.fillRect (segmentWidth * (float) index - 0.5f, 1.0f,
                            1.0f, (float) getHeight() - 2.0f);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (1);
        for (size_t index = 0; index < buttons.size(); ++index)
        {
            const int width = index + 1 == buttons.size()
                                ? area.getWidth()
                                : getWidth() / (int) buttons.size();
            buttons[index]->setBounds (area.removeFromLeft (width));
        }
    }

    void enablementChanged() override
    {
        for (auto& button : buttons)
            button->setEnabled (isEnabled());
        repaint();
    }

    void syncFromTarget()
    {
        const auto selected = target.getSelectedItemIndex();
        for (size_t index = 0; index < buttons.size(); ++index)
            buttons[index]->setToggleState ((int) index == selected,
                                            juce::dontSendNotification);
        repaint();
    }

private:
    void comboBoxChanged (juce::ComboBox*) override
    {
        syncFromTarget();
    }

    juce::ComboBox& target;
    std::vector<std::unique_ptr<juce::TextButton>> buttons;
};

//==============================================================================
// A truthful, side-effect-free visualisation of the currently selected pitch
// map. Atomic mode draws the immutable catalog degrees in cents; Tonal mode
// draws the actual MidiPitchMap degrees. No runtime files or network fonts are
// involved, so painting remains deterministic inside a host.
class PitchSpectrumDisplay final : public juce::Component
{
public:
    explicit PitchSpectrumDisplay (AudienceProcessor& processorToUse)
        : proc (processorToUse)
    {
        setTitle ("Pitch map spectrum");
        setDescription ("Visual preview of the selected Tonal or Atomic pitch degrees.");
        setInterceptsMouseClicks (false, false);
        setWantsKeyboardFocus (false);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        if (bounds.getWidth() < 40 || bounds.getHeight() < 38)
            return;

        auto meta = bounds.removeFromBottom (18);
        auto plot = bounds;
        auto axis = plot.removeFromBottom (15);

        g.setColour (cm::spectrumBg);
        g.fillRoundedRectangle (bounds.toFloat(), 7.0f);
        g.setColour (cm::lineSoft);
        g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 7.0f, 0.9f);

        const bool atomic = cm::choiceValue (proc.apvts, "pitchSystem") == 1;
        juce::String metaLeft;
        juce::String metaRight;

        if (atomic)
        {
            juce::ColourGradient spectrum (juce::Colour (0xff704bd7),
                                           (float) plot.getX(), 0.0f,
                                           juce::Colour (0xffd75050),
                                           (float) plot.getRight(), 0.0f, false);
            spectrum.addColour (0.22, juce::Colour (0xff4f79d9));
            spectrum.addColour (0.42, juce::Colour (0xff4fc3d9));
            spectrum.addColour (0.60, juce::Colour (0xff4fd98f));
            spectrum.addColour (0.78, juce::Colour (0xffd9c34f));
            g.setGradientFill (spectrum);
            g.setOpacity (0.055f);
            g.fillRect (plot);
            g.setOpacity (1.0f);

            const int element = AtomicScaleCatalog::clampElementIndex (
                cm::choiceValue (proc.apvts, "spectralElement"));
            const int mode = AtomicScaleCatalog::clampModeIndex (
                cm::choiceValue (proc.apvts, "atomicScaleMode"));
            const auto& map = AtomicScaleCatalog::instance().getMap (element, mode);
            const int count = map.getDegreeCount();
            for (int index = 0; index < count; ++index)
            {
                const auto degree = map.getDegree (index);
                const float position = (float) juce::jlimit (0.0, 1199.999,
                                                            degree.cents) / 1200.0f;
                const float x = (float) plot.getX() + position * (float) plot.getWidth();
                const float salience = (float) juce::jlimit (0.0, 1.0, degree.weight);
                const float top = (float) plot.getY() + 5.0f
                                  + (1.0f - salience) * (float) plot.getHeight() * 0.16f;
                g.setColour (juce::Colour::fromHSV (0.73f - position * 0.73f,
                                                    0.72f, 0.92f, 0.88f));
                g.fillRoundedRectangle (x, top, index == 0 ? 2.8f : 1.8f,
                                        (float) plot.getBottom() - top - 2.0f, 0.9f);
            }

            metaLeft = proc.getAtomicElementSymbol (element).toUpperCase() + "  "
                       + proc.getAtomicElementName (element).toUpperCase()
                       + "  ·  " + juce::String (count) + " DEGREES";
            const auto reference = proc.getSelectedAtomicReferenceWavelengthNm();
            if (std::isfinite (reference) && reference > 0.0)
                metaRight = "λREF " + juce::String (reference, 2) + " NM  ·  ";
            metaRight += proc.getAtomicModeName (mode).toUpperCase();
        }
        else
        {
            const int root = juce::jlimit (0, 11, cm::choiceValue (proc.apvts, "scaleRoot"));
            const int octave = juce::jlimit (0, 6,
                                             cm::choiceValue (proc.apvts, "scaleRootOctave"));
            const int mode = juce::jlimit (0, MidiPitchMap::numScaleModes - 1,
                                           cm::choiceValue (proc.apvts, "scaleMode"));
            MidiPitchMap map;
            map.configure (root, octave, mode, 1);
            const int count = map.getScaleTableSize();
            const int rootMidi = map.getRootMidi();
            for (int index = 0; index < count; ++index)
            {
                const int semitone = juce::jlimit (0, 11, map.getScaleMidi (index) - rootMidi);
                const float position = (float) semitone / 12.0f;
                const float x = (float) plot.getX() + position * (float) plot.getWidth();
                g.setColour (index == 0 ? cm::cyan : cm::textDim);
                g.fillRoundedRectangle (x, (float) plot.getY() + 6.0f,
                                        index == 0 ? 2.8f : 1.8f,
                                        (float) plot.getHeight() - 8.0f, 0.9f);
            }

            static constexpr const char* rootNames[] {
                "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
            };
            metaLeft = juce::String (rootNames[root]) + juce::String (octave) + "  "
                       + juce::String (MidiPitchMap::scaleName (mode)).toUpperCase()
                       + "  ·  " + juce::String (count) + " DEGREES / OCT";
            metaRight = "12-TET";
        }

        g.setColour (cm::header.withAlpha (0.96f));
        g.fillRect (axis);
        g.setColour (cm::lineSoft);
        g.fillRect (axis.removeFromTop (1));
        g.setFont (juce::Font (juce::FontOptions (7.8f)));
        g.setColour (cm::textDim);
        g.drawText ("0 c", axis.removeFromLeft (44), juce::Justification::centredLeft, false);
        auto rightAxis = axis.removeFromRight (50);
        g.drawText ("1200 c", rightAxis, juce::Justification::centredRight, false);
        g.setColour (cm::textLow);
        g.drawText ("600", axis, juce::Justification::centred, false);

        g.setFont (juce::Font (juce::FontOptions (8.2f).withStyle ("bold")));
        g.setColour (cm::textMuted);
        g.drawFittedText (metaLeft, meta.removeFromLeft ((meta.getWidth() * 3) / 5),
                          juce::Justification::centredLeft, 1, 0.74f);
        g.setFont (juce::Font (juce::FontOptions (7.7f)));
        g.setColour (cm::textLow);
        g.drawFittedText (metaRight, meta, juce::Justification::centredRight, 1, 0.68f);
    }

private:
    AudienceProcessor& proc;
};

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
        setDescription ("Admitted OSC source IDs grouped by their stable sixteen-channel MIDI assignment.");
        setInterceptsMouseClicks (false, false);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        const int sourceCapacity = juce::jlimit (
            64, MidiAudienceModel::MAX_SOURCES, model.getSourceCapacity());
        const int sourcesPerChannel = sourceCapacity / 16;
        int activeSources = 0;
        int activeTouches = 0;
        std::array<MidiAudienceModel::SourceSnapshot,
                   MidiAudienceModel::MAX_SOURCES> snapshots {};
        for (int sourceId = 0; sourceId < sourceCapacity; ++sourceId)
        {
            auto& snapshot = snapshots[(size_t) sourceId];
            snapshot = model.getSourceSnapshot (sourceId);
            if (snapshot.active)
                ++activeSources;
            activeTouches += snapshot.activeFingerCount;
        }

        cm::drawCard (g, bounds, "SOURCE MATRIX",
                      juce::String (activeTouches) + " TOUCH  ·  "
                          + juce::String (activeSources) + " SRC  ·  "
                          + juce::String (sourceCapacity) + " CAP  ·  "
                          + juce::String (sourcesPerChannel) + " / CH");

        auto content = bounds.reduced (16, 0);
        content.removeFromTop (35);

        auto subtitle = content.removeFromTop (21);
        g.setColour (cm::textLow);
        g.setFont (juce::Font (juce::FontOptions (9.2f)));
        g.drawFittedText ("ID-locked source / MIDI channel map  ·  dot = finger U/V",
                          subtitle, juce::Justification::centredLeft, 1, 0.74f);

        auto footer = content.removeFromBottom (35);
        content.removeFromBottom (5);
        auto channelHeader = content.removeFromTop (22);
        auto grid = content;
        if (grid.getWidth() < 160 || grid.getHeight() < 128)
            return;

        const float columnWidth = (float) grid.getWidth() / 16.0f;
        const float rowHeight = (float) grid.getHeight()
                              / (float) sourcesPerChannel;
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
        for (int row = 4; row < sourcesPerChannel; row += 4)
        {
            const auto y = (float) grid.getY() + rowHeight * (float) row;
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
            const float headerDot = 5.5f;
            g.setColour (colour.withAlpha (0.92f));
            g.fillEllipse (heading.getCentreX() - 10.0f,
                           heading.getCentreY() - headerDot * 0.5f,
                           headerDot, headerDot);
            g.setColour (colour.withAlpha (0.90f));
            g.setFont (juce::Font (juce::FontOptions (8.4f).withStyle ("bold")));
            const auto channelText = midiChannel < 10 ? "0" + juce::String (midiChannel)
                                                       : juce::String (midiChannel);
            g.drawText (channelText, heading.translated (3.0f, 0.0f).reduced (1.0f, 0.0f),
                        juce::Justification::centred, false);

            for (int slot = 0; slot < sourcesPerChannel; ++slot)
            {
                // OSC source ids are [0,255], while the documented musical map
                // is 1->ch1 ... 17->ch1. Source 0 therefore occupies the final
                // ch16 cell after ids 16,32,...240.
                const int oneBasedId = midiChannel + slot * 16;
                const int sourceId = oneBasedId == sourceCapacity ? 0 : oneBasedId;
                const auto& snapshot = snapshots[(size_t) sourceId];

                auto cell = juce::Rectangle<float> (
                    (float) grid.getX() + columnWidth * (float) channelIndex,
                    (float) grid.getY() + rowHeight * (float) slot,
                    columnWidth, rowHeight).reduced (gap * 0.5f);

                const bool active = snapshot.active;
                g.setColour (active ? colour.withAlpha (0.22f) : cm::matrixIdle);
                g.fillRoundedRectangle (cell, juce::jmin (3.5f, rowHeight * 0.22f));

                g.setColour (active ? colour.withAlpha (0.88f)
                                    : cm::lineSoft.withAlpha (0.76f));
                g.drawRoundedRectangle (cell, juce::jmin (3.5f, rowHeight * 0.22f),
                                        active ? 0.9f : 0.65f);

                if (active)
                {
                    const float px = cell.getX()
                                   + (0.06f + juce::jlimit (0.0f, 1.0f, snapshot.x) * 0.76f)
                                         * cell.getWidth();
                    const float py = cell.getY()
                                   + (0.08f + (1.0f - juce::jlimit (0.0f, 1.0f, snapshot.y))
                                                  * 0.58f)
                                         * cell.getHeight();
                    const float dot = juce::jlimit (2.4f, 5.0f,
                                                   2.5f + 0.3f * (float) snapshot.activeFingerCount);
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

        g.setColour (cm::lineSoft);
        g.fillRect (footer.removeFromTop (1));
        footer.reduce (0, 6);

        auto drawLegend = [&g] (juce::Rectangle<int> item, juce::Colour fill,
                                juce::Colour border, const juce::String& label, bool dot)
        {
            auto swatch = item.removeFromLeft (16).withSizeKeepingCentre (10, 10).toFloat();
            g.setColour (fill);
            g.fillRoundedRectangle (swatch, 3.0f);
            g.setColour (border);
            g.drawRoundedRectangle (swatch.reduced (0.5f), 3.0f, 0.8f);
            if (dot)
            {
                g.setColour (cm::text);
                g.fillEllipse (swatch.getCentreX() - 1.6f, swatch.getCentreY() - 1.6f,
                               3.2f, 3.2f);
            }
            g.setColour (cm::textDim);
            g.setFont (juce::Font (juce::FontOptions (8.2f)));
            g.drawText (label, item, juce::Justification::centredLeft, false);
        };

        auto legend = footer.removeFromLeft (juce::jmin (315, footer.getWidth()));
        drawLegend (legend.removeFromLeft (70), cm::matrixIdle, cm::lineSoft,
                    "IDLE", false);
        drawLegend (legend.removeFromLeft (108), cm::cyan.withAlpha (0.11f),
                    cm::cyan.withAlpha (0.55f), "ACTIVE TOUCH", false);
        if (legend.getWidth() >= 100)
            drawLegend (legend, cm::cyan.withAlpha (0.28f), cm::cyan,
                        "U/V POSITION", true);

        if (footer.getWidth() >= 190)
        {
            g.setColour (cm::textLow);
            g.setFont (juce::Font (juce::FontOptions (7.8f)));
            g.drawFittedText ("identity locked across on / off / finger messages", footer,
                              juce::Justification::centredRight, 1, 0.72f);
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

    setSize (1440, 900);
    setResizable (true, true);
    setResizeLimits (1000, 650, 2200, 1300);
    setOpaque (true);
    const auto versionText = "v" + juce::String (JucePlugin_VersionString);
    setTitle ("Cosmic Microwave " + versionText + " OSC to MIDI notes router");
    setDescription ("Notes-only control surface for zone OSC input, source routing, pitch mapping, Time Field scheduling and MIDI output.");

    versionLabel.setText (versionText, juce::dontSendNotification);
    versionLabel.setJustificationType (juce::Justification::centred);
    versionLabel.setColour (juce::Label::textColourId, cm::cyan.withAlpha (0.88f));
    versionLabel.setFont (juce::Font (juce::FontOptions (9.0f).withStyle ("bold")));
    versionLabel.setTitle ("Cosmic Microwave version");
    versionLabel.setDescription ("Cosmic Microwave version "
                                 + juce::String (JucePlugin_VersionString));
    addAndMakeVisible (versionLabel);

    for (auto* tab : { &performTabButton, &showConsoleTabButton })
    {
        styleButton (*tab);
        tab->setComponentID ("pageTab");
        tab->setRadioGroupId (1001, juce::dontSendNotification);
        tab->setClickingTogglesState (false);
        tab->setWantsKeyboardFocus (true);
        addAndMakeVisible (*tab);
    }
    performTabButton.setTitle ("Perform page");
    performTabButton.setDescription ("Open the OSC, source matrix, Time Field, pitch and MIDI performance controls.");
    performTabButton.setTooltip (performTabButton.getDescription());
    showConsoleTabButton.setTitle ("Show Console page");
    showConsoleTabButton.setDescription ("Open venue preflight, safety telemetry, Global Conductor and Notes Only MIDI policy controls.");
    showConsoleTabButton.setTooltip (showConsoleTabButton.getDescription());
    performTabButton.onClick = [this] { showPage (false); };
    showConsoleTabButton.onClick = [this] { showPage (true); };

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
    styleMetric (activeSourcesValue, activeSourcesCaption, "SOURCES", "Active input sources");
    styleMetric (activeFingersValue, activeFingersCaption, "TOUCHES", "Active touches");
    styleMetric (notesSentValue, notesSentCaption, "MIDI NOTES", "MIDI notes sent");
    styleMetric (activeNotesValue, activeNotesCaption, "ACTIVE NOTES", "Active scheduled notes");
    notesSentValue.setColour (juce::Label::textColourId, cm::cyan);
    activeNotesValue.setColour (juce::Label::textColourId, cm::green);

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
    portEditor.setTooltip ("Each zone uses the UDP output port assigned to it in the per-show Venue Bridge manifest.");
    portEditor.onTextChange = [this]
    {
        if (! updatingPortEditor)
        {
            portEditorDirty = portEditor.getText().trim() != juce::String (proc.getUdpPort());
            portApplyButton.setEnabled (portEditorDirty);
            portEditor.setDescription ("UDP port for this Cosmic Microwave instance. Enter a number from 1 to 65535 and press Return or Apply.");
            oscPathLabel.setText ("/cs/{zone}/{source}/finger0  |  u/v/on  |  0-1  |  immediate",
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

    styleLabel (oscPathLabel, "/cs/{zone}/{source}/finger0  |  u/v/on  |  0-1  |  immediate");
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
    simAddButton.setTitle ("Add held simulator touch");
    simAddButton.setTooltip ("Add one continuously held finger0 touch for deterministic pitch and channel tests.");
    simCrowdButton.setTitle ("Add crowd simulator participants");
    simCrowdButton.setTooltip ("Add 25 stable participant IDs whose finger0 touches independently press and release like production Pad traffic.");
    simRemoveButton.setTitle ("Remove simulator participant");
    simRemoveButton.setTooltip ("Remove one participant from the local simulator, releasing it first when active.");
    simClearButton.setTitle ("Clear simulator");
    simClearButton.setTooltip ("Release and remove every local simulator participant. Live OSC sources are not simulator-owned; do not run both inputs together.");
    simAddButton.onClick = [this] { proc.simulator.addRandomSeat(); };
    simCrowdButton.onClick = [this] { proc.simulator.addCrowdParticipants (25); };
    simRemoveButton.onClick = [this] { proc.simulator.removeRandomSeat(); };
    simClearButton.onClick = [this] { proc.simulator.clear(); };

    addChoiceItems (simProfileCombo, { "Human", "Dense", "Stress" });
    styleCombo (simProfileCombo);
    simProfileCombo.setSelectedItemIndex (
        static_cast<int> (proc.simulator.getProfile()), juce::dontSendNotification);
    simProfileCombo.setTitle ("Simulator behaviour profile");
    simProfileCombo.setDescription (
        "Human follows the measured phone timing and gesture mixture. Dense increases activity while preserving independent human motion. Stress applies a bounded worst-case load profile.");
    simProfileCombo.setTooltip (simProfileCombo.getDescription());
    simProfileCombo.onChange = [this]
    {
        const int selected = simProfileCombo.getSelectedItemIndex();
        const auto profile = selected == 1 ? Simulator::Profile::dense
                           : selected == 2 ? Simulator::Profile::stress
                                           : Simulator::Profile::human;
        proc.simulator.setProfile (profile);
    };
    addAndMakeVisible (simProfileCombo);

    styleLabel (simStatusLabel, "HELD 0  ·  CROWD 0 / 0 ACTIVE",
                juce::Justification::centredLeft);
    simStatusLabel.setColour (juce::Label::textColourId, cm::textDim);
    simStatusLabel.setFont (juce::Font (juce::FontOptions (9.3f).withStyle ("bold")));
    simStatusLabel.setTitle ("Simulator lifecycle status");
    simStatusLabel.setDescription ("No local simulator participants.");
    addAndMakeVisible (simStatusLabel);

    simMoveButton.setColour (juce::ToggleButton::textColourId, cm::textMuted);
    simMoveButton.setColour (juce::ToggleButton::tickColourId, cm::green);
    simMoveButton.setColour (juce::ToggleButton::tickDisabledColourId, cm::line);
    simMoveButton.setTitle ("Move active simulator touches");
    simMoveButton.setTooltip ("Continuously move only currently pressed simulator touches across the U and V axes. Crowd press/release cycling continues independently.");
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
    addChoiceItems (noteDurationCombo, { "2n", "4n", "8n", "16n", "32n" });
    addChoiceItems (ensembleSameNoteCombo, { "Tie", "Retrigger" });
    addChoiceItems (temporalSpreadCombo, { "1", "2", "4", "8", "16" });

    for (auto* combo : { &timeModeCombo, &clockSourceCombo,
                         &gridDivisionCombo, &noteDurationCombo,
                         &ensembleSameNoteCombo,
                         &temporalSpreadCombo })
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
    noteDurationCombo.setTitle ("Note duration");
    noteDurationCombo.setDescription ("Fixed Note On to Note Off lifetime for each new attack, calculated from the current host tempo.");
    ensembleSameNoteCombo.setTitle ("Ensemble same-note articulation");
    ensembleSameNoteCombo.setDescription (
        "Tie extends an already sounding identical note. Retrigger sends a safe Note Off then Note On on every admitted Ensemble pulse. This choice affects Ensemble only.");
    ensembleSameNoteCombo.setTooltip (ensembleSameNoteCombo.getDescription());
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
    styleLabel (noteDurationLabel, "NOTE DURATION");
    styleLabel (ensembleSameNoteLabel, "SAME NOTE");
    ensembleSameNoteLabel.setTitle ("Ensemble same-note articulation");
    ensembleSameNoteLabel.setDescription (ensembleSameNoteCombo.getDescription());
    ensembleSameNoteLabel.setTooltip (ensembleSameNoteCombo.getDescription());
    styleLabel (maxAttacksLabel, "ATTACKS / STEP");
    styleLabel (maxActiveVoicesLabel, "ACTIVE LIMIT");
    styleLabel (gatePercentLabel, "GATE");
    styleLabel (temporalSpreadLabel, "SPREAD / STEPS");
    for (auto* label : { &timeModeLabel, &clockSourceLabel, &internalBpmLabel,
                         &gridDivisionLabel, &noteDurationLabel,
                         &ensembleSameNoteLabel,
                         &maxAttacksLabel, &maxActiveVoicesLabel,
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
    timeTelemetryLabel.setDescription (
        "Pending attacks, active scheduled voices, and pending work whose admission window elapsed; held intent is renewed while released taps may expire.");
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
    noteDurationAttachment = std::make_unique<ComboAttachment> (proc.apvts, "noteDuration", noteDurationCombo);
    ensembleSameNoteAttachment = std::make_unique<ComboAttachment> (
        proc.apvts, "ensembleSameNoteMode", ensembleSameNoteCombo);
    maxAttacksAttachment = std::make_unique<SliderAttachment> (proc.apvts, "maxAttacksPerStep", maxAttacksSlider);
    maxActiveVoicesAttachment = std::make_unique<SliderAttachment> (proc.apvts, "maxActiveVoices", maxActiveVoicesSlider);
    gatePercentAttachment = std::make_unique<SliderAttachment> (proc.apvts, "gatePercent", gatePercentSlider);
    temporalSpreadAttachment = std::make_unique<ComboAttachment> (proc.apvts, "temporalSpread", temporalSpreadCombo);
    governorModeAttachment = std::make_unique<ButtonAttachment> (proc.apvts, "crowdGovernorEnabled", governorModeButton);

    timeModeCombo.onChange = [this] { updateModeVisibility(); updateLiveText(); };
    clockSourceCombo.onChange = [this] { updateModeVisibility(); updateLiveText(); };
    gridDivisionCombo.onChange = [this] { updateLiveText(); };
    noteDurationCombo.onChange = [this] { updateLiveText(); };
    governorModeButton.onClick = [this] { updateModeVisibility(); updateLiveText(); };

    // MIDI routing ------------------------------------------------------------
    addChoiceItems (midiTypeCombo, { "Off", "Notes Only" });
    addChoiceItems (normalRoutingCombo, { "Single channel", "Per source 1-16" });
    addChoiceItems (sourceCapacityCombo,
                    { "64 participants - 4 / channel",
                      "128 participants - 8 / channel",
                      "256 participants - 16 / channel" });
    juce::StringArray channels;
    for (int channel = 1; channel <= 16; ++channel)
        channels.add ("Channel " + juce::String (channel));
    addChoiceItems (normalChannelCombo, channels);

    for (auto* combo : { &midiTypeCombo, &normalRoutingCombo,
                         &normalChannelCombo, &sourceCapacityCombo })
    {
        styleCombo (*combo);
        addAndMakeVisible (*combo);
    }
    midiTypeCombo.setTitle ("MIDI note output");
    midiTypeCombo.setDescription ("Off, or Note On/Off only. Cosmic Microwave does not emit musical CC, Channel Pressure, Pitch Bend or MPE messages.");
    normalRoutingCombo.setTitle ("Notes Only source routing");
    normalChannelCombo.setTitle ("Fixed Notes Only MIDI channel");
    sourceCapacityCombo.setTitle ("Participant source capacity");
    sourceCapacityCombo.setDescription ("Logical zone capacity distributed evenly across the same sixteen physical MIDI channels.");

    styleLabel (midiTypeLabel, "OUTPUT");
    styleLabel (normalRoutingLabel, "SOURCE ROUTING");
    styleLabel (normalChannelLabel, "FIXED CHANNEL");
    styleLabel (sourceCapacityLabel, "SOURCE CAPACITY");
    for (auto* label : { &midiTypeLabel, &normalRoutingLabel,
                         &normalChannelLabel, &sourceCapacityLabel })
        addAndMakeVisible (*label);

    midiTypeAttachment = std::make_unique<ComboAttachment> (proc.apvts, "midiOutputType", midiTypeCombo);
    normalRoutingAttachment = std::make_unique<ComboAttachment> (proc.apvts, "normalMidiRoutingMode", normalRoutingCombo);
    normalChannelAttachment = std::make_unique<ComboAttachment> (proc.apvts, "normalMidiChannel", normalChannelCombo);
    sourceCapacityAttachment = std::make_unique<ComboAttachment> (proc.apvts, "sourceCapacity", sourceCapacityCombo);

    midiTypeCombo.onChange = [this] { updateModeVisibility(); updateLiveText(); };
    normalRoutingCombo.onChange = [this] { updateModeVisibility(); updateLiveText(); };
    sourceCapacityCombo.onChange = [this] { updateLiveText(); };

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
    panicButton.setButtonText ("PANIC  —  ALL NOTES OFF");
    panicButton.setComponentID ("panicButton");
    panicButton.setTitle ("MIDI panic");
    panicButton.setDescription ("Send note-off and all-sound-off messages on all MIDI channels.");
    panicButton.setTooltip ("Release every active note on the host and external MIDI outputs.");
    panicButton.onClick = [this] { proc.panic(); updateLiveText(); };
    addAndMakeVisible (panicButton);

    // Show Console ----------------------------------------------------------
    // The console owns only controls and telemetry. Capture/replay remains an
    // external process so the plug-in's audio callback never performs file or
    // socket I/O beyond its existing bounded hand-off.
    auto styleToggle = [this] (juce::ToggleButton& button, const juce::String& title,
                               const juce::String& description)
    {
        button.setColour (juce::ToggleButton::textColourId, cm::text);
        button.setColour (juce::ToggleButton::tickColourId, cm::cyan);
        button.setColour (juce::ToggleButton::tickDisabledColourId, cm::line);
        button.setTitle (title);
        button.setDescription (description);
        button.setTooltip (description);
        addAndMakeVisible (button);
    };

    auto styleReadout = [this] (juce::Label& label, const juce::String& title,
                                juce::Justification justification = juce::Justification::centredLeft)
    {
        styleLabel (label, {}, justification);
        label.setColour (juce::Label::backgroundColourId, cm::background.withAlpha (0.46f));
        label.setColour (juce::Label::outlineColourId, cm::lineSoft);
        label.setColour (juce::Label::textColourId, cm::text);
        label.setFont (juce::Font (juce::FontOptions (10.2f).withStyle ("bold")));
        label.setTitle (title);
        label.setInterceptsMouseClicks (false, false);
        label.setWantsKeyboardFocus (false);
        addAndMakeVisible (label);
    };

    auto styleConsoleSlider = [this] (juce::Slider& slider, const juce::String& title,
                                      const juce::String& description,
                                      double minimum, double maximum)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 42, 24);
        slider.setRange (minimum, maximum, 1.0);
        slider.setNumDecimalPlacesToDisplay (0);
        slider.setColour (juce::Slider::trackColourId, cm::violet);
        slider.setColour (juce::Slider::backgroundColourId, cm::line);
        slider.setColour (juce::Slider::thumbColourId, cm::text);
        slider.setColour (juce::Slider::textBoxTextColourId, cm::text);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, cm::cardRaised);
        slider.setColour (juce::Slider::textBoxOutlineColourId, cm::line);
        slider.setTitle (title);
        slider.setDescription (description);
        slider.setTooltip (description);
        addAndMakeVisible (slider);
    };

    addChoiceItems (outputPathCombo, { "Host Only", "External Only", "Mirror" });
    juce::StringArray expectedZones { "Any" };
    for (char zone = 'A'; zone <= 'Z'; ++zone)
        expectedZones.add (juce::String::charToString ((juce::juce_wchar) zone));
    addChoiceItems (expectedZoneCombo, expectedZones);
    for (auto* combo : { &outputPathCombo, &expectedZoneCombo })
    {
        styleCombo (*combo);
        addAndMakeVisible (*combo);
    }
    styleLabel (outputPathLabel, "OUTPUT PATH");
    styleLabel (expectedZoneLabel, "EXPECTED ZONE");
    addAndMakeVisible (outputPathLabel);
    addAndMakeVisible (expectedZoneLabel);
    outputPathCombo.setTitle ("MIDI output path");
    outputPathCombo.setDescription ("Choose one explicit destination policy: host bus, external endpoint, or both.");
    expectedZoneCombo.setTitle ("Expected OSC zone");
    expectedZoneCombo.setDescription ("Reject valid OSC messages from every zone except the selected A-Z zone. Any disables zone filtering.");
    styleToggle (exclusivePortButton, "Exclusive UDP ownership",
                 "Require this plug-in instance to be the sole in-process owner of its UDP port.");
    styleReadout (routeConsoleStatusLabel, "Routing safety status");

    styleLabel (factoryPresetLabel, "FACTORY PERFORMANCE PRESET");
    addAndMakeVisible (factoryPresetLabel);
    for (int index = 0; index < AudienceProcessor::getNumFactoryPresets(); ++index)
        factoryPresetCombo.addItem (AudienceProcessor::getFactoryPresetName(index), index + 1);
    factoryPresetCombo.setTextWhenNothingSelected ("CUSTOM / SAVED PROJECT STATE");
    styleCombo (factoryPresetCombo);
    factoryPresetCombo.setTitle ("Factory performance preset");
    factoryPresetCombo.setDescription (
        "Recall the screenshot setup for Zone A-H. Recall sends Panic, selects UDP 6062-6069, "
        "opens the matching virtual MIDI port and restores the Notes Only show controls.");
    factoryPresetCombo.setTooltip (factoryPresetCombo.getDescription());
    addAndMakeVisible (factoryPresetCombo);
    styleButton (autoAssignRetryButton);
    autoAssignRetryButton.setTitle ("Retry automatic Zone / UDP assignment");
    autoAssignRetryButton.setDescription (
        "Retry the retained exclusive scan of UDP 6062-6069 after every factory route was busy.");
    autoAssignRetryButton.setTooltip (autoAssignRetryButton.getDescription());
    autoAssignRetryButton.onClick = [this]
    {
        proc.retryFreshRouteAssignment();
        restoreUdpPortEditor();
        refreshMidiOutputCombo();
        updateLiveText();
        updateConsoleTelemetry();
    };
    addAndMakeVisible (autoAssignRetryButton);
    styleReadout (factoryPresetStatusLabel, "Factory performance preset status");
    factoryPresetCombo.onChange = [this]
    {
        if (refreshingFactoryPreset)
            return;
        const int index = factoryPresetCombo.getSelectedItemIndex();
        if (index < 0 || index >= AudienceProcessor::getNumFactoryPresets())
            return;
        proc.applyFactoryPreset(index);
        restoreUdpPortEditor();
        refreshMidiOutputCombo();
        updateModeVisibility();
        updateLiveText();
        updateConsoleTelemetry();
    };

    styleToggle (safetyGovernorButton, "Safety Governor enabled",
                 "Continuously reduce attack admission, motion rate and voice ceilings as realtime pressure rises.");
    styleReadout (safetyStateLabel, "Safety Governor state");
    styleReadout (safetyReasonLabel, "Safety Governor reasons");
    styleReadout (safetyIngressLabel, "OSC ingress rate");
    styleReadout (safetyDeadlineLabel, "Audio callback deadline use");
    styleReadout (safetyFifoLabel, "External MIDI FIFO pressure");
    styleReadout (safetyQueueLabel, "OSC queue pressure");

    styleReadout (preflightSummaryLabel, "Cosmic local preflight summary");
    preflightSummaryLabel.setDescription (
        "Summarizes checks performed inside this Cosmic Microwave instance. It is not "
        "a server roster result or a substitute for the separate 60-second venue soak.");
    preflightSummaryLabel.setTooltip (preflightSummaryLabel.getDescription());
    static constexpr const char* preflightTitles[] {
        "OSC receiver preflight", "Zone contract preflight", "UDP ownership preflight",
        "MIDI route preflight", "Safety Governor preflight", "Time Field preflight",
        "Global Conductor preflight", "Source Quality Ready Gate preflight"
    };
    for (size_t index = 0; index < preflightRows.size(); ++index)
        styleReadout (preflightRows[index], preflightTitles[index]);
    preflightRows[7].setDescription (
        "Short local accepted-live-OSC signal census. A READY result keeps the server "
        "roster and the 60-second production soak explicitly external.");
    preflightRows[7].setTooltip (preflightRows[7].getDescription());

    addChoiceItems (conductorRoleCombo, { "Off", "Leader", "Follower" });
    addChoiceItems (conductorGroupCombo, { "Group 1", "Group 2", "Group 3", "Group 4" });
    for (auto* combo : { &conductorRoleCombo, &conductorGroupCombo })
    {
        styleCombo (*combo);
        addAndMakeVisible (*combo);
    }
    styleLabel (conductorRoleLabel, "ROLE");
    styleLabel (conductorGroupLabel, "GROUP");
    styleLabel (conductorAttackBudgetLabel, "GLOBAL ATTACK BUDGET");
    styleLabel (conductorVoiceBudgetLabel, "GLOBAL VOICE BUDGET");
    for (auto* label : { &conductorRoleLabel, &conductorGroupLabel,
                         &conductorAttackBudgetLabel, &conductorVoiceBudgetLabel })
        addAndMakeVisible (*label);
    conductorRoleCombo.setTitle ("Global Conductor role");
    conductorRoleCombo.setDescription ("Off uses local policy. Leader publishes budgets. Follower consumes the elected leader allocation.");
    conductorGroupCombo.setTitle ("Global Conductor group");
    conductorGroupCombo.setDescription ("Coordinate only Cosmic Microwave instances assigned to the same process-local group.");
    styleConsoleSlider (conductorAttackBudgetSlider, "Global attack budget",
                        "Total attacks shared fairly across live zones.", 1.0, 64.0);
    styleConsoleSlider (conductorVoiceBudgetSlider, "Global voice budget",
                        "Total active voices shared fairly across live zones.", 1.0, 128.0);
    styleReadout (conductorStatusLabel, "Global Conductor status");
    styleReadout (conductorQuotaLabel, "Global Conductor allocation");

    juce::StringArray macroChannels;
    for (int channel = 1; channel <= 16; ++channel)
        macroChannels.add ("Channel " + juce::String (channel));
    macroChannels.add ("Broadcast 1-16");
    addChoiceItems (macroChannelCombo, macroChannels);
    addChoiceItems (macroRateCombo, { "5 Hz", "10 Hz", "20 Hz", "30 Hz" });
    for (auto* combo : { &macroChannelCombo, &macroRateCombo })
    {
        styleCombo (*combo);
        addAndMakeVisible (*combo);
    }
    styleToggle (crowdMacrosButton, "Crowd Expression macros",
                 "Emit bounded density, centroid and motion Control Change messages after note lifecycle traffic.");
    styleLabel (macroChannelLabel, "MIDI CHANNEL");
    styleLabel (macroRateLabel, "CONTROL RATE");
    styleLabel (macroDensityCcLabel, "DENSITY CC");
    styleLabel (macroCentroidXCcLabel, "CENTROID X CC");
    styleLabel (macroCentroidYCcLabel, "CENTROID Y CC");
    styleLabel (macroMotionCcLabel, "MOTION CC");
    for (auto* label : { &macroChannelLabel, &macroRateLabel, &macroDensityCcLabel,
                         &macroCentroidXCcLabel, &macroCentroidYCcLabel, &macroMotionCcLabel })
        addAndMakeVisible (*label);
    styleConsoleSlider (macroDensityCcSlider, "Crowd density CC",
                        "MIDI CC number carrying normalized active-crowd density.", 0.0, 127.0);
    styleConsoleSlider (macroCentroidXCcSlider, "Crowd centroid X CC",
                        "MIDI CC number carrying the crowd horizontal centroid.", 0.0, 127.0);
    styleConsoleSlider (macroCentroidYCcSlider, "Crowd centroid Y CC",
                        "MIDI CC number carrying the crowd vertical centroid.", 0.0, 127.0);
    styleConsoleSlider (macroMotionCcSlider, "Crowd motion CC",
                        "MIDI CC number carrying smoothed aggregate crowd motion.", 0.0, 127.0);
    styleReadout (macroStatusLabel, "Notes Only MIDI policy status");

    styleButton (sourceQualityButton);
    sourceQualityButton.setTitle ("Source Quality Ready Gate");
    sourceQualityButton.setDescription (
        "Start a fresh local accepted-live-OSC signal census for every source in the selected 64, 128 or 256 capacity domain. This short gate does not verify the server roster or replace the separate 60-second production soak. While armed and warming, only new attacks wait; Off, Cancel and sounding notes remain safe.");
    sourceQualityButton.setTooltip (sourceQualityButton.getDescription());
    sourceQualityButton.onClick = [this]
    {
        if (proc.isSourceQualityCheckArmed())
            proc.stopSourceQualityCheck();
        else
            proc.startSourceQualityCheck();
        updateConsoleTelemetry();
    };
    addAndMakeVisible (sourceQualityButton);
    styleReadout (sourceQualityGateLabel, "Source Quality gate state");
    styleReadout (sourceQualityCoverageLabel, "Source signal census coverage");
    styleReadout (sourceQualityTimingLabel, "Source U and V heartbeat and motion rate");
    styleReadout (sourceQualityFaultLabel, "Source lifecycle and split drop incidents");

    styleReadout (chaosTitleLabel, "Capture and Replay Chaos Lab");
    chaosTitleLabel.setText ("EXTERNAL TOOL  /  AUDIO THREAD ISOLATED", juce::dontSendNotification);
    chaosTitleLabel.setColour (juce::Label::textColourId, cm::cyan);
    styleLabel (chaosBodyLabel,
                "Capture, replay and deterministic crowd storms run outside the plug-in. "
                "No recording, file access or replay socket is executed on the audio thread.");
    chaosBodyLabel.setTitle ("Chaos Lab architecture");
    chaosBodyLabel.setDescription (chaosBodyLabel.getText());
    chaosBodyLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (chaosBodyLabel);
    styleReadout (chaosCommandLabel, "Chaos Lab command");
    chaosCommandLabel.setText ("node tools/cosmic-chaos-lab.mjs --help", juce::dontSendNotification);
    chaosCommandLabel.setColour (juce::Label::textColourId, cm::violet);
    chaosCommandLabel.setDescription ("Run this command from the Cosmic Microwave repository in Terminal.");
    chaosCommandLabel.setTooltip (chaosCommandLabel.getDescription());

    outputPathAttachment = std::make_unique<ComboAttachment> (proc.apvts, "midiOutputPath", outputPathCombo);
    expectedZoneAttachment = std::make_unique<ComboAttachment> (proc.apvts, "expectedZone", expectedZoneCombo);
    exclusivePortAttachment = std::make_unique<ButtonAttachment> (proc.apvts, "exclusiveUdpPort", exclusivePortButton);
    safetyGovernorAttachment = std::make_unique<ButtonAttachment> (proc.apvts, "safetyGovernorEnabled", safetyGovernorButton);
    conductorRoleAttachment = std::make_unique<ComboAttachment> (proc.apvts, "conductorRole", conductorRoleCombo);
    conductorGroupAttachment = std::make_unique<ComboAttachment> (proc.apvts, "conductorGroup", conductorGroupCombo);
    conductorAttackBudgetAttachment = std::make_unique<SliderAttachment> (proc.apvts, "conductorAttackBudget", conductorAttackBudgetSlider);
    conductorVoiceBudgetAttachment = std::make_unique<SliderAttachment> (proc.apvts, "conductorVoiceBudget", conductorVoiceBudgetSlider);
    crowdMacrosAttachment = std::make_unique<ButtonAttachment> (proc.apvts, "crowdMacrosEnabled", crowdMacrosButton);
    macroChannelAttachment = std::make_unique<ComboAttachment> (proc.apvts, "crowdMacroChannel", macroChannelCombo);
    macroRateAttachment = std::make_unique<ComboAttachment> (proc.apvts, "crowdMacroRate", macroRateCombo);
    macroDensityCcAttachment = std::make_unique<SliderAttachment> (proc.apvts, "crowdMacroDensityCc", macroDensityCcSlider);
    macroCentroidXCcAttachment = std::make_unique<SliderAttachment> (proc.apvts, "crowdMacroCentroidXCc", macroCentroidXCcSlider);
    macroCentroidYCcAttachment = std::make_unique<SliderAttachment> (proc.apvts, "crowdMacroCentroidYCc", macroCentroidYCcSlider);
    macroMotionCcAttachment = std::make_unique<SliderAttachment> (proc.apvts, "crowdMacroMotionCc", macroMotionCcSlider);

    for (auto* combo : { &outputPathCombo, &expectedZoneCombo, &conductorRoleCombo,
                         &conductorGroupCombo, &macroChannelCombo, &macroRateCombo })
        combo->onChange = [this] { updateConsoleTelemetry(); };
    for (auto* toggle : { &exclusivePortButton, &safetyGovernorButton, &crowdMacrosButton })
        toggle->onClick = [this] { updateConsoleTelemetry(); };

    // Presentation proxies keep the existing ComboBoxAttachments authoritative
    // while matching the compact segmented language of the reference design.
    timeModeSegments = std::make_unique<SegmentedChoice> (
        timeModeCombo, juce::StringArray { "Flow", "Grid", "Ensemble" },
        "Time Field mode");
    clockSourceSegments = std::make_unique<SegmentedChoice> (
        clockSourceCombo, juce::StringArray { "Host", "Internal" },
        "Time Field clock source");
    pitchSystemSegments = std::make_unique<SegmentedChoice> (
        pitchSystemCombo, juce::StringArray { "Tonal", "Atomic" },
        "Pitch system");
    pitchSpectrumDisplay = std::make_unique<PitchSpectrumDisplay> (proc);
    addAndMakeVisible (*timeModeSegments);
    addAndMakeVisible (*clockSourceSegments);
    addAndMakeVisible (*pitchSystemSegments);
    addAndMakeVisible (*pitchSpectrumDisplay);

    // The hidden ComboBoxes retain parameter ownership only. The focusable
    // segmented proxies above are the user-facing accessible controls.
    timeModeCombo.setWantsKeyboardFocus (false);
    clockSourceCombo.setWantsKeyboardFocus (false);
    pitchSystemCombo.setWantsKeyboardFocus (false);

    restoreUdpPortEditor();
    refreshMidiOutputCombo();
    updateModeVisibility();
    updateLiveText();
    updateConsoleTelemetry();
    showPage (false);
    startTimerHz (8);
}

AudienceEditor::~AudienceEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void AudienceEditor::setPerformControlsVisible (bool shouldBeVisible)
{
    if (sourceMap != nullptr)
        sourceMap->setVisible (shouldBeVisible);
    if (timeModeSegments != nullptr)
        timeModeSegments->setVisible (shouldBeVisible);
    if (clockSourceSegments != nullptr)
        clockSourceSegments->setVisible (shouldBeVisible);
    if (pitchSystemSegments != nullptr)
        pitchSystemSegments->setVisible (shouldBeVisible);
    if (pitchSpectrumDisplay != nullptr)
        pitchSpectrumDisplay->setVisible (shouldBeVisible);

    auto set = [shouldBeVisible] (std::initializer_list<juce::Component*> components)
    {
        for (auto* component : components)
            component->setVisible (shouldBeVisible);
    };

    set ({ &portLabel, &portEditor, &portApplyButton, &oscStatusLabel, &oscPathLabel,
           &routingSummaryLabel, &routingDetailLabel, &zoneStatusLabel,
           &simAddButton, &simCrowdButton, &simRemoveButton, &simClearButton,
           &simProfileCombo,
           &simStatusLabel, &simMoveButton,
           &rootLabel, &rootOctaveLabel, &scaleLabel, &octavesLabel,
           &atomicElementLabel, &atomicModeLabel, &pitchSystemCombo, &rootCombo,
           &rootOctaveCombo, &scaleCombo, &atomicElementCombo, &atomicModeCombo, &octavesSlider,
           &timeStatusLabel, &timeTelemetryLabel, &governorModeButton,
           &timeModeLabel, &clockSourceLabel, &internalBpmLabel, &gridDivisionLabel,
           &noteDurationLabel, &ensembleSameNoteLabel,
           &maxAttacksLabel, &maxActiveVoicesLabel, &gatePercentLabel, &temporalSpreadLabel,
           &governorAttacksValue, &governorActiveVoicesValue, &governorSpreadValue,
           &timeModeCombo, &clockSourceCombo, &gridDivisionCombo, &noteDurationCombo,
           &ensembleSameNoteCombo,
           &temporalSpreadCombo,
           &internalBpmSlider, &maxAttacksSlider, &maxActiveVoicesSlider, &gatePercentSlider,
           &midiTypeLabel, &normalRoutingLabel, &normalChannelLabel, &sourceCapacityLabel,
           &midiTypeCombo, &normalRoutingCombo, &normalChannelCombo, &sourceCapacityCombo,
           &destinationLabel, &destinationCombo, &rescanButton,
           &destinationStatusLabel, &destinationDetailLabel, &panicButton });
}

void AudienceEditor::setConsoleControlsVisible (bool shouldBeVisible)
{
    auto set = [shouldBeVisible] (std::initializer_list<juce::Component*> components)
    {
        for (auto* component : components)
            component->setVisible (shouldBeVisible);
    };

    set ({ &outputPathLabel, &expectedZoneLabel, &outputPathCombo, &expectedZoneCombo,
           &exclusivePortButton, &routeConsoleStatusLabel,
           &factoryPresetLabel, &factoryPresetCombo, &autoAssignRetryButton,
           &factoryPresetStatusLabel,
           &safetyGovernorButton, &safetyStateLabel, &safetyReasonLabel,
           &safetyIngressLabel, &safetyDeadlineLabel, &safetyFifoLabel, &safetyQueueLabel,
           &preflightSummaryLabel,
           &conductorRoleLabel, &conductorGroupLabel, &conductorAttackBudgetLabel,
           &conductorVoiceBudgetLabel, &conductorRoleCombo, &conductorGroupCombo,
           &conductorAttackBudgetSlider, &conductorVoiceBudgetSlider,
           &conductorStatusLabel, &conductorQuotaLabel,
           &crowdMacrosButton, &macroChannelLabel, &macroRateLabel,
           &macroDensityCcLabel, &macroCentroidXCcLabel, &macroCentroidYCcLabel,
           &macroMotionCcLabel, &macroChannelCombo, &macroRateCombo,
           &macroDensityCcSlider, &macroCentroidXCcSlider, &macroCentroidYCcSlider,
           &macroMotionCcSlider, &macroStatusLabel,
           &sourceQualityButton, &sourceQualityGateLabel,
           &sourceQualityCoverageLabel, &sourceQualityTimingLabel,
           &sourceQualityFaultLabel,
           &chaosTitleLabel, &chaosBodyLabel, &chaosCommandLabel });

    // These legacy controls remain constructed only so old APVTS parameter
    // topology is harmless. Notes Only never exposes or emits Crowd Macro CCs.
    for (auto* retired : { static_cast<juce::Component*> (&crowdMacrosButton),
                           static_cast<juce::Component*> (&macroChannelLabel),
                           static_cast<juce::Component*> (&macroRateLabel),
                           static_cast<juce::Component*> (&macroDensityCcLabel),
                           static_cast<juce::Component*> (&macroCentroidXCcLabel),
                           static_cast<juce::Component*> (&macroCentroidYCcLabel),
                           static_cast<juce::Component*> (&macroMotionCcLabel),
                           static_cast<juce::Component*> (&macroChannelCombo),
                           static_cast<juce::Component*> (&macroRateCombo),
                           static_cast<juce::Component*> (&macroDensityCcSlider),
                           static_cast<juce::Component*> (&macroCentroidXCcSlider),
                           static_cast<juce::Component*> (&macroCentroidYCcSlider),
                           static_cast<juce::Component*> (&macroMotionCcSlider) })
        retired->setVisible (false);

    for (auto& row : preflightRows)
        row.setVisible (shouldBeVisible);
}

void AudienceEditor::showPage (bool shouldShowConsole)
{
    if (showConsolePage == shouldShowConsole
        && performTabButton.getToggleState() == ! shouldShowConsole)
        return;

    showConsolePage = shouldShowConsole;
    performTabButton.setToggleState (! showConsolePage, juce::dontSendNotification);
    showConsoleTabButton.setToggleState (showConsolePage, juce::dontSendNotification);
    performTabButton.setColour (juce::TextButton::textColourOffId,
                                showConsolePage ? cm::textMuted : cm::cyan);
    showConsoleTabButton.setColour (juce::TextButton::textColourOffId,
                                    showConsolePage ? cm::cyan : cm::textMuted);

    setConsoleControlsVisible (showConsolePage);
    setPerformControlsVisible (! showConsolePage);
    if (! showConsolePage)
        updateModeVisibility();
    else
        updateConsoleTelemetry();

    resized();
    repaint();
}

void AudienceEditor::paint (juce::Graphics& g)
{
    g.fillAll (cm::background);

    auto headerBounds = getLocalBounds().removeFromTop (62);
    g.setColour (cm::header);
    g.fillRect (headerBounds);
    g.setColour (cm::lineSoft);
    g.fillRect (headerBounds.removeFromBottom (1));

    auto brand = juce::Rectangle<int> (18, 15, 32, 32).toFloat();
    g.setColour (cm::cyan.withAlpha (0.88f));
    g.drawEllipse (brand, 1.8f);
    g.setColour (cm::violet);
    g.fillEllipse (brand.getCentreX() - 4.0f, brand.getCentreY() - 4.0f, 8.0f, 8.0f);
    g.setColour (cm::cyan);
    g.fillEllipse (brand.getRight() - 5.0f, brand.getY() + 3.0f, 4.0f, 4.0f);

    g.setColour (cm::text);
    g.setFont (juce::Font (juce::FontOptions (15.0f).withStyle ("bold")));
    g.drawText ("COSMIC MICROWAVE", 62, 11, 214, 22,
                juce::Justification::centredLeft, false);
    g.setColour (cm::textDim);
    g.setFont (juce::Font (juce::FontOptions (9.0f).withStyle ("bold")));
    g.drawText ("OSC / MIDI ROUTING", 62, 34, 214, 14,
                juce::Justification::centredLeft, false);

    auto badge = juce::Rectangle<float> (282.0f, 14.0f, 78.0f, 20.0f);
    g.setColour (juce::Colour (0xff0c1713));
    g.fillRoundedRectangle (badge, 10.0f);
    g.setColour (juce::Colour (0xff1f4438));
    g.drawRoundedRectangle (badge.reduced (0.5f), 10.0f, 0.8f);
    g.setColour (cm::green);
    g.fillEllipse (badge.getX() + 9.0f, badge.getCentreY() - 2.0f, 4.0f, 4.0f);
    g.setFont (juce::Font (juce::FontOptions (8.5f).withStyle ("bold")));
    g.drawText ("MIDI ONLY", badge.toNearestInt().withTrimmedLeft (10),
                juce::Justification::centred, false);

    const std::array<juce::Label*, 4> metricValues {{ &activeSourcesValue, &activeFingersValue,
                                                      &notesSentValue, &activeNotesValue }};
    const std::array<juce::Label*, 4> metricCaptions {{ &activeSourcesCaption, &activeFingersCaption,
                                                        &notesSentCaption, &activeNotesCaption }};
    for (size_t index = 0; index < metricValues.size(); ++index)
    {
        const auto metricBounds = metricValues[index]->getBounds()
                                      .getUnion (metricCaptions[index]->getBounds()).toFloat();
        g.setColour (cm::card);
        g.fillRoundedRectangle (metricBounds, 7.0f);
        g.setColour (cm::lineSoft);
        g.drawRoundedRectangle (metricBounds.reduced (0.5f), 7.0f, 0.8f);

        const std::array<juce::Colour, 4> accents {{ juce::Colour (0xff3a4a5c),
                                                     juce::Colour (0xff3a4a5c),
                                                     juce::Colour (0xff2e6f86),
                                                     juce::Colour (0xff564e86) }};
        const auto accent = accents[index];
        g.setColour (accent.withAlpha (0.72f));
        g.fillRoundedRectangle (metricBounds.withHeight (1.4f).reduced (8.0f, 0.0f), 0.7f);
    }

    if (showConsolePage)
    {
        cm::drawCard (g, safetyCardBounds, "SAFETY GOVERNOR", "PRESSURE-AWARE");
        cm::drawCard (g, preflightCardBounds, "VENUE PREFLIGHT", "LIVE CHECKLIST");
        cm::drawCard (g, conductorCardBounds, "GLOBAL CONDUCTOR", "PROCESS-LOCAL");
        cm::drawCard (g, routeConsoleCardBounds, "ROUTING SAFETY", "EXPLICIT PATH");
        cm::drawCard (g, macrosCardBounds, "SOURCE QUALITY", "READY GATE");
        cm::drawCard (g, chaosCardBounds, "CAPTURE / REPLAY CHAOS LAB", "EXTERNAL CLI");
    }
    else
    {
        cm::drawCard (g, oscCardBounds, "OSC INPUT", "ZONE / FINGER0");
        cm::drawCard (g, routingCardBounds, "SOURCE ROUTING", "ID-LOCKED");
        cm::drawCard (g, simulatorCardBounds, "SIMULATOR", {});
        cm::drawCard (g, timeCardBounds, "TIME FIELD");
        cm::drawCard (g, pitchCardBounds, "PITCH MAPPING",
                      "U → NOTE  ·  V → VELOCITY  ·  NO MUSICAL CC");
        const int midiType = cm::choiceValue (proc.apvts, "midiOutputType");
        const auto midiTag = midiType == 0 ? "OFF" : "NOTES ONLY";
        cm::drawCard (g, midiCardBounds, "MIDI", midiTag);
    }

    if (! showConsolePage && ! timeStatusLabel.getBounds().isEmpty()
        && ! timeTelemetryLabel.getBounds().isEmpty())
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
    auto headerArea = area.removeFromTop (62);
    versionLabel.setBounds (282, 36, 78, 14);

    auto metrics = headerArea.reduced (14, 8)
                            .removeFromRight (juce::jlimit (344, 500,
                                                           (int) std::round ((double) getWidth() * 0.36)));
    constexpr int metricGap = 7;
    const int metricWidth = (metrics.getWidth() - metricGap * 3) / 4;
    const std::array<juce::Label*, 4> metricValues {{ &activeSourcesValue, &activeFingersValue,
                                                      &notesSentValue, &activeNotesValue }};
    const std::array<juce::Label*, 4> metricCaptions {{ &activeSourcesCaption, &activeFingersCaption,
                                                        &notesSentCaption, &activeNotesCaption }};
    for (size_t index = 0; index < metricValues.size(); ++index)
    {
        auto metric = metrics.removeFromLeft (metricWidth);
        metricValues[index]->setBounds (metric.removeFromTop (25));
        metricCaptions[index]->setBounds (metric);
        metrics.removeFromLeft (metricGap);
    }

    auto tabs = juce::Rectangle<int> (374, 16,
                                      juce::jmax (190, headerArea.getWidth()
                                                        - 374
                                                        - juce::jlimit (344, 500,
                                                            (int) std::round ((double) getWidth() * 0.36))
                                                        - 22),
                                      30);
    const int tabGap = 6;
    const int performWidth = juce::jmin (100, (tabs.getWidth() - tabGap) / 2);
    performTabButton.setBounds (tabs.removeFromLeft (performWidth));
    tabs.removeFromLeft (tabGap);
    showConsoleTabButton.setBounds (tabs.removeFromLeft (juce::jmin (130, tabs.getWidth())));

    area.reduce (12, 12);

    if (showConsolePage)
    {
        constexpr int gap = 10;
        auto top = area.removeFromTop ((area.getHeight() - gap) / 2);
        area.removeFromTop (gap);
        auto bottom = area;

        auto makeColumns = [] (juce::Rectangle<int> row,
                               juce::Rectangle<int>& first,
                               juce::Rectangle<int>& second,
                               juce::Rectangle<int>& third)
        {
            constexpr int columnGap = 10;
            const int usable = row.getWidth() - columnGap * 2;
            const int firstWidth = (int) std::round ((double) usable * 0.32);
            const int secondWidth = (int) std::round ((double) usable * 0.35);
            first = row.removeFromLeft (firstWidth);
            row.removeFromLeft (columnGap);
            second = row.removeFromLeft (secondWidth);
            row.removeFromLeft (columnGap);
            third = row;
        };
        makeColumns (top, safetyCardBounds, preflightCardBounds, conductorCardBounds);
        makeColumns (bottom, routeConsoleCardBounds, macrosCardBounds, chaosCardBounds);

        auto twoFields = [] (juce::Rectangle<int> row,
                             juce::Label& leftLabel, juce::Component& leftControl,
                             juce::Label& rightLabel, juce::Component& rightControl)
        {
            constexpr int fieldGap = 7;
            auto labels = row.removeFromTop (12);
            const int leftWidth = (labels.getWidth() - fieldGap) / 2;
            leftLabel.setBounds (labels.removeFromLeft (leftWidth));
            labels.removeFromLeft (fieldGap);
            rightLabel.setBounds (labels);
            leftControl.setBounds (row.removeFromLeft (leftWidth));
            row.removeFromLeft (fieldGap);
            rightControl.setBounds (row);
        };

        // Pressure-aware Safety Governor
        {
            auto inner = safetyCardBounds.reduced (13);
            inner.removeFromTop (31);
            safetyGovernorButton.setBounds (inner.removeFromTop (26));
            inner.removeFromTop (3);
            safetyStateLabel.setBounds (inner.removeFromTop (26));
            inner.removeFromTop (3);
            safetyReasonLabel.setBounds (inner.removeFromTop (26));
            inner.removeFromTop (3);
            auto telemetry = inner.removeFromTop (26);
            safetyIngressLabel.setBounds (telemetry.removeFromLeft ((telemetry.getWidth() - 5) / 2));
            telemetry.removeFromLeft (5);
            safetyDeadlineLabel.setBounds (telemetry);
            inner.removeFromTop (3);
            telemetry = inner.removeFromTop (26);
            safetyFifoLabel.setBounds (telemetry.removeFromLeft ((telemetry.getWidth() - 5) / 2));
            telemetry.removeFromLeft (5);
            safetyQueueLabel.setBounds (telemetry);
        }

        // Venue Preflight
        {
            auto inner = preflightCardBounds.reduced (13);
            inner.removeFromTop (31);
            preflightSummaryLabel.setBounds (inner.removeFromTop (27));
            inner.removeFromTop (3);
            const int rowGap = 2;
            const int rowHeight = juce::jlimit (20, 25,
                (inner.getHeight() - rowGap * ((int) preflightRows.size() - 1))
                    / (int) preflightRows.size());
            for (auto& row : preflightRows)
            {
                row.setBounds (inner.removeFromTop (rowHeight));
                inner.removeFromTop (rowGap);
            }
        }

        // Global Conductor
        {
            auto inner = conductorCardBounds.reduced (13);
            inner.removeFromTop (31);
            twoFields (inner.removeFromTop (40), conductorRoleLabel, conductorRoleCombo,
                       conductorGroupLabel, conductorGroupCombo);
            inner.removeFromTop (4);
            twoFields (inner.removeFromTop (40), conductorAttackBudgetLabel,
                       conductorAttackBudgetSlider, conductorVoiceBudgetLabel,
                       conductorVoiceBudgetSlider);
            inner.removeFromTop (5);
            conductorStatusLabel.setBounds (inner.removeFromTop (27));
            inner.removeFromTop (3);
            conductorQuotaLabel.setBounds (inner.removeFromTop (27));
        }

        // Explicit MIDI / zone / UDP route contract
        {
            auto inner = routeConsoleCardBounds.reduced (13);
            inner.removeFromTop (31);
            twoFields (inner.removeFromTop (42), outputPathLabel, outputPathCombo,
                       expectedZoneLabel, expectedZoneCombo);
            inner.removeFromTop (5);
            exclusivePortButton.setBounds (inner.removeFromTop (27));
            inner.removeFromTop (5);
            routeConsoleStatusLabel.setBounds (inner.removeFromTop (juce::jmin (54, inner.getHeight())));
            inner.removeFromTop (9);
            factoryPresetLabel.setBounds (inner.removeFromTop (12));
            inner.removeFromTop (3);
            auto presetRow = inner.removeFromTop (30);
            autoAssignRetryButton.setBounds (presetRow.removeFromRight (94));
            presetRow.removeFromRight (5);
            factoryPresetCombo.setBounds (presetRow);
            inner.removeFromTop (4);
            factoryPresetStatusLabel.setBounds (inner.removeFromTop (27));
        }

        // Runtime-only live-source census and Ready Gate. Legacy Crowd Macro
        // controls remain hidden and inert so old sessions keep their exact
        // parameter topology.
        {
            auto inner = macrosCardBounds.reduced (13);
            inner.removeFromTop (31);
            sourceQualityButton.setBounds (inner.removeFromTop (29));
            inner.removeFromTop (5);
            sourceQualityGateLabel.setBounds (inner.removeFromTop (28));
            inner.removeFromTop (4);
            sourceQualityCoverageLabel.setBounds (inner.removeFromTop (28));
            inner.removeFromTop (4);
            sourceQualityTimingLabel.setBounds (inner.removeFromTop (28));
            inner.removeFromTop (4);
            sourceQualityFaultLabel.setBounds (inner.removeFromTop (28));
            inner.removeFromTop (5);
            macroStatusLabel.setBounds (inner.removeFromTop (
                juce::jmin (48, inner.getHeight())));
            macroStatusLabel.setJustificationType (
                juce::Justification::centredLeft);
        }

        // External Chaos Lab
        {
            auto inner = chaosCardBounds.reduced (13);
            inner.removeFromTop (31);
            chaosTitleLabel.setBounds (inner.removeFromTop (28));
            inner.removeFromTop (6);
            chaosBodyLabel.setBounds (inner.removeFromTop (juce::jmin (90, inner.getHeight() - 38)));
            inner.removeFromTop (6);
            chaosCommandLabel.setBounds (inner.removeFromTop (30));
        }
        return;
    }

    // Reference layout: setup cards on the left, a fluid 16x16 matrix in the
    // centre, and all musical decisions stacked on the right. Widths remain
    // proportional below the reference's 1280 px desktop target so existing
    // host windows down to the supported 1000 px minimum remain usable.
    constexpr int columnGap = 12;
    const int leftColumnWidth = juce::jlimit (248, 304,
                                              (int) std::round ((double) area.getWidth() * 0.225));
    const int rightColumnWidth = juce::jlimit (310, 362,
                                               (int) std::round ((double) area.getWidth() * 0.265));
    auto leftColumn = area.removeFromLeft (leftColumnWidth);
    area.removeFromLeft (columnGap);
    auto rightColumn = area.removeFromRight (rightColumnWidth);
    area.removeFromRight (columnGap);
    mapCardBounds = area;
    if (sourceMap != nullptr)
        sourceMap->setBounds (mapCardBounds);

    const int leftUsable = leftColumn.getHeight() - columnGap * 2;
    const int oscHeight = juce::jlimit (160, 230,
        (int) std::round ((double) leftUsable * 0.28));
    const int routingHeight = juce::jlimit (135, 170,
        (int) std::round ((double) leftUsable * 0.20));
    const int simulatorHeight = juce::jlimit (166, 180,
        (int) std::round ((double) leftUsable * 0.22));
    oscCardBounds = leftColumn.removeFromTop (juce::jmin (oscHeight, leftColumn.getHeight()));
    leftColumn.removeFromTop (columnGap);
    routingCardBounds = leftColumn.removeFromTop (juce::jmin (routingHeight, leftColumn.getHeight()));
    leftColumn.removeFromTop (columnGap);
    simulatorCardBounds = leftColumn.removeFromTop (
        juce::jmin (simulatorHeight, leftColumn.getHeight()));

    const int rightHeight = rightColumn.getHeight();
    constexpr int minimumTimeHeight = 232;
    constexpr int minimumPitchHeight = 138;
    constexpr int minimumMidiHeight = 200;
    int timeHeight = juce::jlimit (minimumTimeHeight, 320,
        (int) std::round ((double) rightHeight * 0.38));
    int pitchHeight = juce::jlimit (minimumPitchHeight, 226,
        (int) std::round ((double) rightHeight * 0.27));

    // Destination selection and Panic must remain usable at the supported
    // 1000x650 minimum. Shrink the upper cards toward their compact layouts
    // before taking any height from the MIDI card.
    const int upperBudget = juce::jmax (0, rightHeight - columnGap * 2 - minimumMidiHeight);
    int excess = juce::jmax (0, timeHeight + pitchHeight - upperBudget);
    const int pitchReduction = juce::jmin (excess, pitchHeight - minimumPitchHeight);
    pitchHeight -= pitchReduction;
    excess -= pitchReduction;
    timeHeight -= juce::jmin (excess, timeHeight - minimumTimeHeight);
    timeCardBounds = rightColumn.removeFromTop (timeHeight);
    rightColumn.removeFromTop (columnGap);
    pitchCardBounds = rightColumn.removeFromTop (
        juce::jmin (pitchHeight, juce::jmax (0, rightColumn.getHeight() - 170)));
    rightColumn.removeFromTop (columnGap);
    midiCardBounds = rightColumn;
    destinationCardBounds = midiCardBounds;

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
        simProfileCombo.setBounds (simulatorCardBounds.getRight() - 98,
                                   simulatorCardBounds.getY() + 6, 86, 23);
        auto inner = simulatorCardBounds.reduced (14);
        inner.removeFromTop (35);
        simStatusLabel.setBounds (inner.removeFromTop (16));
        inner.removeFromTop (4);
        constexpr int buttonGap = 7;
        const int rowHeight = juce::jlimit (22, 27, (inner.getHeight() - 34) / 2);
        auto topRow = inner.removeFromTop (rowHeight);
        const int half = (topRow.getWidth() - buttonGap) / 2;
        simAddButton.setBounds (topRow.removeFromLeft (half));
        topRow.removeFromLeft (buttonGap);
        simCrowdButton.setBounds (topRow);
        inner.removeFromTop (buttonGap);
        auto bottomRow = inner.removeFromTop (rowHeight);
        simRemoveButton.setBounds (bottomRow.removeFromLeft (half));
        bottomRow.removeFromLeft (buttonGap);
        simClearButton.setBounds (bottomRow);
        inner.removeFromTop (5);
        simMoveButton.setBounds (inner.removeFromTop (21));
    }

    // Time Field: the reference's segmented mode and clock controls remain
    // proxies for the original APVTS-attached ComboBoxes.
    {
        governorModeButton.setBounds (timeCardBounds.getRight() - 109,
                                      timeCardBounds.getY() + 6, 96, 24);
        auto inner = timeCardBounds.reduced (13);
        inner.removeFromTop (25);
        timeStatusLabel.setBounds (inner.removeFromTop (18));
        const bool compactTimeLayout = timeCardBounds.getHeight() < 220;
        if (compactTimeLayout)
            timeTelemetryLabel.setBounds ({});
        else
            timeTelemetryLabel.setBounds (inner.removeFromTop (15));
        inner.removeFromTop (2);

        auto layoutPair = [] (juce::Rectangle<int> row,
                              juce::Label& leftLabel, juce::Component& leftControl,
                              juce::Label& rightLabel, juce::Component& rightControl)
        {
            constexpr int gap = 7;
            auto labels = row.removeFromTop (11);
            auto controls = row;
            const int leftWidth = (labels.getWidth() - gap) / 2;
            leftLabel.setBounds (labels.removeFromLeft (leftWidth));
            labels.removeFromLeft (gap);
            rightLabel.setBounds (labels);
            leftControl.setBounds (controls.removeFromLeft (leftWidth));
            controls.removeFromLeft (gap);
            rightControl.setBounds (controls);
        };

        constexpr int rowGap = 3;
        const int rowHeight = juce::jmax (27, (inner.getHeight() - rowGap * 4) / 5);
        const int controlsHeight = rowHeight * 5 + rowGap * 4;
        inner.removeFromTop (juce::jmax (0, (inner.getHeight() - controlsHeight) / 2));
        timeModeLabel.setBounds ({});
        timeModeCombo.setBounds ({});
        if (timeModeSegments != nullptr)
            timeModeSegments->setBounds (inner.removeFromTop (rowHeight));
        else
            inner.removeFromTop (rowHeight);
        inner.removeFromTop (rowGap);

        const int selectedMode = timeModeCombo.getSelectedItemIndex();
        const bool timedMode = selectedMode >= 0
                                 ? selectedMode != 0
                                 : cm::choiceValue (proc.apvts, "timeMode") != 0;
        const int selectedClock = clockSourceCombo.getSelectedItemIndex();
        const bool internalClock = selectedClock >= 0
                                     ? selectedClock == 1
                                     : cm::choiceValue (proc.apvts, "clockSource") == 1;
        auto clockRow = inner.removeFromTop (rowHeight);
        if (clockSourceSegments != nullptr)
            layoutPair (clockRow, clockSourceLabel, *clockSourceSegments,
                        gridDivisionLabel, gridDivisionCombo);
        inner.removeFromTop (rowGap);

        auto durationRow = inner.removeFromTop (rowHeight);
        if (timedMode && internalClock)
        {
            constexpr int gap = 5;
            auto labels = durationRow.removeFromTop (11);
            const int columnWidth = (labels.getWidth() - gap * 2) / 3;
            noteDurationLabel.setBounds (labels.removeFromLeft (columnWidth));
            labels.removeFromLeft (gap);
            internalBpmLabel.setBounds (labels.removeFromLeft (columnWidth));
            labels.removeFromLeft (gap);
            ensembleSameNoteLabel.setBounds (labels);

            noteDurationCombo.setBounds (durationRow.removeFromLeft (columnWidth));
            durationRow.removeFromLeft (gap);
            internalBpmSlider.setBounds (durationRow.removeFromLeft (columnWidth));
            durationRow.removeFromLeft (gap);
            ensembleSameNoteCombo.setBounds (durationRow);
        }
        else
        {
            internalBpmLabel.setBounds ({});
            internalBpmSlider.setBounds ({});
            layoutPair (durationRow, noteDurationLabel, noteDurationCombo,
                        ensembleSameNoteLabel, ensembleSameNoteCombo);
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

    // Pitch mapping: a real catalog/scale preview, then compact semantic fields.
    {
        pitchSystemCombo.setBounds ({});
        auto inner = pitchCardBounds.reduced (13);
        inner.removeFromTop (25);
        if (pitchSystemSegments != nullptr)
            pitchSystemSegments->setBounds (inner.removeFromTop (24));
        inner.removeFromTop (3);

        const bool showSpectrum = inner.getHeight() >= 108;
        if (pitchSpectrumDisplay != nullptr)
        {
            pitchSpectrumDisplay->setBounds (showSpectrum ? inner.removeFromTop (
                juce::jlimit (46, 66, inner.getHeight() - 62)) : juce::Rectangle<int>());
        }
        if (showSpectrum)
            inner.removeFromTop (3);

        auto layoutPair = [] (juce::Rectangle<int> row,
                              juce::Label& leftLabel, juce::Component& leftControl,
                              juce::Label& rightLabel, juce::Component& rightControl,
                              double leftRatio = 0.5)
        {
            constexpr int gap = 6;
            auto labels = row.removeFromTop (10);
            const int leftWidth = juce::jlimit (36, labels.getWidth() - gap - 36,
                (int) std::round ((double) (labels.getWidth() - gap) * leftRatio));
            leftLabel.setBounds (labels.removeFromLeft (leftWidth));
            labels.removeFromLeft (gap);
            rightLabel.setBounds (labels);
            leftControl.setBounds (row.removeFromLeft (leftWidth));
            row.removeFromLeft (gap);
            rightControl.setBounds (row);
        };

        auto layoutThree = [] (juce::Rectangle<int> row,
                               std::array<juce::Label*, 3> labels,
                               std::array<juce::Component*, 3> controls)
        {
            constexpr int gap = 6;
            auto labelArea = row.removeFromTop (10);
            const int width = (labelArea.getWidth() - gap * 2) / 3;
            for (size_t index = 0; index < controls.size(); ++index)
            {
                const int fieldWidth = index + 1 == controls.size()
                    ? labelArea.getWidth() : width;
                labels[index]->setBounds (labelArea.removeFromLeft (fieldWidth));
                controls[index]->setBounds (row.removeFromLeft (fieldWidth));
                if (index + 1 != controls.size())
                {
                    labelArea.removeFromLeft (gap);
                    row.removeFromLeft (gap);
                }
            }
        };

        const int fieldGap = 3;
        const int fieldHeight = juce::jmax (25, (inner.getHeight() - fieldGap) / 2);
        const bool atomic = cm::choiceValue (proc.apvts, "pitchSystem") == 1;
        if (atomic)
        {
            layoutPair (inner.removeFromTop (fieldHeight),
                        atomicElementLabel, atomicElementCombo,
                        atomicModeLabel, atomicModeCombo, 0.58);
            inner.removeFromTop (fieldGap);
            layoutThree (inner.removeFromTop (fieldHeight),
                         {{ &rootLabel, &rootOctaveLabel, &octavesLabel }},
                         {{ &rootCombo, &rootOctaveCombo, &octavesSlider }});
        }
        else
        {
            scaleLabel.setBounds (inner.removeFromTop (10));
            scaleCombo.setBounds (inner.removeFromTop (juce::jmax (15, fieldHeight - 10)));
            inner.removeFromTop (fieldGap);
            layoutThree (inner.removeFromTop (fieldHeight),
                         {{ &rootLabel, &rootOctaveLabel, &octavesLabel }},
                         {{ &rootCombo, &rootOctaveCombo, &octavesSlider }});
        }
    }

    // MIDI protocol, routing, destination and panic share one coherent card.
    {
        auto inner = midiCardBounds.reduced (13);
        inner.removeFromTop (25);
        panicButton.setBounds (inner.removeFromBottom (juce::jlimit (27, 34,
            (int) std::round ((double) midiCardBounds.getHeight() * 0.12))));
        inner.removeFromBottom (5);

        const bool showDetail = inner.getHeight() >= 155;
        destinationDetailLabel.setVisible (showDetail);
        if (showDetail)
        {
            destinationDetailLabel.setBounds (inner.removeFromBottom (16));
            inner.removeFromBottom (2);
        }
        else
        {
            destinationDetailLabel.setBounds ({});
        }
        destinationStatusLabel.setBounds (inner.removeFromBottom (17));
        inner.removeFromBottom (3);

        constexpr int rowGap = 3;
        const int rowHeight = juce::jmax (21, (inner.getHeight() - rowGap * 3) / 4);
        auto outputRow = inner.removeFromTop (rowHeight);
        inner.removeFromTop (rowGap);
        auto capacityRow = inner.removeFromTop (rowHeight);
        inner.removeFromTop (rowGap);
        auto routingRow = inner.removeFromTop (rowHeight);
        inner.removeFromTop (rowGap);
        auto destinationRow = inner.removeFromTop (rowHeight);

        auto splitLabelControl = [] (juce::Rectangle<int> row, juce::Label& label,
                                     juce::Component& control)
        {
            const int labelHeight = juce::jmin (10, juce::jmax (7, row.getHeight() / 3));
            label.setBounds (row.removeFromTop (labelHeight));
            control.setBounds (row);
        };

        const int mode = cm::choiceValue (proc.apvts, "midiOutputType");
        splitLabelControl (outputRow, midiTypeLabel, midiTypeCombo);
        splitLabelControl (capacityRow, sourceCapacityLabel, sourceCapacityCombo);

        if (mode == 1)
        {
            const bool fixed = cm::choiceValue (proc.apvts, "normalMidiRoutingMode") == 0;
            auto labels = routingRow.removeFromTop (juce::jmin (10, routingRow.getHeight() / 3));
            auto controls = routingRow;
            const int leftWidth = fixed
                ? (int) std::round ((double) (controls.getWidth() - 7) * 0.62)
                : controls.getWidth();
            normalRoutingLabel.setBounds (labels.removeFromLeft (leftWidth));
            normalRoutingCombo.setBounds (controls.removeFromLeft (leftWidth));
            if (fixed)
            {
                labels.removeFromLeft (7);
                controls.removeFromLeft (7);
                normalChannelLabel.setBounds (labels);
                normalChannelCombo.setBounds (controls);
            }
        }

        auto destinationLabels = destinationRow.removeFromTop (
            juce::jmin (10, destinationRow.getHeight() / 3));
        destinationLabel.setBounds (destinationLabels);
        rescanButton.setBounds (destinationRow.removeFromRight (64));
        destinationRow.removeFromRight (6);
        destinationCombo.setBounds (destinationRow);
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
    oscPathLabel.setText ("/cs/{zone}/{source}/finger0  |  u/v/on  |  0-1  |  immediate",
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
    if (showConsolePage)
    {
        setPerformControlsVisible (false);
        return;
    }

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
    const bool ensemble = timeMode == 2;
    ensembleSameNoteLabel.setEnabled (ensemble);
    ensembleSameNoteCombo.setEnabled (ensemble);

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

    // ComboBoxes remain attached to APVTS but are intentionally presentation-
    // hidden. Their segmented proxies mirror host automation and state recall.
    pitchSystemCombo.setVisible (false);
    timeModeCombo.setVisible (false);
    clockSourceCombo.setVisible (false);
    timeModeLabel.setVisible (false);
    if (pitchSystemSegments != nullptr)
    {
        pitchSystemSegments->setVisible (true);
        pitchSystemSegments->syncFromTarget();
    }
    if (timeModeSegments != nullptr)
    {
        timeModeSegments->setVisible (true);
        timeModeSegments->setEnabled (true);
        timeModeSegments->syncFromTarget();
    }
    if (clockSourceSegments != nullptr)
    {
        clockSourceSegments->setVisible (true);
        clockSourceSegments->setEnabled (timed);
        clockSourceSegments->syncFromTarget();
    }
    clockSourceLabel.setEnabled (timed);
    if (pitchSpectrumDisplay != nullptr)
    {
        pitchSpectrumDisplay->setVisible (true);
        pitchSpectrumDisplay->repaint();
    }

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
    const int activeNotes = proc.getScheduledMidiNoteCount();

    activeSourcesValue.setText (juce::String (activeSources), juce::dontSendNotification);
    activeFingersValue.setText (juce::String (activeFingers), juce::dontSendNotification);
    notesSentValue.setText (cm::compactCount (notesSent), juce::dontSendNotification);
    activeNotesValue.setText (juce::String (activeNotes), juce::dontSendNotification);
    activeSourcesValue.setDescription (juce::String (activeSources) + " active OSC or simulator sources.");
    activeFingersValue.setDescription (juce::String (activeFingers) + " active finger0 touches.");
    notesSentValue.setDescription (juce::String (notesSent) + " MIDI note attacks sent.");
    activeNotesValue.setDescription (juce::String (activeNotes) + " active scheduled MIDI notes.");

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
                            "Notes Only sends the nearest MIDI note.";
        pitchSystemCombo.setTooltip (detail);
        pitchSystemCombo.setDescription (detail);
    }
    else
    {
        pitchSystemCombo.setTooltip ("Seven fixed 12-TET tonal maps. Horizontal OSC position selects the scale step.");
        pitchSystemCombo.setDescription (pitchSystemCombo.getTooltip());
    }

    static constexpr const char* divisions[] { "1/4", "1/8", "1/16", "1/32" };
    static constexpr const char* durations[] { "2n", "4n", "8n", "16n", "32n" };
    static constexpr double durationQuarterNotes[] { 2.0, 1.0, 0.5, 0.25, 0.125 };
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
    const auto effectivePolicy = proc.getEffectiveTimeFieldPolicy();
    const bool schedulerTimed = effectivePolicy.mode
                              != CrowdTimeField::Mode::Flow;
    const bool schedulerEnsemble = effectivePolicy.mode
                                 == CrowdTimeField::Mode::Ensemble;
    const bool governorAdaptive = cm::choiceValue (proc.apvts, "crowdGovernorEnabled") != 0;
    const int observedCrowd = juce::jmax (0, proc.getGovernorObservedDensity());
    const int effectiveAttacks = juce::jmax (1, proc.getGovernorEffectiveAttacksPerStep());
    const int effectiveActiveLimit = juce::jmax (1, proc.getGovernorEffectiveActiveVoices());
    const int effectiveSpread = juce::jmax (1, proc.getGovernorEffectiveSpreadSlots());
    const auto observedSourceWord = observedCrowd == 1 ? " source" : " sources";
    const auto spreadStepWord = effectiveSpread == 1 ? " grid step" : " grid steps";
    const bool clockLocked = proc.getTimeFieldClockLocked();
    const int pending = proc.getTimeFieldPending();
    const int timeFieldActive = proc.getTimeFieldActive();
    const auto merged = proc.getTimeFieldMerged();
    const auto bpm = juce::String (proc.getTimeFieldBpm(), 0);
    const auto grid = juce::String (divisions[divisionIndex]);
    const int durationIndex = juce::jlimit (
        0, 4, cm::choiceValue (proc.apvts, "noteDuration"));
    const auto durationText = juce::String (durations[durationIndex]);
    const bool ensembleRetrigger = cm::choiceValue (
        proc.apvts, "ensembleSameNoteMode") == 1;
    const auto sameNoteText = juce::String (
        ensembleRetrigger ? "RETRIGGER" : "TIE");
    const double durationMs = 60000.0 * durationQuarterNotes[durationIndex]
                            / juce::jmax (1.0, proc.getNoteDurationBpm());
    const auto durationDescription = durationText + " produces approximately "
        + juce::String (durationMs, 1)
        + " ms notes at the current tempo. Attack start times are unchanged.";
    noteDurationCombo.setDescription (durationDescription);
    noteDurationCombo.setTooltip (durationDescription);
    const auto sameNoteDescription = ensembleRetrigger
        ? "Ensemble Retrigger sends a safe Note Off then Note On when the same pitch is admitted again, so every pulse has a new attack. Flow and Grid still use Tie."
        : "Ensemble Tie extends an identical sounding note instead of starting another attack. This preserves the historical behaviour. Flow and Grid also use Tie.";
    ensembleSameNoteCombo.setDescription (sameNoteDescription);
    ensembleSameNoteCombo.setTooltip (sameNoteDescription);

    juce::String primaryTimeStatus;
    juce::String clockDescription;
    if (liveTimeMode == 0)
    {
        primaryTimeStatus = "FLOW  /  DIRECT  /  " + durationText + " NOTE";
        clockDescription = "Flow sends attacks directly; only the fixed "
                         + durationText + " note lifetime follows tempo.";
    }
    else if (hostClock && clockLocked)
    {
        primaryTimeStatus = "HOST LOCK  /  " + bpm + " BPM  /  " + grid
                          + "  /  " + durationText;
        clockDescription = "Time Field is locked to the running host transport at "
                         + bpm + " BPM on the " + grid + " grid.";
    }
    else if (hostClock)
    {
        // Host transport may be stopped or absent in Standalone. Scheduling is
        // still active: every instance shares the monotonic-seconds fallback.
        primaryTimeStatus = "FREE CLOCK  /  " + bpm + " BPM  /  " + grid
                          + "  /  " + durationText;
        clockDescription = "Host transport is stopped or unavailable. Time Field remains active on the shared monotonic clock at "
                         + bpm + " BPM on the " + grid + " grid.";
    }
    else
    {
        primaryTimeStatus = "INTERNAL  /  " + bpm + " BPM  /  " + grid
                          + "  /  " + durationText;
        clockDescription = "Time Field is running from its internal clock at "
                         + bpm + " BPM on the " + grid + " grid.";
    }

    if (liveTimeMode == 2)
    {
        primaryTimeStatus += "  /  " + sameNoteText;
        clockDescription += " Same-pitch Ensemble pulses use "
                          + sameNoteText.toLowerCase() + " articulation.";
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
        + juce::String (observedCrowd) + observedSourceWord
        + ". If the limit falls below voices that are already sounding, those voices release naturally and new attacks wait.");
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

    // The packed snapshot is the policy the audio scheduler actually received
    // after Manual/Adaptive, Safety Governor and Global Conductor clamps. Do
    // not infer load from the controls: those can be less restrictive than the
    // final venue policy and would make a genuinely full queue look normal.
    const int activeLimit = juce::jmax (0, effectivePolicy.activeLimit);
    const int attacksPerStep = juce::jmax (0, effectivePolicy.attacksPerStep);
    const int spreadSteps = juce::jmax (1, effectivePolicy.spreadSlots);
    const int oneSchedulingWindowCapacity = attacksPerStep
                                          * (schedulerEnsemble ? spreadSteps : 1);
    const bool admissionBlocked = schedulerTimed && pending > 0
                               && ! effectivePolicy.admissionOpen;
    const bool atActiveLimit = schedulerTimed && pending > 0
                            && activeLimit > 0
                            && timeFieldActive >= activeLimit;
    const bool queuePressure = schedulerTimed
                            && pending > oneSchedulingWindowCapacity;
    const bool highLoad = admissionBlocked || atActiveLimit || queuePressure;
    const bool mergeActivity = schedulerTimed && nowMs < mergeActivityUntilMs;
    const auto queueState = "PENDING " + juce::String (pending)
                          + "  /  TF ACTIVE " + juce::String (timeFieldActive)
                          + "  /  MERGED " + juce::String (merged);
    auto loadDescription = liveTimeMode == 0
                         ? "Direct signal path with " + juce::String (timeFieldActive)
                             + " active touches."
                         : queueState
                             + ". Merged counts pending work whose admission window elapsed. Held intent is renewed; released short taps may expire. It is not a packet-loss count."
                             + (admissionBlocked
                                  ? " Final venue policy is holding new attacks while releases and cancellations continue."
                                  : atActiveLimit
                                  ? " The active-voice limit is currently full and attacks remain queued."
                                  : queuePressure
                                      ? " High load: the pending queue exceeds one effective scheduling window."
                                  : mergeActivity
                                      ? " Pending work reached its admission lifetime during the latest scheduling window."
                                      : " Scheduler load is within the selected limits.");
    if (governorAdaptive)
        loadDescription += " Adaptive Crowd Governor sees an observed crowd density of "
                         + juce::String (observedCrowd)
                         + observedSourceWord + " and proposes "
                         + juce::String (effectiveAttacks) + " attacks per step, "
                         + juce::String (effectiveActiveLimit) + " active voices and "
                         + juce::String (effectiveSpread) + spreadStepWord + "."
                         + (timeFieldActive > activeLimit && schedulerTimed
                              ? " Existing voices above the new soft limit release naturally; new attacks remain queued."
                              : "")
                         + (liveTimeMode == 0 ? " These limits are bypassed in Flow mode." : "");
    if (schedulerTimed)
        loadDescription += " Final scheduler policy: "
                         + juce::String (attacksPerStep) + " attacks, "
                         + juce::String (activeLimit) + " active, "
                         + juce::String (spreadSteps) + " spread, admission "
                         + (effectivePolicy.admissionOpen ? "open." : "held.");
    timeTelemetryLabel.setText (liveTimeMode == 0
                                  ? governorAdaptive
                                          ? "GOVERNOR BYPASS  /  CROWD " + juce::String (observedCrowd)
                                          + "  /  ACTIVE " + juce::String (timeFieldActive)
                                      : "ATTACKS PASS THROUGH  /  ACTIVE "
                                          + juce::String (timeFieldActive)
                                  : governorAdaptive
                                      ? "ADAPT  /  CROWD " + juce::String (observedCrowd)
                                          + "  /  P" + juce::String (pending)
                                          + "  A" + juce::String (timeFieldActive)
                                          + "  M" + juce::String (merged)
                                  : highLoad
                                      ? "HIGH LOAD  /  P" + juce::String (pending)
                                          + "  /  A" + juce::String (timeFieldActive)
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
        oscDetail = proc.oscStatus.isNotEmpty() ? proc.oscStatus
                  : proc.osc.oscStatus().isNotEmpty() ? proc.osc.oscStatus()
                                                     : "OSC receiver stopped";
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
    const int sourceCapacity = proc.audienceModel.getSourceCapacity();
    const int sourcesPerChannel = sourceCapacity / 16;
    const auto capacityText = juce::String (sourceCapacity) + " participants / "
                            + juce::String (sourcesPerChannel) + " per channel";
    if (midiType == 1)
    {
        const int routingMode = cm::choiceValue (proc.apvts, "normalMidiRoutingMode");
        routingSummaryLabel.setText (routingMode == 1 ? "Source ID  ->  MIDI Ch 1-16"
                                                       : "All sources  ->  Fixed channel",
                                     juce::dontSendNotification);
        if (routingMode == 1)
        {
            routingDetailLabel.setText (capacityText
                                        + "  /  stable ID modulo 16  /  note ownership safe",
                                        juce::dontSendNotification);
        }
        else
        {
            const int channel = cm::choiceValue (proc.apvts, "normalMidiChannel") + 1;
            routingDetailLabel.setText ("Every source/finger0 uses MIDI Channel " + juce::String (channel),
                                        juce::dontSendNotification);
        }
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
                             + juce::String (activeFingers) + " touches  /  "
                             + juce::String ((int) proc.audienceModel.getCapacityDroppedEventCount())
                             + " cap drops",
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

    const int simulatorHeld = proc.simulator.getHeldSeatCount();
    const int simulatorCrowd = proc.simulator.getCrowdParticipantCount();
    const int simulatorCrowdActive = proc.simulator.getActiveCrowdParticipantCount();
    const int simulatorTotal = proc.simulator.getSimSeatCount();
    const bool mixedLiveAndSimulator = oscFresh && simulatorTotal > 0;
    if (mixedLiveAndSimulator)
    {
        simStatusLabel.setText ("LOCAL + OSC INPUT  |  SHARED IDS",
                                juce::dontSendNotification);
        simStatusLabel.setColour (juce::Label::textColourId, cm::amber);
        simStatusLabel.setDescription ("Live OSC and the simulator share source IDs 0 to 255. Clear the simulator before production input to avoid lifecycle collisions.");
    }
    else
    {
        simStatusLabel.setText ("HELD " + juce::String (simulatorHeld)
                                + "  ·  CROWD " + juce::String (simulatorCrowdActive)
                                + " / " + juce::String (simulatorCrowd) + " ACTIVE",
                                juce::dontSendNotification);
        simStatusLabel.setColour (juce::Label::textColourId,
                                  simulatorTotal > 0 ? cm::green : cm::textDim);
        simStatusLabel.setDescription (
            juce::String (simulatorHeld) + " held test touches and "
            + juce::String (simulatorCrowdActive) + " active touches in a stable pool of "
            + juce::String (simulatorCrowd) + " crowd participants.");
    }
    simStatusLabel.setTooltip (simStatusLabel.getDescription());
    simMoveButton.setToggleState (proc.simulator.isRandomMovementOn(), juce::dontSendNotification);
    simProfileCombo.setSelectedItemIndex (
        static_cast<int> (proc.simulator.getProfile()), juce::dontSendNotification);

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

void AudienceEditor::updateConsoleTelemetry()
{
    enum class CheckState { pass, warning, fail, bypassed };
    auto setStatus = [] (juce::Label& label, CheckState state,
                         const juce::String& detail)
    {
        const juce::String prefix = state == CheckState::pass ? "PASS"
                                  : state == CheckState::warning ? "WARN"
                                  : state == CheckState::fail ? "FAIL" : "BYPASS";
        const auto colour = state == CheckState::pass ? cm::green
                          : state == CheckState::warning ? cm::amber
                          : state == CheckState::fail ? cm::red : cm::textMuted;
        const auto text = prefix + "  /  " + detail;
        label.setText (text, juce::dontSendNotification);
        label.setColour (juce::Label::textColourId, colour);
        label.setDescription (text);
        label.setTooltip (text);
    };

    const bool safetyEnabled = cm::choiceValue (proc.apvts, "safetyGovernorEnabled") != 0;
    const int safetyState = juce::jlimit (0, 3, proc.getSafetyGovernorState());
    static constexpr const char* safetyNames[] { "NORMAL", "HIGH", "CRITICAL", "EMERGENCY" };
    const auto safetyName = juce::String (safetyNames[safetyState]);
    setStatus (safetyStateLabel,
               ! safetyEnabled ? CheckState::bypassed
               : safetyState == 0 ? CheckState::pass
               : safetyState < 3 ? CheckState::warning : CheckState::fail,
               safetyEnabled ? safetyName : "DISABLED / manual pressure policy");

    const auto reasonBits = proc.getSafetyGovernorReasonBits();
    juce::StringArray reasons;
    auto addReason = [&] (uint32_t bit, const juce::String& text)
    {
        if ((reasonBits & bit) != 0u)
            reasons.add (text);
    };
    addReason (PressureAwareSafetyGovernor::ReasonIngressRate, "INGRESS");
    addReason (PressureAwareSafetyGovernor::ReasonLifecycleQueue, "LIFECYCLE QUEUE");
    addReason (PressureAwareSafetyGovernor::ReasonMotionDrop, "MOTION DROP");
    addReason (PressureAwareSafetyGovernor::ReasonTimeFieldPending, "TIME FIELD");
    addReason (PressureAwareSafetyGovernor::ReasonExternalFifo, "EXTERNAL FIFO");
    addReason (PressureAwareSafetyGovernor::ReasonExternalFifoAge, "FIFO AGE");
    addReason (PressureAwareSafetyGovernor::ReasonProcessDeadline, "DEADLINE");
    addReason (PressureAwareSafetyGovernor::ReasonInvalidInput, "INVALID INPUT");
    addReason (PressureAwareSafetyGovernor::ReasonInvalidClock, "CLOCK");
    addReason (PressureAwareSafetyGovernor::ReasonRecoveryHeld, "RECOVERY HOLD");
    safetyReasonLabel.setText (reasons.isEmpty() ? "NO ACTIVE PRESSURE FLAGS"
                                                 : reasons.joinIntoString (" + "),
                               juce::dontSendNotification);
    safetyReasonLabel.setColour (juce::Label::textColourId,
                                 reasons.isEmpty() ? cm::textMuted
                                                   : safetyState >= 3 ? cm::red : cm::amber);
    safetyReasonLabel.setDescription ("Current pressure causes: "
                                      + (reasons.isEmpty() ? juce::String ("none")
                                                           : reasons.joinIntoString (", ")) + ".");
    safetyReasonLabel.setTooltip (safetyReasonLabel.getDescription());

    auto finiteOrZero = [] (double value) noexcept
    {
        return std::isfinite (value) && value >= 0.0 ? value : 0.0;
    };
    const double ingress = finiteOrZero (proc.getIngressEventsPerSecond());
    const double deadline = finiteOrZero (proc.getProcessDeadlineRatio());
    const double fifoPressure = finiteOrZero (proc.getExternalFifoPressure());
    const double fifoAge = finiteOrZero (proc.getExternalFifoOldestAgeSeconds());
    safetyIngressLabel.setText (juce::String (ingress, ingress < 100.0 ? 1 : 0) + " evt/s",
                                juce::dontSendNotification);
    safetyDeadlineLabel.setText ("DSP " + juce::String (deadline * 100.0, 1) + "% deadline",
                                 juce::dontSendNotification);
    safetyFifoLabel.setText ("FIFO " + juce::String (fifoPressure * 100.0, 1)
                             + "% / " + juce::String (fifoAge * 1000.0, 1) + " ms",
                             juce::dontSendNotification);
    const int lifecycleDepth = proc.fingerRouter.getLifecycleQueueDepth();
    const int motionDepth = proc.fingerRouter.getMotionQueueDepth();
    safetyQueueLabel.setText ("Q " + juce::String (lifecycleDepth) + "L / "
                              + juce::String (motionDepth) + "M",
                              juce::dontSendNotification);
    safetyQueueLabel.setDescription (
        "Current lifecycle queue " + juce::String (lifecycleDepth)
        + ", motion queue " + juce::String (motionDepth)
        + "; high-water marks " + juce::String ((int) proc.fingerRouter.getLifecycleHighWater())
        + " and " + juce::String ((int) proc.fingerRouter.getMotionHighWater())
        + "; dropped " + juce::String ((int) proc.fingerRouter.getDroppedEventCount())
        + ", coalesced " + juce::String ((int) proc.fingerRouter.getCoalescedMotionEventCount()) + ".");
    safetyQueueLabel.setTooltip (safetyQueueLabel.getDescription());

    const int outputPath = juce::jlimit (0, 2, proc.getMidiOutputPath());
    static constexpr const char* outputPathNames[] { "HOST ONLY", "EXTERNAL ONLY", "MIRROR" };
    const int endpointIndex = proc.getResolvedMidiOutputOptionIndex();
    const bool externalEndpointAvailable = endpointIndex > 0
                                        && ! destinationRouteUnresolved
                                        && proc.isMidiOutputReady();
    const int expectedZone = proc.getExpectedZone();
    const auto expectedZoneText = expectedZone < 0
                                ? juce::String ("ANY")
                                : juce::String::charToString ((juce::juce_wchar) ('A' + expectedZone));
    const bool exclusiveRequested = cm::choiceValue (proc.apvts, "exclusiveUdpPort") != 0;
    const bool exclusiveActive = exclusiveRequested
                              && proc.osc.isExclusive()
                              && proc.osc.isReceiving();
    const bool routeCoherent = outputPath != 1 || externalEndpointAvailable;
    const auto routeState = ! routeCoherent ? CheckState::fail
                          : ! exclusiveActive ? CheckState::warning : CheckState::pass;
    setStatus (routeConsoleStatusLabel, routeState,
               juce::String (outputPathNames[outputPath]) + " / ZONE " + expectedZoneText
               + " / UDP " + juce::String (proc.getUdpPort())
               + (outputPath == 0 ? " / HOST BUS"
                  : externalEndpointAvailable ? " / ENDPOINT READY" : " / NO EXTERNAL ENDPOINT"));

    const int matchingFactoryPreset = proc.getMatchingFactoryPresetIndex();
    const auto freshRouteState = proc.getFreshRouteAssignmentState();
    const bool freshRoutePending =
        freshRouteState == AudienceProcessor::FreshRouteAssignmentState::pending;
    const bool freshRouteAssigned =
        freshRouteState == AudienceProcessor::FreshRouteAssignmentState::assigned;
    const bool freshRouteExhausted =
        freshRouteState == AudienceProcessor::FreshRouteAssignmentState::exhausted;
    autoAssignRetryButton.setEnabled (freshRouteExhausted);
    autoAssignRetryButton.setButtonText (freshRouteExhausted ? "RETRY AUTO"
                                                             : "AUTO READY");
    refreshingFactoryPreset = true;
    if (matchingFactoryPreset >= 0)
        factoryPresetCombo.setSelectedItemIndex (matchingFactoryPreset,
                                                  juce::dontSendNotification);
    else
        factoryPresetCombo.setSelectedId (0, juce::dontSendNotification);
    refreshingFactoryPreset = false;
    const auto factoryState = freshRouteExhausted
                            ? "NO FREE UDP PORT  /  6062-6069  /  FAIL-CLOSED"
                            : freshRoutePending
                                ? juce::String ("AUTO ASSIGNING  /  6062-6069")
                                : matchingFactoryPreset >= 0
                                    ? (freshRouteAssigned
                                        ? "AUTO ASSIGNED  /  "
                                        : "ACTIVE  /  ")
                                        + AudienceProcessor::getFactoryPresetName(
                                            matchingFactoryPreset)
                                    : juce::String ("CUSTOM  /  SAVED PROJECT STATE");
    factoryPresetStatusLabel.setText (factoryState, juce::dontSendNotification);
    factoryPresetStatusLabel.setColour (
        juce::Label::textColourId,
        freshRouteExhausted ? cm::red
        : freshRoutePending ? cm::amber
        : matchingFactoryPreset >= 0 ? cm::green : cm::textMuted);
    factoryPresetStatusLabel.setDescription (
        freshRouteExhausted
            ? "Every factory UDP port is occupied. No OSC receiver, virtual MIDI endpoint "
              "or Global Conductor registration was opened; retry after a port is released."
        : freshRoutePending
            ? "Cosmic Microwave is claiming the lowest free retained exclusive factory route."
        : matchingFactoryPreset >= 0
            ? "The screenshot-aligned factory performance baseline is active. "
              "Venue Preflight may still require Safety Governor activation."
            : "One or more controls differ from every factory Zone A-H preset. "
              "The current custom state will still be stored by the host project.");
    factoryPresetStatusLabel.setTooltip (factoryPresetStatusLabel.getDescription());

    int warnings = 0;
    int failures = 0;
    auto preflight = [&] (int index, CheckState state, const juce::String& detail)
    {
        setStatus (preflightRows[(size_t) index], state, detail);
        if (state == CheckState::warning || state == CheckState::bypassed)
            ++warnings;
        else if (state == CheckState::fail)
            ++failures;
    };

    const bool oscListening = proc.osc.isRunning() && proc.osc.isReceiving();
    const auto validMessages = proc.osc.getValidMessageCount();
    const auto ageMs = proc.osc.getLastValidMessageAgeMs();
    const bool oscRecent = oscListening && validMessages > 0u && ageMs <= 1500u;
    preflight (0, ! oscListening ? CheckState::fail
                  : oscRecent ? CheckState::pass : CheckState::warning,
               ! oscListening ? "OSC RECEIVER DOWN"
               : oscRecent ? "OSC LIVE / " + juce::String ((int) ageMs) + " ms"
                           : validMessages == 0u ? "LISTENING / WAITING FOR OSC"
                                                : "OSC STALE / " + juce::String ((double) ageMs / 1000.0, 1) + " s");

    const auto zoneMismatchCount = proc.osc.getZoneMismatchCount();
    preflight (1, zoneMismatchCount > 0u ? CheckState::fail
                  : expectedZone < 0 ? CheckState::fail : CheckState::pass,
               zoneMismatchCount > 0u ? juce::String ((int) zoneMismatchCount) + " ZONE MISMATCH"
               : expectedZone < 0 ? "EXPECTED ZONE IS ANY"
                                  : "ZONE " + expectedZoneText + " LOCKED");

    preflight (2, exclusiveActive ? CheckState::pass : CheckState::fail,
               exclusiveActive ? "UDP PORT OWNED EXCLUSIVELY"
                               : exclusiveRequested ? "EXCLUSIVE BIND NOT ACTIVE"
                                                    : "EXCLUSIVE OWNERSHIP DISABLED");

    preflight (3, ! routeCoherent ? CheckState::fail
                  : outputPath == 2 && ! externalEndpointAvailable ? CheckState::warning
                                                                  : CheckState::pass,
               ! routeCoherent ? "EXTERNAL ONLY / ENDPOINT MISSING"
               : outputPath == 0 ? "HOST ONLY / ISOLATED"
               : outputPath == 1 ? "EXTERNAL ONLY / ENDPOINT READY"
               : externalEndpointAvailable ? "MIRROR / BOTH PATHS READY"
                                           : "MIRROR / HOST FALLBACK ONLY");

    preflight (4, ! safetyEnabled ? CheckState::fail
                  : safetyState == 3 ? CheckState::fail
                  : safetyState == 0 ? CheckState::pass : CheckState::warning,
               ! safetyEnabled ? "SAFETY GOVERNOR DISABLED"
                              : "SAFETY " + safetyName);

    const int timeMode = cm::choiceValue (proc.apvts, "timeMode");
    const bool hostClock = cm::choiceValue (proc.apvts, "clockSource") == 0;
    const bool clockLocked = proc.getTimeFieldClockLocked();
    preflight (5, timeMode == 0 ? CheckState::warning
                  : hostClock && ! clockLocked ? CheckState::warning : CheckState::pass,
               timeMode == 0 ? "FLOW / TIME GRID BYPASSED"
               : hostClock && clockLocked ? "HOST CLOCK LOCKED"
               : hostClock ? "HOST FREE-CLOCK FALLBACK"
                           : "INTERNAL CLOCK / " + juce::String (proc.getTimeFieldBpm(), 1) + " BPM");

    const int conductorRole = cm::choiceValue (proc.apvts, "conductorRole");
    const int conductorRegistration = proc.getConductorRegistrationStatus();
    const int conductorSource = proc.getConductorSnapshotSource();
    const bool conductorRegistered = conductorRegistration == 0;
    const bool conductorGlobal = conductorSource == 2;
    preflight (6, conductorRole == 0 ? CheckState::bypassed
                  : ! conductorRegistered ? CheckState::fail
                  : conductorGlobal ? CheckState::pass : CheckState::warning,
               conductorRole == 0 ? "GLOBAL CONDUCTOR OFF"
               : ! conductorRegistered ? "CONDUCTOR REGISTRATION FAILED"
               : conductorGlobal ? "GLOBAL ALLOCATION LIVE"
                                 : "LOCAL FALLBACK / WAITING FOR LEADER");

    const auto quality = proc.getSourceQualityOutput();
    const int qualityTarget = juce::jmax (1, quality.expectedSources);
    sourceQualityButton.setButtonText (
        quality.armed ? "STOP " + juce::String (qualityTarget) + " CHECK"
                      : "START " + juce::String (qualityTarget) + " CHECK");

    const auto qualityState = ! quality.armed ? CheckState::bypassed
                            : quality.simulatorActive ? CheckState::fail
                            : quality.state == SourceQualityController::State::READY
                                ? CheckState::pass
                            : quality.state == SourceQualityController::State::DEGRADED
                                  && quality.admissionOpen
                                ? CheckState::warning
                            : CheckState::fail;
    const auto gateDetail = ! quality.armed
        ? juce::String ("NOT ARMED / NEW ATTACKS OPEN")
        : quality.simulatorActive
            ? juce::String ("BLOCKED / SIMULATOR ACTIVE / CLEAR BEFORE CHECK")
        : quality.state == SourceQualityController::State::WARMING
            ? "WARMING / NEW ATTACKS HELD / CLEAN "
                + juce::String (quality.readinessHoldProgress * 100.0, 0) + "%"
        : quality.state == SourceQualityController::State::READY
            ? juce::String ("READY LATCHED / NEW ATTACKS OPEN")
        : quality.admissionOpen
            ? juce::String ("DEGRADED / READY LATCHED / NEW ATTACKS OPEN")
            : juce::String ("HARD FAULT / NEW ATTACKS HELD / RESTART REQUIRED");
    setStatus (sourceQualityGateLabel, qualityState, gateDetail);
    sourceQualityGateLabel.setDescription (
        "The operator-armed gate holds only new attacks until the local accepted-live-OSC "
        "signal census is complete and stable. It is not a server roster check or a "
        "60-second production soak. Note Off, watchdog Cancel and already sounding notes "
        "always remain release-safe.");
    sourceQualityGateLabel.setTooltip (sourceQualityGateLabel.getDescription());

    const auto coverageState = ! quality.armed ? CheckState::bypassed
                             : quality.readyLatched ? CheckState::pass
                             : quality.qualifiedSources == qualityTarget
                                  && quality.activeSources == qualityTarget
                                ? CheckState::pass : CheckState::warning;
    setStatus (sourceQualityCoverageLabel, coverageState,
               "CENSUS " + juce::String (quality.observedSources) + "/"
                   + juce::String (qualityTarget)
                   + " / U+V+ON " + juce::String (quality.qualifiedSources) + "/"
                   + juce::String (qualityTarget)
                   + (quality.readyLatched ? " / ACTIVE NOW "
                                           : " / ACTIVE REQUIRED ")
                   + juce::String (quality.activeSources) + "/"
                   + juce::String (qualityTarget));
    sourceQualityCoverageLabel.setDescription (
        "Local accepted OSC signal census over the exact dense source domain 0.."
        + juce::String (qualityTarget - 1)
        + ". Qualification requires every identity to send U, V and On since START, "
          "and every identity must remain active with both heartbeat axes during the "
          "clean hold. After READY, ACTIVE is current telemetry only. This does not "
          "prove the server roster or replace the 60-second soak.");
    sourceQualityCoverageLabel.setTooltip (
        sourceQualityCoverageLabel.getDescription());

    const bool hotTraffic = SourceQualityController::hasReason(
        quality.reasonBits, SourceQualityController::ReasonHotSource);
    const bool invalidQualityClock = SourceQualityController::hasReason(
        quality.reasonBits, SourceQualityController::ReasonInvalidClock);
    const auto timingState = ! quality.armed ? CheckState::bypassed
                           : quality.staleActiveSources > 0 || hotTraffic
                                  || quality.aggregateRateHigh || invalidQualityClock
                                ? CheckState::warning : CheckState::pass;
    const auto heartbeatText = quality.maxActiveHeartbeatAgeMs
                                   == std::numeric_limits<std::uint32_t>::max()
        ? juce::String ("INVALID")
        : juce::String ((int) quality.maxActiveHeartbeatAgeMs) + " ms";
    setStatus (sourceQualityTimingLabel, timingState,
               "MOTION " + juce::String (quality.totalMotionEventsPerSecond, 1)
                   + " evt/s / MAX "
                   + juce::String (quality.maxSourceMotionEventsPerSecond, 1)
                   + " / U+V HB " + heartbeatText
                   + " / STALE " + juce::String (quality.staleActiveSources)
                   + " / HOT " + juce::String (quality.hotSources)
                   + " / TOP " + juce::String (quality.topTalkerShare * 100.0, 0) + "%"
                   + " / AGG " + (quality.aggregateRateHigh ? "HIGH" : "OK"));
    sourceQualityTimingLabel.setDescription (
        "Per-source and aggregate accepted U/V motion rate, plus the oldest required "
        "U-and-V heartbeat among currently held live sources. A stationary held phone "
        "must continue both heartbeat axes. Simulator traffic cannot satisfy this local "
        "signal census and blocks the check while active.");
    sourceQualityTimingLabel.setTooltip (sourceQualityTimingLabel.getDescription());

    const bool hardQualityFault = quality.capacityDropCount > 0u
                               || quality.lifecycleDropCount > 0u;
    const bool lifecycleQualityFault = quality.duplicateOnCount > 0u
                                    || quality.orphanOffCount > 0u
                                    || quality.watchdogCancelCount > 0u;
    const auto faultState = ! quality.armed ? CheckState::bypassed
                          : hardQualityFault ? CheckState::fail
                          : lifecycleQualityFault || quality.motionDropCount > 0u
                              ? CheckState::warning
                                                  : CheckState::pass;
    setStatus (sourceQualityFaultLabel, faultState,
               "DUP " + juce::String ((int) quality.duplicateOnCount)
                   + " / ORPHAN " + juce::String ((int) quality.orphanOffCount)
                   + " / CANCEL " + juce::String ((int) quality.watchdogCancelCount)
                   + " / CAP " + juce::String ((int) quality.capacityDropCount)
                   + " / MDROP " + juce::String ((int) quality.motionDropCount)
                   + " / LDROP " + juce::String ((int) quality.lifecycleDropCount));
    sourceQualityFaultLabel.setDescription (
        "Incidents in the current local check epoch. Capacity or lifecycle-message "
        "drops latch a hard fault and hold new attacks until a fresh check. Motion "
        "drops and lifecycle anomalies remain warnings without cutting already "
        "sounding notes.");
    sourceQualityFaultLabel.setTooltip (sourceQualityFaultLabel.getDescription());

    preflight (7, qualityState,
               ! quality.armed ? "SOURCE SIGNAL CENSUS BYPASSED"
               : quality.simulatorActive
                    ? "LOCAL SIGNAL CENSUS BLOCKED / SIMULATOR ACTIVE"
               : quality.state == SourceQualityController::State::READY
                    ? "LOCAL SIGNAL CENSUS READY / 0.."
                        + juce::String (qualityTarget - 1)
                        + " / SERVER ROSTER EXTERNAL"
               : quality.state == SourceQualityController::State::DEGRADED
                    ? gateDetail
                    : "LOCAL SIGNAL CENSUS "
                        + juce::String (quality.qualifiedSources) + "/"
                        + juce::String (qualityTarget));

    const auto summaryState = failures > 0 ? CheckState::fail
                            : warnings > 0 ? CheckState::warning : CheckState::pass;
    setStatus (preflightSummaryLabel, summaryState,
               failures > 0 ? "COSMIC LOCAL PREFLIGHT / "
                                + juce::String (failures) + " BLOCKER"
                                + (failures == 1 ? juce::String() : "S")
                                + " / " + juce::String (warnings) + " ADVISORY"
              : warnings > 0 ? "COSMIC LOCAL PREFLIGHT / PASS WITH "
                                   + juce::String (warnings) + " ADVISORY"
                             : "COSMIC LOCAL PREFLIGHT / ALL LOCAL CHECKS PASS");

    static constexpr const char* registrationNames[] {
        "REGISTERED", "INVALID PORT", "DUPLICATE PORT", "CAPACITY FULL"
    };
    const int safeRegistration = juce::jlimit (0, 3, conductorRegistration);
    static constexpr const char* sourceNames[] { "LOCAL FALLBACK", "BYPASSED", "GLOBAL" };
    const int safeSource = juce::jlimit (0, 2, conductorSource);
    const auto conductorState = conductorRole == 0 ? CheckState::bypassed
                              : ! conductorRegistered ? CheckState::fail
                              : conductorGlobal ? CheckState::pass : CheckState::warning;
    setStatus (conductorStatusLabel, conductorState,
               juce::String (registrationNames[safeRegistration]) + " / "
               + sourceNames[safeSource]);
    conductorQuotaLabel.setText (
        "A " + juce::String (proc.getConductorAttackQuota())
        + "  /  V " + juce::String (proc.getConductorVoiceQuota())
        + "  /  " + juce::String (proc.getConductorActiveZoneCount())
        + " ZONES  /  LEADER UDP " + juce::String (proc.getConductorLeaderPort()),
        juce::dontSendNotification);
    conductorQuotaLabel.setColour (juce::Label::textColourId,
                                   conductorGlobal ? cm::cyan : cm::textMuted);
    conductorQuotaLabel.setDescription ("Current fair allocation: "
        + juce::String (proc.getConductorAttackQuota()) + " attacks, "
        + juce::String (proc.getConductorVoiceQuota()) + " voices across "
        + juce::String (proc.getConductorActiveZoneCount()) + " active zones; leader UDP port "
        + juce::String (proc.getConductorLeaderPort()) + ".");
    conductorQuotaLabel.setTooltip (conductorQuotaLabel.getDescription());

    const auto macroStateText = "NOTES ONLY  /  U=PITCH  /  V=VELOCITY\n"
                                "NO MUSICAL CC / PITCH BEND / MPE";
    macroStatusLabel.setText (macroStateText, juce::dontSendNotification);
    macroStatusLabel.setColour (juce::Label::textColourId, cm::green);
    const auto macroDescription = "Cosmic Microwave v2.6 notes-only MIDI policy. "
        "Musical controller and MPE output are disabled; only panic safety uses CC120 and CC123.";
    macroStatusLabel.setDescription (macroDescription);
    macroStatusLabel.setTooltip (macroDescription);
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
    updateConsoleTelemetry();
    if (sourceMap != nullptr && ! showConsolePage)
        sourceMap->repaint();
}
