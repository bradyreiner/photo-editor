"""Shared byte-exact hashing and fail-closed JSON contracts for color proofs."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any


def artifact_set_digest(root: Path, paths: list[str]) -> tuple[int, str]:
  total = 0
  digest = hashlib.sha256()
  for logical_path in sorted(paths):
    path = root / logical_path
    size = path.stat().st_size
    digest.update(logical_path.encode("utf-8"))
    digest.update(b"\0")
    digest.update(str(size).encode("ascii"))
    digest.update(b"\0")
    digest.update(sha256_file(path).encode("ascii"))
    digest.update(b"\n")
    total += size
  return total, digest.hexdigest()


def exact_keys(
    value: Any, keys: set[str], label: str, errors: list[str]
) -> dict[str, Any] | None:
  if not isinstance(value, dict):
    errors.append(f"{label} must be an object")
    return None
  for key in sorted(set(value) - keys):
    errors.append(f"{label}: unknown field {key!r}")
  for key in sorted(keys - set(value)):
    errors.append(f"{label}: missing required field {key!r}")
  return value


def exact_object(
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


def load_json_with_errors(path: Path, label: str, errors: list[str]) -> dict[str, Any] | None:
  try:
    with path.open("r", encoding="utf-8") as handle:
      value = json.load(handle)
  except (OSError, json.JSONDecodeError) as exc:
    errors.append(f"unable to read {label}: {exc}")
    return None
  if not isinstance(value, dict):
    errors.append(f"{label} root must be an object")
    return None
  return value


def read_json_object(path: Path) -> dict[str, Any]:
  with path.open("r", encoding="utf-8") as handle:
    value = json.load(handle)
  if not isinstance(value, dict):
    raise ValueError(f"JSON root must be an object: {path}")
  return value


def sha256_file(path: Path) -> str:
  digest = hashlib.sha256()
  with path.open("rb") as handle:
    for block in iter(lambda: handle.read(1024 * 1024), b""):
      digest.update(block)
  return digest.hexdigest()
