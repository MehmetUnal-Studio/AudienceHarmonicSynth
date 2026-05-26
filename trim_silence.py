#!/usr/bin/env python3
"""
trim_silence.py — physically trim trailing silence from .wav files.

Mirrors the auto-trim behavior built into the plugin's SampleLibrary,
but writes the result back to disk so the library is smaller and the
plugin doesn't have to do this at load time.

Usage:
    python3 trim_silence.py "<folder>"

For each .wav in the folder:
    1. find last sample with |amplitude| > threshold (≈ -56 dB)
    2. keep that point + a 60 ms tail
    3. apply a 25 ms cosine-like fade-out at the new end
    4. overwrite the file (originals are first copied to <name>.orig.wav so you
       can recover them — these are pruned from the library so the plugin
       doesn't see them)
"""

import argparse
import array
import math
import os
import shutil
import sys
import wave

THRESHOLD_DB     = -56.0           # silence floor
TAIL_MS          = 60.0            # keep this many ms after the last audible sample
FADE_MS          = 25.0            # cosine fade-out length

BACKUP_SUFFIX    = ".orig.wav"


def amp_threshold_for(sampwidth: int) -> int:
    full_scale = (1 << (sampwidth * 8 - 1))   # 32768 for 16-bit etc.
    return int(full_scale * (10 ** (THRESHOLD_DB / 20.0)))


def unpack_samples(data: bytes, sampwidth: int):
    """Return a mutable int list of all interleaved samples (any nch)."""
    if sampwidth == 2:
        a = array.array('h')
        a.frombytes(data)
        return a, 16
    if sampwidth == 3:
        # 24-bit PCM little-endian, signed
        n = len(data) // 3
        out = array.array('i', [0] * n)
        for i in range(n):
            b0 = data[i * 3]
            b1 = data[i * 3 + 1]
            b2 = data[i * 3 + 2]
            v = b0 | (b1 << 8) | (b2 << 16)
            if v & 0x800000:           # sign extend
                v -= 1 << 24
            out[i] = v
        return out, 24
    if sampwidth == 4:
        a = array.array('i')
        a.frombytes(data)
        return a, 32
    if sampwidth == 1:
        # 8-bit PCM is unsigned in WAV
        a = array.array('B')
        a.frombytes(data)
        out = array.array('h', [int(x) - 128 for x in a])
        return out, 8
    raise NotImplementedError(f"unsupported sampwidth: {sampwidth}")


def pack_samples(samples, sampwidth: int) -> bytes:
    if sampwidth == 2:
        return array.array('h', samples).tobytes()
    if sampwidth == 3:
        out = bytearray(len(samples) * 3)
        for i, v in enumerate(samples):
            if v < 0:
                v += 1 << 24
            out[i * 3]     = v & 0xff
            out[i * 3 + 1] = (v >> 8) & 0xff
            out[i * 3 + 2] = (v >> 16) & 0xff
        return bytes(out)
    if sampwidth == 4:
        return array.array('i', samples).tobytes()
    if sampwidth == 1:
        return bytes(int(s + 128) & 0xff for s in samples)
    raise NotImplementedError(f"unsupported sampwidth: {sampwidth}")


def trim_file(path: str, dry_run: bool = False):
    with wave.open(path, 'rb') as wf:
        nch       = wf.getnchannels()
        sw        = wf.getsampwidth()
        sr        = wf.getframerate()
        n_frames  = wf.getnframes()
        raw       = wf.readframes(n_frames)

    samples, _ = unpack_samples(raw, sw)
    thr = amp_threshold_for(sw)

    # Walk backwards through frames, taking peak across channels per frame.
    last_audible = 0
    for f in range(n_frames - 1, -1, -1):
        peak = 0
        base = f * nch
        for ch in range(nch):
            v = samples[base + ch]
            if v < 0: v = -v
            if v > peak: peak = v
        if peak > thr:
            last_audible = f
            break

    tail_frames = int(TAIL_MS * 0.001 * sr)
    new_n = min(n_frames, last_audible + tail_frames)

    # Cosine fade-out at the tail
    fade_frames = int(FADE_MS * 0.001 * sr)
    if new_n > fade_frames + 32:
        start = new_n - fade_frames
        for i in range(fade_frames):
            # cosine taper from 1.0 → 0.0
            g = 0.5 * (1.0 + math.cos(math.pi * (i / fade_frames)))
            base = (start + i) * nch
            for ch in range(nch):
                samples[base + ch] = int(samples[base + ch] * g)

    truncated = samples[: new_n * nch]
    saved_ms  = (n_frames - new_n) * 1000.0 / sr

    name = os.path.basename(path)
    print(f"  {name:<14}  {n_frames:>8} → {new_n:>8} frames  ({saved_ms:>6.0f} ms cut)")

    if dry_run:
        return

    # Back up the original once.
    backup = path + ".orig"
    if not os.path.exists(backup):
        shutil.copy2(path, backup)

    with wave.open(path, 'wb') as wf:
        wf.setnchannels(nch)
        wf.setsampwidth(sw)
        wf.setframerate(sr)
        wf.writeframes(pack_samples(truncated, sw))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("folder", help="folder containing .wav files")
    ap.add_argument("--dry-run", action="store_true",
                    help="only report what would change")
    args = ap.parse_args()

    folder = args.folder
    if not os.path.isdir(folder):
        print(f"not a directory: {folder}", file=sys.stderr)
        sys.exit(1)

    wavs = sorted(f for f in os.listdir(folder)
                  if f.lower().endswith(".wav") and not f.startswith("."))
    print(f"trimming {len(wavs)} files in {folder}\n")
    for w in wavs:
        path = os.path.join(folder, w)
        try:
            trim_file(path, dry_run=args.dry_run)
        except Exception as e:
            print(f"  !! {w}: {e}", file=sys.stderr)


if __name__ == "__main__":
    main()
