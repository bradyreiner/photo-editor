#!/usr/bin/env python3
"""Fetch and selectively materialize the pinned Middlebury chart reference."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import shutil
import sys
import urllib.error
import urllib.request
import zipfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = (
    REPO_ROOT / "perf-corpora" / "color-validation-required" /
    "independent-chart-source.json"
)
VALIDATOR_PATH = Path(__file__).with_name("validate-independent-chart-source.py")


class NoRedirectHandler(urllib.request.HTTPRedirectHandler):
  """Reject provenance-changing redirects instead of following them."""

  def redirect_request(
      self,
      req: urllib.request.Request,
      fp: Any,
      code: int,
      msg: str,
      headers: Any,
      newurl: str,
  ) -> None:
    del req, fp, code, msg, headers, newurl
    return None


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(
      description="Materialize the checksum-pinned independent chart subset."
  )
  parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
  parser.add_argument("--output", type=Path, required=True)
  parser.add_argument(
      "--archive", type=Path,
      help="Use an already downloaded archive after exact verification.",
  )
  parser.add_argument("--force", action="store_true")
  parser.add_argument("--timeout", type=float, default=180.0)
  return parser.parse_args()


def load_validator() -> Any:
  spec = importlib.util.spec_from_file_location(
      "independent_chart_source_validator", VALIDATOR_PATH
  )
  if spec is None or spec.loader is None:
    raise RuntimeError(f"unable to import {VALIDATOR_PATH}")
  module = importlib.util.module_from_spec(spec)
  sys.modules["independent_chart_source_validator"] = module
  spec.loader.exec_module(module)
  return module


def load_json(path: Path) -> dict[str, Any]:
  with path.open("r", encoding="utf-8") as handle:
    value = json.load(handle)
  if not isinstance(value, dict):
    raise ValueError(f"{path} root must be a JSON object")
  return value


def sha256_file(path: Path) -> str:
  digest = hashlib.sha256()
  with path.open("rb") as handle:
    for block in iter(lambda: handle.read(1024 * 1024), b""):
      digest.update(block)
  return digest.hexdigest()


def download_archive(
    url: str, destination: Path, expected_size: int, timeout: float
) -> None:
  opener = urllib.request.build_opener(NoRedirectHandler())
  request = urllib.request.Request(
      url, headers={"User-Agent": "PhotoEditorIndependentChartSource/1.0"}
  )
  received = 0
  try:
    with opener.open(request, timeout=timeout) as response:
      if response.geturl() != url:
        raise ValueError(f"source URL changed during download: {url}")
      declared = response.headers.get("Content-Length")
      if declared is not None and int(declared) != expected_size:
        raise ValueError(
            f"archive Content-Length mismatch: expected {expected_size}, got {declared}"
        )
      with destination.open("wb") as handle:
        while True:
          block = response.read(1024 * 1024)
          if not block:
            break
          received += len(block)
          if received > expected_size:
            raise ValueError("archive exceeded its pinned byte size")
          handle.write(block)
  except urllib.error.HTTPError as exc:
    if 300 <= exc.code < 400:
      raise ValueError(f"source URL redirected and was rejected: {url}") from exc
    raise
  if received != expected_size:
    raise ValueError(
        f"archive byte-size mismatch: expected {expected_size}, received {received}"
    )


def materialize_source(
    manifest_path: Path,
    output: Path,
    timeout: float,
    *,
    archive_path: Path | None = None,
    force: bool = False,
) -> dict[str, Any]:
  validator = load_validator()
  errors = validator.validate_manifest(manifest_path.resolve())
  if errors:
    raise ValueError("invalid chart-source manifest: " + "; ".join(errors))
  manifest = load_json(manifest_path)
  if output.exists():
    if not force:
      raise ValueError(f"output already exists: {output}; pass --force to replace it")
    shutil.rmtree(output)
  output.mkdir(parents=True, exist_ok=False)
  partial_archive = output / ".source-archive.partial"
  source_archive = archive_path.resolve() if archive_path is not None else partial_archive
  try:
    if archive_path is None:
      dataset = manifest["dataset"]
      download_archive(
          dataset["archive_url"],
          partial_archive,
          dataset["archive_byte_size"],
          timeout,
      )
    errors = validator.validate_manifest(
        manifest_path.resolve(), archive_path=source_archive
    )
    if errors:
      raise ValueError("archive verification failed: " + "; ".join(errors))

    members_root = output / "members"
    members_root.mkdir()
    selected = validator.selected_member_names(manifest)
    with zipfile.ZipFile(source_archive) as archive:
      for name in selected:
        destination = members_root / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(archive.read(name))
    if partial_archive.exists():
      partial_archive.unlink()
    errors = validator.validate_manifest(
        manifest_path.resolve(), materialized_root=output.resolve()
    )
    if errors:
      raise ValueError("materialized chart subset failed verification: " + "; ".join(errors))

    shutil.copy2(manifest_path, output / "independent-chart-source.json")
    summary = {
        "schema": "photo-editor-independent-chart-fetch-summary:v1",
        "inventory_id": manifest["inventory_id"],
        "manifest_sha256": sha256_file(manifest_path),
        "archive_sha256": manifest["dataset"]["archive_sha256"],
        "created_at": datetime.now(timezone.utc).isoformat(),
        "selected_member_count": len(selected),
        "selected_byte_size": manifest["selection"]["selected_byte_size"],
        "promotion_eligible": False,
    }
    (output / "fetch-summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return summary
  except Exception:
    shutil.rmtree(output, ignore_errors=True)
    raise


def main() -> int:
  args = parse_args()
  try:
    summary = materialize_source(
        args.manifest.resolve(),
        args.output.resolve(),
        args.timeout,
        archive_path=args.archive,
        force=args.force,
    )
  except Exception as exc:  # noqa: BLE001 - retain the exact source failure.
    print(f"unable to materialize independent chart source: {exc}", file=sys.stderr)
    return 1
  print(f"output={args.output.resolve()}")
  print(f"files={summary['selected_member_count']}")
  print(f"bytes={summary['selected_byte_size']}")
  print("promotion_eligible=false")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
