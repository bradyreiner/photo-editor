"""Pinned-artifact verification tests; synthetic bytes, no downloads or models."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location(
    "search_model_installer", Path(__file__).resolve().parents[1] / "scripts/install-search-model.py")
INSTALLER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INSTALLER)


class InstallerTest(unittest.TestCase):
    def test_both_upstream_digest_formats(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "artifact"
            payload = b"exact pinned bytes\x00\x01"
            path.write_bytes(payload)
            sha = hashlib.sha256(payload).hexdigest()
            git = hashlib.sha1(f"blob {len(payload)}\0".encode() + payload).hexdigest()
            self.assertEqual(INSTALLER.verify_file(path, (len(payload), "sha256", sha)), sha)
            self.assertEqual(INSTALLER.verify_file(path, (len(payload), "git-sha1", git)), sha)
            for spec in ((len(payload) + 1, "sha256", sha), (len(payload), "sha256", "0" * 64)):
                with self.assertRaises(ValueError):
                    INSTALLER.verify_file(path, spec)
            path.write_bytes(b"x" * len(payload))
            with self.assertRaises(ValueError):
                INSTALLER.verify_file(path, (len(payload), "sha256", sha))

    def test_missing_and_symlink_artifacts_fail(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaises(ValueError):
                INSTALLER.verify_model(root)
            file = root / "actual"
            file.write_bytes(b"data")
            link = root / "link"
            link.symlink_to(file)
            with self.assertRaises(ValueError):
                INSTALLER.verify_file(link, (4, "sha256", hashlib.sha256(b"data").hexdigest()))
            with self.assertRaises(ValueError):
                INSTALLER.verify_model(Path("relative-root"))


if __name__ == "__main__":
    unittest.main()
