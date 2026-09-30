"""Regression checks for notice/source delivery failure cases."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile
import argparse
from unittest import mock

SCRIPT = Path(__file__).resolve().parents[1] / "prepare-dependency-materials.py"
spec = importlib.util.spec_from_file_location("dependency_materials", SCRIPT)
materials = importlib.util.module_from_spec(spec)
spec.loader.exec_module(materials)


class DependencyMaterialsTests(unittest.TestCase):
    def test_regeneration_removes_stale_materials(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            output = root / "build/materials"
            output.mkdir(parents=True)
            (output / "removed-version.zip").write_bytes(b"stale")
            args = argparse.Namespace(output=output, cache=root / "build/downloads")
            def generate(fresh_args):
                self.assertFalse((fresh_args.output / "removed-version.zip").exists())
                (fresh_args.output / "current.txt").write_bytes(b"current")
            with mock.patch.object(materials, "REPO", root), mock.patch.object(materials, "prepare_contents", side_effect=generate):
                materials.prepare(args)
            self.assertEqual([p.name for p in output.iterdir()], ["current.txt"])

    def test_failed_regeneration_preserves_previous_materials(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            output = root / "build/materials"
            output.mkdir(parents=True)
            (output / "previous.txt").write_bytes(b"previous")
            args = argparse.Namespace(output=output, cache=root / "build/downloads")
            with mock.patch.object(materials, "REPO", root), mock.patch.object(materials, "prepare_contents", side_effect=ValueError("failed preparation")):
                with self.assertRaisesRegex(ValueError, "failed preparation"):
                    materials.prepare(args)
            self.assertEqual((output / "previous.txt").read_bytes(), b"previous")

    def test_output_cannot_replace_build_root_or_download_cache(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with mock.patch.object(materials, "REPO", root):
                for output, cache in [(root / "build", root / "build/downloads"), (root / "build/materials", root / "build/materials/downloads")]:
                    with self.assertRaisesRegex(ValueError, "dedicated directory"):
                        materials.prepare(argparse.Namespace(output=output, cache=cache))

    def test_cached_archive_mismatch_is_rejected_without_download(self):
        with tempfile.TemporaryDirectory() as temp:
            cache = Path(temp)
            (cache / "source.zip").write_bytes(b"corrupted archive")
            record = {"name": "source.zip", "sha256": "0" * 64, "url": "https://invalid.example/unused"}
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                materials.fetch(record, cache)

    def test_archive_path_traversal_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(ValueError, "basename"):
                materials.fetch({"name": "../source.zip"}, Path(temp))

    def test_source_zip_preserves_bytes_and_is_deterministic(self):
        with tempfile.TemporaryDirectory() as temp:
            first, second = Path(temp) / "first.zip", Path(temp) / "second.zip"
            files = {"src/test.cpp": b"// exact source\r\n", "LICENSE": b"copyright\n"}
            materials.zip_files(first, files)
            materials.zip_files(second, dict(reversed(list(files.items()))))
            self.assertEqual(first.read_bytes(), second.read_bytes())
            with zipfile.ZipFile(first) as archive:
                self.assertEqual({n: archive.read(n) for n in archive.namelist()}, files)

    def test_comment_notice_retains_terms_and_origin(self):
        text = materials.license_headers({"src/codec.c": b"/* Copyright Example.\nPermission is hereby granted.\n*/\nint main() {}"})
        self.assertIn("src/codec.c", text)
        self.assertIn("Permission is hereby granted.", text)
        self.assertNotIn("int main", text)

    def test_source_only_repack_excludes_binaries_and_records_every_omission(self):
        files = {"src/customaction.cpp": b"source", "test/Microsoft.dll": b"binary", "font.ttf": b"font", "test.pfx": b"key", "LICENSE": b"license"}
        exclusions = {}
        actual = materials.source_only(files, exclusions, "source.zip")
        self.assertEqual(actual, {"src/customaction.cpp": b"source", "LICENSE": b"license"})
        self.assertEqual(exclusions["source.zip"], ["font.ttf", "test.pfx", "test/Microsoft.dll"])

    def test_xml_build_template_with_lib_suffix_is_preserved(self):
        files = {"mkspecs/Info.plist.lib": b"<?xml version='1.0'?><plist/>"}
        exclusions = {}
        self.assertEqual(materials.source_only(files, exclusions, "source.zip"), files)
        self.assertEqual(exclusions["source.zip"], [])


if __name__ == "__main__":
    unittest.main()
