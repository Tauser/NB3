"""Estimate mic/ref lag in an intentional stereo PCM16 WAV capture.

Channel 0 is mic, channel 1 is speaker reference, both BEFORE AFE. Positive
lag means the echo reaches mic AFTER the corresponding reference sample.
Use isolated robot playback and a broadband signal; speech by a person,
tonal signals and UDP loss can invalidate the estimate. No firmware tuning
or network access is performed by this tool.
"""
import argparse
from array import array
import json
import math
import sys
import wave


def estimate_lag(mic, ref, sample_rate, max_lag_ms=40):
    margin = round(sample_rate * max_lag_ms / 1000)
    count = min(len(mic), len(ref))
    if margin < 1 or count < 2 * margin + 512:
        raise ValueError('Not enough audio for this lag search window')
    mic_mean = sum(mic[:count]) / count
    ref_mean = sum(ref[:count]) / count

    def score(lag, stride):
        xy = xx = yy = 0.0
        for index in range(margin, count - margin, stride):
            x = mic[index] - mic_mean
            y = ref[index - lag] - ref_mean
            xy += x * y
            xx += x * x
            yy += y * y
        return xy / math.sqrt(xx * yy) if xx > 0 and yy > 0 else 0.0

    coarse = range(-margin, margin + 1)
    best = max(coarse, key=lambda lag: abs(score(lag, 8)))
    refined = range(max(-margin, best - 4), min(margin, best + 4) + 1)
    lag, correlation = max(((lag, score(lag, 4)) for lag in refined),
                           key=lambda pair: abs(pair[1]))
    usable = abs(correlation) >= 0.3 and abs(lag) < margin
    return {'lag_samples': lag if usable else None,
            'lag_ms': 1000 * lag / sample_rate if usable else None,
            'correlation': correlation, 'usable_estimate': usable,
            'warning': 'Estimate only: validate broadband content and capture continuity.'}


def channel_levels(samples):
    rms = math.sqrt(sum(float(s) * s for s in samples) / len(samples)) if samples else 0
    return {'rms_dbfs': 20 * math.log10(rms / 32768) if rms else None,
            'peak': max((abs(s) for s in samples), default=0),
            'saturation_samples': sum(abs(s) >= 32767 for s in samples)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('wav')
    parser.add_argument('--start', type=float, default=0, help='Start time in seconds')
    parser.add_argument('--seconds', type=float, default=2, help='Window duration, 0..10 s')
    parser.add_argument('--max-lag-ms', type=int, default=40)
    args = parser.parse_args()
    if args.start < 0 or not 0 < args.seconds <= 10 or not 1 <= args.max_lag_ms <= 100:
        parser.error('Require start >= 0, 0 < seconds <= 10 and 1 <= max-lag-ms <= 100')
    with wave.open(args.wav, 'rb') as source:
        if source.getnchannels() != 2 or source.getsampwidth() != 2:
            parser.error('Expected stereo PCM16: [mic, reference]')
        rate = source.getframerate()
        source.setpos(min(int(args.start * rate), source.getnframes()))
        pcm = array('h', source.readframes(int(args.seconds * rate)))
    if sys.byteorder != 'little':
        pcm.byteswap()
    mic, ref = pcm[::2], pcm[1::2]
    try:
        result = estimate_lag(mic, ref, rate, args.max_lag_ms)
    except ValueError as error:
        parser.error(str(error))
    result.update({'sample_rate': rate, 'samples': len(mic),
                   'mic': channel_levels(mic), 'reference': channel_levels(ref)})
    print(json.dumps(result, indent=2, allow_nan=False))


if __name__ == '__main__':
    main()
