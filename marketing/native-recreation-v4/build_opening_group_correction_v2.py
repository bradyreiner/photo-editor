"""Build scoped author_native plans for the prepared desktop-group fragments.

This offline builder never opens an MCP session.  It emits one synchronous
native authoring plan at a time; the compound producer remains a separate
asynchronous task-owned MCP operation described in
opening_shared_group_execution_v2.md.
"""

import argparse
import hashlib
import json
import os
import re
import uuid
from pathlib import Path


ROOT = Path(__file__).resolve().parent
FRAGMENTS = {
    "landscape": ROOT / "opening_shared_group_correction.v2.fragment.json",
    "portrait": ROOT / "opening_shared_group_correction.portrait.v2.fragment.json",
}
SAFE_PHASE1_COMMANDS = {
    "timeline.action.clearTimelineClipMask",
    "timeline.action.setTimelineClipAnimation",
}
UUID_RE = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$")
HASH_RE = re.compile(r"^[0-9a-f]{64}$")
RESOURCE_RE = re.compile(r"^timeline:([0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12})$")


def sha256_bytes(value):
    return hashlib.sha256(value).hexdigest()


def sha256_file(path):
    return sha256_bytes(path.read_bytes())


def _require_uuid(value, label):
    if not isinstance(value, str) or not UUID_RE.fullmatch(value):
        raise ValueError(f"{label} must be a canonical lowercase UUID.")


def _require_scope(resource_id, task_id, document_hash):
    if not isinstance(resource_id, str) or not RESOURCE_RE.fullmatch(resource_id):
        raise ValueError("resourceId must be timeline:<canonical-lowercase-UUID>.")
    _require_uuid(task_id, "taskId")
    if not isinstance(document_hash, str) or not HASH_RE.fullmatch(document_hash):
        raise ValueError("startingDocumentHash must be an exact lowercase SHA-256.")


def _load_fragment(orientation):
    path = FRAGMENTS[orientation]
    fragment_bytes = path.read_bytes()
    fragment = json.loads(fragment_bytes)
    if fragment.get("schema") != "looksy.native-opening-shared-group-correction/v2":
        raise ValueError(f"{path.name} is not a v2 shared-group fragment.")
    if fragment.get("status") != "prepared_unexecuted" or fragment.get("executionReceipt") is not None:
        raise ValueError("Only an unexecuted prepared fragment can be compiled.")
    expected = (1920, 1080, 30) if orientation == "landscape" else (1080, 1920, 30)
    scope = fragment.get("scope", {})
    if (scope.get("width"), scope.get("height"), scope.get("fps")) != expected:
        raise ValueError("Fragment composition does not match the selected orientation.")

    children = fragment.get("compositionTruth", {}).get("sixNativeLayers", [])
    child_ids = [child.get("clipId") for child in children]
    if len(children) != 6 or len(set(child_ids)) != 6:
        raise ValueError("The fragment must name exactly six unique native child clip IDs.")
    for child_id in child_ids:
        _require_uuid(child_id, "child clipId")

    phase = fragment.get("phase1_precompound_batch", {})
    batch = phase.get("values", {})
    operations = batch.get("operations")
    if (phase.get("commandId") != "timeline.action.applyDraftBatch" or
            not isinstance(batch.get("batchId"), str) or not UUID_RE.fullmatch(batch["batchId"]) or
            not isinstance(operations, list) or len(operations) != 12):
        raise ValueError("Phase 1 must be a 12-operation native draft batch with a canonical batchId.")

    seen_clear = set()
    seen_opacity = set()
    for index, operation in enumerate(operations):
        if set(operation) != {"id", "commandId", "values"}:
            raise ValueError("Native batch operations may contain only id, commandId, and values.")
        expected_op_id = f"op{index:03d}"
        if operation["id"] != expected_op_id or operation["commandId"] not in SAFE_PHASE1_COMMANDS:
            raise ValueError("Phase 1 operation order or command allowlist is invalid.")
        values = operation["values"]
        if operation["commandId"] == "timeline.action.clearTimelineClipMask":
            if set(values) != {"clipId"} or values["clipId"] in seen_clear:
                raise ValueError("clearTimelineClipMask must carry one unique clipId only.")
            seen_clear.add(values["clipId"])
        else:
            if set(values) != {"clipId", "channels"} or values["clipId"] in seen_opacity:
                raise ValueError("Opacity reset must carry clipId and opacity channel only.")
            channels = values["channels"]
            if (not isinstance(channels, list) or len(channels) != 1 or
                    set(channels[0]) != {"parameter", "keyframes"} or
                    channels[0]["parameter"] != "opacity"):
                raise ValueError("Child animation edits must replace only the opacity channel.")
            keyframes = channels[0]["keyframes"]
            if (not isinstance(keyframes, list) or len(keyframes) != 1 or
                    set(keyframes[0]) != {"localSeconds", "value", "interpolation"} or
                    keyframes[0]["localSeconds"] != 0.0 or keyframes[0]["interpolation"] != "hold"):
                raise ValueError("Each child opacity reset must be one held value at local time zero.")
            if not isinstance(keyframes[0]["value"], (int, float)) or not 0.0 <= keyframes[0]["value"] <= 1.0:
                raise ValueError("Child opacity must be finite and within 0..1.")
            seen_opacity.add(values["clipId"])
    if seen_clear != set(child_ids) or seen_opacity != set(child_ids):
        raise ValueError("Phase 1 must clear the mask and set opacity once for every fragment child.")
    if any("purpose" in op for op in operations):
        raise ValueError("Descriptive annotations must stay outside native operation payloads.")

    compound = fragment.get("phase2_compound_create", {}).get("values", {})
    _require_uuid(compound.get("requestId"), "compound requestId")
    _require_uuid(compound.get("childTimelineId"), "compound childTimelineId")
    if compound.get("clipIds") != child_ids:
        raise ValueError("Compound request clipIds must match the six fragment children in order.")
    return path, fragment, fragment_bytes


def _composition(fragment):
    scope = fragment["scope"]
    return {"width": scope["width"], "height": scope["height"], "fps": scope["fps"]}


def _step_id(orientation, action):
    return f"opening-group/{orientation}/{action}"


def _build_phase1_steps(orientation, fragment):
    operations = fragment["phase1_precompound_batch"]["values"]["operations"]
    steps = []
    for operation in operations:
        clip_id = operation["values"]["clipId"]
        phase = "clear-mask" if operation["commandId"].endswith("clearTimelineClipMask") else "reset-opacity"
        steps.append({
            "id": _step_id(orientation, f"{phase}/{clip_id}"),
            "commandId": operation["commandId"],
            "values": operation["values"],
        })
    return steps


def _common_plan(orientation, fragment_path, fragment_bytes,
                 resource_id, task_id, starting_hash, phase, steps):
    fragment = json.loads(fragment_bytes)
    return {
        "reference": f"{orientation}.mp4",
        "durationSeconds": fragment["scope"]["chapterDurationSeconds"],
        "composition": _composition(fragment),
        "proofStatus": "Offline plan prepared; native execution and visual parity are unverified.",
        "sourcePlans": [{
            "plan": fragment_path.name,
            "sha256": sha256_bytes(fragment_bytes),
            "phase": phase,
            "steps": len(steps),
        }],
        "assetPaths": [],
        "unresolvedParity": [],
        "resourceId": resource_id,
        "taskId": task_id,
        "startingDocumentHash": starting_hash,
        "openingGroupAuthoring": {
            "schema": "looksy.opening-group-authoring-plan/v1",
            "orientation": orientation,
            "phase": phase,
            "fragmentPath": fragment_path.name,
            "fragmentSha256": sha256_bytes(fragment_bytes),
        },
        "steps": steps,
    }


def _read_json(path, label):
    try:
        return json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"Cannot read {label}: {error}") from error


def _extract_tool_result(value):
    """Unwrap a saved MCP result envelope without making any MCP call."""
    candidate = value
    for _ in range(8):
        if not isinstance(candidate, dict):
            return candidate
        structured = candidate.get("structuredContent")
        if isinstance(structured, dict):
            candidate = structured
            continue
        result = candidate.get("result")
        if isinstance(result, dict) and isinstance(result.get("content"), list):
            candidate = result
            continue
        content = candidate.get("content")
        if isinstance(content, list):
            texts = [item.get("text") for item in content if isinstance(item, dict) and isinstance(item.get("text"), str)]
            if not texts:
                break
            try:
                candidate = json.loads(texts[0])
                continue
            except json.JSONDecodeError:
                break
        if isinstance(result, dict) and "ok" in result:
            candidate = result
            continue
        return candidate
    return candidate


def _validate_precompound_receipt(plan_path, receipt_path,
                                  orientation, fragment_path, fragment_bytes,
                                  resource_id, task_id):
    plan = _read_json(plan_path, "phase 1 plan")
    receipt = _read_json(receipt_path, "phase 1 author_native receipt")
    if (plan.get("openingGroupAuthoring", {}).get("phase") != "precompound" or
            plan.get("openingGroupAuthoring", {}).get("orientation") != orientation or
            plan.get("openingGroupAuthoring", {}).get("fragmentSha256") != sha256_bytes(fragment_bytes) or
            plan.get("resourceId") != resource_id or plan.get("taskId") != task_id):
        raise ValueError("The phase 1 plan does not match this orientation, fragment, resource, or task.")
    if receipt.get("planSha256") != sha256_file(plan_path):
        raise ValueError("The phase 1 receipt belongs to a different plan file.")
    if receipt.get("status") != "authored-awaiting-native-review":
        raise ValueError("The phase 1 author_native receipt is not a successful completed run.")
    planned_steps = plan.get("steps", [])
    if len(planned_steps) != 12:
        raise ValueError("The phase 1 plan does not contain the exact 12 synchronous child operations.")
    expected_steps = {step.get("id"): step for step in planned_steps}
    actual_steps = {step.get("id"): step for step in receipt.get("steps", [])}
    if set(actual_steps) != set(expected_steps):
        raise ValueError("The phase 1 receipt has missing, extra, or duplicate operation IDs.")
    for step_id, expected in expected_steps.items():
        actual = actual_steps[step_id]
        if (actual.get("commandId") != expected.get("commandId") or
                actual.get("values") != expected.get("values") or
                not isinstance(actual.get("result"), dict) or actual["result"].get("ok") is not True):
            raise ValueError(f"Phase 1 step {step_id} is not proven successful with the planned values.")
    return plan, receipt


def _validate_settlement(path, fragment, resource_id):
    envelope = _read_json(path, "compound settlement receipt")
    result = _extract_tool_result(envelope)
    compound = fragment["phase2_compound_create"]["values"]
    if not isinstance(result, dict):
        raise ValueError("The settlement receipt did not contain a structured result object.")
    expected = {
        "ok": True,
        "settled": True,
        "status": "ready",
        "transparencyVerified": True,
        "requestId": compound["requestId"],
        "childTimelineId": compound["childTimelineId"],
        "resourceId": resource_id,
    }
    for field, value in expected.items():
        if result.get(field) is not value and result.get(field) != value:
            raise ValueError(f"The settlement receipt does not prove {field}={value!r}.")
    clip_id = result.get("compoundClipId")
    _require_uuid(clip_id, "settled compoundClipId")
    if clip_id == compound["childTimelineId"]:
        raise ValueError("The returned parent compound clip ID cannot equal the child Timeline ID.")
    return result, envelope


def build_plan(orientation, phase, resource_id, task_id, starting_hash,
               output_path, phase1_plan_path=None, phase1_receipt_path=None,
               settlement_receipt_path=None):
    _require_scope(resource_id, task_id, starting_hash)
    if orientation not in FRAGMENTS:
        raise ValueError("orientation must be landscape or portrait.")
    if phase not in {"precompound", "parent-reveal"}:
        raise ValueError("phase must be precompound or parent-reveal.")
    fragment_path, fragment, fragment_bytes = _load_fragment(orientation)

    if phase == "precompound":
        if phase1_plan_path or phase1_receipt_path or settlement_receipt_path:
            raise ValueError("Precompound phase does not accept prior phase proofs.")
        steps = _build_phase1_steps(orientation, fragment)
        plan = _common_plan(orientation, fragment_path, fragment_bytes,
                            resource_id, task_id, starting_hash, phase, steps)
    else:
        if not all((phase1_plan_path, phase1_receipt_path, settlement_receipt_path)):
            raise ValueError("Parent reveal requires phase 1 plan, phase 1 receipt, and compound settlement receipt.")
        phase1_plan_path = Path(phase1_plan_path)
        phase1_receipt_path = Path(phase1_receipt_path)
        settlement_receipt_path = Path(settlement_receipt_path)
        _, phase1_receipt = _validate_precompound_receipt(
            phase1_plan_path, phase1_receipt_path, orientation, fragment_path,
            fragment_bytes, resource_id, task_id)
        settlement, _ = _validate_settlement(settlement_receipt_path, fragment, resource_id)
        parent_id = settlement["compoundClipId"]
        phase4 = fragment["phase4_parent_reveal_after_settlement"]
        mask_values = dict(phase4["command1"]["values"])
        mask_values["clipId"] = parent_id
        animation_values = dict(phase4["command2"]["values"])
        animation_values["clipId"] = parent_id
        steps = [
            {"id": _step_id(orientation, f"parent-mask/{parent_id}"),
             "commandId": phase4["command1"]["commandId"], "values": mask_values},
            {"id": _step_id(orientation, f"parent-reveal/{parent_id}"),
             "commandId": phase4["command2"]["commandId"], "values": animation_values},
        ]
        plan = _common_plan(orientation, fragment_path, fragment_bytes,
                            resource_id, task_id, starting_hash, phase, steps)
        plan["openingGroupAuthoring"].update({
            "phase1PlanSha256": sha256_file(phase1_plan_path),
            "phase1ReceiptSha256": sha256_file(phase1_receipt_path),
            "compoundSettlementReceiptSha256": sha256_file(settlement_receipt_path),
            "requestId": settlement["requestId"],
            "childTimelineId": settlement["childTimelineId"],
            "compoundClipId": parent_id,
        })
        plan["sourcePlans"].extend([
            {"plan": phase1_plan_path.name, "sha256": sha256_file(phase1_plan_path), "receiptSha256": sha256_file(phase1_receipt_path)},
            {"plan": settlement_receipt_path.name, "sha256": sha256_file(settlement_receipt_path)},
        ])

    output_path = Path(output_path).expanduser()
    if output_path.suffix != ".json":
        raise ValueError("Output plan path must end in .json.")
    output_parent = output_path.parent.resolve(strict=True)
    try:
        output_parent.relative_to(ROOT.resolve())
    except ValueError as error:
        raise ValueError("Output plans must stay inside marketing/native-recreation-v4.") from error
    output_path = output_parent / output_path.name
    payload = (json.dumps(plan, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        fd = os.open(output_path, flags, 0o600)
    except FileExistsError as error:
        raise ValueError(f"Refusing to overwrite existing plan: {output_path}") from error
    try:
        with os.fdopen(fd, "wb") as target:
            target.write(payload)
            target.flush()
            os.fsync(target.fileno())
    except Exception:
        try:
            output_path.unlink()
        except OSError:
            pass
        raise
    return plan


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--orientation", choices=tuple(FRAGMENTS), required=True)
    parser.add_argument("--phase", choices=("precompound", "parent-reveal"), required=True)
    parser.add_argument("--resource-id", required=True)
    parser.add_argument("--task-id", required=True)
    parser.add_argument("--starting-document-hash", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--phase1-plan", type=Path)
    parser.add_argument("--phase1-receipt", type=Path)
    parser.add_argument("--settlement-receipt", type=Path)
    args = parser.parse_args()
    plan = build_plan(
        args.orientation, args.phase, args.resource_id, args.task_id,
        args.starting_document_hash, args.output,
        args.phase1_plan, args.phase1_receipt, args.settlement_receipt)
    print(json.dumps({
        "plan": str(args.output),
        "phase": args.phase,
        "steps": len(plan["steps"]),
        "resourceId": plan["resourceId"],
        "taskId": plan["taskId"],
        "startingDocumentHash": plan["startingDocumentHash"],
        "nativeExecution": "not started",
    }, indent=2))


if __name__ == "__main__":
    main()
