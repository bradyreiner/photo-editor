#!/usr/bin/env python3
"""Adversarial self-test for the color source inventory and fetcher."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import sys
import tempfile
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
INVENTORY_PATH = (
    REPO_ROOT / "perf-corpora" / "color-validation-required" / "source-inventory.json"
)
VALIDATOR_PATH = REPO_ROOT / "scripts/color/validate-color-source-inventory.py"
FETCHER_PATH = REPO_ROOT / "scripts/color/fetch-color-source-inventory.py"


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


def all_contracts(manifest: dict[str, Any]) -> list[dict[str, Any]]:
  contracts: list[dict[str, Any]] = []
  for candidate in manifest["candidates"]:
    contracts.extend([candidate["capture"], candidate["reference"]])
    contracts.extend(candidate["sidecars"])
  return contracts


def error_contains(errors: list[str], text: str) -> bool:
  return any(text in error for error in errors)


def main() -> int:
  validator = load_module("color_source_validator_test", VALIDATOR_PATH)
  fetcher = load_module("color_source_fetcher_test", FETCHER_PATH)
  with INVENTORY_PATH.open("r", encoding="utf-8") as handle:
    baseline = json.load(handle)

  errors = validator.validate_inventory(INVENTORY_PATH)
  require(not errors, f"checked-in source inventory was rejected: {errors}")

  with tempfile.TemporaryDirectory(prefix="photo-editor-color-source-") as temp:
    root = Path(temp)

    def validate_mutation(
        name: str, payload: dict[str, Any], expected_error: str
    ) -> None:
      path = root / f"{name}.json"
      write_json(path, payload)
      mutation_errors = validator.validate_inventory(path)
      require(
          error_contains(mutation_errors, expected_error),
          f"{name} mutation was not rejected for {expected_error!r}: {mutation_errors}",
      )

    stale_count = copy.deepcopy(baseline)
    stale_count["candidate_count"] += 1
    validate_mutation("stale-count", stale_count, "candidate_count mismatch")

    unknown = copy.deepcopy(baseline)
    unknown["candidates"][0]["license_ok"] = True
    validate_mutation("unknown-field", unknown, "unknown field")

    fake_promotion = copy.deepcopy(baseline)
    fake_promotion["candidates"][0]["promotion_eligible"] = True
    validate_mutation("fake-promotion", fake_promotion, "must not claim promotion eligibility")

    missing_iso_blocker = copy.deepcopy(baseline)
    missing_iso_blocker["candidates"][1]["promotion_blockers"].remove(
        "missing-iso-metadata"
    )
    validate_mutation(
        "missing-iso-blocker", missing_iso_blocker, "null iso requires"
    )

    ordinary = next(
        candidate for candidate in baseline["candidates"]
        if candidate["technical_status"] == "source-qualified"
    )
    ordinary_index = baseline["candidates"].index(ordinary)
    people = copy.deepcopy(baseline)
    people["candidates"][ordinary_index]["visual_review"]["people_present"] = True
    validate_mutation("people", people, "must contain no recognizable people")

    portrait = next(
        candidate for candidate in baseline["candidates"]
        if candidate["technical_status"]
        == "source-qualified-depicted-person-review-pending"
    )
    portrait_index = baseline["candidates"].index(portrait)
    missing_person_blocker = copy.deepcopy(baseline)
    missing_person_blocker["candidates"][portrait_index]["promotion_blockers"].remove(
        "depicted-person-review-required"
    )
    validate_mutation(
        "missing-person-blocker",
        missing_person_blocker,
        "requires the depicted-person-review-required blocker",
    )

    hidden_person = copy.deepcopy(baseline)
    hidden_person["candidates"][portrait_index]["visual_review"][
        "people_present"
    ] = False
    validate_mutation(
        "hidden-person", hidden_person, "requires people_present=true"
    )

    substituted_host = copy.deepcopy(baseline)
    substituted_host["candidates"][0]["capture"]["url"] = (
        "https://example.invalid/substituted.dng"
    )
    validate_mutation(
        "substituted-host", substituted_host, "dataset download_prefix"
    )

    path_escape = copy.deepcopy(baseline)
    path_escape["candidates"][0]["capture"]["file_name"] = "../escape.dng"
    validate_mutation("path-escape", path_escape, "must be a plain file name")

    self_reference = copy.deepcopy(baseline)
    self_reference["candidates"][0]["reference"]["producer"] = (
        "Photo-Editor candidate renderer"
    )
    validate_mutation("self-reference", self_reference, "must be independent")

    materialized_manifest = copy.deepcopy(baseline)
    source_bytes: dict[str, bytes] = {}
    for index, contract in enumerate(all_contracts(materialized_manifest)):
      content = f"color-source-fixture:{index}:{contract['url']}\n".encode("utf-8")
      contract["byte_size"] = len(content)
      contract["sha256"] = hashlib.sha256(content).hexdigest()
      source_bytes[contract["url"]] = content
    materialized_manifest_path = root / "materialized-inventory.json"
    write_json(materialized_manifest_path, materialized_manifest)

    original_download = fetcher.download_file
    fetcher.download_file = (
        lambda url, destination, _timeout: destination.write_bytes(source_bytes[url])
    )
    try:
      output = root / "materialized"
      summary = fetcher.materialize_inventory(
          materialized_manifest_path, output, 1.0
      )
      require(summary["file_count"] == 24, "fetcher did not materialize every contract")
      require(
          summary["promotion_eligible"] is False,
          "fetch summary incorrectly claimed promotion eligibility",
      )
      errors = validator.validate_inventory(materialized_manifest_path, output)
      require(not errors, f"materialized inventory was rejected: {errors}")

      first = materialized_manifest["candidates"][0]
      first_capture = output / first["candidate_id"] / first["capture"]["file_name"]
      original = first_capture.read_bytes()
      first_capture.write_bytes(b"X" + original[1:])
      errors = validator.validate_inventory(materialized_manifest_path, output)
      require(
          error_contains(errors, "materialized sha256 mismatch"),
          f"same-size materialized tamper was not rejected: {errors}",
      )

      corrupt_output = root / "corrupt-download"
      changed = dict(source_bytes)
      first_url = materialized_manifest["candidates"][0]["capture"]["url"]
      changed[first_url] = b"upstream-substitution\n"
      fetcher.download_file = (
          lambda url, destination, _timeout: destination.write_bytes(changed[url])
      )
      try:
        fetcher.materialize_inventory(
            materialized_manifest_path, corrupt_output, 1.0
        )
      except ValueError as exc:
        require(
            "mismatch" in str(exc),
            f"upstream substitution failed for the wrong reason: {exc}",
        )
      else:
        raise SystemExit("upstream byte substitution was not rejected")
      require(
          not corrupt_output.exists(),
          "failed materialization left a partially trusted output directory",
      )
    finally:
      fetcher.download_file = original_download

  print("color_source_inventory_self_test=passed")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
