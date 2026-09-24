#!/usr/bin/env python3
"""Materialize the checksum-pinned, non-shipping color source inventory."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import shutil
import sys
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_INVENTORY = (
    REPO_ROOT / "perf-corpora" / "color-validation-required" / "source-inventory.json"
)
VALIDATOR_PATH = Path(__file__).with_name("validate-color-source-inventory.py")


class NoRedirectHandler(urllib.request.HTTPRedirectHandler):
  """Reject provenance-changing redirects instead of following them silently."""

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
      description="Download the verified color source inventory for private CI use."
  )
  parser.add_argument("--inventory", type=Path, default=DEFAULT_INVENTORY)
  parser.add_argument("--output", type=Path, required=True)
  parser.add_argument("--force", action="store_true")
  parser.add_argument("--timeout", type=float, default=120.0)
  return parser.parse_args()


def load_validator() -> Any:
  spec = importlib.util.spec_from_file_location("color_source_inventory_validator", VALIDATOR_PATH)
  if spec is None or spec.loader is None:
    raise RuntimeError(f"unable to import {VALIDATOR_PATH}")
  module = importlib.util.module_from_spec(spec)
  sys.modules["color_source_inventory_validator"] = module
  spec.loader.exec_module(module)
  return module


def load_json(path: Path) -> dict[str, Any]:
  with path.open("r", encoding="utf-8") as handle:
    payload = json.load(handle)
  if not isinstance(payload, dict):
    raise ValueError(f"{path} root must be a JSON object")
  return payload


def sha256_file(path: Path) -> str:
  digest = hashlib.sha256()
  with path.open("rb") as handle:
    for block in iter(lambda: handle.read(1024 * 1024), b""):
      digest.update(block)
  return digest.hexdigest()


def download_file(url: str, destination: Path, timeout: float) -> None:
  opener = urllib.request.build_opener(NoRedirectHandler())
  request = urllib.request.Request(
      url, headers={"User-Agent": "PhotoEditorColorSourceInventory/1.0"}
  )
  try:
    with opener.open(request, timeout=timeout) as response:
      final_url = response.geturl()
      if final_url != url:
        raise ValueError(f"source URL changed during download: {url} -> {final_url}")
      with destination.open("wb") as handle:
        shutil.copyfileobj(response, handle)
  except urllib.error.HTTPError as exc:
    if 300 <= exc.code < 400:
      raise ValueError(f"source URL redirected and was rejected: {url}") from exc
    raise


def file_contracts(candidate: dict[str, Any]) -> list[dict[str, Any]]:
  contracts = [candidate["capture"], candidate["reference"]]
  contracts.extend(candidate["sidecars"])
  return contracts


def materialize_file(
    contract: dict[str, Any], destination_dir: Path, timeout: float
) -> tuple[int, str]:
  file_name = contract["file_name"]
  destination = destination_dir / file_name
  partial = destination.with_name(f".{destination.name}.partial")
  if partial.exists():
    partial.unlink()
  try:
    download_file(contract["url"], partial, timeout)
    actual_size = partial.stat().st_size
    actual_sha256 = sha256_file(partial)
    if actual_size != contract["byte_size"]:
      raise ValueError(
          f"{file_name}: byte size mismatch; expected {contract['byte_size']}, "
          f"received {actual_size}"
      )
    if actual_sha256 != contract["sha256"]:
      raise ValueError(
          f"{file_name}: SHA-256 mismatch; expected {contract['sha256']}, "
          f"received {actual_sha256}"
      )
    partial.replace(destination)
    return actual_size, actual_sha256
  finally:
    if partial.exists():
      partial.unlink()


def materialize_inventory(
    inventory_path: Path,
    output: Path,
    timeout: float,
    *,
    force: bool = False,
) -> dict[str, Any]:
  validator = load_validator()
  errors = validator.validate_inventory(inventory_path.resolve())
  if errors:
    raise ValueError("invalid source inventory: " + "; ".join(errors))
  inventory = load_json(inventory_path)

  if output.exists():
    if not force:
      raise ValueError(f"output already exists: {output}; pass --force to replace it")
    shutil.rmtree(output)
  output.mkdir(parents=True, exist_ok=False)

  file_count = 0
  total_bytes = 0
  try:
    for candidate in inventory["candidates"]:
      candidate_dir = output / candidate["candidate_id"]
      candidate_dir.mkdir()
      for contract in file_contracts(candidate):
        print(f"download {candidate['candidate_id']}: {contract['url']}")
        size, _digest = materialize_file(contract, candidate_dir, timeout)
        file_count += 1
        total_bytes += size

    errors = validator.validate_inventory(inventory_path.resolve(), output.resolve())
    if errors:
      raise ValueError("materialized inventory failed verification: " + "; ".join(errors))
    shutil.copy2(inventory_path, output / "source-inventory.json")
    summary = {
        "schema": "photo-editor-color-source-fetch-summary:v1",
        "inventory_id": inventory["inventory_id"],
        "inventory_sha256": sha256_file(inventory_path),
        "created_at": datetime.now(timezone.utc).isoformat(),
        "file_count": file_count,
        "total_bytes": total_bytes,
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
    summary = materialize_inventory(
        args.inventory.resolve(),
        args.output.resolve(),
        args.timeout,
        force=args.force,
    )
  except Exception as exc:  # noqa: BLE001 - CLI should preserve the exact source failure.
    print(f"unable to materialize color source inventory: {exc}", file=sys.stderr)
    return 1
  print(f"output={args.output.resolve()}")
  print(f"files={summary['file_count']}")
  print(f"bytes={summary['total_bytes']}")
  print("promotion_eligible=false")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
