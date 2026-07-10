#!/usr/bin/env python3
"""Export locked Rust dependency provenance for release review and SBOM input."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    rustc_details = subprocess.run(
        ["rustc", "-vV"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    target_triple = next(
        line.removeprefix("host: ")
        for line in rustc_details.splitlines()
        if line.startswith("host: ")
    )
    result = subprocess.run(
        [
            "cargo",
            "metadata",
            "--manifest-path",
            str(ROOT / "rust/Cargo.toml"),
            "--format-version",
            "1",
            "--locked",
            "--offline",
            "--filter-platform",
            target_triple,
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    metadata = json.loads(result.stdout)
    workspace_members = set(metadata.get("workspace_members", []))
    packages = []
    missing_licenses = []
    for package in metadata.get("packages", []):
        if package["id"] in workspace_members:
            continue
        license_expression = package.get("license") or ""
        entry = {
            "name": package["name"],
            "version": package["version"],
            "license": license_expression,
            "repository": package.get("repository") or "",
            "source": package.get("source") or "",
        }
        packages.append(entry)
        if not license_expression:
            missing_licenses.append(f"{package['name']} {package['version']}")

    document = {
        "schemaVersion": 1,
        "ecosystem": "cargo",
        "locked": True,
        "targetTriple": target_triple,
        "packages": sorted(packages, key=lambda item: (item["name"], item["version"])),
        "missingLicenseMetadata": sorted(missing_licenses),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    if missing_licenses:
        print("warning: dependency license metadata is missing for:")
        for package in missing_licenses:
            print(f"  {package}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
