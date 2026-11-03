import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest


TOOL = Path(__file__).resolve().parents[2] / 'tools' / 'audio_capture.py'
SPEC = importlib.util.spec_from_file_location('audio_capture', TOOL)
capture = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(capture)


def read_pages(data):
    pages, offset = [], 0
    while offset < len(data):
        count = data[offset + 26]
        lacing = data[offset + 27:offset + 27 + count]
        size = sum(lacing)
        end = offset + 27 + count + size
        page = data[offset:end]
        stored = struct.unpack_from('<I', page, 22)[0]
        blank = page[:22] + b'\0\0\0\0' + page[26:]
        pages.append({'crc_ok': stored == capture.ogg_crc(blank),
                      'granule': struct.unpack_from('<q', page, 6)[0],
                      'flags': page[5],
                      'packet': data[offset + 27 + count:end]})
        offset = end
    return pages


class ParseTapTest(unittest.TestCase):
    def test_same_layout_as_test_uplink_stats(self):
        datagram = bytes([0x4E, 0x42, 0x55, 0x50, 2, 0, 0, 0, 1, 2, 3, 4, 0xA0, 0xB0, 0xC0, 0xD0])
        self.assertEqual(capture.parse_tap(datagram + b'xy'),
                         (2, 0x01020304, 0xA0B0C0D0, b'xy'))

    def test_rejects_other_datagrams(self):
        self.assertIsNone(capture.parse_tap(b'short'))
        self.assertIsNone(capture.parse_tap(b'XXXX' + bytes(12)))


class OggTest(unittest.TestCase):
    def test_crc_check_value(self):
        self.assertEqual(capture.ogg_crc(b'123456789'), 0x89A1897F)

    def test_file_structure(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'a.ogg'
            writer = capture.OggOpusWriter(path)
            writer.write(b'\xf8\xff\xfe')
            writer.write(b'x' * 600)  # needs more than one lacing value
            writer.close()
            pages = read_pages(path.read_bytes())
        self.assertTrue(all(page['crc_ok'] for page in pages))
        self.assertEqual(pages[0]['flags'], 2)
        self.assertTrue(pages[0]['packet'].startswith(b'OpusHead'))
        self.assertTrue(pages[1]['packet'].startswith(b'OpusTags'))
        self.assertEqual(pages[2]['packet'], b'\xf8\xff\xfe')
        self.assertEqual(len(pages[3]['packet']), 600)
        self.assertEqual([page['granule'] for page in pages], [0, 0, 2880, 5760])


class TurnTest(unittest.TestCase):
    def test_counts_small_and_lost(self):
        with tempfile.TemporaryDirectory() as folder:
            turn = capture.Turn(1, Path(folder))
            turn.add(10, b'')  # start mark
            turn.add(11, b'x' * 100)
            turn.add(14, b'y' * capture.SMALL_PACKET_BYTES)
            self.assertEqual((turn.packets, turn.small, turn.lost), (2, 1, 2))
            turn.writer.close()


class BenchConfigTest(unittest.TestCase):
    def test_replaces_only_the_bench_options(self):
        with tempfile.TemporaryDirectory() as folder:
            current = Path(folder) / 'sdkconfig'
            current.write_text('CONFIG_A=y\n# CONFIG_USE_AUDIO_DEBUGGER is not set\n'
                               '# CONFIG_NB3_AUDIO_DIAGNOSTICS is not set\nCONFIG_B=1\n',
                               encoding='utf-8')
            output = Path(folder) / 'bench' / 'sdkconfig'
            capture.write_bench_config(current, output, '10.0.0.2')
            lines = output.read_text(encoding='utf-8').splitlines()
        self.assertIn('CONFIG_A=y', lines)
        self.assertIn('CONFIG_B=1', lines)
        self.assertIn('CONFIG_USE_AUDIO_DEBUGGER=y', lines)
        self.assertIn('CONFIG_NB3_UPLINK_TAP=y', lines)
        self.assertIn('CONFIG_AUDIO_DEBUG_UDP_SERVER="10.0.0.2:8000"', lines)
        self.assertNotIn('# CONFIG_USE_AUDIO_DEBUGGER is not set', lines)


if __name__ == '__main__':
    unittest.main()
