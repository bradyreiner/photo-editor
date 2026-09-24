#!/usr/bin/env python3
"""Render every materialized real-RAW candidate through both shipping paths."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import shutil
import subprocess
from pathlib import Path
from typing import Any


VALIDATOR_PATH = Path(__file__).with_name("validate-real-raw-render-set.py")
SCHEMA = "photo-editor-real-raw-render-set:v1"
SET_ID = "hdrplus-real-raw-aces2-srgb-candidate-set-v1"
RENDER_PATHS = ("portable-libraw", "shipping-default")
HASH_ALGORITHM = (
    "sha256(sorted(logical_path + NUL + byte_size + NUL + file_sha256 + LF))"
)
ADDITIONAL_BLOCKERS = (
    "candidate-output-is-not-an-independent-color-reference",
    "core-image-actual-device-render-proof-pending",
    "no-blinded-editorial-approval",
)


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--source-root", type=Path, required=True)
  parser.add_argument("--candidate-tool", type=Path, required=True)
  parser.add_argument("--output", type=Path, required=True)
  parser.add_argument("--force", action="store_true")
  return parser.parse_args()


def load_json(path: Path) -> dict[str, Any]:
  with path.open("r", encoding="utf-8") as handle:
    value = json.load(handle)
  if not isinstance(value, dict):
    raise ValueError(f"JSON root must be an object: {path}")
  return value


def load_validator() -> Any:
  spec = importlib.util.spec_from_file_location(
      "real_raw_render_set_validator", VALIDATOR_PATH
  )
  if spec is None or spec.loader is None:
    raise RuntimeError(f"unable to load validator: {VALIDATOR_PATH}")
  module = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(module)
  return module


def sha256_file(path: Path) -> str:
  digest = hashlib.sha256()
  with path.open("rb") as handle:
    for block in iter(lambda: handle.read(1024 * 1024), b""):
      digest.update(block)
  return digest.hexdigest()


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


def run_checked(command: list[str], label: str) -> None:
  process = subprocess.run(command, check=False, capture_output=True, text=True)
  if process.returncode != 0:
    detail = process.stderr.strip() or process.stdout.strip()
    raise ValueError(f"{label} failed: {detail}")


def build_set(
    source_root: Path, candidate_tool: Path, output: Path, *, force: bool
) -> dict[str, Any]:
  inventory_path = source_root / "source-inventory.json"
  inventory = load_json(inventory_path)
  if inventory.get("schema") != "photo-editor-color-source-inventory:v1":
    raise ValueError("source inventory schema is unsupported")
  if inventory.get("promotion_eligible") is not False:
    raise ValueError("source inventory must remain non-promotable")
  if not candidate_tool.is_file():
    raise ValueError(f"candidate renderer is missing: {candidate_tool}")
  if output.exists() and not force:
    raise ValueError(f"output already exists: {output}; pass --force to replace it")

  staging = output.with_name(f".{output.name}.partial")
  shutil.rmtree(staging, ignore_errors=True)
  staging.mkdir(parents=True)
  artifact_paths: list[str] = []
  cells: list[dict[str, Any]] = []
  qt_version: str | None = None
  try:
    for candidate in inventory["candidates"]:
      candidate_id = candidate["candidate_id"]
      capture = candidate["capture"]
      camera = candidate["camera"]
      source_path = source_root / candidate_id / capture["file_name"]
      if not source_path.is_file() or sha256_file(source_path) != capture["sha256"]:
        raise ValueError(f"materialized RAW identity mismatch: {candidate_id}")
      renders: list[dict[str, Any]] = []
      reports: dict[str, dict[str, Any]] = {}
      for decode_path in RENDER_PATHS:
        output_id = f"renders/{candidate_id}/{decode_path}/candidate.png"
        report_id = f"reports/{candidate_id}/{decode_path}/candidate.json"
        output_path = staging / output_id
        report_path = staging / report_id
        output_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.parent.mkdir(parents=True, exist_ok=True)
        source_id = f"sources/{candidate_id}/capture"
        run_checked(
            [
                str(candidate_tool), "--input", str(source_path),
                "--output", str(output_path), "--report", str(report_path),
                "--source-id", source_id, "--output-id", output_id,
                "--decode-path", decode_path,
            ],
            f"{decode_path} render for {candidate_id}",
        )
        report = load_json(report_path)
        if report.get("source_file_sha256") != capture["sha256"]:
          raise ValueError(f"source identity mismatch: {candidate_id}/{decode_path}")
        if report.get("output_file_sha256") != sha256_file(output_path):
          raise ValueError(f"output identity mismatch: {candidate_id}/{decode_path}")
        if qt_version is None:
          qt_version = report.get("qt_version")
        elif report.get("qt_version") != qt_version:
          raise ValueError("Qt version changed within the render set")
        reports[decode_path] = report
        renders.append({
            "decode_path": decode_path,
            "output_id": output_id,
            "output_path": output_id,
            "output_sha256": report["output_file_sha256"],
            "report_path": report_id,
            "report_sha256": sha256_file(report_path),
            "decoder_backend": report["decoded_metadata"]["decoder_backend"],
            "output_pixel_sha256": report["output_pixel_sha256"],
        })
        artifact_paths.extend((output_id, report_id))
      same_backend = (
          reports[RENDER_PATHS[0]]["decoded_metadata"]["decoder_backend"] ==
          reports[RENDER_PATHS[1]]["decoded_metadata"]["decoder_backend"]
      )
      pixel_identical = (
          reports[RENDER_PATHS[0]]["output_pixel_sha256"] ==
          reports[RENDER_PATHS[1]]["output_pixel_sha256"]
      )
      if same_backend and not pixel_identical:
        raise ValueError(
            f"same-backend routes diverged for {candidate_id}"
        )
      cells.append({
          "candidate_id": candidate_id,
          "source_id": f"sources/{candidate_id}/capture",
          "source_sha256": capture["sha256"],
          "camera_make": camera["make"],
          "camera_model": camera["model"],
          "illuminant": candidate["illuminant"],
          "scene_tags": candidate["scene_tags"],
          "same_decoder_backend": same_backend,
          "pixel_identical": pixel_identical,
          "renders": renders,
      })

    byte_size, artifact_digest = artifact_set_digest(staging, artifact_paths)
    cameras = {
        (candidate["camera"]["make"], candidate["camera"]["model"])
        for candidate in inventory["candidates"]
    }
    blockers = list(dict.fromkeys(
        list(inventory["promotion_blockers"]) + list(ADDITIONAL_BLOCKERS)
    ))
    manifest = {
        "schema": SCHEMA,
        "set_id": SET_ID,
        "description": (
            "Non-promotable real-DNG execution evidence for the shipping "
            "and portable ACES 2 sRGB paths."
        ),
        "promotion_eligible": False,
        "promotion_blockers": blockers,
        "source_inventory": {
            "inventory_id": inventory["inventory_id"],
            "manifest_sha256": sha256_file(inventory_path),
        },
        "tools": {
            "candidate_renderer_sha256": sha256_file(candidate_tool),
            "candidate_tool_version": (
                "photo-editor-real-raw-candidate-render:v3"
            ),
            "qt_version": qt_version,
        },
        "render_paths": list(RENDER_PATHS),
        "matrix": {
            "candidate_count": len(inventory["candidates"]),
            "camera_count": len(cameras),
            "illuminants": sorted({
                candidate["illuminant"] for candidate in inventory["candidates"]
            }),
            "render_count": len(inventory["candidates"]) * len(RENDER_PATHS),
        },
        "artifact_summary": {
            "artifact_count": len(artifact_paths),
            "byte_size": byte_size,
            "artifact_set_sha256": artifact_digest,
            "hash_algorithm": HASH_ALGORITHM,
        },
        "cells": cells,
    }
    manifest_path = staging / "real-raw-render-set.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    validator = load_validator()
    errors = validator.validate_manifest(
        manifest_path, source_inventory_path=inventory_path,
        artifact_root=staging,
    )
    if errors:
      raise ValueError("generated render set is invalid: " + "; ".join(errors))
    if output.exists():
      shutil.rmtree(output)
    staging.rename(output)
    return manifest
  except Exception:
    shutil.rmtree(staging, ignore_errors=True)
    raise


def main() -> int:
  args = parse_args()
  try:
    manifest = build_set(
        args.source_root.resolve(), args.candidate_tool.resolve(),
        args.output.resolve(), force=args.force,
    )
  except (OSError, ValueError, KeyError, TypeError) as exc:
    print(f"error={exc}")
    return 1
  print(f"status=created-real-raw-render-set:{args.output}")
  print(f"render_count={manifest['matrix']['render_count']}")
  print(f"artifact_set_sha256={manifest['artifact_summary']['artifact_set_sha256']}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
