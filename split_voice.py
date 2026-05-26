#!/usr/bin/env python3
"""
split_voice.py — slice a single multi-note vocal recording into per-note .wav
files using simple RMS-onset detection.

Usage:
    python3 split_voice.py <input.wav> <output_folder> \
        <NOTE_LIST...>

NOTE_LIST is a space-separated list of note names matching the audio order,
e.g. C1 D1 E1 F1 G1 A1 B1  C2 D2 E2 F2 G2 A2 B2  C3 D3 E3 F3 G3 A3 B3  C4

The script:
  1. computes a windowed RMS envelope (10 ms hop)
  2. detects onsets where RMS crosses a rising threshold
  3. enforces a minimum spacing so we don't grab tail bumps
  4. picks the N best onsets (where N = len(NOTE_LIST))
  5. writes each segment to <output>/<note>.wav (24-bit preserved)
  6. applies the same auto-trim used elsewhere in the project
"""

import argparse
import array
import math
import os
import struct
import sys
import wave

WINDOW_MS         = 10.0
DEFAULT_MIN_SPACING_MS = 1100.0
DEFAULT_THRESHOLD_REL  = 0.06    # onset must rise above this fraction of overall peak

TAIL_MS           = 60.0
FADE_MS           = 25.0
TRIM_DB           = -56.0


def unpack(data: bytes, sampwidth: int):
    if sampwidth == 2:
        a = array.array('h'); a.frombytes(data); return a
    if sampwidth == 3:
        n = len(data) // 3
        out = array.array('i', [0] * n)
        for i in range(n):
            v = data[i * 3] | (data[i * 3 + 1] << 8) | (data[i * 3 + 2] << 16)
            if v & 0x800000: v -= 1 << 24
            out[i] = v
        return out
    if sampwidth == 4:
        a = array.array('i'); a.frombytes(data); return a
    raise NotImplementedError(f"sw={sampwidth}")


def pack(samples, sampwidth: int) -> bytes:
    if sampwidth == 2:
        return array.array('h', samples).tobytes()
    if sampwidth == 3:
        out = bytearray(len(samples) * 3)
        for i, v in enumerate(samples):
            if v < 0: v += 1 << 24
            out[i * 3]     = v & 0xff
            out[i * 3 + 1] = (v >> 8) & 0xff
            out[i * 3 + 2] = (v >> 16) & 0xff
        return bytes(out)
    if sampwidth == 4:
        return array.array('i', samples).tobytes()
    raise NotImplementedError(f"sw={sampwidth}")


def detect_onsets(samples, nch: int, sr: int, sampwidth: int, n_notes: int,
                  min_spacing_ms: float = DEFAULT_MIN_SPACING_MS,
                  threshold_rel: float = DEFAULT_THRESHOLD_REL):
    full_scale = 1 << (sampwidth * 8 - 1)
    win = int(WINDOW_MS * 0.001 * sr)
    n_frames = len(samples) // nch
    n_windows = n_frames // win

    # frame-wise mono RMS, normalized 0..1
    rms = [0.0] * n_windows
    overall = 0.0
    for w in range(n_windows):
        s = 0.0
        for i in range(win):
            base = (w * win + i) * nch
            for ch in range(nch):
                v = samples[base + ch] / full_scale
                s += v * v
        rms[w] = math.sqrt(s / (win * nch))
        if rms[w] > overall: overall = rms[w]

    if overall <= 0:
        return []

    norm = [r / overall for r in rms]

    threshold = threshold_rel
    min_spacing = int(min_spacing_ms / WINDOW_MS)

    # candidate onsets — points where envelope rises crossing the threshold
    candidates = []
    armed = True
    for w in range(1, n_windows):
        if armed and norm[w] > threshold and norm[w - 1] <= threshold:
            candidates.append((w, norm[w]))
            armed = False
        if (not armed) and norm[w] < threshold * 0.4:
            armed = True

    # enforce minimum spacing — keep first if too close
    pruned = []
    last = -10 ** 9
    for w, v in candidates:
        if w - last < min_spacing: continue
        pruned.append((w, v))
        last = w

    # if too few onsets, also accept any local peak above 0.30 of overall
    if len(pruned) < n_notes:
        for w in range(2, n_windows - 2):
            if (norm[w] > 0.30
                    and norm[w] > norm[w - 1] and norm[w] > norm[w + 1]):
                if all(abs(w - p) >= min_spacing for p, _ in pruned):
                    pruned.append((w, norm[w]))
        pruned.sort()

    # if too many, keep the strongest by peak
    if len(pruned) > n_notes:
        pruned.sort(key=lambda t: t[1], reverse=True)
        pruned = pruned[:n_notes]
        pruned.sort()

    # convert window indices to sample-frame indices
    return [w * win for w, _ in pruned]


def auto_trim_tail(samples, nch: int, sr: int, sampwidth: int):
    full_scale = 1 << (sampwidth * 8 - 1)
    thr = int(full_scale * (10 ** (TRIM_DB / 20.0)))
    n_frames = len(samples) // nch

    last_aud = 0
    for f in range(n_frames - 1, -1, -1):
        peak = 0
        base = f * nch
        for ch in range(nch):
            v = samples[base + ch]
            if v < 0: v = -v
            if v > peak: peak = v
        if peak > thr:
            last_aud = f; break

    tail = int(TAIL_MS * 0.001 * sr)
    new_n = min(n_frames, last_aud + tail)

    fade = int(FADE_MS * 0.001 * sr)
    if new_n > fade + 32:
        start = new_n - fade
        for i in range(fade):
            g = 0.5 * (1.0 + math.cos(math.pi * (i / fade)))
            base = (start + i) * nch
            for ch in range(nch):
                samples[base + ch] = int(samples[base + ch] * g)

    return samples[: new_n * nch]


def write_wav(path: str, samples, nch: int, sr: int, sampwidth: int):
    with wave.open(path, 'wb') as wf:
        wf.setnchannels(nch)
        wf.setsampwidth(sampwidth)
        wf.setframerate(sr)
        wf.writeframes(pack(samples, sampwidth))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("outdir")
    ap.add_argument("notes", nargs="+", help="note names in order, e.g. C1 D1 E1 ...")
    ap.add_argument("--min-spacing-ms", type=float, default=DEFAULT_MIN_SPACING_MS,
                    help="minimum gap between detected onsets")
    ap.add_argument("--threshold", type=float, default=DEFAULT_THRESHOLD_REL,
                    help="onset detection threshold (fraction of overall peak)")
    args = ap.parse_args()

    if not os.path.isdir(args.outdir):
        os.makedirs(args.outdir, exist_ok=True)

    with wave.open(args.input, 'rb') as wf:
        nch       = wf.getnchannels()
        sw        = wf.getsampwidth()
        sr        = wf.getframerate()
        n_frames  = wf.getnframes()
        raw       = wf.readframes(n_frames)

    print(f"input: {nch}ch · {sw*8}-bit · {sr} Hz · {n_frames/sr:.2f} s")
    samples = unpack(raw, sw)

    onsets = detect_onsets(samples, nch, sr, sw, len(args.notes),
                            min_spacing_ms=args.min_spacing_ms,
                            threshold_rel=args.threshold)
    if len(onsets) < len(args.notes):
        print(f"!! only found {len(onsets)} onsets, expected {len(args.notes)}",
              file=sys.stderr)
        sys.exit(2)

    # append end marker so we can slice up to it
    onsets.append(n_frames)
    for i, note in enumerate(args.notes):
        start = onsets[i]
        end   = onsets[i + 1]
        seg   = list(samples[start * nch : end * nch])
        seg   = auto_trim_tail(seg, nch, sr, sw)
        out   = os.path.join(args.outdir, f"{note}.wav")
        write_wav(out, seg, nch, sr, sw)
        dur_ms = len(seg) * 1000.0 / (nch * sr)
        print(f"  {note:<3} → {os.path.basename(out)}  ({dur_ms:>6.0f} ms)  "
              f"from frame {start} ({start/sr:5.2f}s)")


if __name__ == "__main__":
    main()
