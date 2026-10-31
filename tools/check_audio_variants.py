"""Compile audio variants using the real build flags without changing sdkconfig.

Run after idf.py build. This is a syntax/type check of the integration units,
not a substitute for linking and booting the alternate configurations.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import shlex
import subprocess


def command_args(command):
    if os.name != 'nt':
        return shlex.split(command)
    parse = ctypes.windll.shell32.CommandLineToArgvW
    parse.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    parse.restype = ctypes.POINTER(ctypes.c_wchar_p)
    count = ctypes.c_int()
    raw = parse(command, ctypes.byref(count))
    if not raw:
        raise OSError('CommandLineToArgvW failed')
    try:
        return [raw[i] for i in range(count.value)]
    finally:
        free = ctypes.windll.kernel32.LocalFree
        free.argtypes = [ctypes.c_void_p]
        free.restype = ctypes.c_void_p
        free(raw)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=Path('firmware/build'))
    args = parser.parse_args()
    build = args.build_dir.resolve()
    output = build / 'audio-variants'
    output.mkdir(exist_ok=True)
    entries = json.loads((build / 'compile_commands.json').read_text(encoding='utf-8'))
    sources = ['afe_audio_engine.cc', 'audio_service.cc', 'noisebot_v3_board.cc']
    variants = {
        'current': {},
        'jarvis': {'CONFIG_NB3_MICRO_WAKE_WORD': None},
        'gate_off': {'CONFIG_NB3_BARGE_IN_GATE': None},
        'half_duplex_jarvis': {'CONFIG_USE_DEVICE_AEC': None,
                              'CONFIG_NB3_BARGE_IN_GATE': None,
                              'CONFIG_NB3_MICRO_WAKE_WORD': None},
        'diagnostics': {'CONFIG_NB3_AUDIO_DIAGNOSTICS': '1'},
    }
    failures = 0
    for name, options in variants.items():
        shim = output / f'{name}.h'
        config = '#include "' + (build / 'config/sdkconfig.h').as_posix() + '"\n'
        for symbol, value in options.items():
            config += f'#undef {symbol}\n'
            if value is not None:
                config += f'#define {symbol} {value}\n'
        shim.write_text(config, encoding='utf-8')
        for source in sources:
            entry = next(e for e in entries if Path(e['file']).name == source)
            command = command_args(entry['command'])
            for flag in ['-o', '-MF', '-MT', '-MQ']:
                while flag in command:
                    index = command.index(flag)
                    del command[index:index + 2]
            command = [arg for arg in command if arg not in ['-c', '-MD', '-MMD']]
            command += ['-fsyntax-only', '-fdiagnostics-color=never', '-fmax-errors=5',
                        '-include', str(shim)]
            result = subprocess.run(command, cwd=entry['directory'], capture_output=True,
                                    text=True, encoding='utf-8', errors='replace')
            (output / f'{name}-{source}.log').write_text(result.stdout + result.stderr,
                                                       encoding='utf-8')
            print(f'{name}/{source}: {"PASS" if result.returncode == 0 else "FAIL"}', flush=True)
            if result.returncode:
                failures += 1
                print(result.stderr)
    return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(main())
