#!/usr/bin/env python3
"""Load the real packaged QML in a private, windowless application instance."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--timeout", type=float, default=45)
    args = parser.parse_args()
    app = args.app.resolve()
    if not app.is_file():
        parser.error(f"application does not exist: {app}")

    with tempfile.TemporaryDirectory(prefix="looksy-packaged-qml-") as temporary:
        root = Path(temporary)
        env = os.environ.copy()
        env.update({
            "QT_QPA_PLATFORM": "offscreen",
            "QT_QUICK_BACKEND": "software",
            "QML_DISABLE_DISK_CACHE": "1",
            "PHOTO_EDITOR_APP_DATA_DIR": str(root / "data"),
            "PHOTO_EDITOR_APP_DATA_ROOT": str(root / "data"),
            "PHOTO_EDITOR_CACHE_DIR": str(root / "cache"),
            "PHOTO_EDITOR_PREFERENCES_DIR": str(root / "preferences"),
            "PHOTO_EDITOR_MANAGED_MEDIA_DIR": str(root / "media"),
            "PHOTO_EDITOR_BACKUP_DIR": str(root / "backups"),
            "PHOTO_EDITOR_ORIGINALS_BACKUP_DIR": str(root / "backups"),
            "PHOTO_EDITOR_LOG_DIR": str(root / "logs"),
            "PHOTO_EDITOR_OBSERVABILITY_RUN_DIR": str(root / "observability"),
            "PHOTO_EDITOR_DATABASE_URL": "postgresql://127.0.0.1:1/packaged_qml_test",
            "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE": "1",
            "PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY": "1",
            "PHOTO_EDITOR_LOG_LEVEL": "info",
        })
        output = root / "process.log"
        ready_at = None
        passed = False
        with output.open("w") as stream:
            process = subprocess.Popen([str(app)], env=env, stdout=stream,
                                       stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + args.timeout
                while time.monotonic() < deadline:
                    log = output.read_text(errors="replace")
                    if process.poll() is not None:
                        break
                    if ("QQmlApplicationEngine failed to load component" in log or
                            "desktop.startup.qml_failed" in log):
                        break
                    if "desktop.startup.ready" in log and "qml.route.ready" in log:
                        if ready_at is None:
                            ready_at = time.monotonic()
                        if time.monotonic() - ready_at >= 1:
                            passed = True
                            break
                    time.sleep(0.1)
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
        if not passed:
            print(output.read_text(errors="replace"))
            print("FAIL: packaged QML did not load and remain alive through route readiness.")
            return 1
        print("PASS: packaged QML loaded; desktop and QML route ready; process remained alive.")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
