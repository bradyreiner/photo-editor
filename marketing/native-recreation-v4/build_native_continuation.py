"""Build an offline, fail-closed continuation plan from native receipts.

This module reads immutable authoring plans, runner receipts, and native task
inspection snapshots. It never calls MCP and never edits the source plans or
their receipts. Output contains only actions not proven complete in the
verified prefix.
"""

from pathlib import Path
import argparse
import hashlib
import json
import math
import os
import sys


HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import author_native  # noqa: E402


RECEIPT_SCHEMA = "looksy.native-authoring-receipt/v1"
CONTINUATION_SCHEMA = "looksy.native-authoring-continuation/v1"
BATCH_COMMAND_ID = "timeline.action.applyDraftBatch"
ALLOWED_EVENTS = {
    "plan_opened",
    "read_call_completed",
    "read_call_timing",
    "invoke_started",
    "invoke_completed",
    "invoke_failed",
    "step_timing",
}


class ContinuationError(RuntimeError):
    """Raised when receipt or native state does not prove a safe resume point."""


def _canonical_json(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=False, allow_nan=False)


def _same_json_value(left, right):
    """Compare JSON values while accepting int/float spellings of one number."""
    if isinstance(left, bool) or isinstance(right, bool):
        return type(left) is type(right) and left == right
    if isinstance(left, (int, float)) and isinstance(right, (int, float)):
        return math.isfinite(float(left)) and math.isfinite(float(right)) and left == right
    if type(left) is not type(right):
        return False
    if isinstance(left, dict):
        return (left.keys() == right.keys() and
                all(_same_json_value(left[key], right[key]) for key in left))
    if isinstance(left, list):
        return len(left) == len(right) and all(
            _same_json_value(a, b) for a, b in zip(left, right))
    return left == right


def _sha256(data):
    return hashlib.sha256(data).hexdigest()


def _read_json(path, label):
    try:
        raw = Path(path).read_bytes()
        value = json.loads(raw)
    except (OSError, json.JSONDecodeError) as error:
        raise ContinuationError(f"Could not read {label}: {error}") from error
    if not isinstance(value, dict):
        raise ContinuationError(f"{label} must contain a JSON object.")
    return raw, value


def _required_dict(value, key, label):
    result = value.get(key)
    if not isinstance(result, dict):
        raise ContinuationError(f"{label} must contain object field {key!r}.")
    return result


def _structured_tool_proof(envelope, label):
    """Accept only the authoritative structuredContent, never text fallback."""
    result = envelope.get("result")
    if not isinstance(result, dict):
        raise ContinuationError(f"{label} is not a native tool-result envelope.")
    structured = result.get("structuredContent")
    if not isinstance(structured, dict):
        raise ContinuationError(f"{label} has no authoritative structuredContent object.")
    if structured.get("ok") is not True:
        raise ContinuationError(f"{label} does not contain an ok:true native proof.")
    return structured


def _validate_plan(plan, label):
    steps = plan.get("steps")
    if not isinstance(steps, list) or not steps:
        raise ContinuationError(f"{label} must contain a nonempty steps array.")
    positions = {}
    for index, step in enumerate(steps):
        if not isinstance(step, dict):
            raise ContinuationError(f"{label} step {index} is not an object.")
        step_id = step.get("id")
        command_id = step.get("commandId")
        if not isinstance(step_id, str) or not step_id or step_id in positions:
            raise ContinuationError(f"{label} has an empty or duplicate step id at index {index}.")
        if not isinstance(command_id, str) or not command_id:
            raise ContinuationError(f"{label} step {step_id} has no commandId.")
        if not isinstance(step.get("values"), dict):
            raise ContinuationError(f"{label} step {step_id} values must be an object.")
        positions[step_id] = index
    composition = plan.get("composition")
    if not isinstance(composition, dict):
        raise ContinuationError(f"{label} must declare composition metadata.")
    for field in ("width", "height", "fps"):
        value = composition.get(field)
        if (isinstance(value, bool) or not isinstance(value, (int, float)) or
                not math.isfinite(float(value)) or value <= 0):
            raise ContinuationError(f"{label} has invalid composition.{field}.")
        if field in ("width", "height") and int(value) != value:
            raise ContinuationError(f"{label} composition.{field} must be an integer.")
    return steps, positions


def _resolve_completed_refs(value, results, positions, current_index, label):
    if isinstance(value, dict):
        if "result" in value or "field" in value:
            if set(value) != {"result", "field"}:
                raise ContinuationError(
                    f"{label} contains a malformed stable-result reference.")
            result_id, field = value["result"], value["field"]
            if not isinstance(result_id, str) or not isinstance(field, str):
                raise ContinuationError(
                    f"{label} stable-result references must use string result and field values.")
            if result_id not in positions or positions[result_id] >= current_index:
                raise ContinuationError(
                    f"{label} references a missing, self, or future step {result_id!r}.")
            result = results.get(result_id)
            if not isinstance(result, dict) or field not in result:
                raise ContinuationError(
                    f"{label} prior result {result_id!r} has no field {field!r}.")
            return result[field]
        return {
            key: _resolve_completed_refs(item, results, positions, current_index, label)
            for key, item in value.items()
        }
    if isinstance(value, list):
        return [
            _resolve_completed_refs(item, results, positions, current_index, label)
            for item in value
        ]
    return value


def _resolve_prefix(plan_steps, receipt_steps, positions, label):
    results = {}
    for index, (planned, recorded) in enumerate(zip(plan_steps, receipt_steps)):
        step_id = planned["id"]
        if recorded.get("id") != step_id:
            raise ContinuationError(f"{label} completed IDs are not an unchanged plan prefix.")
        if recorded.get("commandId") != planned.get("commandId"):
            raise ContinuationError(f"{label} command changed for completed step {step_id}.")
        result = recorded.get("result")
        if not isinstance(result, dict) or result.get("ok") is not True:
            raise ContinuationError(f"{label} step {step_id} has no successful result receipt.")
        if result.get("commandId", planned["commandId"]) != planned["commandId"]:
            raise ContinuationError(f"{label} result command changed for completed step {step_id}.")
        expected_values = _resolve_completed_refs(
            planned["values"], results, positions, index, f"{label} step {step_id}")
        if _canonical_json(expected_values) != _canonical_json(recorded.get("values")):
            raise ContinuationError(
                f"{label} resolved values changed for completed step {step_id}.")
        results[step_id] = result
    return results


def _validate_event_log(path, expected_plan_hash, plan_steps, receipt_steps):
    events = []
    try:
        with Path(path).open("r", encoding="utf-8") as source:
            for line_number, line in enumerate(source, 1):
                if not line.strip():
                    continue
                try:
                    event = json.loads(line)
                except json.JSONDecodeError as error:
                    raise ContinuationError(
                        f"The NDJSON event log is damaged at line {line_number}.") from error
                if not isinstance(event, dict):
                    raise ContinuationError(f"NDJSON event {line_number} is not an object.")
                if (event.get("schema") != RECEIPT_SCHEMA or
                        event.get("planSha256") != expected_plan_hash):
                    raise ContinuationError(
                        f"NDJSON event {line_number} belongs to another plan or schema.")
                if event.get("event") not in ALLOWED_EVENTS:
                    raise ContinuationError(
                        f"NDJSON event {line_number} has an unknown event type.")
                events.append(event)
    except OSError as error:
        raise ContinuationError(f"Could not read the NDJSON event log: {error}") from error
    if not events:
        raise ContinuationError("The NDJSON event log is empty.")

    opened = [event for event in events if event.get("event") == "plan_opened"]
    if len(opened) != 1 or opened[0].get("stepCount") != len(plan_steps):
        raise ContinuationError("The NDJSON log does not contain one matching plan_opened record.")

    starts = {}
    terminals = {}
    start_order = []
    complete_order = []
    failed_order = []
    for event in events:
        kind = event.get("event")
        if kind not in {"invoke_started", "invoke_completed", "invoke_failed"}:
            if kind == "read_call_failed":
                raise ContinuationError("The NDJSON log contains a failed read call.")
            continue
        step_id = event.get("stepId")
        if not isinstance(step_id, str) or not step_id:
            raise ContinuationError("An authoring event has no stable stepId.")
        if kind == "invoke_started":
            if step_id in starts:
                raise ContinuationError(f"Step {step_id} has duplicate mutation intents.")
            starts[step_id] = event
            start_order.append(step_id)
        else:
            if step_id in terminals:
                raise ContinuationError(f"Step {step_id} has multiple terminal events.")
            terminals[step_id] = event
            if kind == "invoke_completed":
                complete_order.append(step_id)
            else:
                failed_order.append(step_id)

    plan_ids = [step["id"] for step in plan_steps]
    if any(step_id not in plan_ids for step_id in set(starts) | set(terminals)):
        raise ContinuationError("The NDJSON log contains an unknown authoring intent or result.")
    if set(starts) != set(terminals):
        unresolved = sorted(set(starts) - set(terminals))
        unexpected = sorted(set(terminals) - set(starts))
        raise ContinuationError(
            f"Mutation intents lack known terminal results (unresolved={unresolved[:4]}, "
            f"terminalWithoutIntent={unexpected[:4]}).")
    if start_order != plan_ids[:len(start_order)]:
        raise ContinuationError("Authoring intents are not a strict ordered prefix of the plan.")
    if complete_order != plan_ids[:len(complete_order)]:
        raise ContinuationError("Completed events are not a strict ordered plan prefix.")
    if failed_order != plan_ids[len(complete_order):len(complete_order) + len(failed_order)]:
        raise ContinuationError("Failed events do not immediately follow the completed prefix.")
    if len(complete_order) + len(failed_order) != len(start_order):
        raise ContinuationError("Mutation event order has a gap after the completed prefix.")
    if len(failed_order) == 0:
        raise ContinuationError("The execution log contains no failed mutation to recover from.")

    for index, (step_id, recorded) in enumerate(zip(complete_order, receipt_steps)):
        event = terminals[step_id]
        planned = plan_steps[index]
        if (event.get("commandId") != planned["commandId"] or
                _canonical_json(event.get("values")) != _canonical_json(recorded.get("values")) or
                _canonical_json(event.get("result")) != _canonical_json(recorded.get("result"))):
            raise ContinuationError(
                f"NDJSON completion and JSON receipt disagree for step {step_id}.")
    return events, starts, terminals, complete_order, failed_order


def _native_error_payload(error_text):
    if not isinstance(error_text, str):
        raise ContinuationError("A failed authoring intent has no native error envelope.")
    try:
        envelope = json.loads(error_text)
    except json.JSONDecodeError as error:
        raise ContinuationError("A failed authoring intent has no parseable native error envelope.") from error
    if not isinstance(envelope, dict):
        raise ContinuationError("A failed authoring error envelope is malformed.")
    result = envelope.get("result")
    if not isinstance(result, dict) or result.get("isError") is not True:
        raise ContinuationError("The failed authoring envelope is not an MCP error result.")
    structured = result.get("structuredContent")
    if not isinstance(structured, dict):
        raise ContinuationError("The failed authoring error has no structured native batch result.")
    return structured


def _validate_native_proof(task, document, resource_id, document_hash,
                            task_id, composition, output_directory,
                            required_asset_paths, failed_ids, failed_starts,
                            failed_terminals):
    if task.get("taskId") != task_id or task.get("status") != "running" or task.get("active") is not True:
        raise ContinuationError("Native task proof does not identify the active running task.")
    if task.get("targetResourceIds") != [resource_id]:
        raise ContinuationError("Native task proof does not target exactly the active Timeline resource.")
    scope = task.get("nativeTimelineScope")
    if not isinstance(scope, dict) or scope.get("targetIds") != [resource_id]:
        raise ContinuationError("Native task scope does not seal the exact Timeline resource.")
    if scope.get("outputDirectory") != output_directory:
        raise ContinuationError("Corrected plan outputDirectory differs from the sealed task scope.")
    scoped_assets = scope.get("assetPaths")
    if not isinstance(scoped_assets, list) or not all(isinstance(path, str) for path in scoped_assets):
        raise ContinuationError("Native task proof has no exact admitted asset path list.")
    if not required_asset_paths.issubset(set(scoped_assets)):
        missing = sorted(required_asset_paths - set(scoped_assets))
        raise ContinuationError(f"Corrected plan requires unadmitted assets: {missing[:3]}.")

    if (document.get("resourceId") != resource_id or document.get("documentHash") != document_hash or
            document.get("ready") is not True):
        raise ContinuationError("Native document proof does not match the Timeline resource and hash.")
    for field in ("width", "height"):
        if document.get(field) != composition.get(field):
            raise ContinuationError(f"Native document {field} does not match the corrected plan composition.")
    fps = document.get("frameRate")
    if (isinstance(fps, bool) or not isinstance(fps, (int, float)) or
            not math.isclose(float(fps), float(composition["fps"]), rel_tol=0.0, abs_tol=1e-9)):
        raise ContinuationError("Native document frameRate does not match the corrected plan composition.")

    matches = [resource for resource in task.get("resources", [])
               if isinstance(resource, dict) and resource.get("id") == resource_id]
    if (len(matches) != 1 or matches[0].get("currentHash") != document_hash or
            matches[0].get("pending") is not False or matches[0].get("reverted") is not False):
        raise ContinuationError("Native journal currentHash does not match the inspected document checkpoint.")

    task_steps = task.get("steps")
    if not isinstance(task_steps, list) or not task_steps:
        raise ContinuationError("Native task proof has no journaled operation history.")
    if any(not isinstance(step, dict) or step.get("id") != index
           for index, step in enumerate(task_steps)):
        raise ContinuationError("Native task operation history has missing or reordered step IDs.")
    failed_native_steps = [step for step in task_steps if step.get("status") == "failed"]
    if len(failed_native_steps) != 1 or failed_native_steps[0] is not task_steps[-1]:
        raise ContinuationError("Native task proof must have exactly one terminal failed batch and no later operations.")
    if any(step.get("status") != "completed" for step in task_steps[:-1]):
        raise ContinuationError("Native task journal contains another incomplete or unresolved operation.")

    errors = [_native_error_payload(failed_terminals[step_id].get("error"))
              for step_id in failed_ids]
    first_error = errors[0]
    batch_id = first_error.get("batchId")
    request_sha = first_error.get("batchRequestSha256")
    if not isinstance(batch_id, str) or not batch_id or not isinstance(request_sha, str) or not request_sha:
        raise ContinuationError("Native failed batch proof has no stable batch ID and request hash.")
    if any(_canonical_json(error) != _canonical_json(first_error) for error in errors[1:]):
        raise ContinuationError("Failed intent receipts disagree about the native batch failure.")
    if (first_error.get("commandId") != BATCH_COMMAND_ID or
            first_error.get("taskStepId") != task_steps[-1]["id"] or
            first_error.get("resourceId") != resource_id or
            first_error.get("batchId") != batch_id or
            first_error.get("batchRequestSha256") != request_sha or
            first_error.get("ok") is not False or
            first_error.get("rolledBack") is not True or
            first_error.get("changed") is not False or
            first_error.get("mutationNotApplied") is not True or
            first_error.get("taskStateHash") != document_hash or
            first_error.get("recoveryRequired") is True):
        raise ContinuationError("Native error result does not prove a complete rollback with no mutation.")

    native_step = task_steps[-1]
    native_args = native_step.get("arguments")
    native_outcome = native_step.get("outcome")
    if (not isinstance(native_args, dict) or
            native_step.get("command") != BATCH_COMMAND_ID or
            native_step.get("resourceId") != resource_id or
            native_args.get("batchId") != batch_id or
            native_args.get("_batchId") != batch_id or
            native_args.get("_batchRequestSha256") != request_sha or
            not isinstance(native_outcome, dict) or
            native_outcome.get("batchId") != batch_id or
            native_outcome.get("batchRequestSha256") != request_sha or
            native_outcome.get("ok") is not False or
            native_outcome.get("rolledBack") is not True or
            native_outcome.get("changed") is not False or
            native_outcome.get("mutationNotApplied") is not True or
            native_outcome.get("recoveryRequired") is True):
        raise ContinuationError("Native journal entry does not prove the exact failed batch was rolled back.")

    native_operations = native_args.get("operations")
    if not isinstance(native_operations, list) or len(native_operations) != len(failed_ids):
        raise ContinuationError("Native journal batch operations do not match all failed intents.")
    for proof in (first_error, native_outcome):
        if "operationCount" in proof and proof["operationCount"] != len(failed_ids):
            raise ContinuationError("Native batch operation count differs from its exact failed intents.")
    for index, step_id in enumerate(failed_ids):
        start = failed_starts[step_id]
        terminal = failed_terminals[step_id]
        operation = native_operations[index]
        expected_operation = {
            "id": start.get("batchOperationId"),
            "commandId": start.get("commandId"),
            "values": start.get("batchOperationValues"),
        }
        if (not _same_json_value(operation, expected_operation) or
                start.get("batchId") != batch_id or
                start.get("batchOperationCount") != len(failed_ids) or
                start.get("batchOperationId") != f"op{index:03d}" or
                terminal.get("commandId") != operation.get("commandId") or
                terminal.get("batchId") != batch_id):
            raise ContinuationError(
                f"Native task operation {index} does not match its exact NDJSON intent.")
    if _canonical_json(first_error.get("batchResults")) != _canonical_json(native_outcome.get("batchResults")):
        raise ContinuationError("Native tool error and task journal disagree on batch rollback results.")
    rows = native_outcome.get("batchResults")
    if not isinstance(rows, list) or not 1 <= len(rows) <= len(native_operations):
        raise ContinuationError("Native rollback proof has no bounded per-operation results.")
    failed_rows = []
    for index, row in enumerate(rows):
        expected = native_operations[index]
        if (not isinstance(row, dict) or row.get("id") != expected["id"] or
                row.get("commandId") != expected["commandId"]):
            raise ContinuationError("Native batch results are not an ordered prefix of the request.")
        if row.get("ok") is False:
            failed_rows.append(index)
        elif row.get("ok") is not True or failed_rows:
            raise ContinuationError("Native batch results contain an invalid or post-failure operation.")
    if len(failed_rows) != 1 or failed_rows[0] != len(rows) - 1:
        raise ContinuationError("Native rollback proof must identify one failing operation after a successful prefix.")
    return batch_id, request_sha, native_step["id"], scoped_assets


def _verify_failed_batch_matches_parent(parent_hash, failed_steps,
                                        parent_results, starts, failed_batch_id,
                                        failed_request_sha):
    """Prove failed NDJSON intents were exactly the next batch in the parent plan."""
    try:
        expected = author_native._build_batch_request(
            parent_hash, failed_steps, parent_results)
    except Exception as error:
        raise ContinuationError(f"Could not reconstruct the failed parent batch: {error}") from error
    if (not expected.get("fitsLimits") or
            expected.get("batchId") != failed_batch_id or
            not isinstance(failed_request_sha, str) or not failed_request_sha or
            len(expected.get("operations", [])) != len(failed_steps)):
        raise ContinuationError("Failed native batch identity does not match the parent plan actions.")
    for index, step in enumerate(failed_steps):
        started = starts.get(step["id"])
        if not isinstance(started, dict):
            raise ContinuationError(f"Failed parent action {step['id']} has no intent record.")
        expected_operation = expected["operations"][index]
        if (started.get("commandId") != step["commandId"] or
                started.get("batchId") != failed_batch_id or
                started.get("batchOperationId") != expected_operation["id"] or
                started.get("batchOperationCount") != len(failed_steps) or
                not _same_json_value(started.get("batchOperationValues"),
                                     expected_operation["values"])):
            raise ContinuationError(
                f"NDJSON intent for failed action {step['id']} differs from the parent plan batch.")


def _rewrite_remaining_refs(value, completed_results, all_positions,
                            current_index, label):
    if isinstance(value, dict):
        if "result" in value or "field" in value:
            if set(value) != {"result", "field"}:
                raise ContinuationError(f"{label} contains a malformed stable-result reference.")
            result_id, field = value["result"], value["field"]
            if not isinstance(result_id, str) or not isinstance(field, str):
                raise ContinuationError(f"{label} stable-result references must be strings.")
            if result_id not in all_positions:
                raise ContinuationError(f"{label} references missing step {result_id!r}.")
            result_index = all_positions[result_id]
            if result_index >= current_index:
                raise ContinuationError(f"{label} references self or future step {result_id!r}.")
            if result_id in completed_results:
                result = completed_results[result_id]
                if field not in result:
                    raise ContinuationError(
                        f"{label} completed result {result_id!r} has no field {field!r}.")
                return result[field]
            return {"result": result_id, "field": field}
        return {
            key: _rewrite_remaining_refs(item, completed_results, all_positions,
                                         current_index, label)
            for key, item in value.items()
        }
    if isinstance(value, list):
        return [
            _rewrite_remaining_refs(item, completed_results, all_positions,
                                    current_index, label)
            for item in value
        ]
    return value


def _validate_admitted_file_paths(plan_steps, asset_paths):
    for step in plan_steps:
        values = step.get("values", {})
        for path in _find_file_paths(values):
            if path not in asset_paths:
                raise ContinuationError(
                    f"Remaining step {step['id']} uses a filePath outside the sealed asset list.")


def _find_file_paths(value):
    found = []
    if isinstance(value, dict):
        for key, item in value.items():
            if key == "filePath" and isinstance(item, str):
                found.append(item)
            else:
                found.extend(_find_file_paths(item))
    elif isinstance(value, list):
        for item in value:
            found.extend(_find_file_paths(item))
    return found


def _output_path_is_scoped(plan):
    output_directory = plan.get("outputDirectory")
    output_path = plan.get("outputPath")
    if output_path is None:
        return
    if not isinstance(output_directory, str) or not isinstance(output_path, str):
        raise ContinuationError("Plan outputPath and outputDirectory must be strings.")
    root = os.path.abspath(output_directory)
    target = os.path.abspath(output_path)
    try:
        if os.path.commonpath((root, target)) != root or target == root:
            raise ContinuationError("Plan outputPath is outside its sealed outputDirectory.")
    except ValueError as error:
        raise ContinuationError("Plan outputPath is on a different path root from outputDirectory.") from error


def build_continuation_plan(parent_plan_path, corrected_plan_path,
                            receipt_path, event_log_path,
                            native_task_proof_path, native_document_proof_path):
    parent_bytes, parent = _read_json(parent_plan_path, "parent plan")
    corrected_bytes, corrected = _read_json(corrected_plan_path, "corrected plan")
    receipt_bytes, receipt = _read_json(receipt_path, "prior JSON receipt")
    task_bytes, task_envelope = _read_json(native_task_proof_path, "native task proof")
    document_bytes, document_envelope = _read_json(native_document_proof_path, "native document proof")
    parent_hash = _sha256(parent_bytes)
    corrected_hash = _sha256(corrected_bytes)
    receipt_hash = _sha256(receipt_bytes)
    task_proof_hash = _sha256(task_bytes)
    document_proof_hash = _sha256(document_bytes)
    event_log_hash = _sha256(Path(event_log_path).read_bytes())

    parent_steps, parent_positions = _validate_plan(parent, "Parent plan")
    corrected_steps, corrected_positions = _validate_plan(corrected, "Corrected plan")
    if receipt.get("planSha256") != parent_hash or receipt.get("status") != "incomplete":
        raise ContinuationError("Prior receipt does not identify the incomplete parent plan.")
    if not parent_hash or not corrected_hash or parent_hash == corrected_hash:
        raise ContinuationError("Corrected plan must be a distinct immutable plan revision.")
    if parent.get("composition") != corrected.get("composition"):
        raise ContinuationError("Corrected plan changed the parent composition geometry.")

    receipt_steps = receipt.get("steps")
    if not isinstance(receipt_steps, list) or not receipt_steps:
        raise ContinuationError("Prior receipt has no successful completed prefix.")
    completed_count = len(receipt_steps)
    if completed_count >= len(parent_steps) or completed_count >= len(corrected_steps):
        raise ContinuationError("The completed receipt is not a strict prefix of both plans.")
    if len(parent_steps) != len(corrected_steps):
        raise ContinuationError("Parent and corrected plans have different step counts.")

    parent_results = _resolve_prefix(parent_steps, receipt_steps, parent_positions, "Parent plan")
    corrected_results = _resolve_prefix(
        corrected_steps, receipt_steps, corrected_positions, "Corrected plan")
    if list(parent_results) != list(corrected_results):
        raise ContinuationError("Parent and corrected completed-prefix result IDs differ.")

    events, starts, terminals, complete_order, failed_order = _validate_event_log(
        event_log_path, parent_hash, parent_steps, receipt_steps)
    if complete_order != [step["id"] for step in parent_steps[:completed_count]]:
        raise ContinuationError("NDJSON completions do not match the JSON receipt prefix.")
    if not 1 <= len(failed_order) <= author_native.BATCH_MAX_OPERATIONS:
        raise ContinuationError("The native checkpoint must have one bounded batch of failed intents to retry.")
    failed_parent_steps = parent_steps[completed_count:completed_count + len(failed_order)]
    failed_corrected_steps = corrected_steps[completed_count:completed_count + len(failed_order)]
    expected_failed_ids = [step["id"] for step in failed_parent_steps]
    if failed_order != expected_failed_ids:
        raise ContinuationError("The failed action intents are not the exact next parent-plan steps.")
    for parent_step, corrected_step in zip(failed_parent_steps, failed_corrected_steps):
        if (parent_step["id"] != corrected_step["id"] or
                parent_step["commandId"] != corrected_step["commandId"]):
            raise ContinuationError("Corrected plan changed the identity or command of an uncompleted failed action.")

    failure = receipt.get("failure")
    if (not isinstance(failure, dict) or failure.get("stepId") != expected_failed_ids[0] or
            not isinstance(failure.get("batchId"), str)):
        raise ContinuationError("Prior summary does not identify the first failed batch action.")
    batch_id, request_sha, native_step_id, scoped_assets = _validate_native_proof(
        _structured_tool_proof(task_envelope, "Native task proof"),
        _structured_tool_proof(document_envelope, "Native document proof"),
        corrected.get("resourceId") or parent.get("resourceId") or
            _structured_tool_proof(document_envelope, "Native document proof").get("resourceId"),
        _structured_tool_proof(document_envelope, "Native document proof").get("documentHash"),
        corrected.get("taskId") or parent.get("taskId") or
            _structured_tool_proof(task_envelope, "Native task proof").get("taskId"),
        corrected["composition"], corrected.get("outputDirectory"),
        set(corrected.get("assetPaths", [])), expected_failed_ids,
        starts, {step_id: terminals[step_id] for step_id in expected_failed_ids})
    if failure.get("batchId") != batch_id:
        raise ContinuationError("Receipt failure batchId differs from the native rollback proof.")
    # The old runner summary may conservatively say recoveryRequired=true.
    # Only the matching native journal and structured batch result establish rollback.
    _verify_failed_batch_matches_parent(
        parent_hash, failed_parent_steps, parent_results, starts, batch_id, request_sha)

    native_task = _structured_tool_proof(task_envelope, "Native task proof")
    native_document = _structured_tool_proof(document_envelope, "Native document proof")
    resource_id = native_document["resourceId"]
    task_id = native_task["taskId"]
    document_hash = native_document["documentHash"]
    parent_starting_hash = parent.get("startingDocumentHash")
    if parent_starting_hash is not None:
        resource = next(row for row in native_task["resources"] if row["id"] == resource_id)
        provenance = parent.get("continuationProvenance", {})
        proven_start = provenance.get("startingDocumentHash", resource.get("beforeHash"))
        if parent_starting_hash != proven_start:
            raise ContinuationError("Parent starting hash differs from its frozen starting checkpoint.")
    for plan, label in ((parent, "Parent plan"), (corrected, "Corrected plan")):
        if plan.get("resourceId") not in (None, resource_id):
            raise ContinuationError(f"{label} resourceId differs from the native Timeline proof.")
        if plan.get("taskId") not in (None, task_id):
            raise ContinuationError(f"{label} taskId differs from the native task proof.")
        if plan.get("startingDocumentHash") != parent_starting_hash:
            raise ContinuationError(f"{label} changed the frozen parent starting checkpoint.")
        if plan.get("outputDirectory") != native_task["nativeTimelineScope"].get("outputDirectory"):
            raise ContinuationError(f"{label} outputDirectory differs from the sealed native task scope.")
    _output_path_is_scoped(corrected)
    _validate_admitted_file_paths(corrected_steps[completed_count:], set(scoped_assets))

    remaining_steps = []
    for index in range(completed_count, len(corrected_steps)):
        step = dict(corrected_steps[index])
        step["values"] = _rewrite_remaining_refs(
            step["values"], corrected_results, corrected_positions, index,
            f"Corrected step {step['id']}")
        remaining_steps.append(step)
    if [step["id"] for step in remaining_steps[:len(expected_failed_ids)]] != expected_failed_ids:
        raise ContinuationError("Continuation does not retry every failed action before later work.")

    result = dict(corrected)
    result["steps"] = remaining_steps
    result["resourceId"] = resource_id
    result["taskId"] = task_id
    result["startingDocumentHash"] = document_hash
    result["continuationProvenance"] = {
        "schema": CONTINUATION_SCHEMA,
        "parentPlanSha256": parent_hash,
        "correctedPlanSha256": corrected_hash,
        "priorReceiptSha256": receipt_hash,
        "priorEventLogSha256": event_log_hash,
        "nativeTaskProofSha256": task_proof_hash,
        "nativeDocumentProofSha256": document_proof_hash,
        "completedPrefixCount": completed_count,
        "lastCompletedStepId": receipt_steps[-1]["id"],
        "failedStepIds": expected_failed_ids,
        "failedBatchId": batch_id,
        "failedBatchRequestSha256": request_sha,
        "nativeTaskStepId": native_step_id,
        "nativeRollbackVerified": True,
        "startingDocumentHash": document_hash,
        "taskId": task_id,
        "resourceId": resource_id,
    }

    output_bytes = serialize_plan(result)
    output_hash = _sha256(output_bytes)
    retry = author_native._build_batch_request(
        output_hash, remaining_steps[:len(expected_failed_ids)], corrected_results)
    if (not retry.get("fitsLimits") or
            retry.get("batchId") == batch_id or
            len(retry.get("operations", [])) != len(expected_failed_ids)):
        raise ContinuationError(
            "The full failed action group does not fit one new bounded batch or reuses its failed batch ID.")
    return result


def serialize_plan(plan):
    return (json.dumps(plan, indent=2, ensure_ascii=False, allow_nan=False) + "\n").encode("utf-8")


def write_new_plan(plan, output_path, input_paths=()):
    output = Path(output_path)
    output_resolved = output.resolve(strict=False)
    for input_path in input_paths:
        if output_resolved == Path(input_path).resolve(strict=False):
            raise ContinuationError("Continuation output must be a new path, not an input plan or receipt.")
    payload = serialize_plan(plan)
    if output.exists():
        if output.read_bytes() == payload:
            return output
        raise ContinuationError("Continuation output already exists with different contents; refusing to overwrite it.")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + ".tmp")
    try:
        with temporary.open("xb") as target:
            target.write(payload)
            target.flush()
            os.fsync(target.fileno())
        os.replace(temporary, output)
        directory_fd = os.open(output.parent, os.O_RDONLY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    except Exception:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
        raise
    return output


def default_paths():
    evidence = HERE / "evidence"
    return {
        "parent_plan_path": HERE / "live-landscape-v4.commands.json",
        "corrected_plan_path": HERE / "full-landscape-v5.commands.json",
        "receipt_path": evidence / "native-full-landscape-v4-receipt.json",
        "event_log_path": evidence / "native-full-landscape-v4-receipt.json.ndjson",
        "native_task_proof_path": evidence / "native-full-blocked-task.json",
        "native_document_proof_path": evidence / "native-full-blocked-document.json",
        "output_path": HERE / "full-landscape-v5-continuation.commands.json",
    }


def main(argv=None):
    defaults = default_paths()
    parser = argparse.ArgumentParser(description=__doc__)
    for name, default in defaults.items():
        parser.add_argument("--" + name.replace("_", "-"), type=Path,
                            default=default, help=f"Input/output path (default: {default})")
    args = parser.parse_args(argv)
    try:
        plan = build_continuation_plan(
            args.parent_plan_path, args.corrected_plan_path, args.receipt_path,
            args.event_log_path, args.native_task_proof_path,
            args.native_document_proof_path)
        output = write_new_plan(plan, args.output_path, input_paths=(
            args.parent_plan_path, args.corrected_plan_path, args.receipt_path,
            args.event_log_path, args.native_task_proof_path,
            args.native_document_proof_path))
    except ContinuationError as error:
        print(f"continuation refused: {error}", file=sys.stderr)
        return 2
    provenance = plan["continuationProvenance"]
    print(json.dumps({
        "ok": True,
        "output": str(output),
        "remainingSteps": len(plan["steps"]),
        "completedPrefixCount": provenance["completedPrefixCount"],
        "firstRetryStepIds": provenance["failedStepIds"],
        "failedBatchId": provenance["failedBatchId"],
        "startingDocumentHash": plan["startingDocumentHash"],
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
