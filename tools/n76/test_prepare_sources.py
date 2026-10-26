"""Offline checks for the source manifest, verified cache, and preparation gate."""

import hashlib
import importlib.util
import os
from pathlib import Path
import stat
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


MODULE_PATH = Path(__file__).with_name("prepare_sources.py")
SPEC = importlib.util.spec_from_file_location("n76_prepare_sources_tested", MODULE_PATH)
prepare = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(prepare)


class SourcePreparationTests(unittest.TestCase):
    def test_plan_is_exact_and_has_all_three_modules(self):
        with patch.object(prepare, "urlopen", side_effect=AssertionError("network in plan")):
            for model, core in (("DH2600", "leds-mcu-n76e003-dh2600.ko"),
                                ("DXP480T Plus", "leds-mcu-n76e003.ko")):
                plan = prepare.source_plan(model)
                self.assertEqual(plan["model"], model)
                self.assertEqual(plan["modules"], [core, "ledtrig-normal-n76e003.ko",
                                                   "ledtrig-breath-n76e003.ko"])
                for source in plan["sources"]:
                    self.assertIn("/" + plan["upstream_commit"] + "/", source["url"])
                    self.assertEqual(len(source["sha256"]), 64)
            for model in ("DH2600 ", "dh2600", "DXP480T", "DXP4800 Plus", ""):
                with self.assertRaises(prepare.PreparationError):
                    prepare.source_plan(model)

    def test_cache_identity_is_verified_and_original_is_preserved(self):
        original = b"synthetic source fixture\n"
        source = {"filename": "fixture.c", "url": "https://invalid.example/fixture.c",
                  "sha256": hashlib.sha256(original).hexdigest()}
        with tempfile.TemporaryDirectory() as directory:
            cache = Path(directory)
            target = cache / source["filename"]
            target.write_bytes(original)
            with patch.object(prepare, "urlopen", side_effect=AssertionError("cached download")):
                self.assertEqual(prepare._read_pinned(source, cache, False), original)
                target.write_bytes(b"changed\n")
                with self.assertRaisesRegex(prepare.PreparationError, "SHA-256 mismatch"):
                    prepare._read_pinned(source, cache, True)
            self.assertEqual(target.read_bytes(), b"changed\n")

    def test_offline_missing_source_does_not_create_output_or_cache(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with patch.object(prepare, "urlopen", side_effect=AssertionError("offline network")):
                with self.assertRaisesRegex(prepare.PreparationError, "offline cache"):
                    prepare.prepare_sources("DH2600", root / "cache", root / "output", allow_download=False)
            self.assertFalse((root / "cache").exists())
            self.assertFalse((root / "output").exists())

    def test_output_cannot_overwrite_files_or_overlap_originals(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            occupied = root / "occupied"
            occupied.mkdir()
            marker = occupied / "keep"
            marker.write_bytes(b"unchanged")
            with patch.object(prepare, "urlopen", side_effect=AssertionError("invalid output download")):
                for cache, output in ((root / "cache", occupied),
                                      (root / "cache", root / "cache"),
                                      (root / "cache", root / "cache" / "child"),
                                      (root / "output" / "cache", root / "output")):
                    with self.assertRaises(prepare.PreparationError):
                        prepare.prepare_sources("DH2600", cache, output)
            self.assertEqual(marker.read_bytes(), b"unchanged")

    def test_wrong_download_is_not_cached(self):
        source = {"filename": "fixture.c", "url": "https://invalid.example/fixture.c",
                  "sha256": hashlib.sha256(b"expected").hexdigest()}

        class Response:
            def __enter__(self):
                return self

            def __exit__(self, *args):
                return False

            def geturl(self):
                return source["url"]

            def read(self, limit):
                return b"unexpected download"

        with tempfile.TemporaryDirectory() as directory:
            cache = Path(directory) / "cache"
            with patch.object(prepare, "urlopen", return_value=Response()):
                with self.assertRaisesRegex(prepare.PreparationError, "SHA-256 mismatch"):
                    prepare._read_pinned(source, cache, True)
            self.assertFalse(cache.exists())

    def test_nonregular_cache_entry_is_rejected_before_open(self):
        fifo = SimpleNamespace(st_mode=stat.S_IFIFO | 0o600, st_nlink=1, st_size=0)
        with patch.object(Path, "lstat", return_value=fifo):
            with patch.object(prepare.os, "open", side_effect=AssertionError("must not open FIFO")):
                with self.assertRaisesRegex(prepare.PreparationError, "regular file"):
                    prepare._cached_bytes(Path("fifo.c"))

    def test_hardlinked_and_oversized_cache_files_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = root / "original.c"
            original.write_bytes(b"small file")
            linked = root / "linked.c"
            os.link(original, linked)
            with self.assertRaisesRegex(prepare.PreparationError, "one link"):
                prepare._cached_bytes(linked)
            large = root / "large.c"
            with large.open("wb") as stream:
                stream.truncate(prepare.MAX_SOURCE_BYTES + 1)
            with self.assertRaisesRegex(prepare.PreparationError, "small regular file"):
                prepare._cached_bytes(large)

    def test_source_changed_during_read_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "source.c"
            target.write_bytes(b"before")
            real_read = os.read
            changed = False

            def changing_read(descriptor, limit):
                nonlocal changed
                data = real_read(descriptor, limit)
                if not changed:
                    changed = True
                    target.write_bytes(b"changed and longer")
                return data

            with patch.object(prepare.os, "read", side_effect=changing_read):
                with self.assertRaisesRegex(prepare.PreparationError, "changed during read"):
                    prepare._cached_bytes(target)

    def test_windows_path_descriptor_ctime_difference_keeps_posix_guard(self):
        fields = dict(st_dev=1, st_ino=2, st_mode=stat.S_IFREG | 0o600,
                      st_nlink=1, st_size=20, st_mtime_ns=30, st_uid=0,
                      st_birthtime_ns=10)
        path_info = SimpleNamespace(**fields, st_ctime_ns=10)
        descriptor_info = SimpleNamespace(**fields, st_ctime_ns=40)
        with patch.object(prepare.os, "name", "nt"):
            self.assertEqual(prepare._file_signature(path_info), prepare._file_signature(descriptor_info))
        with patch.object(prepare.os, "name", "posix"):
            self.assertNotEqual(prepare._file_signature(path_info), prepare._file_signature(descriptor_info))

    def test_symlink_and_broken_symlink_paths_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            link = root / "cache"
            try:
                link.symlink_to(root / "missing", target_is_directory=True)
            except OSError as error:
                self.skipTest("Symlink creation not available: " + str(error))
            with self.assertRaisesRegex(prepare.PreparationError, "symlink"):
                prepare.prepare_sources("DH2600", link, root / "output", allow_download=False)
            with self.assertRaisesRegex(prepare.PreparationError, "symlink"):
                prepare.prepare_sources("DH2600", root / "originals", link, allow_download=False)

    @unittest.skipUnless(os.name == "posix", "POSIX permission check")
    def test_cache_must_be_private(self):
        with tempfile.TemporaryDirectory() as directory:
            cache = Path(directory) / "cache"
            cache.mkdir(mode=0o755)
            cache.chmod(0o755)
            with self.assertRaisesRegex(prepare.PreparationError, "private 0700"):
                prepare._validate_cache_directory(cache)

    def test_patch_anchors_fail_closed_if_missing_duplicated_or_reordered(self):
        for source in ("missing", "anchor anchor"):
            with self.assertRaisesRegex(prepare.PreparationError, "anchor mismatch"):
                prepare._replace_once(source, "anchor", "replacement")
        with self.assertRaisesRegex(prepare.PreparationError, "out of order"):
            prepare._replace_section("end then start", "start", "end", "replacement")


if __name__ == "__main__":
    unittest.main()
