#!/usr/bin/env python3
"""Inventory a staged release tree and fail on unexplained duplicate bytes."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import zlib
from collections import defaultdict
from datetime import date, datetime, timezone
from pathlib import Path


SCHEMA = "framehouse-package-composition:v1"


ARTIFACT_SUFFIXES = {".dmg", ".exe", ".msix", ".msixbundle", ".msi", ".pkg", ".tgz", ".zip"}

CATALOG_HELPER_MANIFEST = Path(__file__).resolve().parents[1] / "cmake" / "CatalogHelpers.txt"
CATALOG_HELPER_NAMES = frozenset({"catalog_helper"}) | frozenset(
    line.strip()
    for line in CATALOG_HELPER_MANIFEST.read_text(encoding="utf-8").splitlines()
    if line.strip() and not line.lstrip().startswith("#")
)

QT_QUICK_CONTROLS_NON_BASIC_STYLE_MARKERS = {
    "fluentwinui3",
    "fusion",
    "imagine",
    "iosstyle",
    "macosstyle",
    "material",
    "nativestyle",
    "universal",
}

QT_MACOS_PRUNED_RUNTIME_NAMES = {
    "libqglib.dylib",
    "libqicns.dylib",
    "libqico.dylib",
    "libqjp2.dylib",
    "libqmng.dylib",
    "libqpdf.dylib",
    "libqtga.dylib",
    "libqtvirtualkeyboardplugin.dylib",
    "libqwbmp.dylib",
    "libgio-2.0.0.dylib",
    "libgmodule-2.0.0.dylib",
    "libgobject-2.0.0.dylib",
    "libmng.2.dylib",
    "libqopensslbackend.dylib",
}

QT_MACOS_PRUNED_FRAMEWORKS = {
    "qtpdf.framework",
    "qtvirtualkeyboard.framework",
    "qtvirtualkeyboardqml.framework",
}


def fingerprint(path: Path, *, measure_deflate: bool = False) -> tuple[str, int | None]:
    digest = hashlib.sha256()
    compressor = zlib.compressobj(level=9, wbits=-zlib.MAX_WBITS) if measure_deflate else None
    compressed_bytes = 0
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
            if compressor is not None:
                compressed_bytes += len(compressor.compress(block))
    if compressor is not None:
        compressed_bytes += len(compressor.flush())
    return digest.hexdigest(), compressed_bytes if compressor is not None else None


def category(path: Path) -> str:
    path_parts = [part.casefold() for part in path.parts]
    filename = path.name.casefold()
    executable_name = path.stem.casefold() if path.suffix.casefold() == ".exe" else filename
    if executable_name in CATALOG_HELPER_NAMES:
        return "catalog-helper"
    if "postgres" in path_parts:
        return "postgres-runtime"
    if filename in QT_MACOS_PRUNED_RUNTIME_NAMES or any(
        part in QT_MACOS_PRUNED_FRAMEWORKS for part in path_parts
    ):
        return "qt-macos-pruned-runtime"
    quick_controls_identity = " ".join(
        part for part in path_parts if "quickcontrols2" in part
    )
    if quick_controls_identity:
        if "basic" in quick_controls_identity:
            return "qt-quick-controls-basic-style"
        if any(
            marker in quick_controls_identity
            for marker in QT_QUICK_CONTROLS_NON_BASIC_STYLE_MARKERS
        ):
            return "qt-quick-controls-non-basic-style"
        return "qt-quick-controls-core"
    suffix = path.suffix.lower()
    if suffix in {".dylib", ".dll", ".so", ".framework"}:
        return "runtime-library"
    if suffix in {".exe", ".app"} or not suffix:
        return "executable-or-data"
    if suffix in {".onnx", ".gguf", ".bin", ".mlmodel", ".mlpackage"}:
        return "model"
    if suffix in {".png", ".jpg", ".jpeg", ".svg", ".ico", ".icns", ".qsb"}:
        return "resource"
    if suffix in {".json", ".qml", ".qm", ".conf", ".ini", ".plist"}:
        return "configuration"
    if suffix in {".md", ".txt", ".html", ".pdf", ".license"}:
        return "documentation-or-license"
    return "other"


def structural_duplicate_reason(paths: list[str]) -> str | None:
    qt_privacy_manifest = re.compile(
        r"^Contents/Frameworks/[^/]+\.framework/Versions/[^/]+/Resources/PrivacyInfo\.xcprivacy$"
    )
    if paths and all(qt_privacy_manifest.fullmatch(path) for path in paths):
        return "Qt embeds the same required Apple privacy manifest in each framework bundle."
    postgres_timezone_data = re.compile(
        r"^(?:.*/)?share/[^/]+/timezone/.+$"
    )
    if paths and all(postgres_timezone_data.fullmatch(path) for path in paths):
        return "PostgreSQL ships IANA timezone aliases as independent runtime files."
    return None


def load_allowlist(path: Path | None) -> tuple[dict[tuple[str, tuple[str, ...]], dict], list[str]]:
    if path is None:
        return {}, []
    errors: list[str] = []
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        return {}, [f"cannot read duplicate allowlist {path}: {exc}"]
    if document.get("schemaVersion") != 1 or not isinstance(document.get("duplicates"), list):
        return {}, ["duplicate allowlist must use schemaVersion 1 and a duplicates array"]

    today = date.today()
    entries: dict[tuple[str, tuple[str, ...]], dict] = {}
    for index, entry in enumerate(document["duplicates"]):
        label = f"duplicates[{index}]"
        if not isinstance(entry, dict):
            errors.append(f"{label} must be an object")
            continue
        digest = entry.get("sha256")
        paths = entry.get("paths")
        reason = entry.get("reason")
        owner = entry.get("owner")
        expires = entry.get("expires")
        if not isinstance(digest, str) or len(digest) != 64:
            errors.append(f"{label}.sha256 must be a full SHA-256")
        if not isinstance(paths, list) or len(paths) < 2 or not all(isinstance(item, str) for item in paths):
            errors.append(f"{label}.paths must contain at least two relative paths")
            paths = []
        if not isinstance(reason, str) or not reason.strip():
            errors.append(f"{label}.reason is required")
        if not isinstance(owner, str) or not owner.strip():
            errors.append(f"{label}.owner is required")
        try:
            expiry = date.fromisoformat(expires)
            if expiry < today:
                errors.append(f"{label} expired on {expires}")
        except (TypeError, ValueError):
            errors.append(f"{label}.expires must be an ISO date")
        if isinstance(digest, str) and paths:
            key = (digest, tuple(sorted(paths)))
            if key in entries:
                errors.append(f"{label} duplicates another allowlist entry")
            entries[key] = entry
    return entries, errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True, help="Staged app/package directory")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--allowlist", type=Path)
    parser.add_argument("--previous", type=Path, help="Previous composition report")
    parser.add_argument("--artifact-dir", type=Path, help="Directory containing final installer artifacts")
    parser.add_argument("--commit", default="")
    parser.add_argument("--max-installed-bytes", type=int)
    parser.add_argument("--max-growth-bytes", type=int)
    parser.add_argument(
        "--max-category-bytes",
        action="append",
        default=[],
        metavar="CATEGORY=BYTES",
        help="Repeatable installed-byte ceiling for one report category",
    )
    parser.add_argument("--fail-on-duplicates", action="store_true")
    args = parser.parse_args()

    root = args.root.resolve()
    output = args.output.resolve()
    errors: list[str] = []
    if not root.is_dir():
        print(f"package-composition: error: root is not a directory: {root}", file=sys.stderr)
        return 2

    allowlist, allowlist_errors = load_allowlist(args.allowlist)
    errors.extend(allowlist_errors)
    category_budgets: dict[str, int] = {}
    for raw_budget in args.max_category_bytes:
        name, separator, raw_bytes = raw_budget.partition("=")
        try:
            byte_limit = int(raw_bytes)
        except ValueError:
            byte_limit = -1
        if not separator or not name.strip() or byte_limit < 0:
            errors.append(
                f"invalid category budget {raw_budget!r}; expected CATEGORY=BYTES"
            )
            continue
        normalized_name = name.strip()
        if normalized_name in category_budgets:
            errors.append(f"duplicate category budget: {normalized_name}")
            continue
        category_budgets[normalized_name] = byte_limit
    files: list[dict[str, object]] = []
    content_groups: dict[tuple[int, str], list[str]] = defaultdict(list)
    basename_groups: dict[str, list[str]] = defaultdict(list)
    total_bytes = 0
    total_deflate_bytes = 0
    category_totals: dict[str, dict[str, int]] = defaultdict(
        lambda: {"fileCount": 0, "installedBytes": 0, "deflateBytes": 0}
    )
    symlinks: list[dict[str, object]] = []
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            relative = path.relative_to(root).as_posix()
            target = os.readlink(path)
            try:
                resolved_target = path.resolve(strict=False)
                resolved_target.relative_to(root)
                within_root = True
            except (OSError, RuntimeError, ValueError):
                resolved_target = None
                within_root = False
            target_exists = path.exists()
            symlinks.append(
                {
                    "path": relative,
                    "target": target,
                    "resolvedTarget": str(resolved_target) if resolved_target is not None else None,
                    "withinRoot": within_root,
                    "targetExists": target_exists,
                }
            )
            if not within_root:
                errors.append(f"symlink escapes package root: {relative} -> {target}")
            elif not target_exists:
                errors.append(f"symlink target is missing: {relative} -> {target}")
            continue
        if path.resolve() == output or not path.is_file():
            continue
        relative = path.relative_to(root).as_posix()
        size = path.stat().st_size
        digest, deflate_bytes = fingerprint(path, measure_deflate=True)
        assert deflate_bytes is not None
        file_category = category(path)
        total_bytes += size
        total_deflate_bytes += deflate_bytes
        category_totals[file_category]["fileCount"] += 1
        category_totals[file_category]["installedBytes"] += size
        category_totals[file_category]["deflateBytes"] += deflate_bytes
        files.append(
            {
                "path": relative,
                "bytes": size,
                "deflateBytes": deflate_bytes,
                "sha256": digest,
                "category": file_category,
            }
        )
        if size > 0:
            content_groups[(size, digest)].append(relative)
        basename_groups[path.name.casefold()].append(relative)

    duplicates: list[dict[str, object]] = []
    matched_allowlist: set[tuple[str, tuple[str, ...]]] = set()
    for (size, digest), paths in sorted(content_groups.items()):
        if len(paths) < 2:
            continue
        key = (digest, tuple(sorted(paths)))
        structural_reason = structural_duplicate_reason(paths)
        allowed = key in allowlist or structural_reason is not None
        if key in allowlist:
            matched_allowlist.add(key)
        duplicates.append(
            {
                "sha256": digest,
                "bytesEach": size,
                "wastedBytes": size * (len(paths) - 1),
                "paths": sorted(paths),
                "allowed": allowed,
                "exception": allowlist.get(key),
                "structuralReason": structural_reason,
            }
        )

    unused_allowlist = [
        {"sha256": digest, "paths": list(paths)}
        for digest, paths in sorted(set(allowlist) - matched_allowlist)
    ]
    if unused_allowlist:
        errors.append("duplicate allowlist contains entries that do not match the staged package")

    repeated_basenames = [
        {"name": name, "paths": sorted(paths)}
        for name, paths in sorted(basename_groups.items())
        if len(paths) > 1
    ]
    unexplained = [item for item in duplicates if not item["allowed"]]
    duplicate_waste = sum(int(item["wastedBytes"]) for item in duplicates)

    artifacts: list[dict[str, object]] = []
    artifact_bytes = 0
    if args.artifact_dir is not None:
        artifact_dir = args.artifact_dir.resolve()
        if not artifact_dir.is_dir():
            errors.append(f"artifact directory is not a directory: {artifact_dir}")
        else:
            for artifact in sorted(artifact_dir.iterdir()):
                if not artifact.is_file() or artifact.suffix.lower() not in ARTIFACT_SUFFIXES:
                    continue
                digest, _ = fingerprint(artifact)
                size = artifact.stat().st_size
                artifact_bytes += size
                artifacts.append({"path": artifact.name, "bytes": size, "sha256": digest})
            if not artifacts:
                errors.append(f"artifact directory contains no supported installer: {artifact_dir}")

    previous_total = None
    growth_bytes = None
    if args.previous is not None:
        try:
            previous = json.loads(args.previous.read_text(encoding="utf-8"))
            if previous.get("schema") != SCHEMA:
                raise ValueError("unexpected report schema")
            previous_total = int(previous["summary"]["installedBytes"])
            growth_bytes = total_bytes - previous_total
        except (OSError, UnicodeError, json.JSONDecodeError, KeyError, TypeError, ValueError) as exc:
            errors.append(f"cannot use previous composition report: {exc}")

    if args.max_installed_bytes is not None and total_bytes > args.max_installed_bytes:
        errors.append(
            f"installed bytes {total_bytes} exceed budget {args.max_installed_bytes}"
        )
    if (
        args.max_growth_bytes is not None
        and growth_bytes is not None
        and growth_bytes > args.max_growth_bytes
    ):
        errors.append(f"package growth {growth_bytes} exceeds budget {args.max_growth_bytes}")
    if args.fail_on_duplicates and unexplained:
        errors.append(f"{len(unexplained)} unexplained duplicate-content group(s)")
    for name, byte_limit in sorted(category_budgets.items()):
        installed_bytes = category_totals[name]["installedBytes"]
        if installed_bytes > byte_limit:
            errors.append(
                f"category {name} installed bytes {installed_bytes} exceed budget {byte_limit}"
            )

    report = {
        "schema": SCHEMA,
        "generatedAt": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "commit": args.commit,
        "root": str(root),
        "summary": {
            "fileCount": len(files),
            "symlinkCount": len(symlinks),
            "installedBytes": total_bytes,
            "deflateBytes": total_deflate_bytes,
            "artifactCount": len(artifacts),
            "artifactBytes": artifact_bytes if artifacts else None,
            "previousInstalledBytes": previous_total,
            "growthBytes": growth_bytes,
            "duplicateGroupCount": len(duplicates),
            "unexplainedDuplicateGroupCount": len(unexplained),
            "duplicateWasteBytes": duplicate_waste,
        },
        "files": files,
        "symlinks": symlinks,
        "largestFiles": sorted(files, key=lambda item: (-int(item["bytes"]), str(item["path"])))[:50],
        "largestFilesByInstalledBytes": sorted(
            files,
            key=lambda item: (-int(item["bytes"]), str(item["path"])),
        )[:50],
        "largestFilesByDeflateBytes": sorted(
            files,
            key=lambda item: (-int(item["deflateBytes"]), str(item["path"])),
        )[:50],
        "categories": [
            {
                "category": name,
                **values,
                "budgetBytes": category_budgets.get(name),
                "withinBudget": (
                    values["installedBytes"] <= category_budgets[name]
                    if name in category_budgets
                    else None
                ),
            }
            for name, values in sorted(
                category_totals.items(),
                key=lambda item: (-item[1]["installedBytes"], item[0]),
            )
        ],
        "categoryBudgets": [
            {
                "category": name,
                "budgetBytes": byte_limit,
                "installedBytes": category_totals[name]["installedBytes"],
                "withinBudget": category_totals[name]["installedBytes"] <= byte_limit,
            }
            for name, byte_limit in sorted(category_budgets.items())
        ],
        "artifacts": artifacts,
        "duplicateContent": duplicates,
        "repeatedBasenames": repeated_basenames,
        "unusedAllowlistEntries": unused_allowlist,
        "errors": errors,
        "passed": not errors,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    if errors:
        for error in errors:
            print(f"package-composition: error: {error}", file=sys.stderr)
        return 1
    print(
        "package-composition: passed "
        f"files={len(files)} symlinks={len(symlinks)} installed_bytes={total_bytes} "
        f"deflate_bytes={total_deflate_bytes} "
        f"artifact_bytes={artifact_bytes if artifacts else 'not-measured'} "
        f"duplicate_waste_bytes={duplicate_waste}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
