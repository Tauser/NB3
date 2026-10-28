"""Fetch external dependencies pinned in repos.json and apply their patches.

Each entry is cloned at a fixed tag. A fetched tree is only ever in one of two
accepted states: pristine at the tag (patch gets applied) or exactly the tag
plus our patch (nothing to do). Anything else aborts, because local edits
inside a fetched tree would silently diverge from the versioned patch.

"Exactly" is checked by building both trees in throwaway git indexes, never
touching the checkout: the expected one is the tag with the patch applied,
the actual one is the working tree with staged, unstaged and untracked files
(ignored files are left out). Equal tree hashes or it fails.
"""

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent


class FetchError(Exception):
    pass


def git(args, cwd=None, check=True, env=None):
    result = subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True,
                            env={**os.environ, **env} if env else None)
    if check and result.returncode != 0:
        raise FetchError(f"git {' '.join(args)} failed:\n{result.stderr.strip()}")
    return result


def head_tag(repo):
    return git(["describe", "--tags", "--exact-match", "HEAD"], cwd=repo, check=False).stdout.strip()


def is_clean(repo):
    return git(["status", "--porcelain"], cwd=repo).stdout.strip() == ""


def normalized(path):
    return path.read_bytes().replace(b"\r\n", b"\n")


def tree_of(repo, fill):
    """Tree hash of an index that starts at HEAD and is changed by fill(env)."""
    with tempfile.TemporaryDirectory() as tmp:
        env = {"GIT_INDEX_FILE": str(Path(tmp) / "index")}
        git(["read-tree", "HEAD"], cwd=repo, env=env)
        fill(env)
        return git(["write-tree"], cwd=repo, env=env).stdout.strip()


def expected_tree(repo, patch):
    return tree_of(repo, lambda env: git(["apply", "--cached", str(patch)], cwd=repo, env=env))


def actual_tree(repo):
    return tree_of(repo, lambda env: git(["add", "--all", "."], cwd=repo, env=env))


def differing_paths(repo, a, b):
    out = git(["diff-tree", "-r", "--name-only", a, b], cwd=repo).stdout.split()
    return ", ".join(out[:10]) + (" ..." if len(out) > 10 else "")


def check_mirrors(entry, repo, root):
    """Files NB3 keeps as verbatim copies of upstream must match the fetched tag."""
    for mirror in entry.get("mirrors", []):
        upstream, local = repo / mirror["upstream"], root / mirror["local"]
        if not local.is_file() or normalized(local) != normalized(upstream):
            raise FetchError(
                f"{mirror['local']} differs from {entry['path']}/{mirror['upstream']} at "
                f"{entry['tag']}. Copy it again (and re-check NB3 additions) after a tag change.")


def fetch(entry, root=ROOT):
    url, tag = entry["url"], entry["tag"]
    repo = root / entry["path"]
    patch = root / entry["patch"] if entry.get("patch") else None
    if patch and not patch.is_file():
        raise FetchError(f"{entry['path']}: patch file {patch} not found")

    if not repo.exists():
        print(f"{entry['path']}: cloning {url} @ {tag}")
        git(["clone", "--quiet", "--depth", "1", "--branch", tag, url, str(repo)])
        git(["submodule", "update", "--init", "--recursive", "--depth", "1"], cwd=repo)
    elif head_tag(repo) != tag:
        raise FetchError(f"{entry['path']}: checked out at '{head_tag(repo) or 'unknown'}', "
                         f"expected {tag}. Delete the directory and run again.")

    check_mirrors(entry, repo, root)

    if patch is None:
        if not is_clean(repo):
            raise FetchError(f"{entry['path']}: tree has changes but no patch is versioned. "
                             f"Delete the directory and run again.")
        return "clean"

    if is_clean(repo):
        if git(["apply", "--check", str(patch)], cwd=repo, check=False).returncode != 0:
            raise FetchError(f"{entry['path']}: {entry['patch']} does not apply on {tag}")
        git(["apply", str(patch)], cwd=repo)
        print(f"{entry['path']}: applied {entry['patch']}")
        return "applied"

    expected = expected_tree(repo, patch)
    actual = actual_tree(repo)
    if expected != actual:
        raise FetchError(
            f"{entry['path']}: tree is not exactly {tag} + {entry['patch']} (differs in: "
            f"{differing_paths(repo, expected, actual)}). Move the changes into "
            f"{entry['patch']} or delete the directory.")
    print(f"{entry['path']}: patch already applied")
    return "already"


def main():
    try:
        for entry in json.loads((ROOT / "repos.json").read_text(encoding="utf-8")):
            fetch(entry)
    except FetchError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
