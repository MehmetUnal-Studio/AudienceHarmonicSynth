#include <JuceHeader.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <algorithm>
#include <array>
#include <atomic>

#include "MidiEngine.h"
#include "OscBridge.h"
#include "Simulator.h"

namespace
{
    constexpr int defaultUdpPort = 6060;
    constexpr double deviceSampleRate = 44100.0;
    constexpr int timerHz = 60;
    constexpr int midiBlockSamples = (int) (deviceSampleRate / (double) timerHz);

    const juce::StringArray rootNames { "C", "C#", "D", "D#", "E", "F",
                                        "F#", "G", "G#", "A", "A#", "B" };

    const juce::StringArray scaleNames { "Major", "Natural Minor", "Pentatonic", "Dorian",
                                         "Lydian", "Harmonic Minor", "Whole Tone" };

    juce::String rowName (int row)
    {
        return juce::String::charToString((juce::juce_wchar) ('A' + juce::jlimit(0, 25, row)));
    }

    juce::Colour bg()       { return juce::Colour(0xff080c12); }
    juce::Colour panel()    { return juce::Colour(0xff101722); }
    juce::Colour stroke()   { return juce::Colour(0xff243142); }
    juce::Colour text()     { return juce::Colour(0xffe7edf7); }
    juce::Colour muted()    { return juce::Colour(0xff8a92a4); }
    juce::Colour blue()     { return juce::Colour(0xff5cb8ff); }
    juce::Colour green()    { return juce::Colour(0xff5df18a); }
    juce::Colour red()      { return juce::Colour(0xffff826f); }

    void styleLabel (juce::Label& l, const juce::String& name, juce::Colour colour = muted())
    {
        l.setText(name, juce::dontSendNotification);
        l.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        l.setColour(juce::Label::textColourId, colour);
    }

    void styleCombo (juce::ComboBox& c)
    {
        c.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff111a28));
        c.setColour(juce::ComboBox::outlineColourId, stroke());
        c.setColour(juce::ComboBox::textColourId, text());
        c.setColour(juce::ComboBox::arrowColourId, blue());
    }

    void styleButton (juce::TextButton& b, juce::Colour colour)
    {
        b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff121b28));
        b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff1a2a3b));
        b.setColour(juce::TextButton::textColourOffId, colour);
        b.setColour(juce::TextButton::textColourOnId, colour);
    }

    void styleSlider (juce::Slider& s)
    {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 22);
        s.setColour(juce::Slider::trackColourId, blue());
        s.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff162030));
        s.setColour(juce::Slider::thumbColourId, juce::Colours::white);
        s.setColour(juce::Slider::textBoxTextColourId, text());
        s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff060a10));
        s.setColour(juce::Slider::textBoxOutlineColourId, stroke());
    }

    void styleMonitor (juce::TextEditor& editor)
    {
        editor.setMultiLine(true);
        editor.setReadOnly(true);
        editor.setScrollbarsShown(true);
        editor.setCaretVisible(false);
        editor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff060a10));
        editor.setColour(juce::TextEditor::textColourId, text());
        editor.setColour(juce::TextEditor::outlineColourId, stroke());
        editor.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f,
                                                    juce::Font::plain)));
    }
}

class AudienceMidiDeviceModel final : public SeatEventSink
{
public:
    AudienceMidiDeviceModel()
        : osc(*this), simulator(*this)
    {
        engine.prepare(deviceSampleRate, midiBlockSamples);
        engine.channelMode.store((int) MidiEngine::MidiChannelMode::Single);
        engine.channel.store(1);
        engine.lowestMidi.store(36);
        engine.rangeOctaves.store(4);
        engine.rangeLowOctave.store(0);
        engine.rangeHighOctave.store(4);
        engine.scaleMode.store(MidiEngine::scaleModeForAudienceScaleIndex(0));
        engine.energyMacro.store(1.0f);
        engine.retriggerMs.store(0.0f);
        engine.ccEnabled.store(false);

        setMidiOutputOptionIndex(0);
        setUdpPort(defaultUdpPort);
    }

    ~AudienceMidiDeviceModel() override
    {
        panic();
        osc.stop();
        midiOutput.reset();
    }

    void setX (int row, int col, float xNorm) override
    {
        engine.setX(row, col, xNorm);
        logInput(InputType::X, row, col, xNorm);
    }

    void setY (int row, int col, float yNorm) override
    {
        engine.setY(row, col, yNorm);
        logInput(InputType::Y, row, col, yNorm);
    }

    void setOn (int row, int col, bool on) override
    {
        engine.setOn(row, col, on);
        logInput(on ? InputType::On : InputType::Off, row, col, on ? 1.0f : 0.0f);
    }

    void setUdpPort (int port)
    {
        udpPort = juce::jlimit(1, 65535, port);
        const bool ok = osc.start(udpPort);
        udpStatus = ok ? "Listening UDP " + juce::String(udpPort)
                       : "UDP bind failed: " + juce::String(udpPort);
    }

    void setChannel (int ch)
    {
        const int safe = juce::jlimit(1, 16, ch);
        if (engine.channel.exchange(safe) != safe)
            engine.requestRetuneActiveNotes();
    }

    void setRootIndex (int root)
    {
        const int safe = juce::jlimit(0, 11, root);
        if (engine.lowestMidi.exchange(36 + safe) != 36 + safe)
            engine.requestRetuneActiveNotes();
    }

    void setScaleMode (int mode)
    {
        const int safe = juce::jlimit(0, scaleNames.size() - 1, mode);
        const int engineMode = MidiEngine::scaleModeForAudienceScaleIndex(safe);
        if (engine.scaleMode.exchange(engineMode) != engineMode)
            engine.requestRetuneActiveNotes();
    }

    void setRangeBounds (int lowOctave, int highOctave)
    {
        const int safeLow = juce::jlimit(0, 7, lowOctave);
        const int safeHigh = juce::jlimit(safeLow + 1, 8, highOctave);
        const bool lowChanged = engine.rangeLowOctave.exchange(safeLow) != safeLow;
        const bool highChanged = engine.rangeHighOctave.exchange(safeHigh) != safeHigh;
        engine.rangeOctaves.store(safeHigh - safeLow);

        if (lowChanged || highChanged)
            engine.requestRetuneActiveNotes();
    }

    juce::StringArray getMidiOutputOptions() const
    {
        juce::StringArray options;
        options.add("Virtual: SpektraSynth MIDI Device Out");
        for (const auto& device : juce::MidiOutput::getAvailableDevices())
            options.add(device.name);
        return options;
    }

    void setMidiOutputOptionIndex (int index)
    {
        const juce::ScopedLock lock(outputLock);
        midiOutput.reset();
        midiOutputOptionIndex = juce::jmax(0, index);

        if (midiOutputOptionIndex == 0)
        {
            midiOutput = juce::MidiOutput::createNewDevice("SpektraSynth MIDI Device Out");
            if (midiOutput != nullptr)
            {
                midiOutput->startBackgroundThread();
                outputStatus = "Virtual port: SpektraSynth MIDI Device Out";
            }
            else
            {
                outputStatus = "Virtual MIDI port unavailable";
            }
            return;
        }

        const auto devices = juce::MidiOutput::getAvailableDevices();
        const int deviceIndex = midiOutputOptionIndex - 1;
        if (deviceIndex >= 0 && deviceIndex < devices.size())
        {
            midiOutput = juce::MidiOutput::openDevice(devices[deviceIndex].identifier);
            if (midiOutput != nullptr)
            {
                midiOutput->startBackgroundThread();
                outputStatus = "MIDI output: " + devices[deviceIndex].name;
            }
            else
            {
                outputStatus = "Failed to open MIDI output";
            }
        }
        else
        {
            outputStatus = "MIDI output device not found";
        }
    }

    void renderAndSend()
    {
        juce::MidiBuffer midi;
        engine.renderMidi(midi, midiBlockSamples);

        const juce::ScopedLock lock(outputLock);
        if (midiOutput != nullptr && ! midi.isEmpty())
            midiOutput->sendBlockOfMessages(midi, juce::Time::getMillisecondCounterHiRes(), deviceSampleRate);
    }

    void panic()
    {
        simulator.clearSilently();
        engine.clearAllSeats();
        renderAndSend();

        const juce::ScopedLock lock(outputLock);
        if (midiOutput == nullptr)
            return;

        for (int ch = 1; ch <= 16; ++ch)
        {
            midiOutput->sendMessageNow(juce::MidiMessage::allNotesOff(ch));
            midiOutput->sendMessageNow(juce::MidiMessage::allSoundOff(ch));
        }
    }

    juce::String getIncomingMonitorText() const
    {
        struct Snapshot
        {
            uint32_t serial = 0;
            int type = 0;
            int row = 0;
            int col = 0;
            float value = 0.0f;
        };

        std::array<Snapshot, 32> snapshots;
        for (size_t i = 0; i < inputSlots.size(); ++i)
        {
            const auto& slot = inputSlots[i];
            snapshots[i].serial = slot.serial.load(std::memory_order_relaxed);
            snapshots[i].type = slot.type.load(std::memory_order_relaxed);
            snapshots[i].row = slot.row.load(std::memory_order_relaxed);
            snapshots[i].col = slot.col.load(std::memory_order_relaxed);
            snapshots[i].value = slot.value.load(std::memory_order_relaxed);
        }

        std::sort(snapshots.begin(), snapshots.end(), [] (const Snapshot& a, const Snapshot& b)
        {
            return a.serial > b.serial;
        });

        juce::String text;
        int lines = 0;
        for (const auto& item : snapshots)
        {
            if (item.serial == 0 || item.type == 0)
                continue;

            if (lines++ > 0)
                text << "\n";

            const auto seat = rowName(item.row) + juce::String(item.col);
            switch ((InputType) item.type)
            {
                case InputType::X:
                    text << "UDP X: seat " << seat << ", x " << juce::String(item.value, 3);
                    break;
                case InputType::Y:
                    text << "UDP Y: seat " << seat << ", velocity source " << juce::String(item.value, 3);
                    break;
                case InputType::On:
                    text << "UDP On: seat " << seat;
                    break;
                case InputType::Off:
                    text << "UDP Off: seat " << seat;
                    break;
            }
        }

        return text.isEmpty() ? "Waiting for incoming UDP or simulator events..." : text;
    }

    juce::String getOutgoingMonitorText() const { return engine.getMonitorText(); }
    juce::String getUdpStatus() const { return udpStatus; }
    juce::String getOutputStatus() const
    {
        const juce::ScopedLock lock(outputLock);
        return outputStatus;
    }

    int getActiveCount() const noexcept { return engine.getRegisteredSeatCount(); }
    int getUdpPort() const noexcept { return udpPort; }
    int getMidiOutputOptionIndex() const noexcept { return midiOutputOptionIndex; }

    MidiEngine engine;
    OscBridge osc;
    Simulator simulator;

private:
    enum class InputType : int { X = 1, Y, On, Off };

    struct InputSlot
    {
        std::atomic<uint32_t> serial { 0 };
        std::atomic<int> type { 0 };
        std::atomic<int> row { 0 };
        std::atomic<int> col { 0 };
        std::atomic<float> value { 0.0f };
    };

    void logInput (InputType type, int row, int col, float value) noexcept
    {
        const uint32_t serial = inputSerial.fetch_add(1, std::memory_order_relaxed) + 1;
        auto& slot = inputSlots[(size_t) (serial % inputSlots.size())];
        slot.type.store((int) type, std::memory_order_relaxed);
        slot.row.store(row, std::memory_order_relaxed);
        slot.col.store(col, std::memory_order_relaxed);
        slot.value.store(value, std::memory_order_relaxed);
        slot.serial.store(serial, std::memory_order_release);
    }

    int udpPort = defaultUdpPort;
    juce::String udpStatus;
    std::unique_ptr<juce::MidiOutput> midiOutput;
    mutable juce::CriticalSection outputLock;
    int midiOutputOptionIndex = 0;
    juce::String outputStatus;
    std::array<InputSlot, 32> inputSlots;
    std::atomic<uint32_t> inputSerial { 0 };
};

class AudienceMidiDeviceComponent final : public juce::Component,
                                          private juce::Timer
{
public:
    AudienceMidiDeviceComponent()
    {
        setSize(1080, 640);

        styleLabel(outputLabel, "MIDI OUTPUT", blue());
        styleLabel(channelLabel, "CHANNEL", blue());
        styleLabel(udpLabel, "UDP PORT", blue());
        styleLabel(rootLabel, "ROOT", muted());
        styleLabel(scaleLabel, "SCALE", muted());
        styleLabel(rangeLabel, "SCALER RANGE", muted());
        styleLabel(rangeLowLabel, "LOW", muted());
        styleLabel(rangeHighLabel, "HIGH", muted());
        styleLabel(rangeValueLabel, "0 TO 4", text());
        styleLabel(statusLabel, "", green());
        styleLabel(activeLabel, "", text());
        styleLabel(incomingLabel, "INCOMING UDP", green());
        styleLabel(outgoingLabel, "OUTGOING MIDI", green());

        for (auto* l : { &outputLabel, &channelLabel, &udpLabel, &rootLabel, &scaleLabel, &rangeLabel,
                         &rangeLowLabel, &rangeHighLabel, &rangeValueLabel, &statusLabel, &activeLabel,
                         &incomingLabel, &outgoingLabel })
            addAndMakeVisible(*l);

        addAndMakeVisible(outputCombo);
        addAndMakeVisible(rootCombo);
        addAndMakeVisible(scaleCombo);
        addAndMakeVisible(channelSlider);
        addAndMakeVisible(rangeLowSlider);
        addAndMakeVisible(rangeHighSlider);
        addAndMakeVisible(udpEditor);
        addAndMakeVisible(applyUdpButton);
        addAndMakeVisible(refreshButton);
        addAndMakeVisible(panicButton);
        addAndMakeVisible(addButton);
        addAndMakeVisible(crowdButton);
        addAndMakeVisible(removeButton);
        addAndMakeVisible(clearButton);
        addAndMakeVisible(moveButton);
        addAndMakeVisible(incomingMonitor);
        addAndMakeVisible(outgoingMonitor);

        styleCombo(outputCombo);
        styleCombo(rootCombo);
        styleCombo(scaleCombo);
        styleSlider(channelSlider);
        styleSlider(rangeLowSlider);
        styleSlider(rangeHighSlider);
        styleButton(applyUdpButton, blue());
        styleButton(refreshButton, blue());
        styleButton(panicButton, red());
        styleButton(addButton, text());
        styleButton(crowdButton, text());
        styleButton(removeButton, text());
        styleButton(clearButton, red());
        styleButton(moveButton, green());
        styleMonitor(incomingMonitor);
        styleMonitor(outgoingMonitor);

        refreshOutputOptions();

        for (int i = 0; i < rootNames.size(); ++i) rootCombo.addItem(rootNames[i], i + 1);
        rootCombo.setSelectedItemIndex(0, juce::dontSendNotification);
        rootCombo.onChange = [this] { model.setRootIndex(rootCombo.getSelectedItemIndex()); };

        for (int i = 0; i < scaleNames.size(); ++i) scaleCombo.addItem(scaleNames[i], i + 1);
        scaleCombo.setSelectedItemIndex(0, juce::dontSendNotification);
        scaleCombo.onChange = [this] { model.setScaleMode(scaleCombo.getSelectedItemIndex()); };

        channelSlider.setRange(1, 16, 1);
        channelSlider.setValue(1, juce::dontSendNotification);
        channelSlider.onValueChange = [this] { model.setChannel((int) channelSlider.getValue()); };

        rangeLowSlider.setRange(0, 7, 1);
        rangeHighSlider.setRange(1, 8, 1);
        rangeLowSlider.setValue(0, juce::dontSendNotification);
        rangeHighSlider.setValue(4, juce::dontSendNotification);
        rangeLowSlider.onValueChange = [this] { syncRangeSliders(&rangeLowSlider); };
        rangeHighSlider.onValueChange = [this] { syncRangeSliders(&rangeHighSlider); };
        model.setRangeBounds((int) rangeLowSlider.getValue(), (int) rangeHighSlider.getValue());
        refreshRangeLabel();

        udpEditor.setText(juce::String(model.getUdpPort()), false);
        udpEditor.setInputRestrictions(5, "0123456789");
        udpEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff060a10));
        udpEditor.setColour(juce::TextEditor::textColourId, text());
        udpEditor.setColour(juce::TextEditor::outlineColourId, stroke());
        applyUdpButton.setButtonText("Apply");
        applyUdpButton.onClick = [this]
        {
            model.setUdpPort(udpEditor.getText().getIntValue());
            statusLabel.setText(model.getUdpStatus() + " | " + model.getOutputStatus(), juce::dontSendNotification);
        };

        refreshButton.setButtonText("Refresh");
        refreshButton.onClick = [this] { refreshOutputOptions(); };

        panicButton.setButtonText("Panic / All Notes Off");
        panicButton.onClick = [this] { model.panic(); };

        addButton.setButtonText("+ Seat");
        crowdButton.setButtonText("+25 Crowd");
        removeButton.setButtonText("Remove");
        clearButton.setButtonText("Clear");
        moveButton.setButtonText("Random Move");
        moveButton.setClickingTogglesState(true);

        addButton.onClick = [this] { model.simulator.addRandomSeat(); };
        crowdButton.onClick = [this] { model.simulator.addRandomSeats(25); };
        removeButton.onClick = [this] { model.simulator.removeRandomSeat(); };
        clearButton.onClick = [this] { model.panic(); };
        moveButton.onClick = [this] { model.simulator.setRandomMovement(moveButton.getToggleState()); };

        incomingMonitor.setText(model.getIncomingMonitorText(), juce::dontSendNotification);
        outgoingMonitor.setText(model.getOutgoingMonitorText(), juce::dontSendNotification);
        statusLabel.setText(model.getUdpStatus() + " | " + model.getOutputStatus(), juce::dontSendNotification);
        activeLabel.setText("Active participants: 0", juce::dontSendNotification);

        startTimerHz(timerHz);
    }

    ~AudienceMidiDeviceComponent() override
    {
        stopTimer();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll(bg());
        g.setColour(text());
        g.setFont(juce::Font(juce::FontOptions(24.0f, juce::Font::bold)));
        g.drawText("SpektraSynth MIDI Device", 24, 16, getWidth() - 48, 30, juce::Justification::centredLeft);

        g.setColour(muted());
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::plain)));
        g.drawText("UDP audience seats -> root/scale/range quantized MIDI note, velocity and channel",
                   24, 46, getWidth() - 48, 20, juce::Justification::centredLeft);

        auto drawPanel = [&g] (juce::Rectangle<int> r)
        {
            g.setColour(panel());
            g.fillRoundedRectangle(r.toFloat(), 8.0f);
            g.setColour(stroke());
            g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 8.0f, 1.0f);
        };

        drawPanel(topPanel);
        drawPanel(simPanel);
        drawPanel(leftPanel);
        drawPanel(rightPanel);
    }

    void resized() override
    {
        const auto area = getLocalBounds().reduced(18);
        topPanel = juce::Rectangle<int>(area.getX(), 82, area.getWidth(), 184);
        simPanel = juce::Rectangle<int>(area.getX(), topPanel.getBottom() + 12, area.getWidth(), 58);

        const int monitorTop = simPanel.getBottom() + 12;
        const int gap = 12;
        const int monitorW = (area.getWidth() - gap) / 2;
        leftPanel = juce::Rectangle<int>(area.getX(), monitorTop, monitorW, area.getBottom() - monitorTop);
        rightPanel = juce::Rectangle<int>(leftPanel.getRight() + gap, monitorTop, monitorW, leftPanel.getHeight());

        int x = topPanel.getX() + 18;
        int y = topPanel.getY() + 18;
        outputLabel.setBounds(x, y, 160, 18);
        outputCombo.setBounds(x, y + 24, 300, 28);
        refreshButton.setBounds(outputCombo.getRight() + 10, y + 24, 84, 28);

        udpLabel.setBounds(x, y + 70, 120, 18);
        udpEditor.setBounds(x, y + 94, 100, 28);
        applyUdpButton.setBounds(x + 110, y + 94, 72, 28);
        statusLabel.setBounds(x + 200, y + 94, topPanel.getRight() - x - 220, 28);

        x = topPanel.getX() + 430;
        rootLabel.setBounds(x, y, 80, 18);
        rootCombo.setBounds(x, y + 24, 96, 28);
        scaleLabel.setBounds(x + 116, y, 100, 18);
        scaleCombo.setBounds(x + 116, y + 24, 190, 28);

        channelLabel.setBounds(x, y + 70, 100, 18);
        channelSlider.setBounds(x, y + 91, 190, 32);
        rangeLabel.setBounds(x + 220, y + 70, 120, 18);
        rangeValueLabel.setBounds(x + 340, y + 70, 130, 18);
        rangeLowLabel.setBounds(x + 220, y + 104, 36, 18);
        rangeLowSlider.setBounds(x + 260, y + 98, 140, 32);
        rangeHighLabel.setBounds(x + 424, y + 104, 44, 18);
        rangeHighSlider.setBounds(x + 474, y + 98, 140, 32);

        panicButton.setBounds(topPanel.getRight() - 178, topPanel.getY() + 20, 154, 32);
        activeLabel.setBounds(topPanel.getRight() - 220, topPanel.getY() + 60, 196, 24);

        int sx = simPanel.getX() + 18;
        int sy = simPanel.getY() + 15;
        addButton.setBounds(sx, sy, 82, 28);
        crowdButton.setBounds(sx + 92, sy, 108, 28);
        removeButton.setBounds(sx + 210, sy, 96, 28);
        clearButton.setBounds(sx + 316, sy, 86, 28);
        moveButton.setBounds(sx + 420, sy, 126, 28);

        incomingLabel.setBounds(leftPanel.getX() + 14, leftPanel.getY() + 12, 180, 18);
        incomingMonitor.setBounds(leftPanel.reduced(14).withTrimmedTop(36));
        outgoingLabel.setBounds(rightPanel.getX() + 14, rightPanel.getY() + 12, 180, 18);
        outgoingMonitor.setBounds(rightPanel.reduced(14).withTrimmedTop(36));
    }

private:
    void refreshRangeLabel()
    {
        rangeValueLabel.setText(juce::String((int) rangeLowSlider.getValue())
                                    + " TO "
                                    + juce::String((int) rangeHighSlider.getValue()),
                                juce::dontSendNotification);
    }

    void syncRangeSliders (juce::Slider* changedSlider)
    {
        if (updatingRangeSliders)
            return;

        const juce::ScopedValueSetter<bool> guard(updatingRangeSliders, true);
        const int low = (int) rangeLowSlider.getValue();
        const int high = (int) rangeHighSlider.getValue();

        if (changedSlider == &rangeLowSlider && low >= high)
            rangeHighSlider.setValue(juce::jlimit(1, 8, low + 1), juce::sendNotificationSync);
        else if (changedSlider == &rangeHighSlider && high <= low)
            rangeLowSlider.setValue(juce::jlimit(0, 7, high - 1), juce::sendNotificationSync);

        model.setRangeBounds((int) rangeLowSlider.getValue(), (int) rangeHighSlider.getValue());
        refreshRangeLabel();
    }

    void timerCallback() override
    {
        model.renderAndSend();
        incomingMonitor.setText(model.getIncomingMonitorText(), juce::dontSendNotification);
        outgoingMonitor.setText(model.getOutgoingMonitorText(), juce::dontSendNotification);
        statusLabel.setText(model.getUdpStatus() + " | " + model.getOutputStatus(), juce::dontSendNotification);
        activeLabel.setText("Active participants: " + juce::String(model.getActiveCount()),
                            juce::dontSendNotification);
    }

    void refreshOutputOptions()
    {
        const auto oldIndex = juce::jmax(0, outputCombo.getSelectedItemIndex());
        outputCombo.clear(juce::dontSendNotification);
        const auto options = model.getMidiOutputOptions();
        for (int i = 0; i < options.size(); ++i)
            outputCombo.addItem(options[i], i + 1);

        const int newIndex = juce::jlimit(0, juce::jmax(0, options.size() - 1), oldIndex);
        outputCombo.setSelectedItemIndex(newIndex, juce::dontSendNotification);
        model.setMidiOutputOptionIndex(newIndex);
        outputCombo.onChange = [this] { model.setMidiOutputOptionIndex(outputCombo.getSelectedItemIndex()); };
    }

    AudienceMidiDeviceModel model;

    juce::Rectangle<int> topPanel, simPanel, leftPanel, rightPanel;
    juce::Label outputLabel, channelLabel, udpLabel, rootLabel, scaleLabel, rangeLabel;
    juce::Label rangeLowLabel, rangeHighLabel, rangeValueLabel;
    juce::Label statusLabel, activeLabel, incomingLabel, outgoingLabel;
    juce::ComboBox outputCombo, rootCombo, scaleCombo;
    juce::Slider channelSlider, rangeLowSlider, rangeHighSlider;
    juce::TextEditor udpEditor, incomingMonitor, outgoingMonitor;
    juce::TextButton applyUdpButton, refreshButton, panicButton;
    juce::TextButton addButton, crowdButton, removeButton, clearButton, moveButton;
    bool updatingRangeSliders = false;
};

class AudienceMidiDeviceApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "SpektraSynth MIDI Device"; }
    const juce::String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override             { return false; }

    void initialise (const juce::String&) override
    {
        mainWindow.reset(new MainWindow(getApplicationName()));
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow(name, bg(), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setResizable(true, true);
            setContentOwned(new AudienceMidiDeviceComponent(), true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(AudienceMidiDeviceApplication)
