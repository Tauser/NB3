#!/usr/bin/env python3
"""Scores the face reading test (docs/TESTE-LEITURA-FACES.md).

  face_reading_score.py KEY_LOG ANSWERS.csv [ANSWERS2.csv ...]

KEY_LOG is the serial log of a build with NB3_FACE_QUIZ; its "KEY item N pass P
face NAME" lines are the answer key. Each ANSWERS file has the rows
"rater,item,answer", the answer being one of the Portuguese names below.
"""
import csv
import re
import sys
from collections import Counter, defaultdict

NAMES = {
    "neutro": "neutral", "feliz": "happy", "animado": "excited", "curioso": "curious",
    "confuso": "confused", "bravo": "angry", "surpreso": "surprised", "triste": "sad",
    "sonolento": "sleepy", "tonto": "dizzy", "apaixonado": "loving",
    "envergonhado": "embarrassed", "contente": "content",
}
# Pairs that are easy to mix up, read as a group (plan, section 10).
PAIRS = [("neutral", "content"), ("curious", "confused"),
         ("sad", "sleepy", "embarrassed"), ("happy", "loving")]
PAIR_TARGET = 0.80
MIN_RATERS = 3

KEY_LINE = re.compile(r"KEY item (\d+) pass \d+ face (\w+)")


def read_key(path):
    key = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = KEY_LINE.search(line)
            if m:
                key[int(m.group(1))] = m.group(2)
    return key


def read_answers(paths):
    answers = []
    for path in paths:
        with open(path, newline="", encoding="utf-8") as f:
            for row in csv.reader(f):
                if len(row) < 3 or not row[1].strip().isdigit():
                    continue  # header or blank line
                name = row[2].strip().lower()
                if name not in NAMES:
                    sys.exit(f"{path}: unknown answer '{row[2]}' (item {row[1]})")
                answers.append((row[0].strip(), int(row[1]), NAMES[name]))
    return answers


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    key = read_key(sys.argv[1])
    answers = read_answers(sys.argv[2:])
    if not key:
        sys.exit("no KEY lines in the log")

    raters = sorted({r for r, _, _ in answers})
    hits = Counter()
    total = Counter()
    confusions = defaultdict(Counter)
    for rater, item, given in answers:
        if item not in key:
            continue
        shown = key[item]
        total[shown] += 1
        hits[shown] += given == shown
        if given != shown:
            confusions[shown][given] += 1

    print(f"{len(raters)} raters ({', '.join(raters)}), {sum(total.values())} answers, "
          f"{len(key)} items in the key\n")
    print(f"{'face':<13}{'hits':>6}{'of':>5}{'rate':>8}  most common mistakes")
    for face in sorted(total):
        rate = hits[face] / total[face]
        mistakes = ", ".join(f"{g} x{n}" for g, n in confusions[face].most_common(3))
        print(f"{face:<13}{hits[face]:>6}{total[face]:>5}{rate:>8.0%}  {mistakes}")

    print()
    for group in PAIRS:
        n = sum(total[f] for f in group)
        if n == 0:
            continue
        rate = sum(hits[f] for f in group) / n
        print(f"{'/'.join(group):<30}{rate:>5.0%}  "
              f"{'meets' if rate >= PAIR_TARGET else 'below'} {PAIR_TARGET:.0%}")

    if len(raters) < MIN_RATERS:
        print(f"\nOnly {len(raters)} raters: record the limitation, do not call the "
              f"criterion met (needs {MIN_RATERS}).")


if __name__ == "__main__":
    main()
