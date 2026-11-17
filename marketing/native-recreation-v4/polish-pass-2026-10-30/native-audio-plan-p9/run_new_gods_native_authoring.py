#!/usr/bin/env python3
"""Guard and execute the reviewed New Gods P9 native authoring plan.

Default mode verifies local source hashes and prints the planned work without
contacting the app. Passing --apply requires a fresh task ID and exact admitted
audio asset scope, then delegates every edit to author_native.py so all calls
use native task receipts and its append-only fsynced intent log.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys


HERE = Path(__file__).resolve().parent
REPO_ROOT = Path(__file__).resolve().parents[4]
NATIVE_RECREATION = REPO_ROOT / "marketing" / "native-recreation-v4"
PLAN_PATH = HERE / "new-gods-native-authoring-plan-p9.template.json"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def canonical_regular_file(path_text: str) -> Path:
    path = Path(path_text).resolve(strict=True)
    if not path.is_file():
        raise RuntimeError(f"Required source is not a regular file: {path}")
    return path


def verify_local_guards(plan: dict) -> list[str]:
    snapshot = plan.get("planningSnapshot", {})
    snapshot_path = canonical_regular_file(snapshot.get("path", ""))
    if sha256_file(snapshot_path) != snapshot.get("sha256"):
        raise RuntimeError("The protected P9 planning snapshot changed; rebuild the reviewed plan first.")
    if snapshot.get("documentHash") != plan.get("startingDocumentHash"):
        raise RuntimeError("The P9 planning snapshot hash and starting document guard disagree.")

    guards = plan.get("assetGuards")
    required_paths = plan.get("requiredAssetPathScope")
    if not isinstance(guards, list) or not isinstance(required_paths, list) or len(guards) != 6:
        raise RuntimeError("The plan must declare exactly six verified audio source paths.")
    verified = []
    paths = []
    for guard in guards:
        path = canonical_regular_file(guard.get("path", ""))
        expected_hash = guard.get("sha256")
        if not isinstance(expected_hash, str) or len(expected_hash) != 64:
            raise RuntimeError(f"Invalid audio source hash guard for {path}.")
        if sha256_file(path) != expected_hash:
            raise RuntimeError(f"Audio source bytes changed after plan review: {path}")
        verified.append(str(path))
        paths.append(str(path))
    if len(set(paths)) != 6 or sorted(paths) != sorted(required_paths):
        raise RuntimeError("The asset hash list and exact task asset path scope disagree.")
    if plan.get("sourceImportCount") != 55:
        raise RuntimeError("The plan no longer contains the reviewed 55-clip score.")
    return sorted(verified)


def _task_targets(task: dict) -> set[str]:
    targets: set[str] = set()
    for field in ("targetResourceIds",):
        value = task.get(field)
        if isinstance(value, list):
            targets.update(item for item in value if isinstance(item, str))
    for field in ("targetResourceId",):
        value = task.get(field)
        if isinstance(value, str):
            targets.add(value)
    scope = task.get("nativeTimelineScope", {})
    if isinstance(scope, dict):
        values = scope.get("targetIds", [])
        if isinstance(values, list):
            targets.update(item for item in values if isinstance(item, str))
    return targets


def verify_native_task(plan: dict, expected_task_id: str) -> None:
    sys.path.insert(0, str(NATIVE_RECREATION))
    from discover import unpack  # pylint: disable=import-outside-toplevel
    from native_mcp import call  # pylint: disable=import-outside-toplevel

    task = unpack(call("timeline.inspect_task", {}))
    if task.get("ok") is not True or task.get("active") is not True or task.get("status") != "running":
        raise RuntimeError("A running admitted Timeline task is required; no edits were sent.")
    if task.get("taskId") != expected_task_id:
        raise RuntimeError("The active task ID does not match the task ID supplied to this runner.")
    resource_id = plan.get("resourceId")
    if resource_id not in _task_targets(task):
        raise RuntimeError("The active task does not own the P9 Timeline resource.")
    scope = task.get("nativeTimelineScope")
    if not isinstance(scope, dict):
        raise RuntimeError("The active task has no immutable native Timeline path scope.")
    admitted = scope.get("assetPaths")
    if not isinstance(admitted, list):
        raise RuntimeError("The active task does not report its admitted audio source paths.")
    canonical_admitted = sorted(str(Path(path).resolve(strict=True)) for path in admitted)
    required = sorted(plan["requiredAssetPathScope"])
    if (len(canonical_admitted) != len(set(canonical_admitted)) or
            canonical_admitted != required):
        raise RuntimeError("The task's admitted file path set differs from the exact six-file audio plan.")


def write_bound_plan(plan: dict, task_id: str, run_dir: Path) -> Path:
    bound = dict(plan)
    bound["taskId"] = task_id
    bound["status"] = "bound-to-verified-task"
    bound["productionCallsMade"] = False
    run_dir.mkdir(parents=True, exist_ok=True)
    output = run_dir / "new-gods-native-authoring-plan.json"
    encoded = (json.dumps(bound, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    if output.exists():
        if output.read_bytes() != encoded:
            raise RuntimeError("A different plan already exists for this task ID; refusing to overwrite its receipt lineage.")
    else:
        temporary = output.with_suffix(output.suffix + ".tmp")
        with temporary.open("xb") as target:
            target.write(encoded)
            target.flush()
            os.fsync(target.fileno())
        os.replace(temporary, output)
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true", help="Run native edits after all local and task guards pass.")
    parser.add_argument("--task-id", help="Exact task ID returned by the fresh native task admission.")
    parser.add_argument("--session", help="Optional isolated Looksy MCP session JSON path.")
    parser.add_argument("--receipt-root", type=Path, default=HERE / "receipts",
                        help="Root folder for task-specific plans, receipts, and durable NDJSON intent logs.")
    args = parser.parse_args()

    plan = json.loads(PLAN_PATH.read_text())
    asset_paths = verify_local_guards(plan)
    if not args.apply:
        print(json.dumps({
            "status": "dry-run-verified",
            "nativeCallsMade": False,
            "resourceId": plan["resourceId"],
            "startingDocumentHash": plan["startingDocumentHash"],
            "snapshotSha256": plan["planningSnapshot"]["sha256"],
            "stepCount": len(plan["steps"]),
            "nativeAudioClipCount": plan["sourceImportCount"],
            "assetPaths": asset_paths,
        }, indent=2))
        return 0

    if not args.task_id or not re.fullmatch(r"[A-Za-z0-9_.-]{1,128}", args.task_id):
        raise RuntimeError("--apply requires the exact task ID, using only letters, digits, dot, underscore, or dash.")
    if args.session:
        session_path = Path(args.session).resolve(strict=True)
        os.environ["LOOKSY_NATIVE_MCP_SESSION"] = str(session_path)
    asset_paths = verify_local_guards(plan)
    verify_native_task(plan, args.task_id)
    asset_paths_after_task_read = verify_local_guards(plan)
    if asset_paths_after_task_read != asset_paths:
        raise RuntimeError("An audio asset changed during native task preflight; no edits were sent.")

    run_dir = args.receipt_root.resolve() / args.task_id
    bound_plan_path = write_bound_plan(plan, args.task_id, run_dir)
    sys.path.insert(0, str(NATIVE_RECREATION))
    import author_native  # pylint: disable=import-outside-toplevel

    receipt_path = run_dir / "receipt.json"
    intent_log_path = run_dir / "receipt.ndjson"
    author_native.execute(bound_plan_path, receipt_path, intent_log_path)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"New Gods native authoring stopped safely: {error}", file=sys.stderr)
        raise SystemExit(2)
