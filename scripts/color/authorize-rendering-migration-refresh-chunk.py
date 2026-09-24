#!/usr/bin/env python3
"""Create a native-render authorization from one approved v2 refresh chunk."""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from pathlib import Path


VALIDATOR_PATH = Path(__file__).resolve().with_name(
    "validate-rendering-migration-refresh-chunk.py"
)
AUTHORIZATION_CONTRACT = (
    "photo-editor-rendering-migration-refresh-chunk-authorization:v1"
)


def fail(message: str) -> int:
    print(f"error={message}", file=sys.stderr)
    return 1


def load_validator():
    specification = importlib.util.spec_from_file_location(
        "framehouse_rendering_migration_chunk_validator", VALIDATOR_PATH
    )
    if specification is None or specification.loader is None:
        raise ValueError("refresh chunk validator is unavailable")
    validator = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(validator)
    return validator


def authorize(
    plan_path: Path,
    chunk_index: int,
    executable: Path,
    output: Path,
) -> int:
    plan_path = plan_path.resolve()
    executable = executable.resolve()
    output = output.resolve()
    if output.exists():
        return fail(f"output already exists: {output}")
    if chunk_index <= 0:
        return fail("chunk index must be positive")
    validator = load_validator()
    plan, producer = validator.validate_plan(plan_path, executable)
    chunks = plan["chunks"]
    if chunk_index > len(chunks):
        return fail("chunk index is absent from the approved refresh plan")
    chunk = chunks[chunk_index - 1]
    authorization = {
        "contract": AUTHORIZATION_CONTRACT,
        "status": "authorized",
        "producer": producer,
        "planSha256": plan["planSha256"],
        "planFileSha256": validator.file_sha256(plan_path),
        "chunkIndex": chunk_index,
        "assetIds": [asset["assetId"] for asset in chunk["assets"]],
        "assetCount": chunk["assetCount"],
        "totalPixels": chunk["totalPixels"],
        "overBudgetSingleAsset": chunk["overBudgetSingleAsset"],
    }
    authorization["authorizationSha256"] = validator.value_sha256(authorization)
    validator.publish_new_file(
        output,
        (json.dumps(authorization, indent=2, sort_keys=True) + "\n").encode("utf-8"),
    )
    print(f"authorization={output}")
    print(f"chunk_index={chunk_index}")
    print(f"assets={chunk['assetCount']}")
    print(f"authorization_sha256={authorization['authorizationSha256']}")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--chunk-index", type=int, required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        return authorize(args.plan, args.chunk_index, args.executable, args.output)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        return fail(str(exc))


if __name__ == "__main__":
    raise SystemExit(main())
