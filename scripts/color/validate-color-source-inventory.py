#!/usr/bin/env python3
"""Validate checksum-pinned, non-shipping color-corpus source candidates."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any
from urllib.parse import urlparse


SCHEMA = "photo-editor-color-source-inventory:v1"
ID_PATTERN = re.compile(r"^[a-z0-9][a-z0-9._-]*$")
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
REQUIRED_ILLUMINANTS = {"daylight", "tungsten", "fluorescent-led", "mixed"}
SOURCE_QUALIFIED_STATUS = "source-qualified"
DEPICTED_PERSON_REVIEW_STATUS = (
    "source-qualified-depicted-person-review-pending"
)
ROOT_KEYS = {
    "schema",
    "inventory_id",
    "description",
    "candidate_count",
    "promotion_eligible",
    "promotion_blockers",
    "datasets",
    "candidates",
}
DATASET_KEYS = {
    "dataset_id",
    "name",
    "source_page",
    "download_prefix",
    "license_name",
    "license_url",
    "license_evidence",
    "attribution",
    "distribution",
    "license_review",
}
CANDIDATE_KEYS = {
    "candidate_id",
    "dataset_id",
    "technical_status",
    "promotion_eligible",
    "promotion_blockers",
    "capture",
    "reference",
    "sidecars",
    "camera",
    "iso",
    "illuminant",
    "scene_tags",
    "visual_review",
}
FILE_KEYS = {"file_name", "url", "sha256", "byte_size"}
REFERENCE_KEYS = FILE_KEYS | {"producer", "pipeline_version"}
SIDECAR_KEYS = FILE_KEYS | {"kind"}


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(
      description="Validate the immutable public-source inventory for the color corpus."
  )
  parser.add_argument("manifest", type=Path)
  parser.add_argument(
      "--materialized-root",
      type=Path,
      help="Also verify candidate files at <root>/<candidate_id>/<file_name>.",
  )
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


def require_id(value: Any, label: str, errors: list[str]) -> str | None:
  text = require_string(value, label, errors)
  if text is not None and ID_PATTERN.fullmatch(text) is None:
    errors.append(f"{label} must match {ID_PATTERN.pattern}")
    return None
  return text


def require_string_array(value: Any, label: str, errors: list[str]) -> list[str]:
  if not isinstance(value, list) or not value or not all(
      isinstance(item, str) and bool(item.strip()) for item in value
  ):
    errors.append(f"{label} must be a non-empty array of strings")
    return []
  if len(value) != len(set(value)):
    errors.append(f"{label} must not contain duplicates")
  return value


def validate_date(value: Any, label: str, errors: list[str]) -> None:
  text = require_string(value, label, errors)
  if text is None:
    return
  try:
    dt.date.fromisoformat(text)
  except ValueError:
    errors.append(f"{label} must use YYYY-MM-DD")


def validate_https_url(value: Any, label: str, errors: list[str]) -> str | None:
  text = require_string(value, label, errors)
  if text is None:
    return None
  parsed = urlparse(text)
  if parsed.scheme != "https" or not parsed.netloc or parsed.username or parsed.password:
    errors.append(f"{label} must be an absolute credential-free HTTPS URL")
    return None
  if parsed.fragment:
    errors.append(f"{label} must not contain a fragment")
    return None
  return text


def validate_file_contract(
    value: Any,
    label: str,
    allowed_keys: set[str],
    download_prefix: str | None,
    materialized_path: Path | None,
    errors: list[str],
) -> str | None:
  if not isinstance(value, dict):
    errors.append(f"{label} must be an object")
    return None
  check_keys(value, allowed_keys, allowed_keys, label, errors)
  file_name = require_string(value.get("file_name"), f"{label}.file_name", errors)
  if file_name is not None and Path(file_name).name != file_name:
    errors.append(f"{label}.file_name must be a plain file name")
  url = validate_https_url(value.get("url"), f"{label}.url", errors)
  if url is not None and download_prefix is not None and not url.startswith(download_prefix):
    errors.append(f"{label}.url must remain under its dataset download_prefix")
  digest = value.get("sha256")
  if not isinstance(digest, str) or SHA256_PATTERN.fullmatch(digest) is None:
    errors.append(f"{label}.sha256 must be a lowercase 64-character digest")
  size = value.get("byte_size")
  if not isinstance(size, int) or isinstance(size, bool) or size <= 0:
    errors.append(f"{label}.byte_size must be a positive integer")
  if materialized_path is not None and file_name is not None:
    path = materialized_path / file_name
    if not path.is_file():
      errors.append(f"{label}: materialized file is missing: {path}")
    else:
      if isinstance(size, int) and path.stat().st_size != size:
        errors.append(f"{label}: materialized byte_size mismatch")
      if isinstance(digest, str) and SHA256_PATTERN.fullmatch(digest) is not None:
        if sha256_file(path) != digest:
          errors.append(f"{label}: materialized sha256 mismatch")
  return file_name


def validate_inventory(
    manifest_path: Path, materialized_root: Path | None = None
) -> list[str]:
  errors: list[str] = []
  try:
    with manifest_path.open("r", encoding="utf-8") as handle:
      manifest = json.load(handle)
  except (OSError, json.JSONDecodeError) as exc:
    return [f"unable to read inventory: {exc}"]
  if not isinstance(manifest, dict):
    return ["inventory root must be an object"]

  check_keys(manifest, ROOT_KEYS, ROOT_KEYS, "inventory", errors)
  if manifest.get("schema") != SCHEMA:
    errors.append(f"inventory.schema must be {SCHEMA!r}")
  require_id(manifest.get("inventory_id"), "inventory.inventory_id", errors)
  require_string(manifest.get("description"), "inventory.description", errors)
  blockers = require_string_array(
      manifest.get("promotion_blockers"), "inventory.promotion_blockers", errors
  )
  if manifest.get("promotion_eligible") is not False:
    errors.append("source inventory must not claim promotion eligibility")
  if not blockers:
    errors.append("source inventory must record its remaining promotion blockers")

  datasets_value = manifest.get("datasets")
  if not isinstance(datasets_value, list) or not datasets_value:
    errors.append("inventory.datasets must be a non-empty array")
    datasets_value = []
  datasets: dict[str, str] = {}
  for index, dataset in enumerate(datasets_value):
    label = f"datasets[{index}]"
    if not isinstance(dataset, dict):
      errors.append(f"{label} must be an object")
      continue
    check_keys(dataset, DATASET_KEYS, DATASET_KEYS, label, errors)
    dataset_id = require_id(dataset.get("dataset_id"), f"{label}.dataset_id", errors)
    for key in (
        "name", "license_name", "license_evidence", "attribution"
    ):
      require_string(dataset.get(key), f"{label}.{key}", errors)
    validate_https_url(dataset.get("source_page"), f"{label}.source_page", errors)
    prefix = validate_https_url(
        dataset.get("download_prefix"), f"{label}.download_prefix", errors
    )
    validate_https_url(dataset.get("license_url"), f"{label}.license_url", errors)
    if prefix is not None and not prefix.endswith("/"):
      errors.append(f"{label}.download_prefix must end with '/'")
    if dataset.get("distribution") != "private-ci-only":
      errors.append(f"{label}.distribution must be 'private-ci-only'")
    review = dataset.get("license_review")
    if not isinstance(review, dict):
      errors.append(f"{label}.license_review must be an object")
    else:
      review_keys = {"status", "reviewed_on", "reviewer_id"}
      check_keys(review, review_keys, review_keys, f"{label}.license_review", errors)
      if review.get("status") not in {"approved", "pending-project-review", "rejected"}:
        errors.append(f"{label}.license_review.status is invalid")
      validate_date(
          review.get("reviewed_on"), f"{label}.license_review.reviewed_on", errors
      )
      require_id(
          review.get("reviewer_id"), f"{label}.license_review.reviewer_id", errors
      )
    if dataset_id is not None:
      if dataset_id in datasets:
        errors.append(f"duplicate dataset_id: {dataset_id}")
      elif prefix is not None:
        datasets[dataset_id] = prefix

  candidates_value = manifest.get("candidates")
  if not isinstance(candidates_value, list) or not candidates_value:
    errors.append("inventory.candidates must be a non-empty array")
    candidates_value = []
  count = manifest.get("candidate_count")
  if not isinstance(count, int) or isinstance(count, bool) or count <= 0:
    errors.append("inventory.candidate_count must be a positive integer")
  elif count != len(candidates_value):
    errors.append(
        f"inventory.candidate_count mismatch: manifest={count}, actual={len(candidates_value)}"
    )

  candidate_ids: set[str] = set()
  file_names: set[str] = set()
  cameras: set[tuple[str, str]] = set()
  illuminants: set[str] = set()
  qualified_count = 0
  for index, candidate in enumerate(candidates_value):
    label = f"candidates[{index}]"
    if not isinstance(candidate, dict):
      errors.append(f"{label} must be an object")
      continue
    check_keys(candidate, CANDIDATE_KEYS, CANDIDATE_KEYS, label, errors)
    candidate_id = require_id(
        candidate.get("candidate_id"), f"{label}.candidate_id", errors
    )
    if candidate_id is not None:
      if candidate_id in candidate_ids:
        errors.append(f"duplicate candidate_id: {candidate_id}")
      candidate_ids.add(candidate_id)
    dataset_id = require_id(candidate.get("dataset_id"), f"{label}.dataset_id", errors)
    prefix = datasets.get(dataset_id or "")
    if dataset_id is not None and dataset_id not in datasets:
      errors.append(f"{label}.dataset_id does not reference a dataset")
    technical_status = candidate.get("technical_status")
    if technical_status not in {
        SOURCE_QUALIFIED_STATUS, DEPICTED_PERSON_REVIEW_STATUS
    }:
      errors.append(
          f"{label}.technical_status must be {SOURCE_QUALIFIED_STATUS!r} or "
          f"{DEPICTED_PERSON_REVIEW_STATUS!r}"
      )
    else:
      qualified_count += 1
    candidate_blockers = require_string_array(
        candidate.get("promotion_blockers"), f"{label}.promotion_blockers", errors
    )
    if candidate.get("promotion_eligible") is not False:
      errors.append(f"{label} must not claim promotion eligibility")
    if not candidate_blockers:
      errors.append(f"{label} must record promotion blockers")
    iso = candidate.get("iso")
    if iso is None:
      if "missing-iso-metadata" not in candidate_blockers:
        errors.append(f"{label}: null iso requires missing-iso-metadata blocker")
    elif not isinstance(iso, int) or isinstance(iso, bool) or iso <= 0:
      errors.append(f"{label}.iso must be null or a positive integer")

    materialized_path = None
    if materialized_root is not None and candidate_id is not None:
      materialized_path = materialized_root.resolve() / candidate_id
    names = [
        validate_file_contract(
            candidate.get("capture"), f"{label}.capture", FILE_KEYS,
            prefix, materialized_path, errors
        ),
        validate_file_contract(
            candidate.get("reference"), f"{label}.reference", REFERENCE_KEYS,
            prefix, materialized_path, errors
        ),
    ]
    reference = candidate.get("reference")
    if isinstance(reference, dict):
      producer = require_string(
          reference.get("producer"), f"{label}.reference.producer", errors
      )
      require_string(
          reference.get("pipeline_version"),
          f"{label}.reference.pipeline_version",
          errors,
      )
      if producer is not None and "photo-editor" in producer.lower():
        errors.append(f"{label}.reference.producer must be independent")

    sidecars = candidate.get("sidecars")
    sidecar_kinds: set[str] = set()
    if not isinstance(sidecars, list) or not sidecars:
      errors.append(f"{label}.sidecars must be a non-empty array")
      sidecars = []
    for sidecar_index, sidecar in enumerate(sidecars):
      sidecar_label = f"{label}.sidecars[{sidecar_index}]"
      names.append(
          validate_file_contract(
              sidecar, sidecar_label, SIDECAR_KEYS, prefix, materialized_path, errors
          )
      )
      if isinstance(sidecar, dict):
        kind = require_id(sidecar.get("kind"), f"{sidecar_label}.kind", errors)
        if kind is not None:
          if kind in sidecar_kinds:
            errors.append(f"{label}.sidecars contains duplicate kind {kind!r}")
          sidecar_kinds.add(kind)
    required_sidecars = {"sensor-to-linear-srgb-matrix", "lens-shading-map"}
    if sidecar_kinds != required_sidecars:
      errors.append(f"{label}.sidecars must contain exactly {sorted(required_sidecars)}")
    for name in names:
      if name is None:
        continue
      if name in file_names:
        errors.append(f"duplicate output file_name: {name}")
      file_names.add(name)

    camera = candidate.get("camera")
    if not isinstance(camera, dict):
      errors.append(f"{label}.camera must be an object")
    else:
      camera_keys = {"make", "model"}
      check_keys(camera, camera_keys, camera_keys, f"{label}.camera", errors)
      make = require_string(camera.get("make"), f"{label}.camera.make", errors)
      model = require_string(camera.get("model"), f"{label}.camera.model", errors)
      if make is not None and model is not None:
        cameras.add((make, model))
    illuminant = candidate.get("illuminant")
    if illuminant not in REQUIRED_ILLUMINANTS:
      errors.append(f"{label}.illuminant is not a required lighting class")
    else:
      illuminants.add(illuminant)
    require_string_array(candidate.get("scene_tags"), f"{label}.scene_tags", errors)

    review = candidate.get("visual_review")
    if not isinstance(review, dict):
      errors.append(f"{label}.visual_review must be an object")
    else:
      review_keys = {"reviewed_on", "reviewer_id", "people_present", "notes"}
      check_keys(review, review_keys, review_keys, f"{label}.visual_review", errors)
      validate_date(review.get("reviewed_on"), f"{label}.visual_review.reviewed_on", errors)
      require_id(review.get("reviewer_id"), f"{label}.visual_review.reviewer_id", errors)
      people_present = review.get("people_present")
      if not isinstance(people_present, bool):
        errors.append(f"{label}.visual_review.people_present must be a boolean")
      elif technical_status == SOURCE_QUALIFIED_STATUS and people_present:
        errors.append(
            f"{label}: source-qualified candidates must contain no recognizable people"
        )
      elif technical_status == DEPICTED_PERSON_REVIEW_STATUS:
        if not people_present:
          errors.append(
              f"{label}: depicted-person review status requires people_present=true"
          )
        if "depicted-person-review-required" not in candidate_blockers:
          errors.append(
              f"{label}: depicted-person review status requires the "
              "depicted-person-review-required blocker"
          )
      require_string(review.get("notes"), f"{label}.visual_review.notes", errors)

  if qualified_count < 4:
    errors.append("inventory must contain at least four source-qualified captures")
  if len(cameras) < 3:
    errors.append("inventory must span at least three camera make/model pairs")
  for illuminant in sorted(REQUIRED_ILLUMINANTS - illuminants):
    errors.append(f"missing source-candidate illuminant coverage: {illuminant}")
  return errors


def main() -> int:
  args = parse_args()
  errors = validate_inventory(args.manifest.resolve(), args.materialized_root)
  if errors:
    for error in errors:
      print(f"error: {error}", file=sys.stderr)
    return 1
  print(f"color_source_inventory={args.manifest.resolve()}")
  print("status=valid-non-promotion-source-inventory")
  if args.materialized_root is not None:
    print(f"materialized_root={args.materialized_root.resolve()}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
