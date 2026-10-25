"""Fetch external dependencies pinned in repos.json and apply their patches.

Each entry is cloned at a fixed tag. A fetched tree is only ever in one of two
accepted states: pristine at the tag (patch gets applied) or already carrying
exactly our patch (nothing to do). Anything else aborts, because local edits
inside a fetched tree would silently diverge from the versioned patch.
"""

import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def git(args, cwd=None, check=True):
    result = subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True)
    if check and result.returncode != 0:
        fail(f"git {' '.join(args)} failed:\n{result.stderr.strip()}")
    return result


def fail(message):
    print(f"ERROR: {message}", file=sys.stderr)
    sys.exit(1)


def head_tag(repo):
    return git(["describe", "--tags", "--exact-match", "HEAD"], cwd=repo, check=False).stdout.strip()


def is_clean(repo):
    return git(["status", "--porcelain"], cwd=repo).stdout.strip() == ""


def fetch(entry):
    url, tag = entry["url"], entry["tag"]
    repo = ROOT / entry["path"]
    patch = ROOT / entry["patch"] if entry.get("patch") else None
    if patch and not patch.is_file():
        fail(f"{entry['path']}: patch file {patch} not found")

    if not repo.exists():
        print(f"{entry['path']}: cloning {url} @ {tag}")
        git(["clone", "--quiet", "--depth", "1", "--branch", tag, url, str(repo)])
        git(["submodule", "update", "--init", "--recursive", "--depth", "1"], cwd=repo)
    elif head_tag(repo) != tag:
        fail(f"{entry['path']}: checked out at '{head_tag(repo) or 'unknown'}', expected {tag}. "
             f"Delete the directory and run again.")

    if patch is None:
        return

    patch_arg = str(patch)
    if git(["apply", "--reverse", "--check", patch_arg], cwd=repo, check=False).returncode == 0:
        print(f"{entry['path']}: patch already applied")
        return
    if not is_clean(repo):
        fail(f"{entry['path']}: tree has changes that are not the versioned patch. "
             f"Move them into {entry['patch']} or delete the directory.")
    if git(["apply", "--check", patch_arg], cwd=repo, check=False).returncode != 0:
        fail(f"{entry['path']}: {entry['patch']} does not apply on {tag}")
    git(["apply", patch_arg], cwd=repo)
    print(f"{entry['path']}: applied {entry['patch']}")


def main():
    for entry in json.loads((ROOT / "repos.json").read_text(encoding="utf-8")):
        fetch(entry)


if __name__ == "__main__":
    main()
