#!/usr/bin/env python3
"""Self-test color adjustment gate checker policy construction."""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
GATE = REPO_ROOT / "scripts/perf/run-color-adjustment-gate.py"


def load_gate() -> Any:
    spec = importlib.util.spec_from_file_location("color_adjustment_gate", GATE)
    if spec is None or spec.loader is None:
        raise SystemExit(f"Unable to import {GATE}")
    module = importlib.util.module_from_spec(spec)
    sys.modules["color_adjustment_gate"] = module
    spec.loader.exec_module(module)
    return module


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def contains_metric(command: list[str], flag: str, value: str) -> bool:
    return any(
        argument == flag and index + 1 < len(command) and command[index + 1] == value
        for index, argument in enumerate(command)
    )


def main() -> int:
    gate = load_gate()
    captured_commands: list[list[str]] = []

    def fake_run_command(command: list[str]) -> Any:
        captured_commands.append(command)
        return gate.subprocess.CompletedProcess(command, 0, "thresholds=passed\n", "")

    original_run_command = gate.run_command
    gate.run_command = fake_run_command
    try:
        gate.validate_adjustment_cases(
            Path("/tmp/artifact"),
            3,
            "spatial-texture-only",
            gate.TEXTURE_ONLY_CASES,
            require_gpu_work=False,
            require_texture_only_shape=True,
        )
        cpu_commands = list(captured_commands)
        captured_commands.clear()
        gate.validate_adjustment_cases(
            Path("/tmp/artifact"),
            3,
            "spatial-texture-only",
            gate.TEXTURE_ONLY_CASES,
            require_gpu_work=True,
            require_texture_only_shape=True,
        )
        gpu_commands = list(captured_commands)
    finally:
        gate.run_command = original_run_command

    require(cpu_commands, "CPU texture-only validation did not build checker commands")
    require(gpu_commands, "GPU texture-only validation did not build checker commands")

    for command in cpu_commands:
        require(
            contains_metric(command, "--metric-min", "adjustment_spatial_texture_blur_avg_ms=1"),
            f"CPU fallback texture-only gate should require CPU texture blur work: {command}",
        )
        require(
            not contains_metric(command, "--metric-max", "adjustment_spatial_texture_blur_avg_ms=0"),
            f"CPU fallback texture-only gate should not require zero CPU texture blur: {command}",
        )

    for command in gpu_commands:
        require(
            contains_metric(command, "--metric-max", "adjustment_spatial_texture_blur_avg_ms=0"),
            f"GPU texture-only gate should require zero CPU texture blur work: {command}",
        )
        require(
            contains_metric(command, "--metric-min", "adjustment_gpu_command_buffer_count_avg=1"),
            f"GPU texture-only gate should require command-buffer work: {command}",
        )
        require(
            contains_metric(command, "--metric-min", "adjustment_gpu_spatial_texture_blur_dispatch_count_avg=1"),
            f"GPU texture-only gate should require texture blur dispatches: {command}",
        )
        require(
            not contains_metric(command, "--metric-min", "adjustment_spatial_texture_blur_avg_ms=1"),
            f"GPU texture-only gate should not require CPU texture blur work: {command}",
        )

    print("color_adjustment_gate_self_test=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
