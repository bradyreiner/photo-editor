#!/usr/bin/env python3
"""Verify and inventory an exported, tracked-only Looksy source tree."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import tomllib
import unicodedata
from pathlib import Path
from typing import Any


CONTRACT = "framehouse-source-closure-v1"


def sha256_file(path: Path) -> str:
  digest = hashlib.sha256()
  with path.open("rb") as handle:
    for chunk in iter(lambda: handle.read(1024 * 1024), b""):
      digest.update(chunk)
  return digest.hexdigest()


def source_entries(source_root: Path, output_path: Path) -> list[dict[str, Any]]:
  entries: list[dict[str, Any]] = []
  for path in sorted(source_root.rglob("*")):
    if path == output_path or path.is_dir():
      continue
    relative_path = path.relative_to(source_root).as_posix()
    if path.is_symlink():
      entries.append(
        {
          "path": relative_path,
          "kind": "symlink",
          "target": os.readlink(path),
        }
      )
      continue
    entries.append(
      {
        "path": relative_path,
        "kind": "file",
        "sizeBytes": path.stat().st_size,
        "sha256": sha256_file(path),
      }
    )
  return entries


def iter_path_dependencies(value: Any) -> list[str]:
  paths: list[str] = []
  if isinstance(value, dict):
    dependency_path = value.get("path")
    if isinstance(dependency_path, str):
      paths.append(dependency_path)
    for nested in value.values():
      paths.extend(iter_path_dependencies(nested))
  elif isinstance(value, list):
    for nested in value:
      paths.extend(iter_path_dependencies(nested))
  return paths


def validate_cargo_sources(source_root: Path, errors: list[str]) -> None:
  rust_root = source_root / "rust"
  workspace_manifest = rust_root / "Cargo.toml"
  lockfile = rust_root / "Cargo.lock"
  if not workspace_manifest.is_file():
    errors.append("rust/Cargo.toml is missing")
    return
  if not lockfile.is_file():
    errors.append("rust/Cargo.lock is missing")

  try:
    workspace = tomllib.loads(workspace_manifest.read_text(encoding="utf-8"))
  except (OSError, tomllib.TOMLDecodeError) as error:
    errors.append(f"rust/Cargo.toml cannot be parsed: {error}")
    return

  members = workspace.get("workspace", {}).get("members", [])
  if not isinstance(members, list):
    errors.append("rust workspace members must be a list")
    return
  for member in members:
    if not isinstance(member, str):
      errors.append("rust workspace member is not a string")
      continue
    manifest = rust_root / member / "Cargo.toml"
    if not manifest.is_file():
      errors.append(f"rust workspace member is absent from exported source: {member}")

  for manifest in sorted(rust_root.rglob("Cargo.toml")):
    try:
      document = tomllib.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, tomllib.TOMLDecodeError) as error:
      errors.append(f"{manifest.relative_to(source_root)} cannot be parsed: {error}")
      continue
    for dependency_path in iter_path_dependencies(document):
      resolved = (manifest.parent / dependency_path).resolve(strict=False)
      try:
        resolved.relative_to(source_root)
      except ValueError:
        errors.append(
          f"{manifest.relative_to(source_root)} has an out-of-tree path dependency: "
          f"{dependency_path}"
        )
        continue
      if not (resolved / "Cargo.toml").is_file():
        errors.append(
          f"{manifest.relative_to(source_root)} has a missing path dependency: "
          f"{dependency_path}"
        )


def validate_portable_paths(entries: list[dict[str, Any]], errors: list[str]) -> None:
  portable_paths: dict[str, str] = {}
  for entry in entries:
    path = entry["path"]
    portable_key = unicodedata.normalize("NFC", path).casefold()
    previous = portable_paths.get(portable_key)
    if previous is not None and previous != path:
      errors.append(f"non-portable source path collision: {previous} and {path}")
    portable_paths[portable_key] = path


def validate_symlinks(
  source_root: Path, entries: list[dict[str, Any]], errors: list[str]
) -> None:
  for entry in entries:
    if entry["kind"] != "symlink":
      continue
    path = source_root / entry["path"]
    resolved = path.resolve(strict=False)
    try:
      resolved.relative_to(source_root)
    except ValueError:
      errors.append(
        f"exported source symlink escapes the source root: {entry['path']} -> "
        f"{entry['target']}"
      )


def main() -> int:
  parser = argparse.ArgumentParser()
  parser.add_argument("--source-root", type=Path, required=True)
  parser.add_argument("--output", type=Path, required=True)
  parser.add_argument("--tree", default="unknown")
  args = parser.parse_args()

  source_root = args.source_root.resolve()
  output_path = args.output.resolve()
  errors: list[str] = []
  if not source_root.is_dir():
    print(f"source closure: source root is not a directory: {source_root}", file=sys.stderr)
    return 2

  entries = source_entries(source_root, output_path)
  validate_portable_paths(entries, errors)
  validate_symlinks(source_root, entries, errors)
  validate_cargo_sources(source_root, errors)

  lockfile = source_root / "rust/Cargo.lock"
  report = {
    "contract": CONTRACT,
    "tree": args.tree,
    "sourceFileCount": len(entries),
    "cargoLockSha256": sha256_file(lockfile) if lockfile.is_file() else "",
    "passed": not errors,
    "errors": errors,
    "files": entries,
  }
  output_path.parent.mkdir(parents=True, exist_ok=True)
  output_path.write_text(
    json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
  )

  if errors:
    for error in errors:
      print(f"source closure: error: {error}", file=sys.stderr)
    return 1
  print(
    f"source closure: {len(entries)} tracked files inventoried; "
    f"Cargo.lock sha256={report['cargoLockSha256']}"
  )
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
