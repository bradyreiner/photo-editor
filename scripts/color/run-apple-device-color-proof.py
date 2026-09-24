#!/usr/bin/env python3
"""Create a pass-only physical Apple Core Image plus Metal color proof."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import platform
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


PREPARE_REAL_RAW_PATH = Path(__file__).with_name(
    "prepare-real-raw-render-set.py"
)
VALIDATOR_PATH = Path(__file__).with_name(
    "validate-apple-device-color-proof.py"
)
SCHEMA = "photo-editor-apple-device-color-proof:v1"
PROOF_ID = "apple-core-image-metal-color-proof-v1"
HASH_ALGORITHM = (
    "sha256(sorted(logical_path + NUL + byte_size + NUL + file_sha256 + LF))"
)
ADDITIONAL_BLOCKERS = (
    "license-approved-production-raw-corpus-pending",
    "physical-sdr-hdr-monitor-switching-proof-pending",
    "physical-printer-paper-proof-pending",
    "blinded-editorial-approval-pending",
    "built-in-preset-retuning-pending",
)


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--source-root", type=Path, required=True)
  parser.add_argument("--tagged-set-root", type=Path, required=True)
  parser.add_argument("--raw-renderer", type=Path, required=True)
  parser.add_argument("--metal-test", type=Path, required=True)
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
      "apple_device_color_proof_validator", VALIDATOR_PATH
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


def artifact_paths(root: Path) -> list[str]:
  return sorted(
      path.relative_to(root).as_posix()
      for path in root.rglob("*") if path.is_file()
  )


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
  process = subprocess.run(
      command, check=False, capture_output=True, text=True
  )
  if process.returncode != 0:
    detail = process.stderr.strip() or process.stdout.strip()
    if process.returncode == 77:
      raise ValueError(f"{label} skipped; physical hardware proof cannot skip: {detail}")
    raise ValueError(f"{label} failed: {detail}")


def build_proof(
    source_root: Path, tagged_set_root: Path, raw_renderer: Path,
    metal_test: Path, output: Path, *, force: bool,
) -> dict[str, Any]:
  if platform.system() != "Darwin":
    raise ValueError("Apple device proof requires a Darwin host")
  inventory_path = source_root / "source-inventory.json"
  tagged_manifest_path = tagged_set_root / "tagged-reference-set.json"
  for path, label in (
      (inventory_path, "source inventory"),
      (tagged_manifest_path, "tagged set"),
      (raw_renderer, "RAW renderer"),
      (metal_test, "Metal test"),
  ):
    if not path.is_file():
      raise ValueError(f"{label} is missing: {path}")
  if output.exists() and not force:
    raise ValueError(f"output already exists: {output}; pass --force to replace it")

  staging = output.with_name(f".{output.name}.partial")
  shutil.rmtree(staging, ignore_errors=True)
  staging.mkdir(parents=True)
  real_root = staging / "real-raw-render-set"
  metal_root = staging / "metal"
  metal_root.mkdir(parents=True)
  metal_report_path = metal_root / "metal-aces2-device-proof.json"
  try:
    run_checked(
        [
            str(metal_test), "--tagged-set", str(tagged_manifest_path),
            "--report", str(metal_report_path),
        ],
        "Metal ACES 2 tagged-chart parity",
    )
    metal_report = load_json(metal_report_path)
    if metal_report.get("result") != "passed":
      raise ValueError("Metal report did not publish a pass result")
    run_checked(
        [
            sys.executable, "-B", str(PREPARE_REAL_RAW_PATH),
            "--source-root", str(source_root), "--candidate-tool",
            str(raw_renderer), "--output", str(real_root),
        ],
        "Core Image real-RAW render set",
    )
    real_manifest_path = real_root / "real-raw-render-set.json"
    real_manifest = load_json(real_manifest_path)
    core_candidates: list[dict[str, Any]] = []
    for cell in real_manifest["cells"]:
      shipping = next(
          render for render in cell["renders"]
          if render["decode_path"] == "shipping-default"
      )
      if shipping["decoder_backend"] != "Core Image CIRAWFilter":
        raise ValueError(
            f"{cell['candidate_id']} used {shipping['decoder_backend']!r}; "
            "actual-device proof forbids fallback"
        )
      core_candidates.append({
          "candidate_id": cell["candidate_id"],
          "decoder_backend": shipping["decoder_backend"],
          "report_path": "real-raw-render-set/" + shipping["report_path"],
          "report_sha256": shipping["report_sha256"],
          "output_path": "real-raw-render-set/" + shipping["output_path"],
          "output_sha256": shipping["output_sha256"],
      })

    inventory = load_json(inventory_path)
    tagged = load_json(tagged_manifest_path)
    blockers = list(dict.fromkeys(
        list(real_manifest["promotion_blockers"]) + list(ADDITIONAL_BLOCKERS)
    ))
    paths = artifact_paths(staging)
    byte_size, digest = artifact_set_digest(staging, paths)
    manifest = {
        "schema": SCHEMA,
        "proof_id": PROOF_ID,
        "description": (
            "Pass-only physical Apple proof for Core Image RAW decoding and "
            "Metal ACES 2 output parity."
        ),
        "promotion_eligible": False,
        "promotion_blockers": blockers,
        "host": {
            "system": platform.system(), "release": platform.release(),
            "machine": platform.machine(),
            "python_version": platform.python_version(),
        },
        "inputs": {
            "source_inventory_manifest_sha256": sha256_file(inventory_path),
            "tagged_set_id": tagged["set_id"],
            "tagged_set_manifest_sha256": sha256_file(tagged_manifest_path),
            "tagged_set_artifact_sha256": tagged["artifact_summary"]["artifact_set_sha256"],
            "real_raw_set_id": real_manifest["set_id"],
            "real_raw_manifest_path": "real-raw-render-set/real-raw-render-set.json",
            "real_raw_manifest_sha256": sha256_file(real_manifest_path),
            "real_raw_artifact_sha256": real_manifest["artifact_summary"]["artifact_set_sha256"],
        },
        "tools": {
            "raw_renderer_sha256": sha256_file(raw_renderer),
            "metal_test_sha256": sha256_file(metal_test),
            "runner_sha256": sha256_file(Path(__file__)),
            "validator_sha256": sha256_file(VALIDATOR_PATH),
        },
        "core_image": {
            "required_backend": "Core Image CIRAWFilter",
            "candidate_count": len(core_candidates),
            "passed_count": len(core_candidates),
            "candidates": core_candidates,
        },
        "metal": {
            "report_path": "metal/metal-aces2-device-proof.json",
            "report_sha256": sha256_file(metal_report_path),
            "device_name": metal_report["hardware"]["device_name"],
            "result": "passed",
        },
        "artifact_summary": {
            "artifact_count": len(paths), "byte_size": byte_size,
            "artifact_set_sha256": digest,
            "hash_algorithm": HASH_ALGORITHM,
        },
        "result": "passed",
    }
    manifest_path = staging / "apple-device-color-proof.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    validator = load_validator()
    errors = validator.validate_manifest(
        manifest_path, source_inventory_path=inventory_path,
        tagged_set_manifest_path=tagged_manifest_path,
        raw_renderer_path=raw_renderer, metal_test_path=metal_test,
        artifact_root=staging,
    )
    if errors:
      raise ValueError("generated Apple device proof is invalid: " + "; ".join(errors))
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
    manifest = build_proof(
        args.source_root.resolve(), args.tagged_set_root.resolve(),
        args.raw_renderer.resolve(), args.metal_test.resolve(),
        args.output.resolve(), force=args.force,
    )
  except (OSError, ValueError, KeyError, StopIteration, TypeError) as exc:
    print(f"error={exc}", file=sys.stderr)
    return 1
  print(f"status=created-physical-apple-device-color-proof:{args.output}")
  print(f"device_name={manifest['metal']['device_name']}")
  print(f"artifact_set_sha256={manifest['artifact_summary']['artifact_set_sha256']}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
