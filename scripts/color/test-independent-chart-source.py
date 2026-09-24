#!/usr/bin/env python3
"""Adversarial self-test for the independent chart source and fetcher."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import struct
import sys
import tempfile
import zlib
import zipfile
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
MANIFEST_PATH = (
    REPO_ROOT / "perf-corpora" / "color-validation-required" /
    "independent-chart-source.json"
)
VALIDATOR_PATH = Path(__file__).with_name("validate-independent-chart-source.py")
FETCHER_PATH = Path(__file__).with_name("fetch-independent-chart-source.py")


def require(condition: bool, message: str) -> None:
  if not condition:
    raise SystemExit(message)


def load_module(name: str, path: Path) -> Any:
  spec = importlib.util.spec_from_file_location(name, path)
  if spec is None or spec.loader is None:
    raise SystemExit(f"unable to import {path}")
  module = importlib.util.module_from_spec(spec)
  sys.modules[name] = module
  spec.loader.exec_module(module)
  return module


def write_json(path: Path, payload: dict[str, Any]) -> None:
  path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def png_chunk(kind: bytes, payload: bytes) -> bytes:
  body = kind + payload
  return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body))


def solid_rgb_png(width: int, height: int, seed: int) -> bytes:
  color = bytes(((seed * 17) % 256, (seed * 29) % 256, (seed * 43) % 256))
  row = b"\0" + color * width
  return (
      b"\x89PNG\r\n\x1a\n" +
      png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
      png_chunk(b"IDAT", zlib.compress(row * height, 9)) +
      png_chunk(b"IEND", b"")
  )


def fake_members(validator: Any, manifest: dict[str, Any]) -> dict[str, bytes]:
  result: dict[str, bytes] = {}
  for index, name in enumerate(validator.selected_member_names(manifest)):
    result[name] = (
        solid_rgb_png(606, 404, index)
        if name.endswith(".png")
        else f"{index / 1000.0:.4f} 0.2000 0.3000\n".encode("ascii")
    )
  return result


def write_archive(path: Path, members: dict[str, bytes]) -> None:
  with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
    for name, payload in sorted(members.items()):
      archive.writestr(name, payload)


def pin_fake_bytes(
    manifest: dict[str, Any], archive: Path, members: dict[str, bytes], validator: Any
) -> None:
  manifest["dataset"]["archive_byte_size"] = archive.stat().st_size
  manifest["dataset"]["archive_sha256"] = validator.sha256_file(archive)
  total_bytes, digest = validator.member_set_digest(list(members.items()))
  manifest["selection"]["selected_byte_size"] = total_bytes
  manifest["selection"]["selected_member_set_sha256"] = digest


def has_error(errors: list[str], text: str) -> bool:
  return any(text in error for error in errors)


def main() -> int:
  validator = load_module("independent_chart_validator_test", VALIDATOR_PATH)
  fetcher = load_module("independent_chart_fetcher_test", FETCHER_PATH)
  with MANIFEST_PATH.open("r", encoding="utf-8") as handle:
    baseline = json.load(handle)
  require(
      not validator.validate_manifest(MANIFEST_PATH),
      "checked-in independent chart manifest was rejected",
  )

  with tempfile.TemporaryDirectory(prefix="photo-editor-chart-source-") as temp:
    root = Path(temp)

    def validate_mutation(
        name: str, payload: dict[str, Any], expected_error: str
    ) -> None:
      path = root / f"{name}.json"
      write_json(path, payload)
      errors = validator.validate_manifest(path)
      require(
          has_error(errors, expected_error),
          f"{name} mutation was not rejected for {expected_error!r}: {errors}",
      )

    promoted = copy.deepcopy(baseline)
    promoted["promotion_eligible"] = True
    validate_mutation("false-promotion", promoted, "must not claim promotion")

    substituted = copy.deepcopy(baseline)
    substituted["dataset"]["archive_url"] = "https://example.invalid/chart.zip"
    validate_mutation("substituted-host", substituted, "authoritative archive")

    duplicate_manufacturer = copy.deepcopy(baseline)
    duplicate_manufacturer["selection"]["cameras"][1]["manufacturer"] = "Canon"
    validate_mutation(
        "duplicate-manufacturer", duplicate_manufacturer,
        "six unique camera directories and manufacturers",
    )

    stale_count = copy.deepcopy(baseline)
    stale_count["selection"]["selected_member_count"] -= 1
    validate_mutation("stale-count", stale_count, "required matrix")

    fake_manifest = copy.deepcopy(baseline)
    members = fake_members(validator, fake_manifest)
    archive = root / "source.zip"
    write_archive(archive, members)
    pin_fake_bytes(fake_manifest, archive, members, validator)
    fake_manifest_path = root / "fake-manifest.json"
    write_json(fake_manifest_path, fake_manifest)
    errors = validator.validate_manifest(fake_manifest_path, archive_path=archive)
    require(not errors, f"valid fake archive was rejected: {errors}")

    malformed_members = dict(members)
    malformed_name = next(name for name in malformed_members if name.endswith(".png"))
    malformed_payload = bytearray(malformed_members[malformed_name])
    malformed_payload[-5] ^= 1
    malformed_members[malformed_name] = bytes(malformed_payload)
    malformed_archive = root / "malformed.zip"
    write_archive(malformed_archive, malformed_members)
    malformed_manifest = copy.deepcopy(fake_manifest)
    pin_fake_bytes(malformed_manifest, malformed_archive, malformed_members, validator)
    malformed_manifest_path = root / "malformed.json"
    write_json(malformed_manifest_path, malformed_manifest)
    errors = validator.validate_manifest(
        malformed_manifest_path, archive_path=malformed_archive
    )
    require(
        has_error(errors, "PNG chunk CRC mismatch"),
        f"malformed PNG was not rejected independently of hashes: {errors}",
    )

    output = root / "materialized"
    summary = fetcher.materialize_source(
        fake_manifest_path, output, 1.0, archive_path=archive
    )
    require(summary["selected_member_count"] == 192, "fetcher omitted selected members")
    require(summary["promotion_eligible"] is False, "fetcher claimed promotion eligibility")
    errors = validator.validate_manifest(fake_manifest_path, materialized_root=output)
    require(not errors, f"materialized fake archive was rejected: {errors}")

    first_name = validator.selected_member_names(fake_manifest)[0]
    first_path = output / "members" / first_name
    payload = first_path.read_bytes()
    first_path.write_bytes(payload[:-1] + bytes((payload[-1] ^ 1,)))
    errors = validator.validate_manifest(fake_manifest_path, materialized_root=output)
    require(
        has_error(errors, "member-set sha256 mismatch"),
        f"same-size member tamper was not rejected: {errors}",
    )

    unsafe_members = dict(members)
    unsafe_members["../escape.txt"] = b"escape\n"
    unsafe_archive = root / "unsafe.zip"
    write_archive(unsafe_archive, unsafe_members)
    unsafe_manifest = copy.deepcopy(fake_manifest)
    unsafe_manifest["dataset"]["archive_byte_size"] = unsafe_archive.stat().st_size
    unsafe_manifest["dataset"]["archive_sha256"] = validator.sha256_file(unsafe_archive)
    unsafe_manifest_path = root / "unsafe.json"
    write_json(unsafe_manifest_path, unsafe_manifest)
    errors = validator.validate_manifest(unsafe_manifest_path, archive_path=unsafe_archive)
    require(has_error(errors, "unsafe member path"), f"path escape was not rejected: {errors}")

    missing_members = dict(members)
    missing_members.pop(next(iter(missing_members)))
    missing_archive = root / "missing.zip"
    write_archive(missing_archive, missing_members)
    missing_manifest = copy.deepcopy(fake_manifest)
    missing_manifest["dataset"]["archive_byte_size"] = missing_archive.stat().st_size
    missing_manifest["dataset"]["archive_sha256"] = validator.sha256_file(missing_archive)
    missing_manifest_path = root / "missing.json"
    write_json(missing_manifest_path, missing_manifest)
    errors = validator.validate_manifest(missing_manifest_path, archive_path=missing_archive)
    require(has_error(errors, "missing 1 selected members"), f"missing member was not rejected: {errors}")

  print("independent_chart_source_self_test=passed")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
