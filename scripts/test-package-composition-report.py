#!/usr/bin/env python3
"""Self-test for package-composition-report.py."""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
from datetime import date, timedelta
from pathlib import Path


SCRIPT = Path(__file__).with_name("package-composition-report.py")


def run(root: Path, output: Path, *extra: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "-B", str(SCRIPT), "--root", str(root), "--output", str(output), *extra],
        check=False,
        capture_output=True,
        text=True,
    )


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        base = Path(temporary)
        staged = base / "staged"
        staged.mkdir()
        (staged / "Looksy").write_bytes(b"application")
        (staged / "unique.dat").write_bytes(b"unique")
        macos_bin = staged / "Contents" / "MacOS"
        macos_bin.mkdir(parents=True)
        (macos_bin / "catalog_state").write_bytes(b"catalog-helper")
        (macos_bin / "catalog_integrity").write_bytes(b"catalog-integrity-helper")
        (macos_bin / "catalog_helper").symlink_to("catalog_state")
        postgres_bin = staged / "Contents" / "Resources" / "postgres" / "bin"
        postgres_bin.mkdir(parents=True)
        (postgres_bin / "postgres").write_bytes(b"postgres-runtime")
        qt_frameworks = staged / "Contents" / "Frameworks"
        (qt_frameworks / "QtQuickControls2Basic.framework").mkdir(parents=True)
        (qt_frameworks / "QtQuickControls2Basic.framework" / "QtQuickControls2Basic").write_bytes(
            b"basic-style"
        )
        (qt_frameworks / "QtQuickControls2Material.framework").mkdir(parents=True)
        (
            qt_frameworks
            / "QtQuickControls2Material.framework"
            / "QtQuickControls2Material"
        ).write_bytes(b"material-style")
        qpdf_plugin = staged / "Contents" / "PlugIns" / "imageformats" / "libqpdf.dylib"
        qpdf_plugin.parent.mkdir(parents=True)
        qpdf_plugin.write_bytes(b"pdf-reader")
        report_path = base / "report.json"

        clean = run(staged, report_path, "--fail-on-duplicates", "--max-installed-bytes", "256")
        require(clean.returncode == 0, clean.stderr)
        report = json.loads(report_path.read_text(encoding="utf-8"))
        require(
            report["passed"] is True
            and report["summary"]["fileCount"] == 8
            and report["summary"]["symlinkCount"] == 1,
            "clean report failed",
        )
        require(
            report["symlinks"][0]["target"] == "catalog_state"
            and report["symlinks"][0]["withinRoot"] is True
            and report["symlinks"][0]["targetExists"] is True,
            "contained package symlink should be inventoried",
        )
        require(
            report["summary"]["deflateBytes"] > 0
            and report["summary"]["deflateBytes"]
            == sum(item["deflateBytes"] for item in report["files"]),
            "report should include deterministic per-file compressed bytes",
        )
        require(report["largestFilesByDeflateBytes"], "compressed-size ranking is missing")
        require(report["categories"], "category totals are missing")
        categories = {item["category"] for item in report["categories"]}
        require(
            {
                "catalog-helper",
                "postgres-runtime",
                "qt-quick-controls-basic-style",
                "qt-quick-controls-non-basic-style",
                "qt-macos-pruned-runtime",
            }
            <= categories,
            "release-specific package categories are missing",
        )
        category_budget = run(
            staged,
            report_path,
            "--max-category-bytes",
            "catalog-helper=1",
        )
        require(
            category_budget.returncode == 1
            and "category catalog-helper installed bytes" in category_budget.stderr,
            "oversized release category should fail",
        )
        category_budget_report = json.loads(report_path.read_text(encoding="utf-8"))
        require(
            category_budget_report["categoryBudgets"][0]["withinBudget"] is False,
            "category budget result is missing from the report",
        )
        pruned_runtime_budget = run(
            staged,
            report_path,
            "--max-category-bytes",
            "qt-macos-pruned-runtime=0",
        )
        require(
            pruned_runtime_budget.returncode == 1
            and "category qt-macos-pruned-runtime installed bytes" in pruned_runtime_budget.stderr,
            "a pruned macOS Qt runtime component should fail the zero-byte budget",
        )
        qpdf_plugin.unlink()

        escaping_link = staged / "escape"
        escaping_link.symlink_to(base / "outside")
        escaping = run(staged, report_path)
        require(
            escaping.returncode == 1 and "symlink escapes package root" in escaping.stderr,
            "package symlinks that escape the staged root must fail",
        )
        escaping_link.unlink()

        artifacts = base / "artifacts"
        artifacts.mkdir()
        installer = artifacts / "Looksy.zip"
        installer.write_bytes(b"installer")
        with_artifact = run(staged, report_path, "--artifact-dir", str(artifacts))
        require(with_artifact.returncode == 0, with_artifact.stderr)
        artifact_report = json.loads(report_path.read_text(encoding="utf-8"))
        require(
            artifact_report["summary"]["artifactBytes"] == len(b"installer")
            and artifact_report["artifacts"][0]["path"] == installer.name,
            "final installer size and identity are missing",
        )

        duplicate_bytes = b"same-runtime"
        (staged / "one.dll").write_bytes(duplicate_bytes)
        nested = staged / "plugins"
        nested.mkdir()
        (nested / "two.dll").write_bytes(duplicate_bytes)
        duplicate = run(staged, report_path, "--fail-on-duplicates")
        require(duplicate.returncode == 1, "unexplained duplicate should fail")
        duplicate_report = json.loads(report_path.read_text(encoding="utf-8"))
        require(
            duplicate_report["summary"]["unexplainedDuplicateGroupCount"] == 1,
            "duplicate report should identify one group",
        )

        allowlist_path = base / "allowlist.json"
        allowlist_path.write_text(
            json.dumps(
                {
                    "schemaVersion": 1,
                    "duplicates": [
                        {
                            "sha256": hashlib.sha256(duplicate_bytes).hexdigest(),
                            "paths": ["one.dll", "plugins/two.dll"],
                            "reason": "Fixture intentionally mirrors one runtime.",
                            "owner": "release-engineering",
                            "expires": (date.today() + timedelta(days=30)).isoformat(),
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )
        allowed = run(
            staged,
            report_path,
            "--fail-on-duplicates",
            "--allowlist",
            str(allowlist_path),
        )
        require(allowed.returncode == 0, allowed.stderr)

        expired = json.loads(allowlist_path.read_text(encoding="utf-8"))
        expired["duplicates"][0]["expires"] = (date.today() - timedelta(days=1)).isoformat()
        allowlist_path.write_text(json.dumps(expired), encoding="utf-8")
        rejected = run(staged, report_path, "--allowlist", str(allowlist_path))
        require(rejected.returncode == 1 and "expired" in rejected.stderr, "expired exception should fail")

        oversized = run(staged, report_path, "--max-installed-bytes", "1")
        require(oversized.returncode == 1 and "exceed budget" in oversized.stderr, "size budget should fail")

        (staged / "one.dll").unlink()
        (nested / "two.dll").unlink()
        privacy_bytes = b"required-privacy-manifest"
        first_privacy = staged / "Contents/Frameworks/QtCore.framework/Versions/A/Resources"
        second_privacy = staged / "Contents/Frameworks/QtGui.framework/Versions/A/Resources"
        first_privacy.mkdir(parents=True)
        second_privacy.mkdir(parents=True)
        (first_privacy / "PrivacyInfo.xcprivacy").write_bytes(privacy_bytes)
        (second_privacy / "PrivacyInfo.xcprivacy").write_bytes(privacy_bytes)
        structural = run(staged, report_path, "--fail-on-duplicates")
        require(structural.returncode == 0, structural.stderr)
        structural_report = json.loads(report_path.read_text(encoding="utf-8"))
        require(
            structural_report["duplicateContent"][0]["structuralReason"],
            "required framework metadata should remain visible and explained",
        )

        timezone_bytes = b"iana-timezone-alias"
        timezone_root = (
            staged
            / "Contents"
            / "Resources"
            / "postgres"
            / "share"
            / "postgresql@14"
            / "timezone"
        )
        (timezone_root / "Europe").mkdir(parents=True)
        (timezone_root / "GB").write_bytes(timezone_bytes)
        (timezone_root / "Europe" / "London").write_bytes(timezone_bytes)
        timezone_aliases = run(staged, report_path, "--fail-on-duplicates")
        require(timezone_aliases.returncode == 0, timezone_aliases.stderr)
        timezone_report = json.loads(report_path.read_text(encoding="utf-8"))
        timezone_groups = [
            item
            for item in timezone_report["duplicateContent"]
            if item["sha256"] == hashlib.sha256(timezone_bytes).hexdigest()
        ]
        require(
            len(timezone_groups) == 1
            and timezone_groups[0]["structuralReason"]
            == "PostgreSQL ships IANA timezone aliases as independent runtime files.",
            "PostgreSQL timezone aliases should remain visible and explained",
        )

    print("package-composition self-test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
