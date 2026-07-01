#!/usr/bin/env python3
"""Self-test the preview release proof report checker."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from copy import deepcopy
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
CHECKER = REPO_ROOT / "scripts/perf/check-preview-release-proof-report.py"


def metal_environment_markers() -> dict[str, str]:
    return {
        "metal_proof_host_system": "Darwin",
        "metal_proof_host_machine": "arm64",
        "metal_proof_python_platform": "darwin",
        "metal_proof_docker_detected": "false",
        "metal_proof_ci_detected": "false",
        "metal_proof_restricted_context_possible": "true",
    }


def base_report(
    evidence_log: Path,
    desktop_proof_input: Path,
    expected_source_image: Path | None = None,
) -> dict[str, Any]:
    evidence_log_path = str(evidence_log.resolve())
    preview_app = str(Path("/tmp/preview_benchmark_driver").resolve())
    preview_corpus = str(Path("/tmp/manifest.json").resolve())
    color_app = str(Path("/tmp/color_adjustment_benchmark_driver").resolve())
    parity_app = str(Path("/tmp/render_parity_test").resolve())
    output_root = str(Path("/tmp/perf-artifacts").resolve())
    desktop_input = str(desktop_proof_input.resolve())
    report = {
        "generated_at": "2026-05-29T00:00:00+00:00",
        "release_eligible": True,
        "gate": "passed",
        "inputs": {
            "preview_app": preview_app,
            "preview_corpus": preview_corpus,
            "color_app": color_app,
            "parity_app": parity_app,
            "desktop_proof_input": desktop_input,
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
                    desktop_input,
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


def write_report(directory: Path, name: str, report: dict[str, Any]) -> Path:
    path = directory / name
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return path


def run_checker(report_path: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "-B", str(CHECKER), str(report_path)],
        cwd=REPO_ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )


def expect_pass(report_path: Path) -> None:
    result = run_checker(report_path)
    if result.returncode != 0:
        raise SystemExit(
            f"Expected checker to pass for {report_path}, got {result.returncode}:\n{result.stdout}"
        )
    if "preview_release_proof_report_valid=" not in result.stdout:
        raise SystemExit(f"Checker pass output missing validity marker:\n{result.stdout}")


def expect_fail(report_path: Path, expected_message: str) -> None:
    result = run_checker(report_path)
    if result.returncode == 0:
        raise SystemExit(f"Expected checker to fail for {report_path}, but it passed")
    if expected_message not in result.stdout:
        raise SystemExit(
            "Checker failed with the wrong message for "
            f"{report_path}:\nexpected substring: {expected_message!r}\nactual:\n{result.stdout}"
        )


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="preview-release-proof-report-") as temporary:
        root = Path(temporary)
        evidence_log = root / "desktop.jsonl"
        evidence_log.write_text('{"event":"desktop.startup.ready","fields":{}}\n', encoding="utf-8")
        valid = base_report(evidence_log, root)
        expect_pass(write_report(root, "valid.json", valid))

        smoke = deepcopy(valid)
        smoke["release_eligible"] = False
        smoke["gate"] = "smoke_passed"
        smoke["smoke_flags"]["allow_metal_unavailable"] = True
        smoke["gates"]["metal"]["metal_proof_gate"] = "skipped"
        expect_fail(
            write_report(root, "smoke.json", smoke),
            "report.release_eligible is False; expected True",
        )

        missing_metal = deepcopy(valid)
        missing_metal["gates"]["metal"]["metal_proof_gate"] = "skipped"
        expect_fail(
            write_report(root, "missing-metal.json", missing_metal),
            "metal.metal_proof_gate is 'skipped'; expected 'passed'",
        )

        missing_metal_environment = deepcopy(valid)
        del missing_metal_environment["gates"]["metal"]["metal_proof_host_system"]
        expect_fail(
            write_report(root, "missing-metal-environment.json", missing_metal_environment),
            "metal.metal_proof_host_system must be a nonempty string",
        )

        docker_metal_environment = deepcopy(valid)
        docker_metal_environment["gates"]["metal"]["metal_proof_docker_detected"] = "true"
        docker_metal_environment["commands"]["metal"]["markers"][
            "metal_proof_docker_detected"
        ] = "true"
        expect_fail(
            write_report(root, "docker-metal-environment.json", docker_metal_environment),
            "metal.metal_proof_docker_detected is 'true'; expected 'false'",
        )

        mismatched_metal_environment = deepcopy(valid)
        mismatched_metal_environment["commands"]["metal"]["markers"][
            "metal_proof_host_machine"
        ] = "x86_64"
        expect_fail(
            write_report(
                root,
                "mismatched-metal-environment.json",
                mismatched_metal_environment,
            ),
            (
                "report.commands.metal.markers.metal_proof_host_machine is 'x86_64'; "
                "expected 'arm64'"
            ),
        )

        desktop_not_release = deepcopy(valid)
        desktop_not_release["gates"]["desktop"]["desktop_preview_proof_release_eligible"] = "false"
        expect_fail(
            write_report(root, "desktop-not-release.json", desktop_not_release),
            (
                "desktop.desktop_preview_proof_release_eligible is 'false'; "
                "expected 'true'"
            ),
        )

        failed_exit_code = deepcopy(valid)
        failed_exit_code["subgate_exit_codes"]["metal"] = 1
        failed_exit_code["commands"]["metal"]["exit_code"] = 1
        failed_exit_code["commands"]["metal"]["markers"] = {
            "metal_proof_gate": "failed",
            "metal_proof_error": "gpu_backend_unavailable",
        }
        expect_fail(
            write_report(root, "failed-exit-code.json", failed_exit_code),
            "report.subgate_exit_codes.metal is 1; expected 0",
        )

        forged_top_level_markers = deepcopy(valid)
        forged_top_level_markers["commands"]["metal"]["markers"]["metal_proof_gate"] = "failed"
        expect_fail(
            write_report(root, "forged-top-level-markers.json", forged_top_level_markers),
            (
                "report.commands.metal.markers.metal_proof_gate is 'failed'; "
                "expected 'passed'"
            ),
        )

        missing_commands = deepcopy(valid)
        del missing_commands["commands"]
        expect_fail(
            write_report(root, "missing-commands.json", missing_commands),
            "report.commands must be an object",
        )

        bad_command_type = deepcopy(valid)
        bad_command_type["commands"]["preview_pipeline"]["command"] = [
            "python3",
            123,
        ]
        expect_fail(
            write_report(root, "bad-command-type.json", bad_command_type),
            "report.commands.preview_pipeline.command must be a list of strings",
        )

        wrong_preview_command_input = deepcopy(valid)
        wrong_preview_command_input["commands"]["preview_pipeline"]["command"] = [
            argument
            for argument in wrong_preview_command_input["commands"]["preview_pipeline"][
                "command"
            ]
            if argument != wrong_preview_command_input["inputs"]["preview_corpus"]
        ]
        expect_fail(
            write_report(root, "wrong-preview-command-input.json", wrong_preview_command_input),
            (
                "report.commands.preview_pipeline.command must pass "
                f"{wrong_preview_command_input['inputs']['preview_corpus']} after --corpus"
            ),
        )

        wrong_preview_script = deepcopy(valid)
        wrong_preview_script["commands"]["preview_pipeline"]["command"][2] = (
            "scripts/perf/not-the-preview-pipeline-gate.py"
        )
        expect_fail(
            write_report(root, "wrong-preview-script.json", wrong_preview_script),
            (
                "report.commands.preview_pipeline.command must invoke "
                "scripts/perf/run-preview-pipeline-gate.py"
            ),
        )

        wrong_metal_command_input = deepcopy(valid)
        wrong_metal_command_input["commands"]["metal"]["command"] = [
            argument
            for argument in wrong_metal_command_input["commands"]["metal"]["command"]
            if argument != wrong_metal_command_input["inputs"]["parity_app"]
        ]
        expect_fail(
            write_report(root, "wrong-metal-command-input.json", wrong_metal_command_input),
            (
                "report.commands.metal.command must pass "
                f"{wrong_metal_command_input['inputs']['parity_app']} after --parity-app"
            ),
        )

        metal_smoke_flag = deepcopy(valid)
        metal_smoke_flag["commands"]["metal"]["command"].append("--allow-unavailable")
        expect_fail(
            write_report(root, "metal-smoke-flag.json", metal_smoke_flag),
            "report.commands.metal.command must not include smoke flag --allow-unavailable",
        )

        wrong_desktop_command_input = deepcopy(valid)
        wrong_desktop_root = root / "wrong-desktop-command-input"
        wrong_desktop_root.mkdir()
        wrong_desktop_command_input["commands"]["desktop"]["command"][-1] = str(
            wrong_desktop_root.resolve()
        )
        expect_fail(
            write_report(root, "wrong-desktop-command-input.json", wrong_desktop_command_input),
            (
                "report.commands.desktop.command must pass "
                f"{valid['inputs']['desktop_proof_input']} immediately after "
                "scripts/perf/run-desktop-preview-proof-gate.py"
            ),
        )

        desktop_smoke_flag = deepcopy(valid)
        desktop_smoke_flag["commands"]["desktop"]["command"].append("--allow-synthetic")
        expect_fail(
            write_report(root, "desktop-smoke-flag.json", desktop_smoke_flag),
            "report.commands.desktop.command must not include smoke flag --allow-synthetic",
        )

        missing_evidence_log = deepcopy(valid)
        del missing_evidence_log["gates"]["desktop"]["desktop_preview_proof_evidence_log"]
        expect_fail(
            write_report(root, "missing-evidence-log.json", missing_evidence_log),
            "desktop.desktop_preview_proof_evidence_log must be a nonempty string",
        )

        missing_command_evidence_log = deepcopy(valid)
        del missing_command_evidence_log["commands"]["desktop"]["markers"][
            "desktop_preview_proof_evidence_log"
        ]
        expect_fail(
            write_report(root, "missing-command-evidence-log.json", missing_command_evidence_log),
            (
                "report.commands.desktop.markers.desktop_preview_proof_evidence_log "
                "must be a nonempty string"
            ),
        )

        mismatched_evidence_log = deepcopy(valid)
        mismatched_evidence_log["commands"]["desktop"]["markers"][
            "desktop_preview_proof_evidence_log"
        ] = str(root / "other.jsonl")
        expect_fail(
            write_report(root, "mismatched-evidence-log.json", mismatched_evidence_log),
            (
                "report.commands.desktop.markers.desktop_preview_proof_evidence_log "
                f"is '{root / 'other.jsonl'}'; expected '{evidence_log.resolve()}'"
            ),
        )

        nonexistent_evidence_log = deepcopy(valid)
        missing_log = root / "missing.jsonl"
        nonexistent_evidence_log["gates"]["desktop"][
            "desktop_preview_proof_evidence_log"
        ] = str(missing_log)
        nonexistent_evidence_log["commands"]["desktop"]["markers"][
            "desktop_preview_proof_evidence_log"
        ] = str(missing_log)
        expect_fail(
            write_report(root, "nonexistent-evidence-log.json", nonexistent_evidence_log),
            f"desktop evidence log does not exist: {missing_log.resolve()}",
        )

        non_jsonl_evidence_log = deepcopy(valid)
        txt_log = root / "desktop.txt"
        txt_log.write_text("not jsonl\n", encoding="utf-8")
        non_jsonl_evidence_log["gates"]["desktop"][
            "desktop_preview_proof_evidence_log"
        ] = str(txt_log)
        non_jsonl_evidence_log["commands"]["desktop"]["markers"][
            "desktop_preview_proof_evidence_log"
        ] = str(txt_log)
        expect_fail(
            write_report(root, "non-jsonl-evidence-log.json", non_jsonl_evidence_log),
            f"desktop evidence log is not a JSONL file: {txt_log.resolve()}",
        )

        missing_desktop_input = deepcopy(valid)
        missing_input = root / "missing-input"
        missing_desktop_input["inputs"]["desktop_proof_input"] = str(missing_input)
        expect_fail(
            write_report(root, "missing-desktop-input.json", missing_desktop_input),
            f"report.inputs.desktop_proof_input does not exist: {missing_input.resolve()}",
        )

        unrelated_input = deepcopy(valid)
        unrelated_root = root / "unrelated-input"
        unrelated_root.mkdir()
        unrelated_input["inputs"]["desktop_proof_input"] = str(unrelated_root)
        expect_fail(
            write_report(root, "unrelated-desktop-input.json", unrelated_input),
            (
                "desktop evidence log is not under report.inputs.desktop_proof_input: "
                f"{evidence_log.resolve()} not under {unrelated_root.resolve()}"
            ),
        )

        file_input = base_report(evidence_log, evidence_log)
        expect_pass(write_report(root, "file-input.json", file_input))

        expected_source_image = root / "real-large-image.dng"
        expected_source_image.write_bytes(b"proof-source")
        source_bound = base_report(evidence_log, root, expected_source_image)
        expect_pass(write_report(root, "source-bound.json", source_bound))

        missing_source_command_flag = deepcopy(source_bound)
        missing_source_command_flag["commands"]["desktop"]["command"] = [
            argument
            for argument in missing_source_command_flag["commands"]["desktop"]["command"]
            if argument not in {"--expected-source-image", str(expected_source_image.resolve())}
        ]
        expect_fail(
            write_report(root, "missing-source-command-flag.json", missing_source_command_flag),
            (
                "report.commands.desktop.command must pass "
                f"{expected_source_image.resolve()} after --expected-source-image"
            ),
        )

        mismatched_source_marker = deepcopy(source_bound)
        other_source_image = root / "other-large-image.dng"
        other_source_image.write_bytes(b"other-proof-source")
        mismatched_source_marker["commands"]["desktop"]["markers"][
            "desktop_preview_proof_expected_source_image"
        ] = str(other_source_image)
        expect_fail(
            write_report(root, "mismatched-source-marker.json", mismatched_source_marker),
            (
                "report.commands.desktop.markers."
                "desktop_preview_proof_expected_source_image is "
                f"'{other_source_image.resolve()}'; expected '{expected_source_image.resolve()}'"
            ),
        )

    print("preview_release_proof_report_self_test=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
