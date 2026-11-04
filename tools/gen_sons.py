#!/usr/bin/env python3
"""Generates the four alert sounds of the robot as OGG Opus files.

  gen_sons.py OUT_DIR

Writes lembrete.ogg, sem_internet.ogg, conectado.ogg and parada.ogg, the names
behavior/sound_bank looks for in noise/sons/ on the card. 16 kHz mono, 60 ms
Opus frames, well under the 64 KB limit. Needs ffmpeg with libopus and numpy.

The voices are FM bells and mallets (a decaying modulator gives the strike a
bright start that settles into a clean tone), doubled with a slightly
detuned copy for width, then a short synthetic room. The pitches stay between
300 Hz and 2.5 kHz, where a small speaker is at its best.
"""
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

import numpy as np

RATE = 16000
PEAK = 0.6  # the little speaker distorts well before full scale
SEED = 7    # the room is noise; a fixed seed keeps the files the same every run


def fm(freq, length, ratio, index, decay, mod_decay, attack=0.004, detune=1.003):
    """An FM voice: carrier at `freq`, modulator at freq*ratio whose depth falls
    from `index` as the note rings. A second copy, detuned a little, widens it."""
    t = np.arange(int(RATE * length)) / RATE
    out = np.zeros_like(t)
    for f in (freq, freq * detune):
        mod = index * np.exp(-t * mod_decay) * np.sin(2 * np.pi * f * ratio * t)
        out += np.sin(2 * np.pi * f * t + mod)
    out *= 0.5 * np.exp(-t * decay)
    out *= np.minimum(1.0, t / attack) * np.minimum(1.0, (length - t) / 0.015)
    return out


def room(samples, seconds, wet):
    """A short synthetic room: an exponentially fading, darkened noise as the
    impulse response, convolved by FFT and mixed under the dry sound."""
    rng = np.random.default_rng(SEED)
    n = int(RATE * seconds)
    t = np.arange(n) / RATE
    impulse = rng.standard_normal(n) * np.exp(-t * (6.9 / seconds))
    impulse = np.convolve(impulse, np.ones(6) / 6, mode="same")  # darker tail
    impulse[0] = 0.0
    impulse /= np.sqrt(np.sum(impulse ** 2))
    size = len(samples) + n
    spectrum = np.fft.rfft(samples, size) * np.fft.rfft(impulse, size)
    tail = np.fft.irfft(spectrum, size)
    dry = np.zeros(size)
    dry[: len(samples)] = samples
    return dry + wet * tail


def place(parts):
    """Mixes (start_seconds, samples) parts into one buffer."""
    end = max(int(RATE * s) + len(x) for s, x in parts)
    out = np.zeros(end)
    for start, samples in parts:
        i = int(RATE * start)
        out[i:i + len(samples)] += samples
    return out


def lembrete():
    # A glassy rising arpeggio (C6 E6 G6 C7 is too thin on this speaker, so it stops
    # at G6) with the last note left ringing in the room: bright, friendly, clear.
    notes = [(0.00, 1046.5, 0.9), (0.16, 1318.5, 0.9), (0.32, 1568.0, 1.5)]
    dry = place([(s, fm(f, d, ratio=3.5, index=2.2, decay=3.2, mod_decay=9.0)) for s, f, d in notes])
    return room(dry, 0.55, 0.55)


def sem_internet():
    # A muted mallet falling a minor third (G5 to E5): something went away, softly.
    dry = place([(0.00, fm(784.0, 0.6, ratio=1.0, index=1.2, decay=5.0, mod_decay=14.0)),
                 (0.24, fm(659.3, 0.9, ratio=1.0, index=1.2, decay=4.0, mod_decay=14.0))])
    return room(dry, 0.45, 0.45)


def conectado():
    # Two bright strikes rising a fifth (A5 to E6), the second a little longer: it is back.
    dry = place([(0.00, fm(880.0, 0.4, ratio=3.5, index=1.8, decay=6.0, mod_decay=12.0)),
                 (0.11, fm(1318.5, 0.7, ratio=3.5, index=1.8, decay=4.5, mod_decay=12.0))])
    return room(dry, 0.4, 0.5)


def parada():
    # Two low round pulses falling a fourth (E4 to B3), with a bright click at the start
    # so they carry on a small speaker: firm and calm, not a buzzer.
    dry = place([(0.00, fm(329.6, 0.28, ratio=2.0, index=3.0, decay=7.0, mod_decay=30.0)),
                 (0.30, fm(246.9, 0.40, ratio=2.0, index=3.0, decay=6.0, mod_decay=30.0))])
    return room(dry, 0.3, 0.3)


def write(name, samples, out_dir):
    samples = samples / (np.max(np.abs(samples)) or 1.0) * PEAK
    fade = int(RATE * 0.04)  # the tail ends in silence, not on a cut
    samples[-fade:] *= np.linspace(1.0, 0.0, fade)
    with tempfile.TemporaryDirectory() as tmp:
        wav = Path(tmp) / (name + ".wav")
        with wave.open(str(wav), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(RATE)
            w.writeframes((samples * 32767).astype("<i2").tobytes())
        target = out_dir / (name + ".ogg")
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(wav), "-c:a", "libopus",
                        "-ar", str(RATE), "-ac", "1", "-b:a", "28k", "-frame_duration", "60",
                        str(target)], check=True)
    size = target.stat().st_size
    assert size < 64 * 1024 and target.read_bytes()[:4] == b"OggS", name
    print(f"{target}: {size} bytes, {len(samples) / RATE:.2f} s")


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
