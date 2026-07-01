#!/usr/bin/env python3
"""Validate a preview release proof JSON report."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


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
            "Validate that a run-preview-release-proof.py JSON report is "
            "release-eligible and not a local smoke proof."
        )
    )
    parser.add_argument("report", type=Path, help="Release proof JSON report path.")
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


def require_command_passed(commands: dict[str, Any], key: str) -> tuple[dict[str, Any], list[str]]:
    command = require_mapping(commands.get(key), f"report.commands.{key}")
    require_value(command, "exit_code", 0, f"report.commands.{key}")
    raw_args = command.get("command")
    require(
        isinstance(raw_args, list) and all(isinstance(argument, str) for argument in raw_args),
        f"report.commands.{key}.command must be a list of strings",
    )
    return require_mapping(command.get("markers"), f"report.commands.{key}.markers"), raw_args


def require_nonempty_string(mapping: dict[str, Any], key: str, label: str) -> str:
    actual = mapping.get(key)
    require(
        isinstance(actual, str) and bool(actual.strip()),
        f"{label}.{key} must be a nonempty string",
    )
    return actual


def optional_resolved_path_string(mapping: dict[str, Any], key: str) -> str | None:
    actual = mapping.get(key)
    if actual is None:
        return None
    require(
        isinstance(actual, str) and bool(actual.strip()),
        f"report.inputs.{key} must be null or a nonempty string",
    )
    return str(Path(actual).expanduser().resolve())


def require_equal(actual: Any, expected: Any, label: str) -> None:
    require(actual == expected, f"{label} is {actual!r}; expected {expected!r}")


def require_existing_jsonl(path_value: str, label: str) -> Path:
    path = Path(path_value).expanduser().resolve()
    require(path.is_file(), f"{label} does not exist: {path}")
    require(path.suffix == ".jsonl", f"{label} is not a JSONL file: {path}")
    return path


def require_evidence_log_matches_input(report: dict[str, Any], evidence_log: Path) -> None:
    inputs = require_mapping(report.get("inputs"), "report.inputs")
    desktop_input_value = require_nonempty_string(
        inputs,
        "desktop_proof_input",
        "report.inputs",
    )
    desktop_input = Path(desktop_input_value).expanduser().resolve()
    require(
        desktop_input.exists(),
        f"report.inputs.desktop_proof_input does not exist: {desktop_input}",
    )
    if desktop_input.is_file():
        require_equal(
            evidence_log,
            desktop_input,
            "desktop evidence log path",
        )
        return
    require(
        desktop_input.is_dir(),
        f"report.inputs.desktop_proof_input is not a file or directory: {desktop_input}",
    )
    try:
        evidence_log.relative_to(desktop_input)
    except ValueError as error:
        raise SystemExit(
            "desktop evidence log is not under report.inputs.desktop_proof_input: "
            f"{evidence_log} not under {desktop_input}"
        ) from error


def require_command_invokes_script(
    command_args: list[str],
    *,
    script: str,
    label: str,
) -> None:
    require(
        script in command_args,
        f"{label} must invoke {script}",
    )


def require_flag_value(
    command_args: list[str],
    *,
    flag: str,
    path_value: str,
    label: str,
) -> None:
    resolved = str(Path(path_value).expanduser().resolve())
    for index, argument in enumerate(command_args[:-1]):
        if argument == flag and command_args[index + 1] == resolved:
            return
    raise SystemExit(f"{label} must pass {resolved} after {flag}")


def require_positional_after_script(
    command_args: list[str],
    *,
    script: str,
    path_value: str,
    label: str,
) -> None:
    resolved = str(Path(path_value).expanduser().resolve())
    for index, argument in enumerate(command_args[:-1]):
        if argument == script and command_args[index + 1] == resolved:
            return
    raise SystemExit(f"{label} must pass {resolved} immediately after {script}")


def require_absent_flags(command_args: list[str], *, flags: set[str], label: str) -> None:
    for flag in sorted(flags):
        require(flag not in command_args, f"{label} must not include smoke flag {flag}")


def require_metal_environment_markers(
    metal_markers: dict[str, Any],
    command_markers: dict[str, Any],
) -> None:
    for marker in METAL_ENVIRONMENT_MARKERS:
        top_level_value = require_nonempty_string(metal_markers, marker, "metal")
        command_value = require_nonempty_string(
            command_markers,
            marker,
            "report.commands.metal.markers",
        )
        require_equal(
            command_value,
            top_level_value,
            f"report.commands.metal.markers.{marker}",
        )
    require_value(metal_markers, "metal_proof_host_system", "Darwin", "metal")
    require_value(metal_markers, "metal_proof_python_platform", "darwin", "metal")
    require_value(metal_markers, "metal_proof_docker_detected", "false", "metal")
    require(
        metal_markers.get("metal_proof_ci_detected") in {"true", "false"},
        "metal.metal_proof_ci_detected must be 'true' or 'false'",
    )
    require(
        metal_markers.get("metal_proof_restricted_context_possible") in {"true", "false"},
        "metal.metal_proof_restricted_context_possible must be 'true' or 'false'",
    )


def require_commands_match_inputs(
    *,
    inputs: dict[str, Any],
    preview_command_args: list[str],
    metal_command_args: list[str],
    desktop_command_args: list[str],
) -> None:
    preview_app = require_nonempty_string(inputs, "preview_app", "report.inputs")
    preview_corpus = require_nonempty_string(inputs, "preview_corpus", "report.inputs")
    color_app = require_nonempty_string(inputs, "color_app", "report.inputs")
    parity_app = require_nonempty_string(inputs, "parity_app", "report.inputs")
    output_root = require_nonempty_string(inputs, "output_root", "report.inputs")
    desktop_proof_input = require_nonempty_string(
        inputs,
        "desktop_proof_input",
        "report.inputs",
    )
    expected_source_image = optional_resolved_path_string(inputs, "expected_source_image")

    require_command_invokes_script(
        preview_command_args,
        label="report.commands.preview_pipeline.command",
        script="scripts/perf/run-preview-pipeline-gate.py",
    )
    require_flag_value(
        preview_command_args,
        flag="--app",
        path_value=preview_app,
        label="report.commands.preview_pipeline.command",
    )
    require_flag_value(
        preview_command_args,
        flag="--corpus",
        path_value=preview_corpus,
        label="report.commands.preview_pipeline.command",
    )
    require_flag_value(
        preview_command_args,
        flag="--output-root",
        path_value=output_root,
        label="report.commands.preview_pipeline.command",
    )
    require_command_invokes_script(
        metal_command_args,
        label="report.commands.metal.command",
        script="scripts/perf/run-metal-proof-gate.py",
    )
    require_flag_value(
        metal_command_args,
        flag="--color-app",
        path_value=color_app,
        label="report.commands.metal.command",
    )
    require_flag_value(
        metal_command_args,
        flag="--parity-app",
        path_value=parity_app,
        label="report.commands.metal.command",
    )
    require_flag_value(
        metal_command_args,
        flag="--output-root",
        path_value=output_root,
        label="report.commands.metal.command",
    )
    require_absent_flags(
        metal_command_args,
        flags={"--allow-unavailable"},
        label="report.commands.metal.command",
    )
    require_command_invokes_script(
        desktop_command_args,
        label="report.commands.desktop.command",
        script="scripts/perf/run-desktop-preview-proof-gate.py",
    )
    require_positional_after_script(
        desktop_command_args,
        script="scripts/perf/run-desktop-preview-proof-gate.py",
        path_value=desktop_proof_input,
        label="report.commands.desktop.command",
    )
    require_absent_flags(
        desktop_command_args,
        flags={"--allow-non-metal", "--allow-synthetic"},
        label="report.commands.desktop.command",
    )
    if expected_source_image is not None:
        require_flag_value(
            desktop_command_args,
            flag="--expected-source-image",
            path_value=expected_source_image,
            label="report.commands.desktop.command",
        )


def load_report(path: Path) -> dict[str, Any]:
    resolved = path.expanduser().resolve()
    require(resolved.is_file(), f"Report does not exist: {resolved}")
    try:
        report = json.loads(resolved.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise SystemExit(f"Invalid JSON report {resolved}: {error}") from error
    return require_mapping(report, "report")


def main() -> int:
    args = parse_args()
    report_path = args.report.expanduser().resolve()
    report = load_report(report_path)

    require_value(report, "release_eligible", True, "report")
    require_value(report, "gate", "passed", "report")
    inputs = require_mapping(report.get("inputs"), "report.inputs")

    smoke_flags = require_mapping(report.get("smoke_flags"), "report.smoke_flags")
    require_value(smoke_flags, "skip_preview_pipeline", False, "report.smoke_flags")
    require_value(smoke_flags, "allow_metal_unavailable", False, "report.smoke_flags")
    require_value(smoke_flags, "allow_desktop_smoke", False, "report.smoke_flags")

    gates = require_mapping(report.get("gates"), "report.gates")
    preview_pipeline = require_mapping(gates.get("preview_pipeline"), "report.gates.preview_pipeline")
    metal = require_mapping(gates.get("metal"), "report.gates.metal")
    desktop = require_mapping(gates.get("desktop"), "report.gates.desktop")

    require_value(preview_pipeline, "preview_pipeline_gate", "passed", "preview_pipeline")
    require_value(metal, "metal_proof_gate", "passed", "metal")
    require_value(desktop, "desktop_preview_proof_gate", "passed", "desktop")
    require_value(desktop, "desktop_preview_proof_release_eligible", "true", "desktop")
    desktop_evidence_log = require_nonempty_string(
        desktop,
        "desktop_preview_proof_evidence_log",
        "desktop",
    )

    subgate_exit_codes = require_mapping(
        report.get("subgate_exit_codes"),
        "report.subgate_exit_codes",
    )
    require_value(subgate_exit_codes, "preview_pipeline", 0, "report.subgate_exit_codes")
    require_value(subgate_exit_codes, "metal", 0, "report.subgate_exit_codes")
    require_value(subgate_exit_codes, "desktop", 0, "report.subgate_exit_codes")

    commands = require_mapping(report.get("commands"), "report.commands")
    preview_command_markers, preview_command_args = require_command_passed(
        commands,
        "preview_pipeline",
    )
    metal_command_markers, metal_command_args = require_command_passed(commands, "metal")
    desktop_command_markers, desktop_command_args = require_command_passed(commands, "desktop")
    require_value(
        preview_command_markers,
        "preview_pipeline_gate",
        "passed",
        "report.commands.preview_pipeline.markers",
    )
    require_value(
        metal_command_markers,
        "metal_proof_gate",
        "passed",
        "report.commands.metal.markers",
    )
    require_metal_environment_markers(metal, metal_command_markers)
    require_value(
        desktop_command_markers,
        "desktop_preview_proof_gate",
        "passed",
        "report.commands.desktop.markers",
    )
    require_value(
        desktop_command_markers,
        "desktop_preview_proof_release_eligible",
        "true",
        "report.commands.desktop.markers",
    )
    command_desktop_evidence_log = require_nonempty_string(
        desktop_command_markers,
        "desktop_preview_proof_evidence_log",
        "report.commands.desktop.markers",
    )
    require_equal(
        command_desktop_evidence_log,
        desktop_evidence_log,
        "report.commands.desktop.markers.desktop_preview_proof_evidence_log",
    )
    evidence_log_path = require_existing_jsonl(desktop_evidence_log, "desktop evidence log")
    require_evidence_log_matches_input(report, evidence_log_path)
    expected_source_image = optional_resolved_path_string(inputs, "expected_source_image")
    if expected_source_image is not None:
        desktop_expected_source_image = require_nonempty_string(
            desktop,
            "desktop_preview_proof_expected_source_image",
            "desktop",
        )
        command_expected_source_image = require_nonempty_string(
            desktop_command_markers,
            "desktop_preview_proof_expected_source_image",
            "report.commands.desktop.markers",
        )
        require_equal(
            str(Path(desktop_expected_source_image).expanduser().resolve()),
            expected_source_image,
            "desktop.desktop_preview_proof_expected_source_image",
        )
        require_equal(
            str(Path(command_expected_source_image).expanduser().resolve()),
            expected_source_image,
            "report.commands.desktop.markers.desktop_preview_proof_expected_source_image",
        )
    require_commands_match_inputs(
        inputs=inputs,
        preview_command_args=preview_command_args,
        metal_command_args=metal_command_args,
        desktop_command_args=desktop_command_args,
    )

    print(f"preview_release_proof_report_valid={report_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
