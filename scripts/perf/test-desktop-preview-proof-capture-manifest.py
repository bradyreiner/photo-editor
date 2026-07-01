#!/usr/bin/env python3
"""Self-test the desktop preview proof capture manifest checker."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from copy import deepcopy
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
CHECKER = REPO_ROOT / "scripts/perf/check-desktop-preview-proof-capture-manifest.py"


def metal_environment_markers() -> dict[str, str]:
    return {
        "metal_proof_host_system": "Darwin",
        "metal_proof_host_machine": "arm64",
        "metal_proof_python_platform": "darwin",
        "metal_proof_docker_detected": "false",
        "metal_proof_ci_detected": "false",
        "metal_proof_restricted_context_possible": "true",
    }


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def release_report(
    *,
    artifact_dir: Path,
    run_dir: Path,
    evidence_log: Path,
    expected_source_image: Path | None = None,
) -> dict[str, Any]:
    preview_app = str(Path("/tmp/preview_benchmark_driver").resolve())
    preview_corpus = str(Path("/tmp/manifest.json").resolve())
    color_app = str(Path("/tmp/color_adjustment_benchmark_driver").resolve())
    parity_app = str(Path("/tmp/render_parity_test").resolve())
    output_root = str(artifact_dir.resolve())
    run_dir_path = str(run_dir.resolve())
    evidence_log_path = str(evidence_log.resolve())
    report = {
        "generated_at": "2026-05-29T00:00:00+00:00",
        "release_eligible": True,
        "gate": "passed",
        "inputs": {
            "preview_app": preview_app,
            "preview_corpus": preview_corpus,
            "color_app": color_app,
            "parity_app": parity_app,
            "desktop_proof_input": run_dir_path,
            "output_root": output_root,
            "preview_timeout": 180,
            "preview_timeout_ms": 30000,
            "metal_iterations": 30,
            "metal_timeout": 120,
        },
        "smoke_flags": {
            "skip_preview_pipeline": False,
            "allow_metal_unavailable": False,
            "allow_desktop_smoke": False,
        },
        "gates": {
            "preview_pipeline": {
                "preview_pipeline_gate": "passed",
            },
            "metal": {
                "metal_proof_gate": "passed",
                **metal_environment_markers(),
            },
            "desktop": {
                "desktop_preview_proof_gate": "passed",
                "desktop_preview_proof_evidence_log": evidence_log_path,
                "desktop_preview_proof_release_eligible": "true",
            },
        },
        "subgate_exit_codes": {
            "preview_pipeline": 0,
            "metal": 0,
            "desktop": 0,
        },
        "commands": {
            "preview_pipeline": {
                "command": [
                    "python3",
                    "-B",
                    "scripts/perf/run-preview-pipeline-gate.py",
                    "--app",
                    preview_app,
                    "--corpus",
                    preview_corpus,
                    "--output-root",
                    output_root,
                    "--timeout",
                    "180",
                    "--timeout-ms",
                    "30000",
                ],
                "exit_code": 0,
                "markers": {
                    "preview_pipeline_gate": "passed",
                },
            },
            "metal": {
                "command": [
                    "python3",
                    "-B",
                    "scripts/perf/run-metal-proof-gate.py",
                    "--color-app",
                    color_app,
                    "--parity-app",
                    parity_app,
                    "--output-root",
                    output_root,
                    "--iterations",
                    "30",
                    "--timeout",
                    "120",
                ],
                "exit_code": 0,
                "markers": {
                    "metal_proof_gate": "passed",
                    **metal_environment_markers(),
                },
            },
            "desktop": {
                "command": [
                    "python3",
                    "-B",
                    "scripts/perf/run-desktop-preview-proof-gate.py",
                    run_dir_path,
                ],
                "exit_code": 0,
                "markers": {
                    "desktop_preview_proof_gate": "passed",
                    "desktop_preview_proof_evidence_log": evidence_log_path,
                    "desktop_preview_proof_release_eligible": "true",
                },
            },
        },
    }
    if expected_source_image is not None:
        expected_source = str(expected_source_image.resolve())
        report["inputs"]["expected_source_image"] = expected_source
        report["gates"]["desktop"]["desktop_preview_proof_expected_source_image"] = expected_source
        report["commands"]["desktop"]["command"].extend(
            ["--expected-source-image", expected_source]
        )
        report["commands"]["desktop"]["markers"][
            "desktop_preview_proof_expected_source_image"
        ] = expected_source
    return report


def capture_manifest(
    *,
    artifact_dir: Path,
    run_dir: Path,
    evidence_log: Path,
    report_path: Path,
    expected_source_image: Path | None = None,
) -> dict[str, Any]:
    preview_app = str(Path("/tmp/preview_benchmark_driver").resolve())
    preview_corpus = str(Path("/tmp/manifest.json").resolve())
    color_app = str(Path("/tmp/color_adjustment_benchmark_driver").resolve())
    parity_app = str(Path("/tmp/render_parity_test").resolve())
    output_root = str(artifact_dir.parent.resolve())
    artifact_path = str(artifact_dir.resolve())
    run_dir_path = str(run_dir.resolve())
    evidence_log_path = str(evidence_log.resolve())
    release_report_path = str(report_path.resolve())
    expected_source = str(expected_source_image.resolve()) if expected_source_image is not None else None
    metal_command = [
        "python3",
        "-B",
        "scripts/perf/run-metal-proof-gate.py",
        "--color-app",
        color_app,
        "--parity-app",
        parity_app,
        "--output-root",
        output_root,
        "--iterations",
        "3",
        "--timeout",
        "120",
    ]
    desktop_command = [
        "python3",
        "-B",
        "scripts/perf/run-desktop-preview-proof-gate.py",
        run_dir_path,
    ]
    if expected_source is not None:
        desktop_command.extend(["--expected-source-image", expected_source])
    release_command = [
        "python3",
        "-B",
        "scripts/perf/run-preview-release-proof.py",
        "--preview-app",
        preview_app,
        "--preview-corpus",
        preview_corpus,
        "--color-app",
        color_app,
        "--parity-app",
        parity_app,
        "--desktop-proof-input",
        run_dir_path,
        "--output-root",
        artifact_path,
        "--report",
        release_report_path,
    ]
    if expected_source is not None:
        release_command.extend(["--expected-source-image", expected_source])
    capture_manifest_validation_command = [
        "python3",
        "-B",
        "scripts/perf/check-desktop-preview-proof-capture-manifest.py",
        artifact_path,
    ]
    return {
        "artifact_dir": artifact_path,
        "app_exit_code": 0,
        "generated_at": "2026-05-29T00:00:00+00:00",
        "inputs": {
            "app": "/tmp/PhotoEditor",
            "color_app": color_app,
            "label": "Proof Manifest",
            "log_level": "info",
            "output_root": output_root,
            "parity_app": parity_app,
            "preview_app": preview_app,
            "preview_corpus": preview_corpus,
            "release_report": None,
            "expected_source_image": expected_source,
        },
        "options": {
            "allow_non_metal": False,
            "allow_synthetic": False,
            "metal_iterations": 3,
            "metal_timeout": 120,
            "no_validate": False,
            "print_only": False,
            "run_release_proof": True,
            "skip_metal_preflight": False,
        },
        "proof_steps": [
            "Open a real non-synthetic image with a source edge >= 2048 px.",
        ],
        "run_dir": run_dir_path,
        "status": "release_proof_passed",
        "commands": {
            "capture_manifest_validation": capture_manifest_validation_command,
            "metal_preflight": metal_command,
            "desktop_validation": desktop_command,
            "release_proof": release_command,
        },
        "metal_preflight": {
            "command": metal_command,
            "exit_code": 0,
            "markers": {
                "metal_proof_gate": "passed",
                **metal_environment_markers(),
            },
        },
        "desktop_validation": {
            "command": desktop_command,
            "exit_code": 0,
            "markers": {
                "desktop_preview_proof_gate": "passed",
                "desktop_preview_proof_evidence_log": evidence_log_path,
                "desktop_preview_proof_release_eligible": "true",
                **(
                    {"desktop_preview_proof_expected_source_image": expected_source}
                    if expected_source is not None
                    else {}
                ),
            },
        },
        "release_proof": {
            "command": release_command,
            "exit_code": 0,
            "markers": {
                "preview_release_proof_gate": "passed",
                "preview_release_proof_release_eligible": "true",
                "preview_release_proof_report": release_report_path,
            },
        },
        "capture_manifest_validation": {
            "command": capture_manifest_validation_command,
            "exit_code": 0,
            "markers": {
                "desktop_preview_proof_capture_manifest_valid": str(
                    artifact_dir / "manifest.json"
                ),
            },
        },
        "release_report": release_report_path,
    }


def run_checker(path: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "-B", str(CHECKER), str(path)],
        cwd=REPO_ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )


def expect_pass(path: Path) -> None:
    result = run_checker(path)
    if result.returncode != 0:
        raise SystemExit(f"Expected checker to pass for {path}, got {result.returncode}:\n{result.stdout}")
    if "desktop_preview_proof_capture_manifest_valid=" not in result.stdout:
        raise SystemExit(f"Checker pass output missing validity marker:\n{result.stdout}")


def expect_fail(path: Path, expected_message: str) -> None:
    result = run_checker(path)
    if result.returncode == 0:
        raise SystemExit(f"Expected checker to fail for {path}, but it passed")
    if expected_message not in result.stdout:
        raise SystemExit(
            "Checker failed with the wrong message for "
            f"{path}:\nexpected substring: {expected_message!r}\nactual:\n{result.stdout}"
        )


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="desktop-proof-capture-manifest-") as temporary:
        root = Path(temporary).resolve()
        artifact_dir = root / "valid-artifact"
        run_dir = artifact_dir / "logs" / "runs" / "Proof-Manifest-12345678"
        run_dir.mkdir(parents=True)
        evidence_log = run_dir / "desktop.jsonl"
        evidence_log.write_text('{"event":"desktop.startup.ready","fields":{}}\n', encoding="utf-8")
        report_path = artifact_dir / "preview-release-proof.json"
        write_json(
            report_path,
            release_report(
                artifact_dir=artifact_dir,
                run_dir=run_dir,
                evidence_log=evidence_log,
            ),
        )
        valid = capture_manifest(
            artifact_dir=artifact_dir,
            run_dir=run_dir,
            evidence_log=evidence_log,
            report_path=report_path,
        )
        artifact_dir.mkdir(exist_ok=True)
        write_json(artifact_dir / "manifest.json", valid)
        expect_pass(artifact_dir)
        expect_pass(artifact_dir / "manifest.json")

        bootstrap_manifest = deepcopy(valid)
        del bootstrap_manifest["capture_manifest_validation"]
        write_json(artifact_dir / "manifest.json", bootstrap_manifest)
        expect_pass(artifact_dir)

        source_artifact_dir = root / "source-bound-artifact"
        source_run_dir = source_artifact_dir / "logs" / "runs" / "Proof-Manifest-Source"
        source_run_dir.mkdir(parents=True)
        source_evidence_log = source_run_dir / "desktop.jsonl"
        source_evidence_log.write_text(
            '{"event":"desktop.startup.ready","fields":{}}\n',
            encoding="utf-8",
        )
        expected_source_image = root / "real-large-image.dng"
        expected_source_image.write_bytes(b"proof-source")
        source_report_path = source_artifact_dir / "preview-release-proof.json"
        source_artifact_dir.mkdir(exist_ok=True)
        write_json(
            source_report_path,
            release_report(
                artifact_dir=source_artifact_dir,
                run_dir=source_run_dir,
                evidence_log=source_evidence_log,
                expected_source_image=expected_source_image,
            ),
        )
        source_bound = capture_manifest(
            artifact_dir=source_artifact_dir,
            run_dir=source_run_dir,
            evidence_log=source_evidence_log,
            report_path=source_report_path,
            expected_source_image=expected_source_image,
        )
        write_json(source_artifact_dir / "manifest.json", source_bound)
        expect_pass(source_artifact_dir)

        mismatched_source_report = release_report(
            artifact_dir=source_artifact_dir,
            run_dir=source_run_dir,
            evidence_log=source_evidence_log,
        )
        write_json(source_report_path, mismatched_source_report)
        write_json(source_artifact_dir / "manifest.json", source_bound)
        expect_fail(
            source_artifact_dir,
            "release report.inputs.expected_source_image is None; expected manifest value",
        )

        app_failed = deepcopy(valid)
        app_failed["app_exit_code"] = 1
        write_json(artifact_dir / "manifest.json", app_failed)
        expect_fail(
            artifact_dir,
            "manifest.app_exit_code is 1; expected 0",
        )

        missing_steps = deepcopy(valid)
        missing_steps["proof_steps"] = []
        write_json(artifact_dir / "manifest.json", missing_steps)
        expect_fail(
            artifact_dir,
            "manifest.proof_steps must be a nonempty list of nonempty strings",
        )

        missing_app_input = deepcopy(valid)
        missing_app_input["inputs"]["app"] = ""
        write_json(artifact_dir / "manifest.json", missing_app_input)
        expect_fail(
            artifact_dir,
            "manifest.inputs.app must be a nonempty string",
        )

        smoke_option = deepcopy(valid)
        smoke_option["options"]["allow_synthetic"] = True
        write_json(artifact_dir / "manifest.json", smoke_option)
        expect_fail(
            artifact_dir,
            "manifest.options.allow_synthetic is True; expected False",
        )

        smoke_command = deepcopy(valid)
        smoke_command["commands"]["desktop_validation"].append("--allow-synthetic")
        smoke_command["desktop_validation"]["command"].append("--allow-synthetic")
        write_json(artifact_dir / "manifest.json", smoke_command)
        expect_fail(
            artifact_dir,
            "manifest.commands.desktop_validation must not include smoke flag --allow-synthetic",
        )

        command_mismatch = deepcopy(valid)
        command_mismatch["release_proof"]["command"] = ["different-command"]
        write_json(artifact_dir / "manifest.json", command_mismatch)
        expect_fail(
            artifact_dir,
            "manifest.release_proof.command does not match manifest.commands.release_proof",
        )

        self_check_mismatch = deepcopy(valid)
        self_check_mismatch["capture_manifest_validation"]["command"] = ["different-command"]
        write_json(artifact_dir / "manifest.json", self_check_mismatch)
        expect_fail(
            artifact_dir,
            (
                "manifest.capture_manifest_validation.command does not match "
                "manifest.commands.capture_manifest_validation"
            ),
        )

        self_check_wrong_manifest = deepcopy(valid)
        self_check_wrong_manifest["capture_manifest_validation"]["markers"][
            "desktop_preview_proof_capture_manifest_valid"
        ] = str(artifact_dir / "other-manifest.json")
        write_json(artifact_dir / "manifest.json", self_check_wrong_manifest)
        expect_fail(
            artifact_dir,
            (
                "manifest.capture_manifest_validation.markers."
                "desktop_preview_proof_capture_manifest_valid must match this manifest"
            ),
        )

        release_report_mismatch = deepcopy(valid)
        release_report_mismatch["release_report"] = str(artifact_dir / "other.json")
        write_json(artifact_dir / "manifest.json", release_report_mismatch)
        expect_fail(
            artifact_dir,
            (
                "manifest.release_report must match "
                "manifest.release_proof.markers.preview_release_proof_report"
            ),
        )

        mismatched_report_inputs = release_report(
            artifact_dir=artifact_dir,
            run_dir=run_dir,
            evidence_log=evidence_log,
        )
        mismatched_preview_app = str(Path("/tmp/other_preview_benchmark_driver").resolve())
        mismatched_report_inputs["inputs"]["preview_app"] = mismatched_preview_app
        mismatched_report_inputs["commands"]["preview_pipeline"]["command"][
            mismatched_report_inputs["commands"]["preview_pipeline"]["command"].index("--app") + 1
        ] = mismatched_preview_app
        write_json(report_path, mismatched_report_inputs)
        write_json(artifact_dir / "manifest.json", valid)
        expect_fail(
            artifact_dir,
            (
                "release report.inputs.preview_app is "
                f"'{mismatched_preview_app}'; expected manifest value"
            ),
        )

        bad_report = deepcopy(valid)
        bad_report_path = artifact_dir / "preview-release-proof.json"
        bad_report["release_report"] = str(bad_report_path)
        bad_report["release_proof"]["markers"]["preview_release_proof_report"] = str(bad_report_path)
        write_json(artifact_dir / "manifest.json", bad_report)
        write_json(bad_report_path, {"release_eligible": False})
        expect_fail(
            artifact_dir,
            "release report.inputs must be an object",
        )

    print("desktop_preview_proof_capture_manifest_self_test=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
