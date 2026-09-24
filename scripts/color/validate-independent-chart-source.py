#!/usr/bin/env python3
"""Validate the pinned, non-shipping independent chart reference source."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import re
import struct
import sys
import zipfile
import zlib
from pathlib import Path, PurePosixPath
from typing import Any
from urllib.parse import urlparse


SCHEMA = "photo-editor-independent-chart-source:v1"
SOURCE_PAGE = "https://vision.middlebury.edu/color/data/"
ARCHIVE_URL = (
    "https://vision.middlebury.edu/color/data/checker/zip/checker24-RAW-JPG.zip"
)
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
DIRECTORY_PATTERN = re.compile(r"^[A-Za-z0-9-]+$")
ROOT_KEYS = {
    "schema", "inventory_id", "description", "promotion_eligible",
    "promotion_blockers", "dataset", "selection",
}
DATASET_KEYS = {
    "dataset_id", "name", "source_page", "archive_url", "archive_sha256",
    "archive_byte_size", "independent_producer", "attribution", "citation_url",
    "distribution", "permission_evidence", "content_contract",
}
PERMISSION_KEYS = {
    "status", "summary", "evidence_url", "accessed_on", "project_review",
}
REVIEW_KEYS = {"status", "reviewed_on", "reviewer_id"}
CONTENT_KEYS = {
    "target", "registered_width", "registered_height", "png_bit_depth",
    "png_color_type", "linear_render_description", "camera_render_description",
    "embedded_color_profile", "sensor_raw_files_in_archive",
}
SELECTION_KEYS = {
    "white_balance_code", "white_balance_description", "cameras", "illuminants",
    "exposures", "artifacts", "selected_member_count", "selected_byte_size",
    "selected_member_set_sha256", "member_set_hash_algorithm",
}
CAMERA_KEYS = {"directory", "manufacturer", "model"}
ILLUMINANT_KEYS = {"code", "kelvin", "description"}
EXPOSURE_KEYS = {"code", "stops"}
ARTIFACT_KEYS = {"suffix", "role"}
EXPECTED_ILLUMINANTS = {"i1": 3200, "i2": 4800}
EXPECTED_EXPOSURES = {"e1": -2, "e2": -1, "e3": 0, "e4": 1}
EXPECTED_ARTIFACTS = {
    "-raw.png": "independent-linear-render",
    "-raw-std.txt": "independent-linear-noise-estimate",
    "-jpg.png": "camera-jpeg-render",
    "-jpg-std.txt": "camera-jpeg-noise-estimate",
}
REQUIRED_BLOCKERS = {
    "no-sensor-raw-files",
    "untagged-reference-images",
    "eight-bit-registered-chart-crops",
    "no-measured-spectral-patch-values",
    "project-license-review-pending",
    "colorchecker-target-rights-review-pending",
    "no-real-dcp-profile",
    "no-rights-cleared-skin-capture",
}


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(
      description="Validate the independent Middlebury chart-source contract."
  )
  parser.add_argument("manifest", type=Path)
  parser.add_argument("--archive", type=Path)
  parser.add_argument("--materialized-root", type=Path)
  return parser.parse_args()


def sha256_file(path: Path) -> str:
  digest = hashlib.sha256()
  with path.open("rb") as handle:
    for block in iter(lambda: handle.read(1024 * 1024), b""):
      digest.update(block)
  return digest.hexdigest()


def check_keys(
    value: dict[str, Any], allowed: set[str], required: set[str], label: str,
    errors: list[str]
) -> None:
  for key in sorted(set(value) - allowed):
    errors.append(f"{label}: unknown field {key!r}")
  for key in sorted(required - set(value)):
    errors.append(f"{label}: missing required field {key!r}")


def require_string(value: Any, label: str, errors: list[str]) -> str | None:
  if not isinstance(value, str) or not value.strip():
    errors.append(f"{label} must be a non-empty string")
    return None
  return value


def require_https(value: Any, label: str, errors: list[str]) -> str | None:
  text = require_string(value, label, errors)
  if text is None:
    return None
  parsed = urlparse(text)
  if parsed.scheme != "https" or not parsed.netloc or parsed.username or parsed.password:
    errors.append(f"{label} must be an absolute credential-free HTTPS URL")
    return None
  if parsed.query or parsed.fragment:
    errors.append(f"{label} must not contain a query or fragment")
  return text


def require_date(value: Any, label: str, errors: list[str]) -> None:
  text = require_string(value, label, errors)
  if text is None:
    return
  try:
    dt.date.fromisoformat(text)
  except ValueError:
    errors.append(f"{label} must use YYYY-MM-DD")


def load_manifest(path: Path) -> tuple[dict[str, Any] | None, list[str]]:
  try:
    with path.open("r", encoding="utf-8") as handle:
      value = json.load(handle)
  except (OSError, json.JSONDecodeError) as exc:
    return None, [f"unable to read chart inventory: {exc}"]
  if not isinstance(value, dict):
    return None, ["chart inventory root must be an object"]
  return value, []


def selected_member_names(manifest: dict[str, Any]) -> list[str]:
  selection = manifest["selection"]
  return sorted(
      f"checker24/{camera['directory']}/{selection['white_balance_code']}"
      f"{illuminant['code']}{exposure['code']}{artifact['suffix']}"
      for camera in selection["cameras"]
      for illuminant in selection["illuminants"]
      for exposure in selection["exposures"]
      for artifact in selection["artifacts"]
  )


def member_set_digest(entries: list[tuple[str, bytes]]) -> tuple[int, str]:
  digest = hashlib.sha256()
  total_bytes = 0
  for name, payload in sorted(entries):
    member_digest = hashlib.sha256(payload).hexdigest()
    digest.update(name.encode("utf-8"))
    digest.update(b"\0")
    digest.update(str(len(payload)).encode("ascii"))
    digest.update(b"\0")
    digest.update(member_digest.encode("ascii"))
    digest.update(b"\n")
    total_bytes += len(payload)
  return total_bytes, digest.hexdigest()


def validate_png(payload: bytes, content: dict[str, Any], label: str) -> str | None:
  if len(payload) < 33 or payload[:8] != b"\x89PNG\r\n\x1a\n" or payload[12:16] != b"IHDR":
    return f"{label}: invalid PNG signature or IHDR"
  width, height, bit_depth, color_type, compression, filtering, interlace = (
      struct.unpack(">IIBBBBB", payload[16:29])
  )
  if (
      width != content["registered_width"] or
      height != content["registered_height"] or
      bit_depth != content["png_bit_depth"] or color_type != 2 or
      compression != 0 or filtering != 0 or interlace != 0
  ):
    return f"{label}: PNG content contract mismatch"
  offset = 8
  chunks: list[bytes] = []
  while offset + 12 <= len(payload):
    size = struct.unpack(">I", payload[offset:offset + 4])[0]
    end = offset + 12 + size
    if end > len(payload):
      return f"{label}: PNG chunk exceeds the payload"
    kind = payload[offset + 4:offset + 8]
    chunk_payload = payload[offset + 8:offset + 8 + size]
    expected_crc = struct.unpack(">I", payload[offset + 8 + size:end])[0]
    if zlib.crc32(kind + chunk_payload) != expected_crc:
      return f"{label}: PNG chunk CRC mismatch"
    chunks.append(kind)
    offset = end
    if kind == b"IEND":
      break
  if offset != len(payload) or not chunks or chunks[0] != b"IHDR" or b"IDAT" not in chunks or chunks[-1] != b"IEND":
    return f"{label}: PNG chunk structure is incomplete"
  if content["embedded_color_profile"] is False and any(
      kind in chunks for kind in (b"iCCP", b"sRGB", b"gAMA", b"cHRM")
  ):
    return f"{label}: PNG unexpectedly contains color-profile metadata"
  return None


def validate_noise_text(payload: bytes, label: str) -> str | None:
  try:
    values = payload.decode("ascii").strip().split()
    numbers = [float(value) for value in values]
  except (UnicodeDecodeError, ValueError):
    return f"{label}: invalid ASCII noise estimate"
  if len(numbers) != 3 or any(value < 0.0 for value in numbers):
    return f"{label}: noise estimate must contain three non-negative channels"
  return None


def validate_member_payloads(
    entries: list[tuple[str, bytes]], content: dict[str, Any], errors: list[str]
) -> None:
  for name, payload in entries:
    error = (
        validate_png(payload, content, name)
        if name.endswith(".png")
        else validate_noise_text(payload, name)
    )
    if error is not None:
      errors.append(error)


def validate_archive(
    archive_path: Path, manifest: dict[str, Any], errors: list[str]
) -> None:
  dataset = manifest["dataset"]
  if not archive_path.is_file():
    errors.append(f"archive is missing: {archive_path}")
    return
  if archive_path.stat().st_size != dataset["archive_byte_size"]:
    errors.append("archive byte-size mismatch")
  if sha256_file(archive_path) != dataset["archive_sha256"]:
    errors.append("archive sha256 mismatch")
  try:
    with zipfile.ZipFile(archive_path) as archive:
      infos = archive.infolist()
      names = [info.filename for info in infos]
      if len(names) != len(set(names)):
        errors.append("archive contains duplicate member paths")
      for info in infos:
        path = PurePosixPath(info.filename)
        if path.is_absolute() or ".." in path.parts or "\\" in info.filename:
          errors.append(f"archive contains unsafe member path: {info.filename}")
      required = selected_member_names(manifest)
      missing = sorted(set(required) - set(names))
      if missing:
        errors.append(f"archive is missing {len(missing)} selected members")
        return
      entries = [(name, archive.read(name)) for name in required]
  except (OSError, zipfile.BadZipFile, RuntimeError) as exc:
    errors.append(f"unable to inspect archive: {exc}")
    return
  validate_selected_entries(entries, manifest, errors)


def validate_selected_entries(
    entries: list[tuple[str, bytes]], manifest: dict[str, Any], errors: list[str]
) -> None:
  selection = manifest["selection"]
  if len(entries) != selection["selected_member_count"]:
    errors.append("selected member count mismatch")
  total_bytes, digest = member_set_digest(entries)
  if total_bytes != selection["selected_byte_size"]:
    errors.append("selected member byte-size mismatch")
  if digest != selection["selected_member_set_sha256"]:
    errors.append("selected member-set sha256 mismatch")
  validate_member_payloads(entries, manifest["dataset"]["content_contract"], errors)


def validate_materialized(
    root: Path, manifest: dict[str, Any], errors: list[str]
) -> None:
  members_root = root / "members"
  required = selected_member_names(manifest)
  actual = sorted(
      path.relative_to(members_root).as_posix()
      for path in members_root.rglob("*")
      if path.is_file()
  ) if members_root.is_dir() else []
  if actual != required:
    errors.append("materialized member path set does not match the selection")
    return
  entries = [(name, (members_root / name).read_bytes()) for name in required]
  validate_selected_entries(entries, manifest, errors)


def validate_manifest(
    manifest_path: Path,
    archive_path: Path | None = None,
    materialized_root: Path | None = None,
) -> list[str]:
  manifest, errors = load_manifest(manifest_path)
  if manifest is None:
    return errors
  check_keys(manifest, ROOT_KEYS, ROOT_KEYS, "inventory", errors)
  if manifest.get("schema") != SCHEMA:
    errors.append(f"inventory.schema must be {SCHEMA!r}")
  for key in ("inventory_id", "description"):
    require_string(manifest.get(key), f"inventory.{key}", errors)
  if manifest.get("promotion_eligible") is not False:
    errors.append("independent chart source must not claim promotion eligibility")
  blockers = manifest.get("promotion_blockers")
  if not isinstance(blockers, list) or set(blockers) != REQUIRED_BLOCKERS:
    errors.append("inventory.promotion_blockers must record the exact unresolved gates")

  dataset = manifest.get("dataset")
  if not isinstance(dataset, dict):
    errors.append("inventory.dataset must be an object")
    return errors
  check_keys(dataset, DATASET_KEYS, DATASET_KEYS, "dataset", errors)
  for key in ("dataset_id", "name", "independent_producer", "attribution"):
    require_string(dataset.get(key), f"dataset.{key}", errors)
  source_page = require_https(dataset.get("source_page"), "dataset.source_page", errors)
  archive_url = require_https(dataset.get("archive_url"), "dataset.archive_url", errors)
  require_https(dataset.get("citation_url"), "dataset.citation_url", errors)
  if source_page != SOURCE_PAGE:
    errors.append("dataset.source_page must remain pinned to the authoritative page")
  if archive_url != ARCHIVE_URL:
    errors.append("dataset.archive_url must remain pinned to the authoritative archive")
  if not isinstance(dataset.get("archive_sha256"), str) or SHA256_PATTERN.fullmatch(
      dataset.get("archive_sha256", "")
  ) is None:
    errors.append("dataset.archive_sha256 must be a lowercase SHA-256 digest")
  if not isinstance(dataset.get("archive_byte_size"), int) or isinstance(
      dataset.get("archive_byte_size"), bool
  ) or dataset.get("archive_byte_size", 0) <= 0:
    errors.append("dataset.archive_byte_size must be a positive integer")
  if dataset.get("distribution") != "private-ci-only":
    errors.append("dataset.distribution must be 'private-ci-only'")

  permission = dataset.get("permission_evidence")
  if not isinstance(permission, dict):
    errors.append("dataset.permission_evidence must be an object")
  else:
    check_keys(permission, PERMISSION_KEYS, PERMISSION_KEYS, "permission_evidence", errors)
    if permission.get("status") != "explicit-use-and-publish-permission":
      errors.append("permission evidence status must remain explicit and pending review")
    require_string(permission.get("summary"), "permission_evidence.summary", errors)
    if require_https(permission.get("evidence_url"), "permission_evidence.evidence_url", errors) != SOURCE_PAGE:
      errors.append("permission evidence must point to the authoritative dataset page")
    require_date(permission.get("accessed_on"), "permission_evidence.accessed_on", errors)
    review = permission.get("project_review")
    if not isinstance(review, dict):
      errors.append("permission_evidence.project_review must be an object")
    else:
      check_keys(review, REVIEW_KEYS, REVIEW_KEYS, "project_review", errors)
      if review.get("status") != "pending-project-review":
        errors.append("automated inventory cannot claim project license approval")
      require_date(review.get("reviewed_on"), "project_review.reviewed_on", errors)
      require_string(review.get("reviewer_id"), "project_review.reviewer_id", errors)

  content = dataset.get("content_contract")
  if not isinstance(content, dict):
    errors.append("dataset.content_contract must be an object")
  else:
    check_keys(content, CONTENT_KEYS, CONTENT_KEYS, "content_contract", errors)
    expected_content = {
        "target": "X-Rite ColorChecker 24",
        "registered_width": 606,
        "registered_height": 404,
        "png_bit_depth": 8,
        "png_color_type": "rgb",
        "embedded_color_profile": False,
        "sensor_raw_files_in_archive": False,
    }
    for key, expected in expected_content.items():
      if content.get(key) != expected:
        errors.append(f"content_contract.{key} must be {expected!r}")
    for key in ("linear_render_description", "camera_render_description"):
      require_string(content.get(key), f"content_contract.{key}", errors)

  selection = manifest.get("selection")
  if not isinstance(selection, dict):
    errors.append("inventory.selection must be an object")
    return errors
  check_keys(selection, SELECTION_KEYS, SELECTION_KEYS, "selection", errors)
  if selection.get("white_balance_code") != "wb1":
    errors.append("selection.white_balance_code must be 'wb1'")
  require_string(selection.get("white_balance_description"), "selection.white_balance_description", errors)
  cameras = selection.get("cameras")
  if not isinstance(cameras, list) or len(cameras) != 6:
    errors.append("selection.cameras must contain exactly six cameras")
    cameras = []
  directories: set[str] = set()
  manufacturers: set[str] = set()
  for index, camera in enumerate(cameras):
    label = f"cameras[{index}]"
    if not isinstance(camera, dict):
      errors.append(f"{label} must be an object")
      continue
    check_keys(camera, CAMERA_KEYS, CAMERA_KEYS, label, errors)
    directory = require_string(camera.get("directory"), f"{label}.directory", errors)
    manufacturer = require_string(camera.get("manufacturer"), f"{label}.manufacturer", errors)
    require_string(camera.get("model"), f"{label}.model", errors)
    if directory is not None:
      if DIRECTORY_PATTERN.fullmatch(directory) is None:
        errors.append(f"{label}.directory is unsafe")
      directories.add(directory)
    if manufacturer is not None:
      manufacturers.add(manufacturer)
  if len(directories) != 6 or len(manufacturers) != 6:
    errors.append("selection must span six unique camera directories and manufacturers")

  def keyed_values(key: str, field: str) -> dict[Any, Any]:
    value = selection.get(key)
    if not isinstance(value, list):
      errors.append(f"selection.{key} must be an array")
      return {}
    result: dict[Any, Any] = {}
    expected_keys = {
        "illuminants": ILLUMINANT_KEYS,
        "exposures": EXPOSURE_KEYS,
        "artifacts": ARTIFACT_KEYS,
    }[key]
    for index, entry in enumerate(value):
      label = f"{key}[{index}]"
      if not isinstance(entry, dict):
        errors.append(f"{label} must be an object")
        continue
      check_keys(entry, expected_keys, expected_keys, label, errors)
      result[entry.get(field)] = entry
    return result

  illuminants = keyed_values("illuminants", "code")
  exposures = keyed_values("exposures", "code")
  artifacts = keyed_values("artifacts", "suffix")
  if {code: value.get("kelvin") for code, value in illuminants.items()} != EXPECTED_ILLUMINANTS:
    errors.append("selection.illuminants must cover the pinned 3200K and 4800K sources")
  if {code: value.get("stops") for code, value in exposures.items()} != EXPECTED_EXPOSURES:
    errors.append("selection.exposures must cover -2, -1, 0, and +1 stops")
  if {suffix: value.get("role") for suffix, value in artifacts.items()} != EXPECTED_ARTIFACTS:
    errors.append("selection.artifacts must include both independent render paths and noise estimates")

  expected_count = len(cameras) * len(illuminants) * len(exposures) * len(artifacts)
  if selection.get("selected_member_count") != expected_count or expected_count != 192:
    errors.append("selection.selected_member_count does not match the required matrix")
  if not isinstance(selection.get("selected_byte_size"), int) or isinstance(
      selection.get("selected_byte_size"), bool
  ) or selection.get("selected_byte_size", 0) <= 0:
    errors.append("selection.selected_byte_size must be a positive integer")
  if not isinstance(selection.get("selected_member_set_sha256"), str) or SHA256_PATTERN.fullmatch(
      selection.get("selected_member_set_sha256", "")
  ) is None:
    errors.append("selection.selected_member_set_sha256 must be a lowercase SHA-256 digest")
  if selection.get("member_set_hash_algorithm") != (
      "sha256(sorted(member_path + NUL + byte_size + NUL + member_sha256 + LF))"
  ):
    errors.append("selection.member_set_hash_algorithm is unsupported")

  if not errors and archive_path is not None:
    validate_archive(archive_path, manifest, errors)
  if not errors and materialized_root is not None:
    validate_materialized(materialized_root, manifest, errors)
  return errors


def main() -> int:
  args = parse_args()
  errors = validate_manifest(
      args.manifest.resolve(),
      args.archive.resolve() if args.archive is not None else None,
      args.materialized_root.resolve() if args.materialized_root is not None else None,
  )
  if errors:
    for error in errors:
      print(error, file=sys.stderr)
    return 1
  print("independent_chart_source_valid=true")
  if args.archive is not None:
    print(f"archive={args.archive.resolve()}")
  if args.materialized_root is not None:
    print(f"materialized_root={args.materialized_root.resolve()}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
