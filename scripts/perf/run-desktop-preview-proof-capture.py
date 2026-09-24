#!/usr/bin/env python3
"""Launch the desktop app for a real max-resolution preview proof capture."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import uuid
from datetime import datetime, timezone
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_APP = REPO_ROOT / "build/dev-macos-debug/apps/desktop/Looksy.app/Contents/MacOS/Looksy"
DEFAULT_PREVIEW_APP = REPO_ROOT / "build/dev-macos-debug/tests/preview_benchmark_driver"
DEFAULT_PREVIEW_CORPUS = REPO_ROOT / "perf-corpora/preview-synthetic-v1/manifest.json"
DEFAULT_COLOR_APP = REPO_ROOT / "build/dev-macos-debug/tests/color_adjustment_benchmark_driver"
DEFAULT_PARITY_APP = REPO_ROOT / "build/dev-macos-debug/tests/render_parity_test"
DEFAULT_OUTPUT_ROOT = REPO_ROOT / "perf-artifacts"
PROOF_STEPS = [
    "Open a real non-synthetic image with a source edge >= 2048 px.",
    "Wait for the first max-resolution preview to settle.",
    "Zoom past 100% and pan into a not-yet-visible region.",
    "Change a heavy edit control, such as texture, clarity, dehaze, or sharpening.",
    "Pan again while the edit/full preview work is active.",
    "Quit the app after the visible high-resolution tile appears.",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Launch PhotoEditor with an isolated observability run directory for "
            "the real desktop max-resolution preview proof, then validate the "
            "captured JSONL when the app exits."
        )
    )
    parser.add_argument("--app", type=Path, default=DEFAULT_APP)
    parser.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--label", default="desktop-preview-proof")
    parser.add_argument("--log-level", default="info")
    parser.add_argument(
        "--skip-metal-preflight",
        action="store_true",
        help="Do not run the quick Metal proof preflight before launching the app.",
    )
    parser.add_argument(
        "--metal-iterations",
        type=int,
        default=3,
        help="Iterations for the quick Metal preflight.",
    )
    parser.add_argument(
        "--metal-timeout",
        type=int,
        default=120,
        help="Timeout seconds for the quick Metal preflight.",
    )
    parser.add_argument(
        "--no-validate",
        action="store_true",
        help="Only capture the run; do not validate it after the app exits.",
    )
    parser.add_argument(
        "--print-only",
        action="store_true",
        help="Print the capture run directory and gesture steps without launching the app.",
    )
    parser.add_argument(
        "--allow-synthetic",
        action="store_true",
        help="Pass through to the desktop proof gate. Smoke only.",
    )
    parser.add_argument(
        "--allow-non-metal",
        action="store_true",
        help="Pass through to the desktop proof gate. Smoke only.",
    )
    parser.add_argument(
        "--run-release-proof",
        action="store_true",
        help="After desktop validation passes, run the full preview release proof bundle.",
    )
    parser.add_argument("--preview-app", type=Path, default=DEFAULT_PREVIEW_APP)
    parser.add_argument("--preview-corpus", type=Path, default=DEFAULT_PREVIEW_CORPUS)
    parser.add_argument("--color-app", type=Path, default=DEFAULT_COLOR_APP)
    parser.add_argument("--parity-app", type=Path, default=DEFAULT_PARITY_APP)
    parser.add_argument(
        "--release-report",
        type=Path,
        help="Optional JSON report path passed to run-preview-release-proof.py.",
    )
    parser.add_argument(
        "--expected-source-image",
        type=Path,
        help=(
            "Optional real image path that the desktop validation and release proof "
            "must observe in render logs."
        ),
    )
    return parser.parse_args()


def require_positive(value: int, label: str) -> None:
    if value <= 0:
        raise SystemExit(f"{label} must be positive")


def missing_input_result(label: str, path: Path) -> subprocess.CompletedProcess[str]:
    resolved = path.expanduser().resolve()
    return subprocess.CompletedProcess(
        [f"{label} missing", str(resolved)],
        2,
        (
            f"desktop_preview_proof_capture_error={label}_missing\n"
            f"desktop_preview_proof_capture_error_path={resolved}\n"
        ),
        "",
    )


def invalid_release_mode_options_result(options: list[str]) -> subprocess.CompletedProcess[str]:
    joined_options = ",".join(options)
    return subprocess.CompletedProcess(
        ["release_mode_options_invalid", joined_options],
        2,
        (
            "desktop_preview_proof_capture_error=release_mode_options_invalid\n"
            f"desktop_preview_proof_capture_error_options={joined_options}\n"
        ),
        "",
    )


def release_report_outside_artifact_result(
    release_report: Path,
    artifact_dir: Path,
) -> subprocess.CompletedProcess[str]:
    resolved_report = release_report.expanduser().resolve()
    resolved_artifact_dir = artifact_dir.expanduser().resolve()
    return subprocess.CompletedProcess(
        ["release_report_outside_artifact", str(resolved_report), str(resolved_artifact_dir)],
        2,
        (
            "desktop_preview_proof_capture_error=release_report_outside_artifact\n"
            f"desktop_preview_proof_capture_error_path={resolved_report}\n"
            f"desktop_preview_proof_capture_artifact_dir={resolved_artifact_dir}\n"
        ),
        "",
    )


def timestamp_slug() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")


def safe_label(label: str) -> str:
    cleaned = "".join(character if character.isalnum() or character in "-_" else "-" for character in label)
    cleaned = cleaned.strip("-_")
    return cleaned or "desktop-preview-proof"


def capture_paths(output_root: Path, label: str, session_id: str, timestamp: str) -> tuple[Path, Path]:
    safe = safe_label(label)
    artifact_dir = output_root.expanduser().resolve() / f"{timestamp}-{safe}-{session_id[:8]}"
    run_dir = artifact_dir / "logs" / "runs" / f"{safe}-{session_id[:8]}"
    return artifact_dir, run_dir


def run_command(command: list[str], *, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    print("+ " + " ".join(command), flush=True)
    result = subprocess.run(
        command,
        cwd=REPO_ROOT,
        env=env,
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


def write_manifest(artifact_dir: Path, manifest: dict[str, object]) -> None:
    artifact_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = artifact_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"desktop_preview_proof_capture_manifest={manifest_path}")


def initial_manifest(
    artifact_dir: Path,
    run_dir: Path,
    app: Path | None,
    args: argparse.Namespace,
) -> dict[str, object]:
    validation_command = desktop_validation_command(run_dir, args)
    release_command = (
        release_proof_command(run_dir, artifact_dir, args) if args.run_release_proof else []
    )
    capture_validation_command = (
        capture_manifest_validation_command(artifact_dir) if args.run_release_proof else []
    )
    metal_preflight = [] if args.skip_metal_preflight else metal_preflight_command(args)
    return {
        "artifact_dir": str(artifact_dir),
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "proof_steps": PROOF_STEPS,
        "run_dir": str(run_dir),
        "status": "initialized",
        "inputs": {
            "app": str(app) if app is not None else None,
            "color_app": str(args.color_app.expanduser().resolve()),
            "label": args.label,
            "log_level": args.log_level,
            "output_root": str(args.output_root.expanduser().resolve()),
            "parity_app": str(args.parity_app.expanduser().resolve()),
            "preview_app": str(args.preview_app.expanduser().resolve()),
            "preview_corpus": str(args.preview_corpus.expanduser().resolve()),
            "release_report": (
                str(args.release_report.expanduser().resolve())
                if args.release_report is not None
                else None
            ),
            "expected_source_image": (
                str(args.expected_source_image.expanduser().resolve())
                if args.expected_source_image is not None
                else None
            ),
        },
        "options": {
            "allow_non_metal": args.allow_non_metal,
            "allow_synthetic": args.allow_synthetic,
            "metal_iterations": args.metal_iterations,
            "metal_timeout": args.metal_timeout,
            "no_validate": args.no_validate,
            "print_only": args.print_only,
            "run_release_proof": args.run_release_proof,
            "skip_metal_preflight": args.skip_metal_preflight,
        },
        "commands": {
            "capture_manifest_validation": capture_validation_command,
            "desktop_validation": validation_command,
            "metal_preflight": metal_preflight,
            "release_proof": release_command,
        },
    }


def record_command_result(
    manifest: dict[str, object],
    key: str,
    result: subprocess.CompletedProcess[str],
) -> dict[str, str]:
    markers = parse_key_values(result.stdout or "")
    manifest[key] = {
        "command": result.args,
        "exit_code": result.returncode,
        "markers": markers,
    }
    if key == "release_proof" and "preview_release_proof_report" in markers:
        manifest["release_report"] = markers["preview_release_proof_report"]
    return markers


def print_proof_steps(run_dir: Path) -> None:
    print()
    print("desktop_preview_proof_capture_run_dir=" + str(run_dir))
    print("desktop_preview_proof_capture_steps:")
    for index, step in enumerate(PROOF_STEPS, start=1):
        print(f"  {index}. {step}")
    print()


def run_metal_preflight(args: argparse.Namespace) -> subprocess.CompletedProcess[str] | None:
    if args.skip_metal_preflight:
        print("desktop_preview_proof_capture_metal_preflight=skipped")
        return None
    require_positive(args.metal_iterations, "--metal-iterations")
    require_positive(args.metal_timeout, "--metal-timeout")
    return run_command(metal_preflight_command(args))


def launch_app(
    app: Path,
    run_dir: Path,
    log_level: str,
    expected_source_image: Path | None,
) -> int:
    env = os.environ.copy()
    env.update(
        {
            "PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY": "1",
            "PHOTO_EDITOR_LOG_DIR": str(run_dir.parents[1]),
            "PHOTO_EDITOR_OBSERVABILITY_RUN_DIR": str(run_dir),
            "PHOTO_EDITOR_SESSION_ID": run_dir.name,
            "PHOTO_EDITOR_LOG_LEVEL": log_level,
        }
    )
    if expected_source_image is not None:
        env["PHOTO_EDITOR_DESKTOP_PROOF_SOURCE_IMAGE"] = str(
            expected_source_image.expanduser().resolve()
        )
    run_dir.mkdir(parents=True, exist_ok=True)
    if expected_source_image is not None:
        print(
            "desktop_preview_proof_capture_expected_source_image="
            f"{expected_source_image.expanduser().resolve()}"
        )
    print("+ " + str(app), flush=True)
    process = subprocess.Popen([str(app)], cwd=REPO_ROOT, env=env)
    try:
        return process.wait()
    except KeyboardInterrupt:
        process.terminate()
        return process.wait()


def validate_run(run_dir: Path, args: argparse.Namespace) -> subprocess.CompletedProcess[str] | None:
    if args.no_validate:
        print("desktop_preview_proof_capture_validation=skipped")
        return None
    return run_command(desktop_validation_command(run_dir, args))


def desktop_validation_command(run_dir: Path, args: argparse.Namespace) -> list[str]:
    command = [
        sys.executable,
        "-B",
        "scripts/perf/run-desktop-preview-proof-gate.py",
        str(run_dir),
    ]
    if args.allow_synthetic:
        command.append("--allow-synthetic")
    if args.allow_non_metal:
        command.append("--allow-non-metal")
    if args.expected_source_image is not None:
        command.extend(
            [
                "--expected-source-image",
                str(args.expected_source_image.expanduser().resolve()),
            ]
        )
    return command


def metal_preflight_command(args: argparse.Namespace) -> list[str]:
    return [
        sys.executable,
        "-B",
        "scripts/perf/run-metal-proof-gate.py",
        "--color-app",
        str(args.color_app.expanduser().resolve()),
        "--parity-app",
        str(args.parity_app.expanduser().resolve()),
        "--output-root",
        str(args.output_root.expanduser().resolve()),
        "--iterations",
        str(args.metal_iterations),
        "--timeout",
        str(args.metal_timeout),
    ]


def release_proof_command(run_dir: Path, artifact_dir: Path, args: argparse.Namespace) -> list[str]:
    command = [
        sys.executable,
        "-B",
        "scripts/perf/run-preview-release-proof.py",
        "--preview-app",
        str(args.preview_app.expanduser().resolve()),
        "--preview-corpus",
        str(args.preview_corpus.expanduser().resolve()),
        "--color-app",
        str(args.color_app.expanduser().resolve()),
        "--parity-app",
        str(args.parity_app.expanduser().resolve()),
        "--desktop-proof-input",
        str(run_dir),
        "--output-root",
        str(artifact_dir),
    ]
    if args.release_report is not None:
        command.extend(["--report", str(args.release_report.expanduser().resolve())])
    if args.expected_source_image is not None:
        command.extend(
            [
                "--expected-source-image",
                str(args.expected_source_image.expanduser().resolve()),
            ]
        )
    return command


def capture_manifest_validation_command(artifact_dir: Path) -> list[str]:
    return [
        sys.executable,
        "-B",
        "scripts/perf/check-desktop-preview-proof-capture-manifest.py",
        str(artifact_dir),
    ]


def run_release_proof(
    run_dir: Path,
    artifact_dir: Path,
    args: argparse.Namespace,
) -> subprocess.CompletedProcess[str] | None:
    if not args.run_release_proof:
        return None
    return run_command(release_proof_command(run_dir, artifact_dir, args))


def run_capture_manifest_validation(
    artifact_dir: Path,
    args: argparse.Namespace,
) -> subprocess.CompletedProcess[str] | None:
    if not args.run_release_proof:
        return None
    return run_command(capture_manifest_validation_command(artifact_dir))


def preflight_release_inputs(args: argparse.Namespace) -> subprocess.CompletedProcess[str] | None:
    if not args.run_release_proof or args.print_only:
        return None
    for label, path in (
        ("preview_app", args.preview_app),
        ("preview_corpus", args.preview_corpus),
        ("color_app", args.color_app),
        ("parity_app", args.parity_app),
    ):
        if not path.expanduser().resolve().exists():
            return missing_input_result(label, path)
    if (
        args.expected_source_image is not None
        and not args.expected_source_image.expanduser().resolve().is_file()
    ):
        return missing_input_result("expected_source_image", args.expected_source_image)
    return None


def preflight_release_mode_options(args: argparse.Namespace) -> subprocess.CompletedProcess[str] | None:
    if not args.run_release_proof or args.print_only:
        return None
    invalid_options: list[str] = []
    if args.allow_synthetic:
        invalid_options.append("--allow-synthetic")
    if args.allow_non_metal:
        invalid_options.append("--allow-non-metal")
    if args.no_validate:
        invalid_options.append("--no-validate")
    if args.skip_metal_preflight:
        invalid_options.append("--skip-metal-preflight")
    return invalid_release_mode_options_result(invalid_options) if invalid_options else None


def preflight_release_report_path(
    args: argparse.Namespace,
    artifact_dir: Path,
) -> subprocess.CompletedProcess[str] | None:
    if not args.run_release_proof or args.print_only or args.release_report is None:
        return None
    resolved_report = args.release_report.expanduser().resolve()
    resolved_artifact_dir = artifact_dir.expanduser().resolve()
    try:
        resolved_report.relative_to(resolved_artifact_dir)
    except ValueError:
        return release_report_outside_artifact_result(args.release_report, artifact_dir)
    return None


def preflight_app(args: argparse.Namespace) -> subprocess.CompletedProcess[str] | None:
    if args.print_only:
        return None
    resolved = args.app.expanduser().resolve()
    if not resolved.is_file():
        return missing_input_result("app", args.app)
    return None


def main() -> int:
    args = parse_args()
    app = None if args.print_only else args.app.expanduser().resolve()
    session_id = uuid.uuid4().hex
    artifact_dir, run_dir = capture_paths(args.output_root, args.label, session_id, timestamp_slug())

    manifest = initial_manifest(artifact_dir, run_dir, app, args)
    write_manifest(artifact_dir, manifest)
    release_mode_options = preflight_release_mode_options(args)
    if release_mode_options is not None:
        record_command_result(manifest, "release_mode_options_preflight", release_mode_options)
        manifest["status"] = "release_mode_options_failed"
        write_manifest(artifact_dir, manifest)
        raise SystemExit(release_mode_options.returncode)
    release_report_preflight = preflight_release_report_path(args, artifact_dir)
    if release_report_preflight is not None:
        record_command_result(manifest, "release_report_preflight", release_report_preflight)
        manifest["status"] = "release_report_preflight_failed"
        write_manifest(artifact_dir, manifest)
        raise SystemExit(release_report_preflight.returncode)
    app_preflight = preflight_app(args)
    if app_preflight is not None:
        record_command_result(manifest, "app_preflight", app_preflight)
        manifest["status"] = "app_preflight_failed"
        write_manifest(artifact_dir, manifest)
        raise SystemExit(app_preflight.returncode)
    release_input_preflight = preflight_release_inputs(args)
    if release_input_preflight is not None:
        record_command_result(manifest, "release_input_preflight", release_input_preflight)
        manifest["status"] = "release_input_preflight_failed"
        write_manifest(artifact_dir, manifest)
        raise SystemExit(release_input_preflight.returncode)
    metal_preflight = run_metal_preflight(args)
    if metal_preflight is not None:
        record_command_result(manifest, "metal_preflight", metal_preflight)
        if metal_preflight.returncode != 0:
            manifest["status"] = "metal_preflight_failed"
            write_manifest(artifact_dir, manifest)
            raise SystemExit(metal_preflight.returncode)
        manifest["status"] = "metal_preflight_passed"
        write_manifest(artifact_dir, manifest)
    print_proof_steps(run_dir)
    if args.print_only:
        manifest["status"] = "print_only"
        write_manifest(artifact_dir, manifest)
        print("desktop_preview_proof_capture_launch=skipped")
        return 0
    app_status = launch_app(app, run_dir, args.log_level, args.expected_source_image)
    if app_status != 0:
        manifest["app_exit_code"] = app_status
        manifest["status"] = "app_failed"
        write_manifest(artifact_dir, manifest)
        raise SystemExit(app_status)
    manifest["app_exit_code"] = app_status
    manifest["status"] = "app_exited"
    write_manifest(artifact_dir, manifest)
    validation = validate_run(run_dir, args)
    if validation is not None:
        record_command_result(manifest, "desktop_validation", validation)
        if validation.returncode != 0:
            manifest["status"] = "desktop_validation_failed"
            write_manifest(artifact_dir, manifest)
            raise SystemExit(validation.returncode)
    manifest["status"] = "desktop_validated" if not args.no_validate else "captured"
    write_manifest(artifact_dir, manifest)
    release_proof = run_release_proof(run_dir, artifact_dir, args)
    if release_proof is not None:
        record_command_result(manifest, "release_proof", release_proof)
        if release_proof.returncode != 0:
            manifest["status"] = "release_proof_failed"
            write_manifest(artifact_dir, manifest)
            raise SystemExit(release_proof.returncode)
        manifest["status"] = "release_proof_passed"
        write_manifest(artifact_dir, manifest)
        capture_manifest_validation = run_capture_manifest_validation(artifact_dir, args)
        if capture_manifest_validation is not None:
            record_command_result(
                manifest,
                "capture_manifest_validation",
                capture_manifest_validation,
            )
            if capture_manifest_validation.returncode != 0:
                manifest["status"] = "capture_manifest_validation_failed"
                write_manifest(artifact_dir, manifest)
                raise SystemExit(capture_manifest_validation.returncode)
            manifest["status"] = "release_proof_passed"
            write_manifest(artifact_dir, manifest)
    print(f"desktop_preview_proof_capture_artifact={artifact_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
