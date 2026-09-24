#!/usr/bin/env python3
"""Record local release prerequisites without printing credentials or claiming QA."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import platform
import subprocess
import sys
from pathlib import Path

from release_commerce import commerce_config

ROOT = Path(__file__).resolve().parents[1]


def run(command):
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, check=False)
    return {"passed": result.returncode == 0, "output": (result.stdout + result.stderr).strip()}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--package", type=Path)
    args = parser.parse_args()
    status = run(["git", "status", "--porcelain"])
    checks = {
        "candidateStructure": run([sys.executable, "scripts/release-preflight.py", "--mode", "candidate", "--allow-dirty"]),
        "publicConfiguration": run([sys.executable, "scripts/release-preflight.py", "--mode", "public", "--allow-dirty"]),
        "cleanWorktree": {"passed": status["passed"] and not status["output"], "changedPaths": len(status["output"].splitlines())},
    }
    if platform.system() == "Darwin":
        identities = run(["security", "find-identity", "-v", "-p", "codesigning"])
        checks["developerIdIdentity"] = {"passed": identities["passed"] and "Developer ID Application:" in identities["output"]}
    # Human/hardware/business evidence cannot be inferred from code or a dev Mac.
    for name in ("cleanMachineWorkflow", "previewExportPhotoReview", "upgradeAndRestore",
                 "livePurchaseReceiptRecoveryRefund", "dependencyRightsReview", "externalBeta"):
        checks[name] = {"passed": False, "status": "external evidence required"}
    document = {
        "schemaVersion": 1,
        "recordedAt": datetime.now(timezone.utc).isoformat(),
        "version": (ROOT / "VERSION").read_text().strip(),
        "commit": run(["git", "rev-parse", "HEAD"])["output"],
        "supportedPlatforms": commerce_config(require_ready=False)["supportedPlatforms"],
        "readyForSale": all(check["passed"] for check in checks.values()),
        "checks": checks,
    }
    if args.package:
        digest = hashlib.sha256()
        with args.package.open("rb") as handle:
            for block in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(block)
        document["localPackage"] = {"path": str(args.package.resolve()), "bytes": args.package.stat().st_size,
                                    "sha256": digest.hexdigest(), "distributionStatus": "local engineering candidate; signing and external QA required"}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, indent=2) + "\n")
    for name, check in checks.items():
        print(f"{'PASS' if check['passed'] else 'OPEN'} {name}")
    print(f"Report: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
