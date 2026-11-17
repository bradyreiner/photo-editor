#!/usr/bin/env python3
"""Receipt-bound executor for the reviewed P13 preview/title correction.

Default mode only verifies the protected snapshot and local assets. Native
changes require --apply, a fresh exact Timeline document hash, an owned running
task, and four explicitly admitted existing video source paths.
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
REPO_ROOT = HERE.parents[3]
NATIVE_RECREATION = REPO_ROOT / "marketing" / "native-recreation-v4"
TEMPLATE_PATH = HERE / "preview-sync-p13.plan.template.json"
STORYBOARD_PATH = HERE / "preview-sync-p13.storyboard.json"
PLAN_VERIFIER_PATH = HERE / "verify_preview_sync_p13_plan.py"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"Cannot read valid plan JSON at {path}: {error}") from error
    if not isinstance(value, dict):
        raise RuntimeError(f"Expected a JSON object at {path}.")
    return value


def verify_local_plan() -> dict:
    if not PLAN_VERIFIER_PATH.is_file():
        raise RuntimeError("The P13 plan verifier is missing.")
    sys.path.insert(0, str(HERE))
    import verify_preview_sync_p13_plan  # pylint: disable=import-outside-toplevel

    template = read_json(TEMPLATE_PATH)
    verify_preview_sync_p13_plan.verify_plan(template)
    for guard in template.get("assetGuards", []):
        path = Path(guard["path"]).resolve(strict=True)
        if not path.is_file() or sha256_file(path) != guard.get("sha256"):
            raise RuntimeError(f"A source video changed since P13 review: {path}")
    for guard in template.get("creativeReference", {}).get("sourceFiles", []):
        path = Path(guard["path"]).resolve(strict=True)
        if sha256_file(path) != guard.get("sha256"):
            raise RuntimeError(f"The Remotion reference source changed since review: {path}")
    return template


def task_targets_resource(task: dict, resource_id: str) -> bool:
    targets = set()
    for key in ("targetResourceIds", "targetIds"):
        value = task.get(key)
        if isinstance(value, list):
            targets.update(item for item in value if isinstance(item, str))
    for key in ("targetResourceId",):
        value = task.get(key)
        if isinstance(value, str):
            targets.add(value)
    for key in ("approvedSnapshot", "nativeTimelineScope"):
        scope = task.get(key)
        if isinstance(scope, dict):
            values = scope.get("targetIds", [])
            if isinstance(values, list):
                targets.update(item for item in values if isinstance(item, str))
    return resource_id in targets


def native_preflight(template: dict, task_id: str, expected_hash: str) -> tuple[dict, dict]:
    sys.path.insert(0, str(NATIVE_RECREATION))
    from discover import unpack  # pylint: disable=import-outside-toplevel
    from native_mcp import call  # pylint: disable=import-outside-toplevel

    task = unpack(call("timeline.inspect_task", {}))
    if (task.get("ok") is not True or task.get("active") is not True or
            task.get("status") != "running" or task.get("taskId") != task_id):
        raise RuntimeError("The supplied task ID is not the exact active running Timeline task.")
    resource_id = template["resourceId"]
    if not task_targets_resource(task, resource_id):
        raise RuntimeError("The active task does not own the P13 Timeline resource.")
    scope = task.get("nativeTimelineScope")
    admitted = scope.get("assetPaths") if isinstance(scope, dict) else None
    if not isinstance(admitted, list):
        raise RuntimeError("The active task has no explicit media path grant; no authoring call was sent.")
    admitted_paths = [str(Path(item).resolve(strict=True)) for item in admitted]
    if len(admitted_paths) != len(set(admitted_paths)):
        raise RuntimeError("The task has duplicate media path grants; inspect the native task before authoring.")
    needed = {str(Path(item["path"]).resolve(strict=True))
              for item in template.get("assetGuards", [])}
    if not needed.issubset(set(admitted_paths)):
        missing = sorted(needed - set(admitted_paths))
        raise RuntimeError("The exact task grant is missing preview source paths: " + ", ".join(missing))

    document = unpack(call("timeline.inspect_document", {}))
    if document.get("contract") != "looksy.native-timeline-inspection/v1":
        raise RuntimeError("The compact native Timeline inspection contract is unavailable.")
    if (document.get("ok") is not True or document.get("ready") is not True or
            document.get("resourceId") != resource_id or
            document.get("documentHash") != expected_hash or
            document.get("width") != 1920 or document.get("height") != 1080 or
            document.get("frameRate") != 30):
        raise RuntimeError("The fresh compact Timeline readback differs from the supplied P13 binding; no mutation sent.")
    return task, document


def canonical_plan_for_binding(template: dict, task_id: str, document_hash: str) -> dict:
    bound = dict(template)
    bound["taskId"] = task_id
    bound["startingDocumentHash"] = document_hash
    bound["status"] = "bound-to-fresh-native-task"
    bound["bindingEvidence"] = {
        "resourceId": template["resourceId"],
        "taskId": task_id,
        "documentHash": document_hash,
        "preflightContract": "timeline.inspect_task + timeline.inspect_document",
    }
    return bound


def atomic_create_json(path: Path, value: dict) -> None:
    encoded = (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    try:
        with temporary.open("xb") as output:
            output.write(encoded)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
        directory_fd = os.open(path.parent, os.O_RDONLY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    except FileExistsError as error:
        raise RuntimeError(f"Temporary binding file already exists; inspect before retrying: {temporary}") from error


def bind_receipt_plan(template: dict, task_id: str, document_hash: str,
                      receipt_root: Path) -> tuple[Path, Path, Path]:
    run_dir = receipt_root / task_id
    plan_path = run_dir / "preview-sync-p13.authoring-plan.json"
    receipt_path = run_dir / "receipt.json"
    log_path = run_dir / "receipt.ndjson"
    expected = canonical_plan_for_binding(template, task_id, document_hash)
    if plan_path.exists():
        existing = read_json(plan_path)
        if existing != expected:
            if (existing.get("taskId") != task_id or
                    existing.get("steps") != template.get("steps") or
                    existing.get("resourceId") != template.get("resourceId")):
                raise RuntimeError("An unrelated plan already occupies this task receipt directory; refusing overwrite.")
            if not log_path.exists() and not receipt_path.exists():
                raise RuntimeError("The existing unstarted task plan has another document guard; use a new task ID.")
            # Keep the original plan digest so author_native can reconcile and
            # resume from its append-only journal after an accepted mutation.
            return plan_path, receipt_path, log_path
    else:
        if log_path.exists() or receipt_path.exists():
            raise RuntimeError("A receipt exists without its bound plan; inspect the task and journal before recovery.")
        atomic_create_json(plan_path, expected)
    return plan_path, receipt_path, log_path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true",
                        help="Run after fresh task/document/path guards pass; default mode is read-only.")
    parser.add_argument("--task-id", help="Exact active native task ID.")
    parser.add_argument("--document-hash", help="Exact fresh compact-inspector documentHash.")
    parser.add_argument("--session", help="Optional isolated Looksy native MCP session JSON path.")
    parser.add_argument("--receipt-root", type=Path,
                        default=HERE / "receipts",
                        help="Per-task immutable plan and durable receipt directory root.")
    args = parser.parse_args()
    template = verify_local_plan()
    print(json.dumps({
        "status": "local-plan-verified",
        "nativeCallsMade": False,
        "productionProjectWrites": 0,
        "templatePath": str(TEMPLATE_PATH),
        "templateSha256": sha256_file(TEMPLATE_PATH),
        "storyboardPath": str(STORYBOARD_PATH),
        "storyboardSha256": sha256_file(STORYBOARD_PATH),
        "snapshotSha256": template["planningSnapshot"]["sha256"],
        "operationCount": len(template["steps"]),
        "newNativeVideoClips": template["approvedMutationSummary"]["newVideoClipCount"],
        "newNativeTitleClips": template["approvedMutationSummary"]["newNativeTitleClipCount"],
        "sourceTimePointsIncludingEndpoints": sum(
            segment["retimePointCount"] for segment in template["segments"]),
        "applyRequested": args.apply,
    }, indent=2), flush=True)
    if not args.apply:
        return 0

    if not args.task_id or not re.fullmatch(r"[A-Za-z0-9_.-]{1,128}", args.task_id):
        raise RuntimeError("--apply requires the exact running native task ID.")
    if not args.document_hash or not re.fullmatch(r"[0-9a-f]{64}", args.document_hash):
        raise RuntimeError("--apply requires a fresh exact lowercase 64-hex document hash.")
    if args.session:
        session_path = Path(args.session).resolve(strict=True)
        os.environ["LOOKSY_NATIVE_MCP_SESSION"] = str(session_path)

    native_preflight(template, args.task_id, args.document_hash)
    plan_path, receipt_path, log_path = bind_receipt_plan(
        template, args.task_id, args.document_hash, args.receipt_root.resolve())
    sys.path.insert(0, str(NATIVE_RECREATION))
    import author_native  # pylint: disable=import-outside-toplevel

    print(json.dumps({
        "status": "native-authoring-started",
        "nativeCallsMade": True,
        "taskId": args.task_id,
        "planPath": str(plan_path),
        "receiptPath": str(receipt_path),
        "appendOnlyIntentLog": str(log_path),
        "uncertainImportRecovery": "Inspect native task + receipt; author_native refuses to replay unresolved import intents.",
    }, indent=2), flush=True)
    author_native.execute(plan_path, receipt_path, log_path, compact_inspection=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"P13 preview native authoring stopped safely: {error}", file=sys.stderr)
        raise SystemExit(2)
