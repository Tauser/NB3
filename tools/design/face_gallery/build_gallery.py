#!/usr/bin/env python3
"""Builds the face gallery from the real avatar core.

  python tools/design/face_gallery/build_gallery.py            # writes out/gallery.html
  python tools/design/face_gallery/build_gallery.py --record   # also replaces the reference poses

The reference poses (reference/poses.json) are what the 15 intentions look like
today: the stage 4 rewrite is compared against them, in motion, in the gallery.
Record them again only on purpose, and say why in the commit.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
CORE = HERE.parents[2] / "firmware" / "main" / "core"
REFERENCE = HERE / "reference" / "poses.json"
OUT = HERE / "out"

SOURCES = ["dump_frames.c", str(CORE / "nb_avatar.c"), str(CORE / "nb_overlay_anchor.c")]


def find_gcc():
    found = shutil.which("gcc")
    if found:
        return found
    fallback = Path("C:/msys64/ucrt64/bin/gcc.exe")
    if fallback.exists():
        return str(fallback)
    sys.exit("gcc not found: put the MSYS2 ucrt64 bin directory on PATH")


def build_dumper(workdir):
    exe = Path(workdir) / ("dump_frames.exe" if os.name == "nt" else "dump_frames")
    cmd = [find_gcc(), "-std=c17", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O1",
           f"-I{CORE}", "-o", str(exe)]
    cmd += [str(HERE / SOURCES[0])] + SOURCES[1:] + ["-lm"]
    subprocess.run(cmd, check=True)
    return exe


def run_dumper(exe, *args):
    # Windows needs the MSYS2 runtime next to the executable on PATH.
    env = dict(os.environ)
    env["PATH"] = str(Path(find_gcc()).parent) + os.pathsep + env.get("PATH", "")
    result = subprocess.run([str(exe), *args], check=True, capture_output=True, text=True, env=env)
    return result.stdout


def dump_all():
    """Returns (current, rest, contract) as parsed JSON, from a fresh build."""
    with tempfile.TemporaryDirectory() as workdir:
        exe = build_dumper(workdir)
        return (json.loads(run_dumper(exe)), json.loads(run_dumper(exe, "--rest")),
                json.loads(run_dumper(exe, "--contract")))


def compact(data):
    return json.dumps(data, separators=(",", ":"))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--record", action="store_true",
                        help="replace reference/poses.json with the current poses")
    args = parser.parse_args()

    current, rest, contract = dump_all()
    if args.record:
        REFERENCE.parent.mkdir(parents=True, exist_ok=True)
        REFERENCE.write_text(compact(rest) + "\n", encoding="utf-8")
        print(f"reference recorded: {REFERENCE}")
    if not REFERENCE.exists():
        sys.exit("no reference poses: run with --record once")
    reference = json.loads(REFERENCE.read_text(encoding="utf-8"))

    template = (HERE / "gallery.template.html").read_text(encoding="utf-8")
    page = (template.replace("/*CURRENT*/null", compact(current))
            .replace("/*REFERENCE*/null", compact(reference))
            .replace("/*CONTRACT*/null", compact(contract)))
    OUT.mkdir(exist_ok=True)
    target = OUT / "gallery.html"
    target.write_text(page, encoding="utf-8")
    print(f"gallery: {target} ({target.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main()
