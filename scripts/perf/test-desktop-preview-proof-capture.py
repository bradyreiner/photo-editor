#!/usr/bin/env python3
"""Self-test desktop preview proof capture helper command construction."""

from __future__ import annotations

import importlib.util
import json
import tempfile
from pathlib import Path
from types import SimpleNamespace
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
CAPTURE = REPO_ROOT / "scripts/perf/run-desktop-preview-proof-capture.py"


def load_capture() -> Any:
    spec = importlib.util.spec_from_file_location("desktop_preview_proof_capture", CAPTURE)
    if spec is None or spec.loader is None:
        raise SystemExit(f"Unable to import {CAPTURE}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def main() -> int:
    capture = load_capture()

    require(capture.safe_label("Desktop Proof!") == "Desktop-Proof", "Label sanitization failed")
    require(
        capture.safe_label(" / ") == "desktop-preview-proof",
        "Blank sanitized labels should fall back to the default",
    )

    output_root = Path("/tmp/photo-proof").resolve()
    artifact_dir, run_dir = capture.capture_paths(
        output_root,
        "Desktop Proof!",
        "abcdef1234567890",
        "20260529T000000.000000Z",
    )
    expected_artifact_dir = output_root / "20260529T000000.000000Z-Desktop-Proof-abcdef12"
    require(
        artifact_dir == expected_artifact_dir,
        f"Unexpected artifact dir: {artifact_dir}",
    )
    require(
        run_dir == expected_artifact_dir / "logs/runs/Desktop-Proof-abcdef12",
        f"Unexpected run dir: {run_dir}",
    )

    smoke_args = SimpleNamespace(
        allow_synthetic=True,
        allow_non_metal=True,
        expected_source_image=None,
    )
    validation = capture.desktop_validation_command(run_dir, smoke_args)
    require(
        validation[-2:] == ["--allow-synthetic", "--allow-non-metal"],
        f"Desktop validation smoke flags missing: {validation}",
    )

    release_report = Path("/tmp/release-report.json").resolve()
    expected_source_image = Path("/tmp/expected-source-image.dng").resolve()
    preview_app = Path("/tmp/preview_benchmark_driver").resolve()
    preview_corpus = Path("/tmp/manifest.json").resolve()
    color_app = Path("/tmp/color_adjustment_benchmark_driver").resolve()
    parity_app = Path("/tmp/render_parity_test").resolve()
    release_args = SimpleNamespace(
        preview_app=preview_app,
        preview_corpus=preview_corpus,
        color_app=color_app,
        parity_app=parity_app,
        release_report=release_report,
        expected_source_image=expected_source_image,
    )
    release = capture.release_proof_command(run_dir, artifact_dir, release_args)
    require(
        "scripts/perf/run-preview-release-proof.py" in release,
        f"Release proof command missing runner: {release}",
    )
    require(
        release[release.index("--preview-app") + 1] == str(preview_app),
        f"Release proof command missing preview app: {release}",
    )
    require(
        release[release.index("--preview-corpus") + 1] == str(preview_corpus),
        f"Release proof command missing preview corpus: {release}",
    )
    require(
        release[release.index("--color-app") + 1] == str(color_app),
        f"Release proof command missing color app: {release}",
    )
    require(
        release[release.index("--parity-app") + 1] == str(parity_app),
        f"Release proof command missing parity app: {release}",
    )
    require(
        release[release.index("--desktop-proof-input") + 1] == str(run_dir),
        f"Release proof command missing desktop input: {release}",
    )
    require(
        release[release.index("--output-root") + 1] == str(artifact_dir),
        f"Release proof command missing output root: {release}",
    )
    require(
        release[release.index("--report") + 1] == str(release_report),
        f"Release proof command missing report path: {release}",
    )
    require(
        release[release.index("--expected-source-image") + 1] == str(expected_source_image),
        f"Release proof command missing expected source image: {release}",
    )
    capture_manifest_validation = capture.capture_manifest_validation_command(artifact_dir)
    require(
        "scripts/perf/check-desktop-preview-proof-capture-manifest.py"
        in capture_manifest_validation,
        f"Capture manifest validation command missing checker: {capture_manifest_validation}",
    )
    require(
        capture_manifest_validation[-1] == str(artifact_dir),
        f"Capture manifest validation command missing artifact dir: {capture_manifest_validation}",
    )

    metal_preflight_args = SimpleNamespace(
        skip_metal_preflight=False,
        metal_iterations=5,
        metal_timeout=77,
        color_app=color_app,
        parity_app=parity_app,
        output_root=output_root,
    )
    expected_metal_preflight_command = capture.metal_preflight_command(metal_preflight_args)
    require(
        expected_metal_preflight_command[
            expected_metal_preflight_command.index("--color-app") + 1
        ]
        == str(color_app),
        f"Metal preflight command missing color app: {expected_metal_preflight_command}",
    )
    require(
        expected_metal_preflight_command[
            expected_metal_preflight_command.index("--parity-app") + 1
        ]
        == str(parity_app),
        f"Metal preflight command missing parity app: {expected_metal_preflight_command}",
    )
    require(
        expected_metal_preflight_command[
            expected_metal_preflight_command.index("--output-root") + 1
        ]
        == str(output_root),
        f"Metal preflight command missing output root: {expected_metal_preflight_command}",
    )
    require(
        expected_metal_preflight_command[
            expected_metal_preflight_command.index("--iterations") + 1
        ]
        == "5",
        f"Metal preflight command missing iterations: {expected_metal_preflight_command}",
    )
    require(
        expected_metal_preflight_command[
            expected_metal_preflight_command.index("--timeout") + 1
        ]
        == "77",
        f"Metal preflight command missing timeout: {expected_metal_preflight_command}",
    )

    missing_args = SimpleNamespace(
        run_release_proof=True,
        print_only=False,
        preview_app=Path("/tmp/does-not-exist-preview"),
        preview_corpus=Path("/tmp/does-not-exist-corpus.json"),
        color_app=Path("/tmp/does-not-exist-color"),
        parity_app=Path("/tmp/does-not-exist-parity"),
        expected_source_image=None,
    )
    missing_preflight = capture.preflight_release_inputs(missing_args)
    require(
        missing_preflight is not None and missing_preflight.returncode == 2,
        "Release input preflight should return a failed result on missing inputs",
    )
    require(
        "desktop_preview_proof_capture_error=preview_app_missing" in missing_preflight.stdout,
        f"Unexpected preflight failure markers: {missing_preflight.stdout}",
    )
    missing_app = capture.preflight_app(
        SimpleNamespace(print_only=False, app=Path("/tmp/does-not-exist-photo-editor-app"))
    )
    require(
        missing_app is not None and missing_app.returncode == 2,
        "App preflight should return a failed result on missing app binary",
    )
    require(
        "desktop_preview_proof_capture_error=app_missing" in missing_app.stdout,
        f"Unexpected app preflight failure markers: {missing_app.stdout}",
    )
    invalid_release_options = capture.preflight_release_mode_options(
        SimpleNamespace(
            run_release_proof=True,
            print_only=False,
            allow_synthetic=True,
            allow_non_metal=True,
            no_validate=True,
            skip_metal_preflight=True,
        )
    )
    require(
        invalid_release_options is not None and invalid_release_options.returncode == 2,
        "Release proof mode should reject smoke or skipped validation options",
    )
    require(
        "desktop_preview_proof_capture_error=release_mode_options_invalid"
        in invalid_release_options.stdout,
        f"Unexpected release option failure markers: {invalid_release_options.stdout}",
    )
    require(
        "--allow-synthetic,--allow-non-metal,--no-validate,--skip-metal-preflight"
        in invalid_release_options.stdout,
        f"Invalid release options were not reported: {invalid_release_options.stdout}",
    )
    print_only_release_options = capture.preflight_release_mode_options(
        SimpleNamespace(
            run_release_proof=True,
            print_only=True,
            allow_synthetic=True,
            allow_non_metal=True,
            no_validate=True,
            skip_metal_preflight=True,
        )
    )
    require(
        print_only_release_options is None,
        "Print-only mode should allow smoke/skip options for layout inspection",
    )
    outside_release_report = capture.preflight_release_report_path(
        SimpleNamespace(
            run_release_proof=True,
            print_only=False,
            release_report=Path("/tmp/outside-release-report.json"),
        ),
        artifact_dir,
    )
    require(
        outside_release_report is not None and outside_release_report.returncode == 2,
        "Release proof mode should reject custom reports outside the capture artifact",
    )
    require(
        "desktop_preview_proof_capture_error=release_report_outside_artifact"
        in outside_release_report.stdout,
        f"Unexpected release report path failure markers: {outside_release_report.stdout}",
    )
    require(
        f"desktop_preview_proof_capture_artifact_dir={artifact_dir}"
        in outside_release_report.stdout,
        f"Release report path failure did not name artifact dir: {outside_release_report.stdout}",
    )
    inside_release_report = capture.preflight_release_report_path(
        SimpleNamespace(
            run_release_proof=True,
            print_only=False,
            release_report=artifact_dir / "release-proof.json",
        ),
        artifact_dir,
    )
    require(
        inside_release_report is None,
        "Release proof mode should allow custom reports inside the capture artifact",
    )
    print_only_release_report = capture.preflight_release_report_path(
        SimpleNamespace(
            run_release_proof=True,
            print_only=True,
            release_report=Path("/tmp/outside-release-report.json"),
        ),
        artifact_dir,
    )
    require(
        print_only_release_report is None,
        "Print-only mode should allow outside report paths for layout inspection",
    )

    captured_commands: list[list[str]] = []

    def fake_run_command(command: list[str], **_: object) -> Any:
        captured_commands.append(command)
        return capture.subprocess.CompletedProcess(command, 0, "metal_proof_gate=passed\n", "")

    original_run_command = capture.run_command
    capture.run_command = fake_run_command
    try:
        metal_preflight = capture.run_metal_preflight(metal_preflight_args)
    finally:
        capture.run_command = original_run_command
    require(
        metal_preflight is not None and metal_preflight.returncode == 0,
        f"Metal preflight should return the command result: {metal_preflight}",
    )
    require(
        captured_commands,
        "Metal preflight should invoke run_command",
    )
    require(
        captured_commands[0] == expected_metal_preflight_command,
        f"Metal preflight should run the reusable command builder: {captured_commands[0]}",
    )
    captured_commands.clear()
    original_run_command = capture.run_command
    capture.run_command = fake_run_command
    try:
        manifest_validation = capture.run_capture_manifest_validation(
            artifact_dir,
            SimpleNamespace(run_release_proof=True),
        )
    finally:
        capture.run_command = original_run_command
    require(
        manifest_validation is not None and manifest_validation.returncode == 0,
        f"Capture manifest validation should return the command result: {manifest_validation}",
    )
    require(
        captured_commands[0] == capture_manifest_validation,
        f"Capture manifest validation should run the checker command: {captured_commands[0]}",
    )

    with tempfile.TemporaryDirectory(prefix="desktop-proof-capture-") as temporary:
        manifest_root = Path(temporary).resolve()
        manifest_artifact_dir, manifest_run_dir = capture.capture_paths(
            manifest_root,
            "Proof Manifest",
            "1234567890abcdef",
            "20260529T010203.000000Z",
        )
        manifest_args = SimpleNamespace(
            allow_non_metal=False,
            allow_synthetic=False,
            color_app=color_app,
            label="Proof Manifest",
            log_level="info",
            metal_iterations=3,
            metal_timeout=120,
            no_validate=False,
            output_root=manifest_root,
            parity_app=parity_app,
            preview_app=preview_app,
            preview_corpus=preview_corpus,
            print_only=False,
            release_report=None,
            run_release_proof=True,
            skip_metal_preflight=False,
            expected_source_image=expected_source_image,
        )
        manifest = capture.initial_manifest(
            manifest_artifact_dir,
            manifest_run_dir,
            None,
            manifest_args,
        )
        require(manifest["status"] == "initialized", "Initial manifest status should be initialized")
        require(
            len(manifest["proof_steps"]) == len(capture.PROOF_STEPS),
            "Manifest should preserve the proof gesture steps",
        )
        require(
            manifest["commands"]["capture_manifest_validation"]
            == capture.capture_manifest_validation_command(manifest_artifact_dir),
            f"Manifest capture validation command is wrong: {manifest['commands']}",
        )
        require(
            manifest["commands"]["desktop_validation"][2]
            == "scripts/perf/run-desktop-preview-proof-gate.py",
            f"Manifest desktop validation command is wrong: {manifest['commands']}",
        )
        require(
            "scripts/perf/run-preview-release-proof.py" in manifest["commands"]["release_proof"],
            f"Manifest release command is wrong: {manifest['commands']}",
        )
        require(
            manifest["inputs"]["expected_source_image"] == str(expected_source_image),
            f"Manifest expected source image input is wrong: {manifest['inputs']}",
        )
        require(
            manifest["commands"]["desktop_validation"][
                manifest["commands"]["desktop_validation"].index("--expected-source-image") + 1
            ]
            == str(expected_source_image),
            f"Manifest desktop validation source flag is wrong: {manifest['commands']}",
        )
        require(
            manifest["commands"]["release_proof"][
                manifest["commands"]["release_proof"].index("--expected-source-image") + 1
            ]
            == str(expected_source_image),
            f"Manifest release proof source flag is wrong: {manifest['commands']}",
        )
        require(
            manifest["commands"]["metal_preflight"]
            == capture.metal_preflight_command(manifest_args),
            f"Manifest Metal preflight command is wrong: {manifest['commands']}",
        )
        command_result = capture.subprocess.CompletedProcess(
            ["proof"],
            7,
            (
                "desktop_preview_proof_gate=failed\n"
                "preview_release_proof_report=/tmp/release-proof.json\n"
            ),
            "",
        )
        markers = capture.record_command_result(manifest, "release_proof", command_result)
        require(
            markers["desktop_preview_proof_gate"] == "failed",
            f"Manifest command markers were not parsed: {markers}",
        )
        require(
            manifest["release_proof"]["command"] == ["proof"],
            f"Manifest command args were not recorded: {manifest}",
        )
        require(
            manifest["release_proof"]["exit_code"] == 7,
            f"Manifest command exit code was not recorded: {manifest}",
        )
        require(
            manifest["release_report"] == "/tmp/release-proof.json",
            f"Manifest release report path was not promoted: {manifest}",
        )
        capture.write_manifest(manifest_artifact_dir, manifest)
        loaded = json.loads((manifest_artifact_dir / "manifest.json").read_text(encoding="utf-8"))
        require(
            loaded["run_dir"] == str(manifest_run_dir),
            f"Written manifest run_dir mismatch: {loaded}",
        )

    print("desktop_preview_proof_capture_self_test=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
