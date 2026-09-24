#!/usr/bin/env python3
"""Publish and validate per-camera RAW decode support and fallback evidence."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import sys
from pathlib import Path
from typing import Any


VALIDATOR_PATH = Path(__file__).with_name("validate-real-raw-render-set.py")
SCHEMA = "photo-editor-camera-support-report:v1"
ROUTES = ("portable-libraw", "shipping-default")
ROOT_KEYS = {
    "schema", "report_id", "source_render_set", "promotion_eligible",
    "promotion_blockers", "summary", "cameras",
}
SOURCE_KEYS = {"set_id", "manifest_sha256", "inventory_id", "inventory_sha256"}
SUMMARY_KEYS = {
    "camera_count", "capture_count", "supported_camera_count",
    "fallback_camera_count", "unsupported_camera_count",
}
CAMERA_KEYS = {
    "camera_id", "make", "model", "capture_ids", "capture_count",
    "routes", "support_status", "fallback_status", "fallback_reason",
    "decoder_backends", "camera_profile_identities",
}
ROUTE_KEYS = {
    "route", "supported", "decoder_backends", "camera_profile_identities",
}


def load_validator() -> Any:
  spec = importlib.util.spec_from_file_location("real_raw_validator", VALIDATOR_PATH)
  if spec is None or spec.loader is None:
    raise RuntimeError(f"unable to load validator: {VALIDATOR_PATH}")
  module = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(module)
  return module


def load_json(path: Path) -> dict[str, Any]:
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


def exact_keys(value: Any, keys: set[str], label: str, errors: list[str]) -> dict[str, Any] | None:
  if not isinstance(value, dict):
    errors.append(f"{label} must be an object")
    return None
  for key in sorted(set(value) - keys):
    errors.append(f"{label}: unknown field {key!r}")
  for key in sorted(keys - set(value)):
    errors.append(f"{label}: missing required field {key!r}")
  return value


def camera_id(make: str, model: str) -> str:
  return f"{make.strip()}::{model.strip()}"


def build_report(
    render_manifest_path: Path, inventory_path: Path, artifact_root: Path,
) -> dict[str, Any]:
  validator = load_validator()
  errors = validator.validate_manifest(
      render_manifest_path,
      source_inventory_path=inventory_path,
      artifact_root=artifact_root,
  )
  if errors:
    raise ValueError("real-RAW render set is invalid: " + "; ".join(errors))
  render_set = load_json(render_manifest_path)
  inventory = load_json(inventory_path)
  grouped: dict[str, dict[str, Any]] = {}
  for cell in render_set["cells"]:
    identifier = camera_id(cell["camera_make"], cell["camera_model"])
    camera = grouped.setdefault(identifier, {
        "camera_id": identifier,
        "make": cell["camera_make"],
        "model": cell["camera_model"],
        "capture_ids": [],
        "route_backends": {route: set() for route in ROUTES},
        "route_profiles": {route: set() for route in ROUTES},
    })
    camera["capture_ids"].append(cell["candidate_id"])
    for render in cell["renders"]:
      report = load_json(artifact_root / render["report_path"])
      route = render["decode_path"]
      metadata = report["decoded_metadata"]
      camera["route_backends"][route].add(metadata["decoder_backend"])
      camera["route_profiles"][route].add(metadata["camera_profile_identity"])

  cameras: list[dict[str, Any]] = []
  for identifier in sorted(grouped):
    grouped_camera = grouped[identifier]
    routes = []
    for route in ROUTES:
      routes.append({
          "route": route,
          "supported": bool(grouped_camera["route_backends"][route]),
          "decoder_backends": sorted(grouped_camera["route_backends"][route]),
          "camera_profile_identities": sorted(grouped_camera["route_profiles"][route]),
      })
    portable_backends = grouped_camera["route_backends"]["portable-libraw"]
    shipping_backends = grouped_camera["route_backends"]["shipping-default"]
    supported = all(route["supported"] for route in routes)
    fallback = supported and shipping_backends == portable_backends
    fallback_reason = (
        "shipping-default-resolved-to-portable-libraw"
        if fallback else "none" if supported else "required-route-unsupported"
    )
    cameras.append({
        "camera_id": identifier,
        "make": grouped_camera["make"],
        "model": grouped_camera["model"],
        "capture_ids": sorted(grouped_camera["capture_ids"]),
        "capture_count": len(grouped_camera["capture_ids"]),
        "routes": routes,
        "support_status": "supported" if supported else "unsupported",
        "fallback_status": "fallback" if fallback else "native" if supported else "unavailable",
        "fallback_reason": fallback_reason,
        "decoder_backends": sorted(portable_backends | shipping_backends),
        "camera_profile_identities": sorted(
            grouped_camera["route_profiles"]["portable-libraw"] |
            grouped_camera["route_profiles"]["shipping-default"]
        ),
    })

  report = {
      "schema": SCHEMA,
      "report_id": f"{render_set['set_id']}-camera-support-v1",
      "source_render_set": {
          "set_id": render_set["set_id"],
          "manifest_sha256": sha256_file(render_manifest_path),
          "inventory_id": inventory["inventory_id"],
          "inventory_sha256": sha256_file(inventory_path),
      },
      "promotion_eligible": False,
      "promotion_blockers": render_set["promotion_blockers"],
      "summary": {
          "camera_count": len(cameras),
          "capture_count": sum(camera["capture_count"] for camera in cameras),
          "supported_camera_count": sum(camera["support_status"] == "supported" for camera in cameras),
          "fallback_camera_count": sum(camera["fallback_status"] == "fallback" for camera in cameras),
          "unsupported_camera_count": sum(camera["support_status"] == "unsupported" for camera in cameras),
      },
      "cameras": cameras,
  }
  report_errors = validate_report(report, render_set, inventory)
  if report_errors:
    raise ValueError("generated camera report is invalid: " + "; ".join(report_errors))
  return report


def validate_report(
    report: dict[str, Any], render_set: dict[str, Any], inventory: dict[str, Any],
) -> list[str]:
  errors: list[str] = []
  exact_keys(report, ROOT_KEYS, "report", errors)
  source = exact_keys(report.get("source_render_set"), SOURCE_KEYS, "report.source_render_set", errors) or {}
  summary = exact_keys(report.get("summary"), SUMMARY_KEYS, "report.summary", errors) or {}
  if report.get("schema") != SCHEMA:
    errors.append("report schema is unsupported")
  if report.get("promotion_eligible") is not False:
    errors.append("camera support report must remain non-promotable")
  blockers = report.get("promotion_blockers")
  if not isinstance(blockers, list) or not set(render_set.get("promotion_blockers", [])).issubset(set(blockers)):
    errors.append("report must retain every render-set promotion blocker")
  if source.get("set_id") != render_set.get("set_id") or source.get("inventory_id") != inventory.get("inventory_id"):
    errors.append("source render-set or inventory identity mismatch")
  cameras = report.get("cameras")
  if not isinstance(cameras, list) or not cameras:
    errors.append("report.cameras must be a non-empty array")
    return errors
  seen: set[str] = set()
  capture_ids: set[str] = set()
  supported_count = fallback_count = unsupported_count = 0
  for index, value in enumerate(cameras):
    label = f"report.cameras[{index}]"
    camera = exact_keys(value, CAMERA_KEYS, label, errors)
    if camera is None:
      continue
    identifier = camera.get("camera_id")
    if identifier != camera_id(str(camera.get("make", "")), str(camera.get("model", ""))) or identifier in seen:
      errors.append(f"{label}.camera_id is invalid or duplicated")
    seen.add(identifier)
    captures = camera.get("capture_ids")
    if not isinstance(captures, list) or not captures or len(captures) != len(set(captures)):
      errors.append(f"{label}.capture_ids must be unique and non-empty")
      captures = []
    if camera.get("capture_count") != len(captures):
      errors.append(f"{label}.capture_count is inaccurate")
    capture_ids.update(captures)
    routes = camera.get("routes")
    if not isinstance(routes, list) or [route.get("route") for route in routes if isinstance(route, dict)] != list(ROUTES):
      errors.append(f"{label}.routes must cover both routes in canonical order")
      routes = []
    for route_index, route in enumerate(routes):
      route_value = exact_keys(route, ROUTE_KEYS, f"{label}.routes[{route_index}]", errors)
      if route_value is not None and route_value.get("supported") is not bool(route_value.get("decoder_backends")):
        errors.append(f"{label}.routes[{route_index}].supported is inaccurate")
    status = camera.get("support_status")
    fallback_status = camera.get("fallback_status")
    if status == "supported":
      supported_count += 1
    elif status == "unsupported":
      unsupported_count += 1
    else:
      errors.append(f"{label}.support_status is invalid")
    if fallback_status == "fallback":
      fallback_count += 1
      if camera.get("fallback_reason") != "shipping-default-resolved-to-portable-libraw":
        errors.append(f"{label}.fallback_reason is inaccurate")
    elif fallback_status not in ("native", "unavailable"):
      errors.append(f"{label}.fallback_status is invalid")
  expected_captures = {cell["candidate_id"] for cell in render_set.get("cells", [])}
  if capture_ids != expected_captures:
    errors.append("camera report does not cover every render-set capture")
  expected_summary = {
      "camera_count": len(cameras),
      "capture_count": len(expected_captures),
      "supported_camera_count": supported_count,
      "fallback_camera_count": fallback_count,
      "unsupported_camera_count": unsupported_count,
  }
  if summary != expected_summary:
    errors.append("report.summary is inaccurate")
  return errors


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("render_manifest", type=Path)
  parser.add_argument("--source-inventory", type=Path, required=True)
  parser.add_argument("--artifact-root", type=Path)
  action = parser.add_mutually_exclusive_group(required=True)
  action.add_argument("--output", type=Path)
  action.add_argument("--validate-report", type=Path)
  return parser.parse_args()


def main() -> int:
  args = parse_args()
  root = args.artifact_root or args.render_manifest.parent
  try:
    report = build_report(
        args.render_manifest.resolve(), args.source_inventory.resolve(), root.resolve()
    )
    if args.validate_report is not None:
      published = load_json(args.validate_report.resolve())
      if published != report:
        raise ValueError("published camera support report does not match validated render evidence")
      print("status=valid-camera-support-report")
      return 0
    if args.output.exists():
      raise ValueError(f"output already exists: {args.output}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
  except (OSError, ValueError, KeyError, TypeError, RuntimeError) as exc:
    print(f"error={exc}", file=sys.stderr)
    return 1
  print(f"status=published-camera-support-report:{args.output}")
  print(f"camera_count={report['summary']['camera_count']}")
  print(f"fallback_camera_count={report['summary']['fallback_camera_count']}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
