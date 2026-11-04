#!/usr/bin/env python3
"""Generates the four alert sounds of the robot as OGG Opus files.

  gen_sons.py OUT_DIR

Writes lembrete.ogg, sem_internet.ogg, conectado.ogg and parada.ogg, the names
behavior/sound_bank looks for in noise/sons/ on the card. 16 kHz mono, 60 ms
Opus frames, well under the 64 KB limit. Needs ffmpeg with libopus.
"""
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

import numpy as np

RATE = 16000
PEAK = 0.55  # the little speaker distorts well before full scale


def note(freq, length, partials, decay, attack=0.008):
    """One note: a sum of decaying partials with a short attack."""
    t = np.arange(int(RATE * length)) / RATE
    wave_ = np.zeros_like(t)
    for ratio, level, fall in partials:
        wave_ += level * np.sin(2 * np.pi * freq * ratio * t) * np.exp(-t * decay * fall)
    envelope = np.minimum(1.0, t / attack) * np.minimum(1.0, (length - t) / 0.02)
    return wave_ * envelope


def place(parts, total):
    """Mixes (start_seconds, samples) parts into one buffer."""
    out = np.zeros(int(RATE * total))
    for start, samples in parts:
        i = int(RATE * start)
        out[i:i + len(samples)] += samples[: len(out) - i]
    return out


BELL = [(1.0, 1.0, 1.0), (2.0, 0.35, 1.6), (3.0, 0.15, 2.4)]
SOFT = [(1.0, 1.0, 1.0), (2.0, 0.12, 1.5)]
BUZZ = [(1.0, 1.0, 0.0), (3.0, 0.22, 0.0), (5.0, 0.08, 0.0)]  # odd partials: firm, not shrill


def lembrete():
    # Three rising bell notes (E5 G5 C6), the last one left to ring: friendly, hard to miss.
    return place([(0.00, note(659.3, 0.55, BELL, 5.0)),
                  (0.20, note(784.0, 0.55, BELL, 5.0)),
                  (0.40, note(1046.5, 1.0, BELL, 3.5))], 1.45)


def sem_internet():
    # Two soft falling notes (G5 then D5): something went away, no alarm.
    return place([(0.00, note(784.0, 0.45, SOFT, 4.0)),
                  (0.28, note(587.3, 0.70, SOFT, 3.5))], 1.0)


def conectado():
    # Two quick rising notes (D5 then A5): it is back.
    return place([(0.00, note(587.3, 0.30, SOFT, 6.0)),
                  (0.14, note(880.0, 0.50, SOFT, 5.0))], 0.7)


def parada():
    # Two short firm low beeps (A4 then E4), flat and steady: stop, attention.
    return place([(0.00, note(440.0, 0.20, BUZZ, 0.0, attack=0.01)),
                  (0.30, note(329.6, 0.28, BUZZ, 0.0, attack=0.01))], 0.7)


def write(name, samples, out_dir):
    samples = samples / (np.max(np.abs(samples)) or 1.0) * PEAK
    with tempfile.TemporaryDirectory() as tmp:
        wav = Path(tmp) / (name + ".wav")
        with wave.open(str(wav), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(RATE)
            w.writeframes((samples * 32767).astype("<i2").tobytes())
        target = out_dir / (name + ".ogg")
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(wav), "-c:a", "libopus",
                        "-ar", str(RATE), "-ac", "1", "-b:a", "24k", "-frame_duration", "60",
                        str(target)], check=True)
    size = target.stat().st_size
    assert size < 64 * 1024 and target.read_bytes()[:4] == b"OggS", name
    print(f"{target}: {size} bytes")


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out_dir = Path(sys.argv[1])
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, make in (("lembrete", lembrete), ("sem_internet", sem_internet),
                       ("conectado", conectado), ("parada", parada)):
        write(name, make(), out_dir)


if __name__ == "__main__":
    main()
