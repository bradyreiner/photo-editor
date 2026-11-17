#!/usr/bin/env python3
"""Exercise immutable runtime preservation around a simulated Qt deployment."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "cmake/PreservePreparedAppRuntimes.cmake"


class PreparedRuntimeTests(unittest.TestCase):
    def run_deployment(self, root, operations):
        script = root / "deploy.cmake"
        script.write_text(
            f'set(QT_DEPLOY_PREFIX "{root.as_posix()}")\n'
            f'include("{MODULE.as_posix()}")\n' + operations)
        return subprocess.run(["cmake", "-P", str(script)],
                              text=True, capture_output=True)

    def fixture(self, root):
        resources = root / "Looksy.app/Contents/Resources"
        for name in ("agent-runtime", "search-runtime"):
            runtime = resources / name
            runtime.mkdir(parents=True)
            (runtime / "worker").write_bytes(b"prepared-native-bytes")
            (runtime / "receipt.json").write_bytes(b'{"trusted":"original"}\n')
            (runtime / "worker-link").symlink_to("worker")
        return resources

    def test_deployer_cannot_mutate_prepared_bytes_or_links(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            resources = self.fixture(root)
            result = self.run_deployment(root, '''
photo_editor_preserve_prepared_app_runtimes("Looksy.app")
if(EXISTS "${QT_DEPLOY_PREFIX}/Looksy.app/Contents/Resources/search-runtime/worker")
  message(FATAL_ERROR "Search runtime remained visible to the deployer")
endif()
if(EXISTS "${QT_DEPLOY_PREFIX}/Looksy.app/Contents/Resources/agent-runtime/worker")
  message(FATAL_ERROR "Agent runtime remained visible to the deployer")
endif()
file(WRITE "${QT_DEPLOY_PREFIX}/Looksy.app/Contents/Resources/qt-deployed.txt" "Qt libraries")
photo_editor_restore_prepared_app_runtimes("Looksy.app")
''')
            self.assertEqual(result.returncode, 0, result.stderr)
            for name in ("agent-runtime", "search-runtime"):
                self.assertEqual((resources / name / "worker").read_bytes(), b"prepared-native-bytes")
                self.assertEqual((resources / name / "receipt.json").read_bytes(), b'{"trusted":"original"}\n')
                self.assertTrue((resources / name / "worker-link").is_symlink())
                self.assertEqual((resources / name / "worker-link").readlink(), Path("worker"))
            self.assertFalse((root / "Looksy.app.prepared-runtimes").exists())
            self.assertEqual((resources / "qt-deployed.txt").read_text(), "Qt libraries")

    def test_stale_staging_is_preserved_and_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            resources = self.fixture(root)
            stash = root / "Looksy.app.prepared-runtimes"
            stash.mkdir()
            (stash / "retain").write_text("previous artifact")
            result = self.run_deployment(root, 'photo_editor_preserve_prepared_app_runtimes("Looksy.app")\n')
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual((stash / "retain").read_text(), "previous artifact")
            self.assertTrue((resources / "search-runtime/worker").exists())

    def test_restore_refuses_to_overwrite_a_changed_destination(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.fixture(root)
            result = self.run_deployment(root, '''
photo_editor_preserve_prepared_app_runtimes("Looksy.app")
file(MAKE_DIRECTORY "${QT_DEPLOY_PREFIX}/Looksy.app/Contents/Resources/search-runtime")
file(WRITE "${QT_DEPLOY_PREFIX}/Looksy.app/Contents/Resources/search-runtime/new" "retain")
photo_editor_restore_prepared_app_runtimes("Looksy.app")
''')
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue((root / "Looksy.app.prepared-runtimes/search-runtime/worker").exists())
            self.assertEqual((root / "Looksy.app/Contents/Resources/search-runtime/new").read_text(), "retain")

    def test_no_optional_runtimes_is_a_noop(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            result = self.run_deployment(root, '''
photo_editor_preserve_prepared_app_runtimes("Looksy.app")
photo_editor_restore_prepared_app_runtimes("Looksy.app")
''')
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse((root / "Looksy.app.prepared-runtimes").exists())


if __name__ == "__main__":
    unittest.main()
