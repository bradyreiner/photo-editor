#!/usr/bin/env python3
"""Self-test the macOS Qt Quick Controls style-pruning deployment hook."""

from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
PRUNER = REPO_ROOT / "cmake" / "PruneQtQuickControlsStyles.cmake"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        bundle = root / "Looksy.app"
        plugins = bundle / "Contents" / "PlugIns"
        controls = bundle / "Contents" / "Resources" / "qml" / "QtQuick" / "Controls"
        dialogs = bundle / "Contents" / "Resources" / "qml" / "QtQuick" / "Dialogs"
        plugins.mkdir(parents=True)

        basic_plugin = plugins / "libqtquickcontrols2basicstyleplugin.dylib"
        material_plugin = plugins / "libqtquickcontrols2materialstyleplugin.dylib"
        core_plugin = plugins / "libqtquickcontrols2plugin.dylib"
        for plugin in (basic_plugin, material_plugin, core_plugin):
            plugin.write_bytes(plugin.name.encode("utf-8"))
        for style in ("Basic", "Material", "Fusion"):
            (controls / style).mkdir(parents=True)
            (controls / style / "qmldir").write_text(style, encoding="utf-8")
        (dialogs / "+Material").mkdir(parents=True)
        (dialogs / "+Material" / "FileDialog.qml").write_text("fixture", encoding="utf-8")

        driver = root / "run.cmake"
        driver.write_text(
            "\n".join(
                [
                    f'set(QT_DEPLOY_PREFIX "{root.as_posix()}")',
                    f'include("{PRUNER.as_posix()}")',
                    "set(plugins_found",
                    "  Looksy.app/Contents/PlugIns/libqtquickcontrols2basicstyleplugin.dylib",
                    "  Looksy.app/Contents/PlugIns/libqtquickcontrols2materialstyleplugin.dylib",
                    "  Looksy.app/Contents/PlugIns/libqtquickcontrols2plugin.dylib)",
                    'framehouse_prune_deployed_qt_quick_controls_styles("Looksy.app" plugins_found)',
                    'file(WRITE "${QT_DEPLOY_PREFIX}/plugins.txt" "${plugins_found}")',
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
        require(basic_plugin.is_file() and core_plugin.is_file(), "required Basic/core plugins were removed")
        require(not material_plugin.exists(), "non-Basic plugin was retained")
        require((controls / "Basic").is_dir(), "Basic style module was removed")
        require(not (controls / "Material").exists(), "Material style module was retained")
        require(not (controls / "Fusion").exists(), "Fusion style module was retained")
        require(not (dialogs / "+Material").exists(), "non-Basic dialog selector was retained")
        retained_plugins = (root / "plugins.txt").read_text(encoding="utf-8")
        require("basicstyleplugin" in retained_plugins, "Basic plugin was filtered")
        require("qtquickcontrols2plugin" in retained_plugins, "core plugin was filtered")
        require("materialstyleplugin" not in retained_plugins, "removed plugin stayed in dependency scan")

    print("Qt Quick Controls style-pruning self-test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
