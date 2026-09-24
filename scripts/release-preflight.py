#!/usr/bin/env python3
"""Fail closed before a candidate or public Looksy release."""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import urlparse

from release_commerce import commerce_config, validate_commerce, validate_customer_build_config


ROOT = Path(__file__).resolve().parents[1]


def fail(errors: list[str], message: str) -> None:
    errors.append(message)


def https_url(value: str) -> bool:
    parsed = urlparse(value)
    return parsed.scheme == "https" and bool(parsed.netloc)


def deployment_value(name: str, errors: list[str]) -> str:
    looksy_name = name.replace("FRAMEHOUSE_", "LOOKSY_", 1) if name.startswith("FRAMEHOUSE_") else ""
    virel_name = name.replace("FRAMEHOUSE_", "VIREL_", 1) if name.startswith("FRAMEHOUSE_") else ""
    values = [(candidate, os.environ.get(candidate, "").strip())
              for candidate in (looksy_name, name, virel_name) if candidate]
    configured = [(candidate, value) for candidate, value in values if value]
    if len({value for _, value in configured}) > 1:
        fail(errors, f"conflicting values configured for {', '.join(candidate for candidate, _ in configured)}")
    return configured[0][1] if configured else ""


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
    try:
        commerce = commerce_config(lambda name, default="": deployment_value(
            name.replace("LOOKSY_", "FRAMEHOUSE_", 1), errors) or default, require_ready=False)
    except ValueError as error:
        fail(errors, str(error))
        commerce = {"accessMode": "free-support", "supportedPlatforms": ["mac-arm"]}
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        fail(errors, "VERSION must contain X.Y.Z")

    ref_name = os.environ.get("GITHUB_REF_NAME", "").strip()
    if ref_name.startswith("v") and ref_name != f"v{version}":
        fail(errors, f"tag {ref_name} does not match VERSION v{version}")

    required_files = (
        "apps/desktop/assets/app-icon-1024.png",
        "apps/desktop/assets/macos/Looksy.icns",
        "apps/desktop/assets/windows/Looksy.ico",
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

    brand_check = subprocess.run(
        [sys.executable, str(ROOT / "scripts/verify-brand-surface.py")],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    if brand_check.returncode != 0:
        details = (brand_check.stdout + brand_check.stderr).strip()
        fail(errors, f"Looksy brand surface validation failed: {details}")

    if not args.allow_dirty:
        dirty = tracked_changes()
        if dirty:
            fail(errors, "release worktree must be clean")

    if args.mode == "public":
        try:
            validate_commerce(commerce, require_ready=True)
            if commerce["accessMode"] == "paid-both":
                validate_customer_build_config()
        except ValueError as error:
            fail(errors, str(error))
        required_values = {
            "LOOKSY_LEGAL_PUBLISHER": deployment_value("FRAMEHOUSE_LEGAL_PUBLISHER", errors),
            "LOOKSY_PUBLIC_RELEASE_REPOSITORY": deployment_value(
                "FRAMEHOUSE_PUBLIC_RELEASE_REPOSITORY", errors
            ),
            "LOOKSY_WEBSITE_URL": deployment_value("FRAMEHOUSE_WEBSITE_URL", errors),
            "LOOKSY_SUPPORT_EMAIL": deployment_value("FRAMEHOUSE_SUPPORT_EMAIL", errors),
            "LOOKSY_SUPPORT_PAYMENT_URL": deployment_value(
                "FRAMEHOUSE_SUPPORT_PAYMENT_URL", errors
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
        if commerce["accessMode"] in ("paid-perpetual", "paid-both"):
            required_values.pop("LOOKSY_SUPPORT_PAYMENT_URL")
        if "windows" not in commerce["supportedPlatforms"]:
            for name in ("PHOTO_EDITOR_WINDOWS_PACKAGE_PUBLISHER", "PHOTO_EDITOR_WINDOWS_APPINSTALLER_URI", "PHOTO_EDITOR_WINDOWS_PACKAGE_BASE_URI"):
                required_values.pop(name)
        if not any(platform.startswith("mac-") for platform in commerce["supportedPlatforms"]):
            required_values.pop("PHOTO_EDITOR_MACOS_APPCAST_URL")
            required_values.pop("PHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY")
        for name, value in required_values.items():
            if not value.strip():
                fail(errors, f"{name} is required for a public release")
        if required_values["LOOKSY_LEGAL_PUBLISHER"] in ("Local verification only", "Publisher identity pending release approval"):
            fail(errors, "LOOKSY_LEGAL_PUBLISHER must identify the actual legal publisher")

        for name in (
            "LOOKSY_WEBSITE_URL",
            "LOOKSY_SUPPORT_PAYMENT_URL",
            "PHOTO_EDITOR_MACOS_APPCAST_URL",
            "PHOTO_EDITOR_WINDOWS_APPINSTALLER_URI",
            "PHOTO_EDITOR_WINDOWS_PACKAGE_BASE_URI",
        ):
            value = required_values.get(name, "")
            if value and not https_url(value):
                fail(errors, f"{name} must be an HTTPS URL")

        repository = required_values["LOOKSY_PUBLIC_RELEASE_REPOSITORY"]
        if repository and not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
            fail(errors, "LOOKSY_PUBLIC_RELEASE_REPOSITORY must use owner/repository form")
        if required_values["LOOKSY_SUPPORT_EMAIL"] and "@" not in required_values["LOOKSY_SUPPORT_EMAIL"]:
            fail(errors, "LOOKSY_SUPPORT_EMAIL must be an email address")
        publisher = required_values.get("PHOTO_EDITOR_WINDOWS_PACKAGE_PUBLISHER", "")
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
            if label.startswith("Windows") and "windows" not in commerce["supportedPlatforms"]:
                continue
            if label.startswith("macOS") and not any(platform.startswith("mac-") for platform in commerce["supportedPlatforms"]):
                continue
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
