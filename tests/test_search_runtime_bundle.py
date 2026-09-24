"""Runtime inventory and trusted receipt checks; no packaging or model download."""
import importlib.util
import json
import os
import plistlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    "search_bundle", Path(__file__).resolve().parents[1] / "scripts/build-search-runtime.py")
BUNDLE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUNDLE)


class SearchRuntimeBundleTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        worker = self.root / BUNDLE.NAME
        worker.write_bytes(b"fixture executable, not a real encoder")
        worker.chmod(0o700)
        self.receipt = {"schema": BUNDLE.SCHEMA, "platform": "Darwin", "architecture": "arm64",
                        "packages": BUNDLE.PINNED,
                        "sources": {name: BUNDLE.sha256(BUNDLE.SOURCE / name) for name in BUNDLE.SOURCES},
                        "files": BUNDLE.inventory(self.root)}
        self.publish()
        self.expected = BUNDLE.sha256(self.root / BUNDLE.RECEIPT)

    def publish(self):
        (self.root / BUNDLE.RECEIPT).write_text(json.dumps(self.receipt))

    def test_valid_and_changed_runtime(self):
        BUNDLE.verify(self.root, self.expected)
        (self.root / BUNDLE.NAME).write_bytes(b"changed")
        with self.assertRaises(ValueError):
            BUNDLE.verify(self.root, self.expected)
        # Rewriting the self-reported file inventory must not defeat the
        # independently supplied trusted build-receipt digest.
        self.receipt["files"] = BUNDLE.inventory(self.root)
        self.publish()
        with self.assertRaises(ValueError):
            BUNDLE.verify(self.root, self.expected)

    def test_extra_file_and_source_drift(self):
        (self.root / "unexpected").write_bytes(b"extra")
        with self.assertRaises(ValueError):
            BUNDLE.verify(self.root, self.expected)
        self.receipt["files"] = BUNDLE.inventory(self.root)
        self.receipt["sources"]["search_model_worker.py"] = "0" * 64
        self.publish()
        with self.assertRaises(ValueError):
            BUNDLE.verify(self.root, BUNDLE.sha256(self.root / BUNDLE.RECEIPT))

    def test_links_must_be_relative_and_remain_inside(self):
        (self.root / "internal").symlink_to(BUNDLE.NAME)
        self.assertEqual(BUNDLE.inventory(self.root)["internal"]["link"], BUNDLE.NAME)
        (self.root / "external").symlink_to(self.root / BUNDLE.NAME)
        with self.assertRaises(ValueError):
            BUNDLE.inventory(self.root)

    def test_missing_executable_and_symlinked_receipt(self):
        (self.root / BUNDLE.NAME).chmod(0o600)
        with self.assertRaises(ValueError):
            BUNDLE.verify(self.root, self.expected)
        receipt = self.root / BUNDLE.RECEIPT
        receipt.rename(self.root / "saved-receipt")
        receipt.symlink_to("saved-receipt")
        with self.assertRaises(ValueError):
            BUNDLE.verify(self.root, self.expected)

    def test_signed_copy_finalizes_inventory_and_preserves_source(self):
        worker = self.root / BUNDLE.NAME
        worker.write_bytes(b"\xcf\xfa\xed\xfe" + b"native fixture")
        framework = self.root / "Python.framework"
        framework.mkdir()
        library = framework / "Python"
        library.write_bytes(b"\xcf\xfa\xed\xfe" + b"library fixture")
        self.receipt["files"] = BUNDLE.inventory(self.root)
        self.publish()
        expected = BUNDLE.sha256(self.root / BUNDLE.RECEIPT)
        source_bytes = worker.read_bytes()
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory).resolve() / "signed"
            calls = []

            def codesign(arguments, **kwargs):
                calls.append(arguments)
                path = Path(arguments[-1])
                if "--force" in arguments:
                    if path.is_file():
                        path.write_bytes(path.read_bytes() + b"signature")
                    else:
                        (path / "seal").write_bytes(b"framework seal")

            with patch.object(BUNDLE.platform, "system", return_value="Darwin"), \
                    patch.object(BUNDLE.subprocess, "run", side_effect=codesign):
                result = BUNDLE.sign_copy(self.root, expected, output, "Fixture Identity", True, True)
            BUNDLE.verify(output, result["receiptSha256"])
            BUNDLE.verify(self.root, expected)
            self.assertEqual(worker.read_bytes(), source_bytes)
            self.assertNotEqual(result["receiptSha256"], expected)
            signed = [Path(call[-1]) for call in calls if "--force" in call]
            self.assertEqual(signed[-1], output / "Python.framework")
            self.assertEqual(len(signed), 3)
            self.assertTrue(all("--timestamp" in call and "runtime" in call
                                for call in calls if "--force" in call))
            with patch.object(BUNDLE.platform, "system", return_value="Darwin"):
                with self.assertRaises(ValueError):
                    BUNDLE.sign_copy(self.root, expected, output, "-")
                with self.assertRaises(ValueError):
                    BUNDLE.sign_copy(self.root, expected, self.root / "nested", "-")
                for hardened, timestamp in ((True, False), (False, True)):
                    rejected = Path(directory).resolve() / "rejected"
                    with self.assertRaises(ValueError):
                        BUNDLE.sign_copy(self.root, expected, rejected, "-", hardened, timestamp)
                    self.assertFalse(rejected.exists())

    def test_release_identity_and_options_fail_before_codesign(self):
        receipt = {"signing": {"identity": "Fixture Identity", "hardenedRuntime": False, "timestamp": False}}
        with patch.object(BUNDLE.subprocess, "run") as run:
            for identity, hardened, timestamp in (("Other", False, False),
                                                  ("Fixture Identity", True, False),
                                                  ("Fixture Identity", False, True)):
                with self.assertRaises(ValueError):
                    BUNDLE.verify_signatures(self.root, receipt, identity, hardened, timestamp)
            run.assert_not_called()

    def test_app_requires_configured_digest_and_never_rewrites_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory).resolve() / "Fixture.app"
            contents = app / "Contents"
            contents.mkdir(parents=True)
            plist = contents / "Info.plist"
            plist.write_bytes(plistlib.dumps({"LooksySearchRuntimeReceiptSHA256": ""}))
            self.assertFalse(BUNDLE.verify_app_signing(app, "-")["searchRuntime"])
            plist.write_bytes(plistlib.dumps({"LooksySearchRuntimeReceiptSHA256": self.expected}))
            with self.assertRaises(ValueError):
                BUNDLE.verify_app_signing(app, "-")
            runtime = contents / "Resources/search-runtime"
            runtime.parent.mkdir()
            BUNDLE.shutil.copytree(self.root, runtime)
            with patch.object(BUNDLE, "verify_signatures") as signatures:
                self.assertTrue(BUNDLE.verify_app_signing(app, "-")["preserved"])
                signatures.assert_called_once()
                before = (runtime / BUNDLE.RECEIPT).read_bytes()
                (runtime / BUNDLE.NAME).write_bytes(b"signed after build - wrong")
                with self.assertRaises(ValueError):
                    BUNDLE.verify_app_signing(app, "-")
                self.assertEqual((runtime / BUNDLE.RECEIPT).read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
