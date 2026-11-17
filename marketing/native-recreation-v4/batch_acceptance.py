"""Guarded live MCP acceptance for native Timeline draft batching.

This runner never admits a task or opens a project. Mutating phases require the
exact active task and Timeline IDs written into a prepared plan, and recheck
the admitted source and output scope before each phase. It is intentionally
separate from the Remotion authoring plans.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path

from author_native import append_event, atomic_write_json, compact_json_bytes, utc_now
from native_mcp import call

ROOT = Path(__file__).resolve().parent
MANIFEST_PATH = ROOT / "source-manifest.json"
PLAN_SCHEMA = "looksy.timeline-batch-acceptance/v1"
LOG_SCHEMA = "looksy.timeline-batch-acceptance-log/v1"
SOURCE_RELATIVE = "creative-polish-v2/detail/detail-0.png"
REQUIRED_ACTION_KINDS = {"media", "layers", "text", "framing", "masking", "effects", "delivery"}


class AcceptanceError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def source_identity_for(path_text: str) -> dict:
    path = Path(path_text)
    if not path.is_file() or str(path.resolve()) != path_text:
        raise AcceptanceError("The source must be a canonical existing regular file.")
    actual_bytes = path.stat().st_size
    actual_sha = sha256_file(path)
    return {"path": str(path), "bytes": actual_bytes, "sha256": actual_sha}


def source_manifest_entry() -> dict:
    manifest = json.loads(MANIFEST_PATH.read_text())
    matches = [
        item for item in manifest.get("sources", [])
        if item.get("path", "").endswith(SOURCE_RELATIVE)
    ]
    if len(matches) != 1:
        raise AcceptanceError(f"Expected exactly one source-manifest entry for {SOURCE_RELATIVE}.")
    entry = matches[0]
    identity = source_identity_for(entry["path"])
    if identity["bytes"] != entry.get("bytes") or identity["sha256"] != entry.get("sha256"):
        raise AcceptanceError("The photo no longer matches the frozen source manifest.")
    return identity


def copy_novel_task_source(copy_id: str, origin: dict) -> dict:
    copy_root = ROOT / "acceptance-input"
    if copy_root.is_symlink():
        raise AcceptanceError("Refusing to place acceptance input through a symlink.")
    copy_root.mkdir(parents=True, exist_ok=True)
    if copy_root.resolve() != copy_root:
        raise AcceptanceError("Acceptance input directory must remain canonical inside this marketing folder.")
    task_directory = copy_root / copy_id
    try:
        task_directory.mkdir(exist_ok=False)
    except FileExistsError as error:
        raise AcceptanceError("This task already has a novel acceptance input copy; choose a new task ID.") from error
    target = task_directory / Path(origin["path"]).name
    descriptor = None
    created_file = False
    try:
        descriptor = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o444)
        created_file = True
        with os.fdopen(descriptor, "wb") as output, Path(origin["path"]).open("rb") as source:
            descriptor = None
            shutil.copyfileobj(source, output)
            output.flush()
            os.fsync(output.fileno())
        copied = source_identity_for(str(target.resolve()))
        if copied["bytes"] != origin["bytes"] or copied["sha256"] != origin["sha256"]:
            raise AcceptanceError("The acceptance copy differs from its unchanged manifest source.")
        return copied
    except Exception:
        if descriptor is not None:
            os.close(descriptor)
        try:
            if created_file:
                target.unlink()
            task_directory.rmdir()
        except OSError:
            pass
        raise


def output_directory(path_text: str) -> Path:
    path = Path(path_text)
    if not path.is_dir() or str(path.resolve()) != path_text:
        raise AcceptanceError("outputDirectory must be an existing canonical directory.")
    return path


def digest_json(value: dict) -> str:
    return hashlib.sha256(compact_json_bytes(value)).hexdigest()


def log_path_for(plan_path: Path) -> Path:
    return plan_path.with_suffix(".ndjson")


def receipt_path_for(plan_path: Path) -> Path:
    return plan_path.with_suffix(".receipt.json")


def read_events(plan_path: Path, plan_sha: str) -> list[dict]:
    path = log_path_for(plan_path)
    if not path.exists():
        return []
    events = []
    with path.open("r", encoding="utf-8") as source:
        for line_number, line in enumerate(source, 1):
            if not line.strip():
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError as error:
                raise AcceptanceError(f"Acceptance log is damaged at line {line_number}: {error}") from error
            if event.get("schema") != LOG_SCHEMA or event.get("planSha256") != plan_sha:
                raise AcceptanceError(f"Acceptance log line {line_number} belongs to a different plan.")
            events.append(event)
    return events


def verify_no_unknown_mutations(events: list[dict]) -> None:
    started = set()
    settled = set()
    for event in events:
        event_id = event.get("eventId")
        if event_id is None:
            continue
        if event.get("event") == "mutation_started":
            if event_id in started and event_id not in settled:
                raise AcceptanceError(f"Duplicate unresolved intent {event_id}; inspect the native task before retry.")
            started.add(event_id)
        elif event.get("event") in {"mutation_completed", "mutation_failed"}:
            settled.add(event_id)
    unresolved = sorted(started - settled)
    if unresolved:
        raise AcceptanceError(
            "A mutating MCP call has no durable response receipt ("
            + ", ".join(unresolved[:6])
            + "). The runner will not replay an uncertain request."
        )


def safe_unpack(raw: dict) -> dict:
    if not isinstance(raw, dict):
        return {"value": raw}
    body = raw.get("result", raw)
    if not isinstance(body, dict):
        return {"value": body}
    if isinstance(body.get("structuredContent"), dict):
        return body["structuredContent"]
    for block in body.get("content", []):
        if block.get("type") == "text":
            try:
                parsed = json.loads(block.get("text", ""))
                if isinstance(parsed, dict):
                    return parsed
            except json.JSONDecodeError:
                continue
    return body


def is_error(raw: dict, body: dict) -> bool:
    # tools/call wraps the tool result in the JSON-RPC `result` object. MCP
    # errors are marked there (`result.isError`), while structuredContent for
    # a failed native command may contain only an `error` string and no `ok`.
    envelope = raw.get("result", {}) if isinstance(raw, dict) else {}
    return bool(raw.get("error") or
                (isinstance(envelope, dict) and envelope.get("isError")) or
                body.get("isError") or body.get("ok") is False)


def record(plan_path: Path, plan_sha: str, event: dict) -> None:
    append_event(log_path_for(plan_path), {
        "schema": LOG_SCHEMA,
        "planSha256": plan_sha,
        "atUtc": utc_now(),
        **event,
    })


def timed_rpc(plan_path: Path, plan_sha: str, step_id: str, tool: str,
              arguments: dict, mutating: bool = False,
              expected_rejection: bool = False) -> dict:
    request_bytes = len(compact_json_bytes({"name": tool, "arguments": arguments}))
    if mutating:
        record(plan_path, plan_sha, {
            "event": "mutation_started", "eventId": step_id, "tool": tool,
            "arguments": arguments, "requestBytes": request_bytes,
        })
    started = time.perf_counter_ns()
    raw = None
    body = {}
    try:
        raw = call(tool, arguments)
        rpc_ms = (time.perf_counter_ns() - started) / 1_000_000.0
        body = safe_unpack(raw)
        response_bytes = len(compact_json_bytes(raw))
        failed = is_error(raw, body)
        if expected_rejection and not failed:
            raise AcceptanceError(f"Native command unexpectedly accepted the altered request {step_id}.")
        if failed and not expected_rejection:
            raise AcceptanceError(json.dumps(raw, ensure_ascii=False))
        record(plan_path, plan_sha, {
            "event": "mutation_completed" if mutating else "read_completed",
            "eventId": step_id if mutating else None,
            "tool": tool,
            "arguments": arguments,
            "result": body,
            "expectedRejection": expected_rejection,
            "rpcMs": round(rpc_ms, 3),
            "requestBytes": request_bytes,
            "responseBytes": response_bytes,
        })
        return body
    except Exception as error:
        rpc_ms = (time.perf_counter_ns() - started) / 1_000_000.0
        if mutating:
            record(plan_path, plan_sha, {
                "event": "mutation_failed", "eventId": step_id, "tool": tool,
                "rpcMs": round(rpc_ms, 3), "requestBytes": request_bytes,
                "responseBytes": len(compact_json_bytes(raw)) if raw is not None else 0,
                "error": str(error),
            })
        else:
            record(plan_path, plan_sha, {
                "event": "read_failed", "tool": tool,
                "rpcMs": round(rpc_ms, 3), "requestBytes": request_bytes,
                "responseBytes": len(compact_json_bytes(raw)) if raw is not None else 0,
                "error": str(error),
            })
        raise


def prior_mutation(plan_path: Path, plan_sha: str, step_id: str,
                   tool: str, arguments: dict) -> dict | None:
    events = read_events(plan_path, plan_sha)
    verify_no_unknown_mutations(events)
    for event in events:
        if event.get("eventId") == step_id and event.get("event") == "mutation_completed":
            if event.get("tool") != tool or event.get("arguments") != arguments:
                raise AcceptanceError(f"Stored intent {step_id} does not match this reviewed plan.")
            return event.get("result", {})
        if event.get("eventId") == step_id and event.get("event") == "mutation_failed":
            raise AcceptanceError(f"Native mutation {step_id} previously failed; inspect its stored receipt.")
    return None


def mutate(plan_path: Path, plan_sha: str, step_id: str, tool: str,
           arguments: dict, expected_rejection: bool = False) -> dict:
    existing = prior_mutation(plan_path, plan_sha, step_id, tool, arguments)
    if existing is not None:
        return existing
    return timed_rpc(plan_path, plan_sha, step_id, tool, arguments,
                     mutating=True, expected_rejection=expected_rejection)


def load_plan(plan_path: Path) -> tuple[dict, str]:
    plan = json.loads(plan_path.read_text())
    if plan.get("schema") != PLAN_SCHEMA:
        raise AcceptanceError("Unsupported batch acceptance plan schema.")
    plan_sha = hashlib.sha256(plan_path.read_bytes()).hexdigest()
    events = read_events(plan_path, plan_sha)
    verify_no_unknown_mutations(events)
    return plan, plan_sha


def save_receipt(plan_path: Path, plan_sha: str, status: str,
                 details: dict | None = None) -> None:
    events = read_events(plan_path, plan_sha)
    rpc_events = [
        event for event in events
        if event.get("event") in {"read_completed", "read_failed", "mutation_completed", "mutation_failed"}
    ]
    totals = {
        "rpcCalls": len(rpc_events),
        "nativeRpcMs": round(sum(float(event.get("rpcMs", 0.0)) for event in rpc_events), 3),
        "requestBytes": sum(int(event.get("requestBytes", 0)) for event in rpc_events),
        "responseBytes": sum(int(event.get("responseBytes", 0)) for event in rpc_events),
    }
    receipt = {}
    previous_path = receipt_path_for(plan_path)
    if previous_path.is_file():
        previous = json.loads(previous_path.read_text())
        if previous.get("planSha256") == plan_sha:
            receipt.update(previous)
    receipt.update({
        "schema": PLAN_SCHEMA,
        "planSha256": plan_sha,
        "taskId": json.loads(plan_path.read_text()).get("taskId"),
        "status": status,
        "timingSummary": totals,
        "evidenceLog": str(log_path_for(plan_path).resolve()),
        "updatedAtUtc": utc_now(),
    })
    if details:
        receipt.update(details)
    atomic_write_json(receipt_path_for(plan_path), receipt)


def require_confirmation(plan: dict, args: argparse.Namespace) -> None:
    if not args.execute:
        raise AcceptanceError("Mutating phases require the explicit --execute flag.")
    require_scope_ids(plan, args)


def require_scope_ids(plan: dict, args: argparse.Namespace) -> None:
    if args.confirm_task_id != plan["taskId"] or args.confirm_resource_id != plan["resourceId"]:
        raise AcceptanceError("Task and resource IDs must exactly match the prepared task and Timeline resource.")


def list_commands(plan_path: Path, plan_sha: str) -> dict[str, dict]:
    rows = []
    offset = 0
    while True:
        page = timed_rpc(plan_path, plan_sha, f"commands.page.{offset}", "app.list_commands",
                         {"domain": "timeline", "offset": offset, "limit": 64})
        if not isinstance(page.get("commands"), list):
            raise AcceptanceError("Timeline command discovery returned no command page.")
        rows.extend(page["commands"])
        next_offset = page.get("nextOffset", -1)
        if not isinstance(next_offset, int) or next_offset < 0 or next_offset <= offset:
            break
        offset = next_offset
    return {item.get("name"): item for item in rows if item.get("name")}


def command_schema(plan_path: Path, plan_sha: str, commands: dict[str, dict],
                   name: str, expected_read_only: bool) -> tuple[str, dict]:
    row = commands.get(name)
    if not row or not row.get("available"):
        raise AcceptanceError(f"Required Timeline command is unavailable: {name}.")
    command_id = row.get("commandId", "")
    schema = timed_rpc(plan_path, plan_sha, f"schema.{name}", "app.command_schema",
                       {"commandId": command_id})
    if not schema.get("available") or bool(schema.get("readOnly")) != expected_read_only:
        raise AcceptanceError(f"Timeline command has an unexpected capability state: {name}.")
    return command_id, schema


def inspect_task(plan_path: Path, plan_sha: str, plan: dict,
                 allowed_statuses: tuple[str, ...] = ("running",)) -> dict:
    task = timed_rpc(plan_path, plan_sha, "task.inspect", "timeline.inspect_task", {})
    status = task.get("status")
    active = bool(task.get("active"))
    if (status not in allowed_statuses or
            (status == "released" and active) or
            (status != "released" and not active)):
        expected = ", ".join(allowed_statuses)
        raise AcceptanceError(
            f"A native Timeline task in status {expected} is required; the runner never admits one.")
    snapshot = task.get("approvedSnapshot", {})
    scope = task.get("nativeTimelineScope", {})
    targets = snapshot.get("targetIds", task.get("targetResourceIds", []))
    if plan["resourceId"] not in targets:
        raise AcceptanceError("The active task does not own the prepared Timeline resource ID.")
    admitted_assets = scope.get("assetPaths", [])
    if plan["source"]["path"] not in admitted_assets:
        raise AcceptanceError("The photo source is not in the exact admitted asset path allowlist.")
    if scope.get("outputDirectory") != plan["outputDirectory"]:
        raise AcceptanceError("The task output directory differs from the prepared canonical scope.")
    action_kinds = {
        action.get("kind") for action in snapshot.get("actions", [])
        if isinstance(action, dict)
    }
    missing = sorted(REQUIRED_ACTION_KINDS - action_kinds)
    if missing:
        raise AcceptanceError("Task approval is missing required native action categories: " + ", ".join(missing))
    return task


def inspect_document(plan_path: Path, plan_sha: str, commands: dict[str, dict],
                     require_ready: bool = True) -> dict:
    command_id, _ = command_schema(plan_path, plan_sha, commands,
                                   "inspectAgentDocument", expected_read_only=True)
    document = timed_rpc(plan_path, plan_sha, "timeline.document", "app.inspect_command",
                         {"commandId": command_id, "values": {}})
    if not document.get("ok") or (require_ready and not document.get("ready")):
        raise AcceptanceError("The open Timeline is not ready for safe task inspection.")
    return document


def require_canonical_document_path(document: dict, plan: dict) -> None:
    expected = plan.get("canonicalDocumentPath")
    actual = document.get("savedDocumentPath")
    try:
        expected_path = Path(expected).resolve(strict=True) if expected else None
        actual_path = Path(actual).resolve(strict=True) if actual else None
    except (OSError, RuntimeError):
        expected_path = actual_path = None
    if (expected_path is None or actual_path is None or
            not expected_path.is_file() or not actual_path.is_file() or
            actual_path != expected_path):
        raise AcceptanceError(
            "The open Timeline is not the exact canonical saved project path prepared for task recovery."
        )


def inspect_clip(plan_path: Path, plan_sha: str, commands: dict[str, dict], clip_id: str) -> dict:
    command_id, _ = command_schema(plan_path, plan_sha, commands,
                                   "inspectTimelineClipById", expected_read_only=True)
    result = timed_rpc(plan_path, plan_sha, f"clip.inspect.{clip_id}", "app.inspect_command",
                       {"commandId": command_id, "values": {"clipId": clip_id}})
    clip = result.get("clip")
    if not isinstance(clip, dict):
        clip = result.get("result", {}).get("clip", {})
    if not clip:
        raise AcceptanceError(f"Native clip inspection returned no detail for stable ID {clip_id}.")
    return clip


def clip_ids(document: dict) -> list[str]:
    return sorted(
        clip.get("clipId", "") for clip in document.get("clips", [])
        if isinstance(clip, dict) and clip.get("clipId")
    )


def require_fresh_document(document: dict, resource_id: str) -> None:
    if document.get("resourceId") != resource_id:
        raise AcceptanceError("The open Timeline does not match the prepared stable resource ID.")
    if document.get("clips"):
        raise AcceptanceError("Acceptance requires a fresh disposable Timeline with zero clips.")


def app_action(plan_path: Path, plan_sha: str, step_id: str,
               command_id: str, values: dict, expected_rejection: bool = False) -> dict:
    return mutate(plan_path, plan_sha, step_id, "app.invoke_command",
                  {"commandId": command_id, "values": values}, expected_rejection)


def batch_values(batch_id: str, operations: list[dict]) -> dict:
    return {"batchId": batch_id, "operations": operations}


def batch_result_rows(result: dict) -> list[dict]:
    rows = result.get("batchResults")
    if not isinstance(rows, list):
        rows = result.get("result", {}).get("batchResults", [])
    return rows if isinstance(rows, list) else []


def require_confirmed_task(plan_path: Path, plan_sha: str, plan: dict,
                           args: argparse.Namespace,
                           expected_status: str = "running") -> tuple[dict, dict[str, dict]]:
    require_confirmation(plan, args)
    task = inspect_task(plan_path, plan_sha, plan)
    if task.get("status") != expected_status:
        raise AcceptanceError(f"This phase requires task status {expected_status}.")
    commands = list_commands(plan_path, plan_sha)
    return task, commands


def phase_preflight(plan_path: Path, plan_sha: str, plan: dict) -> None:
    task = inspect_task(plan_path, plan_sha, plan)
    commands = list_commands(plan_path, plan_sha)
    doc = inspect_document(plan_path, plan_sha, commands)
    require_fresh_document(doc, plan["resourceId"])
    action_names = [
        "applyDraftBatch", "createTimelineTrack", "createTimelineClip", "createTimelineGraphic",
        "createTimelineTitle", "setTimelineClipFrame", "setTimelineClipMask",
        "setTimelineClipBlendMode", "setTimelineClipAnimation", "setTimelineClipTitle", "saveTimeline",
    ]
    available = {}
    for name in action_names:
        command_id, schema = command_schema(plan_path, plan_sha, commands, name, expected_read_only=False)
        available[name] = {"commandId": command_id, "inputSchema": schema.get("inputSchema", {})}
    if Path(plan["outputPath"]).exists():
        raise AcceptanceError("The task-owned output project path already exists; choose a new task ID.")
    save_receipt(plan_path, plan_sha, "preflight-passed", {
        "taskStatus": task.get("status"), "resourceId": doc.get("resourceId"),
        "baselineDocumentHash": doc.get("documentHash"),
        "baselineClipCount": len(doc.get("clips", [])), "commands": available,
    })
    print(json.dumps({"preflight": "passed", "taskId": plan["taskId"],
                      "resourceId": doc["resourceId"], "baselineDocumentHash": doc["documentHash"],
                      "source": plan["source"], "outputPath": plan["outputPath"]}, indent=2))


def phase_author(plan_path: Path, plan_sha: str, plan: dict, args: argparse.Namespace) -> None:
    task, commands = require_confirmed_task(plan_path, plan_sha, plan, args)
    doc_before = inspect_document(plan_path, plan_sha, commands)
    require_fresh_document(doc_before, plan["resourceId"])
    source = source_identity_for(plan["source"]["path"])
    if source != plan["source"]:
        raise AcceptanceError("The canonical source file changed after plan preparation.")

    batch_command, _ = command_schema(plan_path, plan_sha, commands,
                                      "applyDraftBatch", expected_read_only=False)
    track_command, _ = command_schema(plan_path, plan_sha, commands,
                                      "createTimelineTrack", expected_read_only=False)
    tracks_batch = batch_values(plan["batchIds"]["tracks"], [
        {"id": "backplate_lane", "commandId": track_command,
         "values": {"kind": "video", "name": "Acceptance / Backplate"}},
        {"id": "photo_lane", "commandId": track_command,
         "values": {"kind": "video", "name": "Acceptance / Photo"}},
        {"id": "headline_lane", "commandId": track_command,
         "values": {"kind": "video", "name": "Acceptance / Headline"}},
    ])
    tracks_result = app_action(plan_path, plan_sha, "batch.tracks", batch_command, tracks_batch)
    tracks_rows = {row.get("id"): row for row in batch_result_rows(tracks_result)}
    track_ids = {
        key: tracks_rows.get(key, {}).get("trackId", "")
        for key in ("backplate_lane", "photo_lane", "headline_lane")
    }
    if not all(track_ids.values()):
        raise AcceptanceError("The track batch returned no stable native track IDs.")

    import_command, _ = command_schema(plan_path, plan_sha, commands,
                                       "createTimelineClip", expected_read_only=False)
    imported = app_action(plan_path, plan_sha, "import.photo", import_command, {
        "trackId": track_ids["photo_lane"],
        "assetId": "acceptance-photo:" + source["sha256"][:16],
        "filePath": source["path"],
        "displayLabel": "Acceptance photo · detail-0.png",
        "durationSeconds": 6.0,
        "trimStartSeconds": 0.0,
        "trimEndSeconds": 6.0,
        "startSeconds": 0.0,
        "posterSource": "",
    })
    imported_clip_id = imported.get("createdClipId", "")
    if not imported_clip_id and imported.get("createdClipIds"):
        imported_clip_id = imported["createdClipIds"][0]
    if not imported_clip_id:
        raise AcceptanceError("The separate native photo import returned no stable clip ID.")

    pre_authoring_batch_document = inspect_document(plan_path, plan_sha, commands)
    pre_authoring_batch_hash = pre_authoring_batch_document.get("documentHash", "")
    if not pre_authoring_batch_hash:
        raise AcceptanceError("The imported-photo state has no stable native document hash for Undo proof.")

    graphic_command, _ = command_schema(plan_path, plan_sha, commands,
                                        "createTimelineGraphic", expected_read_only=False)
    title_command, _ = command_schema(plan_path, plan_sha, commands,
                                      "createTimelineTitle", expected_read_only=False)
    frame_command, _ = command_schema(plan_path, plan_sha, commands,
                                      "setTimelineClipFrame", expected_read_only=False)
    mask_command, _ = command_schema(plan_path, plan_sha, commands,
                                     "setTimelineClipMask", expected_read_only=False)
    blend_command, _ = command_schema(plan_path, plan_sha, commands,
                                      "setTimelineClipBlendMode", expected_read_only=False)
    animation_command, _ = command_schema(plan_path, plan_sha, commands,
                                          "setTimelineClipAnimation", expected_read_only=False)
    title_update_command, _ = command_schema(plan_path, plan_sha, commands,
                                             "setTimelineClipTitle", expected_read_only=False)

    frame_values = {
        "offsetX": 0.0, "offsetY": 0.0, "scale": 1.0, "rotationDegrees": 0.0,
        "perspectiveX": 0.0, "perspectiveY": 0.0, "opacity": 1.0,
        "objectFit": "fit", "width": 1.0, "height": 1.0,
        "cornerRadius": 0.0, "cropLeft": 0.0, "cropTop": 0.0,
        "cropRight": 1.0, "cropBottom": 1.0,
    }
    photo_frame = {
        "offsetX": 0.0, "offsetY": 0.0, "scale": 1.0, "rotationDegrees": 0.0,
        "perspectiveX": 0.0, "perspectiveY": 0.0, "opacity": 1.0,
        "objectFit": "fit", "width": 0.74, "height": 0.74,
        "cornerRadius": 0.04, "cropLeft": 0.0, "cropTop": 0.0,
        "cropRight": 1.0, "cropBottom": 1.0,
    }
    batch_b = batch_values(plan["batchIds"]["authoring"], [
        {"id": "backplate", "commandId": graphic_command, "values": {
            "trackId": track_ids["backplate_lane"], "shapeType": "rectangle",
            "startSeconds": 0.0, "durationSeconds": 6.0,
            "values": {"backgroundColor": "#ff182b42", "positionX": 0.5,
                       "positionY": 0.5, "shapeWidth": 1.0, "shapeHeight": 1.0}}},
        {"id": "backplate_frame", "commandId": frame_command, "values": {
            "clipId": {"result": "backplate", "field": "createdClipId"}, **frame_values}},
        {"id": "headline", "commandId": title_command, "values": {
            "trackId": track_ids["headline_lane"], "templateKey": "headline",
            "startSeconds": 0.3, "durationSeconds": 5.4,
            "values": {"text": "BATCH ACCEPTANCE / EDITABLE", "fontFamily": "Arial",
                       "fontSize": 30.0, "color": "#fff2d8", "positionX": 0.5,
                       "positionY": 0.16, "alignment": "center"}}},
        {"id": "headline_text", "commandId": title_update_command, "values": {
            "clipId": {"result": "headline", "field": "createdClipId"},
            "values": {"text": "NATIVE MASK + SCREEN BLEND", "fontFamily": "Arial",
                       "fontSize": 30.0, "color": "#fff2d8", "positionX": 0.5,
                       "positionY": 0.16, "alignment": "center"}}},
        {"id": "photo_frame", "commandId": frame_command,
         "values": {"clipId": imported_clip_id, **photo_frame}},
        {"id": "photo_mask", "commandId": mask_command, "values": {
            "clipId": imported_clip_id, "shape": "rectangle",
            "left": 0.08, "top": 0.08, "right": 0.92, "bottom": 0.92,
            "cornerRadius": 0.08}},
        {"id": "photo_blend", "commandId": blend_command,
         "values": {"clipId": imported_clip_id, "blendMode": "screen"}},
        {"id": "photo_opacity", "commandId": animation_command, "values": {
            "clipId": imported_clip_id, "channels": [{"parameter": "opacity", "keyframes": [
                {"localSeconds": 0.0, "value": 0.82, "interpolation": "linear"},
                {"localSeconds": 2.0, "value": 1.0, "interpolation": "ease-in-out"},
                {"localSeconds": 5.95, "value": 0.82, "interpolation": "linear"},
            ]}]}}
    ])
    batch_b_result = app_action(plan_path, plan_sha, "batch.authoring", batch_command, batch_b)
    batch_b_rows = batch_result_rows(batch_b_result)
    if len(batch_b_rows) != 8 or not all(row.get("ok") for row in batch_b_rows):
        raise AcceptanceError("The authoring batch did not return eight successful compact action receipts.")

    document_after = inspect_document(plan_path, plan_sha, commands)
    clip = inspect_clip(plan_path, plan_sha, commands, imported_clip_id)
    if clip.get("blendMode") != "screen" or not clip.get("clipMask", {}).get("enabled"):
        raise AcceptanceError("The imported photo readback does not retain screen blend mode and sequence-space mask.")
    source_fingerprint = {
        "filePath": clip.get("filePath", ""),
        "sourceFileSize": clip.get("sourceFileSize"),
        "sourceContentSha256": clip.get("sourceContentSha256", ""),
        "sourceIdentitySignature": clip.get("sourceIdentitySignature", ""),
        "mediaAnalysisStatus": clip.get("mediaAnalysisStatus", ""),
        "sourceIntegrityStatus": clip.get("sourceIntegrityStatus", ""),
    }
    save_command, _ = command_schema(plan_path, plan_sha, commands,
                                     "saveTimeline", expected_read_only=False)
    saved = app_action(plan_path, plan_sha, "save.project-copy", save_command,
                       {"outputPath": plan["outputPath"]})
    if not saved.get("ok"):
        raise AcceptanceError("The task-owned native project copy could not be saved.")

    save_receipt(plan_path, plan_sha, "authored-awaiting-ui-proof", {
        "taskStatus": task.get("status"), "resourceId": plan["resourceId"],
        "baselineDocumentHash": doc_before.get("documentHash"),
        "baselineClipCount": len(doc_before.get("clips", [])),
        "preAuthoringBatchDocumentHash": pre_authoring_batch_hash,
        "postAuthoringDocumentHash": document_after.get("documentHash"),
        "trackIds": track_ids, "importedClipId": imported_clip_id,
        "batchAResults": batch_result_rows(tracks_result), "batchBResults": batch_b_rows,
        "sourceFingerprintAtImport": source_fingerprint,
        "outputPath": plan["outputPath"],
        "outputExists": Path(plan["outputPath"]).is_file(),
    })
    print(json.dumps({"status": "authored-awaiting-ui-proof", "importedClipId": imported_clip_id,
                      "trackIds": track_ids, "documentHash": document_after.get("documentHash"),
                      "sourceFingerprintAtImport": source_fingerprint,
                      "outputPath": plan["outputPath"]}, indent=2))


def phase_replay(plan_path: Path, plan_sha: str, plan: dict, args: argparse.Namespace) -> None:
    _, commands = require_confirmed_task(plan_path, plan_sha, plan, args)
    command_id, _ = command_schema(plan_path, plan_sha, commands,
                                   "applyDraftBatch", expected_read_only=False)
    receipt = json.loads(receipt_path_for(plan_path).read_text())
    imported_clip_id = receipt.get("importedClipId", "")
    track_ids = receipt.get("trackIds", {})
    if not imported_clip_id or not track_ids:
        raise AcceptanceError("Authoring receipts are required before batch idempotency checks.")
    before = inspect_document(plan_path, plan_sha, commands)
    before_ids = clip_ids(before)
    batch_values_body = build_authoring_batch(plan, commands, track_ids, imported_clip_id)
    same = app_action(plan_path, plan_sha, "replay.batch.authoring.same", command_id, batch_values_body)
    if not same.get("idempotentReplay"):
        raise AcceptanceError("Retrying the exact batch ID/body did not return idempotentReplay:true.")
    after_same = inspect_document(plan_path, plan_sha, commands)
    if after_same.get("documentHash") != before.get("documentHash") or clip_ids(after_same) != before_ids:
        raise AcceptanceError("An exact batch retry changed the Timeline document or created duplicate clips.")

    changed = json.loads(json.dumps(batch_values_body))
    changed["operations"][2]["values"]["values"]["text"] = "CHANGED BODY MUST REJECT"
    rejection = timed_rpc(plan_path, plan_sha, "replay.batch.authoring.changed", "app.invoke_command",
                          {"commandId": command_id, "values": changed},
                          mutating=True, expected_rejection=True)
    after_changed = inspect_document(plan_path, plan_sha, commands)
    if after_changed.get("documentHash") != before.get("documentHash") or clip_ids(after_changed) != before_ids:
        raise AcceptanceError("A changed-body retry modified the Timeline despite the stable batch ID.")
    record(plan_path, plan_sha, {
        "event": "idempotency_checks_passed", "sameBodyReplayed": True,
        "changedBodyRejected": True, "sameBodyReceipt": same,
        "changedBodyReceipt": rejection, "documentHash": after_changed.get("documentHash"),
        "clipIds": before_ids,
    })
    save_receipt(plan_path, plan_sha, "idempotency-passed", {
        "postAuthoringDocumentHash": after_changed.get("documentHash"),
        "clipIds": before_ids, "sameBatchIdReplay": "passed", "changedBodyRejection": "passed",
    })
    print(json.dumps({"sameBatchIdReplay": "passed", "changedBodyRejection": "passed",
                      "clipCount": len(before_ids), "documentHash": after_changed.get("documentHash")}, indent=2))


def build_authoring_batch(plan: dict, commands: dict[str, dict],
                          track_ids: dict, imported_clip_id: str) -> dict:
    def command(name: str) -> str:
        row = commands.get(name)
        if not row or not row.get("available"):
            raise AcceptanceError(f"Required native action is unavailable: {name}.")
        return row["commandId"]

    frame_values = {
        "offsetX": 0.0, "offsetY": 0.0, "scale": 1.0, "rotationDegrees": 0.0,
        "perspectiveX": 0.0, "perspectiveY": 0.0, "opacity": 1.0,
        "objectFit": "fit", "width": 1.0, "height": 1.0,
        "cornerRadius": 0.0, "cropLeft": 0.0, "cropTop": 0.0,
        "cropRight": 1.0, "cropBottom": 1.0,
    }
    photo_frame = {
        "offsetX": 0.0, "offsetY": 0.0, "scale": 1.0, "rotationDegrees": 0.0,
        "perspectiveX": 0.0, "perspectiveY": 0.0, "opacity": 1.0,
        "objectFit": "fit", "width": 0.74, "height": 0.74,
        "cornerRadius": 0.04, "cropLeft": 0.0, "cropTop": 0.0,
        "cropRight": 1.0, "cropBottom": 1.0,
    }
    graphic = command("createTimelineGraphic")
    frame = command("setTimelineClipFrame")
    title = command("createTimelineTitle")
    title_update = command("setTimelineClipTitle")
    mask = command("setTimelineClipMask")
    blend = command("setTimelineClipBlendMode")
    animation = command("setTimelineClipAnimation")
    return batch_values(plan["batchIds"]["authoring"], [
        {"id": "backplate", "commandId": graphic, "values": {
            "trackId": track_ids["backplate_lane"], "shapeType": "rectangle",
            "startSeconds": 0.0, "durationSeconds": 6.0,
            "values": {"backgroundColor": "#ff182b42", "positionX": 0.5,
                       "positionY": 0.5, "shapeWidth": 1.0, "shapeHeight": 1.0}}},
        {"id": "backplate_frame", "commandId": frame, "values": {
            "clipId": {"result": "backplate", "field": "createdClipId"}, **frame_values}},
        {"id": "headline", "commandId": title, "values": {
            "trackId": track_ids["headline_lane"], "templateKey": "headline",
            "startSeconds": 0.3, "durationSeconds": 5.4,
            "values": {"text": "BATCH ACCEPTANCE / EDITABLE", "fontFamily": "Arial",
                       "fontSize": 30.0, "color": "#fff2d8", "positionX": 0.5,
                       "positionY": 0.16, "alignment": "center"}}},
        {"id": "headline_text", "commandId": title_update, "values": {
            "clipId": {"result": "headline", "field": "createdClipId"},
            "values": {"text": "NATIVE MASK + SCREEN BLEND", "fontFamily": "Arial",
                       "fontSize": 30.0, "color": "#fff2d8", "positionX": 0.5,
                       "positionY": 0.16, "alignment": "center"}}},
        {"id": "photo_frame", "commandId": frame,
         "values": {"clipId": imported_clip_id, **photo_frame}},
        {"id": "photo_mask", "commandId": mask, "values": {
            "clipId": imported_clip_id, "shape": "rectangle",
            "left": 0.08, "top": 0.08, "right": 0.92, "bottom": 0.92,
            "cornerRadius": 0.08}},
        {"id": "photo_blend", "commandId": blend,
         "values": {"clipId": imported_clip_id, "blendMode": "screen"}},
        {"id": "photo_opacity", "commandId": animation, "values": {
            "clipId": imported_clip_id, "channels": [{"parameter": "opacity", "keyframes": [
                {"localSeconds": 0.0, "value": 0.82, "interpolation": "linear"},
                {"localSeconds": 2.0, "value": 1.0, "interpolation": "ease-in-out"},
                {"localSeconds": 5.95, "value": 0.82, "interpolation": "linear"},
            ]}]}}
    ])


def phase_verify_undo(plan_path: Path, plan_sha: str, plan: dict,
                      args: argparse.Namespace) -> None:
    require_scope_ids(plan, args)
    if inspect_task(plan_path, plan_sha, plan).get("status") != "running":
        raise AcceptanceError("Undo proof requires the same active running Timeline task.")
    image = Path(args.evidence_after_undo)
    if not image.is_file() or image.stat().st_size == 0:
        raise AcceptanceError("Supply the saved native Timeline screenshot captured after one Undo.")
    commands = list_commands(plan_path, plan_sha)
    doc = inspect_document(plan_path, plan_sha, commands)
    receipt = json.loads(receipt_path_for(plan_path).read_text())
    expected_hash = receipt.get("preAuthoringBatchDocumentHash")
    if receipt.get("status") != "native-visible-proof-passed" or not expected_hash:
        raise AcceptanceError("Visible mask/blend proof is required before native Undo proof.")
    if doc.get("documentHash") != expected_hash:
        raise AcceptanceError("One native Undo did not restore the exact pre-batch document hash.")
    evidence_hash = sha256_file(image)
    record(plan_path, plan_sha, {
        "event": "native_undo_verified", "documentHash": expected_hash,
        "screenshotPath": str(image.resolve()), "screenshotSha256": evidence_hash,
        "clipIds": clip_ids(doc),
    })
    save_receipt(plan_path, plan_sha, "native-undo-passed", {
        "preAuthoringBatchDocumentHash": expected_hash,
        "undoScreenshot": str(image.resolve()), "undoScreenshotSha256": evidence_hash,
    })
    print(json.dumps({"nativeUndo": "passed", "documentHash": expected_hash,
                      "screenshot": str(image.resolve()), "screenshotSha256": evidence_hash}, indent=2))


def phase_verify_visible(plan_path: Path, plan_sha: str, plan: dict,
                         args: argparse.Namespace) -> None:
    require_scope_ids(plan, args)
    if inspect_task(plan_path, plan_sha, plan).get("status") != "running":
        raise AcceptanceError("Visible proof requires the same active running Timeline task.")
    image = Path(args.evidence_visible)
    if not image.is_file() or image.stat().st_size == 0:
        raise AcceptanceError("Supply a screenshot of the native Program with the masked photo and blend active.")
    commands = list_commands(plan_path, plan_sha)
    doc = inspect_document(plan_path, plan_sha, commands)
    receipt = json.loads(receipt_path_for(plan_path).read_text())
    clip_id = receipt.get("importedClipId", "")
    clip = inspect_clip(plan_path, plan_sha, commands, clip_id)
    if (receipt.get("status") != "idempotency-passed" or
            doc.get("documentHash") != receipt.get("postAuthoringDocumentHash") or
            clip.get("blendMode") != "screen" or
            not clip.get("clipMask", {}).get("enabled")):
        raise AcceptanceError("The current native clip readback does not match the authored visible mask/blend state.")
    evidence_hash = sha256_file(image)
    details = {
        "visibleProofScreenshot": str(image.resolve()),
        "visibleProofScreenshotSha256": evidence_hash,
        "visibleProofDocumentHash": doc.get("documentHash"),
        "visibleProofClip": {
            "clipId": clip_id,
            "blendMode": clip.get("blendMode"),
            "clipMask": clip.get("clipMask"),
        },
        "reviewerType": args.reviewer_type,
    }
    record(plan_path, plan_sha, {"event": "native_mask_blend_visible_proof_captured", **details})
    save_receipt(plan_path, plan_sha, "native-visible-proof-passed", details)
    print(json.dumps({"nativeVisibleProof": "captured-for-root-review", **details}, indent=2))


def phase_verify_redo(plan_path: Path, plan_sha: str, plan: dict,
                      args: argparse.Namespace) -> None:
    require_scope_ids(plan, args)
    if inspect_task(plan_path, plan_sha, plan).get("status") != "running":
        raise AcceptanceError("Redo proof requires the same active running Timeline task.")
    image = Path(args.evidence_after_redo)
    if not image.is_file() or image.stat().st_size == 0:
        raise AcceptanceError("Supply the saved native Timeline screenshot captured after Redo.")
    commands = list_commands(plan_path, plan_sha)
    doc = inspect_document(plan_path, plan_sha, commands)
    receipt = json.loads(receipt_path_for(plan_path).read_text())
    expected_hash = receipt.get("postAuthoringDocumentHash")
    if receipt.get("status") != "native-undo-passed" or not expected_hash:
        raise AcceptanceError("A passing native Undo checkpoint is required before Redo proof.")
    if doc.get("documentHash") != expected_hash:
        raise AcceptanceError("One native Redo did not restore the exact post-batch document hash.")
    evidence_hash = sha256_file(image)
    record(plan_path, plan_sha, {
        "event": "native_redo_verified", "documentHash": expected_hash,
        "screenshotPath": str(image.resolve()), "screenshotSha256": evidence_hash,
        "clipIds": clip_ids(doc),
    })
    save_receipt(plan_path, plan_sha, "native-undo-redo-passed", {
        "postAuthoringDocumentHash": expected_hash,
        "redoScreenshot": str(image.resolve()), "redoScreenshotSha256": evidence_hash,
    })
    print(json.dumps({"nativeUndoRedo": "passed", "documentHash": expected_hash,
                      "screenshot": str(image.resolve()), "screenshotSha256": evidence_hash}, indent=2))


def phase_review_release(plan_path: Path, plan_sha: str, plan: dict,
                         args: argparse.Namespace) -> None:
    require_confirmation(plan, args)
    task = inspect_task(
        plan_path, plan_sha, plan,
        allowed_statuses=("running", "review", "released"))
    receipt = json.loads(receipt_path_for(plan_path).read_text())
    if receipt.get("status") not in {
            "native-undo-redo-passed", "released-awaiting-derived-drain"}:
        raise AcceptanceError("Review/release waits for the native visible-proof and Undo/Redo checkpoint.")
    if not Path(plan["outputPath"]).is_file():
        raise AcceptanceError("The task-owned project copy is missing before review.")
    finish_args = {"taskId": plan["taskId"], "action": "review"}
    finish = prior_mutation(plan_path, plan_sha, "task.finish.review",
                            "timeline.finish_task", finish_args)
    if task.get("status") == "running":
        if finish is not None:
            raise AcceptanceError("The stored review receipt conflicts with the still-running native task; inspect the task before continuing.")
        finish = mutate(plan_path, plan_sha, "task.finish.review", "timeline.finish_task", finish_args)
        if finish.get("status") != "review":
            raise AcceptanceError("Timeline task did not enter review status.")
        task = inspect_task(plan_path, plan_sha, plan, allowed_statuses=("review",))
    elif not isinstance(finish, dict) or finish.get("status") != "review":
        raise AcceptanceError("Resuming review requires the runner's durable successful finish receipt.")
    if task.get("status") == "released":
        released = prior_mutation(
            plan_path, plan_sha, "task.release.reviewed", "timeline.release_reviewed_task",
            {"taskId": plan["taskId"], "confirmed": True})
        if (not isinstance(released, dict) or released.get("status") != "released" or
                released.get("active") is not False):
            raise AcceptanceError("The already-released task has no matching durable release receipt.")
    else:
        release_args = {"taskId": plan["taskId"], "confirmed": True}
        released = prior_mutation(plan_path, plan_sha, "task.release.reviewed",
                                   "timeline.release_reviewed_task", release_args)
        if released is not None:
            raise AcceptanceError("A release receipt exists while the native task still reports ownership; inspect the task before continuing.")
        released = mutate(plan_path, plan_sha, "task.release.reviewed",
                          "timeline.release_reviewed_task", release_args)
    if released.get("status") != "released" or released.get("active") is not False:
        raise AcceptanceError("Reviewed Timeline ownership was not released cleanly.")
    save_receipt(plan_path, plan_sha, "released-awaiting-derived-drain", {
        "finishReceipt": finish,
        "releaseReceipt": released,
        "releaseStatus": released.get("status"), "outputPath": plan["outputPath"],
    })
    print(json.dumps({"status": "released-awaiting-derived-drain",
                      "outputPath": plan["outputPath"]}, indent=2))


def path_from_clip(clip: dict) -> str:
    for field in ("filePath", "sourcePath", "sourceUrl"):
        value = clip.get(field, "")
        if not value:
            continue
        if value.startswith("file://"):
            from urllib.parse import unquote, urlparse
            return str(Path(unquote(urlparse(value).path)).resolve())
        try:
            return str(Path(value).resolve())
        except (OSError, ValueError):
            continue
    return ""


def phase_drain(plan_path: Path, plan_sha: str, plan: dict, args: argparse.Namespace) -> None:
    receipt = json.loads(receipt_path_for(plan_path).read_text())
    clip_id = receipt.get("importedClipId", "")
    if receipt.get("status") != "released-awaiting-derived-drain" or not clip_id:
        raise AcceptanceError("Release receipt and imported stable clip ID are required before metadata drain.")
    source = source_identity_for(plan["source"]["path"])
    if source != plan["source"]:
        raise AcceptanceError("The source file changed after authoring; preserve the task baseline and stop.")
    commands = list_commands(plan_path, plan_sha)
    deadline = time.monotonic() + max(1, args.drain_timeout)
    samples = []
    clip = {}
    doc = {}
    while time.monotonic() < deadline:
        doc = inspect_document(plan_path, plan_sha, commands, require_ready=False)
        if not doc.get("ready"):
            sample = {
                "atUtc": utc_now(), "ready": False,
                "documentHash": doc.get("documentHash"),
                "filePath": "", "sourceFileSize": None,
                "sourceContentSha256": "", "sourceIdentitySignature": "",
                "mediaAnalysisStatus": "busy", "sourceIntegrityStatus": "busy",
            }
            samples.append(sample)
            time.sleep(1.0)
            continue
        clip = inspect_clip(plan_path, plan_sha, commands, clip_id)
        sample = {
            "atUtc": utc_now(), "ready": doc.get("ready"),
            "documentHash": doc.get("documentHash"),
            "filePath": path_from_clip(clip),
            "sourceFileSize": clip.get("sourceFileSize"),
            "sourceContentSha256": clip.get("sourceContentSha256", ""),
            "sourceIdentitySignature": clip.get("sourceIdentitySignature", ""),
            "mediaAnalysisStatus": clip.get("mediaAnalysisStatus", ""),
            "sourceIntegrityStatus": clip.get("sourceIntegrityStatus", ""),
        }
        samples.append(sample)
        if sample["filePath"] != source["path"]:
            raise AcceptanceError("Imported photo source path changed during derived metadata drain.")
        if sample["sourceFileSize"] not in (None, 0, source["bytes"]):
            raise AcceptanceError("Imported photo source byte size changed during metadata drain.")
        if sample["sourceContentSha256"] not in ("", source["sha256"]):
            raise AcceptanceError("Imported photo full SHA-256 changed to an unexpected value.")
        if (sample["sourceFileSize"] == source["bytes"] and
                sample["sourceContentSha256"] == source["sha256"] and doc.get("ready")):
            break
        time.sleep(1.0)
    drained = bool(clip.get("sourceFileSize") == source["bytes"] and
                   clip.get("sourceContentSha256") == source["sha256"] and doc.get("ready"))
    details = {
        "derivedDrain": "matched-full-source-identity" if drained else "timed-out-or-unresolved",
        "sourceIdentityExpected": source,
        "sourceIdentityFinal": samples[-1] if samples else {},
        "documentHashAfterRelease": doc.get("documentHash"),
        "derivedDrainSamples": samples,
    }
    record(plan_path, plan_sha, {"event": "derived_source_drain_observed", **details})
    save_receipt(plan_path, plan_sha, "derived-drained" if drained else "derived-drain-incomplete", details)
    print(json.dumps(details, indent=2))
    if not drained:
        raise AcceptanceError("Derived source identity did not reach the admitted full path/size/SHA within the timeout.")


def phase_recover(plan_path: Path, plan_sha: str, plan: dict,
                  args: argparse.Namespace) -> None:
    if not args.execute or args.confirm_task_id != plan["taskId"] or args.confirm_resource_id != plan["resourceId"]:
        raise AcceptanceError("Recovery requires --execute and the exact prepared task and Timeline IDs.")
    commands = list_commands(plan_path, plan_sha)
    previous = json.loads(receipt_path_for(plan_path).read_text())
    if previous.get("status") != "derived-drained":
        raise AcceptanceError("Recovery requires a completed post-release full source-identity drain.")
    doc = inspect_document(plan_path, plan_sha, commands)
    if doc.get("resourceId") != plan["resourceId"]:
        raise AcceptanceError(
            "Reopen the canonical original Timeline project path first. The current open resourceId does not match the released task."
        )
    require_canonical_document_path(doc, plan)
    recovered = timed_rpc(plan_path, plan_sha, "task.recover", "timeline.recover_task", {
        "taskId": plan["taskId"], "targetResourceId": plan["resourceId"],
    }, mutating=True)
    if not recovered.get("active"):
        raise AcceptanceError("The exact released task did not reattach to the open Timeline.")
    reopened_doc = inspect_document(plan_path, plan_sha, commands)
    require_canonical_document_path(reopened_doc, plan)
    clip = inspect_clip(plan_path, plan_sha, commands, previous["importedClipId"])
    identity = {
        "resourceId": reopened_doc.get("resourceId"),
        "savedDocumentPath": reopened_doc.get("savedDocumentPath"),
        "documentHash": reopened_doc.get("documentHash"),
        "filePath": path_from_clip(clip),
        "sourceFileSize": clip.get("sourceFileSize"),
        "sourceContentSha256": clip.get("sourceContentSha256", ""),
        "sourceIdentitySignature": clip.get("sourceIdentitySignature", ""),
        "mediaAnalysisStatus": clip.get("mediaAnalysisStatus", ""),
        "sourceIntegrityStatus": clip.get("sourceIntegrityStatus", ""),
    }
    record(plan_path, plan_sha, {"event": "released_task_recovered", "identityAfterReopen": identity})
    if (identity["resourceId"] != plan["resourceId"] or
            identity["filePath"] != plan["source"]["path"] or
            identity["sourceFileSize"] != plan["source"]["bytes"] or
            identity["sourceContentSha256"] != plan["source"]["sha256"]):
        save_receipt(plan_path, plan_sha, "recovered-source-identity-conflict", {
            "identityAfterReopen": identity,
            "outputPathExistsBeforeRevert": Path(plan["outputPath"]).exists(),
        })
        raise AcceptanceError("Recovery reattached the task, but the admitted source identity did not match after reopen; preserve the task for conflict review.")
    save_receipt(plan_path, plan_sha, "recovered-awaiting-confirmed-revert", {
        "identityAfterReopen": identity,
        "outputPathExistsBeforeRevert": Path(plan["outputPath"]).exists(),
    })
    print(json.dumps({"status": "recovered-awaiting-confirmed-revert",
                      "identityAfterReopen": identity}, indent=2))


def phase_revert(plan_path: Path, plan_sha: str, plan: dict,
                 args: argparse.Namespace) -> None:
    if not args.execute or not args.confirm_task_id or not args.confirm_resource_id or not args.confirm_revert:
        raise AcceptanceError("Revert requires --execute, --confirm-revert and exact task/resource confirmation IDs.")
    if args.confirm_task_id != plan["taskId"] or args.confirm_resource_id != plan["resourceId"]:
        raise AcceptanceError("Revert confirmation IDs do not match the prepared task and Timeline.")
    task = timed_rpc(plan_path, plan_sha, "task.inspect.before-revert", "timeline.inspect_task", {})
    if not task.get("active") or task.get("status") != "running":
        raise AcceptanceError("Confirmed task revert requires the recovered running task.")
    receipt = json.loads(receipt_path_for(plan_path).read_text())
    if receipt.get("status") != "recovered-awaiting-confirmed-revert":
        raise AcceptanceError("Confirmed task revert requires a successful canonical-path recovery and source identity check.")
    expected_baseline_hash = receipt.get("baselineDocumentHash")
    expected_baseline_clip_count = receipt.get("baselineClipCount")
    if not expected_baseline_hash or expected_baseline_clip_count != 0:
        raise AcceptanceError("The prepared fresh Timeline baseline hash and empty clip count are required before revert.")
    result = timed_rpc(plan_path, plan_sha, "task.revert.confirmed", "timeline.revert_task", {
        "taskId": plan["taskId"], "confirmed": True,
    }, mutating=True)
    doc = inspect_document(plan_path, plan_sha, list_commands(plan_path, plan_sha))
    output_exists = Path(plan["outputPath"]).exists()
    post_revert_clips = doc.get("clips")
    baseline_restored = (
        isinstance(post_revert_clips, list) and
        not post_revert_clips and
        doc.get("documentHash") == expected_baseline_hash
    )
    save_receipt(plan_path, plan_sha,
                 "revert-passed" if result.get("ok") and baseline_restored and not output_exists else "revert-conflict", {
        "revertResult": result, "postRevertDocumentHash": doc.get("documentHash"),
        "postRevertClipCount": len(doc.get("clips", [])),
        "baselineDocumentHash": expected_baseline_hash,
        "baselineClipCount": expected_baseline_clip_count,
        "baselineRestored": baseline_restored,
        "taskOutputExistsAfterRevert": output_exists,
    })
    print(json.dumps({"revertResult": result, "postRevertDocumentHash": doc.get("documentHash"),
                      "postRevertClipCount": len(doc.get("clips", [])),
                      "taskOutputExistsAfterRevert": output_exists}, indent=2))
    if not result.get("ok"):
        raise AcceptanceError("Confirmed revert was blocked; current draft and recovery baseline remain preserved.")
    if not baseline_restored:
        raise AcceptanceError("Native revert did not restore the exact saved empty Timeline baseline.")
    if output_exists:
        raise AcceptanceError("Task revert succeeded for the Timeline but left its enrolled project copy behind.")


def prepare(args: argparse.Namespace) -> None:
    task_id = args.task_id.strip()
    if not task_id or not re.fullmatch(r"[A-Za-z0-9_-]{1,80}", task_id):
        raise AcceptanceError("Supply the exact taskId returned after root admits the fresh Timeline.")
    resource_id = args.resource_id.strip()
    if not resource_id.startswith("timeline:") or len(resource_id) <= len("timeline:"):
        raise AcceptanceError("resourceId must be the exact timeline:<stable UUID> returned by native inspection.")
    source_origin = source_manifest_entry()
    source_copy_path = Path(args.source_copy)
    copy_root = ROOT / "acceptance-input"
    if copy_root.is_symlink() or not copy_root.is_dir() or copy_root.resolve() != copy_root:
        raise AcceptanceError("Create a canonical fresh input copy under this marketing folder before task admission.")
    if (str(source_copy_path.resolve()) != args.source_copy or
            source_copy_path.name != Path(source_origin["path"]).name or
            source_copy_path.parent.parent != copy_root or
            source_copy_path.parent.resolve() != source_copy_path.parent):
        raise AcceptanceError("sourceCopy must be a canonical PNG copy directly under acceptance-input/<copy-id>/.")
    source = source_identity_for(args.source_copy)
    if source["bytes"] != source_origin["bytes"] or source["sha256"] != source_origin["sha256"]:
        raise AcceptanceError("The novel admitted source copy must match the exact manifest source bytes and SHA-256.")
    source_copy_receipt = source_copy_path.parent / "source-identity.json"
    if not source_copy_receipt.is_file():
        raise AcceptanceError("The fresh source-copy identity receipt is missing; preserve the input and stop.")
    source_copy_record = json.loads(source_copy_receipt.read_text())
    if (source_copy_record.get("origin") != source_origin or
            source_copy_record.get("copy") != source):
        raise AcceptanceError("The source-copy receipt does not match the manifest and canonical input bytes.")
    if source_copy_record.get("taskId") or source_copy_record.get("resourceId"):
        raise AcceptanceError("This cold source copy was already claimed by another acceptance task; make a fresh copy.")
    output = output_directory(args.output_directory)
    if (args.plan.exists() or log_path_for(args.plan).exists() or
            receipt_path_for(args.plan).exists()):
        raise AcceptanceError("The plan or its evidence already exists; choose a new acceptance plan path to preserve prior evidence.")
    canonical_document = Path(args.canonical_document)
    if not canonical_document.is_file() or str(canonical_document.resolve()) != args.canonical_document:
        raise AcceptanceError("Supply the canonical saved project path for reopening the original Timeline after release.")
    output_path = output / f"native-batch-acceptance-{task_id}.json"
    if output_path.exists():
        raise AcceptanceError("The task-owned output path already exists; use a new disposable task ID.")
    source_copy_record.update({
        "taskId": task_id,
        "resourceId": resource_id,
        "claimedAtUtc": utc_now(),
    })
    atomic_write_json(source_copy_receipt, source_copy_record)
    plan = {
        "schema": PLAN_SCHEMA,
        "taskId": task_id,
        "resourceId": resource_id,
        "source": source,
        "sourceOrigin": source_origin,
        "sourceCopyReceiptPath": str(source_copy_receipt.resolve()),
        "outputDirectory": str(output),
        "outputPath": str(output_path),
        "canonicalDocumentPath": str(canonical_document),
        "batchIds": {"tracks": str(uuid.uuid4()), "authoring": str(uuid.uuid4())},
        "createdAtUtc": utc_now(),
        "requiredCategories": sorted(REQUIRED_ACTION_KINDS),
        "operations": [
            "batchA: create 3 independent video tracks",
            "nonbatch: import one exact admitted photo on its own track",
            "batchB: graphic + title + frame + mask + blend + opacity animation",
            "replay identical batchB ID/body; reject altered body under same ID",
            "save one task-owned native project copy",
            "native Program screenshot with mask and screen blend active",
            "native UI Undo once and verify the pre-batch hash, then Redo once and verify the post-batch hash",
            "finish review, release ownership, drain source SHA, reopen canonical project and recover",
            "confirmed task revert; preserve conflict details if derived source metadata changed the checkpoint",
        ],
    }
    args.plan.parent.mkdir(parents=True, exist_ok=True)
    atomic_write_json(args.plan, plan)
    print(json.dumps({"plan": str(args.plan.resolve()), "taskId": task_id,
                      "resourceId": resource_id, "source": source,
                      "sourceCopyReceiptPath": str(source_copy_receipt.resolve()),
                      "outputPath": str(output_path),
                      "canonicalDocumentPath": str(canonical_document)}, indent=2))


def prepare_source(args: argparse.Namespace) -> None:
    copy_id = args.copy_id.strip()
    if not copy_id or not re.fullmatch(r"[A-Za-z0-9_-]{1,80}", copy_id):
        raise AcceptanceError("Supply a new unique source-copy ID containing only letters, numbers, underscores, or hyphens.")
    origin = source_manifest_entry()
    copy = copy_novel_task_source(copy_id, origin)
    receipt_path = Path(copy["path"]).parent / "source-identity.json"
    receipt = {
        "schema": "looksy.timeline-batch-source-copy/v1",
        "copyId": copy_id,
        "origin": origin,
        "copy": copy,
        "createdAtUtc": utc_now(),
    }
    atomic_write_json(receipt_path, receipt)
    print(json.dumps({"sourceCopy": copy, "sourceOrigin": origin,
                      "sourceCopyReceiptPath": str(receipt_path.resolve()),
                      "admitThisExactPath": copy["path"]}, indent=2))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="phase", required=True)
    source_parser = subparsers.add_parser("prepare-source", help="Make a novel canonical source copy; makes no MCP calls.")
    source_parser.add_argument("--copy-id", required=True)
    prepare_parser = subparsers.add_parser("prepare", help="Write a run-specific local plan; makes no MCP calls.")
    prepare_parser.add_argument("--task-id", required=True)
    prepare_parser.add_argument("--resource-id", required=True)
    prepare_parser.add_argument("--output-directory", required=True)
    prepare_parser.add_argument("--canonical-document", required=True)
    prepare_parser.add_argument("--source-copy", required=True,
                                help="Exact canonical copy path printed by prepare-source and admitted by root.")
    prepare_parser.add_argument("--plan", type=Path, required=True)
    for phase in ("preflight", "author", "replay", "verify-visible", "verify-undo", "verify-redo",
                  "review-release", "drain", "recover", "revert"):
        child = subparsers.add_parser(phase)
        child.add_argument("--plan", type=Path, required=True)
        if phase in {"author", "replay", "review-release", "recover", "revert"}:
            child.add_argument("--execute", action="store_true")
        child.add_argument("--confirm-task-id", default="")
        child.add_argument("--confirm-resource-id", default="")
        if phase == "verify-visible":
            child.add_argument("--evidence-visible", required=True)
            child.add_argument("--reviewer-type", choices=("assistant", "user"), default="assistant")
        if phase == "verify-undo":
            child.add_argument("--evidence-after-undo", required=True)
        if phase == "verify-redo":
            child.add_argument("--evidence-after-redo", required=True)
        if phase == "drain":
            child.add_argument("--drain-timeout", type=int, default=90)
        if phase == "revert":
            child.add_argument("--confirm-revert", action="store_true")
    args = parser.parse_args()
    try:
        if args.phase == "prepare":
            prepare(args)
            return 0
        if args.phase == "prepare-source":
            prepare_source(args)
            return 0
        plan, plan_sha = load_plan(args.plan)
        if args.phase == "preflight":
            if getattr(args, "execute", False):
                raise AcceptanceError("Preflight is read-only; omit --execute.")
            phase_preflight(args.plan, plan_sha, plan)
        elif args.phase == "author":
            phase_author(args.plan, plan_sha, plan, args)
        elif args.phase == "replay":
            phase_replay(args.plan, plan_sha, plan, args)
        elif args.phase == "verify-visible":
            phase_verify_visible(args.plan, plan_sha, plan, args)
        elif args.phase == "verify-undo":
            phase_verify_undo(args.plan, plan_sha, plan, args)
        elif args.phase == "verify-redo":
            phase_verify_redo(args.plan, plan_sha, plan, args)
        elif args.phase == "review-release":
            phase_review_release(args.plan, plan_sha, plan, args)
        elif args.phase == "drain":
            phase_drain(args.plan, plan_sha, plan, args)
        elif args.phase == "recover":
            phase_recover(args.plan, plan_sha, plan, args)
        elif args.phase == "revert":
            phase_revert(args.plan, plan_sha, plan, args)
        return 0
    except Exception as error:
        print(f"batch acceptance {args.phase} stopped: {error}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
