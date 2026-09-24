#!/usr/bin/env python3
"""Run the complete immutable color-corpus comparison matrix."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any, Callable


REPO_ROOT = Path(__file__).resolve().parents[2]
VALIDATOR_PATH = REPO_ROOT / "scripts/color/validate-color-corpus.py"
AGGREGATOR_PATH = REPO_ROOT / "scripts/color/aggregate-color-corpus-report.py"
RENDER_SET_SCHEMA = "photo-editor-color-corpus-render-set:v1"
RUN_SCHEMA = "photo-editor-color-corpus-run:v1"
REQUIRED_PATHS = {
    "camera-input",
    "icc",
    "sdr",
    "hdr",
    "proof",
    "preview",
    "export",
}
ROOT_KEYS = {
    "schema",
    "corpus_id",
    "corpus_version",
    "comparison_set_id",
    "candidate_rendering_model",
    "reference_renderer",
    "artifact_count",
    "artifacts",
}
ARTIFACT_KEYS = {
    "capture_id",
    "execution_path",
    "reference_asset_id",
    "output_profile_id",
    "candidate",
    "reference",
}
FILE_KEYS = {"path", "sha256", "byte_size", "color_encoding"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate and execute every capture/path comparison in a blinded render set."
    )
    parser.add_argument("manifest", type=Path, help="Populated color-corpus manifest.")
    parser.add_argument("comparison_set_id", help="Blinded comparison set to execute.")
    parser.add_argument(
        "--comparison-tool",
        type=Path,
        required=True,
        help="Built color_corpus_comparison_tool executable.",
    )
    parser.add_argument("--output-dir", type=Path, required=True, help="New run artifact directory.")
    parser.add_argument(
        "--validate-only",
        action="store_true",
        help="Validate the corpus and render-set bytes without invoking comparisons.",
    )
    return parser.parse_args()


def load_module(path: Path, name: str) -> Any:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to import {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def resolve_path(base_file: Path, value: str) -> Path:
    candidate = Path(value).expanduser()
    if candidate.is_absolute():
        return candidate
    return (base_file.parent / candidate).resolve()


def check_keys(
    value: dict[str, Any],
    allowed: set[str],
    required: set[str],
    label: str,
    errors: list[str],
) -> None:
    for key in sorted(set(value) - allowed):
        errors.append(f"{label}: unknown field {key!r}")
    for key in sorted(required - set(value)):
        errors.append(f"{label}: missing required field {key!r}")


def verify_file_record(
    render_set_path: Path,
    value: Any,
    label: str,
    schema_only: bool,
    errors: list[str],
) -> Path | None:
    if not isinstance(value, dict):
        errors.append(f"{label} must be an object")
        return None
    check_keys(value, FILE_KEYS, FILE_KEYS, label, errors)
    path_value = value.get("path")
    expected_sha = value.get("sha256")
    expected_size = value.get("byte_size")
    encoding = value.get("color_encoding")
    if not isinstance(path_value, str) or not path_value:
        errors.append(f"{label}.path must be a non-empty string")
        return None
    if (
        not isinstance(expected_sha, str)
        or len(expected_sha) != 64
        or any(character not in "0123456789abcdef" for character in expected_sha)
    ):
        errors.append(f"{label}.sha256 must be a lowercase SHA-256 digest")
    if not isinstance(expected_size, int) or isinstance(expected_size, bool) or expected_size <= 0:
        errors.append(f"{label}.byte_size must be a positive integer")
    if not isinstance(encoding, str) or not encoding:
        errors.append(f"{label}.color_encoding must be a non-empty string")
    path = resolve_path(render_set_path, path_value)
    if schema_only:
        return path
    if not path.is_file():
        errors.append(f"{label}.path does not exist: {path}")
        return path
    actual_size = path.stat().st_size
    if isinstance(expected_size, int) and not isinstance(expected_size, bool):
        if actual_size != expected_size:
            errors.append(
                f"{label}.byte_size mismatch: manifest={expected_size}, actual={actual_size}"
            )
    if isinstance(expected_sha, str) and len(expected_sha) == 64:
        actual_sha = sha256_file(path)
        if actual_sha != expected_sha:
            errors.append(
                f"{label}.sha256 mismatch: manifest={expected_sha}, actual={actual_sha}"
            )
    return path


def validate_render_set(
    corpus_path: Path,
    comparison_set_id: str,
    *,
    schema_only: bool = False,
) -> tuple[list[str], dict[str, Any] | None, list[dict[str, Any]]]:
    errors: list[str] = []
    corpus_path = corpus_path.resolve()
    validator = load_module(VALIDATOR_PATH, "color_corpus_validator_for_runner")
    errors.extend(
        f"corpus: {error}"
        for error in validator.validate_manifest(corpus_path, schema_only=schema_only)
    )
    try:
        corpus = load_json(corpus_path)
    except (OSError, json.JSONDecodeError) as exc:
        return ([f"unable to read corpus manifest: {exc}"], None, [])
    if not isinstance(corpus, dict):
        return (["corpus manifest root must be an object"], None, [])
    comparison_set = next(
        (
            value
            for value in corpus.get("comparison_sets", [])
            if isinstance(value, dict)
            and value.get("comparison_set_id") == comparison_set_id
        ),
        None,
    )
    if comparison_set is None:
        return (errors + [f"comparison set not found: {comparison_set_id}"], None, [])
    artifact_path_value = comparison_set.get("artifact_manifest_path")
    if not isinstance(artifact_path_value, str) or not artifact_path_value:
        return (errors + ["comparison set artifact_manifest_path is invalid"], None, [])
    render_set_path = resolve_path(corpus_path, artifact_path_value)
    try:
        render_set = load_json(render_set_path)
    except (OSError, json.JSONDecodeError) as exc:
        return (errors + [f"unable to read render set: {exc}"], None, [])
    if not isinstance(render_set, dict):
        return (errors + ["render set root must be an object"], None, [])
    check_keys(render_set, ROOT_KEYS, ROOT_KEYS, "render_set", errors)
    if render_set.get("schema") != RENDER_SET_SCHEMA:
        errors.append(f"render_set.schema must be {RENDER_SET_SCHEMA}")
    if render_set.get("corpus_id") != corpus.get("corpus_id"):
        errors.append("render_set.corpus_id does not match the corpus")
    if render_set.get("corpus_version") != corpus.get("corpus_version"):
        errors.append("render_set.corpus_version does not match the corpus")
    if render_set.get("comparison_set_id") != comparison_set_id:
        errors.append("render_set.comparison_set_id does not match")
    candidate_model = corpus.get("candidate_rendering_model")
    if render_set.get("candidate_rendering_model") != candidate_model:
        errors.append("render_set.candidate_rendering_model does not match the corpus")

    renderer = render_set.get("reference_renderer")
    renderer_keys = {
        "rendering_model",
        "organization",
        "tool",
        "version",
        "independent_of_photo_editor",
    }
    if not isinstance(renderer, dict):
        errors.append("render_set.reference_renderer must be an object")
        renderer = {}
    else:
        check_keys(renderer, renderer_keys, renderer_keys, "render_set.reference_renderer", errors)
    for key in ("rendering_model", "organization", "tool", "version"):
        if not isinstance(renderer.get(key), str) or not renderer.get(key):
            errors.append(f"render_set.reference_renderer.{key} must be a non-empty string")
    if renderer.get("independent_of_photo_editor") is not True:
        errors.append("render_set.reference_renderer.independent_of_photo_editor must be true")

    assets = {
        asset["asset_id"]: asset
        for asset in corpus.get("assets", [])
        if isinstance(asset, dict) and isinstance(asset.get("asset_id"), str)
    }
    captures = {
        asset_id: asset
        for asset_id, asset in assets.items()
        if asset.get("kind") == "capture"
    }
    expected_capture_ids = set(captures)
    comparison_capture_ids = set(comparison_set.get("capture_ids", []))
    if comparison_capture_ids != expected_capture_ids:
        errors.append("comparison set must contain every corpus capture for a complete run")
    expected_pairs = {
        (capture_id, execution_path)
        for capture_id in expected_capture_ids
        for execution_path in REQUIRED_PATHS
    }

    artifacts = render_set.get("artifacts")
    if not isinstance(artifacts, list) or not artifacts:
        errors.append("render_set.artifacts must be a non-empty array")
        artifacts = []
    artifact_count = render_set.get("artifact_count")
    if not isinstance(artifact_count, int) or isinstance(artifact_count, bool):
        errors.append("render_set.artifact_count must be an integer")
    elif artifact_count != len(artifacts):
        errors.append(
            f"render_set.artifact_count mismatch: manifest={artifact_count}, actual={len(artifacts)}"
        )

    jobs: list[dict[str, Any]] = []
    seen_pairs: set[tuple[str, str]] = set()
    for index, artifact in enumerate(artifacts):
        label = f"render_set.artifacts[{index}]"
        if not isinstance(artifact, dict):
            errors.append(f"{label} must be an object")
            continue
        check_keys(artifact, ARTIFACT_KEYS, ARTIFACT_KEYS, label, errors)
        capture_id = artifact.get("capture_id")
        execution_path = artifact.get("execution_path")
        if not isinstance(capture_id, str) or capture_id not in captures:
            errors.append(f"{label}.capture_id is unknown")
            continue
        if execution_path not in REQUIRED_PATHS:
            errors.append(f"{label}.execution_path is invalid")
            continue
        pair = (capture_id, execution_path)
        if pair in seen_pairs:
            errors.append(f"duplicate render artifact for {capture_id} / {execution_path}")
        seen_pairs.add(pair)
        capture = captures[capture_id]
        reference_asset_id = artifact.get("reference_asset_id")
        if reference_asset_id not in capture.get("reference_ids", []):
            errors.append(f"{label}.reference_asset_id is not linked to the capture")
        reference_asset = assets.get(reference_asset_id)
        if not isinstance(reference_asset, dict) or reference_asset.get("kind") != "reference":
            errors.append(f"{label}.reference_asset_id does not identify reference evidence")
        output_profile_id = artifact.get("output_profile_id")
        if output_profile_id not in capture.get("profile_ids", []):
            errors.append(f"{label}.output_profile_id is not linked to the capture")
        output_profile = assets.get(output_profile_id)
        if not isinstance(output_profile, dict) or output_profile.get("kind") != "profile":
            errors.append(f"{label}.output_profile_id does not identify a profile")
        candidate_path = verify_file_record(
            render_set_path,
            artifact.get("candidate"),
            f"{label}.candidate",
            schema_only,
            errors,
        )
        reference_path = verify_file_record(
            render_set_path,
            artifact.get("reference"),
            f"{label}.reference",
            schema_only,
            errors,
        )
        jobs.append(
            {
                "capture_id": capture_id,
                "execution_path": execution_path,
                "reference_asset_id": reference_asset_id,
                "output_profile_id": output_profile_id,
                "candidate_path": candidate_path,
                "reference_path": reference_path,
            }
        )

    for capture_id, execution_path in sorted(expected_pairs - seen_pairs):
        errors.append(f"missing render artifact for {capture_id} / {execution_path}")
    for capture_id, execution_path in sorted(seen_pairs - expected_pairs):
        errors.append(f"unexpected render artifact for {capture_id} / {execution_path}")

    context = {
        "corpus": corpus,
        "corpus_path": corpus_path,
        "comparison_set": comparison_set,
        "render_set": render_set,
        "render_set_path": render_set_path,
        "assets": assets,
        "reference_rendering_model": renderer.get("rendering_model", ""),
    }
    return (errors, context, jobs)


def default_command_runner(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, capture_output=True, check=False, text=True)


def execute_gate(
    corpus_path: Path,
    comparison_set_id: str,
    comparison_tool: Path,
    output_dir: Path,
    *,
    validate_only: bool = False,
    command_runner: Callable[[list[str]], subprocess.CompletedProcess[str]] = default_command_runner,
) -> tuple[int, dict[str, Any]]:
    corpus_path = corpus_path.resolve()
    comparison_tool = comparison_tool.resolve()
    output_dir = output_dir.resolve()
    if output_dir.exists():
        return (1, {"schema": RUN_SCHEMA, "passed": False, "errors": [f"output exists: {output_dir}"]})
    output_dir.mkdir(parents=True)
    preflight_errors, context, jobs = validate_render_set(
        corpus_path, comparison_set_id, schema_only=False
    )
    if not validate_only and (
        not comparison_tool.is_file() or not os.access(comparison_tool, os.X_OK)
    ):
        preflight_errors.append(f"comparison tool is not executable: {comparison_tool}")
    if preflight_errors or context is None:
        summary = {
            "schema": RUN_SCHEMA,
            "passed": False,
            "stage": "preflight",
            "errors": preflight_errors,
            "comparison_set_id": comparison_set_id,
            "invocations": [],
        }
        write_json(output_dir / "run-summary.json", summary)
        return (1, summary)
    if validate_only:
        summary = {
            "schema": RUN_SCHEMA,
            "passed": True,
            "stage": "validated",
            "errors": [],
            "comparison_set_id": comparison_set_id,
            "job_count": len(jobs),
            "invocations": [],
        }
        write_json(output_dir / "run-summary.json", summary)
        return (0, summary)

    reports_dir = output_dir / "reports"
    heatmaps_dir = output_dir / "heatmaps"
    reports_dir.mkdir()
    heatmaps_dir.mkdir()
    assets = context["assets"]
    invocations: list[dict[str, Any]] = []
    any_tool_failure = False
    for job in sorted(jobs, key=lambda item: (item["capture_id"], item["execution_path"])):
        capture = assets[job["capture_id"]]
        recipe = assets[capture["recipe_id"]]
        profile = assets[job["output_profile_id"]]
        stem = f"{job['capture_id']}--{job['execution_path']}"
        report_path = reports_dir / f"{stem}.json"
        heatmap_path = heatmaps_dir / f"{stem}.png"
        command = [
            str(comparison_tool),
            "--candidate",
            str(job["candidate_path"]),
            "--reference",
            str(job["reference_path"]),
            "--report",
            str(report_path),
            "--heatmap",
            str(heatmap_path),
            "--corpus-manifest",
            str(corpus_path),
            "--capture-id",
            job["capture_id"],
            "--execution-path",
            job["execution_path"],
            "--candidate-rendering-model",
            context["corpus"]["candidate_rendering_model"],
            "--reference-rendering-model",
            context["reference_rendering_model"],
            "--recipe-sha256",
            recipe["sha256"],
            "--output-profile-sha256",
            profile["sha256"],
        ]
        try:
            completed = command_runner(command)
            return_code = completed.returncode
            stdout = completed.stdout[-65536:]
            stderr = completed.stderr[-65536:]
        except OSError as exc:
            return_code = 1
            stdout = ""
            stderr = str(exc)
        any_tool_failure = any_tool_failure or return_code != 0
        invocations.append(
            {
                "capture_id": job["capture_id"],
                "execution_path": job["execution_path"],
                "return_code": return_code,
                "report_path": str(report_path),
                "heatmap_path": str(heatmap_path),
                "stdout": stdout,
                "stderr": stderr,
            }
        )

    aggregator = load_module(AGGREGATOR_PATH, "color_corpus_aggregator_for_runner")
    aggregate_path = output_dir / "aggregate.json"
    aggregate_errors, aggregate_summary = aggregator.aggregate(
        corpus_path, reports_dir, aggregate_path
    )
    if aggregate_summary:
        write_json(aggregate_path, aggregate_summary)
    errors = list(aggregate_errors)
    if any_tool_failure:
        errors.append("one or more native comparisons failed")
    summary = {
        "schema": RUN_SCHEMA,
        "passed": not errors,
        "stage": "complete",
        "errors": errors,
        "comparison_set_id": comparison_set_id,
        "render_set_path": str(context["render_set_path"]),
        "render_set_sha256": sha256_file(context["render_set_path"]),
        "aggregate_path": str(aggregate_path),
        "aggregate_sha256": sha256_file(aggregate_path) if aggregate_path.is_file() else None,
        "job_count": len(jobs),
        "invocations": invocations,
    }
    write_json(output_dir / "run-summary.json", summary)
    return (0 if not errors else 1, summary)


def main() -> int:
    args = parse_args()
    code, summary = execute_gate(
        args.manifest,
        args.comparison_set_id,
        args.comparison_tool,
        args.output_dir,
        validate_only=args.validate_only,
    )
    if code == 0:
        print(
            f"color corpus run {summary['stage']}: "
            f"{summary.get('job_count', 0)} immutable comparisons"
        )
    else:
        for error in summary.get("errors", []):
            print(error, file=sys.stderr)
    return code


if __name__ == "__main__":
    raise SystemExit(main())
