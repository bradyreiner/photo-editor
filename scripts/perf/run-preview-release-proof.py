#!/usr/bin/env python3
"""Run the full preview rendering release proof bundle."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_PREVIEW_APP = REPO_ROOT / "build/dev-macos-debug/tests/preview_benchmark_driver"
DEFAULT_PREVIEW_CORPUS = REPO_ROOT / "perf-corpora/preview-synthetic-v1/manifest.json"
DEFAULT_COLOR_APP = REPO_ROOT / "build/dev-macos-debug/tests/color_adjustment_benchmark_driver"
DEFAULT_PARITY_APP = REPO_ROOT / "build/dev-macos-debug/tests/render_parity_test"
DEFAULT_OUTPUT_ROOT = REPO_ROOT / "perf-artifacts"
METAL_ENVIRONMENT_MARKERS = (
    "metal_proof_host_system",
    "metal_proof_host_machine",
    "metal_proof_python_platform",
    "metal_proof_docker_detected",
    "metal_proof_ci_detected",
    "metal_proof_restricted_context_possible",
)


def synthetic_result(command: list[str], returncode: int, output: str) -> subprocess.CompletedProcess[str]:
    return subprocess.CompletedProcess(command, returncode, output, "")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run the release proof for the max-resolution preview rendering work. "
            "A release-eligible pass requires the synthetic statistical pipeline "
            "gate, hardware Metal proof, and a real desktop observability proof."
        )
    )
    parser.add_argument("--preview-app", type=Path, default=DEFAULT_PREVIEW_APP)
    parser.add_argument("--preview-corpus", type=Path, default=DEFAULT_PREVIEW_CORPUS)
    parser.add_argument("--color-app", type=Path, default=DEFAULT_COLOR_APP)
    parser.add_argument("--parity-app", type=Path, default=DEFAULT_PARITY_APP)
    parser.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument(
        "--report",
        type=Path,
        help="Write a machine-readable JSON proof report. Defaults under --output-root.",
    )
    parser.add_argument(
        "--desktop-proof-input",
        type=Path,
        help="Real desktop observability run directory, artifact directory, or desktop.jsonl.",
    )
    parser.add_argument(
        "--expected-source-image",
        type=Path,
        help="Require the desktop proof gate to observe this resolved source image path.",
    )
    parser.add_argument("--preview-timeout", type=int, default=180)
    parser.add_argument("--preview-timeout-ms", type=int, default=30000)
    parser.add_argument("--metal-iterations", type=int, default=30)
    parser.add_argument("--metal-timeout", type=int, default=120)
    parser.add_argument(
        "--skip-preview-pipeline",
        action="store_true",
        help="Skip the synthetic statistical pipeline gate. Local smoke only.",
    )
    parser.add_argument(
        "--allow-metal-unavailable",
        action="store_true",
        help="Allow Metal proof to skip when GPU backend is unavailable. Local smoke only.",
    )
    parser.add_argument(
        "--allow-desktop-smoke",
        action="store_true",
        help="Allow synthetic/non-Metal desktop proof input. Local smoke only.",
    )
    return parser.parse_args()


def require_positive(value: int, label: str) -> None:
    if value <= 0:
        raise SystemExit(f"{label} must be positive")


def existing_path(path: Path) -> Path | None:
    resolved = path.expanduser().resolve()
    return resolved if resolved.exists() else None


def missing_input_result(
    *,
    gate_key: str,
    error_key: str,
    label: str,
    path: Path,
) -> subprocess.CompletedProcess[str]:
    resolved = path.expanduser().resolve()
    return synthetic_result(
        [f"{label} missing", str(resolved)],
        2,
        f"{gate_key}=failed\n{error_key}={label}_missing\n{error_key}_path={resolved}\n",
    )


def run_command(command: list[str]) -> subprocess.CompletedProcess[str]:
    print("+ " + " ".join(command), flush=True)
    result = subprocess.run(
        command,
        cwd=REPO_ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if result.stdout:
        print(result.stdout, end="" if result.stdout.endswith("\n") else "\n")
    return result


def parse_key_values(output: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in output.splitlines():
        key, separator, value = line.partition("=")
        if separator and key.strip():
            values[key.strip()] = value.strip()
    return values


def release_eligible_from_outputs(
    *,
    skip_preview_pipeline: bool,
    allow_metal_unavailable: bool,
    allow_desktop_smoke: bool,
    preview_values: dict[str, str],
    metal_values: dict[str, str],
    desktop_values: dict[str, str],
) -> bool:
    metal_environment_valid = (
        metal_values.get("metal_proof_host_system") == "Darwin"
        and bool(metal_values.get("metal_proof_host_machine", "").strip())
        and metal_values.get("metal_proof_python_platform") == "darwin"
        and metal_values.get("metal_proof_docker_detected") == "false"
        and metal_values.get("metal_proof_ci_detected") in {"true", "false"}
        and metal_values.get("metal_proof_restricted_context_possible") in {"true", "false"}
        and all(bool(metal_values.get(marker, "").strip()) for marker in METAL_ENVIRONMENT_MARKERS)
    )
    return (
        not skip_preview_pipeline
        and not allow_metal_unavailable
        and not allow_desktop_smoke
        and preview_values.get("preview_pipeline_gate") == "passed"
        and metal_values.get("metal_proof_gate") == "passed"
        and metal_environment_valid
        and desktop_values.get("desktop_preview_proof_gate") == "passed"
        and bool(desktop_values.get("desktop_preview_proof_evidence_log", "").strip())
        and desktop_values.get("desktop_preview_proof_release_eligible") == "true"
    )


def smoke_mode_enabled(
    *,
    skip_preview_pipeline: bool,
    allow_metal_unavailable: bool,
    allow_desktop_smoke: bool,
) -> bool:
    return skip_preview_pipeline or allow_metal_unavailable or allow_desktop_smoke


def release_runner_exit_code(*, release_eligible: bool, smoke_mode: bool) -> int:
    return 0 if release_eligible or smoke_mode else 1


def should_check_release_report(*, release_eligible: bool, exit_code: int) -> bool:
    return release_eligible and exit_code == 0


def release_gate_status(
    *,
    release_eligible: bool,
    smoke_mode: bool,
    subgates_passed: bool,
) -> str:
    if not subgates_passed:
        return "failed"
    if release_eligible:
        return "passed"
    if smoke_mode:
        return "smoke_passed"
    return "failed"


def default_report_path(output_root: Path) -> Path:
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    return output_root.expanduser().resolve() / f"preview-release-proof-{timestamp}.json"


def write_report(path: Path, report: dict[str, Any]) -> None:
    resolved = path.expanduser().resolve()
    resolved.parent.mkdir(parents=True, exist_ok=True)
    resolved.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"preview_release_proof_report={resolved}")


def check_release_report(path: Path) -> None:
    result = run_command(
        [
            sys.executable,
            "-B",
            "scripts/perf/check-preview-release-proof-report.py",
            str(path.expanduser().resolve()),
        ]
    )
    if result.returncode != 0:
        raise SystemExit(result.returncode)


def run_preview_pipeline(args: argparse.Namespace) -> subprocess.CompletedProcess[str]:
    if args.skip_preview_pipeline:
        print("preview_pipeline_gate=skipped")
        return synthetic_result(["preview_pipeline_gate=skipped"], 0, "preview_pipeline_gate=skipped\n")
    preview_app = existing_path(args.preview_app)
    if preview_app is None:
        return missing_input_result(
            gate_key="preview_pipeline_gate",
            error_key="preview_pipeline_error",
            label="preview_app",
            path=args.preview_app,
        )
    preview_corpus = existing_path(args.preview_corpus)
    if preview_corpus is None:
        return missing_input_result(
            gate_key="preview_pipeline_gate",
            error_key="preview_pipeline_error",
            label="preview_corpus",
            path=args.preview_corpus,
        )
    command = [
        sys.executable,
        "-B",
        "scripts/perf/run-preview-pipeline-gate.py",
        "--app",
        str(preview_app),
        "--corpus",
        str(preview_corpus),
        "--output-root",
        str(args.output_root.expanduser().resolve()),
        "--timeout",
        str(args.preview_timeout),
        "--timeout-ms",
        str(args.preview_timeout_ms),
    ]
    return run_command(command)


def run_metal_proof(args: argparse.Namespace) -> subprocess.CompletedProcess[str]:
    color_app = existing_path(args.color_app)
    if color_app is None:
        return missing_input_result(
            gate_key="metal_proof_gate",
            error_key="metal_proof_error",
            label="color_app",
            path=args.color_app,
        )
    parity_app = existing_path(args.parity_app)
    if parity_app is None:
        return missing_input_result(
            gate_key="metal_proof_gate",
            error_key="metal_proof_error",
            label="parity_app",
            path=args.parity_app,
        )
    command = [
        sys.executable,
        "-B",
        "scripts/perf/run-metal-proof-gate.py",
        "--color-app",
        str(color_app),
        "--parity-app",
        str(parity_app),
        "--output-root",
        str(args.output_root.expanduser().resolve()),
        "--iterations",
        str(args.metal_iterations),
        "--timeout",
        str(args.metal_timeout),
    ]
    if args.allow_metal_unavailable:
        command.append("--allow-unavailable")
    return run_command(command)


def run_desktop_proof(args: argparse.Namespace) -> subprocess.CompletedProcess[str]:
    if args.desktop_proof_input is None:
        return synthetic_result(
            ["desktop_proof_input_missing"],
            2,
            "desktop_preview_proof_error=desktop_proof_input_missing\n",
        )
    desktop_proof_input = existing_path(args.desktop_proof_input)
    if desktop_proof_input is None:
        return missing_input_result(
            gate_key="desktop_preview_proof_gate",
            error_key="desktop_preview_proof_error",
            label="desktop_proof_input",
            path=args.desktop_proof_input,
        )
    command = [
        sys.executable,
        "-B",
        "scripts/perf/run-desktop-preview-proof-gate.py",
        str(desktop_proof_input),
    ]
    if args.expected_source_image is not None:
        command.extend(
            [
                "--expected-source-image",
                str(args.expected_source_image.expanduser().resolve()),
            ]
        )
    if args.allow_desktop_smoke:
        command.extend(["--allow-synthetic", "--allow-non-metal"])
    return run_command(command)


def result_values(result: subprocess.CompletedProcess[str]) -> dict[str, str]:
    return parse_key_values(result.stdout or "")


def command_report(result: subprocess.CompletedProcess[str]) -> dict[str, Any]:
    return {
        "command": result.args,
        "exit_code": result.returncode,
        "markers": result_values(result),
    }


def main() -> int:
    args = parse_args()
    require_positive(args.preview_timeout, "--preview-timeout")
    require_positive(args.preview_timeout_ms, "--preview-timeout-ms")
    require_positive(args.metal_iterations, "--metal-iterations")
    require_positive(args.metal_timeout, "--metal-timeout")

    preview_result = run_preview_pipeline(args)
    metal_result = run_metal_proof(args)
    desktop_result = run_desktop_proof(args)

    preview_values = result_values(preview_result)
    metal_values = result_values(metal_result)
    desktop_values = result_values(desktop_result)
    subgate_failures = {
        "preview_pipeline": preview_result.returncode,
        "metal": metal_result.returncode,
        "desktop": desktop_result.returncode,
    }
    subgates_passed = all(exit_code == 0 for exit_code in subgate_failures.values())
    release_eligible = release_eligible_from_outputs(
        skip_preview_pipeline=args.skip_preview_pipeline,
        allow_metal_unavailable=args.allow_metal_unavailable,
        allow_desktop_smoke=args.allow_desktop_smoke,
        preview_values=preview_values,
        metal_values=metal_values,
        desktop_values=desktop_values,
    ) and subgates_passed
    smoke_mode = smoke_mode_enabled(
        skip_preview_pipeline=args.skip_preview_pipeline,
        allow_metal_unavailable=args.allow_metal_unavailable,
        allow_desktop_smoke=args.allow_desktop_smoke,
    )
    gate_status = release_gate_status(
        release_eligible=release_eligible,
        smoke_mode=smoke_mode,
        subgates_passed=subgates_passed,
    )
    print(f"preview_release_proof_release_eligible={'true' if release_eligible else 'false'}")
    print(f"preview_release_proof_gate={gate_status}")
    if not release_eligible:
        print(
            "preview_release_proof_note=smoke flags, skipped gates, or non-release "
            "desktop/Metal evidence prevent completion"
        )
    report_path = (
        args.report.expanduser().resolve()
        if args.report is not None
        else default_report_path(args.output_root)
    )
    write_report(
        report_path,
        {
            "generated_at": datetime.now(timezone.utc).isoformat(),
            "release_eligible": release_eligible,
            "gate": gate_status,
            "inputs": {
                "preview_app": str(args.preview_app.expanduser().resolve()),
                "preview_corpus": str(args.preview_corpus.expanduser().resolve()),
                "color_app": str(args.color_app.expanduser().resolve()),
                "parity_app": str(args.parity_app.expanduser().resolve()),
                "desktop_proof_input": (
                    str(args.desktop_proof_input.expanduser().resolve())
                    if args.desktop_proof_input is not None
                    else None
                ),
                "expected_source_image": (
                    str(args.expected_source_image.expanduser().resolve())
                    if args.expected_source_image is not None
                    else None
                ),
                "output_root": str(args.output_root.expanduser().resolve()),
                "preview_timeout": args.preview_timeout,
                "preview_timeout_ms": args.preview_timeout_ms,
                "metal_iterations": args.metal_iterations,
                "metal_timeout": args.metal_timeout,
            },
            "smoke_flags": {
                "skip_preview_pipeline": args.skip_preview_pipeline,
                "allow_metal_unavailable": args.allow_metal_unavailable,
                "allow_desktop_smoke": args.allow_desktop_smoke,
            },
            "gates": {
                "preview_pipeline": preview_values,
                "metal": metal_values,
                "desktop": desktop_values,
            },
            "commands": {
                "preview_pipeline": command_report(preview_result),
                "metal": command_report(metal_result),
                "desktop": command_report(desktop_result),
            },
            "subgate_exit_codes": subgate_failures,
        },
    )
    if not release_eligible and not smoke_mode:
        print("preview_release_proof_error=no-smoke release proof did not pass")
    exit_code = (
        release_runner_exit_code(release_eligible=release_eligible, smoke_mode=smoke_mode)
        if subgates_passed
        else next(exit_code for exit_code in subgate_failures.values() if exit_code != 0)
    )
    if should_check_release_report(release_eligible=release_eligible, exit_code=exit_code):
        check_release_report(report_path)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
