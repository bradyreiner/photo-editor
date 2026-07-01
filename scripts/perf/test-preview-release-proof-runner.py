#!/usr/bin/env python3
"""Self-test release eligibility logic for the preview release proof runner."""

from __future__ import annotations

import importlib.util
import tempfile
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
RUNNER = REPO_ROOT / "scripts/perf/run-preview-release-proof.py"


def load_runner() -> Any:
    spec = importlib.util.spec_from_file_location("preview_release_proof_runner", RUNNER)
    if spec is None or spec.loader is None:
        raise SystemExit(f"Unable to import {RUNNER}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def valid_metal_markers() -> dict[str, str]:
    return {
        "metal_proof_gate": "passed",
        "metal_proof_host_system": "Darwin",
        "metal_proof_host_machine": "arm64",
        "metal_proof_python_platform": "darwin",
        "metal_proof_docker_detected": "false",
        "metal_proof_ci_detected": "false",
        "metal_proof_restricted_context_possible": "true",
    }


def main() -> int:
    runner = load_runner()
    valid_preview = {"preview_pipeline_gate": "passed"}
    valid_metal = valid_metal_markers()
    valid_desktop = {
        "desktop_preview_proof_gate": "passed",
        "desktop_preview_proof_evidence_log": "/tmp/real-metal-desktop-run/desktop.jsonl",
        "desktop_preview_proof_release_eligible": "true",
    }
    eligible = runner.release_eligible_from_outputs(
        skip_preview_pipeline=False,
        allow_metal_unavailable=False,
        allow_desktop_smoke=False,
        preview_values=valid_preview,
        metal_values=valid_metal,
        desktop_values=valid_desktop,
    )
    require(eligible, "Expected release-shaped outputs to be eligible")

    missing_preview_marker = runner.release_eligible_from_outputs(
        skip_preview_pipeline=False,
        allow_metal_unavailable=False,
        allow_desktop_smoke=False,
        preview_values={},
        metal_values=valid_metal,
        desktop_values=valid_desktop,
    )
    require(
        not missing_preview_marker,
        "Missing preview_pipeline_gate=passed must not be release eligible",
    )

    missing_metal_environment = runner.release_eligible_from_outputs(
        skip_preview_pipeline=False,
        allow_metal_unavailable=False,
        allow_desktop_smoke=False,
        preview_values=valid_preview,
        metal_values={"metal_proof_gate": "passed"},
        desktop_values=valid_desktop,
    )
    require(
        not missing_metal_environment,
        "Missing Metal environment markers must not be release eligible",
    )

    docker_metal_environment = valid_metal_markers()
    docker_metal_environment["metal_proof_docker_detected"] = "true"
    require(
        not runner.release_eligible_from_outputs(
            skip_preview_pipeline=False,
            allow_metal_unavailable=False,
            allow_desktop_smoke=False,
            preview_values=valid_preview,
            metal_values=docker_metal_environment,
            desktop_values=valid_desktop,
        ),
        "Docker Metal markers must not be release eligible",
    )

    desktop_not_release = runner.release_eligible_from_outputs(
        skip_preview_pipeline=False,
        allow_metal_unavailable=False,
        allow_desktop_smoke=False,
        preview_values=valid_preview,
        metal_values=valid_metal,
        desktop_values={
            "desktop_preview_proof_gate": "passed",
            "desktop_preview_proof_release_eligible": "false",
        },
    )
    require(
        not desktop_not_release,
        "Desktop smoke output must not be release eligible even if gate marker passed",
    )

    missing_desktop_evidence_log = runner.release_eligible_from_outputs(
        skip_preview_pipeline=False,
        allow_metal_unavailable=False,
        allow_desktop_smoke=False,
        preview_values=valid_preview,
        metal_values=valid_metal,
        desktop_values={
            "desktop_preview_proof_gate": "passed",
            "desktop_preview_proof_release_eligible": "true",
        },
    )
    require(
        not missing_desktop_evidence_log,
        "Missing desktop evidence-log marker must not be release eligible",
    )

    smoke_flag = runner.release_eligible_from_outputs(
        skip_preview_pipeline=False,
        allow_metal_unavailable=True,
        allow_desktop_smoke=False,
        preview_values=valid_preview,
        metal_values=valid_metal,
        desktop_values=valid_desktop,
    )
    require(not smoke_flag, "Smoke flags must prevent release eligibility")

    require(
        runner.smoke_mode_enabled(
            skip_preview_pipeline=False,
            allow_metal_unavailable=True,
            allow_desktop_smoke=False,
        ),
        "Smoke mode should be enabled when any smoke flag is set",
    )
    require(
        not runner.smoke_mode_enabled(
            skip_preview_pipeline=False,
            allow_metal_unavailable=False,
            allow_desktop_smoke=False,
        ),
        "Smoke mode should be disabled for no-smoke release runs",
    )
    require(
        runner.release_runner_exit_code(release_eligible=True, smoke_mode=False) == 0,
        "Release-eligible no-smoke run should exit zero",
    )
    require(
        runner.release_runner_exit_code(release_eligible=False, smoke_mode=True) == 0,
        "Explicit smoke run should exit zero so local control-flow checks remain usable",
    )
    require(
        runner.release_runner_exit_code(release_eligible=False, smoke_mode=False) == 1,
        "No-smoke non-release run should fail automation",
    )
    require(
        runner.should_check_release_report(release_eligible=True, exit_code=0),
        "Release-eligible zero-exit runs should validate the generated JSON report",
    )
    require(
        not runner.should_check_release_report(release_eligible=False, exit_code=0),
        "Smoke runs should not validate the generated JSON report as release proof",
    )
    require(
        not runner.should_check_release_report(release_eligible=True, exit_code=1),
        "Failed runs should not attempt release report validation",
    )
    require(
        runner.release_gate_status(
            release_eligible=True,
            smoke_mode=False,
            subgates_passed=True,
        )
        == "passed",
        "Release-eligible passing sub-gates should produce a passed gate",
    )
    require(
        runner.release_gate_status(
            release_eligible=False,
            smoke_mode=True,
            subgates_passed=True,
        )
        == "smoke_passed",
        "Smoke mode should produce smoke_passed only when sub-gates pass",
    )
    require(
        runner.release_gate_status(
            release_eligible=False,
            smoke_mode=True,
            subgates_passed=False,
        )
        == "failed",
        "Smoke mode must not hide failed sub-gates",
    )
    failed_result = runner.synthetic_result(
        ["metal-proof"],
        77,
        "metal_proof_gate=failed\nmetal_proof_error=no_system_metal_device\n",
    )
    failed_report = runner.command_report(failed_result)
    require(
        failed_report["exit_code"] == 77,
        f"Command report should include failed exit code: {failed_report}",
    )
    require(
        failed_report["markers"]["metal_proof_error"] == "no_system_metal_device",
        f"Command report should parse failed gate markers: {failed_report}",
    )
    missing_desktop = runner.run_desktop_proof(
        type(
            "Args",
            (),
            {
                "desktop_proof_input": None,
                "allow_desktop_smoke": False,
                "expected_source_image": None,
            },
        )()
    )
    require(
        missing_desktop.returncode != 0,
        "Missing desktop proof input should be represented as a failed gate result",
    )
    require(
        runner.result_values(missing_desktop)["desktop_preview_proof_error"]
        == "desktop_proof_input_missing",
        f"Missing desktop proof input marker was not parsed: {missing_desktop.stdout}",
    )
    missing_preview_app = runner.run_preview_pipeline(
        type(
            "Args",
            (),
            {
                "skip_preview_pipeline": False,
                "preview_app": Path("/tmp/does-not-exist-preview-app"),
                "preview_corpus": Path("/tmp/does-not-exist-preview-corpus"),
                "output_root": Path("/tmp/proof-output"),
                "preview_timeout": 180,
                "preview_timeout_ms": 30000,
            },
        )()
    )
    require(
        missing_preview_app.returncode != 0,
        "Missing preview app should be represented as a failed preview gate result",
    )
    require(
        runner.result_values(missing_preview_app)["preview_pipeline_error"] == "preview_app_missing",
        f"Missing preview app marker was not parsed: {missing_preview_app.stdout}",
    )
    missing_color_app = runner.run_metal_proof(
        type(
            "Args",
            (),
            {
                "color_app": Path("/tmp/does-not-exist-color-app"),
                "parity_app": Path("/tmp/does-not-exist-parity-app"),
                "output_root": Path("/tmp/proof-output"),
                "metal_iterations": 30,
                "metal_timeout": 120,
                "allow_metal_unavailable": False,
            },
        )()
    )
    require(
        missing_color_app.returncode != 0,
        "Missing color app should be represented as a failed Metal gate result",
    )
    require(
        runner.result_values(missing_color_app)["metal_proof_error"] == "color_app_missing",
        f"Missing color app marker was not parsed: {missing_color_app.stdout}",
    )
    missing_desktop_path = runner.run_desktop_proof(
        type(
            "Args",
            (),
            {
                "desktop_proof_input": Path("/tmp/does-not-exist-desktop-proof"),
                "allow_desktop_smoke": False,
                "expected_source_image": None,
            },
        )()
    )
    require(
        missing_desktop_path.returncode != 0,
        "Missing desktop proof path should be represented as a failed desktop gate result",
    )
    require(
        runner.result_values(missing_desktop_path)["desktop_preview_proof_error"]
        == "desktop_proof_input_missing",
        f"Missing desktop proof path marker was not parsed: {missing_desktop_path.stdout}",
    )

    with tempfile.TemporaryDirectory(prefix="preview-release-proof-runner-") as temporary:
        root = Path(temporary)
        desktop_run = root / "desktop-run"
        desktop_run.mkdir()
        expected_source_image = root / "real-large-image.dng"
        expected_source_image.write_bytes(b"proof-source")
        captured_commands: list[list[str]] = []

        def fake_run_command(command: list[str]) -> Any:
            captured_commands.append(command)
            return runner.synthetic_result(
                command,
                0,
                (
                    "desktop_preview_proof_gate=passed\n"
                    "desktop_preview_proof_release_eligible=true\n"
                    f"desktop_preview_proof_expected_source_image={expected_source_image.resolve()}\n"
                ),
            )

        original_run_command = runner.run_command
        runner.run_command = fake_run_command
        try:
            desktop_result = runner.run_desktop_proof(
                type(
                    "Args",
                    (),
                    {
                        "desktop_proof_input": desktop_run,
                        "allow_desktop_smoke": False,
                        "expected_source_image": expected_source_image,
                    },
                )()
            )
        finally:
            runner.run_command = original_run_command
        require(desktop_result.returncode == 0, "Desktop proof command should return command result")
        require(captured_commands, "Desktop proof should invoke run_command for existing input")
        require(
            captured_commands[0][captured_commands[0].index("--expected-source-image") + 1]
            == str(expected_source_image.resolve()),
            f"Desktop proof command missing expected source image: {captured_commands[0]}",
        )

    print("preview_release_proof_runner_self_test=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
