#!/usr/bin/env python3
"""Focused tests for the real local-agent tier benchmark runner."""

from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPO_ROOT / "scripts/perf/run-agent-live-tier-benchmark.py"


def load_module():
    spec = importlib.util.spec_from_file_location("run_agent_live_tier_benchmark", SCRIPT_PATH)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def sample_receipt(planning_ms: int, proof_ms: int) -> dict:
    return {
        "contract": "agent-live-local-runtime-smoke-v1",
        "modelId": "ollama-qwen3-vl-32b",
        "ollamaModel": "qwen3-vl:32b",
        "modelFamily": "qwen3vl",
        "parameterSize": "33.4B",
        "quantizationLevel": "Q4_K_M",
        "planning": {
            "ok": True,
            "imageIncluded": True,
            "fallbackUsed": False,
            "elapsedMs": planning_ms,
        },
        "proofCritique": {
            "ok": True,
            "imageIncluded": True,
            "elapsedMs": proof_ms,
        },
        "localOnly": True,
        "cloudUsed": False,
        "automaticCommitAllowed": False,
        "photographerApprovalRequired": True,
    }


def test_nearest_rank_and_summary() -> None:
    module = load_module()
    values = [50, 10, 30, 20, 40]
    assert module.nearest_rank(values, 0.50) == 30
    assert module.nearest_rank(values, 0.95) == 50
    assert module.hardware_tier_for_memory(96 * 1024**3) == "performance"
    assert module.hardware_tier_for_memory(48 * 1024**3) == "balanced"
    assert module.hardware_tier_for_memory(16 * 1024**3) == "constrained"
    summary = module.summarize(values)
    assert summary["min"] == 10
    assert summary["p50"] == 30
    assert summary["p95"] == 50
    assert summary["max"] == 50
    assert summary["mean"] == 30


def test_aggregate_only_contract() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        output_dir = Path(temp_dir)
        for condition, planning_base, proof_base in (
            ("cold", 80_000, 30_000),
            ("warm", 38_000, 8_000),
        ):
            for index in range(1, 6):
                receipt = sample_receipt(
                    planning_base + index * 1_000,
                    proof_base + index * 100,
                )
                (output_dir / f"{condition}-{index:02d}.json").write_text(
                    json.dumps(receipt), encoding="utf-8"
                )
        result = subprocess.run(
            [
                sys.executable,
                "-B",
                str(SCRIPT_PATH),
                "--output-dir",
                str(output_dir),
                "--hardware-tier",
                "balanced",
                "--aggregate-only",
            ],
            cwd=REPO_ROOT,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        assert result.returncode == 0, result.stdout
        aggregate = json.loads(
            (output_dir / "aggregate-receipt.json").read_text(encoding="utf-8")
        )
        assert aggregate["contract"] == "agent-live-local-runtime-tier-benchmark-v1"
        assert aggregate["cold"]["planningMs"]["p50"] == 83_000
        assert aggregate["cold"]["planningMs"]["p95"] == 85_000
        assert aggregate["warm"]["planningMs"]["p50"] == 41_000
        assert aggregate["warm"]["planningMs"]["p95"] == 43_000
        assert aggregate["deadlineAssessment"]["deadlineMs"] == 30_000
        assert aggregate["deadlineAssessment"]["deadlinePrecedesWarmPlanningP50ByMs"] == 11_000
        assert aggregate["deadlineAssessment"]["policyAcceptedForThisTier"] is True
        assert aggregate["deadlineAssessment"]["policyChangeRequired"] is False
        assert len(aggregate["sampleReceipts"]) == 10
        assert all(entry["sha256"] for entry in aggregate["sampleReceipts"])
        assert aggregate["guardrails"]["allRunsPassed"] is True


def test_guardrail_failure_is_rejected() -> None:
    module = load_module()
    with tempfile.TemporaryDirectory() as temp_dir:
        path = Path(temp_dir) / "invalid.json"
        receipt = sample_receipt(40_000, 8_000)
        receipt["planning"]["fallbackUsed"] = True
        path.write_text(json.dumps(receipt), encoding="utf-8")
        try:
            module.load_receipt(path)
        except SystemExit as error:
            assert "guardrails" in str(error)
        else:
            raise AssertionError("fallback receipt should fail closed")


def main() -> int:
    test_nearest_rank_and_summary()
    test_aggregate_only_contract()
    test_guardrail_failure_is_rejected()
    print("agent_live_tier_benchmark_tests=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
