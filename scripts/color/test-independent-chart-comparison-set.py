#!/usr/bin/env python3
"""End-to-end and adversarial tests for the 48-cell chart comparison gate."""

from __future__ import annotations

import copy
import importlib.util
import json
import os
import tempfile
from pathlib import Path
from typing import Any


SCRIPT_ROOT = Path(__file__).resolve().parent
RENDER_FIXTURE_PATH = SCRIPT_ROOT / "test-independent-chart-render-set.py"
RENDER_VALIDATOR_PATH = SCRIPT_ROOT / "validate-independent-chart-render-set.py"
RUNNER_PATH = SCRIPT_ROOT / "run-independent-chart-comparison-set.py"
VALIDATOR_PATH = SCRIPT_ROOT / "validate-independent-chart-comparison-set.py"


def load_module(path: Path, name: str) -> Any:
  spec = importlib.util.spec_from_file_location(name, path)
  if spec is None or spec.loader is None:
    raise RuntimeError(f"unable to load module: {path}")
  module = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(module)
  return module


def write_json(path: Path, value: dict[str, Any]) -> None:
  path.write_text(
      json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
  )


FAKE_TOOL = r'''#!/usr/bin/env python3
import hashlib
import json
import pathlib
import sys

args = sys.argv[1:]
def option(name):
  return args[args.index(name) + 1]
def sha(path):
  return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()

candidate = option("--candidate")
reference = option("--reference")
report_path = pathlib.Path(option("--report"))
heatmap_path = pathlib.Path(option("--heatmap"))
contract = option("--corpus-manifest")
heatmap_path.parent.mkdir(parents=True, exist_ok=True)
heatmap_path.write_bytes(b"synthetic-heatmap:" + option("--capture-id").encode())
count = 606 * 404
summary = {
    "count": count, "gate_passed": True, "max": 0.03, "mean": 0.01,
    "p50_upper_bound": 0.01, "p95_upper_bound": 0.02,
    "p99_upper_bound": 0.025,
}
alpha = dict(summary)
alpha.update({"max": 0.0, "mean": 0.0, "p50_upper_bound": 0.0,
              "p95_upper_bound": 0.0, "p99_upper_bound": 0.0})
report = {
    "schema": "photo-editor-color-corpus-comparison:v1",
    "tool_version": "synthetic-test", "passed": True, "failure": "",
    "dimensions": {"width": 606, "height": 404},
    "comparison_space": "linear Rec.2020 D65", "linear_one_nits": 100,
    "candidate": {"path": candidate, "sha256": sha(candidate),
                  "byte_size": pathlib.Path(candidate).stat().st_size},
    "reference": {"path": reference, "sha256": sha(reference),
                  "byte_size": pathlib.Path(reference).stat().st_size},
    "heatmap": {"path": str(heatmap_path), "sha256": sha(heatmap_path),
                "byte_size": heatmap_path.stat().st_size},
    "provenance": {
        "execution_path": option("--execution-path"),
        "capture_id": option("--capture-id"),
        "candidate_rendering_model": option("--candidate-rendering-model"),
        "reference_rendering_model": option("--reference-rendering-model"),
        "recipe_sha256": option("--recipe-sha256"),
        "output_profile_sha256": option("--output-profile-sha256"),
        "corpus_manifest_sha256": sha(contract),
    },
    "metrics": {
        "delta_e_00_p95": dict(summary), "delta_e_00_max": dict(summary),
        "delta_e_itp_p95": dict(summary), "delta_e_itp_max": dict(summary),
        "alpha_error_max": alpha,
    },
    "regions": [], "worst_pixel": {},
    "percentile_method": "synthetic deterministic fixture",
}
report_path.parent.mkdir(parents=True, exist_ok=True)
report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
'''


def main() -> int:
  render_fixture = load_module(RENDER_FIXTURE_PATH, "render_fixture")
  render_validator = load_module(RENDER_VALIDATOR_PATH, "render_validator")
  runner = load_module(RUNNER_PATH, "comparison_runner")
  validator = load_module(VALIDATOR_PATH, "comparison_validator")
  with tempfile.TemporaryDirectory(prefix="chart-comparison-set-test-") as temporary:
    root = Path(temporary)
    render_root = root / "render-set"
    render_root.mkdir()
    render_path, tagged_path, _ = render_fixture.make_fixture(
        render_root, render_validator
    )
    if render_path != render_root / "render-set.json":
      raise AssertionError("render fixture path contract changed")
    fake_tool = root / "fake-comparison-tool.py"
    fake_tool.write_text(FAKE_TOOL, encoding="utf-8")
    fake_tool.chmod(fake_tool.stat().st_mode | 0o111)
    output = root / "comparison-set"
    aggregate = runner.run_set(
        render_root, tagged_path, fake_tool, output, force=False
    )
    if not aggregate["passed"] or aggregate["summary"]["cell_count"] != 48:
      raise AssertionError("complete synthetic comparison run did not pass")
    manifest_path = output / "comparison-set.json"

    def validate(value: dict[str, Any]) -> list[str]:
      write_json(manifest_path, value)
      return validator.validate_manifest(
          manifest_path, artifact_root=output, render_set_root=render_root,
          tagged_set_manifest_path=tagged_path,
      )

    baseline = copy.deepcopy(aggregate)
    errors = validate(baseline)
    if errors:
      raise AssertionError(f"valid comparison set rejected: {errors}")

    mutations = [
        ("unknown field", lambda value: value.__setitem__("surprise", True),
         "unknown field"),
        ("missing cell", lambda value: value["cells"].pop(), "exactly 48"),
        ("post-hoc gate", lambda value: value["quality_gates"].__setitem__(
            "p95_delta_e_00", 99), "precommitted"),
        ("contradictory pass", lambda value: value.__setitem__("passed", False),
         "contradicts"),
        ("worst summary", lambda value: value["summary"][
            "worst_max_delta_e_00"].__setitem__("value", 0), "contradicts"),
    ]
    for name, mutate, expected in mutations:
      value = copy.deepcopy(baseline)
      mutate(value)
      errors = validate(value)
      if not any(expected in error for error in errors):
        raise AssertionError(f"{name} was not rejected as expected: {errors}")

    heatmap = output / baseline["cells"][0]["heatmap_id"]
    original = heatmap.read_bytes()
    heatmap.write_bytes(original + b"tampered")
    errors = validate(copy.deepcopy(baseline))
    heatmap.write_bytes(original)
    if not any("heatmap hash mismatch" in error for error in errors):
      raise AssertionError(f"heatmap byte tampering was not rejected: {errors}")

  print("independent chart comparison-set tests passed")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
