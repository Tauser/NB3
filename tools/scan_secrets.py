#!/usr/bin/env python3
"""Simple secret scan over the files tracked by git (CI job secrets-scan).

Catches the usual leaks: Wi-Fi or account passwords, API keys and tokens
written as literals, private keys. Not a full DLP; it is the minimum safety
net that runs on every push. For a false positive, add "path:line" to
ALLOWLIST with a comment saying why.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

# (label, pattern, file suffixes exempt from it)
PATTERNS = [
    ("password literal",
     re.compile(r"(password|passwd|senha|wifi_pass\w*)\s*[=:]\s*[\"'][^\"']{4,}[\"']", re.I),
     set()),
    ("api key / secret literal",
     re.compile(r"(api[_-]?key|secret[_-]?key|client[_-]?secret|access[_-]?token)\s*[=:]\s*"
                r"[\"'][A-Za-z0-9_\-]{16,}[\"']", re.I),
     set()),
    ("long hex token literal",
     re.compile(r"[\"'][0-9a-f]{32,}[\"']"),
     set()),
    ("private key",
     re.compile(r"-----BEGIN (RSA |EC |OPENSSH )?PRIVATE KEY-----"),
     set()),
    ("github token",
     re.compile(r"\b(ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}"),
     set()),
    ("aws access key",
     re.compile(r"\bAKIA[0-9A-Z]{16}\b"),
     set()),
    ("slack token",
     re.compile(r"\bxox[abprs]-[A-Za-z0-9-]{10,}"),
     set()),
    ("openai/anthropic key",
     re.compile(r"\bsk-(ant-)?[A-Za-z0-9_\-]{20,}"),
     set()),
]

SKIP_SUFFIXES = {".png", ".jpg", ".jpeg", ".gif", ".bmp", ".ico", ".bin", ".ogg", ".wav",
                 ".mp3", ".pdf", ".zip", ".gz"}

# path:line entries that are known not to be secrets.
ALLOWLIST: set[str] = set()


def tracked_files(root: Path) -> list[Path]:
    out = subprocess.run(["git", "ls-files", "-z"], cwd=root, capture_output=True, check=True)
    return [root / name for name in out.stdout.decode("utf-8").split("\0") if name]


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    this_file = Path(__file__).resolve()
    findings = []
    for path in tracked_files(root):
        if path.suffix.lower() in SKIP_SUFFIXES or path.resolve() == this_file:
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        rel = path.relative_to(root).as_posix()
        for lineno, line in enumerate(text.splitlines(), 1):
            key = f"{rel}:{lineno}"
            if key in ALLOWLIST:
                continue
            for label, pattern, exempt in PATTERNS:
                if path.suffix in exempt:
                    continue
                if pattern.search(line):
                    findings.append(f"{key}: [{label}] {line.strip()[:100]}")

    if findings:
        print("Possible secrets found:")
        print("\n".join(findings))
        return 1
    print("secrets-scan: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
