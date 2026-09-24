#!/usr/bin/env python3
"""Materialize public site configuration from reviewed deployment values."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

from release_commerce import commerce_config


def env(name: str, default: str = "") -> str:
    looksy_name = name.replace("FRAMEHOUSE_", "LOOKSY_", 1) if name.startswith("FRAMEHOUSE_") else ""
    virel_name = name.replace("FRAMEHOUSE_", "VIREL_", 1) if name.startswith("FRAMEHOUSE_") else ""
    values = [(candidate, os.environ.get(candidate, "").strip())
              for candidate in (looksy_name, name, virel_name) if candidate]
    configured = [(candidate, value) for candidate, value in values if value]
    if len({value for _, value in configured}) > 1:
        raise ValueError(f"conflicting values configured for {', '.join(candidate for candidate, _ in configured)}")
    return (configured[0][1] if configured else "") or default


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path("site/download-config.js"))
    parser.add_argument("--preview", action="store_true", help="Allow incomplete purchase settings for a local preview; never use for deployment")
    args = parser.parse_args()

    repository = env("FRAMEHOUSE_PUBLIC_RELEASE_REPOSITORY")
    owner, repo = (repository.split("/", 1) if "/" in repository else ("", ""))
    repository_version = (Path(__file__).resolve().parents[1] / "VERSION").read_text(encoding="utf-8").strip()
    configuration = {
        "productName": "Looksy",
        "legalPublisher": env("FRAMEHOUSE_LEGAL_PUBLISHER"),
        "channel": env("FRAMEHOUSE_RELEASE_CHANNEL", "Public alpha"),
        "fallbackVersion": env("FRAMEHOUSE_RELEASE_VERSION", repository_version),
        "updatedAt": env("FRAMEHOUSE_RELEASE_UPDATED_AT"),
        "accessLabel": env("FRAMEHOUSE_ACCESS_LABEL", "Public alpha"),
        "accessMode": env("FRAMEHOUSE_ACCESS_MODE", "free-support"),
        "githubOwner": owner,
        "githubRepo": repo,
        "assetPatterns": {
            "macArm": r"Looksy.*Darwin-arm64.*\.(dmg|zip)$",
            "macIntel": r"Looksy.*Darwin-x86_64.*\.(dmg|zip)$",
            "windows": r"Looksy.*x64\.msix$",
            "windowsInstaller": r"Looksy\.appinstaller$",
        },
        "directDownloads": {
            "macArm": env("FRAMEHOUSE_MAC_ARM_DOWNLOAD_URL"),
            "macIntel": env("FRAMEHOUSE_MAC_INTEL_DOWNLOAD_URL"),
            "windows": env("FRAMEHOUSE_WINDOWS_DOWNLOAD_URL"),
        },
        "purchaseUrl": env("FRAMEHOUSE_PURCHASE_URL"),
        "supportPaymentUrl": env("FRAMEHOUSE_SUPPORT_PAYMENT_URL"),
        "releaseNotesUrl": env("FRAMEHOUSE_RELEASE_NOTES_URL"),
        "supportEmail": env("FRAMEHOUSE_SUPPORT_EMAIL"),
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
    # Keep legacy deployment names readable, but publish only the explicit public
    # fields from the shared configuration. Never serialize the environment.
    commerce = commerce_config(lambda name, default="": env(name.replace("LOOKSY_", "FRAMEHOUSE_", 1), default), require_ready=not args.preview)
    configuration.update(commerce)
    if commerce["accessMode"] == "paid-perpetual":
        configuration["accessLabel"] = "One-time purchase"
    elif commerce["accessMode"] == "paid-both":
        configuration["accessLabel"] = "Subscription or one-time purchase"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        "window.LooksyDownloads = " + json.dumps(configuration, indent=2) + ";\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
