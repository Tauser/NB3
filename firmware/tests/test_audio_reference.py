"""Check lag sign against known broadband signals, including a late reference."""
from pathlib import Path
import random
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from analyze_audio_reference import estimate_lag


class AudioReferenceTests(unittest.TestCase):
    def test_known_delays_and_inverted_polarity(self):
        rng = random.Random(481)
        source = [rng.randint(-16000, 16000) for _ in range(8000)]
        for delay, polarity in [(80, 1), (-64, 1), (0, 1), (37, -1)]:
            with self.subTest(delay=delay, polarity=polarity):
                mic = [polarity * source[i - delay] // 2 if 0 <= i - delay < len(source)
                       else 0 for i in range(len(source))]
                result = estimate_lag(mic, source, 16000)
                self.assertTrue(result['usable_estimate'])
                self.assertEqual(result['lag_samples'], delay)

    def test_silence_is_not_a_measurement(self):
        result = estimate_lag([0] * 8000, [0] * 8000, 16000)
        self.assertFalse(result['usable_estimate'])
        self.assertIsNone(result['lag_ms'])

    def test_insufficient_capture(self):
        with self.assertRaises(ValueError):
            estimate_lag([1] * 10, [1] * 10, 16000)


if __name__ == '__main__':
    unittest.main()
