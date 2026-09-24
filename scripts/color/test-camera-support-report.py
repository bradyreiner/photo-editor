#!/usr/bin/env python3
"""Adversarial tests for the per-camera support/fallback report."""

from __future__ import annotations

import copy
import importlib.util
import json
import tempfile
from pathlib import Path
from typing import Any


SCRIPT_DIR = Path(__file__).parent


def load_module(name: str, path: Path) -> Any:
  spec = importlib.util.spec_from_file_location(name, path)
  if spec is None or spec.loader is None:
    raise RuntimeError(f"unable to load {path}")
  module = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(module)
  return module


def write_json(path: Path, value: dict[str, Any]) -> None:
  path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> int:
  render_test = load_module("render_set_test", SCRIPT_DIR / "test-real-raw-render-set.py")
  render_validator = load_module("render_validator", SCRIPT_DIR / "validate-real-raw-render-set.py")
  publisher = load_module("camera_report", SCRIPT_DIR / "publish-camera-support-report.py")
  with tempfile.TemporaryDirectory(prefix="camera-support-report-test-") as temporary:
    root = Path(temporary)
    manifest_path, inventory_path, _, _ = render_test.make_fixture(root, render_validator)
    report = publisher.build_report(manifest_path, inventory_path, root)
    if report["summary"] != {
        "camera_count": 1,
        "capture_count": 1,
        "supported_camera_count": 1,
        "fallback_camera_count": 1,
        "unsupported_camera_count": 0,
    }:
      raise AssertionError(f"unexpected camera report summary: {report['summary']}")
    camera = report["cameras"][0]
    if camera["fallback_status"] != "fallback" or camera["support_status"] != "supported":
      raise AssertionError(f"fallback support was not classified: {camera}")
    render_set = render_test.load_json(manifest_path)
    inventory = render_test.load_json(inventory_path)
    cases = [
        ("promotion", lambda value: value.__setitem__("promotion_eligible", True), "non-promotable"),
        ("missing-capture", lambda value: value["cameras"][0].__setitem__("capture_ids", []), "non-empty"),
        ("false-summary", lambda value: value["summary"].__setitem__("fallback_camera_count", 0), "summary"),
        ("false-fallback", lambda value: value["cameras"][0].__setitem__("fallback_reason", "none"), "fallback_reason"),
        ("unknown-field", lambda value: value.__setitem__("surprise", True), "unknown field"),
    ]
    for name, mutate, expected in cases:
      value = copy.deepcopy(report)
      mutate(value)
      errors = publisher.validate_report(value, render_set, inventory)
      if not any(expected in error for error in errors):
        raise AssertionError(f"{name} mutation was accepted: {errors}")
    report_path = root / "camera-support-report.json"
    write_json(report_path, report)
    if publisher.load_json(report_path) != report:
      raise AssertionError("published report did not round-trip")
  print("camera support report tests passed")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
