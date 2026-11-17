"""Retarget a frozen native continuation after a verified project-copy import.

This helper is offline only. It reads an immutable continuation plan and
structured native receipts, then creates a new plan that names the imported
Timeline and its new task checkpoint. It never calls MCP and never modifies
any source plan, project, or receipt.
"""

from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import sys
import uuid


HERE = Path(__file__).resolve().parent
DEFAULT_PLAN = HERE / "full-landscape-v6-continuation.commands.json"
DEFAULT_PARENT_DOCUMENT = HERE / "evidence" / "native-full-blocked-document.json"
DEFAULT_SOURCE_SHA256 = (
    "bcd04fe53a66dafe252c4b59c31fe74f2ad82ca0a09284655e8de6af268006c2")
SAVE_COMMAND_ID = "timeline.action.saveTimeline"
PROVENANCE_SCHEMA = "looksy.native-project-import-retarget/v1"
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
TIMELINE_RESOURCE_RE = re.compile(
    r"^timeline:([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})$")


class RetargetError(RuntimeError):
    """Raised when native import, task admission, or scope is not proven."""


def _sha256(data):
    return hashlib.sha256(data).hexdigest()


def _canonical_json(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=False, allow_nan=False)


def _read_json(path, label):
    try:
        raw = Path(path).read_bytes()
        value = json.loads(raw)
    except (OSError, json.JSONDecodeError) as error:
        raise RetargetError(f"Could not read {label}: {error}") from error
    if not isinstance(value, dict):
        raise RetargetError(f"{label} must contain a JSON object.")
    return raw, value


def _structured_tool_proof(envelope, label):
    result = envelope.get("result")
    if not isinstance(result, dict) or result.get("isError") is True:
        raise RetargetError(f"{label} is not a successful native tool response.")
    structured = result.get("structuredContent")
    if not isinstance(structured, dict) or structured.get("ok") is not True:
        raise RetargetError(f"{label} has no authoritative ok:true structuredContent.")
    return structured


def _valid_sha(value, label):
    if not isinstance(value, str) or not SHA256_RE.fullmatch(value):
        raise RetargetError(f"{label} is not a canonical lowercase SHA-256.")
    return value


def _canonical_existing_path(value, label, *, regular_file=None):
    if not isinstance(value, str) or not value or not Path(value).is_absolute():
        raise RetargetError(f"{label} must be an absolute canonical path.")
    path = Path(value)
    if path.is_symlink():
        raise RetargetError(f"{label} cannot be a symbolic link.")
    try:
        resolved = path.resolve(strict=True)
    except OSError as error:
        raise RetargetError(f"{label} is unavailable: {error}") from error
    if os.path.normpath(str(path)) != str(resolved):
        raise RetargetError(f"{label} is not canonical.")
    if regular_file is True and not resolved.is_file():
        raise RetargetError(f"{label} must be a regular file.")
    if regular_file is False and not resolved.is_dir():
        raise RetargetError(f"{label} must be an existing directory.")
    return resolved


def _canonical_new_output(value, output_directory):
    try:
        value = os.fspath(value)
    except TypeError as error:
        raise RetargetError("The new save path must be an absolute path.") from error
    if not isinstance(value, str) or not value or not Path(value).is_absolute():
        raise RetargetError("The new save path must be absolute.")
    directory = _canonical_existing_path(
        output_directory, "Admitted outputDirectory", regular_file=False)
    path = Path(value)
    if path.is_symlink():
        raise RetargetError("The new save path cannot be a symbolic link.")
    normalized = Path(os.path.normpath(str(path)))
    if str(path) != str(normalized):
        raise RetargetError("The new save path must already be lexically canonical.")
    if normalized.parent != directory:
        raise RetargetError("The new save path must be a direct child of the admitted outputDirectory.")
    if path.exists():
        raise RetargetError("The new save path already exists; choose a fresh v7 filename.")
    return normalized


def _resource_uuid(resource_id, label):
    if not isinstance(resource_id, str):
        raise RetargetError(f"{label} has no Timeline resource ID.")
    match = TIMELINE_RESOURCE_RE.fullmatch(resource_id)
    if not match:
        raise RetargetError(f"{label} has a noncanonical Timeline resource ID.")
    return match.group(1)


def _unique_ids(records, key, label):
    if not isinstance(records, list) or not records:
        raise RetargetError(f"{label} must be a nonempty array.")
    values = []
    for record in records:
        if not isinstance(record, dict):
            raise RetargetError(f"{label} contains a malformed record.")
        value = record.get(key)
        if not isinstance(value, str) or not value:
            raise RetargetError(f"{label} contains a missing {key}.")
        values.append(value)
    if len(values) != len(set(values)):
        raise RetargetError(f"{label} contains duplicate {key} values.")
    return set(values)


def _project_ids(project, label):
    tracks = project.get("tracks")
    if not isinstance(tracks, list) or not tracks:
        raise RetargetError(f"{label} has no native track array.")
    track_ids = []
    clip_ids = []
    for track in tracks:
        if not isinstance(track, dict):
            raise RetargetError(f"{label} contains a malformed track.")
        track_id = track.get("trackId")
        clips = track.get("clips")
        if not isinstance(track_id, str) or not track_id or not isinstance(clips, list):
            raise RetargetError(f"{label} contains an invalid track identity or clip list.")
        track_ids.append(track_id)
        for clip in clips:
            if not isinstance(clip, dict):
                raise RetargetError(f"{label} contains a malformed clip.")
            clip_id = clip.get("clipId")
            if not isinstance(clip_id, str) or not clip_id:
                raise RetargetError(f"{label} contains a missing clipId.")
            clip_ids.append(clip_id)
    if len(track_ids) != len(set(track_ids)) or len(clip_ids) != len(set(clip_ids)):
        raise RetargetError(f"{label} contains duplicate stable Timeline IDs.")
    return set(track_ids), set(clip_ids)


def _inspect_ids(document_proof, label):
    track_ids = _unique_ids(document_proof.get("tracks"), "trackId", f"{label} tracks")
    clip_ids = _unique_ids(document_proof.get("clips"), "clipId", f"{label} clips")
    return track_ids, clip_ids


def _validate_plan_steps(plan):
    steps = plan.get("steps")
    if not isinstance(steps, list) or not steps:
        raise RetargetError("The continuation plan must contain a nonempty steps array.")
    positions = {}
    for index, step in enumerate(steps):
        if not isinstance(step, dict):
            raise RetargetError(f"Plan step {index} is not an object.")
        step_id = step.get("id")
        command_id = step.get("commandId")
        if not isinstance(step_id, str) or not step_id or step_id in positions:
            raise RetargetError(f"Plan has an empty or duplicate step id at index {index}.")
        if not isinstance(command_id, str) or not command_id:
            raise RetargetError(f"Plan step {step_id} has no commandId.")
        if not isinstance(step.get("values"), dict):
            raise RetargetError(f"Plan step {step_id} values must be an object.")
        positions[step_id] = index
    saves = [index for index, step in enumerate(steps)
             if step.get("commandId") == SAVE_COMMAND_ID]
    if saves != [len(steps) - 1]:
        raise RetargetError("The plan must end with exactly one saveTimeline action.")
    if steps[-1].get("id") != "delivery/save-editable-project":
        raise RetargetError("The final save action does not have the frozen plan's expected stable ID.")
    if steps[-1]["values"].get("outputPath") != plan.get("outputPath"):
        raise RetargetError("The final save action and top-level plan outputPath disagree.")
    return steps, positions


def _validate_frozen_identity(plan):
    resource_id = plan.get("resourceId")
    task_id = plan.get("taskId")
    _resource_uuid(resource_id, "Frozen continuation")
    try:
        parsed_task_id = uuid.UUID(task_id)
    except (ValueError, TypeError, AttributeError) as error:
        raise RetargetError("The frozen continuation has no canonical original task ID.") from error
    if str(parsed_task_id) != task_id:
        raise RetargetError("The frozen continuation task ID is not canonical.")
    starting_hash = _valid_sha(
        plan.get("startingDocumentHash"), "Frozen continuation starting document hash")
    provenance = plan.get("continuationProvenance")
    if (not isinstance(provenance, dict) or
            provenance.get("schema") != "looksy.native-authoring-continuation/v1" or
            provenance.get("resourceId") != resource_id or
            provenance.get("taskId") != task_id or
            provenance.get("startingDocumentHash") != starting_hash):
        raise RetargetError("Frozen continuation identity does not match its source provenance.")
    return resource_id, task_id


def _validate_refs_and_ids(steps, positions, clip_ids, track_ids):
    def visit(value, current_index, label):
        if isinstance(value, dict):
            if "result" in value or "field" in value:
                if set(value) != {"result", "field"}:
                    raise RetargetError(f"{label} has a malformed stable-result reference.")
                result_id, field = value["result"], value["field"]
                if not isinstance(result_id, str) or not isinstance(field, str):
                    raise RetargetError(f"{label} has a malformed stable-result reference.")
                if result_id not in positions or positions[result_id] >= current_index:
                    raise RetargetError(
                        f"{label} references a missing, current, or future result {result_id!r}; "
                        "completed-prefix references must already be literal native IDs.")
                return
            for key, item in value.items():
                if key == "clipId":
                    is_reference = isinstance(item, dict) and (
                        "result" in item or "field" in item)
                    if not is_reference and (not isinstance(item, str) or item not in clip_ids):
                        raise RetargetError(
                            f"{label} has a literal clipId absent from the imported copy.")
                if key == "trackId":
                    is_reference = isinstance(item, dict) and (
                        "result" in item or "field" in item)
                    if not is_reference and (not isinstance(item, str) or item not in track_ids):
                        raise RetargetError(
                            f"{label} has a literal trackId absent from the imported copy.")
                if key == "clipIds":
                    if (not isinstance(item, list) or
                            any(not isinstance(value, str) or value not in clip_ids
                                for value in item)):
                        raise RetargetError(
                            f"{label} has a clipIds list that is not present in the imported copy.")
                if key == "trackIds":
                    if (not isinstance(item, list) or
                            any(not isinstance(value, str) or value not in track_ids
                                for value in item)):
                        raise RetargetError(
                            f"{label} has a trackIds list that is not present in the imported copy.")
                visit(item, current_index, label)
        elif isinstance(value, list):
            for item in value:
                visit(item, current_index, label)

    for index, step in enumerate(steps):
        visit(step.get("values"), index, f"Step {step['id']}")


def _verify_import(imported, parent_document, source_sha256):
    if imported.get("sourceSha256") != source_sha256:
        raise RetargetError("The imported project source SHA differs from the exact startup grant.")
    if imported.get("graphMatches") is not True or imported.get("readbackVerified") is not True:
        raise RetargetError("Native import did not verify an exact saved project graph.")
    if imported.get("libraryRegistered") is not True:
        raise RetargetError("The imported project was not confirmed in the app-managed Library.")
    source_graph = _valid_sha(imported.get("sourceGraphSha256"), "Import sourceGraphSha256")
    copy_graph = _valid_sha(imported.get("copyGraphSha256"), "Import copyGraphSha256")
    if source_graph != copy_graph:
        raise RetargetError("The imported copy graph hash differs from the granted source graph.")

    resource = imported.get("resource")
    if not isinstance(resource, dict) or resource.get("ok") is not True:
        raise RetargetError("The imported project resource has no successful native readback.")
    ready_at_import = resource.get("ready")
    if not isinstance(ready_at_import, bool):
        raise RetargetError("The import receipt has no explicit resource readiness state.")
    resource_id = imported.get("resourceId")
    if resource.get("resourceId") != resource_id:
        raise RetargetError("The import receipt resource identity is inconsistent.")
    timeline_id = _resource_uuid(resource_id, "Imported project")
    if imported.get("timelineId") != timeline_id:
        raise RetargetError("The import receipt Timeline ID does not match its resource ID.")
    if resource.get("timelineId") not in (None, timeline_id):
        raise RetargetError("The inspected import resource has a different Timeline ID.")
    receipt_path = imported.get("savedDocumentPath")
    resource_path = resource.get("savedDocumentPath")
    if not isinstance(receipt_path, str) or not receipt_path:
        receipt_path = resource_path
    if resource_path not in (None, receipt_path):
        raise RetargetError("The import receipt and resource report different saved paths.")
    canonical_copy_path = _canonical_existing_path(
        receipt_path, "Imported savedDocumentPath", regular_file=True)
    if canonical_copy_path.name != timeline_id + ".json":
        raise RetargetError("The imported project path does not match its fresh managed Timeline ID.")
    try:
        copy_project = json.loads(canonical_copy_path.read_bytes())
    except (OSError, json.JSONDecodeError) as error:
        raise RetargetError(f"The imported project file is unreadable: {error}") from error
    if not isinstance(copy_project, dict) or copy_project.get("timelineId") != timeline_id:
        raise RetargetError("The imported project file does not contain its fresh Timeline ID.")

    copy_track_ids, copy_clip_ids = _project_ids(copy_project, "Imported project")
    source_document = parent_document
    source_id = source_document.get("resourceId")
    _resource_uuid(source_id, "Source document proof")
    source_track_ids, source_clip_ids = _inspect_ids(source_document, "Source document proof")
    receipt_clip_ids = imported.get("clipIds")
    if not isinstance(receipt_clip_ids, list) or any(
            not isinstance(value, str) or not value for value in receipt_clip_ids):
        raise RetargetError("The import receipt has no exact stable clip ID list.")
    if len(receipt_clip_ids) != len(set(receipt_clip_ids)):
        raise RetargetError("The import receipt contains duplicate stable clip IDs.")
    if (set(receipt_clip_ids) != source_clip_ids or copy_clip_ids != source_clip_ids or
            copy_track_ids != source_track_ids):
        raise RetargetError("The imported copy did not retain the exact source clip and track IDs.")
    if imported.get("clipCount") != len(source_clip_ids) or imported.get("trackCount") != len(source_track_ids):
        raise RetargetError("Import receipt track or clip counts differ from the source proof.")
    return {
        "resourceId": resource_id,
        "timelineId": timeline_id,
        "savedDocumentPath": str(canonical_copy_path),
        "readyAtImport": ready_at_import,
        "sourceGraphSha256": source_graph,
        "copyGraphSha256": copy_graph,
        "clipIds": source_clip_ids,
        "trackIds": source_track_ids,
        "copyProjectSha256": _sha256(canonical_copy_path.read_bytes()),
        "sourceProjectResourceId": source_id,
        "sourceProjectDocumentHash": _valid_sha(
            source_document.get("documentHash"), "Source document hash"),
    }


def _verify_admission(admission, document, plan, imported_resource_id):
    task_id = admission.get("taskId")
    try:
        parsed_task_id = uuid.UUID(task_id)
    except (ValueError, TypeError, AttributeError) as error:
        raise RetargetError("Task admission has no canonical taskId.") from error
    if str(parsed_task_id) != task_id:
        raise RetargetError("Task admission taskId is not canonical.")
    if admission.get("active") is not True or admission.get("status") != "running":
        raise RetargetError("The new Timeline task is not active and running.")

    target_ids = admission.get("targetResourceIds")
    target_id = admission.get("targetResourceId")
    if target_ids is None:
        target_ids = [target_id] if isinstance(target_id, str) else None
    if target_ids != [imported_resource_id] or target_id not in (None, imported_resource_id):
        raise RetargetError("Task admission does not own exactly the imported Timeline resource.")
    scope = admission.get("nativeTimelineScope")
    if not isinstance(scope, dict) or scope.get("targetIds") != [imported_resource_id]:
        raise RetargetError("Task admission native scope targets another resource.")

    expected_assets = plan.get("assetPaths")
    scope_assets = scope.get("assetPaths")
    if (not isinstance(expected_assets, list) or not isinstance(scope_assets, list) or
            any(not isinstance(path, str) or not path for path in expected_assets) or
            any(not isinstance(path, str) or not path for path in scope_assets) or
            len(expected_assets) != len(set(expected_assets)) or
            len(scope_assets) != len(set(scope_assets)) or
            scope_assets != expected_assets):
        raise RetargetError("Task admission assetPaths differ from the frozen continuation scope.")
    expected_directory = _canonical_existing_path(
        plan.get("outputDirectory"), "Plan outputDirectory", regular_file=False)
    scope_directory = _canonical_existing_path(
        scope.get("outputDirectory"), "Admitted outputDirectory", regular_file=False)
    if scope_directory != expected_directory:
        raise RetargetError("Task admission outputDirectory differs from the frozen continuation scope.")

    document_resource_id = document.get("resourceId")
    if document_resource_id != imported_resource_id or document.get("ready") is not True:
        raise RetargetError("Native document inspection does not match the imported Timeline.")
    document_hash = _valid_sha(document.get("documentHash"), "Current document hash")
    if (document.get("width") != plan.get("composition", {}).get("width") or
            document.get("height") != plan.get("composition", {}).get("height") or
            document.get("frameRate") != plan.get("composition", {}).get("fps")):
        raise RetargetError("Imported Timeline composition differs from the continuation plan.")

    resources = admission.get("resources")
    if not isinstance(resources, list) or len(resources) != 1:
        raise RetargetError("Task admission must checkpoint exactly one Timeline resource.")
    checkpoint = resources[0]
    if (not isinstance(checkpoint, dict) or checkpoint.get("id") != imported_resource_id or
            checkpoint.get("pending") is not False or checkpoint.get("reverted") is not False):
        raise RetargetError("The imported Timeline has no settled journal checkpoint.")
    current_hash = _valid_sha(checkpoint.get("currentHash"), "Current journal resource hash")
    before_hash = _valid_sha(checkpoint.get("beforeHash"), "Journal baseline hash")
    if current_hash != document_hash or before_hash != document_hash:
        raise RetargetError("Current native document hash does not match the new journal checkpoint.")
    return task_id, document_hash, expected_directory, scope_assets


def retarget_continuation_plan(
    plan_path,
    import_receipt_path,
    admission_receipt_path,
    document_proof_path,
    source_document_proof_path,
    output_path,
    expected_source_sha256=DEFAULT_SOURCE_SHA256,
):
    plan_bytes, plan = _read_json(plan_path, "frozen continuation plan")
    import_bytes, import_envelope = _read_json(import_receipt_path, "native project import receipt")
    admission_bytes, admission_envelope = _read_json(admission_receipt_path, "native task admission receipt")
    document_bytes, document_envelope = _read_json(document_proof_path, "current native document proof")
    source_document_bytes, source_document_envelope = _read_json(
        source_document_proof_path, "source native document proof")
    _valid_sha(expected_source_sha256, "Expected startup-granted source SHA")

    steps, positions = _validate_plan_steps(plan)
    old_resource_id, old_task_id = _validate_frozen_identity(plan)
    old_output_path = plan.get("outputPath")
    output_directory = plan.get("outputDirectory")
    if not isinstance(old_output_path, str) or not Path(old_output_path).is_absolute():
        raise RetargetError("The frozen plan outputPath must be absolute.")
    original_output = Path(old_output_path)
    original_directory = _canonical_existing_path(
        output_directory, "Plan outputDirectory", regular_file=False)
    if (original_output.is_symlink() or
            Path(os.path.normpath(old_output_path)) != original_output or
            original_output.parent != original_directory):
        raise RetargetError("The frozen plan outputPath is not a canonical direct child of its outputDirectory.")
    new_output = _canonical_new_output(output_path, output_directory)
    if str(new_output) == str(original_output):
        raise RetargetError("The new v7 save target must differ from the frozen v6 output path.")

    imported = _structured_tool_proof(import_envelope, "Project import receipt")
    admission = _structured_tool_proof(admission_envelope, "Task admission receipt")
    document = _structured_tool_proof(document_envelope, "Current document proof")
    source_document = _structured_tool_proof(source_document_envelope, "Source document proof")
    imported_result = _verify_import(imported, source_document, expected_source_sha256)
    resource_id = imported_result["resourceId"]
    if resource_id == old_resource_id:
        raise RetargetError("The imported Timeline must have a fresh resource ID.")
    task_id, document_hash, admitted_output_directory, scope_assets = _verify_admission(
        admission, document, plan, resource_id)
    if task_id == old_task_id:
        raise RetargetError("The imported Timeline must use a new task ID.")
    if document.get("savedDocumentPath") != imported_result["savedDocumentPath"]:
        raise RetargetError("Current document inspection reports a different canonical project path.")
    inspected_track_ids, inspected_clip_ids = _inspect_ids(document, "Current document proof")
    if (inspected_track_ids != imported_result["trackIds"] or
            inspected_clip_ids != imported_result["clipIds"]):
        raise RetargetError("Current document inspection differs from the imported project copy ID set.")

    _validate_refs_and_ids(
        steps, positions, imported_result["clipIds"], imported_result["trackIds"])
    _canonical_new_output(str(new_output), str(admitted_output_directory))
    save_step = steps[-1]
    new_plan = dict(plan)
    new_steps = [dict(step) for step in steps]
    new_steps[-1]["values"] = dict(save_step["values"])
    new_steps[-1]["values"]["outputPath"] = str(new_output)
    new_plan["steps"] = new_steps
    new_plan["resourceId"] = resource_id
    new_plan["taskId"] = task_id
    new_plan["startingDocumentHash"] = document_hash
    new_plan["outputPath"] = str(new_output)
    prior_provenance = plan.get("continuationProvenance")
    new_plan["continuationProvenance"] = {
        "schema": PROVENANCE_SCHEMA,
        "priorPlanSha256": _sha256(plan_bytes),
        "priorContinuationProvenanceSha256": _sha256(
            _canonical_json(prior_provenance).encode("utf-8")),
        "priorContinuationProvenance": prior_provenance,
        "nativeImportReceiptSha256": _sha256(import_bytes),
        "nativeAdmissionReceiptSha256": _sha256(admission_bytes),
        "nativeDocumentProofSha256": _sha256(document_bytes),
        "sourceDocumentProofSha256": _sha256(source_document_bytes),
        "sourceProjectResourceId": imported_result["sourceProjectResourceId"],
        "sourceProjectDocumentHash": imported_result["sourceProjectDocumentHash"],
        "sourceProjectSha256": imported["sourceSha256"],
        "sourceGraphSha256": imported_result["sourceGraphSha256"],
        "copyGraphSha256": imported_result["copyGraphSha256"],
        "copyProjectSha256": imported_result["copyProjectSha256"],
        "copyReadyAtImport": imported_result["readyAtImport"],
        "clipIdSetSha256": _sha256(_canonical_json(sorted(imported_result["clipIds"])).encode("utf-8")),
        "trackIdSetSha256": _sha256(_canonical_json(sorted(imported_result["trackIds"])).encode("utf-8")),
        "clipCount": len(imported_result["clipIds"]),
        "trackCount": len(imported_result["trackIds"]),
        "resourceId": resource_id,
        "timelineId": imported_result["timelineId"],
        "taskId": task_id,
        "startingDocumentHash": document_hash,
        "journalCheckpointHash": document_hash,
        "outputDirectory": str(admitted_output_directory),
        "outputPath": str(new_output),
        "retargetedFields": [
            "resourceId",
            "taskId",
            "startingDocumentHash",
            "outputPath",
            "steps[delivery/save-editable-project].values.outputPath",
            "continuationProvenance",
        ],
    }
    return new_plan


def serialize_plan(plan):
    return (json.dumps(plan, indent=2, ensure_ascii=False, allow_nan=False) + "\n").encode("utf-8")


def write_new_plan(plan, output_path, input_paths=()):
    output = Path(output_path)
    directory = _canonical_existing_path(
        str(output.parent), "Retargeted plan output directory", regular_file=False)
    if output.is_symlink() or Path(os.path.normpath(str(output))) != directory / output.name:
        raise RetargetError("Retargeted plan output path is not canonical.")
    for input_path in input_paths:
        if output.resolve(strict=False) == Path(input_path).resolve(strict=False):
            raise RetargetError("Retargeted output must not replace an input plan, project, or receipt.")
    if output.exists():
        raise RetargetError("Retargeted plan output already exists; refusing to overwrite it.")

    payload = serialize_plan(plan)
    temporary = output.with_name(output.name + "." + str(uuid.uuid4()) + ".tmp")
    try:
        descriptor = os.open(temporary, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        with os.fdopen(descriptor, "wb") as target:
            target.write(payload)
            target.flush()
            os.fsync(target.fileno())
        # A hard link publishes atomically while refusing to replace a path
        # created concurrently after the absent-target check.
        os.link(temporary, output)
        temporary.unlink()
        directory_fd = os.open(directory, os.O_RDONLY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    except FileExistsError as error:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
        raise RetargetError("Retargeted plan output appeared concurrently; refusing to overwrite it.") from error
    except Exception:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
        # Never risk deleting a concurrently created/replaced output path.
        raise
    return output


def _default_paths():
    projects = HERE / "projects"
    return {
        "plan_path": DEFAULT_PLAN,
        "import_receipt_path": HERE / "evidence" / "native-project-import-receipt.json",
        "admission_receipt_path": HERE / "evidence" / "native-project-import-admission.json",
        "document_proof_path": HERE / "evidence" / "native-project-import-document.json",
        "source_document_proof_path": DEFAULT_PARENT_DOCUMENT,
        "native_output_path": projects / "looksy-native-full-landscape-v7.json",
        "plan_output_path": HERE / "full-landscape-v7-continuation.commands.json",
    }


def main(argv=None):
    defaults = _default_paths()
    parser = argparse.ArgumentParser(description=__doc__)
    for name, default in defaults.items():
        parser.add_argument("--" + name.replace("_", "-"), type=Path, default=default)
    parser.add_argument("--expected-source-sha256", default=DEFAULT_SOURCE_SHA256)
    args = parser.parse_args(argv)
    try:
        result = retarget_continuation_plan(
            args.plan_path,
            args.import_receipt_path,
            args.admission_receipt_path,
            args.document_proof_path,
            args.source_document_proof_path,
            args.native_output_path,
            args.expected_source_sha256,
        )
        output = write_new_plan(result, args.plan_output_path, input_paths=(
            args.plan_path,
            args.import_receipt_path,
            args.admission_receipt_path,
            args.document_proof_path,
            args.source_document_proof_path,
            args.native_output_path,
        ))
    except (RetargetError, OSError) as error:
        print(f"retarget refused: {error}", file=sys.stderr)
        return 2
    print(json.dumps({
        "ok": True,
        "planOutput": str(output),
        "nativeOutputPath": result["outputPath"],
        "stepCount": len(result["steps"]),
        "resourceId": result["resourceId"],
        "taskId": result["taskId"],
        "startingDocumentHash": result["startingDocumentHash"],
        "provenance": result["continuationProvenance"],
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
