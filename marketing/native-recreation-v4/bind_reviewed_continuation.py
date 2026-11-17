"""Bind an unexecuted suffix to a newly imported, reviewed Timeline checkpoint.

This is an offline evidence binder. It does not call MCP, alter the parent plan,
or treat a rejected pre-dispatch batch as a completed action. The old native
task journal must prove every completed plan action, the exact rejected guard,
the later save, review and release. A fresh import and task admission must then
prove that the same saved graph and stable IDs are available under a new
checkpoint before a new suffix plan is emitted.
"""

from pathlib import Path
import argparse
import copy
import hashlib
import json
import math
import os
import re
import sys
import uuid

import author_native
import build_native_continuation as continuation
import retarget_imported_continuation as retarget


HERE = Path(__file__).resolve().parent
BATCH_COMMAND_ID = "timeline.action.applyDraftBatch"
SAVE_COMMAND_ID = "timeline.action.saveTimeline"
RECEIPT_SCHEMA = continuation.RECEIPT_SCHEMA
BINDER_SCHEMA = "looksy.native-reviewed-prefix-import-continuation/v1"
SCHEMA_UPGRADE_SCHEMA = "looksy.native-schema-upgrade-import-continuation/v1"
SCOPE_REVIEW_SCHEMA = "looksy.native-scope-rejection-import-continuation/v1"
PORTRAIT_BOOTSTRAP_SCHEMA = "looksy.native-portrait-bootstrap-binding/v1"
SCHEMA_TYPED_REJECTION = "A batch operation does not match its registered typed action schema."
EXPECTED_GUARD_ERROR = "An idempotent batch retry requires the same running owned task."
SCOPE_REJECTION_ERROR = "The path operation requires the running task's exact Timeline target."
SCOPE_REJECTION_COMMAND = "timeline.action.createTimelineClip"
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


class BinderError(RuntimeError):
    """Raised when the supplied native receipts do not prove a safe suffix."""


def _canonical_json(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=False, allow_nan=False)


def _same(left, right):
    return continuation._same_json_value(left, right)


def _sha256(data):
    return hashlib.sha256(data).hexdigest()


def _read_json(path, label):
    try:
        raw = Path(path).read_bytes()
        value = json.loads(raw)
    except (OSError, json.JSONDecodeError) as error:
        raise BinderError(f"Could not read {label}: {error}") from error
    if not isinstance(value, dict):
        raise BinderError(f"{label} must contain a JSON object.")
    return raw, value


def _proof(path, label):
    _, envelope = _read_json(path, label)
    return _structured(envelope, label)


def _structured(envelope, label):
    result = envelope.get("result")
    if not isinstance(result, dict) or result.get("isError") is True:
        raise BinderError(f"{label} is not a successful native tool response.")
    structured = result.get("structuredContent")
    if not isinstance(structured, dict) or structured.get("ok") is not True:
        raise BinderError(f"{label} has no authoritative ok:true structuredContent.")
    return structured


def _valid_sha(value, label):
    if not isinstance(value, str) or not SHA256_RE.fullmatch(value):
        raise BinderError(f"{label} is not a canonical lowercase SHA-256.")
    return value


def _event_log(path, plan_hash, step_count):
    events = []
    try:
        with Path(path).open("r", encoding="utf-8") as source:
            for line_number, line in enumerate(source, 1):
                if not line.strip():
                    continue
                try:
                    event = json.loads(line)
                except json.JSONDecodeError as error:
                    raise BinderError(
                        f"The authoring NDJSON is damaged at line {line_number}.") from error
                if not isinstance(event, dict):
                    raise BinderError(f"NDJSON event {line_number} is not an object.")
                if (event.get("schema") != RECEIPT_SCHEMA or
                        event.get("planSha256") != plan_hash):
                    raise BinderError(
                        f"NDJSON event {line_number} belongs to another plan or schema.")
                if event.get("event") not in continuation.ALLOWED_EVENTS:
                    raise BinderError(f"NDJSON event {line_number} has an unknown event type.")
                if event.get("event") == "read_call_failed":
                    raise BinderError("The runner log contains a failed native read call.")
                events.append(event)
    except OSError as error:
        raise BinderError(f"Could not read the authoring NDJSON: {error}") from error
    opened = [event for event in events if event.get("event") == "plan_opened"]
    if len(opened) != 1 or opened[0].get("stepCount") != step_count:
        raise BinderError("The NDJSON log has no single matching plan_opened event.")
    return events


def _native_guard_error(error_text):
    if not isinstance(error_text, str):
        raise BinderError("A rejected batch intent has no native error envelope.")
    try:
        envelope = json.loads(error_text)
    except json.JSONDecodeError as error:
        raise BinderError("A rejected batch intent has an invalid native error envelope.") from error
    result = envelope.get("result") if isinstance(envelope, dict) else None
    structured = result.get("structuredContent") if isinstance(result, dict) else None
    if (not isinstance(result, dict) or result.get("isError") is not True or
            not isinstance(structured, dict)):
        raise BinderError("A rejected batch is not an MCP error with structured native content.")
    if (structured.get("commandId") != BATCH_COMMAND_ID or
            structured.get("ok") is not False or
            structured.get("error") != EXPECTED_GUARD_ERROR):
        raise BinderError("The failed batch was not rejected by the exact running-task guard.")
    # Historical response did not claim rollback or no-mutation flags. Do not
    # synthesize them; absence is part of this exact pre-dispatch proof.
    if set(structured) != {"commandId", "ok", "error"}:
        raise BinderError("The historical pre-dispatch guard response has unexpected claims.")
    return structured


def _native_error_structured(error_text, label):
    if not isinstance(error_text, str):
        raise BinderError(f"{label} has no native error envelope.")
    try:
        envelope = json.loads(error_text)
    except json.JSONDecodeError as error:
        raise BinderError(f"{label} has an invalid native error envelope.") from error
    result = envelope.get("result") if isinstance(envelope, dict) else None
    structured = result.get("structuredContent") if isinstance(result, dict) else None
    if (not isinstance(result, dict) or result.get("isError") is not True or
            not isinstance(structured, dict)):
        raise BinderError(f"{label} is not an MCP error with structured native content.")
    return structured


def _validate_specific_batch_failure(error_text, request, policy):
    kind = policy.get("kind")
    structured = _native_error_structured(error_text, "Rejected batch")
    common = {
        "commandId": BATCH_COMMAND_ID,
        "ok": False,
        "error": policy.get("error"),
        "changed": False,
        "mutationNotApplied": True,
        "recoverableError": True,
        "resourceId": policy.get("resourceId"),
        "taskStateHash": policy.get("taskStateHash"),
        "taskStepId": policy.get("taskStepId"),
    }
    if kind == "atomic_rollback":
        expected_keys = set(common) | {
            "batchId", "batchRequestSha256", "batchResults", "operationCount", "rolledBack",
        }
        expected_results = [{
            "commandId": request["operations"][0]["commandId"],
            "error": policy["error"],
            "id": request["operations"][0]["id"],
            "ok": False,
        }]
        common.update({
            "batchId": request["batchId"],
            "batchRequestSha256": policy.get("batchRequestSha256"),
            "batchResults": expected_results,
            "operationCount": len(request["operations"]),
            "rolledBack": True,
        })
    elif kind == "pre_dispatch_schema":
        expected_keys = set(common)
    else:
        raise BinderError("Unknown native batch failure policy.")
    if set(structured) != expected_keys or not _same(structured, common):
        raise BinderError("Native rejected-batch receipt does not match its exact no-mutation policy.")
    expected_batch_id = policy.get("batchId")
    if expected_batch_id is not None and request.get("batchId") != expected_batch_id:
        raise BinderError("Rejected batch ID differs from its sealed failure proof.")
    expected_request_sha = policy.get("batchRequestSha256")
    if expected_request_sha is not None:
        _valid_sha(expected_request_sha, "Rejected batch request SHA-256")
        if kind == "atomic_rollback" and structured.get("batchRequestSha256") != expected_request_sha:
            raise BinderError("Rejected batch request hash differs from its sealed failure proof.")
    return structured


def _validate_scope_rejection(error_text, policy):
    if not isinstance(error_text, str):
        raise BinderError("Native scope rejection has no MCP error envelope.")
    try:
        envelope = json.loads(error_text)
    except json.JSONDecodeError as error:
        raise BinderError("Native scope rejection has an invalid MCP error envelope.") from error
    result = envelope.get("result") if isinstance(envelope, dict) else None
    if (not isinstance(result, dict) or result.get("isError") is not True or
            set(result) != {"content", "isError", "structuredContent"}):
        raise BinderError("Native scope rejection is not an exact MCP error response.")
    expected_error = policy.get("error")
    expected_content = [{"text": expected_error, "type": "text"}]
    if result.get("content") != expected_content:
        raise BinderError("Native scope rejection text differs from its structured reason.")
    structured = result.get("structuredContent")
    if (not isinstance(structured, dict) or
            set(structured) != {"blockedByScope", "error", "ok"} or
            structured.get("blockedByScope") is not True or
            structured.get("ok") is not False or
            structured.get("error") != expected_error):
        raise BinderError("Native error is not the exact pre-dispatch Timeline scope rejection.")
    return structured


def _failure_journal_descriptor(prefix, policy, journal_index):
    structured = prefix.get("failedNativeStructured")
    request = prefix.get("failedBatchRequest")
    if not isinstance(structured, dict) or not isinstance(request, dict):
        raise BinderError("Rejected batch has no validated native failure descriptor.")
    if request.get("batchId") != prefix.get("failedBatchId"):
        raise BinderError("Rejected batch descriptor has a conflicting batch ID.")
    request_sha = policy.get("batchRequestSha256")
    if not isinstance(request_sha, str):
        request_sha = structured.get("batchRequestSha256")
    _valid_sha(request_sha, "Rejected batch request SHA-256")
    # The journal outcome intentionally excludes the identity fields attached
    # by the outer MCP mutation owner. Keep the native failure flags exact.
    journal_outcome = {
        key: value for key, value in structured.items()
        if key not in {"commandId", "resourceId", "taskStateHash", "taskStepId"}
    }
    return {
        "journalIndex": journal_index,
        "batchId": request["batchId"],
        "batchRequestSha256": request_sha,
        "arguments": {
            "batchId": request["batchId"],
            "_batchId": request["batchId"],
            "_batchRequestSha256": request_sha,
            "operations": request["operations"],
        },
        "outcome": journal_outcome,
        "status": "failed",
    }


def _resolved_plan_values(step, prior_results, positions, index):
    try:
        return continuation._resolve_completed_refs(
            step["values"], prior_results, positions, index,
            f"Plan step {step['id']}")
    except continuation.ContinuationError as error:
        raise BinderError(str(error)) from error


def _greedy_batch_ids(steps, start_index, result_by_id, plan_hash):
    selected = []
    selected_request = None
    for candidate in steps[start_index:start_index + author_native.BATCH_MAX_OPERATIONS]:
        if candidate.get("commandId") not in author_native.BATCH_SAFE_COMMAND_IDS:
            break
        trial_steps = selected + [candidate]
        request = author_native._build_batch_request(plan_hash, trial_steps, result_by_id)
        if not request.get("fitsLimits"):
            break
        selected = trial_steps
        selected_request = request
    return selected, selected_request


def _validate_completion_prefix(plan, receipt, events, plan_hash, failure_policy=None):
    steps, positions = continuation._validate_plan(plan, "Parent plan")
    receipt_steps = receipt.get("steps")
    if not isinstance(receipt_steps, list) or not receipt_steps:
        raise BinderError("The JSON receipt has no completed authoring prefix.")
    completed_count = len(receipt_steps)
    if completed_count >= len(steps):
        raise BinderError("The completed receipt is not a strict prefix of the parent plan.")
    try:
        results = continuation._resolve_prefix(
            steps, receipt_steps, positions, "Parent plan")
    except continuation.ContinuationError as error:
        raise BinderError(str(error)) from error
    if receipt.get("planSha256") != plan_hash or receipt.get("status") != "incomplete":
        raise BinderError("The JSON receipt does not identify the incomplete parent plan.")

    starts = {}
    terminals = {}
    start_order = []
    complete_order = []
    failed_order = []
    event_positions = {}
    for event_index, event in enumerate(events):
        kind = event["event"]
        if kind not in {"invoke_started", "invoke_completed", "invoke_failed"}:
            continue
        step_id = event.get("stepId")
        if not isinstance(step_id, str) or not step_id:
            raise BinderError("An authoring event has no stable step ID.")
        if kind == "invoke_started":
            if step_id in starts:
                raise BinderError(f"Step {step_id} has duplicate mutation intents.")
            starts[step_id] = event
            event_positions[(kind, step_id)] = event_index
            start_order.append(step_id)
        else:
            if step_id in terminals:
                raise BinderError(f"Step {step_id} has duplicate terminal events.")
            terminals[step_id] = event
            event_positions[(kind, step_id)] = event_index
            if kind == "invoke_completed":
                complete_order.append(step_id)
            else:
                failed_order.append(step_id)

    plan_ids = [step["id"] for step in steps]
    if (complete_order != plan_ids[:completed_count] or
            start_order != plan_ids[:len(start_order)] or
            set(starts) != set(terminals) or
            len(complete_order) + len(failed_order) != len(start_order)):
        raise BinderError("Runner mutation events are not one completed prefix followed by known failures.")
    if failed_order != plan_ids[completed_count:completed_count + len(failed_order)]:
        raise BinderError("Failed runner intents are not the next contiguous plan suffix.")
    if not failed_order or len(failed_order) > author_native.BATCH_MAX_OPERATIONS:
        raise BinderError("The runner has no bounded rejected batch suffix.")
    if (failure_policy is not None and
            len(failed_order) != failure_policy.get("expectedFailureCount")):
        raise BinderError("The rejected batch does not contain the exact expected number of intents.")

    result_by_id = {}
    for index, (planned, recorded) in enumerate(zip(steps, receipt_steps)):
        step_id = planned["id"]
        started = starts.get(step_id)
        completed = terminals.get(step_id)
        result = recorded.get("result")
        expected_values = _resolved_plan_values(planned, result_by_id, positions, index)
        expected_intent_values = (planned["values"] if started is not None and
                                  started.get("batchId") is not None else expected_values)
        if (started is None or completed is None or completed.get("event") != "invoke_completed" or
                started.get("commandId") != planned["commandId"] or
                completed.get("commandId") != planned["commandId"] or
                not _same(started.get("values"), expected_intent_values) or
                not _same(completed.get("values"), expected_values) or
                not isinstance(result, dict) or result.get("ok") is not True or
                not _same(completed.get("result"), result)):
            raise BinderError(f"Runner receipt/event mismatch at completed step {step_id}.")
        if event_positions[("invoke_completed", step_id)] <= event_positions[("invoke_started", step_id)]:
            raise BinderError(f"Completion event precedes its intent for step {step_id}.")
        result_by_id[step_id] = result

    failed_steps = steps[completed_count:completed_count + len(failed_order)]
    scope_failure = (isinstance(failure_policy, dict) and
                     failure_policy.get("kind") == "pre_dispatch_scope")
    if scope_failure:
        if (len(failed_steps) != 1 or len(failed_order) != 1 or
                failure_policy.get("expectedFailureCount") != 1):
            raise BinderError("The exact scope-rejected attempt must contain one unbatched intent.")
        failed_step = failed_steps[0]
        if (failed_step.get("id") != failure_policy.get("stepId") or
                failed_step.get("commandId") != failure_policy.get("commandId") or
                failed_step.get("commandId") in author_native.BATCH_SAFE_COMMAND_IDS):
            raise BinderError("The scope-rejected intent differs from the one approved source operation.")
        request = None
        failure_batch_id = None
        failure_count = 1
    else:
        selected_failed_steps, selected_failed_request = _greedy_batch_ids(
            steps, completed_count, result_by_id, plan_hash)
        if ([step["id"] for step in selected_failed_steps] !=
                [step["id"] for step in failed_steps] or selected_failed_request is None):
            raise BinderError("Rejected intents do not equal the runner's next deterministic bounded batch.")
        request = author_native._build_batch_request(plan_hash, failed_steps, result_by_id)
        if not request.get("fitsLimits") or len(request.get("operations", [])) != len(failed_steps):
            raise BinderError("The rejected suffix does not reconstruct as one bounded native batch.")
        failure_batch_id = request["batchId"]
        failure_count = len(failed_steps)
    failure_receipts = []
    first_failure_terminal = min(event_positions[("invoke_failed", step["id"])]
                                 for step in failed_steps)
    if any(event_positions[("invoke_started", step["id"])] >= first_failure_terminal
           for step in failed_steps):
        raise BinderError("The native batch failure arrived before all durable mutation intents.")
    if scope_failure:
        step = failed_steps[0]
        step_id = step["id"]
        started = starts[step_id]
        failed = terminals[step_id]
        expected_values = _resolved_plan_values(step, result_by_id, positions, completed_count)
        forbidden_batch_fields = {
            "batchId", "batchOperationId", "batchOperationCount", "batchOperationValues",
            "batchRequestSha256", "rolledBack", "recoveryRequired",
        }
        if (set(started) != {"schema", "planSha256", "event", "stepId", "commandId", "values", "atUtc"} or
                set(failed) != {"schema", "planSha256", "event", "stepId", "commandId",
                                "atUtc", "rpcMs", "requestBytes", "error"} or
                failed.get("event") != "invoke_failed" or
                started.get("event") != "invoke_started" or
                started.get("stepId") != step_id or failed.get("stepId") != step_id or
                started.get("commandId") != failure_policy.get("commandId") or
                failed.get("commandId") != failure_policy.get("commandId") or
                not _same(started.get("values"), expected_values) or
                forbidden_batch_fields.intersection(started) or
                forbidden_batch_fields.intersection(failed) or
                "values" in failed or
                not isinstance(failed.get("rpcMs"), (int, float)) or
                isinstance(failed.get("rpcMs"), bool) or
                not math.isfinite(failed["rpcMs"]) or failed["rpcMs"] <= 0 or
                not isinstance(failed.get("requestBytes"), int) or
                isinstance(failed.get("requestBytes"), bool) or failed["requestBytes"] <= 0):
            raise BinderError("Scope-rejected intent is not the exact unbatched frozen operation.")
        native_error = _validate_scope_rejection(failed.get("error"), failure_policy)
        if (set(receipt["failure"]) != {"stepId", "error"} or
                receipt["failure"].get("stepId") != step_id or
                receipt["failure"].get("error") != failed.get("error")):
            raise BinderError("Scope failure summary contains an unexpected recovery or rollback claim.")
        expected_rolled_back = None
        expected_recovery_required = None
        failure_receipts.append((failed, native_error))
    else:
        for step, operation in zip(failed_steps, request["operations"]):
            step_id = step["id"]
            started = starts[step_id]
            failed = terminals[step_id]
            if (failed.get("event") != "invoke_failed" or
                    started.get("commandId") != step["commandId"] or
                    failed.get("commandId") != step["commandId"] or
                    started.get("batchId") != failure_batch_id or
                    failed.get("batchId") != failure_batch_id or
                    started.get("batchOperationId") != operation["id"] or
                    failed.get("batchOperationId") != operation["id"] or
                    started.get("batchOperationCount") != failure_count or
                    failed.get("batchOperationCount") != failure_count or
                    not _same(started.get("values"), step["values"]) or
                    not _same(started.get("batchOperationValues"), operation["values"])):
                raise BinderError(f"Rejected batch intent {step_id} differs from the frozen plan.")
            if failure_policy is None:
                native_error = _native_guard_error(failed.get("error"))
                expected_rolled_back = False
                expected_recovery_required = True
            else:
                native_error = _validate_specific_batch_failure(
                    failed.get("error"), request, failure_policy)
                expected_rolled_back = failure_policy.get("runnerRolledBack")
                expected_recovery_required = failure_policy.get("runnerRecoveryRequired")
            if (failed.get("rolledBack") is not expected_rolled_back or
                    failed.get("recoveryRequired") is not expected_recovery_required):
                raise BinderError("Runner failure flags differ from the exact failure policy.")
            failure_receipts.append((failed, native_error))

    summary_failure = receipt.get("failure")
    if scope_failure:
        if (not isinstance(summary_failure, dict) or
                summary_failure.get("stepId") != failed_steps[0]["id"] or
                summary_failure.get("error") != failure_receipts[0][0].get("error")):
            raise BinderError("The JSON failure summary disagrees with the exact scope rejection.")
    elif (not isinstance(summary_failure, dict) or
          summary_failure.get("stepId") != failed_steps[0]["id"] or
          summary_failure.get("batchId") != failure_batch_id or
          summary_failure.get("error") != failure_receipts[0][0].get("error") or
          summary_failure.get("rolledBack") is not expected_rolled_back or
          summary_failure.get("recoveryRequired") is not expected_recovery_required):
        raise BinderError("The JSON failure summary disagrees with the exact guard-rejection events.")
    if any(row[0].get("error") != failure_receipts[0][0].get("error")
           for row in failure_receipts[1:]):
        raise BinderError("Rejected batch operations do not share one exact native guard response.")
    return {
        "steps": steps,
        "positions": positions,
        "completedCount": completed_count,
        "completedResults": result_by_id,
        "completedRecords": receipt_steps,
        "completedEvents": [terminals[step_id] for step_id in complete_order],
        "starts": starts,
        "terminals": terminals,
        "failedSteps": failed_steps,
        "failedEvents": [terminals[step_id] for step_id in failed_order],
        "failedBatchId": failure_batch_id,
        "failedBatchRequest": request,
        "failedNativeStructured": failure_receipts[0][1],
        "failedScopeCommandId": failure_policy.get("commandId") if scope_failure else None,
    }


def _outcome_matches(expected_result, outcome, label):
    if not isinstance(outcome, dict) or outcome.get("ok") is not True:
        raise BinderError(f"{label} has no successful native journal outcome.")
    for key, value in outcome.items():
        if key not in expected_result or not _same(value, expected_result[key]):
            raise BinderError(f"{label} journal outcome disagrees with its runner receipt at {key}.")


def _expected_journal_prefix(prefix, plan_hash, resource_id):
    steps = prefix["steps"]
    records = prefix["completedRecords"]
    events = prefix["completedEvents"]
    positions = prefix["positions"]
    expected = []
    prior_results = {}
    index = 0
    while index < len(records):
        event = events[index]
        step = steps[index]
        batch_id = event.get("batchId")
        if batch_id is None:
            expected_values = _resolved_plan_values(step, prior_results, positions, index)
            if (not _same(prefix["starts"][step["id"]].get("values"), expected_values) or
                    not _same(event.get("values"), expected_values)):
                raise BinderError(f"Completed values mismatch for {step['id']}.")
            result = records[index].get("result")
            expected.append({
                "resourceId": resource_id,
                "command": step["commandId"],
                "arguments": expected_values,
                "outcome": result,
                "ids": [step["id"]],
            })
            prior_results[step["id"]] = result
            index += 1
            continue

        end = index + 1
        while end < len(events) and events[end].get("batchId") == batch_id:
            end += 1
        group_steps = steps[index:end]
        if end - index > author_native.BATCH_MAX_OPERATIONS:
            raise BinderError("A completed batch exceeds the native operation bound.")
        request = author_native._build_batch_request(plan_hash, group_steps, prior_results)
        if not request.get("fitsLimits") or request.get("batchId") != batch_id:
            raise BinderError(f"Completed batch {batch_id} does not reconstruct from the plan.")
        greedy_steps, greedy_request = _greedy_batch_ids(
            steps, index, prior_results, plan_hash)
        if ([step["id"] for step in greedy_steps] !=
                [step["id"] for step in group_steps] or greedy_request is None):
            raise BinderError(f"Completed batch {batch_id} differs from the runner's deterministic grouping.")
        start_events = [prefix["starts"][s["id"]] for s in group_steps]
        group_events = events[index:end]
        request_shas = set()
        rows = []
        derived_clip_ids = []
        for offset, (group_step, operation, started, completed) in enumerate(
                zip(group_steps, request["operations"], start_events, group_events)):
            result = records[index + offset].get("result")
            if (started.get("batchId") != batch_id or
                    started.get("batchOperationId") != operation["id"] or
                    started.get("batchOperationCount") != len(group_steps) or
                    not _same(started.get("values"), group_step["values"]) or
                    not _same(started.get("batchOperationValues"), operation["values"])):
                raise BinderError(f"Completed batch intent differs at {group_step['id']}.")
            expected_values = _resolved_plan_values(
                group_step, prior_results, positions, index + offset)
            if (not _same(completed.get("values"), expected_values) or
                    not isinstance(result, dict) or result.get("ok") is not True or
                    result.get("commandId") != group_step["commandId"] or
                    result.get("batchId") != batch_id or
                    completed.get("batchOperationId") != operation["id"] or
                    completed.get("batchOperationCount") != len(group_steps)):
                raise BinderError(f"Completed batch result differs at {group_step['id']}.")
            request_sha = _valid_sha(completed.get("batchRequestSha256"),
                                     "Completed batch request hash")
            request_shas.add(request_sha)
            row = {"id": operation["id"], "commandId": group_step["commandId"],
                   "ok": True}
            for key in ("createdClipId", "trackId", "alreadySatisfied"):
                if key in result:
                    row[key] = result[key]
            rows.append(row)
            if isinstance(result.get("createdClipId"), str):
                derived_clip_ids.append(result["createdClipId"])
            prior_results[group_step["id"]] = result
        if len(request_shas) != 1:
            raise BinderError(f"Completed batch {batch_id} has conflicting request hashes.")
        expected.append({
            "resourceId": resource_id,
            "command": BATCH_COMMAND_ID,
            "arguments": {
                "batchId": batch_id,
                "_batchId": batch_id,
                "_batchRequestSha256": next(iter(request_shas)),
                "operations": request["operations"],
            },
            "batchResults": rows,
            "batchOperationCount": len(group_steps),
            "createdClipIds": derived_clip_ids,
            "ids": [s["id"] for s in group_steps],
        })
        index = end
    return expected


def _validate_task_scope(task, expected_status, task_id, resource_id, plan):
    if (task.get("taskId") != task_id or task.get("status") != expected_status or
            task.get("ok") is not True):
        raise BinderError(f"Native task proof does not identify the {expected_status} source task.")
    if task.get("active") not in (None, expected_status != "released"):
        raise BinderError("Native task active state conflicts with its lifecycle status.")
    target_ids = task.get("targetResourceIds")
    if target_ids is not None and target_ids != [resource_id]:
        raise BinderError("Native task does not target exactly the original Timeline resource.")
    scope = task.get("nativeTimelineScope")
    if not isinstance(scope, dict) or scope.get("targetIds") != [resource_id]:
        raise BinderError("Native task scope does not seal the exact original Timeline resource.")
    if scope.get("outputDirectory") != plan.get("outputDirectory"):
        raise BinderError("Native task output directory differs from the frozen plan scope.")
    if scope.get("assetPaths") != plan.get("assetPaths"):
        raise BinderError("Native task assetPaths differ from the frozen 104-file scope.")
    resources = task.get("resources")
    if not isinstance(resources, list):
        raise BinderError("Native task proof has no resource checkpoint list.")
    if task.get("uncertainOperation") is True:
        raise BinderError("Native task proof contains an uncertain operation.")
    if any(not isinstance(item, dict) or item.get("pending") is not False or
           item.get("reverted") is not False for item in resources):
        raise BinderError("A native task resource remains pending or reverted.")
    matches = [resource for resource in resources
               if isinstance(resource, dict) and resource.get("id") == resource_id]
    if len(matches) != 1:
        raise BinderError("Native task must contain exactly one original Timeline resource checkpoint.")
    resource = matches[0]
    if (resource.get("pending") is not False or resource.get("reverted") is not False):
        raise BinderError("Native Timeline resource is pending or reverted.")
    current_hash = _valid_sha(resource.get("currentHash"), "Native task currentHash")
    return scope, resource, current_hash


def _validate_journal_steps(task, expected_rows, resource_id, save_path=None,
                            save_result=None):
    actual = task.get("steps")
    expected_count = len(expected_rows)
    add_save = save_path is not None
    if not isinstance(actual, list) or len(actual) != expected_count + int(add_save):
        raise BinderError("Native task journal contains missing or extra operations beyond the proven prefix/save.")
    for index, expected in enumerate(expected_rows):
        step = actual[index]
        if (not isinstance(step, dict) or step.get("id") != index or
                step.get("status") != "completed" or
                step.get("resourceId") != resource_id or
                step.get("command") != expected["command"] or
                not _same(step.get("arguments"), expected["arguments"])):
            raise BinderError(f"Native journal does not match completed runner prefix at step {index}.")
        outcome = step.get("outcome")
        if expected["command"] != BATCH_COMMAND_ID:
            _outcome_matches(expected["outcome"], outcome, f"Native journal step {index}")
        else:
            if (not isinstance(outcome, dict) or outcome.get("ok") is not True or
                    outcome.get("batchId") != expected["arguments"]["batchId"] or
                    outcome.get("batchRequestSha256") != expected["arguments"]["_batchRequestSha256"] or
                    outcome.get("operationCount") != expected["batchOperationCount"] or
                    not _same(outcome.get("batchResults"), expected["batchResults"])):
                raise BinderError(f"Native journal batch outcome disagrees at step {index}.")
            if "changed" not in outcome or not isinstance(outcome.get("changed"), bool):
                raise BinderError(f"Native journal batch has no explicit changed flag at step {index}.")
            if "createdClipIds" in outcome and outcome.get("createdClipIds") != expected["createdClipIds"]:
                raise BinderError(f"Native journal batch clip IDs disagree at step {index}.")
            if "createdClipId" in outcome:
                ids = expected["createdClipIds"]
                if outcome["createdClipId"] != (ids[0] if len(ids) == 1 else None):
                    raise BinderError(f"Native journal batch singular clip ID disagrees at step {index}.")
    if not add_save:
        return
    save = actual[-1]
    if (not isinstance(save, dict) or save.get("id") != expected_count or
            save.get("status") != "completed" or save.get("resourceId") != resource_id or
            save.get("command") != SAVE_COMMAND_ID or
            not _same(save.get("arguments"), {"outputPath": save_path})):
        raise BinderError("The only post-prefix native action must be the exact successful task-owned save.")
    outcome = save.get("outcome")
    if not isinstance(outcome, dict) or outcome.get("ok") is not True:
        raise BinderError("Native journal does not show the checkpoint save completed successfully.")
    if save_result is not None:
        for key in ("outputPath", "timelineId", "documentHash", "fileSha256", "readbackVerified", "clipCount"):
            if key in outcome and key in save_result and not _same(outcome[key], save_result[key]):
                raise BinderError(f"Native save journal and tool receipt disagree at {key}.")


def _validate_journal_history(task, expected_rows, failure_rows, resource_id,
                              save_path=None, save_result=None):
    """Validate successful plan rows interleaved with exact failed attempts."""
    actual = task.get("steps")
    failures = sorted(failure_rows, key=lambda item: item["journalIndex"])
    indexes = [item.get("journalIndex") for item in failures]
    if (not isinstance(actual, list) or len(indexes) != len(set(indexes)) or
            any(not isinstance(index, int) or isinstance(index, bool) or index < 0
                for index in indexes)):
        raise BinderError("Native journal failure positions are malformed.")
    add_save = save_path is not None
    if len(actual) != len(expected_rows) + len(failures) + int(add_save):
        raise BinderError("Native journal contains missing or extra operations beyond the proven lineage/save.")
    failures_by_index = {item["journalIndex"]: item for item in failures}
    expected_index = 0
    for actual_index, step in enumerate(actual[:len(actual) - int(add_save)]):
        if actual_index in failures_by_index:
            expected_failure = failures_by_index[actual_index]
            outcome = step.get("outcome") if isinstance(step, dict) else None
            if (not isinstance(step, dict) or step.get("id") != actual_index or
                    step.get("status") != "failed" or
                    step.get("resourceId") != resource_id or
                    step.get("command") != BATCH_COMMAND_ID or
                    not _same(step.get("arguments"), expected_failure["arguments"]) or
                    not isinstance(outcome, dict) or
                    not _same(outcome, expected_failure["outcome"])):
                raise BinderError(f"Native journal rejected-batch row differs at step {actual_index}.")
            if (outcome.get("ok") is not False or outcome.get("changed") is not False or
                    outcome.get("mutationNotApplied") is not True):
                raise BinderError(f"Native journal rejected-batch row claims a mutation at step {actual_index}.")
            continue
        if expected_index >= len(expected_rows):
            raise BinderError("Native journal has an unexpected successful step after the verified lineage.")
        normalized = dict(step) if isinstance(step, dict) else step
        if isinstance(normalized, dict):
            normalized["id"] = 0
        _validate_journal_steps({"steps": [normalized]}, [expected_rows[expected_index]], resource_id)
        expected_index += 1
    if expected_index != len(expected_rows):
        raise BinderError("Native journal ended before all completed prefix rows were proven.")
    if set(failures_by_index) != {item["journalIndex"] for item in failures}:
        raise BinderError("Native journal has conflicting rejected-batch positions.")
    if not add_save:
        return
    save = actual[-1]
    if (not isinstance(save, dict) or save.get("id") != len(actual) - 1 or
            save.get("status") != "completed" or save.get("resourceId") != resource_id or
            save.get("command") != SAVE_COMMAND_ID or
            not _same(save.get("arguments"), {"outputPath": save_path})):
        raise BinderError("The only post-lineage native action must be the exact task-owned checkpoint save.")
    outcome = save.get("outcome")
    if not isinstance(outcome, dict) or outcome.get("ok") is not True:
        raise BinderError("Native journal does not show the checkpoint save completed successfully.")
    if save_result is not None:
        for key in ("outputPath", "timelineId", "documentHash", "fileSha256", "readbackVerified", "clipCount"):
            if key in outcome and key in save_result and not _same(outcome[key], save_result[key]):
                raise BinderError(f"Native save journal and tool receipt disagree at {key}.")


def _validate_review_receipt(task, label, task_id, expected_status):
    if (task.get("taskId") != task_id or task.get("ok") is not True or
            task.get("status") != expected_status):
        raise BinderError(f"{label} does not prove task {task_id} reached {expected_status}.")


def _canonical_task_uuid(value, label):
    try:
        parsed = uuid.UUID(value)
    except (ValueError, TypeError, AttributeError) as error:
        raise BinderError(f"{label} is not a canonical UUID.") from error
    if str(parsed) != value:
        raise BinderError(f"{label} is not a canonical UUID.")
    return value


def _expected_pill_radius_corrections():
    corrections = []
    for index in range(3):
        corrections.append({
            "stepId": f"film-closing/framing-pill-background-{index}",
            "path": ["values", "values", "shapeCornerRadius"],
            "old": 0.8333333333333333,
            "new": 0.5,
        })
        corrections.append({
            "stepId": f"film-closing/framing-pill-background-{index}-frame",
            "path": ["values", "cornerRadius"],
            "old": 0.8333333333333333,
            "new": 0.5,
        })
    return corrections


def _validate_schema_upgrade_evidence(evidence):
    expected_fields = {
        "schema", "priorPlanSha256", "priorReceiptSha256", "priorEventLogSha256",
        "currentPlanSha256", "currentReceiptSha256", "currentEventLogSha256",
        "correctedPlanSha256", "priorFailure", "currentFailure", "radiusRepair",
        "journalCompletedRowCount", "journalFailedStepIds", "blockedTaskProofSha256",
        "checkpointDocumentProofSha256",
    }
    if (not isinstance(evidence, dict) or set(evidence) != expected_fields or
            evidence.get("schema") != SCHEMA_UPGRADE_SCHEMA):
        raise BinderError("Schema-upgrade provenance is malformed or uses an unknown schema.")
    for field in ("priorPlanSha256", "priorReceiptSha256", "priorEventLogSha256",
                  "currentPlanSha256", "currentReceiptSha256", "currentEventLogSha256",
                  "correctedPlanSha256", "blockedTaskProofSha256",
                  "checkpointDocumentProofSha256"):
        _valid_sha(evidence.get(field), f"Schema-upgrade {field}")
    prior = evidence.get("priorFailure")
    if (not isinstance(prior, dict) or set(prior) != {
            "journalStepId", "batchId", "requestSha256", "taskStateHash",
            "rolledBack", "mutationNotApplied", "changed"} or
            prior.get("journalStepId") != 44 or prior.get("rolledBack") is not True or
            prior.get("mutationNotApplied") is not True or prior.get("changed") is not False):
        raise BinderError("Schema-upgrade provenance does not prove the earlier atomic rollback.")
    _canonical_task_uuid(prior.get("batchId"), "Earlier rolled-back batch ID")
    _valid_sha(prior.get("requestSha256"), "Earlier rolled-back batch request hash")
    _valid_sha(prior.get("taskStateHash"), "Earlier rolled-back batch state hash")
    current = evidence.get("currentFailure")
    if (not isinstance(current, dict) or set(current) != {
            "journalStepId", "batchId", "requestSha256", "taskStateHash",
            "mutationNotApplied", "changed"} or
            current.get("journalStepId") != 76 or
            current.get("mutationNotApplied") is not True or current.get("changed") is not False):
        raise BinderError("Schema-upgrade provenance does not prove the exact pre-dispatch typed-schema rejection.")
    _canonical_task_uuid(current.get("batchId"), "Current rejected batch ID")
    _valid_sha(current.get("requestSha256"), "Current rejected batch request hash")
    _valid_sha(current.get("taskStateHash"), "Current rejected batch state hash")
    if evidence.get("journalCompletedRowCount") != 75 or evidence.get("journalFailedStepIds") != [44, 76]:
        raise BinderError("Schema-upgrade provenance does not describe the exact native journal shape.")
    repair = evidence.get("radiusRepair")
    if (not isinstance(repair, dict) or repair.get("schema") != "looksy.native-pill-radius-correction/v1" or
            repair.get("completedPrefixPreserved") != 278 or
            repair.get("nativeAuthoringExecuted") is not False):
        raise BinderError("Schema-upgrade provenance is missing the unexecuted pill-radius correction proof.")
    if set(repair) != {"schema", "parentPlanPath", "parentPlanSha256",
                       "completedPrefixPreserved", "corrections", "nativeAuthoringExecuted"}:
        raise BinderError("Pill-radius correction provenance has unexpected fields.")
    _valid_sha(repair.get("parentPlanSha256"), "Pill-radius source plan hash")
    expected = _expected_pill_radius_corrections()
    raw_corrections = repair.get("corrections")
    if not isinstance(raw_corrections, list) or len(raw_corrections) != len(expected):
        raise BinderError("Pill-radius correction provenance must list exactly six fields.")
    compact = []
    for item in raw_corrections:
        if not isinstance(item, dict) or not {"stepId", "path", "old", "new"}.issubset(item):
            raise BinderError("Pill-radius correction entry is malformed.")
        compact.append({key: item[key] for key in ("stepId", "path", "old", "new")})
    if not _same(compact, expected):
        raise BinderError("Pill-radius correction provenance differs from the six approved native-limit clamps.")


def _validate_provenance_node(provenance, depth=0):
    if depth > 12 or not isinstance(provenance, dict):
        raise BinderError("Continuation provenance chain is malformed or too deep.")
    schema = provenance.get("schema")
    resource_id = provenance.get("resourceId")
    task_id = provenance.get("taskId")
    starting_hash = provenance.get("startingDocumentHash")
    try:
        retarget._resource_uuid(resource_id, "Continuation provenance")
    except retarget.RetargetError as error:
        raise BinderError(str(error)) from error
    _canonical_task_uuid(task_id, "Continuation provenance task ID")
    _valid_sha(starting_hash, "Continuation provenance starting hash")

    if schema == SCOPE_REVIEW_SCHEMA:
        _validate_scope_continuation_provenance(provenance)
        return

    if schema == continuation.CONTINUATION_SCHEMA:
        return

    if schema == retarget.PROVENANCE_SCHEMA:
        prior = provenance.get("priorContinuationProvenance")
        if not isinstance(prior, dict):
            raise BinderError("Imported continuation provenance has no prior source provenance.")
        expected_prior_hash = _sha256(_canonical_json(prior).encode("utf-8"))
        if provenance.get("priorContinuationProvenanceSha256") != expected_prior_hash:
            raise BinderError("Imported continuation provenance prior hash does not match its payload.")
        if provenance.get("journalCheckpointHash") != starting_hash:
            raise BinderError("Imported continuation provenance checkpoint differs from its starting hash.")
        for field in (
                "priorPlanSha256", "nativeImportReceiptSha256",
                "nativeAdmissionReceiptSha256", "nativeDocumentProofSha256",
                "sourceDocumentProofSha256", "sourceProjectSha256",
                "copyProjectSha256", "clipIdSetSha256", "trackIdSetSha256"):
            _valid_sha(provenance.get(field), f"Imported provenance {field}")
        source_graph = _valid_sha(provenance.get("sourceGraphSha256"), "Imported source graph hash")
        copy_graph = _valid_sha(provenance.get("copyGraphSha256"), "Imported copy graph hash")
        if source_graph != copy_graph:
            raise BinderError("Imported continuation provenance does not prove an exact graph copy.")
        source_resource_id = provenance.get("sourceProjectResourceId")
        try:
            retarget._resource_uuid(source_resource_id, "Imported source resource")
        except retarget.RetargetError as error:
            raise BinderError(str(error)) from error
        if (not isinstance(provenance.get("clipCount"), int) or
                isinstance(provenance.get("clipCount"), bool) or provenance["clipCount"] < 1 or
                not isinstance(provenance.get("trackCount"), int) or
                isinstance(provenance.get("trackCount"), bool) or provenance["trackCount"] < 1):
            raise BinderError("Imported continuation provenance has invalid project graph counts.")
        _validate_provenance_node(prior, depth + 1)
        return

    if schema != BINDER_SCHEMA:
        raise BinderError("Continuation provenance uses an unknown schema.")

    legacy_binder_fields = {
        "schema", "parentPlanSha256", "parentReceiptSha256", "parentEventLogSha256",
        "stopReviewProofSha256", "finalReviewProofSha256", "releasedSourceTaskProofSha256",
        "sourceDocumentProofSha256", "sourceSaveProofSha256", "sourceProjectSha256",
        "sourceGraphSha256", "copyGraphSha256", "importReceiptSha256",
        "freshAdmissionProofSha256", "importedDocumentProofSha256",
        "completedPrefixCount", "lastCompletedStepId", "completedPrefixResultSha256",
        "rejectedStepIds", "rejectedBatchId", "rejectedGuard", "runnerClaimedRollback",
        "runnerClaimedRecoveryRequired", "nativePreDispatchRejectionVerified",
        "nativeTaskJournalHasNoRejectedBatchStep", "sourceResourceId", "sourceTaskId",
        "sourceDocumentHash", "sourceSavedDocumentPath", "sourceFileSha256",
        "sourceTrackIdSetSha256", "sourceClipIdSetSha256", "resourceId", "taskId",
        "startingDocumentHash", "journalCheckpointHash", "outputDirectory", "outputPath",
        "clipCount", "trackCount", "retargetedFields",
    }
    chain_fields = {"priorContinuationProvenance", "priorContinuationProvenanceSha256"}
    upgrade_fields = {"schemaUpgradeEvidence"}
    allowed_fields = {
        frozenset(legacy_binder_fields),
        frozenset(legacy_binder_fields | chain_fields),
        frozenset(legacy_binder_fields | upgrade_fields),
        frozenset(legacy_binder_fields | chain_fields | upgrade_fields),
    }
    if frozenset(provenance) not in allowed_fields:
        raise BinderError("Reviewed-prefix provenance has missing or unexpected fields.")
    for field in (
            "parentPlanSha256", "parentReceiptSha256", "parentEventLogSha256",
            "stopReviewProofSha256", "finalReviewProofSha256", "releasedSourceTaskProofSha256",
            "sourceDocumentProofSha256", "sourceSaveProofSha256", "sourceProjectSha256",
            "sourceGraphSha256", "copyGraphSha256", "importReceiptSha256",
            "freshAdmissionProofSha256", "importedDocumentProofSha256",
            "completedPrefixResultSha256", "sourceDocumentHash", "sourceFileSha256",
            "sourceTrackIdSetSha256", "sourceClipIdSetSha256", "journalCheckpointHash"):
        _valid_sha(provenance.get(field), f"Reviewed-prefix provenance {field}")
    if (provenance["sourceProjectSha256"] != provenance["sourceFileSha256"] or
            provenance["sourceGraphSha256"] != provenance["copyGraphSha256"] or
            provenance["journalCheckpointHash"] != starting_hash):
        raise BinderError("Reviewed-prefix provenance source, graph, or checkpoint hashes disagree.")
    try:
        retarget._resource_uuid(provenance.get("sourceResourceId"), "Reviewed source resource")
    except retarget.RetargetError as error:
        raise BinderError(str(error)) from error
    _canonical_task_uuid(provenance.get("sourceTaskId"), "Reviewed source task ID")
    if (not isinstance(provenance.get("completedPrefixCount"), int) or
            isinstance(provenance.get("completedPrefixCount"), bool) or
            provenance["completedPrefixCount"] < 1 or
            not isinstance(provenance.get("lastCompletedStepId"), str) or
            not provenance["lastCompletedStepId"]):
        raise BinderError("Reviewed-prefix provenance has no valid completed prefix.")
    rejected_ids = provenance.get("rejectedStepIds")
    if (not isinstance(rejected_ids, list) or not rejected_ids or
            any(not isinstance(item, str) or not item for item in rejected_ids) or
            len(rejected_ids) != len(set(rejected_ids))):
        raise BinderError("Reviewed-prefix provenance has invalid rejected step IDs.")
    _canonical_task_uuid(provenance.get("rejectedBatchId"), "Rejected batch ID")
    if "schemaUpgradeEvidence" not in provenance:
        if (provenance.get("rejectedGuard") != EXPECTED_GUARD_ERROR or
                provenance.get("runnerClaimedRollback") is not False or
                provenance.get("runnerClaimedRecoveryRequired") is not True or
                provenance.get("nativePreDispatchRejectionVerified") is not True or
                provenance.get("nativeTaskJournalHasNoRejectedBatchStep") is not True):
            raise BinderError("Reviewed-prefix provenance does not preserve its verified pre-dispatch guard facts.")
    else:
        upgrade_evidence = provenance["schemaUpgradeEvidence"]
        _validate_schema_upgrade_evidence(upgrade_evidence)
        prior = provenance.get("priorContinuationProvenance")
        if (not isinstance(prior, dict) or
                upgrade_evidence.get("priorPlanSha256") != prior.get("parentPlanSha256") or
                upgrade_evidence.get("priorReceiptSha256") != prior.get("priorReceiptSha256") or
                upgrade_evidence.get("priorEventLogSha256") != prior.get("priorEventLogSha256") or
                upgrade_evidence.get("priorFailure", {}).get("batchId") != prior.get("failedBatchId") or
                upgrade_evidence.get("priorFailure", {}).get("requestSha256") != prior.get("failedBatchRequestSha256") or
                upgrade_evidence.get("priorFailure", {}).get("journalStepId") != prior.get("nativeTaskStepId") or
                prior.get("nativeRollbackVerified") is not True or
                upgrade_evidence.get("currentPlanSha256") != provenance.get("parentPlanSha256") or
                upgrade_evidence.get("currentReceiptSha256") != provenance.get("parentReceiptSha256") or
                upgrade_evidence.get("currentEventLogSha256") != provenance.get("parentEventLogSha256") or
                upgrade_evidence.get("currentFailure", {}).get("batchId") != provenance.get("rejectedBatchId") or
                upgrade_evidence.get("currentFailure", {}).get("taskStateHash") != provenance.get("sourceDocumentHash") or
                upgrade_evidence.get("radiusRepair", {}).get("parentPlanSha256") != upgrade_evidence.get("currentPlanSha256")):
            raise BinderError("Schema-upgrade evidence is disconnected from the source or current plan provenance.")
        if (provenance.get("rejectedGuard") != SCHEMA_TYPED_REJECTION or
                provenance.get("runnerClaimedRollback") is not False or
                provenance.get("runnerClaimedRecoveryRequired") is not False or
                provenance.get("nativePreDispatchRejectionVerified") is not True or
                provenance.get("nativeTaskJournalHasNoRejectedBatchStep") is not False):
            raise BinderError("Schema-upgrade provenance misstates runner or native journal failure facts.")
    for field in ("clipCount", "trackCount"):
        if not isinstance(provenance.get(field), int) or isinstance(provenance.get(field), bool) or provenance[field] < 1:
            raise BinderError("Reviewed-prefix provenance has invalid project graph counts.")
    source_path = provenance.get("sourceSavedDocumentPath")
    output_directory = provenance.get("outputDirectory")
    output_path = provenance.get("outputPath")
    if (not isinstance(source_path, str) or not Path(source_path).is_absolute() or
            not isinstance(output_directory, str) or not Path(output_directory).is_absolute() or
            not isinstance(output_path, str) or not Path(output_path).is_absolute() or
            str(Path(source_path)) != source_path or
            str(Path(output_directory)) != output_directory or
            str(Path(output_path)) != output_path or
            Path(output_path).parent != Path(output_directory) or
            Path(output_path).suffix.lower() != ".json"):
        raise BinderError("Reviewed-prefix provenance paths are malformed.")
    expected_retargeted_fields = [
        "steps[completed-prefix:]", "resourceId", "taskId", "startingDocumentHash",
        "outputPath", "steps[delivery/save-editable-project].values.outputPath",
        "continuationProvenance", "proofStatus",
    ]
    if provenance.get("retargetedFields") != expected_retargeted_fields:
        raise BinderError("Reviewed-prefix provenance does not identify the reviewed retarget operation.")

    if chain_fields <= set(provenance):
        prior = provenance.get("priorContinuationProvenance")
        if not isinstance(prior, dict) or provenance.get("priorContinuationProvenanceSha256") != _sha256(
                _canonical_json(prior).encode("utf-8")):
            raise BinderError("Reviewed-prefix provenance prior hash does not match its payload.")
        _validate_provenance_node(prior, depth + 1)


def _chain_parent_provenance(parent_provenance, generated_provenance):
    if not isinstance(parent_provenance, dict):
        raise BinderError("Frozen parent plan has no continuation provenance to carry forward.")
    chained = json.loads(_canonical_json(generated_provenance))
    prior = json.loads(_canonical_json(parent_provenance))
    chained["priorContinuationProvenance"] = prior
    chained["priorContinuationProvenanceSha256"] = _sha256(
        _canonical_json(prior).encode("utf-8"))
    return chained


def _validate_scope_continuation_provenance(provenance):
    expected_fields = {
        "schema", "parentPlanSha256", "parentReceiptSha256", "parentEventLogSha256",
        "stopReviewProofSha256", "finalReviewProofSha256", "releasedSourceTaskProofSha256",
        "sourceDocumentProofSha256", "sourceSaveProofSha256", "sourceProjectSha256",
        "sourceGraphSha256", "copyGraphSha256", "importReceiptSha256",
        "freshAdmissionProofSha256", "importedDocumentProofSha256", "completedPrefixCount",
        "lastCompletedStepId", "completedPrefixResultSha256", "rejectedStepIds",
        "rejectedGuard", "nativePreDispatchRejectionVerified", "sourceResourceId",
        "sourceTaskId", "sourceDocumentHash", "sourceSavedDocumentPath", "sourceFileSha256",
        "sourceTrackIdSetSha256", "sourceClipIdSetSha256", "resourceId", "taskId",
        "startingDocumentHash", "journalCheckpointHash", "outputDirectory", "outputPath",
        "clipCount", "trackCount", "retargetedFields", "runnerFailureClaims",
        "nativeJournalRejectedOperationPresent", "scopeFailureEvidence",
        "sourceBindingProvenance", "sourceBindingProvenanceSha256",
    }
    if (set(provenance) != expected_fields or
            provenance.get("schema") != SCOPE_REVIEW_SCHEMA or
            provenance.get("rejectedGuard") != SCOPE_REJECTION_ERROR or
            provenance.get("nativePreDispatchRejectionVerified") is not True or
            provenance.get("nativeJournalRejectedOperationPresent") is not False or
            provenance.get("runnerFailureClaims") != {"rollback": None, "recoveryRequired": None}):
        raise BinderError("Scope-rejection provenance is malformed or overstates mutation status.")
    for field in (
            "parentPlanSha256", "parentReceiptSha256", "parentEventLogSha256",
            "stopReviewProofSha256", "finalReviewProofSha256", "releasedSourceTaskProofSha256",
            "sourceDocumentProofSha256", "sourceSaveProofSha256", "sourceProjectSha256",
            "sourceGraphSha256", "copyGraphSha256", "importReceiptSha256",
            "freshAdmissionProofSha256", "importedDocumentProofSha256",
            "completedPrefixResultSha256", "sourceDocumentHash", "sourceFileSha256",
            "sourceTrackIdSetSha256", "sourceClipIdSetSha256", "journalCheckpointHash",
            "sourceBindingProvenanceSha256"):
        _valid_sha(provenance.get(field), f"Scope continuation provenance {field}")
    if (provenance.get("sourceProjectSha256") != provenance.get("sourceFileSha256") or
            provenance.get("sourceGraphSha256") != provenance.get("copyGraphSha256") or
            provenance.get("journalCheckpointHash") != provenance.get("startingDocumentHash")):
        raise BinderError("Scope-rejection source, graph, or checkpoint hashes disagree.")
    try:
        retarget._resource_uuid(provenance.get("sourceResourceId"), "Reviewed source resource")
        retarget._resource_uuid(provenance.get("resourceId"), "Imported Timeline resource")
    except retarget.RetargetError as error:
        raise BinderError(str(error)) from error
    _canonical_task_uuid(provenance.get("sourceTaskId"), "Reviewed source task ID")
    _canonical_task_uuid(provenance.get("taskId"), "Imported native task ID")
    if (provenance.get("completedPrefixCount") != 1637 or
            isinstance(provenance.get("completedPrefixCount"), bool) or
            provenance.get("lastCompletedStepId") != "library-carousel/lane-portrait-library-media-07" or
            provenance.get("rejectedStepIds") != ["library-carousel/p-lib-62-IMG_4788-image"]):
        raise BinderError("Scope-rejection provenance identifies a different portrait prefix or retry step.")

    evidence = provenance.get("scopeFailureEvidence")
    evidence_fields = {
        "schema", "parentPlanSha256", "parentReceiptSha256", "parentEventLogSha256",
        "correctedPlanPath", "correctedPlanSha256", "reviewTaskProofSha256",
        "sourceResourceId", "sourceTaskId", "stepId", "commandId", "sourcePath", "error",
        "structuredContent", "runnerClaims", "nativeJournalRowCount",
        "nativeRejectedOperationRowPresent", "reviewTaskHash", "completedPrefixCount",
        "completedPrefixResultSha256", "schemaRepair",
    }
    if (not isinstance(evidence, dict) or set(evidence) != evidence_fields or
            evidence.get("schema") != "looksy.native-scope-rejection/v1" or
            evidence.get("parentPlanSha256") != provenance.get("parentPlanSha256") or
            evidence.get("parentReceiptSha256") != provenance.get("parentReceiptSha256") or
            evidence.get("parentEventLogSha256") != provenance.get("parentEventLogSha256") or
            evidence.get("sourceResourceId") != provenance.get("sourceResourceId") or
            evidence.get("sourceTaskId") != provenance.get("sourceTaskId") or
            evidence.get("stepId") != provenance["rejectedStepIds"][0] or
            evidence.get("commandId") != SCOPE_REJECTION_COMMAND or
            evidence.get("error") != SCOPE_REJECTION_ERROR or
            evidence.get("structuredContent") != {
                "blockedByScope": True, "ok": False, "error": SCOPE_REJECTION_ERROR} or
            evidence.get("runnerClaims") != {"rollback": None, "recoveryRequired": None} or
            evidence.get("nativeJournalRowCount") != 236 or
            evidence.get("nativeRejectedOperationRowPresent") is not False or
            evidence.get("reviewTaskHash") != provenance.get("sourceDocumentHash") or
            evidence.get("completedPrefixCount") != 1637 or
            evidence.get("completedPrefixResultSha256") != provenance.get("completedPrefixResultSha256")):
        raise BinderError("Scope-rejection evidence does not pin the exact pre-dispatch and no-row proof.")
    for field in ("parentPlanSha256", "parentReceiptSha256", "parentEventLogSha256",
                  "correctedPlanSha256", "reviewTaskProofSha256", "reviewTaskHash",
                  "completedPrefixResultSha256"):
        _valid_sha(evidence.get(field), f"Scope rejection evidence {field}")
    corrected_path = evidence.get("correctedPlanPath")
    if (not isinstance(corrected_path, str) or not Path(corrected_path).is_absolute() or
            str(Path(corrected_path)) != corrected_path):
        raise BinderError("Scope-rejection corrected parent path is not canonical.")
    corrected_bytes, corrected_plan = _read_json(corrected_path, "scope corrected parent plan")
    if _sha256(corrected_bytes) != evidence.get("correctedPlanSha256"):
        raise BinderError("Scope-rejection corrected parent plan hash changed after binding.")
    repair = evidence.get("schemaRepair")
    if (not isinstance(repair, dict) or
            repair.get("schema") != "looksy.native-schema-repair/v1" or
            repair.get("parentPlanSha256") != evidence.get("parentPlanSha256") or
            repair.get("nativeAuthoringExecuted") is not False or
            len(repair.get("changes", [])) != 7 or
            corrected_plan.get("nativeSchemaRepairProvenance") != repair):
        raise BinderError("Scope-rejection provenance lost its corrected portrait repair proof.")

    binding = provenance.get("sourceBindingProvenance")
    if (not isinstance(binding, dict) or
            binding.get("schema") != PORTRAIT_BOOTSTRAP_SCHEMA or
            binding.get("resourceId") != provenance.get("sourceResourceId") or
            binding.get("taskId") != provenance.get("sourceTaskId") or
            binding.get("startingDocumentHash") != corrected_plan.get("startingDocumentHash") or
            provenance.get("sourceBindingProvenanceSha256") != _sha256(
                _canonical_json(binding).encode("utf-8"))):
        raise BinderError("Scope-rejection provenance lost its native bootstrap binding.")
    source_path = provenance.get("sourceSavedDocumentPath")
    output_directory = provenance.get("outputDirectory")
    output_path = provenance.get("outputPath")
    if (not isinstance(source_path, str) or not Path(source_path).is_absolute() or
            not isinstance(output_directory, str) or not Path(output_directory).is_absolute() or
            not isinstance(output_path, str) or not Path(output_path).is_absolute() or
            Path(output_path).parent != Path(output_directory) or
            Path(output_path).suffix.lower() != ".json"):
        raise BinderError("Scope-rejection provenance has malformed source or output paths.")
    if provenance.get("retargetedFields") != [
            "steps[completed-prefix:]", "resourceId", "taskId", "startingDocumentHash",
            "outputPath", "steps[delivery/save-editable-project].values.outputPath",
            "continuationProvenance", "proofStatus"]:
        raise BinderError("Scope-rejection provenance does not describe exact retargeting.")


def _validate_parent_identity(plan):
    resource_id = plan.get("resourceId")
    task_id = plan.get("taskId")
    starting_hash = _valid_sha(plan.get("startingDocumentHash"),
                               "Parent starting document hash")
    provenance = plan.get("continuationProvenance")
    if not isinstance(provenance, dict):
        bootstrap = plan.get("bindingProvenance")
        if (not isinstance(bootstrap, dict) or
                bootstrap.get("schema") != PORTRAIT_BOOTSTRAP_SCHEMA or
                bootstrap.get("resourceId") != resource_id or
                bootstrap.get("taskId") != task_id or
                bootstrap.get("startingDocumentHash") != starting_hash):
            raise BinderError("Frozen parent plan has no matching native binding provenance.")
        try:
            retarget._resource_uuid(resource_id, "Bootstrap Timeline resource")
        except retarget.RetargetError as error:
            raise BinderError(str(error)) from error
        _canonical_task_uuid(task_id, "Bootstrap native task ID")
        timeline_id = resource_id.split(":", 1)[1]
        if bootstrap.get("createdTimelineId") != timeline_id:
            raise BinderError("Bootstrap provenance names a different native Timeline ID.")
        return resource_id, task_id
    if (provenance.get("resourceId") != resource_id or
            provenance.get("taskId") != task_id or
            provenance.get("startingDocumentHash") != starting_hash):
        raise BinderError("Frozen parent plan identity does not match its continuation provenance.")
    _validate_provenance_node(provenance)
    if provenance.get("resourceId") != resource_id or provenance.get("taskId") != task_id:
        raise BinderError("Frozen parent plan identity differs from its continuation provenance.")
    return resource_id, task_id


def _validate_output_path(path_value, output_directory):
    path = retarget._canonical_existing_path(
        output_directory, "Admitted outputDirectory", regular_file=False)
    if (not isinstance(path_value, str) or not path_value or
            not Path(path_value).is_absolute()):
        raise BinderError("The checkpoint save path is not an absolute canonical path.")
    target = Path(path_value)
    if (target.is_symlink() or Path(os.path.normpath(path_value)) != target or
            target.parent != path or target.suffix.lower() != ".json"):
        raise BinderError("The checkpoint save path is not a new direct .json child of the admitted output directory.")
    resolved = retarget._canonical_existing_path(
        path_value, "Saved source project", regular_file=True)
    if resolved != target:
        raise BinderError("The saved source project path is not canonical.")
    return resolved


def _stream_sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _verify_derived_integrity_refresh(checkpoint_document, ready_document,
                                      asset_paths, expected_change_counts=(119, 36),
                                      allow_ready_checkpoint=False):
    """Allow only the known source-integrity hydration delta after release."""
    allowed_checkpoint_ready = (False, True) if allow_ready_checkpoint else (False,)
    if (not isinstance(checkpoint_document, dict) or not isinstance(ready_document, dict) or
            checkpoint_document.get("ready") not in allowed_checkpoint_ready or
            ready_document.get("ready") is not True or
            checkpoint_document.get("resourceId") != ready_document.get("resourceId")):
        raise BinderError("The ready Timeline proof does not descend from the saved checkpoint resource.")
    if (checkpoint_document.get("width") != ready_document.get("width") or
            checkpoint_document.get("height") != ready_document.get("height") or
            checkpoint_document.get("frameRate") != ready_document.get("frameRate")):
        raise BinderError("Source-integrity hydration changed the saved Timeline geometry.")
    allowed_top = {"ready", "documentHash", "clips", "tracks"}
    if any(not _same(checkpoint_document.get(key), ready_document.get(key))
           for key in set(checkpoint_document) | set(ready_document)
           if key not in allowed_top):
        raise BinderError("The ready Timeline proof contains non-derived document changes.")

    before_clips = checkpoint_document.get("clips")
    after_clips = ready_document.get("clips")
    before_tracks = checkpoint_document.get("tracks")
    after_tracks = ready_document.get("tracks")
    if (not isinstance(before_clips, list) or not isinstance(after_clips, list) or
            not isinstance(before_tracks, list) or not isinstance(after_tracks, list) or
            len(before_clips) != len(after_clips) or len(before_tracks) != len(after_tracks)):
        raise BinderError("The ready Timeline proof changed clip or track counts.")
    for before_track, after_track in zip(before_tracks, after_tracks):
        if (not isinstance(before_track, dict) or not isinstance(after_track, dict) or
                not _same({k: v for k, v in before_track.items() if k != "clips"},
                          {k: v for k, v in after_track.items() if k != "clips"})):
            raise BinderError("Source-integrity hydration changed native track metadata.")
    before_by_id = {item.get("clipId"): item for item in before_clips if isinstance(item, dict)}
    after_by_id = {item.get("clipId"): item for item in after_clips if isinstance(item, dict)}
    if (len(before_by_id) != len(before_clips) or len(after_by_id) != len(after_clips) or
            None in before_by_id or set(before_by_id) != set(after_by_id)):
        raise BinderError("The ready Timeline proof changed stable clip identities.")

    allowed_clip_fields = {"sourceIntegrityStatus", "sourceContentSha256"}
    changed_statuses = 0
    changed_hashes = 0
    canonical_assets = set()
    for value in asset_paths:
        resolved = retarget._canonical_existing_path(value, "Admitted source asset", regular_file=True)
        canonical_assets.add(str(resolved))
    verified_hashes = {}
    for clip_id, before in before_by_id.items():
        after = after_by_id[clip_id]
        if (set(before) != set(after) or any(
                not _same(before.get(key), after.get(key))
                for key in set(before) | set(after) if key not in allowed_clip_fields)):
            raise BinderError(f"Source-integrity hydration changed authored clip state for {clip_id}.")
        before_status = before.get("sourceIntegrityStatus")
        after_status = after.get("sourceIntegrityStatus")
        if before_status != after_status:
            if (before_status, after_status) != ("pending", "verified"):
                raise BinderError(f"Source-integrity status changed unexpectedly for clip {clip_id}.")
            changed_statuses += 1
        before_hash = before.get("sourceContentSha256")
        after_hash = after.get("sourceContentSha256")
        if before_hash != after_hash:
            if (before_hash != "" or not isinstance(after_hash, str) or
                    not SHA256_RE.fullmatch(after_hash)):
                raise BinderError(f"Source content hash changed unexpectedly for clip {clip_id}.")
            path_value = before.get("filePath") or before.get("sourcePath")
            if not isinstance(path_value, str) or not path_value:
                raise BinderError(f"Hydrated clip {clip_id} has no source file path.")
            path = retarget._canonical_existing_path(
                path_value, f"Hydrated source file for clip {clip_id}", regular_file=True)
            if str(path) not in canonical_assets:
                raise BinderError(f"Hydrated source file for clip {clip_id} is outside the admitted asset set.")
            actual = verified_hashes.get(str(path))
            if actual is None:
                actual = _stream_sha256(path)
                verified_hashes[str(path)] = actual
            if actual != after_hash:
                raise BinderError(f"Hydrated content hash does not match source bytes for clip {clip_id}.")
            changed_hashes += 1
    if (expected_change_counts is not None and
            (changed_statuses, changed_hashes) != tuple(expected_change_counts)):
        raise BinderError("The source-integrity hydration delta did not match the captured 119-status/36-hash update.")


def _verify_source_checkpoint(plan, prefix, stop_review, final_review,
                              release_task, source_document, save_result,
                              source_task_id, source_resource_id, plan_hash,
                              journal_history=None, checkpoint_document=None,
                              integrity_change_counts=(119, 36),
                              allow_ready_checkpoint=False):
    source_assets = plan.get("assetPaths")
    if (not isinstance(source_assets, list) or len(source_assets) != 104 or
            any(not isinstance(item, str) or not item for item in source_assets) or
            len(source_assets) != len(set(source_assets))):
        raise BinderError("The source continuation must retain its exact 104 distinct admitted asset paths.")
    if plan.get("resourceId") != source_resource_id or plan.get("taskId") != source_task_id:
        raise BinderError("The frozen parent plan does not identify the source task/resource.")

    stop_status = stop_review.get("status") if isinstance(stop_review, dict) else None
    if stop_status not in {"review", "running"}:
        raise BinderError("The source task lacks a settled pre-save review or running checkpoint proof.")
    _, stop_resource, stop_hash = _validate_task_scope(
        stop_review, stop_status, source_task_id, source_resource_id, plan)
    expected_prefix_rows = _expected_journal_prefix(
        prefix, plan_hash, source_resource_id)
    expected_rows = (journal_history.get("successfulRows")
                     if isinstance(journal_history, dict) else expected_prefix_rows)
    failed_rows = (journal_history.get("failedRows", [])
                   if isinstance(journal_history, dict) else [])
    if stop_status == "review":
        _validate_review_receipt(stop_review, "Stop review receipt", source_task_id, "review")
    if stop_resource.get("pending") is not False:
        raise BinderError("The task was not settled when it crossed the review stop boundary.")

    _, final_review_resource, final_review_hash = _validate_task_scope(
        final_review, "review", source_task_id, source_resource_id, plan)
    _validate_review_receipt(final_review, "Final review receipt", source_task_id, "review")
    _validate_review_receipt(release_task, "Final release receipt", source_task_id, "released")
    _, released_resource, released_hash = _validate_task_scope(
        release_task, "released", source_task_id, source_resource_id, plan)
    if released_hash != stop_hash or final_review_hash != stop_hash:
        raise BinderError("The source Timeline changed between the stop review and save/release checkpoint.")

    if save_result.get("ok") is not True or save_result.get("readbackVerified") is not True:
        raise BinderError("The source project save did not report successful native readback.")
    saved_path = save_result.get("outputPath")
    source_path = _validate_output_path(saved_path, plan.get("outputDirectory"))
    save_hash = _valid_sha(save_result.get("fileSha256"), "Saved project file SHA-256")
    actual_file_hash = _sha256(source_path.read_bytes())
    if actual_file_hash != save_hash:
        raise BinderError("Saved source project bytes differ from the native save receipt.")
    if save_result.get("timelineId") != source_resource_id.split(":", 1)[1]:
        raise BinderError("Saved project timelineId differs from the original Timeline resource.")

    source_resource_id_value = source_document.get("resourceId")
    if (source_document.get("ok") is not True or source_document.get("ready") is not True or
            source_resource_id_value != source_resource_id):
        raise BinderError("Ready source document inspection does not match the task-owned Timeline.")
    checkpoint = checkpoint_document if isinstance(checkpoint_document, dict) else source_document
    if (checkpoint.get("ok") is not True or checkpoint.get("resourceId") != source_resource_id):
        raise BinderError("Saved source checkpoint inspection does not match the task-owned Timeline.")
    source_hash = _valid_sha(checkpoint.get("documentHash"), "Saved source checkpoint hash")
    if (source_hash != released_hash or save_result.get("documentHash") != source_hash or
            source_hash != stop_hash):
        raise BinderError("Saved checkpoint, released task, and stop review hashes are not identical.")
    if checkpoint is not source_document:
        _verify_derived_integrity_refresh(
            checkpoint, source_document, source_assets, integrity_change_counts,
            allow_ready_checkpoint)
    elif source_document.get("ready") is not True:
        raise BinderError("Saved source document is not ready for exact native copy readback.")
    if (checkpoint.get("width") != plan["composition"]["width"] or
            checkpoint.get("height") != plan["composition"]["height"] or
            checkpoint.get("frameRate") != plan["composition"]["fps"] or
            source_document.get("width") != checkpoint.get("width") or
            source_document.get("height") != checkpoint.get("height") or
            source_document.get("frameRate") != checkpoint.get("frameRate")):
        raise BinderError("Saved source checkpoint geometry differs from the frozen plan.")
    checkpoint_track_ids, checkpoint_clip_ids = retarget._inspect_ids(
        checkpoint, "Saved source checkpoint proof")
    ready_track_ids, ready_clip_ids = retarget._inspect_ids(
        source_document, "Ready source document proof")
    if (checkpoint_track_ids != ready_track_ids or checkpoint_clip_ids != ready_clip_ids):
        raise BinderError("Source-integrity hydration changed the saved graph's stable native IDs.")

    _validate_journal_history(stop_review, expected_rows, failed_rows, source_resource_id)
    _validate_journal_history(release_task, expected_rows, failed_rows, source_resource_id,
                              str(source_path), save_result)
    _validate_journal_history(final_review, expected_rows, failed_rows, source_resource_id,
                              str(source_path), save_result)
    output_paths = release_task.get("nativeTimelineOutputPaths")
    if output_paths is None:
        release_scope = release_task.get("nativeTimelineScope")
        output_paths = release_scope.get("outputPaths") if isinstance(release_scope, dict) else None
    if not isinstance(output_paths, list) or str(source_path) not in output_paths:
        raise BinderError("Released task does not journal the exact saved project output path.")
    return {
        "sourcePath": source_path,
        "sourceFileSha256": actual_file_hash,
        "sourceDocumentHash": source_hash,
        "sourceGraphTrackIds": ready_track_ids,
        "sourceGraphClipIds": ready_clip_ids,
        "expectedJournalRows": expected_prefix_rows,
        "expectedJournalHistoryRows": expected_rows,
        "expectedJournalFailureRows": failed_rows,
    }


def _read_path_value(value, path, label):
    current = value
    for key in path:
        if not isinstance(current, dict) or key not in current:
            raise BinderError(f"{label} is missing path component {key!r}.")
        current = current[key]
    return current


def _replace_path_value(value, path, replacement):
    current = value
    for key in path[:-1]:
        current = current[key]
    current[path[-1]] = replacement


def _validate_radius_repaired_plan(parent_plan, corrected_plan, parent_hash,
                                   parent_path):
    correction = corrected_plan.get("nativePillRadiusCorrection")
    if (not isinstance(correction, dict) or
            set(correction) != {"schema", "parentPlanPath", "parentPlanSha256",
                                "completedPrefixPreserved", "corrections",
                                "nativeAuthoringExecuted"} or
            correction.get("schema") != "looksy.native-pill-radius-correction/v1" or
            correction.get("parentPlanSha256") != parent_hash or
            correction.get("completedPrefixPreserved") != 278 or
            correction.get("nativeAuthoringExecuted") is not False):
        raise BinderError("Corrected plan lacks exact unexecuted native pill-radius repair provenance.")
    try:
        recorded_parent = Path(correction.get("parentPlanPath", "")).resolve(strict=True)
        actual_parent = Path(parent_path).resolve(strict=True)
    except (OSError, TypeError) as error:
        raise BinderError("Pill-radius repair provenance has no resolvable parent plan path.") from error
    if recorded_parent != actual_parent:
        raise BinderError("Pill-radius repair provenance names a different parent plan.")
    parent_steps, _ = continuation._validate_plan(parent_plan, "Frozen v14 parent plan")
    corrected_steps, _ = continuation._validate_plan(corrected_plan, "Corrected v15 plan")
    if len(parent_steps) != len(corrected_steps):
        raise BinderError("Corrected plan changed the frozen parent step count.")
    expected = _expected_pill_radius_corrections()
    raw = correction.get("corrections")
    if not isinstance(raw, list) or len(raw) != len(expected):
        raise BinderError("Corrected plan must record exactly six pill-radius field changes.")
    compact = []
    for entry in raw:
        if not isinstance(entry, dict) or not {"stepId", "path", "old", "new"}.issubset(entry):
            raise BinderError("Pill-radius correction entry is malformed.")
        compact.append({key: entry[key] for key in ("stepId", "path", "old", "new")})
    if not _same(compact, expected):
        raise BinderError("Corrected plan pill-radius provenance differs from the six approved fields.")

    if set(corrected_plan) != set(parent_plan) | {"nativePillRadiusCorrection"}:
        raise BinderError("Corrected plan has unexpected top-level changes.")
    if any(not _same(parent_plan[key], corrected_plan[key])
           for key in parent_plan if key != "steps"):
        raise BinderError("Corrected plan changed frozen metadata outside its radius provenance.")
    correction_by_id = {entry["stepId"]: entry for entry in expected}
    parent_index = {step["id"]: index for index, step in enumerate(parent_steps)}
    if any(step_id not in parent_index or parent_index[step_id] < 294
           for step_id in correction_by_id):
        raise BinderError("A pill-radius correction overlaps an already completed or rejected action.")
    for index, (before, after) in enumerate(zip(parent_steps, corrected_steps)):
        entry = correction_by_id.get(before["id"])
        if entry is None:
            if not _same(before, after):
                raise BinderError(f"Corrected plan changed unapproved step {before['id']}.")
            continue
        if before["id"] != after["id"] or before["commandId"] != after["commandId"]:
            raise BinderError("Pill-radius repair changed a native action identity.")
        path = entry["path"]
        if (_read_path_value(before, path, before["id"]) != entry["old"] or
                _read_path_value(after, path, after["id"]) != entry["new"]):
            raise BinderError(f"Pill-radius repair values differ at {before['id']}.")
        restored = copy.deepcopy(after)
        _replace_path_value(restored, path, entry["old"])
        if not _same(before, restored):
            raise BinderError(f"Pill-radius repair changed unrelated values in {before['id']}.")
        if index < 278 and not _same(before, after):
            raise BinderError("Pill-radius repair changed an already completed prefix action.")
    return correction


def _validate_portrait_v9_schema_plan(parent_plan, corrected_plan, parent_hash,
                                      parent_path):
    """Validate only the exact, unexecuted portrait v9 schema repairs."""
    metadata = corrected_plan.get("nativeSchemaRepairProvenance")
    required_fields = {
        "schema", "parentPlanPath", "parentPlanSha256", "changes",
        "oldOutputPath", "newOutputPath", "nativeAuthoringExecuted",
    }
    if (not isinstance(metadata, dict) or set(metadata) != required_fields or
            metadata.get("schema") != "looksy.native-schema-repair/v1" or
            metadata.get("parentPlanSha256") != parent_hash or
            metadata.get("nativeAuthoringExecuted") is not False):
        raise BinderError("Corrected portrait parent lacks exact unexecuted v9 repair provenance.")
    try:
        expected_parent_path = str(Path(parent_path).resolve(strict=True))
        recorded_parent_path = str(Path(metadata.get("parentPlanPath", "")).resolve(strict=True))
    except (OSError, TypeError) as error:
        raise BinderError("Corrected portrait parent source path is unavailable.") from error
    if recorded_parent_path != expected_parent_path:
        raise BinderError("Portrait schema repair provenance names a different v8 parent plan.")

    old_output_path = parent_plan.get("outputPath")
    expected_output_path = str(Path(old_output_path).with_name("looksy-native-full-portrait-v9.json"))
    if (metadata.get("oldOutputPath") != old_output_path or
            metadata.get("newOutputPath") != expected_output_path or
            Path(expected_output_path).exists()):
        raise BinderError("Portrait v9 repair does not use its exact absent native-save path.")

    expected = copy.deepcopy(parent_plan)
    changes = []
    for index in range(3):
        graphic_id = f"film-closing/framing-pill-background-{index}"
        frame_id = f"{graphic_id}-frame"
        for step_id, path in (
                (graphic_id, ["values", "values", "shapeCornerRadius"]),
                (frame_id, ["values", "cornerRadius"])):
            step = next((item for item in expected["steps"] if item.get("id") == step_id), None)
            if step is None:
                raise BinderError(f"Portrait v9 repair cannot find pill step {step_id}.")
            old_value = _read_path_value(step, path, step_id)
            if old_value != 0.8333333333333334:
                raise BinderError(f"Portrait v9 pill radius source value changed at {step_id}.")
            _replace_path_value(step, path, 0.5)
            changes.append({
                "stepId": step_id,
                "path": path,
                "old": old_value,
                "new": 0.5,
                "reason": "CSS pill radius saturates at half the short side.",
            })

    gradient_id = "library-carousel/p-carousel-phone-gradient"
    gradient_index = next((index for index, item in enumerate(expected["steps"])
                           if item.get("id") == gradient_id), None)
    if gradient_index != 2054:
        raise BinderError("Portrait v8 gradient moved from its reviewed lane-repair insertion point.")
    gradient = expected["steps"][gradient_index]
    old_marker = {"__lanePool": "p-carousel-phone-effects", "__laneKind": "video"}
    track_id = "library-carousel/lane-p-carousel-phone-effects-01"
    track_result = {"result": track_id, "field": "trackId"}
    inserted_track = {
        "id": track_id,
        "commandId": "timeline.action.createTimelineTrack",
        "values": {"kind": "video", "name": "P Carousel Phone Effects 01", "audioRole": ""},
    }
    if _read_path_value(gradient, ["values", "trackId"], gradient_id) != old_marker:
        raise BinderError("Portrait v8 gradient no longer contains the exact unresolved lane marker.")
    _replace_path_value(gradient, ["values", "trackId"], track_result)
    expected["steps"].insert(gradient_index, copy.deepcopy(inserted_track))
    changes.append({
        "stepId": gradient_id,
        "path": ["values", "trackId"],
        "old": old_marker,
        "new": track_result,
        "insertedStep": inserted_track,
        "reason": "Resolve the generator lane pool to an actual native-created track.",
    })

    save = next((step for step in expected["steps"]
                 if step.get("id") == "delivery/save-editable-project"), None)
    if not isinstance(save, dict) or save.get("commandId") != SAVE_COMMAND_ID:
        raise BinderError("Portrait v9 repair cannot find the terminal native save step.")
    save["values"]["outputPath"] = expected_output_path
    expected_metadata = {
        "schema": "looksy.native-schema-repair/v1",
        "parentPlanPath": expected_parent_path,
        "parentPlanSha256": parent_hash,
        "changes": changes,
        "oldOutputPath": old_output_path,
        "newOutputPath": expected_output_path,
        "nativeAuthoringExecuted": False,
    }
    expected["nativeSchemaRepairProvenance"] = expected_metadata
    if not _same(metadata, expected_metadata) or not _same(corrected_plan, expected):
        raise BinderError("Portrait v9 plan contains changes outside its seven approved repairs.")
    return expected_metadata


def validate_portrait_v8_scope_blocked_attempt(
    parent_plan_path,
    receipt_path,
    event_log_path,
    review_task_path,
    corrected_plan_path,
):
    """Verify the 1,637-step portrait prefix and exact unjournaled scope rejection."""
    parent_bytes, parent = _read_json(parent_plan_path, "frozen portrait v8 plan")
    receipt_bytes, receipt = _read_json(receipt_path, "portrait v8 authoring receipt")
    corrected_bytes, corrected = _read_json(corrected_plan_path, "corrected portrait v9 parent")
    parent_hash = _sha256(parent_bytes)
    if receipt.get("planSha256") != parent_hash or receipt.get("status") != "incomplete":
        raise BinderError("Portrait v8 runner receipt does not identify the frozen plan.")
    resource_id, task_id = _validate_parent_identity(parent)
    if (parent.get("composition") != {"width": 1080, "height": 1920, "fps": 30} or
            parent.get("resourceId") != resource_id or parent.get("taskId") != task_id):
        raise BinderError("Portrait v8 source plan does not have the admitted native identity and geometry.")
    source_assets = parent.get("assetPaths")
    if (not isinstance(source_assets, list) or len(source_assets) != 104 or
            len(source_assets) != len(set(source_assets)) or
            any(not isinstance(item, str) or not item for item in source_assets)):
        raise BinderError("Portrait v8 task must retain its exact 104-file source allowlist.")

    corrected_hash = _sha256(corrected_bytes)
    if (corrected.get("resourceId") != resource_id or corrected.get("taskId") != task_id or
            corrected.get("startingDocumentHash") != parent.get("startingDocumentHash")):
        raise BinderError("Portrait v9 corrected parent changed the source Timeline task identity.")
    schema_repair = _validate_portrait_v9_schema_plan(
        parent, corrected, parent_hash, parent_plan_path)
    parent_steps, _ = continuation._validate_plan(parent, "Frozen portrait v8 plan")
    corrected_steps, _ = continuation._validate_plan(corrected, "Corrected portrait v9 parent")
    if (len(parent_steps) != 3642 or len(corrected_steps) != 3643 or
            any(not _same(parent_steps[index], corrected_steps[index]) for index in range(1638))):
        raise BinderError("Portrait v9 correction changed the completed or rejected native prefix.")

    events = _event_log(event_log_path, parent_hash, len(parent_steps))
    failure_policy = {
        "kind": "pre_dispatch_scope",
        "error": SCOPE_REJECTION_ERROR,
        "commandId": SCOPE_REJECTION_COMMAND,
        "stepId": "library-carousel/p-lib-62-IMG_4788-image",
        "expectedFailureCount": 1,
    }
    prefix = _validate_completion_prefix(
        parent, receipt, events, parent_hash, failure_policy=failure_policy)
    if prefix["completedCount"] != 1637:
        raise BinderError("Portrait v8 receipt does not prove exactly 1,637 completed operations.")
    failed_step = prefix["failedSteps"][0]
    failed_values = failed_step["values"]
    if failed_values.get("filePath") not in parent.get("assetPaths", []):
        raise BinderError("Scope-rejected portrait asset is not in the sealed 104-file allowlist.")
    try:
        source_path = retarget._canonical_existing_path(
            failed_values["filePath"], "Scope-rejected portrait source asset", regular_file=True)
    except retarget.RetargetError as error:
        raise BinderError(str(error)) from error
    if str(source_path) != failed_values["filePath"]:
        raise BinderError("Scope-rejected portrait source path is not canonical.")

    successful_rows = _expected_journal_prefix(prefix, parent_hash, resource_id)
    if len(successful_rows) != 236:
        raise BinderError("Portrait v8 prefix does not reconstruct as the exact 236 native journal rows.")
    review_task = _proof(review_task_path, "Portrait v8 review task proof")
    _validate_task_scope(review_task, "review", task_id, resource_id, parent)
    _validate_review_receipt(review_task, "Portrait v8 review task proof", task_id, "review")
    resource = next(item for item in review_task["resources"] if item.get("id") == resource_id)
    _validate_journal_history(review_task, successful_rows, [], resource_id)
    if (len(review_task.get("steps", [])) != len(successful_rows) or
            resource.get("pending") is not False or
            review_task.get("uncertainOperation") is True):
        raise BinderError("Native review proof has an uncertain or extra operation beyond the safe prefix.")

    scope = prefix["failedNativeStructured"]
    evidence = {
        "schema": "looksy.native-scope-rejection/v1",
        "parentPlanSha256": parent_hash,
        "parentReceiptSha256": _sha256(receipt_bytes),
        "parentEventLogSha256": _sha256(Path(event_log_path).read_bytes()),
        "correctedPlanPath": str(Path(corrected_plan_path).resolve()),
        "correctedPlanSha256": corrected_hash,
        "reviewTaskProofSha256": _sha256(Path(review_task_path).read_bytes()),
        "sourceResourceId": resource_id,
        "sourceTaskId": task_id,
        "stepId": failed_step["id"],
        "commandId": failed_step["commandId"],
        "sourcePath": str(source_path),
        "error": SCOPE_REJECTION_ERROR,
        "structuredContent": scope,
        "runnerClaims": {"rollback": None, "recoveryRequired": None},
        "nativeJournalRowCount": len(successful_rows),
        "nativeRejectedOperationRowPresent": False,
        "reviewTaskHash": resource["currentHash"],
        "completedPrefixCount": prefix["completedCount"],
        "completedPrefixResultSha256": _sha256(
            _canonical_json(prefix["completedRecords"]).encode("utf-8")),
        "schemaRepair": schema_repair,
    }
    return {
        "prefix": prefix,
        "futurePlan": corrected,
        "journalHistory": {"successfulRows": successful_rows, "failedRows": []},
        "scopeFailureEvidence": evidence,
        "reviewTask": review_task,
    }


def bind_portrait_v8_scope_recovery_suffix(
    parent_plan_path,
    receipt_path,
    event_log_path,
    review_task_path,
    corrected_plan_path,
    final_review_path,
    release_task_path,
    checkpoint_document_path,
    ready_document_path,
    save_result_path,
    import_receipt_path,
    admission_receipt_path,
    imported_document_path,
    native_output_path,
):
    """Bind the v9 portrait suffix only after proving review, copy, and release."""
    validated = validate_portrait_v8_scope_blocked_attempt(
        parent_plan_path, receipt_path, event_log_path, review_task_path,
        corrected_plan_path)
    return bind_reviewed_suffix(
        parent_plan_path, receipt_path, event_log_path,
        review_task_path, final_review_path, release_task_path,
        ready_document_path, save_result_path, import_receipt_path,
        admission_receipt_path, imported_document_path, native_output_path,
        validated_prefix=validated["prefix"],
        future_plan=validated["futurePlan"],
        journal_history=validated["journalHistory"],
        scope_failure_evidence=validated["scopeFailureEvidence"],
        checkpoint_document_path=checkpoint_document_path,
        integrity_change_counts=(79, 79),
        allow_ready_checkpoint=True,
    )


def validate_v14_schema_blocked_attempt(
    current_plan_path,
    current_receipt_path,
    current_event_log_path,
    prior_plan_path,
    prior_receipt_path,
    prior_event_log_path,
    blocked_task_path,
    corrected_plan_path,
    checkpoint_document_path,
):
    """Prove the v12 rollback, v14 completed prefix, and typed-schema rejection."""
    current_plan_bytes, current_plan = _read_json(current_plan_path, "frozen v14 plan")
    corrected_plan_bytes, corrected_plan = _read_json(corrected_plan_path, "corrected v15 plan")
    prior_plan_bytes, prior_plan = _read_json(prior_plan_path, "prior v12 plan")
    prior_receipt_bytes, prior_receipt = _read_json(prior_receipt_path, "prior v12 receipt")
    current_receipt_bytes, current_receipt = _read_json(current_receipt_path, "current v14 receipt")
    current_plan_hash = _sha256(current_plan_bytes)
    corrected_plan_hash = _sha256(corrected_plan_bytes)
    prior_plan_hash = _sha256(prior_plan_bytes)
    prior_event_bytes = Path(prior_event_log_path).read_bytes()
    current_event_bytes = Path(current_event_log_path).read_bytes()
    prior_event_hash = _sha256(prior_event_bytes)
    current_event_hash = _sha256(current_event_bytes)
    prior_events = _event_log(prior_event_log_path, prior_plan_hash, len(prior_plan.get("steps", [])))
    current_events = _event_log(current_event_log_path, current_plan_hash, len(current_plan.get("steps", [])))

    current_provenance = current_plan.get("continuationProvenance")
    if (not isinstance(current_provenance, dict) or
            current_provenance.get("schema") != continuation.CONTINUATION_SCHEMA):
        raise BinderError("The current v14 suffix has no native authoring continuation provenance.")
    _validate_parent_identity(prior_plan)
    resource_id = current_plan.get("resourceId")
    task_id = current_plan.get("taskId")
    if (prior_plan.get("resourceId") != resource_id or prior_plan.get("taskId") != task_id or
            current_provenance.get("resourceId") != resource_id or
            current_provenance.get("taskId") != task_id or
            current_provenance.get("startingDocumentHash") != current_plan.get("startingDocumentHash")):
        raise BinderError("v12 and v14 plans do not target one exact native Timeline task lineage.")
    _valid_sha(current_plan.get("startingDocumentHash"), "v14 starting document hash")
    if (current_provenance.get("parentPlanSha256") != prior_plan_hash or
            current_provenance.get("priorReceiptSha256") != _sha256(prior_receipt_bytes) or
            current_provenance.get("priorEventLogSha256") != prior_event_hash):
        raise BinderError("v14 continuation provenance does not pin the exact v12 plan and runner receipts.")
    if (corrected_plan.get("resourceId") != resource_id or
            corrected_plan.get("taskId") != task_id or
            corrected_plan.get("startingDocumentHash") != current_plan.get("startingDocumentHash")):
        raise BinderError("Corrected v15 plan changed the source Timeline task identity.")
    correction = _validate_radius_repaired_plan(
        current_plan, corrected_plan, current_plan_hash, current_plan_path)

    prior_policy = {
        "kind": "atomic_rollback",
        "error": "Geometry, fit mode, source crop, opacity, or corner radius is outside Timeline limits.",
        "resourceId": resource_id,
        "taskStateHash": current_provenance.get("startingDocumentHash"),
        "taskStepId": 44,
        "runnerRolledBack": True,
        "runnerRecoveryRequired": False,
        "expectedFailureCount": 13,
        "batchId": current_provenance.get("failedBatchId"),
        "batchRequestSha256": current_provenance.get("failedBatchRequestSha256"),
    }
    prior_prefix = _validate_completion_prefix(
        prior_plan, prior_receipt, prior_events, prior_plan_hash,
        failure_policy=prior_policy)
    if (prior_prefix["completedCount"] != 297 or
            current_provenance.get("completedPrefixCount") != 297):
        raise BinderError("The crop-repair continuation does not follow exactly 297 prior completed actions.")
    if ([step["id"] for step in prior_prefix["failedSteps"]] !=
            current_provenance.get("failedStepIds") or
            prior_prefix["failedBatchId"] != current_provenance.get("failedBatchId") or
            current_provenance.get("nativeTaskStepId") != 44 or
            current_provenance.get("nativeRollbackVerified") is not True):
        raise BinderError("v14 provenance does not match the exact v12 atomic rollback receipt.")
    prior_success_rows = _expected_journal_prefix(
        prior_prefix, prior_plan_hash, resource_id)
    if len(prior_success_rows) != 44:
        raise BinderError("The v12 completed prefix does not map to the expected 44 native journal rows.")

    current_failure = current_receipt.get("failure")
    if not isinstance(current_failure, dict):
        raise BinderError("The v14 authoring receipt has no exact schema-rejection summary.")
    native_failure = _native_error_structured(current_failure.get("error"), "v14 typed-schema rejection")
    task_state_hash = native_failure.get("taskStateHash")
    task_step_id = native_failure.get("taskStepId")
    current_policy = {
        "kind": "pre_dispatch_schema",
        "error": SCHEMA_TYPED_REJECTION,
        "resourceId": resource_id,
        "taskStateHash": task_state_hash,
        "taskStepId": 76,
        "runnerRolledBack": False,
        "runnerRecoveryRequired": False,
        "expectedFailureCount": 16,
        "batchId": current_failure.get("batchId"),
    }
    if task_step_id != 76:
        raise BinderError("The typed-schema rejection does not identify native journal step 76.")
    current_prefix = _validate_completion_prefix(
        current_plan, current_receipt, current_events, current_plan_hash,
        failure_policy=current_policy)
    if current_prefix["completedCount"] != 278:
        raise BinderError("The v14 run does not prove exactly 278 completed suffix actions.")
    current_success_rows = _expected_journal_prefix(
        current_prefix, current_plan_hash, resource_id)
    if len(current_success_rows) != 31:
        raise BinderError("The v14 completed prefix does not map to the expected 31 native journal rows.")

    blocked_task = _proof(blocked_task_path, "Native v14 blocked-task inspection")
    _validate_task_scope(blocked_task, "running", task_id, resource_id, current_plan)
    current_resource = next(item for item in blocked_task["resources"] if item["id"] == resource_id)
    if current_resource.get("currentHash") != task_state_hash:
        raise BinderError("Native blocked-task hash differs from the exact failed batch state hash.")
    if current_receipt.get("failure", {}).get("batchId") != current_prefix["failedBatchId"]:
        raise BinderError("v14 runner summary and deterministic failed batch ID disagree.")
    native_steps = blocked_task.get("steps")
    if not isinstance(native_steps, list) or len(native_steps) != 77:
        raise BinderError("Blocked task journal must contain exactly 77 rows before the checkpoint save.")
    current_journal_failure = native_steps[76]
    if (not isinstance(current_journal_failure, dict) or
            not isinstance(current_journal_failure.get("arguments"), dict)):
        raise BinderError("Native journal lacks the exact current rejected-batch request arguments.")
    request_sha = current_journal_failure["arguments"].get("_batchRequestSha256")
    _valid_sha(request_sha, "Current native rejected-batch request hash")
    current_policy["batchRequestSha256"] = request_sha
    current_failure_entry = _failure_journal_descriptor(
        current_prefix, current_policy, len(prior_success_rows) + 1 + len(current_success_rows))
    prior_failure_entry = _failure_journal_descriptor(
        prior_prefix, prior_policy, len(prior_success_rows))
    successful_rows = prior_success_rows + current_success_rows
    failed_rows = [prior_failure_entry, current_failure_entry]
    _validate_journal_history(blocked_task, successful_rows, failed_rows, resource_id)

    radius_evidence = {
        key: correction[key]
        for key in ("schema", "parentPlanPath", "parentPlanSha256", "completedPrefixPreserved",
                    "corrections", "nativeAuthoringExecuted")
    }
    evidence = {
        "schema": SCHEMA_UPGRADE_SCHEMA,
        "priorPlanSha256": prior_plan_hash,
        "priorReceiptSha256": _sha256(prior_receipt_bytes),
        "priorEventLogSha256": prior_event_hash,
        "currentPlanSha256": current_plan_hash,
        "currentReceiptSha256": _sha256(current_receipt_bytes),
        "currentEventLogSha256": current_event_hash,
        "correctedPlanSha256": corrected_plan_hash,
        "priorFailure": {
            "journalStepId": 44,
            "batchId": prior_prefix["failedBatchId"],
            "requestSha256": prior_policy["batchRequestSha256"],
            "taskStateHash": prior_policy["taskStateHash"],
            "rolledBack": True,
            "mutationNotApplied": True,
            "changed": False,
        },
        "currentFailure": {
            "journalStepId": 76,
            "batchId": current_prefix["failedBatchId"],
            "requestSha256": request_sha,
            "taskStateHash": task_state_hash,
            "mutationNotApplied": True,
            "changed": False,
        },
        "radiusRepair": radius_evidence,
        "journalCompletedRowCount": len(successful_rows),
        "journalFailedStepIds": [44, 76],
        "blockedTaskProofSha256": _sha256(Path(blocked_task_path).read_bytes()),
        "checkpointDocumentProofSha256": _sha256(Path(checkpoint_document_path).read_bytes()),
    }
    future_plan = copy.deepcopy(current_plan)
    future_plan["steps"] = copy.deepcopy(corrected_plan["steps"])
    future_plan["nativePillRadiusCorrection"] = copy.deepcopy(correction)
    return {
        "prefix": current_prefix,
        "futurePlan": future_plan,
        "journalHistory": {"successfulRows": successful_rows, "failedRows": failed_rows},
        "upgradeEvidence": evidence,
        "priorPrefix": prior_prefix,
        "priorJournalRows": len(prior_success_rows),
        "currentJournalRows": len(current_success_rows),
    }


def bind_v14_schema_upgrade_suffix(
    current_plan_path,
    current_receipt_path,
    current_event_log_path,
    prior_plan_path,
    prior_receipt_path,
    prior_event_log_path,
    blocked_task_path,
    corrected_plan_path,
    checkpoint_document_path,
    stop_review_path,
    final_review_path,
    release_task_path,
    ready_document_path,
    save_result_path,
    import_receipt_path,
    admission_receipt_path,
    imported_document_path,
    native_output_path,
):
    """Bind the unexecuted v14 suffix after the exact schema rejection is fixed."""
    validated = validate_v14_schema_blocked_attempt(
        current_plan_path, current_receipt_path, current_event_log_path,
        prior_plan_path, prior_receipt_path, prior_event_log_path,
        blocked_task_path, corrected_plan_path, checkpoint_document_path)
    return bind_reviewed_suffix(
        current_plan_path,
        current_receipt_path,
        current_event_log_path,
        stop_review_path,
        final_review_path,
        release_task_path,
        ready_document_path,
        save_result_path,
        import_receipt_path,
        admission_receipt_path,
        imported_document_path,
        native_output_path,
        validated_prefix=validated["prefix"],
        future_plan=validated["futurePlan"],
        journal_history=validated["journalHistory"],
        upgrade_evidence=validated["upgradeEvidence"],
        checkpoint_document_path=checkpoint_document_path,
    )


def bind_reviewed_suffix(
    plan_path,
    receipt_path,
    event_log_path,
    stop_review_path,
    final_review_path,
    release_task_path,
    source_document_path,
    save_result_path,
    import_receipt_path,
    admission_receipt_path,
    imported_document_path,
    native_output_path,
    *,
    validated_prefix=None,
    future_plan=None,
    journal_history=None,
    upgrade_evidence=None,
    scope_failure_evidence=None,
    checkpoint_document_path=None,
    integrity_change_counts=(119, 36),
    allow_ready_checkpoint=False,
):
    plan_bytes, plan = _read_json(plan_path, "frozen parent plan")
    receipt_bytes, receipt = _read_json(receipt_path, "authoring receipt")
    stop_bytes, stop_envelope = _read_json(stop_review_path, "stop review proof")
    final_review_bytes, final_review_envelope = _read_json(final_review_path, "final review proof")
    release_bytes, release_envelope = _read_json(release_task_path, "released task proof")
    source_document_bytes, source_document_envelope = _read_json(
        source_document_path, "saved source document proof")
    checkpoint_document_bytes = None
    checkpoint_document_envelope = None
    if checkpoint_document_path is not None:
        checkpoint_document_bytes, checkpoint_document_envelope = _read_json(
            checkpoint_document_path, "saved checkpoint document proof")
    save_bytes, save_envelope = _read_json(save_result_path, "source save proof")
    import_bytes, import_envelope = _read_json(import_receipt_path, "native import proof")
    admission_bytes, admission_envelope = _read_json(admission_receipt_path, "fresh task admission proof")
    imported_document_bytes, imported_document_envelope = _read_json(
        imported_document_path, "imported document proof")

    plan_hash = _sha256(plan_bytes)
    if receipt.get("planSha256") != plan_hash:
        raise BinderError("Runner receipt plan hash differs from the immutable parent plan.")
    steps, positions = continuation._validate_plan(plan, "Parent plan")
    frozen_resource_id, frozen_task_id = _validate_parent_identity(plan)
    for key in ("resourceId", "taskId", "startingDocumentHash"):
        if not isinstance(plan.get(key), str) or not plan.get(key):
            raise BinderError(f"Frozen parent plan is missing {key}.")
    source_resource_id = plan["resourceId"]
    source_task_id = plan["taskId"]
    _valid_sha(plan["startingDocumentHash"], "Parent starting document hash")
    events = _event_log(event_log_path, plan_hash, len(steps))
    prefix = (validated_prefix if isinstance(validated_prefix, dict) else
              _validate_completion_prefix(plan, receipt, events, plan_hash))
    suffix_plan = future_plan if isinstance(future_plan, dict) else plan
    suffix_steps, suffix_positions = continuation._validate_plan(suffix_plan, "Continuation source plan")
    if scope_failure_evidence is not None:
        old_ids = [step["id"] for step in steps]
        new_ids = [step["id"] for step in suffix_steps]
        if (len(new_ids) != len(old_ids) + 1 or
                new_ids[:prefix["completedCount"] + 1] != old_ids[:prefix["completedCount"] + 1] or
                new_ids[:2054] != old_ids[:2054] or
                new_ids[2054] != "library-carousel/lane-p-carousel-phone-effects-01" or
                new_ids[2055:] != old_ids[2054:]):
            raise BinderError("Portrait scope recovery changed steps outside the one reviewed lane insertion.")
    elif [step["id"] for step in suffix_steps] != [step["id"] for step in steps]:
        raise BinderError("Continuation source plan changed the frozen step identity sequence.")
    if any(not _same(steps[index], suffix_steps[index])
           for index in range(prefix["completedCount"])):
        raise BinderError("Continuation source plan changed an already completed plan action.")
    if scope_failure_evidence is not None and not _same(
            steps[prefix["completedCount"]], suffix_steps[prefix["completedCount"]]):
        raise BinderError("Portrait schema repairs changed the exact scope-rejected intent.")

    stop_review = _structured(stop_envelope, "Stop review proof")
    final_review = _structured(final_review_envelope, "Final review proof")
    release_task = _structured(release_envelope, "Released task proof")
    source_document = _structured(source_document_envelope, "Saved source document proof")
    checkpoint_document = (
        _structured(checkpoint_document_envelope, "Saved checkpoint document proof")
        if checkpoint_document_envelope is not None else None)
    save_result = _structured(save_envelope, "Source save proof")
    imported = _structured(import_envelope, "Native import proof")
    admission = _structured(admission_envelope, "Fresh task admission proof")
    imported_document = _structured(imported_document_envelope, "Imported document proof")

    source = _verify_source_checkpoint(
        plan, prefix, stop_review, final_review, release_task, source_document,
        save_result, source_task_id, source_resource_id, plan_hash,
        journal_history=journal_history, checkpoint_document=checkpoint_document,
        integrity_change_counts=integrity_change_counts,
        allow_ready_checkpoint=allow_ready_checkpoint)
    source_project_sha = source["sourceFileSha256"]
    imported_result = retarget._verify_import(
        imported, source_document, source_project_sha)
    if imported_result["sourceProjectResourceId"] != source_resource_id:
        raise BinderError("The imported project did not come from the reviewed source Timeline.")
    resource_id = imported_result["resourceId"]
    if resource_id == source_resource_id:
        raise BinderError("The imported Timeline must have a fresh resource ID.")
    task_id, document_hash, output_directory, scoped_assets = retarget._verify_admission(
        admission, imported_document, plan, resource_id)
    if task_id == source_task_id:
        raise BinderError("The imported copy must have a fresh task ID.")
    if not isinstance(admission.get("steps"), list) or admission.get("steps"):
        raise BinderError("Fresh imported task admission must have an empty native journal.")
    admission_scope = admission.get("nativeTimelineScope")
    if (isinstance(admission_scope, dict) and
            admission_scope.get("outputPaths") not in (None, [])):
        raise BinderError("Fresh imported task admission already contains task-owned output paths.")
    if imported_document.get("savedDocumentPath") != imported_result["savedDocumentPath"]:
        raise BinderError("Imported document inspection disagrees with the native import receipt path.")
    current_track_ids, current_clip_ids = retarget._inspect_ids(
        imported_document, "Imported document proof")
    if (current_track_ids != imported_result["trackIds"] or
            current_clip_ids != imported_result["clipIds"]):
        raise BinderError("Imported document IDs differ from the exact source project graph.")
    if (current_track_ids != source["sourceGraphTrackIds"] or
            current_clip_ids != source["sourceGraphClipIds"]):
        raise BinderError("Project copy changed the reviewed graph's stable native IDs.")

    remaining = []
    for index in range(prefix["completedCount"], len(suffix_steps)):
        step = dict(suffix_steps[index])
        try:
            step["values"] = continuation._rewrite_remaining_refs(
                step["values"], prefix["completedResults"], suffix_positions, index,
                f"Continuation step {step['id']}")
        except continuation.ContinuationError as error:
            raise BinderError(str(error)) from error
        remaining.append(step)
    if not remaining:
        raise BinderError("There are no remaining parent-plan actions to bind.")
    remaining_ids = {step["id"]: index for index, step in enumerate(remaining)}
    for index, step in enumerate(remaining):
        _validate_suffix_refs(step["values"], remaining_ids, index, current_clip_ids, current_track_ids)
    try:
        continuation._validate_admitted_file_paths(remaining, set(scoped_assets))
    except continuation.ContinuationError as error:
        raise BinderError(str(error)) from error

    output_path = retarget._canonical_new_output(native_output_path, str(output_directory))
    if output_path == source["sourcePath"]:
        raise BinderError("The continuation save target cannot overwrite the reviewed source checkpoint.")
    if output_path == Path(plan.get("outputPath", "")):
        raise BinderError("The continuation save target must be a fresh output separate from the parent plan.")

    result = dict(suffix_plan)
    result["steps"] = remaining
    result["resourceId"] = resource_id
    result["taskId"] = task_id
    result["startingDocumentHash"] = document_hash
    result["outputPath"] = str(output_path)
    result["steps"][-1] = dict(result["steps"][-1])
    result["steps"][-1]["values"] = dict(result["steps"][-1]["values"])
    if result["steps"][-1].get("commandId") != SAVE_COMMAND_ID:
        raise BinderError("Remaining suffix no longer ends with the frozen native save action.")
    result["steps"][-1]["values"]["outputPath"] = str(output_path)
    result["proofStatus"] = (
        "Completed prefix verified against the released native journal and exact copied graph; "
        "the bound suffix is unexecuted and visual parity remains unreviewed.")

    source_event_bytes = Path(event_log_path).read_bytes()
    source_scope, source_resource, source_hash = _validate_task_scope(
        release_task, "released", source_task_id, source_resource_id, plan)
    upgraded = isinstance(upgrade_evidence, dict)
    scope_recovered = isinstance(scope_failure_evidence, dict)
    if upgraded and scope_recovered:
        raise BinderError("A continuation cannot combine schema-upgrade and scope-rejection provenance.")
    generated_provenance = {
        "schema": BINDER_SCHEMA,
        "parentPlanSha256": plan_hash,
        "parentReceiptSha256": _sha256(receipt_bytes),
        "parentEventLogSha256": _sha256(source_event_bytes),
        "stopReviewProofSha256": _sha256(stop_bytes),
        "finalReviewProofSha256": _sha256(final_review_bytes),
        "releasedSourceTaskProofSha256": _sha256(release_bytes),
        "sourceDocumentProofSha256": _sha256(source_document_bytes),
        "sourceSaveProofSha256": _sha256(save_bytes),
        "sourceProjectSha256": source_project_sha,
        "sourceGraphSha256": imported_result["sourceGraphSha256"],
        "copyGraphSha256": imported_result["copyGraphSha256"],
        "importReceiptSha256": _sha256(import_bytes),
        "freshAdmissionProofSha256": _sha256(admission_bytes),
        "importedDocumentProofSha256": _sha256(imported_document_bytes),
        "completedPrefixCount": prefix["completedCount"],
        "lastCompletedStepId": prefix["completedRecords"][-1]["id"],
        "completedPrefixResultSha256": _sha256(_canonical_json(
            prefix["completedRecords"]).encode("utf-8")),
        "rejectedStepIds": [step["id"] for step in prefix["failedSteps"]],
        "rejectedBatchId": prefix["failedBatchId"],
        "rejectedGuard": SCHEMA_TYPED_REJECTION if upgraded else EXPECTED_GUARD_ERROR,
        "runnerClaimedRollback": False,
        "runnerClaimedRecoveryRequired": False if upgraded else True,
        "nativePreDispatchRejectionVerified": True,
        "nativeTaskJournalHasNoRejectedBatchStep": False if upgraded else True,
        "sourceResourceId": source_resource_id,
        "sourceTaskId": source_task_id,
        "sourceDocumentHash": source_hash,
        "sourceSavedDocumentPath": str(source["sourcePath"]),
        "sourceFileSha256": source_project_sha,
        "sourceTrackIdSetSha256": _sha256(_canonical_json(
            sorted(source["sourceGraphTrackIds"])).encode("utf-8")),
        "sourceClipIdSetSha256": _sha256(_canonical_json(
            sorted(source["sourceGraphClipIds"])).encode("utf-8")),
        "resourceId": resource_id,
        "taskId": task_id,
        "startingDocumentHash": document_hash,
        "journalCheckpointHash": document_hash,
        "outputDirectory": str(output_directory),
        "outputPath": str(output_path),
        "clipCount": len(current_clip_ids),
        "trackCount": len(current_track_ids),
        "retargetedFields": [
            "steps[completed-prefix:]",
            "resourceId",
            "taskId",
            "startingDocumentHash",
            "outputPath",
            "steps[delivery/save-editable-project].values.outputPath",
            "continuationProvenance",
            "proofStatus",
        ],
    }
    if upgraded:
        generated_provenance["schemaUpgradeEvidence"] = json.loads(
            _canonical_json(upgrade_evidence))
    if scope_recovered:
        source_binding = plan.get("bindingProvenance")
        if not isinstance(source_binding, dict):
            raise BinderError("Portrait scope recovery requires the frozen bootstrap binding provenance.")
        generated_provenance["schema"] = SCOPE_REVIEW_SCHEMA
        generated_provenance["rejectedGuard"] = SCOPE_REJECTION_ERROR
        for key in ("rejectedBatchId", "runnerClaimedRollback",
                    "runnerClaimedRecoveryRequired", "nativeTaskJournalHasNoRejectedBatchStep"):
            generated_provenance.pop(key, None)
        generated_provenance["runnerFailureClaims"] = {
            "rollback": None,
            "recoveryRequired": None,
        }
        generated_provenance["nativeJournalRejectedOperationPresent"] = False
        generated_provenance["scopeFailureEvidence"] = json.loads(
            _canonical_json(scope_failure_evidence))
        generated_provenance["sourceBindingProvenance"] = json.loads(
            _canonical_json(source_binding))
        generated_provenance["sourceBindingProvenanceSha256"] = _sha256(
            _canonical_json(source_binding).encode("utf-8"))
        result["continuationProvenance"] = generated_provenance
    else:
        result["continuationProvenance"] = _chain_parent_provenance(
            plan.get("continuationProvenance"), generated_provenance)
    return result


def _validate_suffix_refs(value, positions, current_index, clip_ids, track_ids, label="suffix action"):
    if isinstance(value, dict):
        if "result" in value or "field" in value:
            if set(value) != {"result", "field"}:
                raise BinderError(f"{label} contains a malformed stable-result reference.")
            result_id, field = value["result"], value["field"]
            if (not isinstance(result_id, str) or not isinstance(field, str) or
                    result_id not in positions or positions[result_id] >= current_index):
                raise BinderError(f"{label} references a removed, missing, self, or future action.")
            return
        for key, item in value.items():
            if key == "clipId" and not (isinstance(item, dict) and "result" in item):
                if not isinstance(item, str) or item not in clip_ids:
                    raise BinderError(f"{label} has a literal clipId absent from the copied project.")
            if key == "trackId" and not (isinstance(item, dict) and "result" in item):
                if not isinstance(item, str) or item not in track_ids:
                    raise BinderError(f"{label} has a literal trackId absent from the copied project.")
            if key == "clipIds":
                if not isinstance(item, list) or any(value not in clip_ids for value in item):
                    raise BinderError(f"{label} has a clipIds list absent from the copied project.")
            if key == "trackIds":
                if not isinstance(item, list) or any(value not in track_ids for value in item):
                    raise BinderError(f"{label} has a trackIds list absent from the copied project.")
            _validate_suffix_refs(item, positions, current_index, clip_ids, track_ids, label)
    elif isinstance(value, list):
        for item in value:
            _validate_suffix_refs(item, positions, current_index, clip_ids, track_ids, label)


def serialize_plan(plan):
    return (json.dumps(plan, indent=2, ensure_ascii=False, allow_nan=False) + "\n").encode("utf-8")


def write_new_plan(plan, output_path, input_paths=()):
    output = Path(output_path)
    directory = retarget._canonical_existing_path(
        str(output.parent), "Continuation plan output directory", regular_file=False)
    if (output.is_symlink() or Path(os.path.normpath(str(output))) != directory / output.name):
        raise BinderError("Continuation plan output path is not canonical.")
    for input_path in input_paths:
        if output.resolve(strict=False) == Path(input_path).resolve(strict=False):
            raise BinderError("Continuation plan output cannot replace an input proof, project, or plan.")
    if output.exists():
        raise BinderError("Continuation plan output already exists; refusing to overwrite it.")
    payload = serialize_plan(plan)
    temporary = output.with_name(output.name + "." + str(uuid.uuid4()) + ".tmp")
    try:
        descriptor = os.open(temporary, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        with os.fdopen(descriptor, "wb") as target:
            target.write(payload)
            target.flush()
            os.fsync(target.fileno())
        os.link(temporary, output)
        temporary.unlink()
        directory_fd = os.open(directory, os.O_RDONLY)
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


def main(argv=None):
    evidence = HERE / "evidence"
    projects = HERE / "projects"
    defaults = {
        "plan_path": HERE / "full-landscape-v10-continuation.commands.json",
        "receipt_path": evidence / "native-full-landscape-v10-receipt.json",
        "event_log_path": evidence / "native-full-landscape-v10-receipt.json.ndjson",
        "stop_review_path": evidence / "native-v10-upgrade-task-review-result.json",
        "final_review_path": evidence / "native-v10-upgrade-task-saved-review-result.json",
        "release_task_path": evidence / "native-v10-upgrade-release-result.json",
        "source_document_path": evidence / "native-v10-upgrade-document-result.json",
        "save_result_path": evidence / "native-v10-upgrade-checkpoint-save-result.json",
        "import_receipt_path": evidence / "native-v11-copy-result.json",
        "admission_receipt_path": evidence / "native-v11-admit-result.json",
        "imported_document_path": evidence / "native-v11-document-result.json",
        "native_output_path": projects / "looksy-native-full-landscape-v11.json",
        "plan_output_path": HERE / "full-landscape-v11-reviewed-suffix.commands.json",
    }
    parser = argparse.ArgumentParser(description=__doc__)
    for name, default in defaults.items():
        parser.add_argument("--" + name.replace("_", "-"), type=Path, default=default)
    args = parser.parse_args(argv)
    try:
        plan = bind_reviewed_suffix(
            args.plan_path, args.receipt_path, args.event_log_path,
            args.stop_review_path, args.final_review_path, args.release_task_path,
            args.source_document_path, args.save_result_path,
            args.import_receipt_path, args.admission_receipt_path,
            args.imported_document_path, args.native_output_path)
        output = write_new_plan(plan, args.plan_output_path, input_paths=tuple(
            getattr(args, name) for name in defaults if name != "plan_output_path"))
    except (BinderError, retarget.RetargetError, continuation.ContinuationError, OSError) as error:
        print(f"reviewed continuation refused: {error}", file=sys.stderr)
        return 2
    provenance = plan["continuationProvenance"]
    print(json.dumps({
        "ok": True,
        "output": str(output),
        "remainingSteps": len(plan["steps"]),
        "completedPrefixCount": provenance["completedPrefixCount"],
        "firstRemainingStepId": plan["steps"][0]["id"],
        "resourceId": plan["resourceId"],
        "taskId": plan["taskId"],
        "startingDocumentHash": plan["startingDocumentHash"],
        "proofStatus": plan["proofStatus"],
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
