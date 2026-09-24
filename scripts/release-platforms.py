#!/usr/bin/env python3
"""Use the same supported-platform selection for CI, promotion and the website."""

import argparse
import json
from pathlib import Path

from release_commerce import commerce_config


TARGETS = {
    "mac-arm": dict(os="macos-14", configure_preset="release-macos", build_preset="build-release-macos", artifact_suffix="macos-arm64"),
    "mac-intel": dict(os="macos-15-intel", configure_preset="release-macos", build_preset="build-release-macos", artifact_suffix="macos-x86_64"),
    "windows": dict(os="windows-2022", configure_preset="release-windows", build_preset="build-release-windows", artifact_suffix="windows-x64"),
}


def validate_artifacts(directory: Path, platforms: list[str], version: str, *, appcast: bool = True) -> list[str]:
    required = []
    if "mac-arm" in platforms:
        required.append(f"Looksy-{version}-Darwin-arm64.dmg")
    if "mac-intel" in platforms:
        required.append(f"Looksy-{version}-Darwin-x86_64.dmg")
    if "windows" in platforms:
        required.extend((f"Looksy-{version}.0-x64.msix", "Looksy.appinstaller"))
    errors = [f"missing selected-platform artifact: {name}" for name in required
              if not (directory / name).is_file() or not (directory / name).stat().st_size]
    if appcast and any(platform.startswith("mac-") for platform in platforms) and not list(directory.glob("*.xml")):
        errors.append("missing macOS appcast")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--github-output", type=Path)
    parser.add_argument("--artifacts", type=Path)
    parser.add_argument("--without-appcast", action="store_true")
    parser.add_argument("--macos-only", action="store_true")
    args = parser.parse_args()
    platforms = commerce_config(require_ready=False)["supportedPlatforms"]
    if args.macos_only:
        platforms = [platform for platform in platforms if platform.startswith("mac-")]
    output = {
        "matrix": json.dumps({"include": [TARGETS[platform] for platform in platforms]}, separators=(",", ":")),
        "macos": str(any(platform.startswith("mac-") for platform in platforms)).lower(),
    }
    if args.github_output:
        with args.github_output.open("a") as handle:
            for key, value in output.items():
                handle.write(f"{key}={value}\n")
    if args.artifacts:
        version = (Path(__file__).resolve().parents[1] / "VERSION").read_text().strip()
        errors = validate_artifacts(args.artifacts, platforms, version, appcast=not args.without_appcast)
        for error in errors:
            print(error)
        return int(bool(errors))
    print(output["matrix"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
