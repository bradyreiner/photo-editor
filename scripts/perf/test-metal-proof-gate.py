#!/usr/bin/env python3
"""Self-test Metal proof gate control-flow helpers without requiring Metal hardware."""

from __future__ import annotations

import contextlib
import importlib.util
import io
from pathlib import Path
from types import SimpleNamespace
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
METAL_GATE = REPO_ROOT / "scripts/perf/run-metal-proof-gate.py"


def load_metal_gate() -> Any:
    spec = importlib.util.spec_from_file_location("metal_proof_gate", METAL_GATE)
    if spec is None or spec.loader is None:
        raise SystemExit(f"Unable to import {METAL_GATE}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def main() -> int:
    gate = load_metal_gate()

    require(gate.bool_marker(True) == "true", "True marker formatting failed")
    require(gate.bool_marker(False) == "false", "False marker formatting failed")

    original_docker_detected = gate.docker_detected
    gate.docker_detected = lambda: True
    try:
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            gate.print_environment_markers()
        markers = dict(line.split("=", 1) for line in buffer.getvalue().splitlines())
    finally:
        gate.docker_detected = original_docker_detected

    require(
        markers["metal_proof_docker_detected"] == "true",
        f"Docker marker was not emitted: {markers}",
    )
    require(
        markers["metal_proof_ci_detected"] in {"true", "false"},
        f"CI marker should be a boolean string: {markers}",
    )
    require(
        markers["metal_proof_restricted_context_possible"] in {"true", "false"},
        f"Restricted-context marker should be a boolean string: {markers}",
    )

    captured_commands: list[list[str]] = []

    def fake_run_command(command: list[str]) -> Any:
        captured_commands.append(command)
        return gate.subprocess.CompletedProcess(command, 0, "color_adjustment_gate=passed\n", "")

    original_run_command = gate.run_command
    gate.run_command = fake_run_command
    try:
        gate.run_color_adjustment_gpu_gate(
            SimpleNamespace(
                app=Path("/unused"),
                color_app=METAL_GATE,
                output_root=Path("/tmp/metal-proof-output"),
                iterations=7,
                timeout=33,
                texture_only_artifact=Path("/tmp/texture-artifact"),
                spatial_detail_artifact=Path("/tmp/spatial-artifact"),
            )
        )
    finally:
        gate.run_command = original_run_command

    require(captured_commands, "GPU color gate did not invoke run_command")
    command = captured_commands[0]
    require(
        "scripts/perf/run-color-adjustment-gate.py" in command,
        f"GPU color gate command missing runner: {command}",
    )
    require("--require-gpu-work" in command, f"GPU color gate must require GPU work: {command}")
    require(
        command[command.index("--iterations") + 1] == "7",
        f"GPU color gate command missing iteration count: {command}",
    )
    require(
        command[command.index("--timeout") + 1] == "33",
        f"GPU color gate command missing timeout: {command}",
    )
    require(
        command[command.index("--texture-only-artifact") + 1]
        == str(Path("/tmp/texture-artifact").resolve()),
        f"GPU color gate command missing texture artifact: {command}",
    )
    require(
        command[command.index("--spatial-detail-artifact") + 1]
        == str(Path("/tmp/spatial-artifact").resolve()),
        f"GPU color gate command missing spatial artifact: {command}",
    )

    print("metal_proof_gate_self_test=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
