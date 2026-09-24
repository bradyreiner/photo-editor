#!/usr/bin/env python3
"""Exercise dependency verification with thin and universal otool output."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BundleDependencyTests(unittest.TestCase):
    def verify(self, dependency):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            app = root / "Looksy.app"
            binary = app / "Contents/MacOS/Looksy"
            binary.parent.mkdir(parents=True)
            binary.write_text("fixture")
            binary.chmod(0o755)
            (root / "outside.dylib").write_text("outside bundle")
            tools = root / "tools"
            tools.mkdir()
            (tools / "file").write_text("#!/bin/sh\nprintf 'Mach-O universal binary\\n'\n")
            (tools / "otool").write_text('''#!/bin/sh
case "$1" in
  -L)
    printf '%s (architecture x86_64):\\n\\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0)\\n' "$2"
    printf '%s (architecture arm64):\\n\\t%s (compatibility version 1.0.0)\\n' "$2" "$FIXTURE_DEPENDENCY"
    ;;
  -D) printf '%s:\\n' "$2" ;;
esac
''')
            for tool in tools.iterdir():
                tool.chmod(0o755)
            env = dict(os.environ, PATH=f"{tools}:{os.environ['PATH']}", FIXTURE_DEPENDENCY=dependency)
            return subprocess.run(["bash", str(ROOT / "scripts/verify-macos-bundle-dependencies.sh"), str(app)],
                                  env=env, capture_output=True, text=True)

    def test_universal_architecture_headers_are_not_dependencies(self):
        result = self.verify("/usr/lib/libc++.1.dylib")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("dependencies=2", result.stdout)

    def test_second_architecture_external_dependency_still_rejected(self):
        result = self.verify("/opt/homebrew/lib/unbundled.dylib")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unbundled.dylib", result.stderr)

    def test_loader_path_cannot_escape_bundle_to_existing_library(self):
        result = self.verify("@loader_path/../../../outside.dylib")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("outside.dylib", result.stderr)


if __name__ == "__main__":
    unittest.main()
