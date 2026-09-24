#!/usr/bin/env python3
"""Reject Adobe runtime, SDK, and generated-fixture dependencies in the color engine."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


SCHEMA = "photo-editor-proprietary-color-boundary:v1"
DEPENDENCY_GLOBS = (
    "CMakeLists.txt",
    "vcpkg.json",
    "cmake/**/*.cmake",
    ".github/workflows/*.yml",
    ".github/workflows/*.yaml",
)
SOURCE_ROOTS = ("engine", "apps", "scripts")
SOURCE_SUFFIXES = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".m", ".mm",
    ".py", ".qml", ".rs",
}
FIXTURE_ROOTS = (
    "tests/fixtures/color",
    "perf-corpora/color-validation-required",
)
SELF_FILES = {
    "scripts/color/validate-proprietary-color-boundary.py",
    "scripts/color/test-proprietary-color-boundary.py",
}

# Adobe RGB, public-DNG field names, and legacy recipe identifiers are intentionally
# not forbidden. They are interoperability contracts, not executable dependencies.
DEPENDENCY_PATTERN = re.compile(
    r"\b(?:adobe[-_ ]?(?:dng|camera[-_ ]?raw|xmp)|"
    r"dng[-_ ]?sdk|xmp[-_ ]?sdk)\b",
    re.IGNORECASE,
)
SDK_INCLUDE_PATTERN = re.compile(
    r"#\s*include\s*[<\"][^>\"]*(?:dng_(?:host|negative|image|"
    r"camera_profile|render|pixel_buffer)|dng[-_]?sdk|xmp[-_]?sdk)[^>\"]*[>\"]",
    re.IGNORECASE,
)
SDK_SYMBOL_PATTERN = re.compile(
    r"\b(?:dng_host|dng_negative|dng_camera_profile|dng_render|"
    r"dng_pixel_buffer)\b"
)
ADOBE_API_PATTERN = re.compile(
    r"https?://[^\s\"']*(?:api\.)?adobe\.com(?:[/:]|$)", re.IGNORECASE
)
FORBIDDEN_FIXTURE_PATH_PATTERN = re.compile(
    r"(?:^|[-_.])(?:adobe|camera[-_.]?raw|dng[-_.]?sdk|xmp[-_.]?sdk)"
    r"(?:[-_.]|$)",
    re.IGNORECASE,
)
PROVENANCE_KEYS = {
    "generator", "generator_name", "oracle", "organization", "producer",
    "reference_engine", "renderer", "source_engine", "tool",
}
FORBIDDEN_PROVENANCE_PATTERN = re.compile(
    r"\b(?:adobe|camera\s*raw|photoshop|lightroom|dng\s*sdk)\b",
    re.IGNORECASE,
)


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("root", nargs="?", type=Path, default=Path("."))
  parser.add_argument("--report", type=Path)
  return parser.parse_args()


def logical_path(root: Path, path: Path) -> str:
  return path.resolve().relative_to(root).as_posix()


def read_text(path: Path, errors: list[str]) -> str | None:
  try:
    return path.read_text(encoding="utf-8")
  except (OSError, UnicodeDecodeError) as exc:
    errors.append(f"unable to inspect {path}: {exc}")
    return None


def dependency_files(root: Path) -> list[Path]:
  paths: set[Path] = set()
  for pattern in DEPENDENCY_GLOBS:
    paths.update(path for path in root.glob(pattern) if path.is_file())
  return sorted(paths)


def source_files(root: Path) -> list[Path]:
  paths: list[Path] = []
  for source_root in SOURCE_ROOTS:
    base = root / source_root
    if not base.is_dir():
      continue
    paths.extend(
        path for path in base.rglob("*")
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES
        and logical_path(root, path) not in SELF_FILES
    )
  return sorted(paths)


def inspect_provenance(
    value: Any, label: str, errors: list[str]
) -> None:
  if isinstance(value, dict):
    for key, child in value.items():
      child_label = f"{label}.{key}"
      if (
          key.lower() in PROVENANCE_KEYS
          and isinstance(child, str)
          and FORBIDDEN_PROVENANCE_PATTERN.search(child)
      ):
        errors.append(
            f"{child_label} names a forbidden Adobe-produced fixture or oracle"
        )
      inspect_provenance(child, child_label, errors)
  elif isinstance(value, list):
    for index, child in enumerate(value):
      inspect_provenance(child, f"{label}[{index}]", errors)


def validate_root(root: Path) -> tuple[list[str], dict[str, int]]:
  root = root.resolve()
  errors: list[str] = []
  dependency_count = 0
  source_count = 0
  fixture_count = 0
  if not (root / "CMakeLists.txt").is_file():
    return ([f"repository root is missing CMakeLists.txt: {root}"], {})

  for path in dependency_files(root):
    dependency_count += 1
    text = read_text(path, errors)
    if text is not None and DEPENDENCY_PATTERN.search(text):
      errors.append(
          f"{logical_path(root, path)} declares a forbidden Adobe SDK dependency"
      )

  for path in source_files(root):
    source_count += 1
    text = read_text(path, errors)
    if text is None:
      continue
    logical = logical_path(root, path)
    if SDK_INCLUDE_PATTERN.search(text):
      errors.append(f"{logical} includes an Adobe DNG/XMP SDK header")
    if SDK_SYMBOL_PATTERN.search(text):
      errors.append(f"{logical} references an Adobe DNG SDK runtime symbol")
    if ADOBE_API_PATTERN.search(text):
      errors.append(f"{logical} references an Adobe network API")

  for fixture_root in FIXTURE_ROOTS:
    base = root / fixture_root
    if not base.is_dir():
      continue
    for path in sorted(value for value in base.rglob("*") if value.is_file()):
      fixture_count += 1
      logical = logical_path(root, path)
      relative_fixture = path.relative_to(base).as_posix()
      if FORBIDDEN_FIXTURE_PATH_PATTERN.search(relative_fixture):
        errors.append(f"{logical} has a forbidden Adobe-generated fixture name")
      if path.suffix.lower() != ".json":
        continue
      try:
        value = json.loads(path.read_text(encoding="utf-8"))
      except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        errors.append(f"unable to inspect fixture provenance in {logical}: {exc}")
        continue
      inspect_provenance(value, logical, errors)

  return errors, {
      "dependency_file_count": dependency_count,
      "runtime_source_file_count": source_count,
      "fixture_file_count": fixture_count,
  }


def main() -> int:
  args = parse_args()
  root = args.root.resolve()
  errors, counts = validate_root(root)
  report = {
      "schema": SCHEMA,
      "repository": root.name,
      "policy": {
          "adobe_api": "forbidden",
          "adobe_generated_fixture": "forbidden",
          "adobe_runtime": "forbidden",
          "adobe_sdk": "forbidden",
          "public_dng_specification": "interoperability-reference-only",
      },
      "inspection": counts,
      "errors": errors,
      "result": "failed" if errors else "passed",
  }
  if args.report is not None:
    if args.report.exists():
      print(f"error=report already exists: {args.report}", file=sys.stderr)
      return 1
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
  if errors:
    for error in errors:
      print(f"error={error}", file=sys.stderr)
    return 1
  print(
      "status=valid-proprietary-color-boundary "
      f"dependencies={counts['dependency_file_count']} "
      f"sources={counts['runtime_source_file_count']} "
      f"fixtures={counts['fixture_file_count']}"
  )
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
