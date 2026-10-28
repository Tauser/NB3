"""Tests of fetch_repos.py against throwaway local repositories (no network)."""

import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import fetch_repos  # noqa: E402


def run(args, cwd):
    subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", *args], cwd=cwd,
                   check=True, capture_output=True)


class FetchReposTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        upstream = self.tmp / "upstream"
        upstream.mkdir()
        run(["init", "-q"], upstream)
        run(["config", "core.autocrlf", "false"], upstream)
        (upstream / "a.txt").write_text("one\nthree\n")
        (upstream / "keep.txt").write_text("same\n")
        run(["add", "."], upstream)
        run(["commit", "-qm", "base"], upstream)
        run(["tag", "v1"], upstream)

        # The versioned patch: change a.txt and add b.txt.
        work = self.tmp / "work"
        run(["clone", "-q", str(upstream), str(work)], self.tmp)
        (work / "a.txt").write_text("one\ntwo\nthree\n")
        (work / "b.txt").write_text("new\n")
        run(["add", "."], work)
        diff = subprocess.run(["git", "diff", "--cached"], cwd=work, check=True,
                              capture_output=True).stdout

        self.root = self.tmp / "root"
        (self.root / "patches").mkdir(parents=True)
        (self.root / "patches" / "dep.patch").write_bytes(diff)
        self.entry = {"url": upstream.as_uri(), "path": "dep", "tag": "v1",
                      "patch": "patches/dep.patch"}
        self.repo = self.root / "dep"

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def fetch(self):
        return fetch_repos.fetch(self.entry, root=self.root)

    def test_clean_tree_gets_the_patch(self):
        self.assertEqual("applied", self.fetch())
        self.assertEqual("one\ntwo\nthree\n", (self.repo / "a.txt").read_text())
        self.assertTrue((self.repo / "b.txt").is_file())

    def test_patched_tree_is_left_alone(self):
        self.fetch()
        self.assertEqual("already", self.fetch())

    def test_patch_plus_an_extra_edit_fails(self):
        self.fetch()
        (self.repo / "keep.txt").write_text("edited by hand\n")
        with self.assertRaisesRegex(fetch_repos.FetchError, "keep.txt"):
            self.fetch()

    def test_patch_plus_an_edit_inside_a_patched_file_fails(self):
        self.fetch()
        (self.repo / "a.txt").write_text("one\ntwo\nthree\nfour\n")
        with self.assertRaisesRegex(fetch_repos.FetchError, "a.txt"):
            self.fetch()

    def test_patch_plus_a_staged_or_untracked_file_fails(self):
        self.fetch()
        (self.repo / "extra.txt").write_text("untracked\n")
        with self.assertRaisesRegex(fetch_repos.FetchError, "extra.txt"):
            self.fetch()
        run(["add", "extra.txt"], self.repo)
        with self.assertRaisesRegex(fetch_repos.FetchError, "extra.txt"):
            self.fetch()

    def test_patch_that_does_not_apply_fails(self):
        self.fetch()
        run(["checkout", "-q", "--", "."], self.repo)
        (self.repo / "b.txt").unlink()
        (self.repo / "a.txt").write_text("totally different\n")
        run(["-c", "core.autocrlf=false", "commit", "-qam", "x"], self.repo)
        run(["tag", "-f", "v1"], self.repo)  # same tag name, other content
        with self.assertRaisesRegex(fetch_repos.FetchError, "does not apply"):
            self.fetch()

    def test_wrong_tag_fails(self):
        self.fetch()
        run(["tag", "-d", "v1"], self.repo)
        with self.assertRaisesRegex(fetch_repos.FetchError, "expected v1"):
            self.fetch()


if __name__ == "__main__":
    unittest.main()
