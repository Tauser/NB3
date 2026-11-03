"""Bench capture of the voice path: what the mic heard and what went to the server.

  config   writes the sdkconfig of the bench firmware from the current one
  capture  receives both streams from that firmware and saves them

capture writes, in the output folder:
  pre_afe.wav     the mic as read, before the AFE
  turn_NNN.ogg    Opus packets of one listening session, as sent to the server
  turn_NNN.wav    the same decoded, when ffmpeg is on the PATH
  events.csv      each datagram, with arrival time and position in pre_afe.wav

Each turn's summary counts packets lost between the robot and this PC; those
are capture losses, not losses on the way to the server.
"""
import argparse
import csv
import select
import shutil
import socket
import struct
import subprocess
import time
import wave
from pathlib import Path

PCM_PORT = 8000
TAP_PORT = PCM_PORT + 1
SAMPLE_RATE = 16000
FRAME_MS = 60
SMALL_PACKET_BYTES = 8  # NB_UPLINK_SMALL_PACKET_BYTES

# firmware/main/core/nb_uplink_stats.h
TAP_HEADER = struct.Struct('>4sB3xII')
TAP_PACKET, TAP_TURN_START, TAP_TURN_END = 0, 1, 2

BENCH_OPTIONS = [
    'CONFIG_USE_AUDIO_DEBUGGER=y',
    'CONFIG_NB3_UPLINK_TAP=y',
    'CONFIG_NB3_AUDIO_DIAGNOSTICS=y',
]


def parse_tap(datagram):
    """Returns (kind, seq, device_ms, payload), or None if it is not a tap datagram."""
    if len(datagram) < TAP_HEADER.size:
        return None
    magic, kind, seq, device_ms = TAP_HEADER.unpack_from(datagram)
    if magic != b'NBUP':
        return None
    return kind, seq, device_ms, datagram[TAP_HEADER.size:]


def ogg_crc(data):
    crc = 0
    for byte in data:
        crc ^= byte << 24
        for _ in range(8):
            crc = (crc << 1) ^ 0x04C11DB7 if crc & 0x80000000 else crc << 1
            crc &= 0xFFFFFFFF
    return crc


def ogg_page(packet, serial, sequence, granule, flags=0):
    segments, rest = [], len(packet)
    while rest >= 255:
        segments.append(255)
        rest -= 255
    segments.append(rest)
    head = struct.pack('<4sBBqIIIB', b'OggS', 0, flags, granule, serial, sequence, 0, len(segments))
    page = head + bytes(segments) + packet
    return page[:22] + struct.pack('<I', ogg_crc(page)) + page[26:]


class OggOpusWriter:
    """One Ogg page per packet, so a file cut short still decodes."""

    SERIAL = 0x4E423300
    GRANULES_PER_PACKET = 48 * FRAME_MS  # Ogg Opus counts time at 48 kHz

    def __init__(self, path):
        self.file = open(path, 'wb')
        self.sequence = 0
        self.granule = 0
        head = b'OpusHead' + struct.pack('<BBHIhB', 1, 1, 0, SAMPLE_RATE, 0, 0)
        self._page(head, flags=0x02)
        self._page(b'OpusTags' + struct.pack('<I', 2) + b'NB' + struct.pack('<I', 0))

    def _page(self, packet, flags=0):
        self.file.write(ogg_page(packet, self.SERIAL, self.sequence, self.granule, flags))
        self.sequence += 1

    def write(self, packet):
        self.granule += self.GRANULES_PER_PACKET
        self._page(packet)

    def close(self):
        self.file.close()


class Turn:
    def __init__(self, number, folder):
        self.path = folder / f'turn_{number:03d}.ogg'
        self.writer = OggOpusWriter(self.path)
        self.packets = 0
        self.small = 0
        self.lost = 0
        self.last_seq = None

    def add(self, seq, payload):
        if self.last_seq is not None and seq > self.last_seq + 1:
            self.lost += seq - self.last_seq - 1
        self.last_seq = seq
        if payload:
            self.writer.write(payload)
            self.packets += 1
            if len(payload) <= SMALL_PACKET_BYTES:
                self.small += 1

    def close(self):
        self.writer.close()
        decoded = ''
        ffmpeg = shutil.which('ffmpeg')
        if ffmpeg and self.packets:
            wav = self.path.with_suffix('.wav')
            done = subprocess.run([ffmpeg, '-y', '-loglevel', 'error', '-i', str(self.path),
                                   str(wav)], check=False)
            decoded = f', {wav.name}' if done.returncode == 0 else ', ffmpeg failed'
        print(f'{self.path.name}: {self.packets} packets ({self.packets * FRAME_MS} ms), '
              f'{self.small} small, {self.lost} lost in capture{decoded}', flush=True)


def capture(folder, seconds=None, channels=1):
    folder.mkdir(parents=True, exist_ok=True)
    pcm_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    pcm_socket.bind(('0.0.0.0', PCM_PORT))
    tap_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    tap_socket.bind(('0.0.0.0', TAP_PORT))

    pcm = wave.open(str(folder / 'pre_afe.wav'), 'wb')
    pcm.setnchannels(channels)
    pcm.setsampwidth(2)
    pcm.setframerate(SAMPLE_RATE)
    events_file = open(folder / 'events.csv', 'w', newline='', encoding='utf-8')
    events = csv.writer(events_file)
    events.writerow(['arrival_s', 'seq', 'kind', 'device_ms', 'bytes', 'pre_afe_sample'])

    print(f'Listening on UDP {PCM_PORT} and {TAP_PORT}, saving to {folder}. Ctrl+C ends.',
          flush=True)
    start = time.monotonic()
    pcm_frames = 0
    turn = None
    turns = 0
    announced = False
    try:
        while seconds is None or time.monotonic() - start < seconds:
            ready, _, _ = select.select([pcm_socket, tap_socket], [], [], 0.5)
            for source in ready:
                datagram, sender = source.recvfrom(4096)
                if not announced:
                    print(f'First datagram from {sender[0]}', flush=True)
                    announced = True
                if source is pcm_socket:
                    pcm.writeframes(datagram)
                    pcm_frames += len(datagram) // (2 * channels)
                    continue
                parsed = parse_tap(datagram)
                if parsed is None:
                    continue
                kind, seq, device_ms, payload = parsed
                events.writerow([f'{time.monotonic() - start:.3f}', seq, kind, device_ms,
                                 len(payload), pcm_frames])
                if kind == TAP_TURN_START and turn is None:
                    turns += 1
                    turn = Turn(turns, folder)
                if turn is None and kind == TAP_PACKET:
                    # The capture began in the middle of a session.
                    turns += 1
                    turn = Turn(turns, folder)
                if turn is not None:
                    turn.add(seq, payload)
                if kind == TAP_TURN_END and turn is not None:
                    turn.close()
                    turn = None
    except KeyboardInterrupt:
        pass
    finally:
        if turn is not None:
            turn.close()
        pcm.close()
        events_file.close()
        pcm_socket.close()
        tap_socket.close()
    print(f'{turns} turns, {pcm_frames / SAMPLE_RATE:.1f} s of pre-AFE audio', flush=True)


def write_bench_config(current, output, host):
    lines = current.read_text(encoding='utf-8').splitlines()
    names = [option.split('=')[0] for option in BENCH_OPTIONS] + ['CONFIG_AUDIO_DEBUG_UDP_SERVER']
    kept = [line for line in lines if not any(
        line.startswith(name + '=') or line == f'# {name} is not set' for name in names)]
    extra = BENCH_OPTIONS + [f'CONFIG_AUDIO_DEBUG_UDP_SERVER="{host}:{PCM_PORT}"']
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('\n'.join(kept + extra) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    config = commands.add_parser('config')
    config.add_argument('--host', required=True, help='IP of the PC running capture')
    config.add_argument('--sdkconfig', type=Path, default=Path('firmware/sdkconfig'))
    config.add_argument('--output', type=Path, default=Path('firmware/build-bench/sdkconfig'))
    run = commands.add_parser('capture')
    run.add_argument('folder', type=Path)
    run.add_argument('--seconds', type=float, help='stop by itself after this long')
    run.add_argument('--channels', type=int, choices=(1, 2), default=1,
                     help='2 only with the device AEC on: mic, then speaker reference')
    args = parser.parse_args()
    if args.command == 'config':
        write_bench_config(args.sdkconfig, args.output, args.host)
        print(f'Wrote {args.output}')
    else:
        capture(args.folder, args.seconds, args.channels)


if __name__ == '__main__':
    main()
