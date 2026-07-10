#!/usr/bin/env python3
"""Materialize public site configuration from reviewed deployment values."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path


def env(name: str, default: str = "") -> str:
    return os.environ.get(name, default).strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path("site/download-config.js"))
    args = parser.parse_args()

    repository = env("VIREL_PUBLIC_RELEASE_REPOSITORY")
    owner, repo = (repository.split("/", 1) if "/" in repository else ("", ""))
    repository_version = (Path(__file__).resolve().parents[1] / "VERSION").read_text(encoding="utf-8").strip()
    configuration = {
        "productName": "Virel Studio",
        "channel": env("VIREL_RELEASE_CHANNEL", "Public alpha"),
        "fallbackVersion": env("VIREL_RELEASE_VERSION", repository_version),
        "updatedAt": env("VIREL_RELEASE_UPDATED_AT"),
        "accessLabel": env("VIREL_ACCESS_LABEL", "Public alpha"),
        "accessMode": env("VIREL_ACCESS_MODE", "free-support"),
        "githubOwner": owner,
        "githubRepo": repo,
        "assetPatterns": {
            "macArm": r"VirelStudio.*Darwin-arm64.*\.(dmg|zip)$",
            "macIntel": r"VirelStudio.*Darwin-x86_64.*\.(dmg|zip)$",
            "windows": r"VirelStudio.*x64\.msix$",
            "windowsInstaller": r"VirelStudio\.appinstaller$",
        },
        "directDownloads": {
            "macArm": env("VIREL_MAC_ARM_DOWNLOAD_URL"),
            "macIntel": env("VIREL_MAC_INTEL_DOWNLOAD_URL"),
            "windows": env("VIREL_WINDOWS_DOWNLOAD_URL"),
        },
        "purchaseUrl": env("VIREL_PURCHASE_URL"),
        "supportPaymentUrl": env("VIREL_SUPPORT_PAYMENT_URL"),
        "releaseNotesUrl": env("VIREL_RELEASE_NOTES_URL"),
        "supportEmail": env("VIREL_SUPPORT_EMAIL"),
        "requirements": {
            "mac": "macOS 13 or newer. Apple silicon and Intel builds are published separately.",
            "windows": "Windows 10 22H2 or Windows 11, 64-bit.",
        },
        "policyUrls": {
            "privacy": "./privacy.html",
            "terms": "./terms.html",
            "refunds": "./refunds.html",
            "support": "./support.html",
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        "window.VirelDownloads = " + json.dumps(configuration, indent=2) + ";\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
