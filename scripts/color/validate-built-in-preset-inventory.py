#!/usr/bin/env python3
"""Validate the immutable inventory of built-in photographic presets."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


SCHEMA = "photo-editor-built-in-preset-inventory:v1"
SOURCE_PATH = "apps/desktop/src/preset_registry.cpp"
BEGIN = "// PHOTO_EDITOR_BUILT_IN_PRESET_REGISTRY_BEGIN"
END = "// PHOTO_EDITOR_BUILT_IN_PRESET_REGISTRY_END"
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
PRESET_PATTERN = re.compile(
    r"presets\.push_back\(\{\s*"
    r"QStringLiteral\(\"([^\"]+)\"\),\s*"
    r"QStringLiteral\(\"([^\"]+)\"\),\s*"
    r"QStringLiteral\(\"([^\"]+)\"\),",
    re.DOTALL,
)
ROOT_KEYS = {
    "schema", "registry_id", "source_path", "source_region_sha256",
    "preset_count", "presets",
}
PRESET_KEYS = {"name", "pack", "category"}


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument(
      "manifest", nargs="?", type=Path,
      default=Path("perf-corpora/color-validation-required/built-in-preset-inventory.json"),
  )
  parser.add_argument("--repository-root", type=Path, default=Path("."))
  parser.add_argument("--print-source-hash", action="store_true")
  return parser.parse_args()


def exact_keys(
    value: Any, keys: set[str], label: str, errors: list[str]
) -> dict[str, Any]:
  if not isinstance(value, dict):
    errors.append(f"{label} must be an object")
    return {}
  for key in sorted(set(value) - keys):
    errors.append(f"{label}: unknown field {key!r}")
  for key in sorted(keys - set(value)):
    errors.append(f"{label}: missing required field {key!r}")
  return value


def source_registry(source_path: Path) -> tuple[str, list[dict[str, str]]]:
  text = source_path.read_text(encoding="utf-8")
  begin = text.find(BEGIN)
  end = text.find(END, begin + len(BEGIN))
  if begin < 0 or end < 0 or text.find(BEGIN, begin + 1) >= 0 or text.find(END, end + 1) >= 0:
    raise ValueError("preset registry must have exactly one begin/end marker pair")
  end += len(END)
  if end < len(text) and text[end] == "\n":
    end += 1
  region = text[begin:end].encode("utf-8")
  presets = [
      {"name": name, "pack": pack, "category": category}
      for name, pack, category in PRESET_PATTERN.findall(region.decode("utf-8"))
  ]
  return hashlib.sha256(region).hexdigest(), presets


def validate_manifest(
    manifest_path: Path, repository_root: Path
) -> list[str]:
  errors: list[str] = []
  try:
    with manifest_path.open("r", encoding="utf-8") as handle:
      manifest = json.load(handle)
  except (OSError, json.JSONDecodeError) as exc:
    return [f"unable to read built-in preset inventory: {exc}"]
  manifest = exact_keys(manifest, ROOT_KEYS, "manifest", errors)
  if manifest.get("schema") != SCHEMA:
    errors.append("manifest must use the v1 built-in preset inventory schema")
  if not isinstance(manifest.get("registry_id"), str) or not manifest["registry_id"]:
    errors.append("manifest.registry_id must be non-empty")
  if manifest.get("source_path") != SOURCE_PATH:
    errors.append(f"manifest.source_path must be {SOURCE_PATH}")
  source_path = repository_root.resolve() / SOURCE_PATH
  try:
    source_hash, source_presets = source_registry(source_path)
  except (OSError, UnicodeDecodeError, ValueError) as exc:
    errors.append(f"unable to inspect built-in preset source registry: {exc}")
    return errors
  digest = manifest.get("source_region_sha256")
  if not isinstance(digest, str) or SHA256_PATTERN.fullmatch(digest) is None:
    errors.append("manifest.source_region_sha256 must be a SHA-256 digest")
  elif digest != source_hash:
    errors.append("built-in preset source-region SHA-256 mismatch")
  values = manifest.get("presets")
  if not isinstance(values, list) or not values:
    errors.append("manifest.presets must be a non-empty array")
    values = []
  parsed: list[dict[str, str]] = []
  for index, value in enumerate(values):
    preset = exact_keys(value, PRESET_KEYS, f"manifest.presets[{index}]", errors)
    if all(isinstance(preset.get(key), str) and preset[key] for key in PRESET_KEYS):
      parsed.append({key: preset[key] for key in ("name", "pack", "category")})
    else:
      errors.append(f"manifest.presets[{index}] fields must be non-empty strings")
  names = [value["name"] for value in parsed]
  if len(names) != len(set(names)):
    errors.append("manifest.presets contains duplicate names")
  if manifest.get("preset_count") != len(parsed):
    errors.append("manifest.preset_count does not match its preset array")
  if parsed != source_presets:
    errors.append("built-in preset inventory does not match executable source order and metadata")
  return errors


def main() -> int:
  args = parse_args()
  if args.print_source_hash:
    try:
      digest, _ = source_registry(args.repository_root.resolve() / SOURCE_PATH)
    except (OSError, UnicodeDecodeError, ValueError) as exc:
      print(f"error={exc}", file=sys.stderr)
      return 1
    print(digest)
    return 0
  errors = validate_manifest(args.manifest, args.repository_root)
  if errors:
    for error in errors:
      print(f"error={error}", file=sys.stderr)
    return 1
  print("status=valid-built-in-preset-inventory")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
