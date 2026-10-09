#!/usr/bin/env python3
"""Fail when obsolete public branding can reach a Looksy release surface."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TEXT_SUFFIXES = {".cpp", ".h", ".html", ".in", ".js", ".md", ".plist", ".ps1", ".py", ".qml", ".rc", ".sh", ".svg", ".txt", ".yml"}
SCAN_PATHS = (
    Path("LICENSE"),
    Path("CMakeLists.txt"),
    Path("THIRD_PARTY_NOTICES.md"),
    Path("apps/desktop/CMakeLists.txt"),
    Path("apps/desktop/Info.plist.in"),
    Path("apps/desktop/assets"),
    Path("apps/desktop/qml"),
    Path("apps/desktop/src"),
    Path("cmake/Packaging.cmake"),
    Path("cmake/DereferenceQtQmlPlugins.cmake"),
    Path("scripts/build-release.sh"),
    Path("scripts/configure-release-site.py"),
    Path("scripts/package-windows-msix.ps1"),
    Path("scripts/release-preflight.py"),
    Path("scripts/sign-notarize-macos.sh"),
    Path("scripts/verify-macos-release.sh"),
    Path("scripts/write-release-manifest.py"),
    Path("site"),
    Path(".github/workflows/release.yml"),
)
BANNED = re.compile(r"stillfold|virel", re.IGNORECASE)
PUBLIC_ENGINEERING_NAME = re.compile(r'QStringLiteral\("[^"]*\bPhotoEditor\b[^"]*"\)')

# These values are deliberately retained so private-alpha installs and deployment
# configuration continue to work. Every exception is a machine identity or a
# one-way compatibility reader, never displayed product copy.
ALLOWED_COMPATIBILITY = {
    "CMakeLists.txt": ("com.virel.studio",),
    "apps/desktop/src/job_runner.cpp": (".virel-partial", "VIREL_IMPORT_RESULT="),
    "apps/desktop/src/timeline_document_controller.cpp": ("stillfold-review-notes",),
    "scripts/configure-release-site.py": ('replace("FRAMEHOUSE_", "VIREL_", 1)',),
    "scripts/package-windows-msix.ps1": (
        'PackageName = "Virel.Studio"',
        'AppId = "VirelStudio"',
        'Fallback "Virel.Studio"',
        'Fallback "VirelStudio"',
    ),
    "scripts/release-preflight.py": (
        'replace("FRAMEHOUSE_", "VIREL_", 1)',
        '"CN=Virel"',
        "virel_name",
    ),
    "scripts/configure-release-site.py": ("virel_name",),
    "apps/desktop/src/carousel_document_controller.cpp": (
        "application/x-stillfold-carousel+json",
        "*.stillfold-design.json",
    ),
    ".github/workflows/release.yml": ("vars.VIREL_",),
}
ALLOWED_ENGINEERING_LITERALS = {
    # QML module identity is an internal compatibility contract, not displayed copy.
    "apps/desktop/src/main.cpp": ('loadFromModule(QStringLiteral("PhotoEditor")',),
}
# Exact compatibility statements may read old application-data directories.
# Keep them separate from displayed copy and do not allow arbitrary literals
# elsewhere in the catalog model.
ALLOWED_ENGINEERING_STATEMENTS = {
    "apps/desktop/src/catalog_asset_model.cpp": (
        'if (root_info.fileName() == QStringLiteral("PhotoEditor")) {',
        'const auto nested = QDir(app_data_root).filePath(QStringLiteral("PhotoEditor"));',
        'if (QFileInfo(root_info.absolutePath()).fileName() == QStringLiteral("PhotoEditor"))',
    ),
}
REQUIRED_FILES = (
    "apps/desktop/assets/looksy-mark.svg",
    "apps/desktop/assets/looksy-logo.svg",
    "apps/desktop/assets/looksy-letterhead.svg",
    "apps/desktop/assets/looksy-rgb-orb.png",
    "apps/desktop/assets/macos/Looksy.icns",
    "apps/desktop/assets/windows/Looksy.ico",
    "apps/desktop/qml/components/LooksyMark.qml",
    "apps/desktop/qml/components/LooksyLetterhead.qml",
    "apps/desktop/qml/components/LooksyLight.qml",
    "apps/desktop/qml/components/LooksyAssistantDock.qml",
    "site/assets/looksy-icon.png",
    "site/assets/og-looksy.png",
)
PUBLIC_COPY_PATHS = (Path("apps/desktop/qml"), Path("site"))


def files_under(path: Path) -> list[Path]:
    absolute = ROOT / path
    if absolute.is_file():
        return [absolute]
    return sorted(
        candidate
        for candidate in absolute.rglob("*")
        if candidate.is_file() and candidate.suffix.lower() in TEXT_SUFFIXES
    )


def allowed(relative_path: str, line: str) -> bool:
    return any(token in line for token in ALLOWED_COMPATIBILITY.get(relative_path, ()))


def main() -> int:
    errors: list[str] = []
    for required in REQUIRED_FILES:
        if not (ROOT / required).is_file():
            errors.append(f"missing Looksy brand asset: {required}")

    for asset_root in (ROOT / "apps/desktop/assets", ROOT / "site/assets"):
        for candidate in asset_root.rglob("*"):
            if candidate.is_file() and BANNED.search(candidate.name):
                errors.append(f"obsolete brand asset path: {candidate.relative_to(ROOT)}")

    for surface in PUBLIC_COPY_PATHS:
        for path in files_under(surface):
            try:
                lines = path.read_text(encoding="utf-8").splitlines()
            except UnicodeDecodeError:
                continue
            for line_number, line in enumerate(lines, start=1):
                if re.search(r"\bFramehouse\b", line):
                    errors.append(f"old product name in {path.relative_to(ROOT)}:{line_number}: {line.strip()}")

    seen: set[Path] = set()
    for scan_path in SCAN_PATHS:
        for path in files_under(scan_path):
            if path in seen:
                continue
            seen.add(path)
            relative = path.relative_to(ROOT).as_posix()
            try:
                lines = path.read_text(encoding="utf-8").splitlines()
            except UnicodeDecodeError:
                continue
            for line_number, line in enumerate(lines, start=1):
                if BANNED.search(line) and not allowed(relative, line):
                    errors.append(f"obsolete public brand in {relative}:{line_number}: {line.strip()}")
                if (
                    relative.startswith("apps/desktop/src/")
                    and PUBLIC_ENGINEERING_NAME.search(line)
                    and not any(token in line for token in ALLOWED_ENGINEERING_LITERALS.get(relative, ()))
                    and line.strip() not in ALLOWED_ENGINEERING_STATEMENTS.get(relative, ())
                ):
                    errors.append(
                        f"internal PhotoEditor identity leaked into public copy in {relative}:{line_number}: {line.strip()}"
                    )

    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    if 'set(FRAMEHOUSE_PRODUCT_NAME "Looksy"' not in cmake or 'set(FRAMEHOUSE_PRODUCT_SLUG "Looksy"' not in cmake:
        errors.append("CMake public product metadata is not canonically Looksy")
    if "FRAMEHOUSE_VENDOR_NAME" in cmake or "FRAMEHOUSE_LEGAL_PUBLISHER" not in cmake:
        errors.append("CMake must keep the legal publisher separate from the Looksy product name")

    if errors:
        for error in errors:
            print(f"brand: error: {error}")
        return 1
    print("brand: Looksy public release surface passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
