/*
    cs_osc_test_sender.js

    Max/MSP OSC test sender for Cosmic Microwave / SpektraSynth.

    Basic patch:

        [start]                 [stop]
             \                 /
          [js cs_osc_test_sender.js]
                    |
          [udpsend 127.0.0.1 6060]

    The left outlet emits OSC-compatible Max messages. Connect it directly to
    [udpsend]. The right outlet reports status and every emitted message.

    Commands:

        bang                    one sweep through sources 1..16
        once                    same as bang
        start                   repeat sweeps until stopped
        stop                    stop and release the currently active source
        zone A                  set zone A..Z
        range 1 16              set inclusive source range, 0..255
        finger 0                set finger index, 0..9
        note 72                 use one line/note value for every source
        notes 60 96             spread a line/note range across the sources
        randomnotes 60 96       choose a new random note for every source
        values 100 0.92126      set one line/note value and v (0..1)
        timing 20 500 100 1000  packet gap, hold, source gap, loop gap (ms)
        randomtiming 200 1000   random hold; keep the current source gap
        randomtiming 200 1000 50 300
                                random hold and random source gap (ms)
        defaults                restore the defaults above
        dump                    report the current configuration

    "note" and "notes" are convenient names for the legacy OSC "line" value.
    The receiver accepts line values in the range 0..127.

    Example after sending "notes 60 96":

        /cs/A/1/finger0/line    60
        ...
        /cs/A/16/finger0/line   96

    Each source emits this lifecycle:

        /cs/A/<source>/finger0/on    1
        /cs/A/<source>/finger0/line  <selected note>
        /cs/A/<source>/finger0/v     0.92126
        /cs/A/<source>/finger0/off   (no argument)

    Written in ES5 syntax for Max's classic [js] object.
*/

autowatch = 1;
inlets = 1;
outlets = 2;

var DEFAULT_ZONE = "A";
var DEFAULT_FIRST_SOURCE = 1;
var DEFAULT_LAST_SOURCE = 16;
var DEFAULT_FINGER = 0;
var DEFAULT_FIRST_NOTE = 100;
var DEFAULT_LAST_NOTE = 100;
var DEFAULT_V_VALUE = 0.92126;
var DEFAULT_PACKET_GAP_MS = 20;
var DEFAULT_HOLD_MS = 500;
var DEFAULT_SOURCE_GAP_MS = 100;
var DEFAULT_LOOP_GAP_MS = 1000;

var zoneName;
var firstSource;
var lastSource;
var fingerIndex;
var firstNoteValue;
var lastNoteValue;
var vValue;
var packetGapMs;
var holdMs;
var sourceGapMs;
var loopGapMs;
var randomNotesEnabled;
var randomTimingEnabled;
var randomHoldMinMs;
var randomHoldMaxMs;
var randomSourceGapMinMs;
var randomSourceGapMaxMs;

var isRunning = false;
var repeatSweeps = false;
var currentSource = 1;
var activeSource = -1;
var phase = 0;

var sequenceTask = new Task(runPhase, this);

restoreDefaults();

function bang()
{
    once();
}

function once()
{
    beginSequence(false);
}

function start()
{
    beginSequence(true);
}

function stop()
{
    var wasRunning = isRunning || activeSource >= 0;

    sequenceTask.cancel();
    isRunning = false;
    repeatSweeps = false;

    // Never leave a source held when the sequence is interrupted.
    if (activeSource >= 0)
        emitOff(activeSource);

    phase = 0;

    if (wasRunning)
        report("stopped");
}

function zone(value)
{
    var candidate = String(value).toUpperCase();

    if (!/^[A-Z]$/.test(candidate))
    {
        report("error", "zone", "expected_A_to_Z");
        return;
    }

    // Release using the old address before changing address components.
    stop();
    zoneName = candidate;
    report("zone", zoneName);
}

function range(first, last)
{
    if (!isWholeNumberInRange(first, 0, 255)
        || !isWholeNumberInRange(last, 0, 255)
        || first > last)
    {
        report("error", "range", "expected_0_to_255_in_ascending_order");
        return;
    }

    stop();
    firstSource = Number(first);
    lastSource = Number(last);
    report("range", firstSource, lastSource);
}

function finger(value)
{
    if (!isWholeNumberInRange(value, 0, 9))
    {
        report("error", "finger", "expected_0_to_9");
        return;
    }

    stop();
    fingerIndex = Number(value);
    report("finger", fingerIndex);
}

function note(value)
{
    notes(value, value);
}

function notes(first, last)
{
    if (!isWholeNumberInRange(first, 0, 127)
        || !isWholeNumberInRange(last, 0, 127))
    {
        report("error", "notes", "expected_two_values_from_0_to_127");
        return;
    }

    firstNoteValue = Number(first);
    lastNoteValue = Number(last);
    randomNotesEnabled = false;
    report("notes", firstNoteValue, lastNoteValue);
}

function randomnotes(first, last)
{
    if (!isWholeNumberInRange(first, 0, 127)
        || !isWholeNumberInRange(last, 0, 127)
        || first > last)
    {
        report("error", "randomnotes", "expected_0_to_127_in_ascending_order");
        return;
    }

    firstNoteValue = Number(first);
    lastNoteValue = Number(last);
    randomNotesEnabled = true;
    report("randomnotes", firstNoteValue, lastNoteValue);
}

function values(newNoteValue, newVValue)
{
    if (!isFiniteNumber(newNoteValue)
        || newNoteValue < 0
        || newNoteValue > 127
        || !isFiniteNumber(newVValue)
        || newVValue < 0
        || newVValue > 1)
    {
        report("error", "values", "expected_note_0_to_127_and_v_0_to_1");
        return;
    }

    firstNoteValue = Number(newNoteValue);
    lastNoteValue = Number(newNoteValue);
    randomNotesEnabled = false;
    vValue = Number(newVValue);
    report("values", firstNoteValue, vValue);
}

function timing(newPacketGapMs, newHoldMs, newSourceGapMs, newLoopGapMs)
{
    if (!isNonNegativeFiniteNumber(newPacketGapMs)
        || !isNonNegativeFiniteNumber(newHoldMs)
        || !isNonNegativeFiniteNumber(newSourceGapMs)
        || !isNonNegativeFiniteNumber(newLoopGapMs))
    {
        report("error", "timing", "expected_four_non_negative_ms_values");
        return;
    }

    packetGapMs = Number(newPacketGapMs);
    holdMs = Number(newHoldMs);
    sourceGapMs = Number(newSourceGapMs);
    loopGapMs = Number(newLoopGapMs);
    randomTimingEnabled = false;
    report("timing", packetGapMs, holdMs, sourceGapMs, loopGapMs);
}

function randomtiming(newHoldMinMs, newHoldMaxMs,
                      newSourceGapMinMs, newSourceGapMaxMs)
{
    var argumentCount = arguments.length;

    if (argumentCount !== 2 && argumentCount !== 4)
    {
        report("error", "randomtiming", "expected_2_or_4_ms_values");
        return;
    }

    if (argumentCount === 2)
    {
        newSourceGapMinMs = sourceGapMs;
        newSourceGapMaxMs = sourceGapMs;
    }

    if (!isNonNegativeFiniteNumber(newHoldMinMs)
        || !isNonNegativeFiniteNumber(newHoldMaxMs)
        || !isNonNegativeFiniteNumber(newSourceGapMinMs)
        || !isNonNegativeFiniteNumber(newSourceGapMaxMs)
        || newHoldMinMs > newHoldMaxMs
        || newSourceGapMinMs > newSourceGapMaxMs)
    {
        report("error", "randomtiming", "expected_ascending_non_negative_ranges");
        return;
    }

    randomHoldMinMs = Number(newHoldMinMs);
    randomHoldMaxMs = Number(newHoldMaxMs);
    randomSourceGapMinMs = Number(newSourceGapMinMs);
    randomSourceGapMaxMs = Number(newSourceGapMaxMs);
    randomTimingEnabled = true;

    report("randomtiming",
           "hold", randomHoldMinMs, randomHoldMaxMs,
           "source_gap", randomSourceGapMinMs, randomSourceGapMaxMs);
}

function defaults()
{
    stop();
    restoreDefaults();
    report("defaults_restored");
    dump();
}

function dump()
{
    report("config",
           "zone", zoneName,
           "range", firstSource, lastSource,
           "finger", fingerIndex,
           "note_mode", noteModeName(),
           "notes", firstNoteValue, lastNoteValue,
           "v", vValue,
           "timing_mode", randomTimingEnabled ? "random" : "fixed",
           "timing_ms", packetGapMs, holdMs, sourceGapMs, loopGapMs,
           "random_hold_ms", randomHoldMinMs, randomHoldMaxMs,
           "random_source_gap_ms", randomSourceGapMinMs, randomSourceGapMaxMs);
}

function beginSequence(shouldRepeat)
{
    // Restarting is deterministic and also releases a possibly held source.
    sequenceTask.cancel();
    if (activeSource >= 0)
        emitOff(activeSource);

    repeatSweeps = shouldRepeat;
    currentSource = firstSource;
    activeSource = -1;
    phase = 0;
    isRunning = true;

    report(shouldRepeat ? "started" : "single_sweep_started",
           "zone", zoneName,
           "range", firstSource, lastSource,
           "note_mode", noteModeName(),
           "notes", firstNoteValue, lastNoteValue,
           "timing_mode", randomTimingEnabled ? "random" : "fixed");

    // First message is immediate; later phases are scheduled.
    runPhase();
}

function runPhase()
{
    if (!isRunning)
        return;

    if (phase === 0)
    {
        emitValue(currentSource, "on", 1);
        activeSource = currentSource;
        phase = 1;
        sequenceTask.schedule(packetGapMs);
        return;
    }

    if (phase === 1)
    {
        emitValue(currentSource, "line", noteValueForSource(currentSource));
        phase = 2;
        sequenceTask.schedule(packetGapMs);
        return;
    }

    if (phase === 2)
    {
        emitValue(currentSource, "v", vValue);
        phase = 3;
        var selectedHoldMs = nextHoldMs();
        report("delay", "hold_ms", selectedHoldMs);
        sequenceTask.schedule(selectedHoldMs);
        return;
    }

    emitOff(currentSource);
    phase = 0;

    if (currentSource < lastSource)
    {
        currentSource += 1;
        var selectedSourceGapMs = nextSourceGapMs();
        report("delay", "source_gap_ms", selectedSourceGapMs);
        sequenceTask.schedule(selectedSourceGapMs);
        return;
    }

    if (repeatSweeps)
    {
        currentSource = firstSource;
        report("sweep_complete", "restarting");
        report("delay", "loop_gap_ms", loopGapMs);
        sequenceTask.schedule(loopGapMs);
        return;
    }

    isRunning = false;
    report("sweep_complete");
}

function emitValue(source, parameter, value)
{
    var address = makeAddress(source, parameter);
    outlet(0, address, value);
    report("sent", address, value);
}

function emitOff(source)
{
    var address = makeAddress(source, "off");
    outlet(0, address);
    report("sent", address, "no_argument");
    activeSource = -1;
}

function makeAddress(source, parameter)
{
    return "/cs/" + zoneName + "/" + source
        + "/finger" + fingerIndex + "/" + parameter;
}

function noteValueForSource(source)
{
    if (randomNotesEnabled)
        return randomIntegerInclusive(firstNoteValue, lastNoteValue);

    if (firstNoteValue === lastNoteValue || firstSource === lastSource)
        return firstNoteValue;

    var position = (source - firstSource) / (lastSource - firstSource);
    return Math.round(firstNoteValue
                      + position * (lastNoteValue - firstNoteValue));
}

function noteModeName()
{
    if (randomNotesEnabled)
        return "random";

    return firstNoteValue === lastNoteValue ? "fixed" : "spread";
}

function nextHoldMs()
{
    if (!randomTimingEnabled)
        return holdMs;

    return randomNumberBetween(randomHoldMinMs, randomHoldMaxMs);
}

function nextSourceGapMs()
{
    if (!randomTimingEnabled)
        return sourceGapMs;

    return randomNumberBetween(randomSourceGapMinMs, randomSourceGapMaxMs);
}

function randomIntegerInclusive(minimum, maximum)
{
    return Math.floor(Math.random() * (maximum - minimum + 1)) + minimum;
}

function randomNumberBetween(minimum, maximum)
{
    if (minimum === maximum)
        return minimum;

    return Math.round(minimum + Math.random() * (maximum - minimum));
}

function report()
{
    outlet(1, arrayfromargs(arguments));
}

function restoreDefaults()
{
    zoneName = DEFAULT_ZONE;
    firstSource = DEFAULT_FIRST_SOURCE;
    lastSource = DEFAULT_LAST_SOURCE;
    fingerIndex = DEFAULT_FINGER;
    firstNoteValue = DEFAULT_FIRST_NOTE;
    lastNoteValue = DEFAULT_LAST_NOTE;
    vValue = DEFAULT_V_VALUE;
    packetGapMs = DEFAULT_PACKET_GAP_MS;
    holdMs = DEFAULT_HOLD_MS;
    sourceGapMs = DEFAULT_SOURCE_GAP_MS;
    loopGapMs = DEFAULT_LOOP_GAP_MS;
    randomNotesEnabled = false;
    randomTimingEnabled = false;
    randomHoldMinMs = DEFAULT_HOLD_MS;
    randomHoldMaxMs = DEFAULT_HOLD_MS;
    randomSourceGapMinMs = DEFAULT_SOURCE_GAP_MS;
    randomSourceGapMaxMs = DEFAULT_SOURCE_GAP_MS;
}

function isFiniteNumber(value)
{
    return typeof value === "number" && isFinite(value);
}

function isNonNegativeFiniteNumber(value)
{
    return isFiniteNumber(value) && value >= 0;
}

function isWholeNumberInRange(value, minimum, maximum)
{
    return isFiniteNumber(value)
        && Math.floor(value) === value
        && value >= minimum
        && value <= maximum;
}

function notifydeleted()
{
    // Max calls this when the [js] object is removed from the patch.
    sequenceTask.cancel();
    if (activeSource >= 0)
        emitOff(activeSource);
}
