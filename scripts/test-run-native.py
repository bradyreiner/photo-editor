#!/usr/bin/env python3
"""Exercise launcher orchestration without touching the user's app or catalog."""

import fcntl
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest


class LauncherTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="launcher-proof-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "checkout [local] (test)"
        self.root.mkdir()
        (self.root / "scripts").mkdir()
        source = Path(__file__).resolve().parent
        shutil.copy(source / "run-native.sh", self.root / "scripts/run-native.sh")
        shutil.copy(source / "build-lock.sh", self.root / "scripts/build-lock.sh")
        shutil.copy(source.parent / "PhotoEditor.command", self.root / "PhotoEditor.command")
        self.bin = self.root / "test-bin"
        self.bin.mkdir()
        self.log = self.root / "calls.jsonl"
        self.stub = "#!" + sys.executable + "\n" + r'''
import json, os, pathlib, sys, time
root = pathlib.Path(os.environ["TEST_REPO"])
tool = pathlib.Path(sys.argv[0]).name
args = sys.argv[1:]
with open(root / "calls.jsonl", "a") as f:
    f.write(json.dumps({"tool": tool, "args": args,
        "database": os.environ.get("PHOTO_EDITOR_DATABASE_URL", ""),
        "data": os.environ.get("PHOTO_EDITOR_APP_DATA_DIR", "")}) + "\n")
if os.environ.get("TEST_FAIL") == tool:
    sys.exit(23)
if os.environ.get("TEST_BLOCK") == tool:
    (root / "build-blocked").touch()
    while not (root / "release-build").exists():
        time.sleep(0.02)
if tool == "cmake":
    preset = args[args.index("--preset") + 1]
    build = root / "build" / preset.removeprefix("build-")
    build.mkdir(parents=True, exist_ok=True)
    if "--build" not in args:
        (build / "CMakeCache.txt").write_text("PHOTO_EDITOR_RUST_BINARY_DIR:PATH=" + str(root / "rust/target/debug") + "\n")
        (build / "build.ninja").touch()
    else:
        app = build / "apps/desktop/Looksy.app/Contents/MacOS/Looksy"
        app.parent.mkdir(parents=True, exist_ok=True)
        app.write_text(pathlib.Path(__file__).read_text())
        app.chmod(0o755)
if tool == "pgrep":
    if os.environ.get("TEST_RUNNING"):
        print("424242")
        sys.exit(0)
    sys.exit(1)
'''
        for tool in ("cmake", "cargo", "ninja", "pgrep"):
            self.write_stub(self.bin / tool)
        for tool in ("catalog_local_db", "catalog_migrate"):
            self.write_stub(self.root / "rust/target/debug" / tool)
        self.env = {**os.environ, "PATH": str(self.bin) + ":/usr/bin:/bin",
                    "TEST_REPO": str(self.root), "PHOTO_EDITOR_NATIVE_LOG_STYLE": "plain",
                    "PHOTO_EDITOR_APP_DATA_DIR": str(self.root / "runtime")}
        (self.bin / "python3").symlink_to(sys.executable)
        for key in ("PHOTO_EDITOR_DATABASE_URL", "PHOTO_EDITOR_LOG_DIR", "PHOTO_EDITOR_BACKUP_DIR",
                    "TEST_FAIL", "TEST_RUNNING", "TEST_STUCK", "TEST_BLOCK", "BASH_ENV"):
            self.env.pop(key, None)

    def write_stub(self, path):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(self.stub)
        path.chmod(0o755)

    def run_launcher(self, *args, wrapper=False):
        entry = "PhotoEditor.command" if wrapper else "scripts/run-native.sh"
        return subprocess.run(["/bin/bash", str(self.root / entry), *args],
                              env=self.env, text=True, capture_output=True, timeout=20)

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def test_default_builds_only_app_and_preserves_arguments(self):
        result = self.run_launcher("--", "a photo.jpg", "--literal", "$(not-a-command)")
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = self.calls()
        self.assertEqual([c["tool"] for c in calls],
                         ["cargo", "cmake", "cmake", "catalog_local_db", "pgrep", "Looksy"])
        self.assertEqual(calls[2]["args"], ["--build", "--preset", "build-release-macos",
                                            "--target", "photo_editor_app"])
        self.assertEqual(calls[3]["args"], ["ensure-running"])
        self.assertEqual(calls[-1]["args"], ["a photo.jpg", "--literal", "$(not-a-command)"])
        self.assertIn(r"checkout \[local\] \(test\)", calls[-2]["args"][-1])
        pattern = calls[-2]["args"][-1]
        for preset in ("dev-macos-debug", "release-macos"):
            executable = self.root / "build" / preset / "apps/desktop/Looksy.app/Contents/MacOS/Looksy"
            self.assertIsNotNone(re.search(pattern, str(executable)))
            self.assertIsNone(re.search(pattern, str(executable) + "-other"))
        self.assertIsNone(re.search(pattern, "/other-checkout/build/release-macos/apps/desktop/Looksy.app/Contents/MacOS/Looksy"))

    def test_reuses_configuration_and_can_force_refresh(self):
        self.assertEqual(self.run_launcher("--build-only").returncode, 0)
        self.log.unlink()
        self.assertEqual(self.run_launcher("--build-only").returncode, 0)
        self.assertEqual([c["tool"] for c in self.calls()], ["cargo", "cmake"])
        self.log.unlink()
        self.assertEqual(self.run_launcher("--build-only", "--configure").returncode, 0)
        self.assertEqual([c["tool"] for c in self.calls()], ["cargo", "cmake", "cmake"])
        self.assertFalse((self.root / "runtime").exists())

    def test_release_and_jobs(self):
        result = self.run_launcher("--release", "--build-only", "--jobs", "4")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.calls()[0]["args"][-2:], ["--jobs", "4"])
        self.assertEqual(self.calls()[-1]["args"], ["--build", "--preset", "build-release-macos",
                                                  "--target", "photo_editor_app", "--parallel", "4"])

    def test_debug_remains_available(self):
        result = self.run_launcher("--debug", "--build-only")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.calls()[-1]["args"], ["--build", "--preset", "build-dev-macos-debug",
                                                  "--target", "photo_editor_app"])

    def test_no_build_needs_no_compiler_tools_and_wrapper_forwards_options(self):
        self.assertEqual(self.run_launcher("--build-only").returncode, 0)
        self.log.unlink()
        for tool in ("cmake", "cargo", "ninja"):
            (self.bin / tool).unlink()
        self.env["PHOTO_EDITOR_DATABASE_URL"] = "postgresql://test.invalid/isolated"
        result = self.run_launcher("--no-build", "--", "photo with spaces.jpg", wrapper=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([c["tool"] for c in self.calls()], ["catalog_migrate", "pgrep", "Looksy"])
        self.assertTrue(all(c["database"] == self.env["PHOTO_EDITOR_DATABASE_URL"] for c in self.calls()))
        self.assertEqual(self.calls()[-1]["args"], ["photo with spaces.jpg"])

    def test_build_failure_does_not_touch_runtime_or_restart(self):
        self.env["TEST_FAIL"] = "cmake"
        result = self.run_launcher()
        self.assertEqual(result.returncode, 23)
        self.assertEqual([c["tool"] for c in self.calls()], ["cargo", "cmake"])
        self.assertFalse((self.root / "runtime").exists())
        self.env.pop("TEST_FAIL")
        self.assertEqual(self.run_launcher("--build-only").returncode, 0)

    def test_overlapping_build_rejected_before_cargo_and_debug_independent(self):
        env = {**self.env, "TEST_BLOCK": "cargo"}
        first = subprocess.Popen(
            ["/bin/bash", str(self.root / "scripts/run-native.sh"), "--build-only"],
            env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 10
            while not (self.root / "build-blocked").exists():
                self.assertIsNone(first.poll(), "first build exited before Cargo")
                self.assertLess(time.monotonic(), deadline, "first build never reached Cargo")
                time.sleep(0.02)
            blocked = self.run_launcher("--build-only")
            self.assertEqual(blocked.returncode, 1, blocked.stderr)
            self.assertIn("A build is already running", blocked.stderr)
            self.assertEqual([c["tool"] for c in self.calls()], ["cargo"])
            debug = self.run_launcher("--debug", "--build-only")
            self.assertEqual(debug.returncode, 0, debug.stderr)
        finally:
            (self.root / "release-build").touch()
            stdout, stderr = first.communicate(timeout=10)
        self.assertEqual(first.returncode, 0, stdout + stderr)
        self.assertEqual(self.run_launcher("--build-only").returncode, 0)

    def test_build_lock_released_before_app_launch(self):
        env = {**self.env, "TEST_BLOCK": "Looksy"}
        first = subprocess.Popen(
            ["/bin/bash", str(self.root / "scripts/run-native.sh")],
            env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 10
            while not (self.root / "build-blocked").exists():
                self.assertIsNone(first.poll(), "launcher exited before app launch")
                self.assertLess(time.monotonic(), deadline, "launcher never reached app")
                time.sleep(0.02)
            result = self.run_launcher("--build-only")
            self.assertEqual(result.returncode, 0, result.stderr)
        finally:
            (self.root / "release-build").touch()
            first.communicate(timeout=10)
        self.assertEqual(first.returncode, 0)

    def test_killed_launcher_keeps_lock_until_build_child_exits(self):
        env = {**self.env, "TEST_BLOCK": "cargo"}
        first = subprocess.Popen(
            ["/bin/bash", str(self.root / "scripts/run-native.sh"), "--build-only"],
            env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 10
            while not (self.root / "build-blocked").exists():
                self.assertIsNone(first.poll(), "first build exited before Cargo")
                self.assertLess(time.monotonic(), deadline, "first build never reached Cargo")
                time.sleep(0.02)
            first.kill()
            first.wait(timeout=5)
            blocked = self.run_launcher("--build-only")
            self.assertEqual(blocked.returncode, 1, blocked.stderr)
            self.assertIn("A build is already running", blocked.stderr)
        finally:
            (self.root / "release-build").touch()
            first.communicate(timeout=10)
        self.assertEqual(self.run_launcher("--build-only").returncode, 0)

    def test_packaging_scripts_share_launcher_lock(self):
        source = Path(__file__).resolve().parent
        for name in ("build-release.sh", "prepare-sales-candidate.sh"):
            shutil.copy(source / name, self.root / "scripts" / name)
        (self.root / "VERSION").write_text("0.1.0\n")
        for tool in ("git", "cpack"):
            self.write_stub(self.bin / tool)
        # Exercise the macOS packaging entry points even on a non-macOS test host.
        (self.bin / "uname").write_text(
            '#!/bin/bash\nif [ "$1" = "-m" ]; then echo arm64; else echo Darwin; fi\n')
        (self.bin / "uname").chmod(0o755)
        env = {**self.env, "CMAKE_PREFIX_PATH": "/fixture/qt"}
        build = self.root / "build/release-macos"
        build.mkdir(parents=True)
        with (build / ".looksy-build.lock").open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            for name, args in (
                ("build-release.sh", ["--allow-dirty", "--skip-tests"]),
                ("prepare-sales-candidate.sh", [str(self.root / "candidate")]),
            ):
                with self.subTest(script=name):
                    result = subprocess.run(
                        ["/bin/bash", str(self.root / "scripts" / name), *args],
                        env=env, text=True, capture_output=True, timeout=10)
                    self.assertEqual(result.returncode, 1, result.stderr)
                    self.assertIn("A build is already running", result.stderr)
        self.assertEqual(self.calls(), [])
        self.assertFalse((self.root / "candidate").exists())
        self.assertEqual(self.run_launcher("--build-only").returncode, 0)

    def test_catalog_failure_prevents_restart(self):
        self.env["TEST_FAIL"] = "catalog_local_db"
        result = self.run_launcher()
        self.assertEqual(result.returncode, 23)
        self.assertNotIn("pgrep", [c["tool"] for c in self.calls()])
        self.assertNotIn("Looksy", [c["tool"] for c in self.calls()])

    def test_process_lookup_failure_prevents_launch(self):
        self.env["TEST_FAIL"] = "pgrep"
        result = self.run_launcher()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Could not check", result.stderr)
        self.assertNotIn("Looksy", [c["tool"] for c in self.calls()])

    def simulate_running_app(self, stuck=False):
        # Override shell builtins in this fixture only; never signal a real PID.
        shell_env = self.root / "shell-env.sh"
        shell_env.write_text('''kill() {
  printf '%s\\n' "$*" >> "$TEST_REPO/signals"
  [ "$1" = "-0" ] || return 0
  [ "${TEST_STUCK:-0}" = "1" ] && return 0
  count="$(grep -c -- '^-0 ' "$TEST_REPO/signals")"
  [ "$count" -lt 3 ]
}
sleep() { :; }
''')
        self.env["BASH_ENV"] = str(shell_env)
        self.env["TEST_RUNNING"] = "1"
        self.env["TEST_STUCK"] = "1" if stuck else "0"

    def test_restart_waits_for_exit(self):
        self.simulate_running_app()
        result = self.run_launcher()
        self.assertEqual(result.returncode, 0, result.stderr)
        signals = (self.root / "signals").read_text().splitlines()
        self.assertEqual(signals, ["424242", "-0 424242", "-0 424242", "-0 424242"])
        self.assertEqual(self.calls()[-1]["tool"], "Looksy")

    def test_stalled_shutdown_prevents_second_instance_without_forcing_exit(self):
        self.simulate_running_app(stuck=True)
        result = self.run_launcher()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("has not exited", result.stderr)
        signals = (self.root / "signals").read_text().splitlines()
        self.assertEqual(signals[0], "424242")
        self.assertTrue(all(s == "-0 424242" for s in signals[1:]))
        self.assertNotIn("Looksy", [c["tool"] for c in self.calls()])

    def test_help_invalid_options_and_missing_binary_do_not_build(self):
        self.assertEqual(self.run_launcher("--help").returncode, 0)
        for args in [("--wat",), ("--jobs",), ("--jobs", "0"), ("--jobs", "two"),
                     ("--no-build", "--build-only"), ("--no-build", "--configure")]:
            self.assertEqual(self.run_launcher(*args).returncode, 2, args)
        self.assertNotEqual(self.run_launcher("--no-build").returncode, 0)
        self.assertEqual(self.calls(), [])
        self.assertFalse((self.root / "runtime").exists())


if __name__ == "__main__":
    unittest.main()
