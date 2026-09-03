#!/usr/bin/env python3
"""Compare Cosmic Symphony OSC JSONL captures.

The recorder stores one JSON object per line: session_start, osc_packet, and
session_end.  This tool flattens OSC events, reconstructs per-source lifecycle
and U/V trajectories, measures timing/motion statistics, and writes machine-
readable tables plus dependency-light PNG charts.

The bundled Codex Python runtime supplies numpy and Pillow when its dependency
directory is placed on PYTHONPATH.  No input file is modified.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import statistics
import tempfile
from collections import Counter, defaultdict
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterable, Sequence

import numpy as np
from PIL import Image, ImageDraw, ImageFont


ADDRESS_RE = re.compile(
    r"^/cs/(?P<zone>[A-Z])/(?P<source>0|[1-9]\d{0,2})/finger(?P<finger>0)/"
    r"(?P<parameter>u|v|on|off|line)$"
)
PALETTE = {
    "background": (7, 10, 15),
    "panel": (14, 20, 28),
    "grid": (38, 49, 61),
    "text": (237, 243, 248),
    "muted": (158, 172, 186),
    "cyan": (88, 214, 255),
    "green": (66, 214, 165),
    "amber": (239, 189, 92),
    "violet": (155, 140, 255),
    "red": (255, 100, 122),
}
SERIES_COLOURS = [
    PALETTE["cyan"],
    PALETTE["amber"],
    PALETTE["violet"],
    PALETTE["green"],
    PALETTE["red"],
    (89, 156, 255),
    (255, 126, 196),
]


@dataclass(frozen=True)
class Event:
    packet: int
    event: int
    timestamp_ms: int
    elapsed_ms: int
    packet_offset_ms: int
    address: str
    zone: str | None
    source: int | None
    finger: int | None
    parameter: str | None
    value: float | None
    packet_completion_reason: str | None


@dataclass(frozen=True)
class MotionPair:
    packet: int
    timestamp_ms: int
    elapsed_ms: int
    zone: str
    source: int
    u: float
    v: float
    lifecycle_boundary: bool


@dataclass
class Capture:
    label: str
    path: Path
    start: dict[str, Any]
    end: dict[str, Any]
    packets: list[dict[str, Any]]
    events: list[Event]
    pairs: list[MotionPair]


def finite_number(value: Any) -> float | None:
    if isinstance(value, bool):
        return None
    if not isinstance(value, (int, float)):
        return None
    result = float(value)
    return result if math.isfinite(result) else None


def exact_int(
    mapping: dict[str, Any],
    key: str,
    context: str,
    *,
    minimum: int | None = None,
) -> int:
    """Read an integer metadata field without JSON coercion.

    Python's ``int()`` accepts booleans, floats, and numeric strings.  Capture
    validation must not silently normalise any of those because IDs and clocks
    are part of the recorder's ordering contract.
    """
    value = mapping.get(key)
    if type(value) is not int:
        raise ValueError(f"{context}: {key} must be an integer")
    if minimum is not None and value < minimum:
        raise ValueError(f"{context}: {key} must be >= {minimum}")
    return value


def exact_alias_int(
    mapping: dict[str, Any],
    keys: Sequence[str],
    context: str,
    *,
    minimum: int | None = None,
) -> int:
    present = [key for key in keys if key in mapping]
    if not present:
        raise ValueError(f"{context}: expected one of {', '.join(keys)}")
    values = [exact_int(mapping, key, context, minimum=minimum) for key in present]
    if any(value != values[0] for value in values[1:]):
        raise ValueError(f"{context}: timestamp aliases disagree")
    return values[0]


def optional_exact_int(
    mapping: dict[str, Any],
    key: str,
    context: str,
    *,
    default: int = 0,
    minimum: int | None = None,
) -> int:
    if key not in mapping:
        return default
    return exact_int(mapping, key, context, minimum=minimum)


IDENTITY_ZONE_STRIDE = 1_000_000


def source_identity(zone: str | None, source: int) -> int:
    """Return a collision-free numeric key for a zone-local source ID."""
    normalised = str(zone or "")
    if len(normalised) != 1 or not ("A" <= normalised <= "Z"):
        raise ValueError(f"invalid OSC zone: {zone!r}")
    if source < 0 or source > 255:
        raise ValueError(f"invalid OSC source: {source}")
    zone_index = ord(normalised) - ord("A")
    return zone_index * IDENTITY_ZONE_STRIDE + source


def source_identity_label(identity: int) -> str:
    zone_index, source = divmod(int(identity), IDENTITY_ZONE_STRIDE)
    return f"{chr(ord('A') + max(0, min(25, zone_index)))}/{source}"


def load_capture(label: str, path: Path) -> Capture:
    rows: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, 1):
            if not line.strip():
                continue
            try:
                row = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(f"{path}:{line_number}: {error}") from error
            if not isinstance(row, dict):
                raise ValueError(f"{path}:{line_number}: JSON object required")
            rows.append(row)

    starts = [row for row in rows if row.get("kind") == "session_start"]
    ends = [row for row in rows if row.get("kind") == "session_end"]
    packets = [row for row in rows if row.get("kind") == "osc_packet"]
    if len(starts) != 1 or len(ends) != 1 or not packets:
        raise ValueError(
            f"{path}: expected one session_start, one session_end, and packets"
        )
    if rows[0].get("kind") != "session_start" or rows[-1].get("kind") != "session_end":
        raise ValueError(f"{path}: session_start must be first and session_end last")
    if len(rows) != len(packets) + 2:
        raise ValueError(f"{path}: unsupported row kind in capture")

    for line_number, row in enumerate(rows, 1):
        version = exact_int(row, "format_version", f"{path}:{line_number}")
        if version != 1:
            raise ValueError(
                f"{path}:{line_number}: unsupported format_version {version}"
            )

    start_ms = exact_int(starts[0], "started_unix_ms", f"{path}: session_start", minimum=0)
    end_ms = exact_alias_int(
        ends[0],
        ("stopped_unix_ms", "ended_unix_ms"),
        f"{path}: session_end",
        minimum=0,
    )
    if end_ms < start_ms:
        raise ValueError(f"{path}: invalid session timestamp range")
    for key in (
        "incomplete_packets",
        "parser_warnings",
        "write_errors",
        "events_dropped_during_fault",
    ):
        optional_exact_int(
            ends[0], key, f"{path}: session_end", minimum=0
        )

    expected_packet_id = 1
    expected_event_id = 1
    previous_packet_ms = start_ms
    previous_event_ms = start_ms
    # OSC U/V packets can be delayed or coalesced independently from lifecycle
    # packets.  Track the source state in recorder event order so a stale U/V
    # pair received while the finger is inactive cannot bridge two gestures and
    # become a many-second physical motion interval.
    source_active: dict[int, bool] = defaultdict(bool)

    events: list[Event] = []
    pairs: list[MotionPair] = []
    for packet in packets:
        packet_id = exact_int(packet, "packet", f"{path}: packet", minimum=1)
        if packet_id != expected_packet_id:
            raise ValueError(
                f"{path}: packet sequence expected {expected_packet_id}, got {packet_id}"
            )
        expected_packet_id += 1
        packet_context = f"{path}: packet {packet_id}"
        packet_timestamp = exact_int(
            packet, "received_unix_ms", packet_context, minimum=0
        )
        packet_elapsed = exact_int(packet, "elapsed_ms", packet_context, minimum=0)
        packet_completed = exact_int(
            packet, "completed_unix_ms", packet_context, minimum=0
        )
        dispatch_span = exact_int(
            packet, "dispatch_span_ms", packet_context, minimum=0
        )
        if not (previous_packet_ms <= packet_timestamp <= end_ms):
            raise ValueError(f"{path}: packet {packet_id} timestamp is not monotonic")
        if not (packet_timestamp <= packet_completed <= end_ms):
            raise ValueError(f"{path}: packet {packet_id} completion timestamp is invalid")
        if dispatch_span != packet_completed - packet_timestamp:
            raise ValueError(f"{path}: packet {packet_id} dispatch span mismatch")
        if previous_event_ms > packet_timestamp:
            raise ValueError(
                f"{path}: packet {packet_id - 1} event timestamp crosses "
                f"packet {packet_id} arrival"
            )
        if packet_elapsed != packet_timestamp - start_ms:
            raise ValueError(f"{path}: packet {packet_id} elapsed timestamp mismatch")
        previous_packet_ms = packet_timestamp
        if packet.get("complete") is not True:
            raise ValueError(f"{path}: packet {packet_id} is incomplete")
        completion_reason = packet.get("completion_reason")
        if completion_reason is not None and not isinstance(completion_reason, str):
            raise ValueError(
                f"{path}: packet {packet_id} completion_reason must be a string"
            )
        framing = packet.get("framing")
        if framing == "cnmat_bundle":
            timetag = packet.get("timetag")
            if not isinstance(timetag, dict) or timetag.get("immediate") is not True:
                raise ValueError(f"{path}: packet {packet_id} has a dated/non-immediate bundle")
        packet_events = packet.get("events")
        if not isinstance(packet_events, list) or not packet_events:
            raise ValueError(f"{path}: packet {packet_id} has no events array")

        axes: dict[int, dict[str, float]] = defaultdict(dict)
        packet_axis_pairs: list[tuple[str, int, float, float, bool]] = []
        lifecycle_sources: set[int] = set()
        for raw_event in packet_events:
            if not isinstance(raw_event, dict):
                raise ValueError(f"{path}: packet {packet_id} event must be an object")
            event_id = exact_int(
                raw_event,
                "event",
                f"{path}: packet {packet_id} event",
                minimum=1,
            )
            if event_id != expected_event_id:
                raise ValueError(
                    f"{path}: event sequence expected {expected_event_id}, got {event_id}"
                )
            expected_event_id += 1
            address = raw_event.get("address")
            if not isinstance(address, str):
                raise ValueError(f"{path}: event {event_id} address must be a string")
            match = ADDRESS_RE.match(address)
            if match is None:
                raise ValueError(
                    f"{path}: packet {packet_id} contains a non-canonical "
                    f"Cosmic address: {address!r}"
                )
            zone = match.group("zone") if match else None
            source = int(match.group("source")) if match else None
            finger = int(match.group("finger")) if match else None
            parameter = match.group("parameter") if match else None
            if source is None or source > 255:
                raise ValueError(
                    f"{path}: packet {packet_id} source must be in 0..255: "
                    f"{address!r}"
                )
            args = raw_event.get("args")
            inferred_types = raw_event.get("arg_types_inferred")
            if not isinstance(args, list) or not isinstance(inferred_types, list):
                raise ValueError(f"{path}: event {event_id} has no typed args array")
            if parameter in ("u", "v"):
                if len(args) != 1 or inferred_types != ["float32_by_contract"]:
                    raise ValueError(f"{path}: event {event_id} requires one float32 value")
                value = finite_number(args[0])
                if value is None or not (0.0 <= value <= 1.0):
                    raise ValueError(f"{path}: event {event_id} U/V must be finite in 0..1")
                if abs(value * 100.0 - round(value * 100.0)) > 1.0e-4:
                    raise ValueError(f"{path}: event {event_id} U/V is not 0.01-quantized")
            elif parameter == "on":
                if (len(args) != 1 or inferred_types != ["int32_by_contract"]
                        or isinstance(args[0], bool) or not isinstance(args[0], int)
                        or args[0] not in (0, 1)):
                    raise ValueError(f"{path}: event {event_id} On requires int32 0 or 1")
                value = float(args[0])
            elif parameter == "off":
                if args or inferred_types:
                    raise ValueError(f"{path}: event {event_id} legacy Off must be argless")
                value = 0.0
            else:  # legacy line
                if (len(args) != 1 or inferred_types != ["int32_by_contract"]
                        or isinstance(args[0], bool) or not isinstance(args[0], int)
                        or not (0 <= args[0] <= 127)):
                    raise ValueError(f"{path}: event {event_id} line requires int32 0..127")
                value = float(args[0])

            event_context = f"{path}: event {event_id}"
            timestamp_ms = exact_int(
                raw_event, "received_unix_ms", event_context, minimum=0
            )
            elapsed_ms = exact_int(raw_event, "elapsed_ms", event_context, minimum=0)
            packet_offset_ms = exact_int(
                raw_event, "packet_offset_ms", event_context, minimum=0
            )
            if not (packet_timestamp <= timestamp_ms <= packet_completed):
                raise ValueError(f"{path}: event {event_id} timestamp is outside its packet")
            if timestamp_ms < previous_event_ms:
                raise ValueError(
                    f"{path}: event {event_id} timestamp is not globally monotonic"
                )
            if elapsed_ms != timestamp_ms - start_ms:
                raise ValueError(f"{path}: event {event_id} elapsed timestamp mismatch")
            if packet_offset_ms != timestamp_ms - packet_timestamp:
                raise ValueError(f"{path}: event {event_id} packet offset mismatch")
            previous_event_ms = timestamp_ms
            events.append(
                Event(
                    packet=packet_id,
                    event=event_id,
                    timestamp_ms=timestamp_ms,
                    elapsed_ms=elapsed_ms,
                    packet_offset_ms=packet_offset_ms,
                    address=address,
                    zone=zone,
                    source=source,
                    finger=finger,
                    parameter=parameter,
                    value=value,
                    packet_completion_reason=completion_reason,
                )
            )

            if source is None or parameter is None:
                continue
            identity = source_identity(zone, source)
            if parameter in ("u", "v") and value is not None:
                axes[identity][parameter] = value
                if "u" in axes[identity] and "v" in axes[identity]:
                    packet_axis_pairs.append(
                        (
                            str(zone),
                            source,
                            axes[identity]["u"],
                            axes[identity]["v"],
                            not source_active[identity],
                        )
                    )
                    axes[identity].clear()
            elif parameter in ("on", "off"):
                lifecycle_sources.add(identity)
                source_active[identity] = (
                    parameter == "on" and value is not None and value != 0.0
                )

        for zone, source, u_value, v_value, paired_while_inactive in packet_axis_pairs:
            identity = source_identity(zone, source)
            pairs.append(
                MotionPair(
                    packet=packet_id,
                    timestamp_ms=packet_timestamp,
                    elapsed_ms=packet_elapsed,
                    zone=zone,
                    source=source,
                    u=u_value,
                    v=v_value,
                    lifecycle_boundary=(
                        paired_while_inactive or identity in lifecycle_sources
                    ),
                )
            )

    if exact_int(ends[0], "packets", f"{path}: session_end", minimum=0) != len(packets):
        raise ValueError(f"{path}: session_end packet count mismatch")
    if exact_int(ends[0], "events", f"{path}: session_end", minimum=0) != len(events):
        raise ValueError(f"{path}: session_end event count mismatch")
    reported_duration = exact_alias_int(
        ends[0],
        ("duration_ms", "elapsed_ms"),
        f"{path}: session_end",
        minimum=0,
    )
    if reported_duration != end_ms - start_ms:
        raise ValueError(f"{path}: session_end duration mismatch")

    # Recorder event IDs are the authoritative in-packet and inter-packet order.
    # Individual Max dispatch timestamps can differ by 1–2 ms inside one bundle.
    events.sort(key=lambda item: item.event)
    pairs.sort(
        key=lambda item: (
            item.timestamp_ms,
            item.packet,
            source_identity(item.zone, item.source),
        )
    )
    return Capture(label, path, starts[0], ends[0], packets, events, pairs)


def percentile(values: Sequence[float], value: float) -> float:
    if not values:
        return 0.0
    return float(np.percentile(np.asarray(values, dtype=float), value))


def describe(values: Iterable[float]) -> dict[str, float | int]:
    clean = [float(value) for value in values if math.isfinite(float(value))]
    if not clean:
        return {
            "count": 0,
            "min": 0.0,
            "p05": 0.0,
            "median": 0.0,
            "mean": 0.0,
            "p95": 0.0,
            "p99": 0.0,
            "max": 0.0,
            "stddev": 0.0,
        }
    array = np.asarray(clean, dtype=float)
    return {
        "count": len(clean),
        "min": float(np.min(array)),
        "p05": float(np.percentile(array, 5)),
        "median": float(np.median(array)),
        "mean": float(np.mean(array)),
        "p95": float(np.percentile(array, 95)),
        "p99": float(np.percentile(array, 99)),
        "max": float(np.max(array)),
        "stddev": float(np.std(array)),
    }


def longest_true_run(flags: Iterable[bool]) -> int:
    longest = 0
    current = 0
    for flag in flags:
        if flag:
            current += 1
            longest = max(longest, current)
        else:
            current = 0
    return longest


def pearson(left: Sequence[float], right: Sequence[float]) -> float | None:
    if len(left) < 4 or len(right) != len(left):
        return None
    a = np.asarray(left, dtype=float)
    b = np.asarray(right, dtype=float)
    if np.std(a) < 1.0e-12 or np.std(b) < 1.0e-12:
        return None
    value = float(np.corrcoef(a, b)[0, 1])
    return value if math.isfinite(value) else None


def distribution_distances(reference: Sequence[float], candidate: Sequence[float]) -> dict[str, float]:
    if not reference or not candidate:
        return {"ks": 0.0, "quantile_mae": 0.0, "normalised_quantile_mae": 0.0}
    ref = np.sort(np.asarray(reference, dtype=float))
    cand = np.sort(np.asarray(candidate, dtype=float))
    combined = np.sort(np.unique(np.concatenate([ref, cand])))
    ref_cdf = np.searchsorted(ref, combined, side="right") / len(ref)
    cand_cdf = np.searchsorted(cand, combined, side="right") / len(cand)
    quantiles = np.linspace(0.01, 0.99, 99)
    ref_q = np.quantile(ref, quantiles)
    cand_q = np.quantile(cand, quantiles)
    mae = float(np.mean(np.abs(ref_q - cand_q)))
    ref_scale = max(float(np.quantile(ref, 0.95) - np.quantile(ref, 0.05)), 1.0e-9)
    return {
        "ks": float(np.max(np.abs(ref_cdf - cand_cdf))),
        "quantile_mae": mae,
        "normalised_quantile_mae": mae / ref_scale,
    }


def source_lifecycle(capture: Capture) -> tuple[dict[str, Any], dict[int, dict[str, Any]]]:
    state: dict[int, bool] = defaultdict(bool)
    onset: dict[int, int] = {}
    last_release: dict[int, int] = {}
    per_source: dict[int, dict[str, Any]] = defaultdict(
        lambda: {
            "onsets": 0,
            "releases": 0,
            "duplicate_onsets": 0,
            "duplicate_releases": 0,
            "cleanup_off_events": 0,
            "right_censored_gestures": 0,
            "gesture_ms": [],
            "idle_ms": [],
            "active_ms_in_capture": 0,
            "span_ms": 0,
            "duty_fraction": 0.0,
        }
    )

    first_event_ms: dict[int, int] = {}
    last_event_ms: dict[int, int] = {}
    packet_transitions: dict[tuple[int, int], set[bool]] = defaultdict(set)

    for event in capture.events:
        if event.source is None or event.parameter not in ("on", "off"):
            continue
        source = source_identity(event.zone, event.source)
        info = per_source[source]
        first_event_ms.setdefault(source, event.timestamp_ms)
        last_event_ms[source] = event.timestamp_ms
        is_on = event.parameter == "on" and event.value is not None and event.value != 0.0
        packet_transitions[(event.packet, source)].add(is_on)
        is_cleanup_release = (
            not is_on
            and event.packet_completion_reason == "simulator_cleanup"
        )
        if is_cleanup_release:
            # GenerateCrowdSimulatorTrace emits protocol-valid Off traffic at
            # EOF so a replay cannot leave a held note. That synthetic boundary
            # says only that the observation ended: it is not a sampled human
            # release and must not truncate the hold/idle distributions.
            info["cleanup_off_events"] += 1
            if state[source] and source in onset:
                info["right_censored_gestures"] += 1
            else:
                info["duplicate_releases"] += 1
            continue
        if is_on:
            info["onsets"] += 1
            if state[source]:
                info["duplicate_onsets"] += 1
                continue
            if source in last_release:
                info["idle_ms"].append(event.timestamp_ms - last_release[source])
            state[source] = True
            onset[source] = event.timestamp_ms
        else:
            info["releases"] += 1
            if not state[source]:
                info["duplicate_releases"] += 1
                continue
            state[source] = False
            if source in onset:
                info["gesture_ms"].append(event.timestamp_ms - onset.pop(source))
            last_release[source] = event.timestamp_ms

    capture_end_ms = capture.events[-1].timestamp_ms if capture.events else 0
    active_user_ms = 0
    for source, info in per_source.items():
        active_ms = int(sum(info["gesture_ms"]))
        if state[source] and source in onset:
            active_ms += max(0, capture_end_ms - onset[source])
        span_ms = max(0, last_event_ms[source] - first_event_ms[source])
        info["active_ms_in_capture"] = active_ms
        info["span_ms"] = span_ms
        info["duty_fraction"] = active_ms / span_ms if span_ms > 0 else 0.0
        active_user_ms += active_ms

    gestures = [value for info in per_source.values() for value in info["gesture_ms"]]
    idle = [value for info in per_source.values() for value in info["idle_ms"]]
    summary = {
        "onsets": sum(info["onsets"] for info in per_source.values()),
        "releases": sum(info["releases"] for info in per_source.values()),
        "duplicate_onsets": sum(info["duplicate_onsets"] for info in per_source.values()),
        "duplicate_releases": sum(info["duplicate_releases"] for info in per_source.values()),
        "cleanup_off_events": sum(info["cleanup_off_events"] for info in per_source.values()),
        "right_censored_gestures": sum(
            info["right_censored_gestures"] for info in per_source.values()
        ),
        "active_at_end": sum(1 for active in state.values() if active),
        "active_user_seconds": active_user_ms / 1000.0,
        "gesture_ms": describe(gestures),
        "idle_ms": describe(idle),
        "gesture_under_10ms_fraction": (
            sum(value < 10 for value in gestures) / len(gestures) if gestures else 0.0
        ),
        "gesture_under_200ms_fraction": (
            sum(value < 200 for value in gestures) / len(gestures) if gestures else 0.0
        ),
        "gesture_under_250ms_fraction": (
            sum(value < 250 for value in gestures) / len(gestures) if gestures else 0.0
        ),
        "gesture_over_3000ms_fraction": (
            sum(value > 3000 for value in gestures) / len(gestures) if gestures else 0.0
        ),
        "same_packet_off_on_count": sum(
            values == {False, True} for values in packet_transitions.values()
        ),
    }
    return summary, dict(per_source)


def motion_sequences(capture: Capture) -> dict[int, list[MotionPair]]:
    result: dict[int, list[MotionPair]] = defaultdict(list)
    for pair in capture.pairs:
        result[source_identity(pair.zone, pair.source)].append(pair)
    for values in result.values():
        values.sort(key=lambda pair: (pair.timestamp_ms, pair.packet))
    return dict(result)


def spectral_entropy(power: np.ndarray) -> float | None:
    clean = np.asarray(power, dtype=float)
    clean = clean[np.isfinite(clean) & (clean > 0.0)]
    total = float(np.sum(clean))
    if len(clean) < 2 or total <= 1.0e-15:
        return None
    probabilities = clean / total
    return float(-np.sum(probabilities * np.log(probabilities)) / math.log(len(clean)))


def advanced_motion_metrics(pairs: list[MotionPair]) -> tuple[dict[str, Any], dict[str, list[float]]]:
    gestures: list[list[MotionPair]] = []
    current: list[MotionPair] = []
    for pair in pairs:
        if pair.lifecycle_boundary and current:
            gestures.append(current)
            current = []
        current.append(pair)
    if current:
        gestures.append(current)

    values: dict[str, list[float]] = defaultdict(list)
    longest_pause_ms = 0.0
    for gesture in gestures:
        if len(gesture) < 2:
            continue
        velocities: list[tuple[float, float, float]] = []
        distances: list[float] = []
        pause_start: int | None = None
        for previous, sample in zip(gesture, gesture[1:]):
            dt = (sample.timestamp_ms - previous.timestamp_ms) / 1000.0
            if dt <= 0.0:
                continue
            du = sample.u - previous.u
            dv = sample.v - previous.v
            distance = math.hypot(du, dv)
            distances.append(distance)
            velocities.append((du / dt, dv / dt, dt))
            if distance < 1.0e-9:
                if pause_start is None:
                    pause_start = previous.timestamp_ms
                longest_pause_ms = max(
                    longest_pause_ms,
                    float(sample.timestamp_ms - pause_start),
                )
            else:
                pause_start = None

        accelerations: list[tuple[float, float, float]] = []
        for previous, sample in zip(velocities, velocities[1:]):
            dt = 0.5 * (previous[2] + sample[2])
            if dt <= 0.0:
                continue
            au = (sample[0] - previous[0]) / dt
            av = (sample[1] - previous[1]) / dt
            accelerations.append((au, av, dt))
            values["vector_acceleration_per_second2"].append(math.hypot(au, av))
        for previous, sample in zip(accelerations, accelerations[1:]):
            dt = 0.5 * (previous[2] + sample[2])
            if dt <= 0.0:
                continue
            ju = (sample[0] - previous[0]) / dt
            jv = (sample[1] - previous[1]) / dt
            values["jerk_per_second3"].append(math.hypot(ju, jv))

        path_length = sum(distances)
        displacement = math.hypot(
            gesture[-1].u - gesture[0].u,
            gesture[-1].v - gesture[0].v,
        )
        if path_length > 1.0e-9:
            values["path_tortuosity"].append(path_length / max(displacement, 1.0e-9))

        speeds = [math.hypot(u, v) for u, v, _ in velocities]
        lag_value = pearson(speeds[:-1], speeds[1:]) if len(speeds) >= 5 else None
        if lag_value is not None:
            values["gesture_speed_lag1"].append(lag_value)

        # Spectral metrics use only sustained gestures and a regular 20 Hz grid.
        unique_samples = {pair.timestamp_ms: pair for pair in gesture}
        ordered = [unique_samples[key] for key in sorted(unique_samples)]
        duration = (ordered[-1].timestamp_ms - ordered[0].timestamp_ms) / 1000.0
        if len(ordered) < 20 or duration < 1.0:
            continue
        source_times = np.asarray(
            [(pair.timestamp_ms - ordered[0].timestamp_ms) / 1000.0 for pair in ordered],
            dtype=float,
        )
        regular_times = np.arange(0.0, source_times[-1] + 1.0e-9, 0.05)
        if len(regular_times) < 20:
            continue
        u_values = np.interp(regular_times, source_times, [pair.u for pair in ordered])
        v_values = np.interp(regular_times, source_times, [pair.v for pair in ordered])
        index = np.arange(len(regular_times), dtype=float)
        for array in (u_values, v_values):
            coefficients = np.polyfit(index, array, 1)
            array -= np.polyval(coefficients, index)
        trajectory_power = (
            np.abs(np.fft.rfft(u_values)) ** 2
            + np.abs(np.fft.rfft(v_values)) ** 2
        )
        trajectory_power[0] = 0.0
        entropy = spectral_entropy(trajectory_power[1:])
        total_power = float(np.sum(trajectory_power))
        if entropy is not None and total_power > 1.0e-15:
            values["trajectory_spectral_entropy"].append(entropy)
            values["dominant_trajectory_power_fraction"].append(
                float(np.max(trajectory_power) / total_power)
            )

        step_values = np.hypot(np.diff(u_values), np.diff(v_values))
        step_values -= float(np.mean(step_values))
        step_power = np.abs(np.fft.rfft(step_values)) ** 2
        if len(step_power):
            step_power[0] = 0.0
        step_entropy = spectral_entropy(step_power[1:])
        if step_entropy is not None:
            values["step_spectral_entropy"].append(step_entropy)

    summary = {name: describe(series) for name, series in values.items()}
    summary["longest_same_pair_ms"] = longest_pause_ms
    return summary, dict(values)


def motion_metrics(capture: Capture) -> tuple[dict[str, Any], dict[int, dict[str, Any]], dict[str, list[float]]]:
    sequences = motion_sequences(capture)
    global_values: dict[str, list[float]] = defaultdict(list)
    per_source: dict[int, dict[str, Any]] = {}

    u_values = [pair.u for pair in capture.pairs]
    v_values = [pair.v for pair in capture.pairs]
    for source, pairs in sequences.items():
        intervals: list[float] = []
        continuous_intervals: list[float] = []
        steps: list[float] = []
        continuous_steps: list[float] = []
        speeds: list[float] = []
        continuous_speeds: list[float] = []
        accelerations: list[float] = []
        turns_degrees: list[float] = []
        same_flags: list[bool] = []
        unchanged_u: list[bool] = []
        unchanged_v: list[bool] = []
        previous_speed: float | None = None
        previous_vector: tuple[float, float] | None = None

        for previous, current in zip(pairs, pairs[1:]):
            dt_ms = current.timestamp_ms - previous.timestamp_ms
            if dt_ms <= 0:
                continue
            dt = dt_ms / 1000.0
            du = current.u - previous.u
            dv = current.v - previous.v
            distance = math.hypot(du, dv)
            speed = distance / dt
            boundary = current.lifecycle_boundary

            intervals.append(float(dt_ms))
            steps.append(distance)
            speeds.append(speed)
            same_flags.append(distance < 1.0e-9)
            unchanged_u.append(abs(du) < 1.0e-9)
            unchanged_v.append(abs(dv) < 1.0e-9)
            if not boundary:
                continuous_intervals.append(float(dt_ms))
                continuous_steps.append(distance)
                continuous_speeds.append(speed)
                if previous_speed is not None:
                    accelerations.append(abs(speed - previous_speed) / dt)
                if previous_vector is not None and distance > 1.0e-9:
                    previous_length = math.hypot(*previous_vector)
                    if previous_length > 1.0e-9:
                        cosine = (previous_vector[0] * du + previous_vector[1] * dv) / (
                            previous_length * distance
                        )
                        turns_degrees.append(
                            math.degrees(math.acos(max(-1.0, min(1.0, cosine))))
                        )
                previous_speed = speed
                if distance > 1.0e-9:
                    previous_vector = (du, dv)
            else:
                previous_speed = None
                previous_vector = None

        source_u = [pair.u for pair in pairs]
        source_v = [pair.v for pair in pairs]
        advanced_summary, advanced_values = advanced_motion_metrics(pairs)
        edge_fraction = (
            sum(
                u <= 0.05 or u >= 0.95 or v <= 0.05 or v >= 0.95
                for u, v in zip(source_u, source_v)
            )
            / len(pairs)
            if pairs
            else 0.0
        )
        occupancy, _, _ = np.histogram2d(
            np.asarray(source_u),
            np.asarray(source_v),
            bins=10,
            range=[[0.0, 1.0], [0.0, 1.0]],
        )
        occupied = occupancy[occupancy > 0.0]
        occupancy_probabilities = occupied / max(float(np.sum(occupied)), 1.0)
        occupancy_entropy = (
            float(
                -np.sum(occupancy_probabilities * np.log(occupancy_probabilities))
                / math.log(100.0)
            )
            if len(occupied)
            else 0.0
        )
        delta_counts = Counter(
            (round(current.u - previous.u, 2), round(current.v - previous.v, 2))
            for previous, current in zip(pairs, pairs[1:])
            if not current.lifecycle_boundary
        )
        delta_total = sum(delta_counts.values())
        source_info = {
            "motion_pairs": len(pairs),
            "u": describe(source_u),
            "v": describe(source_v),
            "interval_ms": describe(intervals),
            "continuous_interval_ms": describe(continuous_intervals),
            "step_distance": describe(steps),
            "continuous_step_distance": describe(continuous_steps),
            "speed_per_second": describe(speeds),
            "continuous_speed_per_second": describe(continuous_speeds),
            "absolute_acceleration_per_second2": describe(accelerations),
            "turn_degrees": describe(turns_degrees),
            "same_pair_fraction": (sum(same_flags) / len(same_flags) if same_flags else 0.0),
            "u_unchanged_fraction": (
                sum(unchanged_u) / len(unchanged_u) if unchanged_u else 0.0
            ),
            "v_unchanged_fraction": (
                sum(unchanged_v) / len(unchanged_v) if unchanged_v else 0.0
            ),
            "longest_same_pair_run": longest_true_run(same_flags),
            "micro_step_fraction": (
                sum(0.0 < value <= 0.02 for value in continuous_steps)
                / len(continuous_steps)
                if continuous_steps
                else 0.0
            ),
            "pause_step_fraction": (
                sum(value < 1.0e-9 for value in continuous_steps)
                / len(continuous_steps)
                if continuous_steps
                else 0.0
            ),
            "large_step_fraction": (
                sum(value >= 0.15 for value in continuous_steps)
                / len(continuous_steps)
                if continuous_steps
                else 0.0
            ),
            "unique_pair_fraction": (
                len(set(zip(source_u, source_v))) / len(pairs) if pairs else 0.0
            ),
            "turn_over_90_fraction": (
                sum(value > 90.0 for value in turns_degrees) / len(turns_degrees)
                if turns_degrees
                else 0.0
            ),
            "edge_band_fraction": edge_fraction,
            "occupancy_entropy_10x10": occupancy_entropy,
            "top_10_delta_fraction": (
                sum(count for _, count in delta_counts.most_common(10)) / delta_total
                if delta_total
                else 0.0
            ),
            **advanced_summary,
        }
        per_source[source] = source_info

        for name, values in {
            "interval_ms": intervals,
            "continuous_interval_ms": continuous_intervals,
            "step_distance": steps,
            "continuous_step_distance": continuous_steps,
            "speed_per_second": speeds,
            "continuous_speed_per_second": continuous_speeds,
            "absolute_acceleration_per_second2": accelerations,
            "turn_degrees": turns_degrees,
            **advanced_values,
        }.items():
            global_values[name].extend(values)

    adjacent_u = [
        abs(current - previous) < 1.0e-9
        for previous, current in zip(u_values, u_values[1:])
    ]
    adjacent_v = [
        abs(current - previous) < 1.0e-9
        for previous, current in zip(v_values, v_values[1:])
    ]
    summary = {
        "motion_pairs": len(capture.pairs),
        "u": describe(u_values),
        "v": describe(v_values),
        "u_unique_values": len(set(u_values)),
        "v_unique_values": len(set(v_values)),
        "u_adjacent_repeat_fraction_global_order": (
            sum(adjacent_u) / len(adjacent_u) if adjacent_u else 0.0
        ),
        "v_adjacent_repeat_fraction_global_order": (
            sum(adjacent_v) / len(adjacent_v) if adjacent_v else 0.0
        ),
        "edge_band_fraction": (
            sum(
                u <= 0.05 or u >= 0.95 or v <= 0.05 or v >= 0.95
                for u, v in zip(u_values, v_values)
            )
            / len(capture.pairs)
            if capture.pairs
            else 0.0
        ),
    }
    for name, values in global_values.items():
        summary[name] = describe(values)
    for fraction_name in (
        "same_pair_fraction",
        "u_unchanged_fraction",
        "v_unchanged_fraction",
        "micro_step_fraction",
        "pause_step_fraction",
        "large_step_fraction",
        "unique_pair_fraction",
        "turn_over_90_fraction",
        "top_10_delta_fraction",
    ):
        weighted = []
        for info in per_source.values():
            count = max(1, int(info["motion_pairs"]) - 1)
            weighted.extend([float(info[fraction_name])] * count)
        summary[fraction_name] = float(np.mean(weighted)) if weighted else 0.0
    summary["longest_same_pair_run"] = max(
        (int(info["longest_same_pair_run"]) for info in per_source.values()),
        default=0,
    )
    summary["longest_same_pair_ms"] = max(
        (float(info.get("longest_same_pair_ms", 0.0)) for info in per_source.values()),
        default=0.0,
    )
    global_occupancy, _, _ = np.histogram2d(
        np.asarray(u_values),
        np.asarray(v_values),
        bins=10,
        range=[[0.0, 1.0], [0.0, 1.0]],
    )
    occupied = global_occupancy[global_occupancy > 0.0]
    occupancy_probabilities = occupied / max(float(np.sum(occupied)), 1.0)
    summary["occupancy_entropy_10x10"] = (
        float(
            -np.sum(occupancy_probabilities * np.log(occupancy_probabilities))
            / math.log(100.0)
        )
        if len(occupied)
        else 0.0
    )

    global_delta_counts: Counter[tuple[float, float]] = Counter()
    for pairs in sequences.values():
        global_delta_counts.update(
            (round(current.u - previous.u, 2), round(current.v - previous.v, 2))
            for previous, current in zip(pairs, pairs[1:])
            if not current.lifecycle_boundary
        )
    global_delta_total = sum(global_delta_counts.values())
    summary["top_10_delta_fraction"] = (
        sum(count for _, count in global_delta_counts.most_common(10))
        / global_delta_total
        if global_delta_total
        else 0.0
    )
    turn_values = global_values.get("turn_degrees", [])
    summary["turn_over_90_fraction"] = (
        sum(value > 90.0 for value in turn_values) / len(turn_values)
        if turn_values
        else 0.0
    )
    magnitude_values = global_values.get("continuous_step_distance", [])
    magnitude_histogram, _ = np.histogram(
        np.asarray(magnitude_values),
        bins=np.linspace(0.0, 0.36, 37),
    )
    magnitude_probabilities = magnitude_histogram[magnitude_histogram > 0]
    magnitude_probabilities = (
        magnitude_probabilities / max(float(np.sum(magnitude_probabilities)), 1.0)
    )
    summary["step_magnitude_entropy_0_01_bins"] = (
        float(
            -np.sum(magnitude_probabilities * np.log(magnitude_probabilities))
            / math.log(36.0)
        )
        if len(magnitude_probabilities)
        else 0.0
    )
    return summary, per_source, dict(global_values)


def rate_series(capture: Capture, event_level: bool) -> tuple[list[int], list[int]]:
    times = (
        [event.timestamp_ms for event in capture.events]
        if event_level
        else [packet["received_unix_ms"] for packet in capture.packets]
    )
    if not times:
        return [], []
    origin = min(times)
    counts: Counter[int] = Counter((timestamp - origin) // 1000 for timestamp in times)
    seconds = list(range(max(counts) + 1))
    return seconds, [counts[second] for second in seconds]


def timing_metrics(capture: Capture) -> tuple[dict[str, Any], dict[str, list[float]]]:
    packet_times = [packet["received_unix_ms"] for packet in capture.packets]
    intervals = [
        float(current - previous)
        for previous, current in zip(packet_times, packet_times[1:])
        if current >= previous
    ]
    event_seconds, events_per_second = rate_series(capture, True)
    packet_seconds, packets_per_second = rate_series(capture, False)
    del event_seconds, packet_seconds
    cardinals = [len(packet.get("events", [])) for packet in capture.packets]
    dispatch_spans = [packet["dispatch_span_ms"] for packet in capture.packets]

    bursts: list[int] = []
    current_burst = 1 if packet_times else 0
    for interval in intervals:
        if interval <= 5.0:
            current_burst += 1
        else:
            if current_burst:
                bursts.append(current_burst)
            current_burst = 1
    if current_burst:
        bursts.append(current_burst)

    start_ms = capture.start["started_unix_ms"]
    stop_ms = (
        capture.end["stopped_unix_ms"]
        if "stopped_unix_ms" in capture.end
        else capture.end["ended_unix_ms"]
    )
    active_span_ms = packet_times[-1] - packet_times[0]
    summary = {
        "session_duration_ms": capture.end.get(
            "duration_ms", capture.end.get("elapsed_ms", stop_ms - start_ms)
        ),
        "prelude_ms": packet_times[0] - start_ms,
        "active_packet_span_ms": active_span_ms,
        "epilogue_ms": stop_ms - packet_times[-1],
        "packets": len(capture.packets),
        "events": len(capture.events),
        "sources": len(
            {
                source_identity(event.zone, event.source)
                for event in capture.events
                if event.source is not None
            }
        ),
        "packet_rate_active_per_second": (
            1000.0 * len(capture.packets) / active_span_ms if active_span_ms > 0 else 0.0
        ),
        "event_rate_active_per_second": (
            1000.0 * len(capture.events) / active_span_ms if active_span_ms > 0 else 0.0
        ),
        "packet_interval_ms": describe(intervals),
        "packet_cardinality": describe(cardinals),
        "dispatch_span_ms": describe(dispatch_spans),
        "events_per_second": describe(events_per_second),
        "packets_per_second": describe(packets_per_second),
        "interval_le_5ms_fraction": (
            sum(value <= 5.0 for value in intervals) / len(intervals) if intervals else 0.0
        ),
        "interval_le_10ms_fraction": (
            sum(value <= 10.0 for value in intervals) / len(intervals) if intervals else 0.0
        ),
        "interval_ge_200ms_fraction": (
            sum(value >= 200.0 for value in intervals) / len(intervals) if intervals else 0.0
        ),
        "interval_ge_500ms_fraction": (
            sum(value >= 500.0 for value in intervals) / len(intervals) if intervals else 0.0
        ),
        "burst_packet_count": sum(value for value in bursts if value > 1),
        "burst_cluster_count": sum(value > 1 for value in bursts),
        "largest_5ms_burst_packets": max(bursts, default=0),
        "framing": dict(Counter(str(packet.get("framing")) for packet in capture.packets)),
        "immediate_bundle_fraction": (
            sum(bool((packet.get("timetag") or {}).get("immediate")) for packet in capture.packets)
            / len(capture.packets)
        ),
        "incomplete_packets": capture.end.get("incomplete_packets", 0),
        "parser_warnings": capture.end.get("parser_warnings", 0),
        "write_errors": capture.end.get("write_errors", 0),
    }
    return summary, {
        "packet_interval_ms": intervals,
        "events_per_second": [float(value) for value in events_per_second],
        "packets_per_second": [float(value) for value in packets_per_second],
    }


def source_speed_bins(
    capture: Capture,
    bin_ms: int = 100,
    minimum_interval_ms: int = 0,
    winsorise_p95: bool = False,
) -> dict[int, dict[int, float]]:
    sequences = motion_sequences(capture)
    raw: dict[int, list[tuple[int, float]]] = defaultdict(list)
    if not capture.pairs:
        return {}
    origin = min(pair.timestamp_ms for pair in capture.pairs)
    for source, pairs in sequences.items():
        for previous, current in zip(pairs, pairs[1:]):
            dt_ms = current.timestamp_ms - previous.timestamp_ms
            if dt_ms <= minimum_interval_ms or current.lifecycle_boundary:
                continue
            distance = math.hypot(current.u - previous.u, current.v - previous.v)
            speed = distance / (dt_ms / 1000.0)
            raw[source].append(((current.timestamp_ms - origin) // bin_ms, speed))

    result: dict[int, dict[int, list[float]]] = defaultdict(lambda: defaultdict(list))
    for source, samples in raw.items():
        ceiling = percentile([speed for _, speed in samples], 95) if winsorise_p95 else math.inf
        for index, speed in samples:
            result[source][index].append(min(speed, ceiling))
    return {
        source: {index: float(np.mean(values)) for index, values in bins.items()}
        for source, bins in result.items()
    }


MAX_CORRELATION_SOURCES = 32


def correlation_metrics(capture: Capture) -> tuple[dict[str, Any], list[int], list[list[float | None]]]:
    raw_bins = source_speed_bins(capture)
    # Sub-20-ms intervals are timestamp-compression artifacts in these captures,
    # not plausible 20 Hz hand motion. P95 winsorisation prevents one shared
    # scheduler spike from falsely making two independent users look cloned.
    bins = source_speed_bins(capture, minimum_interval_ms=20, winsorise_p95=True)
    all_sources = sorted(bins)
    # Pairwise lag searches are quadratic.  A deterministic, evenly spaced
    # sample is enough to detect cloned fleets without turning a 256-person
    # calibration run into an unbounded report-generation task.
    if len(all_sources) > MAX_CORRELATION_SOURCES:
        indexes = np.linspace(
            0, len(all_sources) - 1, MAX_CORRELATION_SOURCES, dtype=int
        )
        sources = [all_sources[int(index)] for index in indexes]
    else:
        sources = all_sources
    matrix: list[list[float | None]] = []
    correlations: list[float] = []
    max_lag_correlations: list[float] = []
    raw_correlations: list[float] = []
    for source_a in sources:
        row: list[float | None] = []
        for source_b in sources:
            if source_a == source_b:
                row.append(1.0)
                continue
            common = sorted(set(bins[source_a]) & set(bins[source_b]))
            value = pearson(
                [bins[source_a][index] for index in common],
                [bins[source_b][index] for index in common],
            )
            row.append(value)
            if source_a < source_b and value is not None:
                correlations.append(value)

                raw_common = sorted(set(raw_bins[source_a]) & set(raw_bins[source_b]))
                raw_value = pearson(
                    [raw_bins[source_a][index] for index in raw_common],
                    [raw_bins[source_b][index] for index in raw_common],
                )
                if raw_value is not None:
                    raw_correlations.append(raw_value)

                best: float | None = None
                for lag in range(-20, 21):
                    overlap = sorted(
                        index
                        for index in bins[source_a]
                        if index + lag in bins[source_b]
                    )
                    candidate = pearson(
                        [bins[source_a][index] for index in overlap],
                        [bins[source_b][index + lag] for index in overlap],
                    )
                    if candidate is not None and (
                        best is None or abs(candidate) > abs(best)
                    ):
                        best = candidate
                if best is not None:
                    max_lag_correlations.append(best)
        matrix.append(row)
    summary = {
        "available_source_count": len(all_sources),
        "evaluated_source_count": len(sources),
        "source_sample_limited": len(sources) < len(all_sources),
        "pair_count": len(correlations),
        "raw_zero_lag_absolute_correlation": describe(
            [abs(value) for value in raw_correlations]
        ),
        "zero_lag_correlation": describe(correlations),
        "zero_lag_absolute_correlation": describe([abs(value) for value in correlations]),
        "max_lag_2s_absolute_correlation": describe(
            [abs(value) for value in max_lag_correlations]
        ),
        "strong_zero_lag_pairs_fraction": (
            sum(abs(value) >= 0.8 for value in correlations) / len(correlations)
            if correlations
            else 0.0
        ),
    }
    return summary, sources, matrix


def schema_metrics(capture: Capture) -> dict[str, Any]:
    event_types = Counter(event.parameter or "invalid" for event in capture.events)
    zones = Counter(event.zone or "invalid" for event in capture.events)
    fingers = Counter(
        str(event.finger) if event.finger is not None else "invalid"
        for event in capture.events
    )
    invalid_addresses = sum(event.parameter is None for event in capture.events)
    missing_values = sum(
        event.value is None and event.parameter not in ("off", None)
        for event in capture.events
    )
    out_of_range_motion = sum(
        event.parameter in ("u", "v")
        and event.value is not None
        and not 0.0 <= event.value <= 1.0
        for event in capture.events
    )
    return {
        "format_versions": sorted(
            {
                row["format_version"]
                for row in [capture.start, capture.end, *capture.packets]
            }
        ),
        "event_types": dict(event_types),
        "zones": dict(zones),
        "fingers": dict(fingers),
        "invalid_addresses": invalid_addresses,
        "missing_values": missing_values,
        "out_of_range_motion": out_of_range_motion,
        "packet_sequence_contiguous": [
            packet["packet"] for packet in capture.packets
        ]
        == list(range(1, len(capture.packets) + 1)),
        "event_sequence_contiguous": [event.event for event in capture.events]
        == list(range(1, len(capture.events) + 1)),
    }


def analyse_capture(capture: Capture) -> tuple[dict[str, Any], dict[str, Any]]:
    schema = schema_metrics(capture)
    timing, timing_series = timing_metrics(capture)
    lifecycle, lifecycle_sources = source_lifecycle(capture)
    active_user_seconds = float(lifecycle["active_user_seconds"])
    timing["events_per_active_user_second"] = (
        len(capture.events) / active_user_seconds if active_user_seconds > 0.0 else 0.0
    )
    timing["motion_pairs_per_active_user_second"] = (
        len(capture.pairs) / active_user_seconds if active_user_seconds > 0.0 else 0.0
    )
    active_span_seconds = float(timing["active_packet_span_ms"]) / 1000.0
    lifecycle["mean_concurrent_sources"] = (
        active_user_seconds / active_span_seconds if active_span_seconds > 0.0 else 0.0
    )
    motion, motion_sources, motion_series = motion_metrics(capture)
    correlation, correlation_sources, correlation_matrix = correlation_metrics(capture)
    summary = {
        "label": capture.label,
        "path": capture.path.name,
        "schema": schema,
        "timing": timing,
        "lifecycle": lifecycle,
        "motion": motion,
        "correlation": correlation,
    }
    details = {
        "timing_series": timing_series,
        "lifecycle_sources": lifecycle_sources,
        "motion_sources": motion_sources,
        "motion_series": motion_series,
        "correlation_sources": correlation_sources,
        "correlation_matrix": correlation_matrix,
    }
    return summary, details


def default_font(size: int) -> ImageFont.ImageFont:
    candidates = [
        "/System/Library/Fonts/SFNSMono.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
    ]
    for candidate in candidates:
        try:
            return ImageFont.truetype(candidate, size)
        except OSError:
            pass
    return ImageFont.load_default()


def new_chart(title: str, width: int = 1500, height: int = 850) -> tuple[Image.Image, ImageDraw.ImageDraw]:
    image = Image.new("RGB", (width, height), PALETTE["background"])
    draw = ImageDraw.Draw(image)
    draw.text((40, 24), title, fill=PALETTE["text"], font=default_font(26))
    return image, draw


def chart_area(width: int, height: int) -> tuple[int, int, int, int]:
    # Reserve two compact legend rows.  Scaling reports contain seven series;
    # keeping the plot below y=130 prevents the second row from covering data.
    return (90, 130, width - 45, height - 80)


def draw_series_legend(
    draw: ImageDraw.ImageDraw,
    labels: Sequence[str],
    width: int,
) -> None:
    if not labels:
        return
    columns = min(4, len(labels))
    column_width = (width - 140) / columns
    font = default_font(16)
    for index, label in enumerate(labels):
        row, column = divmod(index, columns)
        x = int(90 + column * column_width)
        y = 58 + row * 27
        colour = SERIES_COLOURS[index % len(SERIES_COLOURS)]
        draw.line((x, y + 8, x + 22, y + 8), fill=colour, width=4)
        draw.text((x + 30, y), label, fill=colour, font=font)


def draw_axes(
    draw: ImageDraw.ImageDraw,
    bounds: tuple[int, int, int, int],
    x_label: str,
    y_label: str,
    x_range: tuple[float, float],
    y_range: tuple[float, float],
) -> None:
    left, top, right, bottom = bounds
    draw.rectangle(bounds, outline=PALETTE["grid"], width=1)
    font = default_font(15)
    for index in range(6):
        fraction = index / 5
        x = int(left + (right - left) * fraction)
        y = int(bottom - (bottom - top) * fraction)
        draw.line((x, top, x, bottom), fill=PALETTE["grid"], width=1)
        draw.line((left, y, right, y), fill=PALETTE["grid"], width=1)
        x_value = x_range[0] + (x_range[1] - x_range[0]) * fraction
        y_value = y_range[0] + (y_range[1] - y_range[0]) * fraction
        draw.text((x - 20, bottom + 8), f"{x_value:.1f}", fill=PALETTE["muted"], font=font)
        y_text = f"{y_value:.2f}" if max(abs(y_range[0]), abs(y_range[1])) < 1.0 else f"{y_value:.1f}"
        draw.text((8, y - 8), y_text, fill=PALETTE["muted"], font=font)
    draw.text(((left + right) // 2 - 50, bottom + 36), x_label, fill=PALETTE["muted"], font=font)
    draw.text((10, top - 26), y_label, fill=PALETTE["muted"], font=font)


def map_point(
    x: float,
    y: float,
    bounds: tuple[int, int, int, int],
    x_range: tuple[float, float],
    y_range: tuple[float, float],
) -> tuple[int, int]:
    left, top, right, bottom = bounds
    x_span = max(x_range[1] - x_range[0], 1.0e-12)
    y_span = max(y_range[1] - y_range[0], 1.0e-12)
    px = left + (x - x_range[0]) / x_span * (right - left)
    py = bottom - (y - y_range[0]) / y_span * (bottom - top)
    return int(px), int(py)


def save_rate_chart(captures: list[Capture], output: Path) -> None:
    image, draw = new_chart("OSC event rate — aligned to first received packet")
    bounds = chart_area(*image.size)
    series = []
    for capture in captures:
        seconds, rates = rate_series(capture, True)
        series.append((capture.label, seconds, rates))
    x_max = max((max(seconds, default=0) for _, seconds, _ in series), default=1)
    y_max = max((max(rates, default=0) for _, _, rates in series), default=1)
    draw_axes(draw, bounds, "seconds", "events / second", (0, x_max), (0, y_max))
    draw_series_legend(draw, [capture.label for capture in captures], image.width)
    for index, (label, seconds, rates) in enumerate(series):
        colour = SERIES_COLOURS[index % len(SERIES_COLOURS)]
        points = [map_point(x, y, bounds, (0, x_max), (0, y_max)) for x, y in zip(seconds, rates)]
        if len(points) >= 2:
            draw.line(points, fill=colour, width=3)
    image.save(output)


def histogram(values: Sequence[float], bins: np.ndarray) -> np.ndarray:
    if not values:
        return np.zeros(len(bins) - 1)
    counts, _ = np.histogram(np.asarray(values, dtype=float), bins=bins)
    total = max(1, int(np.sum(counts)))
    return counts / total


def save_histogram_chart(
    title: str,
    captures: list[Capture],
    values_by_label: dict[str, list[float]],
    bins: np.ndarray,
    x_label: str,
    output: Path,
) -> None:
    image, draw = new_chart(title)
    bounds = chart_area(*image.size)
    histograms = [histogram(values_by_label.get(capture.label, []), bins) for capture in captures]
    y_max = max((float(np.max(values)) for values in histograms if len(values)), default=1.0)
    draw_axes(draw, bounds, x_label, "fraction", (float(bins[0]), float(bins[-1])), (0.0, y_max))
    draw_series_legend(draw, [capture.label for capture in captures], image.width)
    centres = (bins[:-1] + bins[1:]) / 2
    for index, (capture, values) in enumerate(zip(captures, histograms)):
        colour = SERIES_COLOURS[index % len(SERIES_COLOURS)]
        points = [
            map_point(float(x), float(y), bounds, (float(bins[0]), float(bins[-1])), (0.0, y_max))
            for x, y in zip(centres, values)
        ]
        if len(points) >= 2:
            draw.line(points, fill=colour, width=3)
    image.save(output)


def save_trajectory_chart(captures: list[Capture], output: Path) -> None:
    width, height = 1500, 1050
    image, draw = new_chart("U/V trajectories — every captured coordinate pair", width, height)
    panel_width = (width - 120) // len(captures)
    font = default_font(16)
    for index, capture in enumerate(captures):
        left = 40 + index * panel_width
        bounds = (left + 45, 115, left + panel_width - 25, height - 90)
        draw.rectangle(bounds, fill=PALETTE["panel"], outline=PALETTE["grid"])
        draw.text(
            (left + 45, 75),
            capture.label,
            fill=SERIES_COLOURS[index % len(SERIES_COLOURS)],
            font=font,
        )
        sequences = motion_sequences(capture)
        for source_index, (source, pairs) in enumerate(sorted(sequences.items())):
            hue_colour = SERIES_COLOURS[source_index % len(SERIES_COLOURS)]
            points = [map_point(pair.u, pair.v, bounds, (0, 1), (0, 1)) for pair in pairs]
            if len(points) >= 2:
                draw.line(points, fill=hue_colour, width=1)
            if points:
                draw.ellipse((points[0][0] - 3, points[0][1] - 3, points[0][0] + 3, points[0][1] + 3), fill=PALETTE["green"])
        draw.text((bounds[0], bounds[3] + 18), "U 0 → 1", fill=PALETTE["muted"], font=font)
        draw.text((bounds[0], bounds[1] - 24), "V", fill=PALETTE["muted"], font=font)
    image.save(output)


def save_correlation_chart(
    capture: Capture,
    sources: list[int],
    matrix: list[list[float | None]],
    output: Path,
) -> None:
    # Long Turkish/capture labels need more room than the small 10-source
    # matrix itself. A 960 px floor keeps the title inside the canvas.
    size = max(960, 100 + len(sources) * 70)
    image, draw = new_chart(
        f"{capture.label} — robust pairwise 100 ms speed correlation",
        size,
        size,
    )
    left, top = 100, 100
    cell = max(36, min(70, (size - 160) // max(1, len(sources))))
    font = default_font(14)
    for row, source_a in enumerate(sources):
        label = source_identity_label(source_a)
        draw.text((35, top + row * cell + 8), label, fill=PALETTE["muted"], font=font)
        draw.text((left + row * cell + 8, 72), label, fill=PALETTE["muted"], font=font)
        for column, _source_b in enumerate(sources):
            value = matrix[row][column]
            if value is None:
                colour = PALETTE["panel"]
                label = "—"
            else:
                strength = min(1.0, abs(value))
                positive = value >= 0.0
                base = PALETTE["cyan"] if positive else PALETTE["red"]
                colour = tuple(int(PALETTE["panel"][i] * (1 - strength) + base[i] * strength) for i in range(3))
                label = f"{value:+.2f}"
            box = (left + column * cell, top + row * cell, left + (column + 1) * cell - 2, top + (row + 1) * cell - 2)
            draw.rectangle(box, fill=colour)
            if cell >= 52:
                draw.text((box[0] + 5, box[1] + 10), label, fill=PALETTE["text"], font=font)
    image.save(output)


def flatten_summary(summary: dict[str, Any], prefix: str = "") -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in summary.items():
        name = f"{prefix}.{key}" if prefix else key
        if isinstance(value, dict):
            result.update(flatten_summary(value, name))
        elif isinstance(value, (list, tuple)):
            result[name] = json.dumps(value, ensure_ascii=False)
        else:
            result[name] = value
    return result


def write_summary_csv(summaries: list[dict[str, Any]], output: Path) -> None:
    flattened = {summary["label"]: flatten_summary(summary) for summary in summaries}
    metrics = sorted({metric for values in flattened.values() for metric in values})
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["metric", *flattened.keys()])
        for metric in metrics:
            writer.writerow([metric, *(flattened[label].get(metric, "") for label in flattened)])


def write_per_source_csv(
    summaries: list[dict[str, Any]],
    details: dict[str, dict[str, Any]],
    output: Path,
) -> None:
    rows: list[dict[str, Any]] = []
    for summary in summaries:
        label = summary["label"]
        source_ids = sorted(
            set(details[label]["motion_sources"])
            | set(details[label]["lifecycle_sources"])
        )
        for source in source_ids:
            row = {"capture": label, "source": source_identity_label(source)}
            motion = details[label]["motion_sources"].get(source, {})
            lifecycle = details[label]["lifecycle_sources"].get(source, {})
            row.update(flatten_summary(motion, "motion"))
            row.update(flatten_summary(lifecycle, "lifecycle"))
            rows.append(row)
    fields = ["capture", "source", *sorted({key for row in rows for key in row if key not in ("capture", "source")})]
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def write_rate_csv(captures: list[Capture], output: Path) -> None:
    rows: list[dict[str, Any]] = []
    for capture in captures:
        event_seconds, event_rates = rate_series(capture, True)
        packet_seconds, packet_rates = rate_series(capture, False)
        event_by_second = dict(zip(event_seconds, event_rates))
        packet_by_second = dict(zip(packet_seconds, packet_rates))
        for second in sorted(set(event_by_second) | set(packet_by_second)):
            rows.append(
                {
                    "capture": capture.label,
                    "second": second,
                    "events": event_by_second.get(second, 0),
                    "packets": packet_by_second.get(second, 0),
                }
            )
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=["capture", "second", "events", "packets"])
        writer.writeheader()
        writer.writerows(rows)


def write_comparison_csv(
    reference_label: str,
    summaries: list[dict[str, Any]],
    details: dict[str, dict[str, Any]],
    output: Path,
) -> None:
    distributions = [
        "packet_interval_ms",
        "continuous_interval_ms",
        "continuous_step_distance",
        "continuous_speed_per_second",
        "absolute_acceleration_per_second2",
        "turn_degrees",
    ]
    reference = details[reference_label]
    rows: list[dict[str, Any]] = []
    for summary in summaries:
        label = summary["label"]
        if label == reference_label:
            continue
        for distribution in distributions:
            if distribution in reference["timing_series"]:
                ref_values = reference["timing_series"][distribution]
                candidate_values = details[label]["timing_series"].get(distribution, [])
            else:
                ref_values = reference["motion_series"].get(distribution, [])
                candidate_values = details[label]["motion_series"].get(distribution, [])
            distances = distribution_distances(ref_values, candidate_values)
            rows.append(
                {
                    "reference": reference_label,
                    "candidate": label,
                    "distribution": distribution,
                    **distances,
                }
            )
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=["reference", "candidate", "distribution", "ks", "quantile_mae", "normalised_quantile_mae"],
        )
        writer.writeheader()
        writer.writerows(rows)


def run_self_test() -> None:
    """Exercise motion boundaries, cleanup censoring, and strict schema guards."""
    start_ms = 1_000_000
    rows: list[dict[str, Any]] = [
        {
            "kind": "session_start",
            "format_version": 1,
            "started_unix_ms": start_ms,
        }
    ]
    next_event = 1

    def append_packet(
        elapsed_ms: int,
        values: Sequence[tuple[str, float | int]],
        *,
        bundle: bool = True,
        completion_reason: str | None = None,
    ) -> None:
        nonlocal next_event
        timestamp_ms = start_ms + elapsed_ms
        events: list[dict[str, Any]] = []
        for parameter, value in values:
            inferred_type = (
                "float32_by_contract" if parameter in ("u", "v")
                else "int32_by_contract"
            )
            events.append(
                {
                    "event": next_event,
                    "received_unix_ms": timestamp_ms,
                    "elapsed_ms": elapsed_ms,
                    "packet_offset_ms": 0,
                    "address": f"/cs/A/7/finger0/{parameter}",
                    "args": [value],
                    "arg_types_inferred": [inferred_type],
                }
            )
            next_event += 1
        packet = {
            "kind": "osc_packet",
            "format_version": 1,
            "packet": len(rows),
            "received_unix_ms": timestamp_ms,
            "elapsed_ms": elapsed_ms,
            "framing": (
                "cnmat_bundle" if bundle
                else "plain_osc_or_direct_max_message"
            ),
            "timetag": {"immediate": True} if bundle else None,
            "events": events,
            "complete": True,
            "completed_unix_ms": timestamp_ms,
            "dispatch_span_ms": 0,
        }
        if completion_reason is not None:
            packet["completion_reason"] = completion_reason
        rows.append(packet)

    append_packet(0, (("u", 0.10), ("v", 0.20), ("on", 1)))
    append_packet(50, (("u", 0.20), ("v", 0.20)))
    append_packet(100, (("on", 0),), bundle=False)
    # A delayed aggregate pair arrives while inactive, shortly before the next
    # canonical U/V + On attack. Neither transition is physical movement.
    append_packet(10_000, (("u", 0.80), ("v", 0.80)))
    append_packet(10_010, (("u", 0.81), ("v", 0.80), ("on", 1)))
    append_packet(10_060, (("u", 0.82), ("v", 0.80)))
    append_packet(10_110, (("u", 0.83), ("v", 0.80)))
    append_packet(
        10_120,
        (("on", 0),),
        bundle=False,
        completion_reason="simulator_cleanup",
    )
    rows.append(
        {
            "kind": "session_end",
            "format_version": 1,
            "stopped_unix_ms": start_ms + 10_121,
            "duration_ms": 10_121,
            "packets": 8,
            "events": next_event - 1,
            "incomplete_packets": 0,
            "parser_warnings": 0,
            "write_errors": 0,
            "events_dropped_during_fault": 0,
        }
    )

    def write_rows(path: Path) -> None:
        with path.open("w", encoding="utf-8") as handle:
            for row in rows:
                handle.write(json.dumps(row, separators=(",", ":")) + "\n")

    with tempfile.TemporaryDirectory(prefix="cosmic-analyzer-self-test-") as temp:
        capture_path = Path(temp) / "inactive-boundary.jsonl"
        write_rows(capture_path)
        capture = load_capture("self-test", capture_path)
        summary, details = analyse_capture(capture)
        intervals = details["motion_series"]["continuous_interval_ms"]
        boundaries = [pair.lifecycle_boundary for pair in capture.pairs]
        if intervals != [50.0, 50.0, 50.0]:
            raise AssertionError(f"inactive U/V leaked into motion: {intervals!r}")
        if boundaries != [True, False, True, True, False, False]:
            raise AssertionError(f"unexpected lifecycle boundaries: {boundaries!r}")
        if summary["motion"]["continuous_interval_ms"]["max"] != 50.0:
            raise AssertionError("stale inactive U/V created a long interval")
        lifecycle = summary["lifecycle"]
        if not (
            summary["timing"]["events"] == next_event - 1
            and lifecycle["onsets"] == 2
            and lifecycle["releases"] == 1
            and lifecycle["cleanup_off_events"] == 1
            and lifecycle["right_censored_gestures"] == 1
            and lifecycle["active_at_end"] == 1
            and lifecycle["gesture_ms"]["count"] == 1
            and lifecycle["gesture_ms"]["min"] == 100.0
        ):
            raise AssertionError(
                "simulator cleanup was not counted as traffic and right-censored "
                f"as lifecycle: {lifecycle!r}"
            )

        hostile_path = Path(temp) / "hostile-source.jsonl"
        first_address = rows[1]["events"][0]["address"]
        rows[1]["events"][0]["address"] = "/cs/A/256/finger0/u"
        write_rows(hostile_path)
        rows[1]["events"][0]["address"] = first_address
        try:
            load_capture("hostile", hostile_path)
        except ValueError:
            pass
        else:
            raise AssertionError("strict source range accepted source 256")

    print(json.dumps({"self_test": "ok", "continuous_intervals_ms": intervals}))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--capture",
        nargs=2,
        action="append",
        metavar=("LABEL", "PATH"),
        help="Repeat for every capture; the first capture is the reference.",
    )
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument(
        "--skip-charts",
        action="store_true",
        help="Write JSON/CSV metrics only (useful for large scalability sweeps).",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="Run the built-in lifecycle-boundary and strict-schema regression.",
    )
    args = parser.parse_args()
    if not args.self_test and (not args.capture or args.output_dir is None):
        parser.error("--capture and --output-dir are required unless --self-test is used")
    return args


def main() -> int:
    args = parse_args()
    if args.self_test:
        run_self_test()
        return 0

    output_dir: Path = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    captures = [load_capture(label, Path(path)) for label, path in args.capture]
    summaries: list[dict[str, Any]] = []
    details: dict[str, dict[str, Any]] = {}
    for capture in captures:
        summary, detail = analyse_capture(capture)
        summaries.append(summary)
        details[capture.label] = detail

    with (output_dir / "summary.json").open("w", encoding="utf-8") as handle:
        json.dump({"captures": summaries}, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    write_summary_csv(summaries, output_dir / "summary.csv")
    write_per_source_csv(summaries, details, output_dir / "per_source.csv")
    write_rate_csv(captures, output_dir / "event_rate_per_second.csv")
    write_comparison_csv(captures[0].label, summaries, details, output_dir / "comparison_to_reference.csv")

    if args.skip_charts:
        print(json.dumps({"output_dir": str(output_dir), "captures": summaries}, ensure_ascii=False, indent=2))
        return 0

    save_rate_chart(captures, output_dir / "01_event_rate.png")
    interval_values = {
        capture.label: details[capture.label]["motion_series"].get("continuous_interval_ms", [])
        for capture in captures
    }
    save_histogram_chart(
        "Per-source continuous U/V update intervals",
        captures,
        interval_values,
        np.linspace(0.0, 500.0, 101),
        "milliseconds (values above 500 ms excluded from plot)",
        output_dir / "02_motion_interval_distribution.png",
    )
    step_values = {
        capture.label: details[capture.label]["motion_series"].get("continuous_step_distance", [])
        for capture in captures
    }
    save_histogram_chart(
        "Continuous U/V step distance",
        captures,
        step_values,
        np.linspace(0.0, 0.4, 101),
        "normalised U/V distance (values above 0.4 excluded)",
        output_dir / "03_motion_step_distribution.png",
    )
    speed_values = {
        capture.label: details[capture.label]["motion_series"].get("continuous_speed_per_second", [])
        for capture in captures
    }
    save_histogram_chart(
        "Continuous U/V speed",
        captures,
        speed_values,
        np.linspace(0.0, 5.0, 101),
        "normalised distance / second (values above 5 excluded)",
        output_dir / "04_motion_speed_distribution.png",
    )
    save_trajectory_chart(captures, output_dir / "05_uv_trajectories.png")

    most_sources_capture = max(captures, key=lambda capture: len(motion_sequences(capture)))
    sources = details[most_sources_capture.label]["correlation_sources"]
    matrix = details[most_sources_capture.label]["correlation_matrix"]
    save_correlation_chart(
        most_sources_capture,
        sources,
        matrix,
        output_dir / "06_multiuser_velocity_correlation.png",
    )

    print(json.dumps({"output_dir": str(output_dir), "captures": summaries}, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
