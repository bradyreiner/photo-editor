#!/usr/bin/env python3
"""Self-test the post-macdeployqt release-runtime pruning hook."""

from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
PRUNER = REPO_ROOT / "cmake" / "PruneQtMacosRuntime.cmake"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        bundle = root / "Looksy.app"
        removable = (
            "Contents/PlugIns/imageformats/libqpdf.dylib",
            "Contents/Frameworks/QtPdf.framework/Versions/A/QtPdf",
            "Contents/PlugIns/imageformats/libqicns.dylib",
            "Contents/PlugIns/imageformats/libqico.dylib",
            "Contents/PlugIns/imageformats/libqjp2.dylib",
            "Contents/PlugIns/imageformats/libqmng.dylib",
            "Contents/PlugIns/imageformats/libqtga.dylib",
            "Contents/PlugIns/imageformats/libqwbmp.dylib",
            "Contents/Frameworks/libmng.2.dylib",
            "Contents/PlugIns/platforminputcontexts/libqtvirtualkeyboardplugin.dylib",
            "Contents/Frameworks/QtVirtualKeyboard.framework/Versions/A/QtVirtualKeyboard",
            "Contents/Frameworks/QtVirtualKeyboardQml.framework/Versions/A/QtVirtualKeyboardQml",
            "Contents/PlugIns/networkinformation/libqglib.dylib",
            "Contents/Frameworks/libgio-2.0.0.dylib",
            "Contents/Frameworks/libgobject-2.0.0.dylib",
            "Contents/Frameworks/libgmodule-2.0.0.dylib",
            "Contents/PlugIns/tls/libqopensslbackend.dylib",
        )
        retained = (
            "Contents/PlugIns/imageformats/libqjpeg.dylib",
            "Contents/PlugIns/imageformats/libqgif.dylib",
            "Contents/PlugIns/networkinformation/libqapplenetworkinformation.dylib",
            "Contents/PlugIns/styles/libqmacstyle.dylib",
            "Contents/Frameworks/QtWidgets.framework/Versions/A/QtWidgets",
            "Contents/Frameworks/libglib-2.0.0.dylib",
            "Contents/Frameworks/libgthread-2.0.0.dylib",
            "Contents/Frameworks/libjasper.7.dylib",
            "Contents/PlugIns/tls/libqsecuretransportbackend.dylib",
            "Contents/PlugIns/tls/libqcertonlybackend.dylib",
            "Contents/Frameworks/libssl.3.dylib",
            "Contents/Frameworks/libcrypto.3.dylib",
            "Contents/Frameworks/QtDBus.framework/Versions/A/QtDBus",
            "Contents/Frameworks/libdbus-1.3.dylib",
        )
        for relative in removable + retained:
            fixture = bundle / relative
            fixture.parent.mkdir(parents=True, exist_ok=True)
            fixture.write_bytes(relative.encode("utf-8"))

        driver = root / "run.cmake"
        driver.write_text(
            "\n".join(
                [
                    f'set(QT_DEPLOY_PREFIX "{root.as_posix()}")',
                    f'include("{PRUNER.as_posix()}")',
                    'framehouse_prune_deployed_qt_macos_runtime("Looksy.app")',
                ]
            )
            + "\n",
            encoding="utf-8",
        )
        result = subprocess.run(
            ["cmake", "-P", str(driver)],
            check=False,
            capture_output=True,
            text=True,
        )
        require(result.returncode == 0, result.stderr)
        for relative in removable:
            require(not (bundle / relative).exists(), f"unreachable runtime survived: {relative}")
        for relative in retained:
            require((bundle / relative).is_file(), f"required runtime was removed: {relative}")

    print("Qt macOS release-runtime pruning self-test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
