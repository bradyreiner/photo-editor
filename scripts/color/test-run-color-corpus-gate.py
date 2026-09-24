#!/usr/bin/env python3
"""Self-test full-matrix color-corpus orchestration and evidence retention."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
RUNNER_PATH = REPO_ROOT / "scripts/color/run-color-corpus-gate.py"
VALIDATOR_PATH = REPO_ROOT / "scripts/color/validate-color-corpus.py"
FIXTURE_HELPER_PATH = REPO_ROOT / "scripts/color/test-validate-color-corpus.py"


def load_module(path: Path, name: str) -> Any:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise SystemExit(f"unable to import {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def file_record(path: Path, color_encoding: str | None = None) -> dict[str, Any]:
    content = path.read_bytes()
    record: dict[str, Any] = {
        "path": str(path.resolve()),
        "byte_size": len(content),
        "sha256": hashlib.sha256(content).hexdigest(),
    }
    if color_encoding is not None:
        record["color_encoding"] = color_encoding
    return record


def materialize_render_set(
    root: Path,
    manifest_path: Path,
    payload: dict[str, Any],
) -> tuple[Path, dict[str, Any]]:
    render_dir = root / "render-pairs"
    render_dir.mkdir()
    artifacts: list[dict[str, Any]] = []
    output_profile_id = "profile-output-icc"
    captures = [asset for asset in payload["assets"] if asset["kind"] == "capture"]
    for capture in captures:
        for execution_path in sorted(capture["execution_paths"]):
            stem = f"{capture['asset_id']}--{execution_path}"
            candidate = render_dir / f"{stem}--candidate.tif"
            reference = render_dir / f"{stem}--reference.tif"
            candidate.write_bytes(f"candidate:{stem}\n".encode("utf-8"))
            reference.write_bytes(f"reference:{stem}\n".encode("utf-8"))
            artifacts.append(
                {
                    "capture_id": capture["asset_id"],
                    "execution_path": execution_path,
                    "reference_asset_id": capture["reference_ids"][0],
                    "output_profile_id": output_profile_id,
                    "candidate": file_record(candidate, "tagged test candidate"),
                    "reference": file_record(reference, "tagged independent test reference"),
                }
            )
    comparison = payload["comparison_sets"][0]
    render_set_path = root / comparison["artifact_manifest_path"]
    render_set = {
        "schema": "photo-editor-color-corpus-render-set:v1",
        "corpus_id": payload["corpus_id"],
        "corpus_version": payload["corpus_version"],
        "comparison_set_id": comparison["comparison_set_id"],
        "candidate_rendering_model": payload["candidate_rendering_model"],
        "reference_renderer": {
            "rendering_model": "independent-reference-v1",
            "organization": "Independent fixture laboratory",
            "tool": "Deterministic runner fixture",
            "version": "1",
            "independent_of_photo_editor": True,
        },
        "artifact_count": len(artifacts),
        "artifacts": artifacts,
    }
    write_json(render_set_path, render_set)
    render_set_bytes = render_set_path.read_bytes()
    comparison["artifact_manifest_sha256"] = hashlib.sha256(render_set_bytes).hexdigest()
    comparison["artifact_manifest_byte_size"] = len(render_set_bytes)
    write_json(manifest_path, payload)
    return render_set_path, render_set


def option(command: list[str], name: str) -> str:
    index = command.index(name)
    return command[index + 1]


def metric(threshold: float, *, percentile: bool = False) -> dict[str, Any]:
    value = {
        "count": 16,
        "mean": 0.0,
        "p50_upper_bound": 0.0,
        "p95_upper_bound": 0.0,
        "p99_upper_bound": 0.0,
        "max": 0.0,
        "threshold": threshold,
        "gate_passed": True,
    }
    if percentile:
        value["above_threshold_count"] = 0
    return value


def make_fake_comparator(
    calls: list[list[str]],
    *,
    fail_first: bool = False,
) -> Any:
    def run(command: list[str]) -> subprocess.CompletedProcess[str]:
        calls.append(command)
        if fail_first and len(calls) == 1:
            return subprocess.CompletedProcess(command, 1, "", "synthetic native failure")
        corpus_path = Path(option(command, "--corpus-manifest"))
        corpus = json.loads(corpus_path.read_text(encoding="utf-8"))
        capture_id = option(command, "--capture-id")
        execution_path = option(command, "--execution-path")
        report_path = Path(option(command, "--report"))
        heatmap_path = Path(option(command, "--heatmap"))
        candidate_path = Path(option(command, "--candidate"))
        reference_path = Path(option(command, "--reference"))
        capture = next(
            asset
            for asset in corpus["assets"]
            if asset.get("kind") == "capture" and asset["asset_id"] == capture_id
        )
        gate = corpus["quality_gates"][execution_path]
        heatmap_path.write_bytes(f"heatmap:{capture_id}:{execution_path}\n".encode("utf-8"))
        report = {
            "schema": "photo-editor-color-corpus-comparison:v1",
            "tool_version": "fixture",
            "passed": True,
            "failure": "",
            "comparison_space": "linear Rec.2020 D65",
            "linear_one_nits": gate["linear_one_nits"],
            "percentile_method": "deterministic runner fixture",
            "dimensions": {"width": 4, "height": 4},
            "provenance": {
                "execution_path": execution_path,
                "capture_id": capture_id,
                "candidate_rendering_model": option(
                    command, "--candidate-rendering-model"
                ),
                "reference_rendering_model": option(
                    command, "--reference-rendering-model"
                ),
                "recipe_sha256": option(command, "--recipe-sha256"),
                "output_profile_sha256": option(
                    command, "--output-profile-sha256"
                ),
                "corpus_manifest_sha256": hashlib.sha256(
                    corpus_path.read_bytes()
                ).hexdigest(),
            },
            "candidate": file_record(candidate_path),
            "reference": file_record(reference_path),
            "heatmap": file_record(heatmap_path),
            "metrics": {
                "delta_e_00_p95": metric(
                    gate["p95_delta_e_00"], percentile=True
                ),
                "delta_e_00_max": metric(gate["max_delta_e_00"]),
                "delta_e_itp_p95": metric(
                    gate["p95_delta_e_itp"], percentile=True
                ),
                "delta_e_itp_max": metric(gate["max_delta_e_itp"]),
                "alpha_error_max": metric(gate["max_alpha_error"]),
            },
            "worst_pixel": {
                "delta_e_00": {"value": 0.0},
                "delta_e_itp": {"value": 0.0},
                "alpha_error": {"value": 0.0},
            },
            "regions": [
                {"label": region["label"]}
                for region in capture["review_regions"]
            ],
        }
        write_json(report_path, report)
        return subprocess.CompletedProcess(command, 0, "comparison=passed\n", "")

    return run


def refresh_render_set_identity(
    manifest_path: Path,
    payload: dict[str, Any],
    render_set_path: Path,
) -> None:
    content = render_set_path.read_bytes()
    comparison = payload["comparison_sets"][0]
    comparison["artifact_manifest_sha256"] = hashlib.sha256(content).hexdigest()
    comparison["artifact_manifest_byte_size"] = len(content)
    write_json(manifest_path, payload)


def main() -> int:
    runner = load_module(RUNNER_PATH, "color_corpus_runner")
    validator = load_module(VALIDATOR_PATH, "color_corpus_validator_runner_fixture")
    fixture_helper = load_module(FIXTURE_HELPER_PATH, "color_corpus_runner_fixture_helper")

    with tempfile.TemporaryDirectory(prefix="photo-editor-color-runner-") as temp:
        root = Path(temp)
        manifest_path, payload = fixture_helper.materialize_fixture(root, validator)
        render_set_path, render_set = materialize_render_set(root, manifest_path, payload)
        comparison_set_id = payload["comparison_sets"][0]["comparison_set_id"]

        preflight_errors, _, jobs = runner.validate_render_set(
            manifest_path, comparison_set_id
        )
        require(not preflight_errors, f"valid render set was rejected: {preflight_errors}")
        require(len(jobs) == 28, f"expected 28 comparison jobs, received {len(jobs)}")

        validation_code, validation_summary = runner.execute_gate(
            manifest_path,
            comparison_set_id,
            root / "comparison-tool-not-built",
            root / "validate-only-run",
            validate_only=True,
        )
        require(
            validation_code == 0
            and validation_summary["passed"] is True
            and validation_summary["job_count"] == 28,
            f"validate-only run failed: {validation_summary}",
        )

        calls: list[list[str]] = []
        code, summary = runner.execute_gate(
            manifest_path,
            comparison_set_id,
            Path(sys.executable),
            root / "successful-run",
            command_runner=make_fake_comparator(calls),
        )
        require(
            code == 0
            and summary["passed"] is True
            and summary["job_count"] == 28
            and len(calls) == 28,
            f"complete runner failed: {summary}",
        )
        aggregate = json.loads(
            (root / "successful-run/aggregate.json").read_text(encoding="utf-8")
        )
        require(
            aggregate["passed"] is True and aggregate["observed_report_count"] == 28,
            f"runner aggregate is incomplete: {aggregate}",
        )

        failure_calls: list[list[str]] = []
        code, summary = runner.execute_gate(
            manifest_path,
            comparison_set_id,
            Path(sys.executable),
            root / "native-failure-run",
            command_runner=make_fake_comparator(failure_calls, fail_first=True),
        )
        require(
            code == 1
            and summary["passed"] is False
            and len(failure_calls) == 28
            and any("native comparisons failed" in error for error in summary["errors"]),
            f"runner did not retain evidence after a native failure: {summary}",
        )

        candidate_path = Path(render_set["artifacts"][0]["candidate"]["path"])
        original_candidate = candidate_path.read_bytes()
        candidate_path.write_bytes(b"X" + original_candidate[1:])
        preflight_calls: list[list[str]] = []
        code, summary = runner.execute_gate(
            manifest_path,
            comparison_set_id,
            Path(sys.executable),
            root / "tampered-run",
            command_runner=make_fake_comparator(preflight_calls),
        )
        require(
            code == 1
            and summary["stage"] == "preflight"
            and not preflight_calls
            and any("sha256 mismatch" in error for error in summary["errors"]),
            f"tampered candidate was not rejected before execution: {summary}",
        )
        candidate_path.write_bytes(original_candidate)

        duplicate_render_set = json.loads(render_set_path.read_text(encoding="utf-8"))
        duplicate_render_set["artifacts"][-1] = duplicate_render_set["artifacts"][0]
        write_json(render_set_path, duplicate_render_set)
        refresh_render_set_identity(manifest_path, payload, render_set_path)
        duplicate_errors, _, _ = runner.validate_render_set(
            manifest_path, comparison_set_id, schema_only=True
        )
        require(
            any("duplicate render artifact" in error for error in duplicate_errors)
            and any("missing render artifact" in error for error in duplicate_errors),
            f"duplicate matrix cell was not rejected: {duplicate_errors}",
        )

    print("color_corpus_runner_self_test=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
