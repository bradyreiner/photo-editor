#!/usr/bin/env python3
"""Fail closed before a candidate or public Virel Studio release."""

from __future__ import annotations

import argparse
import os
import re
import subprocess
from pathlib import Path
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[1]


def fail(errors: list[str], message: str) -> None:
    errors.append(message)


def https_url(value: str) -> bool:
    parsed = urlparse(value)
    return parsed.scheme == "https" and bool(parsed.netloc)


def tracked_changes() -> list[str]:
    result = subprocess.run(
        ["git", "status", "--short", "--untracked-files=normal"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    return [line for line in result.stdout.splitlines() if line.strip()]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("candidate", "public"), default="candidate")
    parser.add_argument("--allow-dirty", action="store_true")
    args = parser.parse_args()

    errors: list[str] = []
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        fail(errors, "VERSION must contain X.Y.Z")

    ref_name = os.environ.get("GITHUB_REF_NAME", "").strip()
    if ref_name.startswith("v") and ref_name != f"v{version}":
        fail(errors, f"tag {ref_name} does not match VERSION v{version}")

    required_files = (
        "apps/desktop/assets/app-icon-1024.png",
        "apps/desktop/assets/macos/VirelStudio.icns",
        "apps/desktop/assets/windows/VirelStudio.ico",
        "LICENSE",
        "THIRD_PARTY_NOTICES.md",
        "site/privacy.html",
        "site/terms.html",
        "site/refunds.html",
        "site/support.html",
        f"docs/releases/v{version}-alpha.md",
    )
    for relative_path in required_files:
        if not (ROOT / relative_path).is_file():
            fail(errors, f"required release file is missing: {relative_path}")

    if not args.allow_dirty:
        dirty = tracked_changes()
        if dirty:
            fail(errors, "release worktree must be clean")

    if args.mode == "public":
        required_values = {
            "VIREL_PUBLIC_RELEASE_REPOSITORY": os.environ.get(
                "VIREL_PUBLIC_RELEASE_REPOSITORY", ""
            ),
            "VIREL_WEBSITE_URL": os.environ.get("VIREL_WEBSITE_URL", ""),
            "VIREL_SUPPORT_EMAIL": os.environ.get("VIREL_SUPPORT_EMAIL", ""),
            "VIREL_SUPPORT_PAYMENT_URL": os.environ.get(
                "VIREL_SUPPORT_PAYMENT_URL", ""
            ),
            "PHOTO_EDITOR_MACOS_APPCAST_URL": os.environ.get(
                "PHOTO_EDITOR_MACOS_APPCAST_URL", ""
            ),
            "PHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY": os.environ.get(
                "PHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY", ""
            ),
            "PHOTO_EDITOR_WINDOWS_PACKAGE_PUBLISHER": os.environ.get(
                "PHOTO_EDITOR_WINDOWS_PACKAGE_PUBLISHER", ""
            ),
            "PHOTO_EDITOR_WINDOWS_APPINSTALLER_URI": os.environ.get(
                "PHOTO_EDITOR_WINDOWS_APPINSTALLER_URI", ""
            ),
            "PHOTO_EDITOR_WINDOWS_PACKAGE_BASE_URI": os.environ.get(
                "PHOTO_EDITOR_WINDOWS_PACKAGE_BASE_URI", ""
            ),
        }
        for name, value in required_values.items():
            if not value.strip():
                fail(errors, f"{name} is required for a public release")

        for name in (
            "VIREL_WEBSITE_URL",
            "VIREL_SUPPORT_PAYMENT_URL",
            "PHOTO_EDITOR_MACOS_APPCAST_URL",
            "PHOTO_EDITOR_WINDOWS_APPINSTALLER_URI",
            "PHOTO_EDITOR_WINDOWS_PACKAGE_BASE_URI",
        ):
            value = required_values[name]
            if value and not https_url(value):
                fail(errors, f"{name} must be an HTTPS URL")

        repository = required_values["VIREL_PUBLIC_RELEASE_REPOSITORY"]
        if repository and not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
            fail(errors, "VIREL_PUBLIC_RELEASE_REPOSITORY must use owner/repository form")
        if required_values["VIREL_SUPPORT_EMAIL"] and "@" not in required_values["VIREL_SUPPORT_EMAIL"]:
            fail(errors, "VIREL_SUPPORT_EMAIL must be an email address")
        publisher = required_values["PHOTO_EDITOR_WINDOWS_PACKAGE_PUBLISHER"]
        if publisher in {"CN=PhotoEditor", "CN=Virel"}:
            fail(errors, "Windows publisher must exactly match the production certificate subject")

        required_alternatives = (
            (
                "macOS Sparkle distribution",
                os.environ.get("PHOTO_EDITOR_MACOS_SPARKLE_ARCHIVE", ""),
                os.environ.get("PHOTO_EDITOR_MACOS_SPARKLE_URL", ""),
            ),
            (
                "Windows PostgreSQL runtime",
                os.environ.get("PHOTO_EDITOR_WINDOWS_POSTGRES_RUNTIME_ARCHIVE", ""),
                os.environ.get("PHOTO_EDITOR_WINDOWS_POSTGRES_RUNTIME_URL", ""),
            ),
        )
        for label, archive, url in required_alternatives:
            if not archive.strip() and not url.strip():
                fail(errors, f"{label} requires an archive path or download URL")
            if url.strip() and not https_url(url):
                fail(errors, f"{label} download URL must use HTTPS")

    if errors:
        for error in errors:
            print(f"preflight: error: {error}")
        return 1

    print(f"preflight: {args.mode} release v{version} is structurally ready")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
