#!/usr/bin/env python3
"""Run the Metal-capable preview rendering proof gate."""

from __future__ import annotations

import argparse
import os
import platform
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_COLOR_APP = REPO_ROOT / "build/dev-macos-debug/tests/color_adjustment_benchmark_driver"
DEFAULT_PARITY_APP = REPO_ROOT / "build/dev-macos-debug/tests/render_parity_test"
DEFAULT_OUTPUT_ROOT = REPO_ROOT / "perf-artifacts"
SKIP_EXIT_CODE = 77


def bool_marker(value: bool) -> str:
    return "true" if value else "false"


def docker_detected() -> bool:
    if Path("/.dockerenv").exists():
        return True
    cgroup_path = Path("/proc/self/cgroup")
    if cgroup_path.exists():
        try:
            cgroup = cgroup_path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            return False
        lowered = cgroup.lower()
        return any(token in lowered for token in ("docker", "containerd", "kubepods"))
    return False


def print_environment_markers() -> None:
    print(f"metal_proof_host_system={platform.system() or 'unknown'}")
    print(f"metal_proof_host_machine={platform.machine() or 'unknown'}")
    print(f"metal_proof_python_platform={sys.platform}")
    print(f"metal_proof_docker_detected={bool_marker(docker_detected())}")
    print(
        "metal_proof_ci_detected="
        f"{bool_marker(any(os.environ.get(name) for name in ('CI', 'GITHUB_ACTIONS')))}"
    )
    print(
        "metal_proof_restricted_context_possible="
        f"{bool_marker(platform.system() == 'Darwin' and not docker_detected())}"
    )


def fail(message: str, code: int = 1) -> None:
    print("metal_proof_gate=failed")
    print(f"metal_proof_error={message}")
    raise SystemExit(code)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run the hardware-only Metal proof: CPU/GPU parity plus positive GPU "
            "work counters for the texture-only spatial path."
        )
    )
    parser.add_argument("--color-app", type=Path, default=DEFAULT_COLOR_APP)
    parser.add_argument("--parity-app", type=Path, default=DEFAULT_PARITY_APP)
    parser.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--iterations", type=int, default=30)
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument(
        "--texture-only-artifact",
        type=Path,
        help="Validate an existing texture-only color-adjustment artifact.",
    )
    parser.add_argument(
        "--spatial-detail-artifact",
        type=Path,
        help="Validate an existing spatial/detail color-adjustment artifact.",
    )
    parser.add_argument(
        "--allow-unavailable",
        action="store_true",
        help=(
            "Treat render_parity_test skip code 77 as a local smoke skip. "
            "Do not use this mode as Metal proof."
        ),
    )
    return parser.parse_args()


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


def require_existing_app(path: Path, label: str) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.exists():
        fail(f"{label} does not exist: {resolved}")
    return resolved


def run_parity_gate(args: argparse.Namespace) -> bool:
    parity_app = require_existing_app(args.parity_app, "--parity-app")
    result = run_command([str(parity_app)])
    if result.returncode == SKIP_EXIT_CODE:
        message = (
            "render_parity_test skipped because the GPU render backend is unavailable. "
            "Metal proof requires this test to run on a Metal-capable host."
        )
        if args.allow_unavailable:
            print(message)
            print("metal_proof_gate=skipped")
            print("metal_proof_skip_reason=gpu_backend_unavailable")
            return False
        fail("gpu_backend_unavailable")
    if result.returncode != 0:
        fail("render_parity_test_failed", result.returncode)
    print("metal_parity_gate=passed")
    return True


def run_color_adjustment_gpu_gate(args: argparse.Namespace) -> None:
    color_app = require_existing_app(args.color_app, "--color-app")
    command = [
        sys.executable,
        "-B",
        "scripts/perf/run-color-adjustment-gate.py",
        "--app",
        str(color_app),
        "--output-root",
        str(args.output_root.expanduser().resolve()),
        "--iterations",
        str(args.iterations),
        "--timeout",
        str(args.timeout),
        "--require-gpu-work",
    ]
    if args.texture_only_artifact is not None:
        command.extend(
            [
                "--texture-only-artifact",
                str(args.texture_only_artifact.expanduser().resolve()),
            ]
        )
    if args.spatial_detail_artifact is not None:
        command.extend(
            [
                "--spatial-detail-artifact",
                str(args.spatial_detail_artifact.expanduser().resolve()),
            ]
        )
    result = run_command(command)
    if result.returncode != 0:
        fail("color_adjustment_gpu_gate_failed", result.returncode)
    print("metal_color_adjustment_gpu_gate=passed")


def main() -> int:
    args = parse_args()
    print_environment_markers()
    if args.iterations <= 0:
        fail("--iterations must be positive")
    if args.timeout <= 0:
        fail("--timeout must be positive")

    if not run_parity_gate(args):
        return 0
    run_color_adjustment_gpu_gate(args)
    print("metal_proof_gate=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
