#!/usr/bin/env python3
"""Adversarial self-test for the tagged chart-reference set validator."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import tempfile
from pathlib import Path
from typing import Any


VALIDATOR_PATH = Path(__file__).with_name(
    "validate-independent-chart-tagged-set.py"
)
CAMERAS = [
    {"directory": "CanonEOS5D", "manufacturer": "Canon", "model": "EOS 5D"},
    {
        "directory": "FujifilmFinePixS5Pro", "manufacturer": "Fujifilm",
        "model": "FinePix S5 Pro",
    },
    {"directory": "LeicaM8", "manufacturer": "Leica", "model": "M8"},
    {"directory": "NikonD3", "manufacturer": "Nikon", "model": "D3"},
    {
        "directory": "PanasonicDMCLX3", "manufacturer": "Panasonic",
        "model": "DMC-LX3",
    },
    {
        "directory": "SonyDSLR-A300", "manufacturer": "Sony",
        "model": "DSLR-A300",
    },
]
ILLUMINANTS = [
    {"code": "i1", "kelvin": 3200, "description": "tungsten photo flood"},
    {"code": "i2", "kelvin": 4800, "description": "daylight photo flood"},
]
EXPOSURES = [
    {"code": "e1", "stops": -2}, {"code": "e2", "stops": -1},
    {"code": "e3", "stops": 0}, {"code": "e4", "stops": 1},
]
BLOCKERS = [
    "no-sensor-raw-files", "eight-bit-registered-chart-crops",
    "no-measured-spectral-patch-values", "project-license-review-pending",
    "colorchecker-target-rights-review-pending", "no-real-dcp-profile",
    "no-rights-cleared-skin-capture",
    "tagged-reference-set-not-yet-corpus-bound",
]


def load_validator() -> Any:
  spec = importlib.util.spec_from_file_location("tagged_set_validator", VALIDATOR_PATH)
  if spec is None or spec.loader is None:
    raise RuntimeError("unable to load tagged-set validator")
  module = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(module)
  return module


def sha256_bytes(payload: bytes) -> str:
  return hashlib.sha256(payload).hexdigest()


def write_json(path: Path, value: dict[str, Any]) -> None:
  path.parent.mkdir(parents=True, exist_ok=True)
  path.write_text(
      json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
  )


def create_set(root: Path, validator: Any) -> tuple[Path, dict[str, Any]]:
  tagger = {
      "version": "photo-editor-independent-chart-profile-tag:v1",
      "binary_sha256": sha256_bytes(b"test tagger"),
      "qt_version": "test-qt",
      "icc_profile_sha256": sha256_bytes(b"linear srgb profile"),
  }
  references: list[dict[str, Any]] = []
  artifact_paths: list[str] = []
  for camera in CAMERAS:
    for illuminant in ILLUMINANTS:
      for exposure in EXPOSURES:
        stem = f"wb1{illuminant['code']}{exposure['code']}"
        source_id = f"members/checker24/{camera['directory']}/{stem}-raw.png"
        output_id = (
            f"references/checker24/{camera['directory']}/{stem}-linear-srgb.png"
        )
        report_id = (
            f"reports/checker24/{camera['directory']}/{stem}-linear-srgb.json"
        )
        source_hash = sha256_bytes(source_id.encode("utf-8"))
        pixel_hash = sha256_bytes(("pixels:" + source_id).encode("utf-8"))
        output_payload = ("tagged-image:" + output_id).encode("utf-8")
        output_hash = sha256_bytes(output_payload)
        output_path = root / output_id
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_bytes(output_payload)
        report = {
            "schema": "photo-editor-independent-chart-tag-report:v1",
            "tagger_version": tagger["version"], "qt_version": tagger["qt_version"],
            "source_id": source_id, "output_id": output_id,
            "source_file_sha256": source_hash, "output_file_sha256": output_hash,
            "source_pixel_sha256": pixel_hash, "output_pixel_sha256": pixel_hash,
            "icc_profile_sha256": tagger["icc_profile_sha256"],
            "declared_color_space": "Linear sRGB", "profile_primaries": "sRGB",
            "profile_transfer": "linear", "width": 606, "height": 404,
            "pixel_samples_unchanged": True, "tag_only": True,
        }
        report_path = root / report_id
        write_json(report_path, report)
        references.append({
            "camera_directory": camera["directory"],
            "manufacturer": camera["manufacturer"], "model": camera["model"],
            "illuminant_code": illuminant["code"], "kelvin": illuminant["kelvin"],
            "exposure_code": exposure["code"], "stops": exposure["stops"],
            "source_id": source_id, "source_sha256": source_hash,
            "output_id": output_id, "output_sha256": output_hash,
            "report_id": report_id,
            "report_sha256": validator.sha256_file(report_path),
            "source_pixel_sha256": pixel_hash, "output_pixel_sha256": pixel_hash,
        })
        artifact_paths.extend((output_id, report_id))
  total, digest = validator.artifact_set_digest(root, artifact_paths)
  manifest = {
      "schema": validator.SCHEMA,
      "set_id": "middlebury-checker24-linear-srgb-tagged-v1",
      "description": "Synthetic contract-only tagged-set validator fixture.",
      "promotion_eligible": False, "promotion_blockers": BLOCKERS,
      "source": {
          "inventory_id": "middlebury-checker24-independent-reference-v1",
          "manifest_sha256": sha256_bytes(b"source manifest"),
          "archive_sha256": sha256_bytes(b"archive"),
          "selected_member_set_sha256": sha256_bytes(b"selected members"),
      },
      "tagger": tagger,
      "matrix": {
          "cameras": CAMERAS, "illuminants": ILLUMINANTS, "exposures": EXPOSURES,
          "reference_count": 48,
      },
      "artifact_summary": {
          "artifact_count": 96, "byte_size": total,
          "artifact_set_sha256": digest,
          "hash_algorithm": validator.HASH_ALGORITHM,
      },
      "references": references,
  }
  manifest_path = root / "tagged-reference-set.json"
  write_json(manifest_path, manifest)
  return manifest_path, manifest


def require_error(errors: list[str], fragment: str, label: str) -> None:
  if not any(fragment in error for error in errors):
    raise AssertionError(f"{label} did not produce {fragment!r}: {errors}")


def main() -> int:
  validator = load_validator()
  with tempfile.TemporaryDirectory(prefix="photo-editor-tagged-set-test-") as temp:
    root = Path(temp)
    manifest_path, baseline = create_set(root, validator)
    errors = validator.validate_manifest(manifest_path, artifact_root=root)
    if errors:
      raise AssertionError(f"baseline tagged set is invalid: {errors}")

    def validate_mutation(
        label: str, mutated: dict[str, Any], expected_error: str
    ) -> None:
      path = root / f"{label}.json"
      write_json(path, mutated)
      require_error(
          validator.validate_manifest(path, artifact_root=root), expected_error, label
      )

    promoted = copy.deepcopy(baseline)
    promoted["promotion_eligible"] = True
    validate_mutation("false-promotion", promoted, "must not claim promotion")

    escaped = copy.deepcopy(baseline)
    escaped["references"][0]["output_id"] = "references/checker24/../escape.png"
    validate_mutation("path-escape", escaped, "output_id is unsafe")

    duplicate = copy.deepcopy(baseline)
    duplicate["references"][1]["camera_directory"] = duplicate["references"][0][
        "camera_directory"
    ]
    duplicate["references"][1]["illuminant_code"] = duplicate["references"][0][
        "illuminant_code"
    ]
    duplicate["references"][1]["exposure_code"] = duplicate["references"][0][
        "exposure_code"
    ]
    validate_mutation("duplicate-cell", duplicate, "duplicates a camera")

    output_path = root / baseline["references"][0]["output_id"]
    original_output = output_path.read_bytes()
    output_path.write_bytes(original_output + b"tampered")
    require_error(
        validator.validate_manifest(manifest_path, artifact_root=root),
        "output artifact hash mismatch", "tampered-output",
    )
    output_path.write_bytes(original_output)

    semantic = copy.deepcopy(baseline)
    report_path = root / semantic["references"][0]["report_id"]
    original_report = report_path.read_bytes()
    report = json.loads(original_report)
    report["icc_profile_sha256"] = sha256_bytes(b"substituted profile")
    write_json(report_path, report)
    semantic["references"][0]["report_sha256"] = validator.sha256_file(report_path)
    artifact_paths = [
        path
        for reference in semantic["references"]
        for path in (reference["output_id"], reference["report_id"])
    ]
    total, digest = validator.artifact_set_digest(root, artifact_paths)
    semantic["artifact_summary"]["byte_size"] = total
    semantic["artifact_summary"]["artifact_set_sha256"] = digest
    validate_mutation("wrong-profile", semantic, "icc_profile_sha256 does not match")
    report_path.write_bytes(original_report)

  print("independent_chart_tagged_set_self_test=passed")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
