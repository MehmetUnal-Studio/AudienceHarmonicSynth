#include "MidiProcessor.h"
#include "MidiGeneratorEditor.h"

#include <algorithm>
#include <atomic>

namespace
{
    constexpr const char* kNoteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    std::atomic<int> nextInstanceId { 1 };

    juce::String rowName (int row)
    {
        return juce::String::charToString((juce::juce_wchar) ('A' + juce::jlimit(0, 25, row)));
    }

    juce::String noteNameWithOctave (int midi)
    {
        const int clamped = juce::jlimit(0, 127, midi);
        return juce::String(kNoteNames[clamped % 12]) + juce::String(clamped / 12 - 1);
    }
}

AudienceMidiProcessor::AudienceMidiProcessor()
    : juce::AudioProcessor(BusesProperties()
                              .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
                              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createLayout()),
      osc(*this),
      simulator(*this)
{
    instanceId = nextInstanceId.fetch_add(1, std::memory_order_relaxed);
    engine.channelMode.store((int) MidiEngine::MidiChannelMode::Single);
    engine.energyMacro.store(1.0f);
    engine.retriggerMs.store(0.0f);
    engine.ccEnabled.store(false);
    setUdpPort(udpPort);
    startTimerHz(60);
}

AudienceMidiProcessor::~AudienceMidiProcessor()
{
    stopTimer();
    sendImmediateAllNotesOffToExternal();
    closeMidiOutput();
    osc.stop();
}

juce::AudioProcessorValueTreeState::ParameterLayout AudienceMidiProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID("channel", 1), "MIDI Channel", 1, 16, 1));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("root", 1), "Root",
        StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" },
        0));

    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID("rangeLowOctave", 1), "Range Low", 0, 7, 0));

    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID("rangeHighOctave", 1), "Range High", 1, 8, 4));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("scaleMode", 1), "Scale", MidiScaleModule::scaleNames(), 0));

    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID("transpose", 1), "Transpose", -24, 24, 0));

    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID("midiScaleEnabled", 1), "Scale On", false));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID("midiScaleCorrection", 1), "Scale Correction",
        MidiScaleModule::correctionModeNames(), 0));

    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID("midiScaleCustomMask", 1), "Custom Scale Mask",
        1, 4095, MidiScaleModule::defaultCustomMask()));

    for (int i = 0; i < 12; ++i)
        layout.add(std::make_unique<AudioParameterInt>(
            ParameterID("midiScaleRemap" + juce::String(i), 1),
            "Scale Remap " + juce::String(i), 0, 11, i));

    return layout;
}

void AudienceMidiProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = juce::jmax(1.0, sampleRate);
    engine.prepare(sampleRate, samplesPerBlock);
    renderScratch.ensureSize(realtimeMidiBufferReserveBytes);
    scaleScratch.ensureSize(realtimeMidiBufferReserveBytes);
    scaleScratchLoanedToHost = false;
    scaleModule.reset();
}

bool AudienceMidiProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::mono()
                      || out == juce::AudioChannelSet::stereo());
}

void AudienceMidiProcessor::pullParams()
{
    const int newChannel = (int) apvts.getRawParameterValue("channel")->load();
    const int newRoot = (int) apvts.getRawParameterValue("root")->load();
    const int rawRangeLow = (int) apvts.getRawParameterValue("rangeLowOctave")->load();
    const int rawRangeHigh = (int) apvts.getRawParameterValue("rangeHighOctave")->load();
    const int newRangeLow = juce::jlimit(0, 7, rawRangeLow);
    const int newRangeHigh = juce::jlimit(newRangeLow + 1, 8, rawRangeHigh);
    const int newScaleMode = (int) apvts.getRawParameterValue("scaleMode")->load();
    const int newTranspose = (int) apvts.getRawParameterValue("transpose")->load();
    const bool scaleEnabled = apvts.getRawParameterValue("midiScaleEnabled")->load() > 0.5f;
    const int scaleCorrection = (int) apvts.getRawParameterValue("midiScaleCorrection")->load();
    const int customMask = (int) apvts.getRawParameterValue("midiScaleCustomMask")->load();
    std::array<int, 12> remap {};
    for (int i = 0; i < 12; ++i)
        remap[(size_t) i] = (int) apvts.getRawParameterValue("midiScaleRemap" + juce::String(i))->load();

    engine.channel.store(newChannel);
    engine.channelMode.store((int) MidiEngine::MidiChannelMode::Single);
    engine.lowestMidi.store(36 + newRoot);
    engine.rangeOctaves.store(newRangeHigh - newRangeLow);
    engine.rangeLowOctave.store(newRangeLow);
    engine.rangeHighOctave.store(newRangeHigh);
    engine.scaleMode.store(newScaleMode);
    engine.transpose.store(scaleEnabled ? 0 : newTranspose);
    engine.energyMacro.store(1.0f);
    engine.retriggerMs.store(0.0f);
    engine.ccEnabled.store(false);
    scaleModule.setConfig(scaleEnabled,
                          newRoot,
                          (MidiScaleModule::ScaleType) juce::jlimit(0, MidiScaleModule::numScaleTypes - 1, newScaleMode),
                          (MidiScaleModule::CorrectionMode) juce::jlimit(0, MidiScaleModule::numCorrectionModes - 1, scaleCorrection),
                          customMask,
                          remap,
                          newTranspose);

    const bool initialized = lastChannel >= 0;
    const bool channelChanged = initialized && lastChannel != newChannel;
    const bool harmonicChanged = initialized
                              && (lastRoot != newRoot
                               || lastRangeLowOctave != newRangeLow
                               || lastRangeHighOctave != newRangeHigh
                               || lastScaleMode != newScaleMode
                               || lastTranspose != newTranspose);

    if (channelChanged || harmonicChanged)
        engine.requestRetuneActiveNotes();

    lastChannel = newChannel;
    lastRoot = newRoot;
    lastRangeLowOctave = newRangeLow;
    lastRangeHighOctave = newRangeHigh;
    lastScaleMode = newScaleMode;
    lastTranspose = newTranspose;
}

void AudienceMidiProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    pullParams();
    buffer.clear();

    renderScratch.clear();
    copyMidiBuffer(midi, renderScratch);

    if (scaleScratchLoanedToHost)
    {
        midi.swapWith(scaleScratch);
        scaleScratchLoanedToHost = false;
    }

    midi.clear();

    engine.renderMidi(renderScratch, buffer.getNumSamples());
    scaleModule.process(renderScratch, scaleScratch);
    midi.swapWith(scaleScratch);
    scaleScratchLoanedToHost = true;

    logOutputMidi(midi);
    queueMidiToExternal(midi);
}

void AudienceMidiProcessor::setUdpPort (int port)
{
    udpPort = juce::jlimit(1, 65535, port);
    const bool ok = osc.start(udpPort);
    oscStatus = ok ? ("Listening on UDP " + juce::String(udpPort))
                   : ("FAILED to bind UDP " + juce::String(udpPort) + " - port busy?");
}

void AudienceMidiProcessor::panic()
{
    simulator.clearSilently();
    engine.requestClearAllSeats();
    scaleModule.reset();
    sendImmediateAllNotesOffToExternal();
}

void AudienceMidiProcessor::setX (int row, int col, float xNorm)
{
    engine.setX(row, col, xNorm);
    logInput(InputType::X, row, col, xNorm);
}

void AudienceMidiProcessor::setY (int row, int col, float yNorm)
{
    engine.setY(row, col, yNorm);
    logInput(InputType::Y, row, col, yNorm);
}

void AudienceMidiProcessor::setOn (int row, int col, bool on)
{
    engine.setOn(row, col, on);
    logInput(on ? InputType::On : InputType::Off, row, col, on ? 1.0f : 0.0f);
}

void AudienceMidiProcessor::logInput (InputType type, int row, int col, float value) noexcept
{
    const uint32_t serial = inputSerial.fetch_add(1, std::memory_order_relaxed) + 1;
    auto& slot = inputSlots[(size_t) (serial % inputSlots.size())];
    slot.type.store((int) type, std::memory_order_relaxed);
    slot.row.store(row, std::memory_order_relaxed);
    slot.col.store(col, std::memory_order_relaxed);
    slot.value.store(value, std::memory_order_relaxed);
    slot.serial.store(serial, std::memory_order_release);
}

void AudienceMidiProcessor::logOutputMidi (const juce::MidiBuffer& midi) noexcept
{
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        OutputType type;
        int data1 = 0;
        int data2 = 0;

        if (message.isNoteOn(false))
        {
            type = OutputType::NoteOn;
            data1 = message.getNoteNumber();
            data2 = message.getVelocity();
        }
        else if (message.isNoteOff(true))
        {
            type = OutputType::NoteOff;
            data1 = message.getNoteNumber();
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            type = OutputType::AllNotesOff;
        }
        else if (message.isController())
        {
            type = OutputType::CC;
            data1 = message.getControllerNumber();
            data2 = message.getControllerValue();
        }
        else
        {
            continue;
        }

        const uint32_t serial = outputSerial.fetch_add(1, std::memory_order_relaxed) + 1;
        auto& slot = outputSlots[(size_t) (serial % outputSlots.size())];
        slot.type.store((int) type, std::memory_order_relaxed);
        slot.channel.store(message.getChannel(), std::memory_order_relaxed);
        slot.data1.store(data1, std::memory_order_relaxed);
        slot.data2.store(data2, std::memory_order_relaxed);
        slot.serial.store(serial, std::memory_order_release);
    }
}

void AudienceMidiProcessor::copyMidiBuffer (const juce::MidiBuffer& source, juce::MidiBuffer& dest)
{
    dest.clear();
    for (const auto metadata : source)
        dest.addEvent(metadata.getMessage(), metadata.samplePosition);
}

void AudienceMidiProcessor::queueMidiToExternal (const juce::MidiBuffer& midi) noexcept
{
    if (midiOutputOptionIndex.load(std::memory_order_relaxed) == 0 || midi.isEmpty())
        return;

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        const int rawSize = message.getRawDataSize();
        if (rawSize <= 0 || rawSize > 3)
            continue;

        int s1, sz1, s2, sz2;
        externalMidiFifo.prepareToWrite(1, s1, sz1, s2, sz2);
        if (sz1 <= 0 && sz2 <= 0)
        {
            externalMidiDropped.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        const int slot = sz1 > 0 ? s1 : s2;
        auto& packed = externalMidiEvents[(size_t) slot];
        packed.size = (juce::uint8) rawSize;

        const auto* raw = message.getRawData();
        for (int i = 0; i < rawSize; ++i)
            packed.data[i] = raw[i];

        externalMidiFifo.finishedWrite(1);
    }
}

void AudienceMidiProcessor::drainExternalMidiQueue()
{
    const juce::ScopedLock lock(midiOutputLock);
    if (midiOutput == nullptr)
        return;

    const int available = externalMidiFifo.getNumReady();
    if (available <= 0)
        return;

    int s1, sz1, s2, sz2;
    externalMidiFifo.prepareToRead(available, s1, sz1, s2, sz2);

    auto sendRange = [this] (int start, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            const auto& event = externalMidiEvents[(size_t) (start + i)];
            if (event.size > 0)
                midiOutput->sendMessageNow(juce::MidiMessage(event.data, (int) event.size));
        }
    };

    sendRange(s1, sz1);
    sendRange(s2, sz2);
    externalMidiFifo.finishedRead(sz1 + sz2);
}

void AudienceMidiProcessor::timerCallback()
{
    drainExternalMidiQueue();
}

juce::String AudienceMidiProcessor::getIncomingMonitorText() const
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
                text << "UDP Y: seat " << seat << ", velocity " << juce::String(item.value, 3);
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

juce::String AudienceMidiProcessor::getOutgoingMonitorText() const
{
    struct Snapshot
    {
        uint32_t serial = 0;
        int type = 0;
        int channel = 0;
        int data1 = 0;
        int data2 = 0;
    };

    std::array<Snapshot, 32> snapshots;
    for (size_t i = 0; i < outputSlots.size(); ++i)
    {
        const auto& slot = outputSlots[i];
        snapshots[i].serial = slot.serial.load(std::memory_order_relaxed);
        snapshots[i].type = slot.type.load(std::memory_order_relaxed);
        snapshots[i].channel = slot.channel.load(std::memory_order_relaxed);
        snapshots[i].data1 = slot.data1.load(std::memory_order_relaxed);
        snapshots[i].data2 = slot.data2.load(std::memory_order_relaxed);
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

        switch ((OutputType) item.type)
        {
            case OutputType::NoteOn:
                text << "Note On: ch " << item.channel << ", note " << item.data1
                     << " (" << noteNameWithOctave(item.data1) << "), velocity " << item.data2;
                break;
            case OutputType::NoteOff:
                text << "Note Off: ch " << item.channel << ", note " << item.data1
                     << " (" << noteNameWithOctave(item.data1) << ")";
                break;
            case OutputType::CC:
                text << "CC" << item.data1 << ": ch " << item.channel << ", value " << item.data2;
                break;
            case OutputType::AllNotesOff:
                text << "All Notes Off: ch " << item.channel;
                break;
        }
    }

    return text.isEmpty() ? "Waiting for outgoing MIDI..." : text;
}

std::array<AudienceMidiProcessor::MidiMonitorEvent, 32> AudienceMidiProcessor::getOutgoingMonitorEvents() const
{
    std::array<MidiMonitorEvent, 32> snapshots;
    for (size_t i = 0; i < outputSlots.size(); ++i)
    {
        const auto& slot = outputSlots[i];
        snapshots[i].serial = slot.serial.load(std::memory_order_relaxed);
        snapshots[i].type = slot.type.load(std::memory_order_relaxed);
        snapshots[i].channel = slot.channel.load(std::memory_order_relaxed);
        snapshots[i].data1 = slot.data1.load(std::memory_order_relaxed);
        snapshots[i].data2 = slot.data2.load(std::memory_order_relaxed);
    }

    return snapshots;
}

juce::String AudienceMidiProcessor::getMidiOutputDescription() const
{
    const auto wrapper = juce::String(juce::AudioProcessor::getWrapperTypeDescription(wrapperType));
    if (wrapperType == wrapperType_VST3)
        return getMidiOutputStatus() + " | Host bus: Ableton track/plugin MIDI output";

    if (wrapperType == wrapperType_Standalone)
        return getMidiOutputStatus();

    return getMidiOutputStatus() + " (" + wrapper + ")";
}

juce::StringArray AudienceMidiProcessor::getMidiOutputOptions() const
{
    juce::StringArray options;
    options.add("Host MIDI Output");
    options.add("Virtual: Audience MIDI Generator Out");

    for (const auto& device : juce::MidiOutput::getAvailableDevices())
        options.add(device.name);

    return options;
}

juce::String AudienceMidiProcessor::getMidiOutputStatus() const
{
    const juce::ScopedLock lock(midiOutputLock);
    return midiOutputStatus;
}

void AudienceMidiProcessor::setMidiOutputOptionIndex (int index)
{
    const juce::ScopedLock lock(midiOutputLock);
    midiOutput.reset();
    midiOutputOptionIndex.store(juce::jmax(0, index), std::memory_order_relaxed);

    if (midiOutputOptionIndex.load(std::memory_order_relaxed) == 0)
    {
        midiOutputStatus = "Host MIDI Output";
        return;
    }

    if (midiOutputOptionIndex.load(std::memory_order_relaxed) == 1)
    {
        const juce::String portName = instanceId <= 1
            ? "Audience MIDI Generator Out"
            : "Audience MIDI Generator Out " + juce::String(instanceId);

        midiOutput = juce::MidiOutput::createNewDevice(portName);
        if (midiOutput != nullptr)
        {
            midiOutput->startBackgroundThread();
            midiOutputStatus = "Virtual port: " + portName;
        }
        else
        {
            midiOutputStatus = "Virtual MIDI port unavailable";
            midiOutputOptionIndex.store(0, std::memory_order_relaxed);
        }
        return;
    }

    const auto devices = juce::MidiOutput::getAvailableDevices();
    const int deviceIndex = midiOutputOptionIndex.load(std::memory_order_relaxed) - 2;
    if (deviceIndex >= 0 && deviceIndex < devices.size())
    {
        midiOutput = juce::MidiOutput::openDevice(devices[deviceIndex].identifier);
        if (midiOutput != nullptr)
        {
            midiOutput->startBackgroundThread();
            midiOutputStatus = "MIDI Output: " + devices[deviceIndex].name;
        }
        else
        {
            midiOutputStatus = "Failed to open MIDI output";
            midiOutputOptionIndex.store(0, std::memory_order_relaxed);
        }
    }
    else
    {
        midiOutputStatus = "MIDI output device not found";
        midiOutputOptionIndex.store(0, std::memory_order_relaxed);
    }
}

void AudienceMidiProcessor::sendImmediateAllNotesOffToExternal()
{
    const juce::ScopedLock lock(midiOutputLock);
    if (midiOutput == nullptr || midiOutputOptionIndex.load(std::memory_order_relaxed) == 0)
        return;

    for (int ch = 1; ch <= 16; ++ch)
    {
        midiOutput->sendMessageNow(juce::MidiMessage::allNotesOff(ch));
        midiOutput->sendMessageNow(juce::MidiMessage::allSoundOff(ch));
    }
}

void AudienceMidiProcessor::closeMidiOutput()
{
    const juce::ScopedLock lock(midiOutputLock);
    midiOutput.reset();
    midiOutputOptionIndex.store(0, std::memory_order_relaxed);
    midiOutputStatus = "Host MIDI Output";
}

void AudienceMidiProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty("udpPort", udpPort, nullptr);
    state.setProperty("midiOutputOption", midiOutputOptionIndex.load(std::memory_order_relaxed), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, dest);
}

void AudienceMidiProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
    {
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.isValid())
        {
            apvts.replaceState(state);
            setUdpPort((int) state.getProperty("udpPort", 6060));
            setMidiOutputOptionIndex((int) state.getProperty("midiOutputOption", 0));
        }
    }
}

juce::AudioProcessorEditor* AudienceMidiProcessor::createEditor()
{
    return new AudienceMidiGeneratorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudienceMidiProcessor();
}
