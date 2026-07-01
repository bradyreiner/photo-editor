#!/usr/bin/env python3
"""Validate a desktop preview proof capture manifest."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
REPORT_CHECKER = "scripts/perf/check-preview-release-proof-report.py"
METAL_ENVIRONMENT_MARKERS = (
    "metal_proof_host_system",
    "metal_proof_host_machine",
    "metal_proof_python_platform",
    "metal_proof_docker_detected",
    "metal_proof_ci_detected",
    "metal_proof_restricted_context_possible",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Validate that a run-desktop-preview-proof-capture.py manifest is a "
            "complete no-smoke release proof capture."
        )
    )
    parser.add_argument(
        "manifest_or_artifact",
        type=Path,
        help="Path to manifest.json or to the capture artifact directory.",
    )
    return parser.parse_args()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def require_mapping(value: Any, label: str) -> dict[str, Any]:
    require(isinstance(value, dict), f"{label} must be an object")
    return value


def require_value(mapping: dict[str, Any], key: str, expected: Any, label: str) -> None:
    actual = mapping.get(key)
    require(actual == expected, f"{label}.{key} is {actual!r}; expected {expected!r}")


def require_nonempty_string(mapping: dict[str, Any], key: str, label: str) -> str:
    actual = mapping.get(key)
    require(
        isinstance(actual, str) and bool(actual.strip()),
        f"{label}.{key} must be a nonempty string",
    )
    return actual


def optional_resolved_path_string(mapping: dict[str, Any], key: str, label: str) -> str | None:
    actual = mapping.get(key)
    if actual is None:
        return None
    require(
        isinstance(actual, str) and bool(actual.strip()),
        f"{label}.{key} must be null or a nonempty string",
    )
    return str(Path(actual).expanduser().resolve())


def require_command_list(mapping: dict[str, Any], key: str, label: str) -> list[str]:
    command = mapping.get(key)
    require(
        isinstance(command, list) and all(isinstance(argument, str) for argument in command),
        f"{label}.{key} must be a list of strings",
    )
    return command


def require_nonempty_string_list(mapping: dict[str, Any], key: str, label: str) -> list[str]:
    value = mapping.get(key)
    require(
        isinstance(value, list)
        and bool(value)
        and all(isinstance(item, str) and bool(item.strip()) for item in value),
        f"{label}.{key} must be a nonempty list of nonempty strings",
    )
    return value


def require_command_result(
    manifest: dict[str, Any],
    key: str,
    *,
    expected_command: list[str],
) -> dict[str, Any]:
    result = require_mapping(manifest.get(key), f"manifest.{key}")
    command = require_command_list(result, "command", f"manifest.{key}")
    require(command == expected_command, f"manifest.{key}.command does not match manifest.commands.{key}")
    require_value(result, "exit_code", 0, f"manifest.{key}")
    return require_mapping(result.get("markers"), f"manifest.{key}.markers")


def require_optional_command_result(
    manifest: dict[str, Any],
    key: str,
    *,
    expected_command: list[str],
) -> dict[str, Any] | None:
    if key not in manifest:
        return None
    return require_command_result(
        manifest,
        key,
        expected_command=expected_command,
    )


def require_flag_value(
    command: list[str],
    *,
    flag: str,
    expected: str,
    label: str,
) -> None:
    for index, argument in enumerate(command[:-1]):
        if argument == flag and command[index + 1] == expected:
            return
    raise SystemExit(f"{label} must pass {expected} after {flag}")


def require_positional_after_script(
    command: list[str],
    *,
    script: str,
    expected: str,
    label: str,
) -> None:
    for index, argument in enumerate(command[:-1]):
        if argument == script and command[index + 1] == expected:
            return
    raise SystemExit(f"{label} must pass {expected} immediately after {script}")


def require_invokes(command: list[str], *, script: str, label: str) -> None:
    require(script in command, f"{label} must invoke {script}")


def require_absent_flags(command: list[str], *, flags: set[str], label: str) -> None:
    for flag in sorted(flags):
        require(flag not in command, f"{label} must not include smoke flag {flag}")


def require_metal_environment_markers(markers: dict[str, Any], label: str) -> None:
    for marker in METAL_ENVIRONMENT_MARKERS:
        require_nonempty_string(markers, marker, label)
    require_value(markers, "metal_proof_host_system", "Darwin", label)
    require_value(markers, "metal_proof_python_platform", "darwin", label)
    require_value(markers, "metal_proof_docker_detected", "false", label)
    require(
        markers.get("metal_proof_ci_detected") in {"true", "false"},
        f"{label}.metal_proof_ci_detected must be 'true' or 'false'",
    )
    require(
        markers.get("metal_proof_restricted_context_possible") in {"true", "false"},
        f"{label}.metal_proof_restricted_context_possible must be 'true' or 'false'",
    )


def manifest_path_from_arg(path: Path) -> Path:
    resolved = path.expanduser().resolve()
    if resolved.is_dir():
        resolved = resolved / "manifest.json"
    require(resolved.is_file(), f"Capture manifest does not exist: {resolved}")
    return resolved


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise SystemExit(f"Invalid JSON manifest {path}: {error}") from error
    return require_mapping(manifest, "manifest")


def load_release_report(path: Path) -> dict[str, Any]:
    try:
        report = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise SystemExit(f"Invalid JSON release report {path}: {error}") from error
    return require_mapping(report, "release report")


def require_release_report_valid(report_path: Path) -> None:
    result = subprocess.run(
        [sys.executable, "-B", REPORT_CHECKER, str(report_path)],
        cwd=REPO_ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if result.returncode != 0:
        raise SystemExit(
            f"Release report validation failed for {report_path}:\n{result.stdout}"
        )


def require_path_under(path: Path, parent: Path, label: str) -> None:
    try:
        path.relative_to(parent)
    except ValueError as error:
        raise SystemExit(f"{label} is not under {parent}: {path}") from error


def require_release_report_matches_manifest(
    report_path: Path,
    *,
    preview_app: str,
    preview_corpus: str,
    color_app: str,
    parity_app: str,
    expected_source_image: str | None,
    artifact_dir: Path,
    run_dir: Path,
) -> None:
    report = load_release_report(report_path)
    inputs = require_mapping(report.get("inputs"), "release report.inputs")
    expected_paths = {
        "preview_app": preview_app,
        "preview_corpus": preview_corpus,
        "color_app": color_app,
        "parity_app": parity_app,
        "desktop_proof_input": str(run_dir),
        "output_root": str(artifact_dir),
    }
    for key, expected in expected_paths.items():
        actual = str(Path(require_nonempty_string(inputs, key, "release report.inputs")).resolve())
        require(
            actual == expected,
            f"release report.inputs.{key} is {actual!r}; expected manifest value {expected!r}",
        )
    actual_expected_source = optional_resolved_path_string(
        inputs,
        "expected_source_image",
        "release report.inputs",
    )
    require(
        actual_expected_source == expected_source_image,
        (
            "release report.inputs.expected_source_image is "
            f"{actual_expected_source!r}; expected manifest value {expected_source_image!r}"
        ),
    )


def validate_manifest(manifest_path: Path, manifest: dict[str, Any]) -> Path:
    require_value(manifest, "status", "release_proof_passed", "manifest")
    require_value(manifest, "app_exit_code", 0, "manifest")
    require_nonempty_string_list(manifest, "proof_steps", "manifest")

    artifact_dir = Path(require_nonempty_string(manifest, "artifact_dir", "manifest")).resolve()
    run_dir = Path(require_nonempty_string(manifest, "run_dir", "manifest")).resolve()
    require(manifest_path == artifact_dir / "manifest.json", "manifest path must equal artifact_dir/manifest.json")
    require(artifact_dir.is_dir(), f"manifest.artifact_dir does not exist: {artifact_dir}")
    require(run_dir.is_dir(), f"manifest.run_dir does not exist: {run_dir}")
    require_path_under(run_dir, artifact_dir, "manifest.run_dir")

    options = require_mapping(manifest.get("options"), "manifest.options")
    require_value(options, "allow_non_metal", False, "manifest.options")
    require_value(options, "allow_synthetic", False, "manifest.options")
    require_value(options, "no_validate", False, "manifest.options")
    require_value(options, "print_only", False, "manifest.options")
    require_value(options, "run_release_proof", True, "manifest.options")
    require_value(options, "skip_metal_preflight", False, "manifest.options")

    inputs = require_mapping(manifest.get("inputs"), "manifest.inputs")
    require_nonempty_string(inputs, "app", "manifest.inputs")
    preview_app = str(Path(require_nonempty_string(inputs, "preview_app", "manifest.inputs")).resolve())
    preview_corpus = str(Path(require_nonempty_string(inputs, "preview_corpus", "manifest.inputs")).resolve())
    color_app = str(Path(require_nonempty_string(inputs, "color_app", "manifest.inputs")).resolve())
    parity_app = str(Path(require_nonempty_string(inputs, "parity_app", "manifest.inputs")).resolve())
    output_root = str(Path(require_nonempty_string(inputs, "output_root", "manifest.inputs")).resolve())
    expected_source_image = optional_resolved_path_string(
        inputs,
        "expected_source_image",
        "manifest.inputs",
    )

    commands = require_mapping(manifest.get("commands"), "manifest.commands")
    capture_manifest_validation_command = require_command_list(
        commands,
        "capture_manifest_validation",
        "manifest.commands",
    )
    metal_command = require_command_list(commands, "metal_preflight", "manifest.commands")
    desktop_command = require_command_list(commands, "desktop_validation", "manifest.commands")
    release_command = require_command_list(commands, "release_proof", "manifest.commands")

    require_invokes(
        capture_manifest_validation_command,
        script="scripts/perf/check-desktop-preview-proof-capture-manifest.py",
        label="manifest.commands.capture_manifest_validation",
    )
    require_positional_after_script(
        capture_manifest_validation_command,
        script="scripts/perf/check-desktop-preview-proof-capture-manifest.py",
        expected=str(artifact_dir),
        label="manifest.commands.capture_manifest_validation",
    )

    require_invokes(
        metal_command,
        script="scripts/perf/run-metal-proof-gate.py",
        label="manifest.commands.metal_preflight",
    )
    require_flag_value(metal_command, flag="--color-app", expected=color_app, label="manifest.commands.metal_preflight")
    require_flag_value(metal_command, flag="--parity-app", expected=parity_app, label="manifest.commands.metal_preflight")
    require_flag_value(metal_command, flag="--output-root", expected=output_root, label="manifest.commands.metal_preflight")
    require_absent_flags(
        metal_command,
        flags={"--allow-unavailable"},
        label="manifest.commands.metal_preflight",
    )

    require_invokes(
        desktop_command,
        script="scripts/perf/run-desktop-preview-proof-gate.py",
        label="manifest.commands.desktop_validation",
    )
    require_positional_after_script(
        desktop_command,
        script="scripts/perf/run-desktop-preview-proof-gate.py",
        expected=str(run_dir),
        label="manifest.commands.desktop_validation",
    )
    require_absent_flags(
        desktop_command,
        flags={"--allow-non-metal", "--allow-synthetic"},
        label="manifest.commands.desktop_validation",
    )
    if expected_source_image is not None:
        require_flag_value(
            desktop_command,
            flag="--expected-source-image",
            expected=expected_source_image,
            label="manifest.commands.desktop_validation",
        )

    require_invokes(
        release_command,
        script="scripts/perf/run-preview-release-proof.py",
        label="manifest.commands.release_proof",
    )
    require_flag_value(release_command, flag="--preview-app", expected=preview_app, label="manifest.commands.release_proof")
    require_flag_value(release_command, flag="--preview-corpus", expected=preview_corpus, label="manifest.commands.release_proof")
    require_flag_value(release_command, flag="--color-app", expected=color_app, label="manifest.commands.release_proof")
    require_flag_value(release_command, flag="--parity-app", expected=parity_app, label="manifest.commands.release_proof")
    require_flag_value(release_command, flag="--desktop-proof-input", expected=str(run_dir), label="manifest.commands.release_proof")
    require_flag_value(release_command, flag="--output-root", expected=str(artifact_dir), label="manifest.commands.release_proof")
    if expected_source_image is not None:
        require_flag_value(
            release_command,
            flag="--expected-source-image",
            expected=expected_source_image,
            label="manifest.commands.release_proof",
        )
    require_absent_flags(
        release_command,
        flags={"--allow-desktop-smoke", "--allow-metal-unavailable", "--skip-preview-pipeline"},
        label="manifest.commands.release_proof",
    )

    metal_markers = require_command_result(
        manifest,
        "metal_preflight",
        expected_command=metal_command,
    )
    desktop_markers = require_command_result(
        manifest,
        "desktop_validation",
        expected_command=desktop_command,
    )
    release_markers = require_command_result(
        manifest,
        "release_proof",
        expected_command=release_command,
    )
    capture_manifest_validation_markers = require_optional_command_result(
        manifest,
        "capture_manifest_validation",
        expected_command=capture_manifest_validation_command,
    )

    require_value(metal_markers, "metal_proof_gate", "passed", "manifest.metal_preflight.markers")
    require_metal_environment_markers(metal_markers, "manifest.metal_preflight.markers")
    require_value(desktop_markers, "desktop_preview_proof_gate", "passed", "manifest.desktop_validation.markers")
    require_value(
        desktop_markers,
        "desktop_preview_proof_release_eligible",
        "true",
        "manifest.desktop_validation.markers",
    )
    if expected_source_image is not None:
        desktop_expected_source = require_nonempty_string(
            desktop_markers,
            "desktop_preview_proof_expected_source_image",
            "manifest.desktop_validation.markers",
        )
        require(
            str(Path(desktop_expected_source).expanduser().resolve()) == expected_source_image,
            (
                "manifest.desktop_validation.markers."
                "desktop_preview_proof_expected_source_image must match manifest input"
            ),
        )
    evidence_log = Path(
        require_nonempty_string(
            desktop_markers,
            "desktop_preview_proof_evidence_log",
            "manifest.desktop_validation.markers",
        )
    ).resolve()
    require(evidence_log.is_file(), f"Desktop evidence log does not exist: {evidence_log}")
    require(evidence_log.suffix == ".jsonl", f"Desktop evidence log is not JSONL: {evidence_log}")
    require_path_under(evidence_log, run_dir, "Desktop evidence log")

    require_value(
        release_markers,
        "preview_release_proof_gate",
        "passed",
        "manifest.release_proof.markers",
    )
    require_value(
        release_markers,
        "preview_release_proof_release_eligible",
        "true",
        "manifest.release_proof.markers",
    )
    release_report_marker = require_nonempty_string(
        release_markers,
        "preview_release_proof_report",
        "manifest.release_proof.markers",
    )
    top_level_release_report = require_nonempty_string(
        manifest,
        "release_report",
        "manifest",
    )
    require(
        Path(release_report_marker).resolve() == Path(top_level_release_report).resolve(),
        "manifest.release_report must match manifest.release_proof.markers.preview_release_proof_report",
    )
    release_report = Path(top_level_release_report).resolve()
    require(release_report.is_file(), f"Release report does not exist: {release_report}")
    require_path_under(release_report, artifact_dir, "Release report")
    require_release_report_matches_manifest(
        release_report,
        preview_app=preview_app,
        preview_corpus=preview_corpus,
        color_app=color_app,
        parity_app=parity_app,
        expected_source_image=expected_source_image,
        artifact_dir=artifact_dir,
        run_dir=run_dir,
    )

    if capture_manifest_validation_markers is not None:
        capture_manifest_marker = require_nonempty_string(
            capture_manifest_validation_markers,
            "desktop_preview_proof_capture_manifest_valid",
            "manifest.capture_manifest_validation.markers",
        )
        require(
            Path(capture_manifest_marker).resolve() == manifest_path,
            (
                "manifest.capture_manifest_validation.markers."
                "desktop_preview_proof_capture_manifest_valid must match this manifest"
            ),
        )
    return release_report


def main() -> int:
    args = parse_args()
    manifest_path = manifest_path_from_arg(args.manifest_or_artifact)
    manifest = load_manifest(manifest_path)
    release_report = validate_manifest(manifest_path, manifest)
    require_release_report_valid(release_report)
    print(f"desktop_preview_proof_capture_manifest_valid={manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
