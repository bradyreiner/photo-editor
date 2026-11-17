"""Execute a reviewed authoring plan through Looksy's typed native MCP commands.

The plan is a sequence of app commands, never a serialized Timeline document.
Safe synchronous Timeline edits are grouped into deterministic bounded native
transactions; imports, readiness checks, saves, and other non-batch actions stay
individual. Each completed mutation is receipted so an interrupted build can
resume without duplicating clips. Native task ownership and schemas remain
authoritative.

The append-only NDJSON sidecar is the durable execution log. It records an
intent before each mutation, the native response afterward, and monotonic
timings. The human-readable JSON receipt is materialized once per run rather
than rewritten after every operation.
"""
from datetime import datetime, timezone
from pathlib import Path
import argparse
import hashlib
import json
import math
import os
import time
import uuid

from discover import unpack
from native_mcp import call, validate_tool_request_size

ROOT = Path(__file__).resolve().parent
RECEIPT_SCHEMA = "looksy.native-authoring-receipt/v1"
BATCH_COMMAND_ID = "timeline.action.applyDraftBatch"
BATCH_SAFE_COMMAND_IDS = frozenset({
    "timeline.action.createTimelineTrack",
    "timeline.action.moveTimelineGraphicsTrack",
    "timeline.action.createTimelineTitle",
    "timeline.action.createTimelineGraphic",
    "timeline.action.createTimelineGenerator",
    "timeline.action.setTimelineClipTiming",
    "timeline.action.setTimelineClipTitle",
    "timeline.action.setTimelineClipFrame",
    "timeline.action.setTimelineClipMask",
    "timeline.action.clearTimelineClipMask",
    "timeline.action.setTimelineClipBlendMode",
    "timeline.action.setTimelineClipAnimation",
    "timeline.action.editTimelineClipKeyframe",
})
BATCH_MAX_OPERATIONS = 16
# Leave room for Qt's JSON number/escape formatting to differ slightly from
# Python's while staying inside the native 256 KiB hard limit.
BATCH_MAX_BYTES = 255 * 1024
BATCH_MAX_FRAME_ITEMS = 2048


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def compact_json_bytes(value):
    return json.dumps(value, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def append_event(path, event):
    """Append and fsync one bounded JSON line; return its write+fsync time."""
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = compact_json_bytes(event) + b"\n"
    started = time.perf_counter_ns()
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
    try:
        offset = 0
        while offset < len(payload):
            offset += os.write(fd, payload[offset:])
        os.fsync(fd)
    finally:
        os.close(fd)
    return (time.perf_counter_ns() - started) / 1_000_000.0, len(payload)


def atomic_write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("wb") as output:
        output.write(payload)
        output.flush()
        os.fsync(output.fileno())
    os.replace(temporary, path)
    directory_fd = os.open(path.parent, os.O_RDONLY)
    try:
        os.fsync(directory_fd)
    finally:
        os.close(directory_fd)


def call_with_timing(name, arguments):
    request_bytes = len(compact_json_bytes({"name": name, "arguments": arguments}))
    started = time.perf_counter_ns()
    raw = call(name, arguments)
    rpc_ms = (time.perf_counter_ns() - started) / 1_000_000.0
    response_bytes = len(compact_json_bytes(raw))
    return raw, unpack(raw), rpc_ms, request_bytes, response_bytes


def fail_if_native_error(raw, result):
    if raw.get("error") or result.get("isError") or result.get("ok") is False:
        raise RuntimeError(json.dumps(raw, ensure_ascii=False))


def resolve(value, results):
    if isinstance(value, dict):
        if set(value) == {"result", "field"}:
            return results[value["result"]][value["field"]]
        return {key: resolve(item, results) for key, item in value.items()}
    if isinstance(value, list):
        return [resolve(item, results) for item in value]
    return value


def _resolve_batch_values(value, results, local_operation_ids, current_position,
                          local_positions):
    """Resolve earlier completed steps and retain earlier in-batch references."""
    if isinstance(value, dict):
        if "result" in value or "field" in value:
            if set(value) != {"result", "field"}:
                raise RuntimeError("A plan stable-ID reference must contain only result and field.")
            result_id = value["result"]
            field = value["field"]
            if not isinstance(result_id, str) or not isinstance(field, str):
                raise RuntimeError("A plan stable-ID reference must use string result and field values.")
            if result_id in local_operation_ids:
                if local_positions[result_id] >= current_position:
                    raise RuntimeError(
                        "A batch action may reference only an earlier operation result: " + result_id
                    )
                return {"result": local_operation_ids[result_id], "field": field}
            try:
                return results[result_id][field]
            except KeyError as error:
                raise RuntimeError(
                    f"Plan step {result_id} has no completed result field {field!r}."
                ) from error
        return {
            key: _resolve_batch_values(item, results, local_operation_ids,
                                       current_position, local_positions)
            for key, item in value.items()
        }
    if isinstance(value, list):
        return [
            _resolve_batch_values(item, results, local_operation_ids,
                                  current_position, local_positions)
            for item in value
        ]
    return value


def _count_batch_frame_items(value):
    """Mirror the native batch limit for keyframe and vector frame samples."""
    if isinstance(value, dict):
        count = 0
        for key, item in value.items():
            if key in ("keyframes", "frames") and isinstance(item, list):
                count += len(item)
            count += _count_batch_frame_items(item)
        return count
    if isinstance(value, list):
        return sum(_count_batch_frame_items(item) for item in value)
    return 0


def _build_batch_request(plan_digest, steps, result_by_id):
    """Build deterministic, schema-shaped operations and their batch ID."""
    step_ids = [step["id"] for step in steps]
    operation_ids = {step_id: f"op{index:03d}" for index, step_id in enumerate(step_ids)}
    local_positions = {step_id: index for index, step_id in enumerate(step_ids)}
    seed = "looksy-native-authoring-batch/v1:" + plan_digest + ":" + "\n".join(step_ids)
    batch_id = str(uuid.uuid5(uuid.NAMESPACE_URL, seed))
    operations = []
    frame_items = 0
    for index, step in enumerate(steps):
        command_id = step["commandId"]
        if command_id not in BATCH_SAFE_COMMAND_IDS:
            raise RuntimeError("Unsafe command entered a draft batch: " + command_id)
        values = _resolve_batch_values(step["values"], result_by_id,
                                       operation_ids, index, local_positions)
        frame_items += _count_batch_frame_items(values)
        if command_id == "timeline.action.editTimelineClipKeyframe":
            frame_items += 1
        operations.append({
            "id": operation_ids[step["id"]],
            "commandId": command_id,
            "values": values,
        })
    request_values = {"batchId": batch_id, "operations": operations}
    request_bytes = len(compact_json_bytes(request_values))
    return {
        "batchId": batch_id,
        "values": request_values,
        "operations": operations,
        "operationIds": operation_ids,
        "frameItems": frame_items,
        "requestBytes": request_bytes,
        "fitsLimits": (len(operations) <= BATCH_MAX_OPERATIONS and
                       frame_items <= BATCH_MAX_FRAME_ITEMS and
                       request_bytes <= BATCH_MAX_BYTES),
    }


def _batch_result_rows(result):
    rows = result.get("batchResults")
    if not isinstance(rows, list):
        nested = result.get("result", {})
        rows = nested.get("batchResults") if isinstance(nested, dict) else None
    return rows if isinstance(rows, list) else []


def _compact_batch_step_result(row, command_id, batch_id, idempotent_replay):
    result = {"ok": True, "commandId": command_id, "batchId": batch_id}
    for key in ("createdClipId", "trackId", "alreadySatisfied"):
        if key in row:
            result[key] = row[key]
    if idempotent_replay:
        result["idempotentReplay"] = True
    return result


def _read_prior_receipt(receipt_path, plan_digest):
    if not receipt_path.exists():
        return {}
    receipt = json.loads(receipt_path.read_text())
    if receipt.get("planSha256") != plan_digest:
        raise RuntimeError("Plan changed; use a new receipt and explicit native baseline.")
    return receipt


def _read_event_log(log_path, plan_digest):
    events = []
    if not log_path.exists():
        return events
    with log_path.open("r", encoding="utf-8") as source:
        for line_number, line in enumerate(source, 1):
            if not line.strip():
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError as error:
                raise RuntimeError(f"The append-only receipt is damaged at line {line_number}: {error}") from error
            if event.get("schema") != RECEIPT_SCHEMA or event.get("planSha256") != plan_digest:
                raise RuntimeError(f"The append-only receipt does not match this plan at line {line_number}.")
            events.append(event)
    return events


def _completed_from_events(events):
    completed = {}
    started = {}
    failed = {}
    timings = {}
    for event in events:
        event_kind = event.get("event")
        step_id = event.get("stepId")
        if event_kind == "invoke_started" and step_id is not None:
            if step_id in started and step_id not in completed and step_id not in failed:
                raise RuntimeError(f"Duplicate unresolved authoring intent for step {step_id}.")
            started[step_id] = event
        elif event_kind == "invoke_completed" and step_id is not None:
            if step_id in completed:
                raise RuntimeError(f"Duplicate completed authoring receipt for step {step_id}.")
            completed[step_id] = event
        elif event_kind == "invoke_failed" and step_id is not None:
            failed[step_id] = event
        elif event_kind == "step_timing" and step_id is not None:
            timings[step_id] = event.get("timing", {})
    unresolved = sorted(set(started) - set(completed) - set(failed))
    if unresolved:
        joined = ", ".join(str(value) for value in unresolved[:8])
        raise RuntimeError(
            "A prior MCP call began but has no durable result receipt (step " + joined + "). "
            "Inspect the native task before retrying; the runner will not replay it."
        )
    if failed:
        first = next(iter(failed.values()))
        raise RuntimeError(
            "The NDJSON log contains a failed native call for step " + str(first.get("stepId")) +
            ". Inspect the native task and use a new or reconciled receipt before resuming."
        )
    return completed, timings


def _append_read_timing(log_path, plan_digest, command_name, arguments):
    raw = None
    result = None
    rpc_ms = None
    request_bytes = len(compact_json_bytes({"name": command_name, "arguments": arguments}))
    try:
        raw, result, rpc_ms, request_bytes, response_bytes = call_with_timing(command_name, arguments)
        fail_if_native_error(raw, result)
    except Exception as error:
        event = {
            "schema": RECEIPT_SCHEMA,
            "planSha256": plan_digest,
            "event": "read_call_failed",
            "tool": command_name,
            "arguments": arguments,
            "atUtc": utc_now(),
            "error": str(error),
            "rpcMs": rpc_ms,
            "requestBytes": request_bytes,
        }
        write_ms, line_bytes = append_event(log_path, event)
        append_event(log_path, {
            "schema": RECEIPT_SCHEMA,
            "planSha256": plan_digest,
            "event": "read_call_timing",
            "tool": command_name,
            "atUtc": utc_now(),
            "timing": {"rpcMs": rpc_ms, "receiptWriteMs": write_ms,
                       "requestBytes": request_bytes, "recordBytes": line_bytes},
        })
        raise
    event = {
        "schema": RECEIPT_SCHEMA,
        "planSha256": plan_digest,
        "event": "read_call_completed",
        "tool": command_name,
        "arguments": arguments,
        "atUtc": utc_now(),
        "rpcMs": rpc_ms,
        "requestBytes": request_bytes,
        "responseBytes": response_bytes,
    }
    write_ms, line_bytes = append_event(log_path, event)
    append_event(log_path, {
        "schema": RECEIPT_SCHEMA,
        "planSha256": plan_digest,
        "event": "read_call_timing",
        "tool": command_name,
        "atUtc": utc_now(),
        "timing": {"rpcMs": rpc_ms, "receiptWriteMs": write_ms,
                   "requestBytes": request_bytes, "responseBytes": response_bytes,
                   "recordBytes": line_bytes},
    })
    return raw, result


def _append_summary(receipt_path, plan_digest, steps, status, failure=None, totals=None):
    receipt = {"planSha256": plan_digest, "steps": steps, "status": status}
    if failure is not None:
        receipt["failure"] = failure
    if totals is not None:
        receipt["timingSummary"] = totals
    atomic_write_json(receipt_path, receipt)


def _plan_composition(plan):
    """Return validated geometry declared by this plan, or None for legacy plans."""
    metadata_keys = ("composition", "canvas", "sequence")
    containers = [(key, plan[key]) for key in metadata_keys if key in plan]
    if not containers:
        return None

    expected = {}
    for key, value in containers:
        if not isinstance(value, dict):
            raise RuntimeError(f"Plan {key} metadata must be an object before native authoring.")
        for field, aliases in {
                "width": ("width",),
                "height": ("height",),
                "fps": ("fps", "frameRate", "frame_rate"),
        }.items():
            present = [value[alias] for alias in aliases if alias in value]
            if not present:
                continue
            if any(candidate != present[0] for candidate in present[1:]):
                raise RuntimeError(f"Plan {key} metadata contains conflicting {field} values.")
            candidate = present[0]
            if (isinstance(candidate, bool) or not isinstance(candidate, (int, float)) or
                    not math.isfinite(float(candidate)) or candidate <= 0):
                raise RuntimeError(f"Plan {key} metadata has an invalid {field} value.")
            if field in ("width", "height") and int(candidate) != candidate:
                raise RuntimeError(f"Plan {key} metadata {field} must be a positive integer.")
            normalized = int(candidate) if field in ("width", "height") else float(candidate)
            if field in expected and expected[field] != normalized:
                raise RuntimeError(f"Plan geometry metadata declares conflicting {field} values.")
            expected[field] = normalized

    if "width" not in expected or "height" not in expected:
        raise RuntimeError("Plan geometry metadata must declare both width and height.")
    return expected


def _preflight_timeline_destination(plan, log_path, plan_digest, require_starting_hash=True,
                                    compact_inspection=False):
    """Fail closed before mutation unless the ready native Timeline matches the plan."""
    expected = _plan_composition(plan)
    if expected is None:
        return

    requested_resource = plan.get("resourceId")
    requested_task = plan.get("taskId")
    if requested_resource is not None and (not isinstance(requested_resource, str) or not requested_resource.strip()):
        raise RuntimeError("Plan resourceId must be a nonempty native Timeline resource ID.")
    if requested_task is not None and (not isinstance(requested_task, str) or not requested_task.strip()):
        raise RuntimeError("Plan taskId must be a nonempty active native task ID.")

    if compact_inspection:
        _, document = _append_read_timing(
            log_path, plan_digest, "timeline.inspect_document", {})
        if document.get("contract") != "looksy.native-timeline-inspection/v1":
            raise RuntimeError("The compact native Timeline inspector contract is unavailable; refusing authoring.")
    else:
        inspect_command_id = "timeline.method.inspectAgentDocument()"
        _, schema = _append_read_timing(
            log_path, plan_digest, "app.command_schema", {"commandId": inspect_command_id})
        if (schema.get("available") is not True or schema.get("readOnly") is not True or
                schema.get("commandId") != inspect_command_id):
            raise RuntimeError("The read-only native Timeline inspector is unavailable; refusing authoring.")

        _, document = _append_read_timing(
            log_path, plan_digest, "app.inspect_command",
            {"commandId": inspect_command_id, "values": {}})
    if document.get("ok") is not True or document.get("ready") is not True:
        raise RuntimeError("The open native Timeline is not ready; no plan mutations were sent.")
    resource_id = document.get("resourceId")
    if not isinstance(resource_id, str) or not resource_id.strip():
        raise RuntimeError("The native Timeline inspector returned no stable resourceId; refusing authoring.")
    if requested_resource is not None and requested_resource != resource_id:
        raise RuntimeError(
            f"Plan resourceId {requested_resource!r} does not match the open Timeline {resource_id!r}.")
    starting_hash = plan.get("startingDocumentHash")
    if starting_hash is not None:
        if (not isinstance(starting_hash, str) or len(starting_hash) != 64 or
                any(character not in "0123456789abcdef" for character in starting_hash)):
            raise RuntimeError("Plan startingDocumentHash must be an exact lowercase SHA-256.")
        if require_starting_hash and document.get("documentHash") != starting_hash:
            raise RuntimeError("The native document changed after continuation planning; no mutations were sent.")

    actual_dimensions = {"width": document.get("width"), "height": document.get("height")}
    for field in ("width", "height"):
        actual = actual_dimensions[field]
        if (isinstance(actual, bool) or not isinstance(actual, (int, float)) or
                not math.isfinite(float(actual)) or actual <= 0 or int(actual) != actual):
            raise RuntimeError(f"The native Timeline inspector returned an invalid {field}.")
        if actual != expected[field]:
            raise RuntimeError(
                f"Plan {field} {expected[field]} does not match the open Timeline {field} {actual}.")
    if "fps" in expected:
        actual_fps = document.get("frameRate")
        if (isinstance(actual_fps, bool) or not isinstance(actual_fps, (int, float)) or
                not math.isfinite(float(actual_fps)) or
                not math.isclose(float(actual_fps), expected["fps"], rel_tol=0.0, abs_tol=1e-9)):
            raise RuntimeError(
                f"Plan fps {expected['fps']:g} does not match the open Timeline frameRate {actual_fps!r}.")

    _, task = _append_read_timing(log_path, plan_digest, "timeline.inspect_task", {})
    if (task.get("ok") is not True or task.get("active") is not True or
            task.get("status") != "running"):
        raise RuntimeError("A running native Timeline task is required before authoring this geometry-scoped plan.")
    task_id = task.get("taskId")
    if not isinstance(task_id, str) or not task_id.strip():
        raise RuntimeError("The active native Timeline task has no stable taskId; refusing authoring.")
    if requested_task is not None and requested_task != task_id:
        raise RuntimeError(f"Plan taskId {requested_task!r} does not match the active native task {task_id!r}.")

    target_ids = set()
    targets = task.get("targetResourceIds", [])
    if isinstance(targets, list):
        target_ids.update(target for target in targets if isinstance(target, str))
    target = task.get("targetResourceId")
    if isinstance(target, str):
        target_ids.add(target)
    for container_name in ("approvedSnapshot", "nativeTimelineScope"):
        container = task.get(container_name)
        if isinstance(container, dict):
            targets = container.get("targetIds", [])
            if isinstance(targets, list):
                target_ids.update(target for target in targets if isinstance(target, str))
    if resource_id not in target_ids:
        raise RuntimeError(
            "The active native Timeline task does not target the inspected Timeline resource; no plan mutations were sent.")


def execute(plan_path, receipt_path, ndjson_path=None, compact_inspection=False):
    plan_bytes = plan_path.read_bytes()
    plan = json.loads(plan_bytes)
    digest = hashlib.sha256(plan_bytes).hexdigest()
    log_path = ndjson_path or receipt_path.with_name(receipt_path.name + ".ndjson")
    prior_receipt = _read_prior_receipt(receipt_path, digest)
    events = _read_event_log(log_path, digest)
    event_completed, event_timings = _completed_from_events(events)

    completed = {}
    timing_by_id = {}
    for item in prior_receipt.get("steps", []):
        step_id = item.get("id")
        if step_id in completed:
            raise RuntimeError(f"Duplicate step {step_id} in the existing JSON receipt.")
        completed[step_id] = item
        if "timing" in item:
            timing_by_id[step_id] = item["timing"]

    for step_id, event in event_completed.items():
        event_step = {
            "id": step_id,
            "commandId": event.get("commandId"),
            "values": event.get("values", {}),
            "result": event.get("result", {}),
        }
        if step_id in completed:
            existing = completed[step_id]
            if existing.get("commandId") != event_step["commandId"] or existing.get("result") != event_step["result"]:
                raise RuntimeError(f"Conflicting JSON and NDJSON receipts for step {step_id}.")
        else:
            completed[step_id] = event_step
        if step_id in event_timings:
            timing_by_id[step_id] = event_timings[step_id]

    result_by_id = {step_id: item.get("result", {}) for step_id, item in completed.items()}
    planned_steps = {step["id"]: step for step in plan["steps"]}
    for step_id in completed:
        if step_id not in planned_steps:
            raise RuntimeError(f"Receipt step {step_id} is not present in the current plan.")

    if any(step_id not in completed for step_id in planned_steps):
        # Catch an intrinsically oversized operation before any plan mutation.
        # Resolve again just before invocation because earlier results can grow
        # a small placeholder (for example a generated path or ID).
        for step in plan["steps"]:
            if step["id"] not in completed:
                validate_tool_request_size("app.invoke_command", {
                    "commandId": step["commandId"], "values": step["values"]})
        _preflight_timeline_destination(plan, log_path, digest,
                                        require_starting_hash=not completed,
                                        compact_inspection=compact_inspection)

    if not any(event.get("event") == "plan_opened" for event in events):
        append_event(log_path, {
            "schema": RECEIPT_SCHEMA,
            "planSha256": digest,
            "event": "plan_opened",
            "atUtc": utc_now(),
            "planPath": str(plan_path.resolve()),
            "stepCount": len(plan["steps"]),
        })

    schemas = {}

    def ensure_schema(command_id):
        if command_id in schemas:
            return
        _, schema = _append_read_timing(
            log_path, digest, "app.command_schema", {"commandId": command_id})
        if not schema.get("available") or schema.get("readOnly"):
            raise RuntimeError("Mutable native command unavailable: " + command_id)
        schemas[command_id] = schema

    def execute_batch(batch_steps, request):
        ensure_schema(BATCH_COMMAND_ID)
        for batch_step in batch_steps:
            ensure_schema(batch_step["commandId"])
        validate_tool_request_size("app.invoke_command", {
            "commandId": BATCH_COMMAND_ID, "values": request["values"]})

        intent_timings = []
        for index, batch_step in enumerate(batch_steps):
            operation = request["operations"][index]
            intent = {
                "schema": RECEIPT_SCHEMA,
                "planSha256": digest,
                "event": "invoke_started",
                "stepId": batch_step["id"],
                "commandId": batch_step["commandId"],
                "values": batch_step["values"],
                "batchId": request["batchId"],
                "batchOperationId": operation["id"],
                "batchOperationValues": operation["values"],
                "batchOperationCount": len(batch_steps),
                "atUtc": utc_now(),
            }
            write_ms, record_bytes = append_event(log_path, intent)
            intent_timings.append({"writeMs": write_ms, "recordBytes": record_bytes})

        invoke_started = time.perf_counter_ns()
        request_bytes = len(compact_json_bytes({
            "commandId": BATCH_COMMAND_ID,
            "values": request["values"],
        }))
        raw = None
        native_result = {}
        response_bytes = 0
        mutation_uncertain = True
        try:
            raw = call("app.invoke_command", {
                "commandId": BATCH_COMMAND_ID,
                "values": request["values"],
            })
            rpc_ms = (time.perf_counter_ns() - invoke_started) / 1_000_000.0
            response_bytes = len(compact_json_bytes(raw))
            native_result = unpack(raw)
            if (native_result.get("ok") is False and
                    (native_result.get("rolledBack") or
                     native_result.get("mutationNotApplied"))):
                mutation_uncertain = False
            fail_if_native_error(raw, native_result)
            if native_result.get("ok") is not True:
                raise RuntimeError("The native authoring batch did not return ok:true.")

            rows = _batch_result_rows(native_result)
            if len(rows) != len(batch_steps):
                raise RuntimeError("The native authoring batch returned an incomplete result list.")
            expected_by_operation = {
                request["operationIds"][batch_step["id"]]: batch_step
                for batch_step in batch_steps
            }
            rows_by_operation = {}
            for row in rows:
                if not isinstance(row, dict):
                    raise RuntimeError("The native authoring batch returned a malformed operation result.")
                operation_id = row.get("id")
                expected = expected_by_operation.get(operation_id)
                if (expected is None or operation_id in rows_by_operation or
                        row.get("commandId") != expected["commandId"] or row.get("ok") is not True):
                    raise RuntimeError("The native authoring batch result IDs or action statuses do not match the request.")
                rows_by_operation[operation_id] = row

            if len(rows_by_operation) != len(batch_steps):
                raise RuntimeError("The native authoring batch omitted one or more operation results.")

            idempotent_replay = bool(native_result.get("idempotentReplay"))
            pending_results = dict(result_by_id)
            resolved_values = []
            step_results = []
            for batch_step in batch_steps:
                operation_id = request["operationIds"][batch_step["id"]]
                row = rows_by_operation[operation_id]
                step_result = _compact_batch_step_result(
                    row, batch_step["commandId"], request["batchId"], idempotent_replay)
                # Resolve the reviewed plan references to the native stable IDs
                # before writing ordinary per-step completion events.
                step_values = resolve(batch_step["values"], pending_results)
                resolved_values.append(step_values)
                step_results.append(step_result)
                pending_results[batch_step["id"]] = step_result
            mutation_uncertain = False
        except Exception as error:
            rpc_ms = (time.perf_counter_ns() - invoke_started) / 1_000_000.0
            response_bytes = len(compact_json_bytes(raw)) if raw is not None else 0
            failure_text = str(error)
            rolled_back = bool(native_result.get("rolledBack"))
            recovery_required = (bool(native_result.get("recoveryRequired")) or
                                 mutation_uncertain)
            first_step_id = batch_steps[0]["id"]
            for index, batch_step in enumerate(batch_steps):
                operation = request["operations"][index]
                failure = {
                    "schema": RECEIPT_SCHEMA,
                    "planSha256": digest,
                    "event": "invoke_failed",
                    "stepId": batch_step["id"],
                    "commandId": batch_step["commandId"],
                    "batchId": request["batchId"],
                    "batchOperationId": operation["id"],
                    "batchOperationCount": len(batch_steps),
                    "atUtc": utc_now(),
                    "rpcMs": rpc_ms if index == 0 else 0.0,
                    "error": failure_text,
                    "rolledBack": rolled_back,
                    "recoveryRequired": recovery_required,
                }
                failure_write_ms, failure_record_bytes = append_event(log_path, failure)
                timing = {
                    "intentReceiptWriteMs": intent_timings[index]["writeMs"],
                    "failureReceiptWriteMs": failure_write_ms,
                    "rpcMs": rpc_ms if index == 0 else 0.0,
                    "requestBytes": request_bytes if index == 0 else 0,
                    "responseBytes": response_bytes if index == 0 else 0,
                    "failureRecordBytes": failure_record_bytes,
                    "intentRecordBytes": intent_timings[index]["recordBytes"],
                    "batchId": request["batchId"],
                    "batchOperationCount": len(batch_steps),
                }
                append_event(log_path, {
                    "schema": RECEIPT_SCHEMA,
                    "planSha256": digest,
                    "event": "step_timing",
                    "stepId": batch_step["id"],
                    "atUtc": utc_now(),
                    "timing": timing,
                })
            failure_summary = {
                "stepId": first_step_id,
                "batchId": request["batchId"],
                "error": failure_text,
                "rolledBack": rolled_back,
                "recoveryRequired": recovery_required,
            }
            _append_summary(receipt_path, digest,
                            [completed[key] for key in planned_steps if key in completed],
                            "incomplete", failure_summary)
            raise RuntimeError(failure_text) from error

        request_sha256 = native_result.get("batchRequestSha256", "")
        for index, batch_step in enumerate(batch_steps):
            operation = request["operations"][index]
            step_result = step_results[index]
            step_values = resolved_values[index]
            completed_record = {
                "schema": RECEIPT_SCHEMA,
                "planSha256": digest,
                "event": "invoke_completed",
                "stepId": batch_step["id"],
                "commandId": batch_step["commandId"],
                "values": step_values,
                "result": step_result,
                "batchId": request["batchId"],
                "batchOperationId": operation["id"],
                "batchOperationCount": len(batch_steps),
                "batchRequestSha256": request_sha256,
                "idempotentReplay": idempotent_replay,
                "atUtc": utc_now(),
                "rpcMs": rpc_ms if index == 0 else 0.0,
                "requestBytes": request_bytes if index == 0 else 0,
                "responseBytes": response_bytes if index == 0 else 0,
            }
            result_write_ms, result_record_bytes = append_event(log_path, completed_record)
            timing = {
                "intentReceiptWriteMs": intent_timings[index]["writeMs"],
                "rpcMs": rpc_ms if index == 0 else 0.0,
                "resultReceiptWriteMs": result_write_ms,
                "elapsedMs": rpc_ms if index == 0 else 0.0,
                "requestBytes": request_bytes if index == 0 else 0,
                "responseBytes": response_bytes if index == 0 else 0,
                "resultBytes": len(compact_json_bytes(step_result)),
                "intentRecordBytes": intent_timings[index]["recordBytes"],
                "resultRecordBytes": result_record_bytes,
                "batchId": request["batchId"],
                "batchOperationId": operation["id"],
                "batchOperationCount": len(batch_steps),
                "idempotentReplay": idempotent_replay,
            }
            append_event(log_path, {
                "schema": RECEIPT_SCHEMA,
                "planSha256": digest,
                "event": "step_timing",
                "stepId": batch_step["id"],
                "atUtc": utc_now(),
                "timing": timing,
            })
            step_record = {"id": batch_step["id"],
                           "commandId": batch_step["commandId"],
                           "values": step_values,
                           "result": step_result,
                           "timing": timing}
            completed[batch_step["id"]] = step_record
            timing_by_id[batch_step["id"]] = timing
            result_by_id[batch_step["id"]] = step_result

        print(f"batch {request['batchId']} {len(batch_steps)} operations "
              f"rpc={rpc_ms:.1f}ms", flush=True)

    plan_steps = plan["steps"]
    if any(step.get("commandId") in BATCH_SAFE_COMMAND_IDS and
           step.get("id") not in completed for step in plan_steps):
        # Fail before any new mutation if the app cannot run the bounded native
        # transaction used by this runner version.
        ensure_schema(BATCH_COMMAND_ID)
        for command_id in sorted({
                step["commandId"] for step in plan_steps
                if step.get("commandId") in BATCH_SAFE_COMMAND_IDS and
                step.get("id") not in completed}):
            ensure_schema(command_id)

    cursor = 0
    while cursor < len(plan_steps):
        step = plan_steps[cursor]
        step_id = step["id"]
        command_id = step["commandId"]
        existing = completed.get(step_id)
        if existing is not None:
            values = resolve(step["values"], result_by_id)
            if existing.get("commandId") != command_id or existing.get("values") != values:
                raise RuntimeError(f"The stored receipt for step {step_id} does not match the reviewed plan inputs.")
            result_by_id[step_id] = existing.get("result", {})
            cursor += 1
            continue

        if command_id in BATCH_SAFE_COMMAND_IDS:
            batch_steps = []
            batch_request = None
            for candidate in plan_steps[cursor:cursor + BATCH_MAX_OPERATIONS]:
                if (candidate.get("id") in completed or
                        candidate.get("commandId") not in BATCH_SAFE_COMMAND_IDS):
                    break
                trial_steps = batch_steps + [candidate]
                trial_request = _build_batch_request(digest, trial_steps, result_by_id)
                if not trial_request["fitsLimits"]:
                    break
                batch_steps = trial_steps
                batch_request = trial_request
            if batch_steps:
                execute_batch(batch_steps, batch_request)
                cursor += len(batch_steps)
                continue

        values = resolve(step["values"], result_by_id)
        ensure_schema(command_id)
        validate_tool_request_size("app.invoke_command", {
            "commandId": command_id, "values": values})

        intent = {
            "schema": RECEIPT_SCHEMA,
            "planSha256": digest,
            "event": "invoke_started",
            "stepId": step_id,
            "commandId": command_id,
            "values": values,
            "atUtc": utc_now(),
        }
        intent_write_ms, intent_record_bytes = append_event(log_path, intent)
        invoke_started = time.perf_counter_ns()
        request_bytes = len(compact_json_bytes({"commandId": command_id, "values": values}))
        try:
            raw = call("app.invoke_command", {"commandId": command_id, "values": values})
            rpc_ms = (time.perf_counter_ns() - invoke_started) / 1_000_000.0
            response_bytes = len(compact_json_bytes(raw))
            result = unpack(raw)
            fail_if_native_error(raw, result)
        except Exception as error:
            rpc_ms = (time.perf_counter_ns() - invoke_started) / 1_000_000.0
            failure = {
                "schema": RECEIPT_SCHEMA,
                "planSha256": digest,
                "event": "invoke_failed",
                "stepId": step_id,
                "commandId": command_id,
                "atUtc": utc_now(),
                "rpcMs": rpc_ms,
                "requestBytes": request_bytes,
                "error": str(error),
            }
            failure_write_ms, failure_record_bytes = append_event(log_path, failure)
            elapsed_ms = (time.perf_counter_ns() - invoke_started) / 1_000_000.0
            append_event(log_path, {
                "schema": RECEIPT_SCHEMA,
                "planSha256": digest,
                "event": "step_timing",
                "stepId": step_id,
                "atUtc": utc_now(),
                "timing": {
                    "intentReceiptWriteMs": intent_write_ms,
                    "rpcMs": rpc_ms,
                    "failureReceiptWriteMs": failure_write_ms,
                    "elapsedMs": elapsed_ms,
                    "requestBytes": request_bytes,
                    "failureRecordBytes": failure_record_bytes,
                    "intentRecordBytes": intent_record_bytes,
                },
            })
            _append_summary(receipt_path, digest,
                            [completed[key] for key in planned_steps if key in completed],
                            "incomplete", {"stepId": step_id, "error": str(error)})
            raise

        completed_record = {
            "schema": RECEIPT_SCHEMA,
            "planSha256": digest,
            "event": "invoke_completed",
            "stepId": step_id,
            "commandId": command_id,
            "values": values,
            "result": result,
            "atUtc": utc_now(),
            "rpcMs": rpc_ms,
            "requestBytes": request_bytes,
            "responseBytes": response_bytes,
        }
        result_write_ms, result_record_bytes = append_event(log_path, completed_record)
        timing = {
            "intentReceiptWriteMs": intent_write_ms,
            "rpcMs": rpc_ms,
            "resultReceiptWriteMs": result_write_ms,
            "elapsedMs": (time.perf_counter_ns() - invoke_started) / 1_000_000.0,
            "requestBytes": request_bytes,
            "responseBytes": response_bytes,
            "resultBytes": len(compact_json_bytes(result)),
            "intentRecordBytes": intent_record_bytes,
            "resultRecordBytes": result_record_bytes,
        }
        append_event(log_path, {
            "schema": RECEIPT_SCHEMA,
            "planSha256": digest,
            "event": "step_timing",
            "stepId": step_id,
            "atUtc": utc_now(),
            "timing": timing,
        })

        step_record = {"id": step_id, "commandId": command_id,
                       "values": values, "result": result, "timing": timing}
        completed[step_id] = step_record
        timing_by_id[step_id] = timing
        result_by_id[step_id] = result
        stable_result = result.get("createdClipId", result.get("trackId", "ok"))
        print(step_id, stable_result, f"rpc={rpc_ms:.1f}ms receipt={result_write_ms:.1f}ms", flush=True)
        cursor += 1

    final_steps = []
    total_rpc_ms = 0.0
    total_receipt_write_ms = 0.0
    for step in plan["steps"]:
        record = completed[step["id"]]
        final_record = {"id": step["id"], "commandId": step["commandId"],
                        "values": record.get("values", {}),
                        "result": record.get("result", {})}
        timing = timing_by_id.get(step["id"], record.get("timing"))
        if timing:
            final_record["timing"] = timing
            total_rpc_ms += float(timing.get("rpcMs") or 0.0)
            total_receipt_write_ms += float(timing.get("resultReceiptWriteMs") or 0.0)
            total_receipt_write_ms += float(timing.get("intentReceiptWriteMs") or 0.0)
        final_steps.append(final_record)
    _append_summary(receipt_path, digest, final_steps,
                    "authored-awaiting-native-review", totals={
                        "nativeRpcMs": round(total_rpc_ms, 3),
                        "authorReceiptWriteMs": round(total_receipt_write_ms, 3),
                        "mutations": len(final_steps),
                        "timedMutations": sum(1 for step in final_steps if "timing" in step),
                        "ndjsonPath": str(log_path.resolve()),
                    })


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("plan", type=Path)
    parser.add_argument("--receipt", type=Path, required=True)
    parser.add_argument("--ndjson", type=Path,
                        help="Append-only execution log; defaults to <receipt>.ndjson.")
    parser.add_argument("--compact-inspection", action="store_true",
                        help="Require the compact native Timeline inspector for preflight.")
    args = parser.parse_args()
    execute(args.plan, args.receipt, args.ndjson, args.compact_inspection)
