#!/usr/bin/env python3
"""Run and aggregate real cold/warm local-agent timing on one Mac tier."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
import statistics
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_TEST_BINARY = REPO_ROOT / "build/dev-macos-debug/tests/agent_local_model_test"
DEFAULT_APP_DATA_DIR = REPO_ROOT / "apps/desktop/resources"
DEADLINE_BY_TIER_MS = {
    "performance": 20_000,
    "balanced": 30_000,
    "constrained": 45_000,
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Collect checksum-bound qwen3-vl cold/warm receipts and write a "
            "p50/p95 tier assessment. Run on each supported Mac tier."
        )
    )
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--hardware-tier",
        choices=tuple(DEADLINE_BY_TIER_MS),
        required=True,
    )
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument(
        "--condition",
        choices=("both", "cold", "warm"),
        default="both",
    )
    parser.add_argument("--test-binary", type=Path, default=DEFAULT_TEST_BINARY)
    parser.add_argument("--app-data-dir", type=Path, default=DEFAULT_APP_DATA_DIR)
    parser.add_argument("--model-id", default="ollama-qwen3-vl-32b")
    parser.add_argument("--ollama-model", default="qwen3-vl:32b")
    parser.add_argument(
        "--aggregate-only",
        action="store_true",
        help="Validate and aggregate cold-*.json/warm-*.json already in output-dir.",
    )
    return parser.parse_args()


def run_command(command: list[str], env: dict[str, str] | None = None) -> None:
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
    if result.returncode != 0:
        raise SystemExit(result.returncode)


def model_is_resident(ollama_model: str) -> bool:
    result = subprocess.run(
        ["ollama", "ps"],
        cwd=REPO_ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    return result.returncode == 0 and ollama_model in result.stdout


def run_live_sample(args: argparse.Namespace, condition: str, index: int) -> Path:
    receipt_path = args.output_dir / f"{condition}-{index:02d}.json"
    if condition == "cold":
        run_command(["ollama", "stop", args.ollama_model])
    env = os.environ.copy()
    env.update(
        {
            "PHOTO_EDITOR_RUN_LIVE_OLLAMA_SMOKE": "1",
            "PHOTO_EDITOR_LIVE_OLLAMA_APP_DATA_DIR": str(args.app_data_dir),
            "PHOTO_EDITOR_LIVE_OLLAMA_MODEL_ID": args.model_id,
            "PHOTO_EDITOR_LIVE_OLLAMA_RECEIPT_PATH": str(receipt_path),
        }
    )
    run_command([str(args.test_binary)], env=env)
    return receipt_path


def load_receipt(path: Path) -> dict[str, Any]:
    try:
        receipt = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"invalid live receipt {path}: {error}") from error
    required_true = (
        receipt.get("contract") == "agent-live-local-runtime-smoke-v1"
        and receipt.get("localOnly") is True
        and receipt.get("cloudUsed") is False
        and receipt.get("automaticCommitAllowed") is False
        and receipt.get("photographerApprovalRequired") is True
        and receipt.get("planning", {}).get("ok") is True
        and receipt.get("planning", {}).get("imageIncluded") is True
        and receipt.get("planning", {}).get("fallbackUsed") is False
        and receipt.get("proofCritique", {}).get("ok") is True
        and receipt.get("proofCritique", {}).get("imageIncluded") is True
    )
    if not required_true:
        raise SystemExit(f"live receipt failed local/image/approval guardrails: {path}")
    for key in ("planning", "proofCritique"):
        elapsed = receipt[key].get("elapsedMs")
        if not isinstance(elapsed, int) or elapsed <= 0:
            raise SystemExit(f"live receipt has invalid {key}.elapsedMs: {path}")
    return receipt


def nearest_rank(values: list[int], percentile: float) -> int:
    if not values:
        raise ValueError("percentile requires at least one value")
    ordered = sorted(values)
    rank = max(1, math.ceil(percentile * len(ordered)))
    return ordered[rank - 1]


def summarize(values: list[int]) -> dict[str, int | float | list[int]]:
    return {
        "samples": values,
        "min": min(values),
        "p50": nearest_rank(values, 0.50),
        "p95": nearest_rank(values, 0.95),
        "max": max(values),
        "mean": statistics.fmean(values),
    }


def summarize_condition(receipts: list[dict[str, Any]]) -> dict[str, Any]:
    planning = [int(receipt["planning"]["elapsedMs"]) for receipt in receipts]
    critique = [int(receipt["proofCritique"]["elapsedMs"]) for receipt in receipts]
    total = [plan + proof for plan, proof in zip(planning, critique, strict=True)]
    return {
        "planningMs": summarize(planning),
        "proofCritiqueMs": summarize(critique),
        "totalMs": summarize(total),
    }


def installed_memory_bytes() -> int:
    result = subprocess.run(
        ["sysctl", "-n", "hw.memsize"],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )
    try:
        return int(result.stdout.strip()) if result.returncode == 0 else 0
    except ValueError:
        return 0


def hardware_tier_for_memory(memory_bytes: int) -> str:
    memory_gib = memory_bytes // (1024**3) if memory_bytes > 0 else 0
    if memory_gib >= 64:
        return "performance"
    if memory_gib >= 32:
        return "balanced"
    return "constrained"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def receipt_paths(output_dir: Path, condition: str, samples: int) -> list[Path]:
    paths = []
    for index in range(1, samples + 1):
        padded = output_dir / f"{condition}-{index:02d}.json"
        legacy_unpadded = output_dir / f"{condition}-{index}.json"
        paths.append(padded if padded.is_file() else legacy_unpadded)
    missing = [str(path) for path in paths if not path.is_file()]
    if missing:
        raise SystemExit("missing live receipts: " + ", ".join(missing))
    return paths


def build_aggregate(
    args: argparse.Namespace,
    cold_paths: list[Path],
    warm_paths: list[Path],
) -> dict[str, Any]:
    cold_receipts = [load_receipt(path) for path in cold_paths]
    warm_receipts = [load_receipt(path) for path in warm_paths]
    representative = (cold_receipts or warm_receipts)[0]
    memory_bytes = installed_memory_bytes()
    detected_tier = hardware_tier_for_memory(memory_bytes)
    deadline_ms = DEADLINE_BY_TIER_MS[args.hardware_tier]
    aggregate: dict[str, Any] = {
        "contract": "agent-live-local-runtime-tier-benchmark-v1",
        "capturedAtUtc": datetime.now(timezone.utc).isoformat(),
        "host": {
            "architecture": platform.machine(),
            "macOSVersion": platform.mac_ver()[0],
            "memoryBytes": memory_bytes,
            "memoryGiB": memory_bytes // (1024**3) if memory_bytes > 0 else 0,
            "hardwareTier": args.hardware_tier,
            "detectedHardwareTier": detected_tier,
        },
        "runtime": {
            "modelId": representative.get("modelId"),
            "ollamaModel": representative.get("ollamaModel"),
            "modelFamily": representative.get("modelFamily"),
            "parameterSize": representative.get("parameterSize"),
            "quantizationLevel": representative.get("quantizationLevel"),
        },
        "generation": {
            "contract": representative.get("planning", {}).get(
                "generationSchemaContract"
            ),
            "schemaByteCount": representative.get("planning", {}).get(
                "generationSchemaByteCount"
            ),
            "globalChangePolicy": representative.get("planning", {}).get(
                "globalChangePolicy"
            ),
            "currentRequestAuthoritative": True,
        },
        "method": {
            "sampleCountPerCondition": args.samples,
            "percentileMethod": "nearest-rank",
            "coldDefinition": "ollama stop immediately before each planning request",
            "warmDefinition": "same model remains resident across unchanged requests",
            "proofCritiqueConditionForColdRuns": (
                "warm follow-up after the cold planning request loaded the model"
            ),
        },
        "deadlineAssessment": {
            "contract": "agent-fast-draft-hardware-deadline-v1",
            "hardwareTier": args.hardware_tier,
            "deadlineMs": deadline_ms,
        },
        "guardrails": {
            "allRunsPassed": True,
            "allRunsLocalOnly": True,
            "allPlanningImageGrounded": True,
            "allProofCritiquesImageGrounded": True,
            "cloudUsed": False,
            "fallbackUsed": False,
            "automaticCommitAllowed": False,
            "photographerApprovalRequired": True,
        },
        "sampleReceipts": [
            {"path": path.name, "condition": "cold", "sha256": sha256(path)}
            for path in cold_paths
        ]
        + [
            {"path": path.name, "condition": "warm", "sha256": sha256(path)}
            for path in warm_paths
        ],
    }
    if cold_receipts:
        aggregate["cold"] = summarize_condition(cold_receipts)
        cold_planning = aggregate["cold"]["planningMs"]
        aggregate["deadlineAssessment"].update(
            {
                "deadlinePrecedesColdPlanningP50ByMs": cold_planning["p50"] - deadline_ms,
                "deadlinePrecedesColdPlanningP95ByMs": cold_planning["p95"] - deadline_ms,
            }
        )
    if warm_receipts:
        aggregate["warm"] = summarize_condition(warm_receipts)
        warm_planning = aggregate["warm"]["planningMs"]
        aggregate["deadlineAssessment"].update(
            {
                "deadlinePrecedesWarmPlanningP50ByMs": warm_planning["p50"] - deadline_ms,
                "deadlinePrecedesWarmPlanningP95ByMs": warm_planning["p95"] - deadline_ms,
            }
        )
    planning_summaries = [
        aggregate[condition]["planningMs"]
        for condition in ("cold", "warm")
        if condition in aggregate
    ]
    policy_accepted = all(
        summary["p50"] > deadline_ms for summary in planning_summaries
    )
    aggregate["deadlineAssessment"].update(
        {
            "policyAcceptedForThisTier": policy_accepted,
            "policyChangeRequired": not policy_accepted,
        }
    )
    return aggregate


def main() -> int:
    args = parse_args()
    if args.samples < 5:
        raise SystemExit("--samples must be at least 5 for p50/p95 evidence")
    args.output_dir = args.output_dir.expanduser().resolve()
    args.test_binary = args.test_binary.expanduser().resolve()
    args.app_data_dir = args.app_data_dir.expanduser().resolve()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    memory_bytes = installed_memory_bytes()
    if memory_bytes > 0:
        detected_tier = hardware_tier_for_memory(memory_bytes)
        if detected_tier != args.hardware_tier:
            raise SystemExit(
                f"--hardware-tier {args.hardware_tier} does not match the "
                f"installed-memory tier {detected_tier}"
            )

    if not args.aggregate_only:
        if not args.test_binary.is_file():
            raise SystemExit(f"missing agent_local_model_test: {args.test_binary}")
        if not (args.app_data_dir / "agent-models/models.json").is_file():
            raise SystemExit(f"missing agent model manifest under {args.app_data_dir}")
        if args.condition in ("both", "cold"):
            for index in range(1, args.samples + 1):
                run_live_sample(args, "cold", index)
        if args.condition in ("both", "warm"):
            if not model_is_resident(args.ollama_model):
                raise SystemExit(
                    "warm samples require a resident model; run --condition both or prime "
                    f"{args.ollama_model} first"
                )
            for index in range(1, args.samples + 1):
                run_live_sample(args, "warm", index)

    cold_paths = (
        receipt_paths(args.output_dir, "cold", args.samples)
        if args.condition in ("both", "cold")
        else []
    )
    warm_paths = (
        receipt_paths(args.output_dir, "warm", args.samples)
        if args.condition in ("both", "warm")
        else []
    )
    aggregate = build_aggregate(args, cold_paths, warm_paths)
    aggregate_path = args.output_dir / "aggregate-receipt.json"
    aggregate_path.write_text(
        json.dumps(aggregate, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"agent_live_tier_receipt={aggregate_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
