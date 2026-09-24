#!/usr/bin/env python3
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("runtime", Path(__file__).with_name("prepare-agent-runtime.py"))
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


class RuntimePreparationTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="agent-runtime-test-")
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.source.mkdir()
        for name in ("ollama", "llama-server"):
            path = self.source / name
            path.write_bytes(b"isolated runtime fixture\n")
            path.chmod(0o755)
        self.license = self.root / "LICENSE"
        self.license.write_text("test-only license\n")
        (self.source / "OLLAMA_LICENSE").write_bytes(self.license.read_bytes())
        (self.source / "runtime-link").symlink_to("ollama")
        self.archive = self.root / "runtime.tgz"
        with tarfile.open(self.archive, "w:gz") as output:
            for path in self.source.iterdir():
                output.add(path, arcname=path.name)
        canonical = (json.dumps(runtime.inventory(self.source), sort_keys=True, separators=(",", ":")) + "\n").encode()
        self.lock = {"schema": "framehouse-agent-runtime-lock-v1", "version": "test",
                     "archive": {"sha256": runtime.sha256(self.archive), "bytes": self.archive.stat().st_size},
                     "licenseSha256": runtime.sha256(self.license),
                     "fileInventorySha256": hashlib.sha256(canonical).hexdigest()}
        self.output = self.root / "installed"

    def tearDown(self):
        self.temp.cleanup()

    def install(self):
        runtime.stage(self.archive, self.license, self.output, self.lock)

    def test_verified_runtime_and_relative_link(self):
        self.install()
        self.assertEqual(runtime.verify(self.output, self.lock)["lock"], self.lock)
        self.assertEqual((self.output / "runtime-link").resolve(), (self.output / "ollama").resolve())

    def test_modified_binary_and_forged_receipt_rejected(self):
        self.install()
        (self.output / "ollama").write_bytes(b"modified executable")
        receipt_path = self.output / runtime.RECEIPT
        receipt = json.loads(receipt_path.read_text())
        receipt["files"] = runtime.inventory(self.output)
        receipt_path.write_text(json.dumps(receipt))
        with self.assertRaisesRegex(ValueError, "pinned release"):
            runtime.verify(self.output, self.lock)

    def test_extra_payload_rejected(self):
        self.install()
        (self.output / "unlisted.dylib").write_bytes(b"unexpected")
        with self.assertRaises(ValueError):
            runtime.verify(self.output, self.lock)

    def test_archive_corruption_and_existing_output_preserved(self):
        self.archive.write_bytes(b"corrupt")
        with self.assertRaisesRegex(ValueError, "checksum or size"):
            self.install()
        self.assertFalse(self.output.exists())
        self.output.mkdir()
        sentinel = self.output / "original"
        sentinel.write_text("preserve me")
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.install()
        self.assertEqual(sentinel.read_text(), "preserve me")

    def test_archive_escape_rejected_without_publication(self):
        with tarfile.open(self.archive, "w:gz") as output:
            entry = tarfile.TarInfo("../escape")
            entry.size = 4
            output.addfile(entry, io.BytesIO(b"nope"))
        self.lock["archive"] = {"sha256": runtime.sha256(self.archive), "bytes": self.archive.stat().st_size}
        with self.assertRaises(tarfile.TarError):
            self.install()
        self.assertFalse(self.output.exists())
        self.assertFalse((self.root / "escape").exists())


if __name__ == "__main__":
    unittest.main()
