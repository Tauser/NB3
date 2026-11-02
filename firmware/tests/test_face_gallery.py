"""Protects the recorded poses of the 15 intentions and the channel contract.

The gallery tool samples the real avatar core. Its reference poses are what the
stage 4 rewrite is compared against, so a change in them must be deliberate:
run `build_gallery.py --record` and say why in the commit.
"""
import json
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'design' / 'face_gallery'))
import build_gallery

EXPECTED = ['IDLE', 'LISTENING', 'THINKING', 'HAPPY', 'EXCITED', 'CURIOUS', 'CONFUSED', 'ANGRY',
            'SURPRISED', 'SAD', 'SLEEPY', 'DIZZY', 'LOVING', 'EMBARRASSED', 'CONTENT']
LOOKS = ['rest', 'up_left', 'up_right', 'down_left', 'down_right']


class FaceGalleryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.current, cls.rest, cls.contract = build_gallery.dump_all()

    def test_rest_poses_match_the_recorded_reference(self):
        reference = json.loads(build_gallery.REFERENCE.read_text(encoding='utf-8'))
        self.assertEqual(reference, self.rest)

    def test_every_intention_has_every_look(self):
        found = {(s['expression'], s['look']) for s in self.current['scenarios']}
        self.assertEqual(found, {(e, l) for e in EXPECTED for l in LOOKS})

    def test_scenarios_enter_hold_and_leave(self):
        for s in self.rest['scenarios']:
            with self.subTest(expression=s['expression']):
                times = [f[0] for f in s['frames']]
                self.assertEqual(times, sorted(times))
                self.assertGreater(times[-1], s['release_ms'])
                self.assertGreater(len(times), 100)

    def touching(self, channel):
        return {(s['expression'], s['look']) for s in self.contract['scenarios']
                if s[channel]['hit_pct']}

    def test_side_column_of_channel_t_is_always_free(self):
        self.assertEqual(self.touching('icon'), set())

    def test_bars_touch_the_eyes_only_in_the_measured_cases(self):
        # Measured on 02/11 with the eye bounding boxes, which overestimate. A new
        # case means the layout or the poses changed: look at the gallery first.
        self.assertLessEqual(self.touching('bars'), {
            ('EXCITED', 'down_left'), ('EXCITED', 'down_right'),
            ('CURIOUS', 'down_right'), ('CURIOUS', 'up_right')})

    def test_hand_touches_the_eyes_only_in_the_measured_cases(self):
        self.assertLessEqual(self.touching('hand'), {
            ('IDLE', 'down_right'), ('LISTENING', 'down_left'), ('LISTENING', 'down_right'),
            ('THINKING', 'down_right'), ('EXCITED', 'up_right'), ('EXCITED', 'down_left'),
            ('EXCITED', 'down_right'), ('CURIOUS', 'up_right'), ('CURIOUS', 'down_right'),
            ('CONFUSED', 'down_right'), ('SURPRISED', 'down_right'), ('DIZZY', 'rest'),
            ('DIZZY', 'up_left'), ('DIZZY', 'up_right'), ('DIZZY', 'down_left'),
            ('DIZZY', 'down_right'), ('CONTENT', 'down_right')})

    def test_layout_constants_match_the_firmware(self):
        display = (ROOT / 'firmware' / 'main' / 'face' / 'face_display.cc').read_text(encoding='utf-8')
        tool = (ROOT / 'tools' / 'design' / 'face_gallery' / 'dump_frames.c').read_text(encoding='utf-8')

        def firmware(name):
            return float(re.search(rf'constexpr float {name} = (-?[\d.]+)f;', display).group(1))

        def gallery(name):
            return float(re.search(rf'#define {name} \(?(-?[\d.]+)\)?', tool).group(1))

        for fw, tl in [('kDesignScale', 'DESIGN_SCALE'), ('kBarsLeft', 'BARS_LEFT'),
                       ('kBarsStep', 'BARS_STEP'), ('kBarsCentreY', 'BARS_CENTRE_Y'),
                       ('kBarsMaxHeight', 'BARS_MAX_HEIGHT'), ('kHandLeft', 'HAND_LEFT'),
                       ('kHandTop', 'HAND_TOP'), ('kHeartsY', 'HEARTS_Y'),
                       ('kHeartsRise', 'HEARTS_RISE')]:
            with self.subTest(constant=fw):
                self.assertEqual(firmware(fw), gallery(tl))


if __name__ == '__main__':
    unittest.main()
