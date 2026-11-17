"""Bind the frozen portrait authoring plan to a fresh, empty native Timeline.

This is an offline proof binder. It never calls MCP. It emits a new plan only
when create, Library, admission, and ready-document receipts agree exactly.
The v6 source plan and its historical evidence remain untouched.
"""

from copy import deepcopy
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import sys


HERE = Path(__file__).resolve().parent
PARENT_PLAN = HERE / "full-portrait-v6.commands.json"
PARENT_SHA256 = "1a7b191061d1f0419216e8a110aaaf08985b472f95b869b4d9409aa8737e034d"
DEFAULT_BOUND_PLAN = HERE / "full-portrait-v7.commands.json"
PARENT_NATIVE_OUTPUT = HERE / "projects" / "looksy-native-full-portrait-v6.json"
DEFAULT_NATIVE_OUTPUT = HERE / "projects" / "looksy-native-full-portrait-v7.json"
TIMELINE_NAME = "Looksy Native Trailer — Portrait"
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
TIMELINE_ID_RE = re.compile(r"^timeline:([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})$")

ADMISSION_ACTIONS = [
    {"kind": "color", "summary": "Recreate the reference color presentation and additive-light scenes."},
    {"kind": "framing", "summary": "Animate the exact reference framing, dimensions and transitions."},
    {"kind": "masking", "summary": "Apply editable geometric masks and source mattes matching the reference."},
    {"kind": "text", "summary": "Build all brand text, chapter copy and labels as native titles."},
    {"kind": "layers", "summary": "Create editable native graphics, vectors, generators and stacked tracks."},
    {"kind": "effects", "summary": "Animate native light fields, veils, opacity, blends and geometry."},
    {"kind": "audio", "summary": "Use the reference soundtrack source once on the native audio timeline."},
    {"kind": "media", "summary": "Import the exact admitted photo and video assets from the approved trailer plan."},
    {"kind": "delivery", "summary": "Save a new editable native project copy for review; retain task recovery."},
]


class BinderError(RuntimeError):
    """Raised when supplied evidence does not establish a safe fresh binding."""


def _sha256(data):
    return hashlib.sha256(data).hexdigest()


def _valid_sha(value, label):
    if not isinstance(value, str) or not SHA256_RE.fullmatch(value):
        raise BinderError(f"{label} must be a canonical lowercase SHA-256.")
    return value


def _read_json(path, label):
    try:
        raw = Path(path).read_bytes()
        value = json.loads(raw)
    except (OSError, json.JSONDecodeError) as error:
        raise BinderError(f"Could not read {label}: {error}") from error
    if not isinstance(value, dict):
        raise BinderError(f"{label} must be a JSON object.")
    return raw, value


def _read_parent(path):
    raw, plan = _read_json(path, "frozen portrait source plan")
    if Path(path).resolve() != PARENT_PLAN.resolve():
        raise BinderError("The source must be the frozen full-portrait-v6.commands.json plan.")
    if _sha256(raw) != PARENT_SHA256:
        raise BinderError("The full-portrait-v6 source plan changed; refusing to bind a modified plan.")
    if (plan.get("composition") != {"width": 1080, "height": 1920, "fps": 30} or
            len(plan.get("steps", [])) != 3642):
        raise BinderError("The frozen source plan no longer matches the portrait 1080x1920, 30fps plan.")
    assets = plan.get("assetPaths")
    if not isinstance(assets, list) or len(assets) != 104 or len(set(assets)) != 104:
        raise BinderError("The frozen portrait plan must contain exactly 104 unique admitted asset paths.")
    for value in assets:
        if not isinstance(value, str) or not Path(value).is_absolute():
            raise BinderError("Every frozen asset path must be absolute and canonical.")
        file_path = Path(value)
        try:
            canonical = str(file_path.resolve(strict=True))
        except OSError as error:
            raise BinderError(f"A frozen portrait source asset is unavailable: {value}") from error
        if not file_path.is_file() or file_path.is_symlink() or canonical != value:
            raise BinderError(f"A frozen portrait source path is not a canonical regular file: {value}")
    output_directory = plan.get("outputDirectory")
    if not isinstance(output_directory, str) or not Path(output_directory).is_absolute():
        raise BinderError("The frozen portrait plan must name an absolute outputDirectory.")
    try:
        canonical_output = str(Path(output_directory).resolve(strict=True))
    except OSError as error:
        raise BinderError("The frozen portrait outputDirectory is unavailable.") from error
    if not Path(output_directory).is_dir() or canonical_output != output_directory:
        raise BinderError("The frozen portrait outputDirectory must be an existing canonical directory.")
    if plan.get("outputPath") != str(PARENT_NATIVE_OUTPUT):
        raise BinderError("The frozen v6 outputPath differs from the expected historical native filename.")
    return raw, plan


def create_draft_request():
    """Exact typed arguments for a new, independent portrait Library draft."""
    return {
        "tool": "timeline.create_draft",
        "arguments": {
            "settings": {
                "timelineName": TIMELINE_NAME,
                "width": 1080,
                "height": 1920,
                "frameRate": 30,
                "audioSampleRate": 48000,
                "colorSpace": "Rec.709",
                "dataLevels": "Legal",
                "trackLayout": "standard",
            }
        },
    }


def admission_request(plan, resource_id):
    """Exact task-admission arguments after a fresh draft ID is inspected."""
    if not isinstance(resource_id, str) or not TIMELINE_ID_RE.fullmatch(resource_id):
        raise BinderError("Use the exact fresh portrait resourceId returned by inspectAgentDocument.")
    return {
        "tool": "timeline.admit_task",
        "arguments": {
            "targetResourceId": resource_id,
            "objective": (
                "Create the complete298-second portrait version of the existing trailer as editable native "
                "Looksy Timeline content. Preserve source media and retain reversible native task recovery. "
                "Rendered reference fidelity remains subject to review."
            ),
            "actions": deepcopy(ADMISSION_ACTIONS),
            "assetPaths": list(plan["assetPaths"]),
            "outputDirectory": plan["outputDirectory"],
            "confirmed": True,
        },
    }


def _native_structured(envelope, label):
    result = envelope.get("result")
    if not isinstance(result, dict) or result.get("isError") is True:
        raise BinderError(f"{label} is not a successful native MCP response.")
    body = result.get("structuredContent")
    if not isinstance(body, dict) or body.get("ok") is not True:
        raise BinderError(f"{label} has no authoritative ok:true structuredContent.")
    return body


def _intent(path, tool, label):
    _, value = _read_json(path, label)
    if value.get("tool") != tool or not isinstance(value.get("arguments"), dict):
        raise BinderError(f"{label} must record the exact {tool} tool and its arguments.")
    return value["arguments"]


def _check_empty_clips(clips, tracks, label):
    if not isinstance(clips, list) or clips:
        raise BinderError(f"{label} must prove exactly zero Timeline clips.")
    if not isinstance(tracks, list):
        raise BinderError(f"{label} must include the actual Timeline tracks.")
    for track in tracks:
        if not isinstance(track, dict) or track.get("clips") != []:
            raise BinderError(f"{label} contains a track with clips or an unreadable track projection.")


def _read_canonical_library_project(resource, expected_id):
    saved_path = resource.get("savedDocumentPath")
    if not isinstance(saved_path, str) or not Path(saved_path).is_absolute():
        raise BinderError("The created portrait draft has no canonical saved Library path.")
    path = Path(saved_path)
    try:
        canonical = path.resolve(strict=True)
    except OSError as error:
        raise BinderError("The created portrait Library project file is missing.") from error
    if (path.is_symlink() or str(canonical) != saved_path or
            path.name != expected_id + ".json" or path.parent.name != "documents" or
            path.parent.parent.name != "timelines"):
        raise BinderError("The created Library project is not one canonical UUID-backed managed document.")
    try:
        project = json.loads(path.read_bytes())
    except (OSError, json.JSONDecodeError) as error:
        raise BinderError(f"The created Library project is not readable JSON: {error}") from error
    if not isinstance(project, dict) or project.get("timelineId") != expected_id:
        raise BinderError("The managed project document ID does not match the created resource.")
    project_tracks = project.get("tracks")
    _check_empty_clips(project.get("clips"), project_tracks, "The saved Library document")
    if not isinstance(project_tracks, list) or not isinstance(project.get("sequenceFormat"), dict):
        raise BinderError("The saved Library document is missing sequence metadata or tracks.")
    return saved_path


def validate_create_proof(create_intent, create_envelope, drafts_envelope):
    args = _intent(create_intent, "timeline.create_draft", "Portrait draft creation intent")
    expected = create_draft_request()["arguments"]
    if args != expected:
        raise BinderError("The create_draft intent does not match the fresh portrait 1080x1920, 30fps settings.")
    body = _native_structured(_read_json(create_envelope, "portrait create_draft result")[1],
                              "Portrait create_draft result")
    if (body.get("operation") != "created" or body.get("projectFileCreated") is not True or
            body.get("libraryRegistered") is not True or body.get("navigationRevertible") is not False):
        raise BinderError("The create_draft receipt does not prove a new saved Library project.")
    resource = body.get("resource")
    if not isinstance(resource, dict) or resource.get("ready") is not True:
        raise BinderError("The created portrait draft is not a ready native document.")
    if (resource.get("width") != 1080 or resource.get("height") != 1920 or
            not isinstance(resource.get("frameRate"), (int, float)) or
            isinstance(resource.get("frameRate"), bool) or resource.get("frameRate") != 30):
        raise BinderError("The create_draft receipt is not for the exact portrait 1080x1920, 30fps format.")
    resource_id = resource.get("resourceId")
    match = TIMELINE_ID_RE.fullmatch(resource_id) if isinstance(resource_id, str) else None
    if not match or resource.get("name") != TIMELINE_NAME:
        raise BinderError("The created portrait resource ID or project name is not the expected fresh draft.")
    _check_empty_clips(resource.get("clips"), resource.get("tracks"), "The create_draft resource")
    saved_path = _read_canonical_library_project(resource, match.group(1))

    drafts = _native_structured(_read_json(drafts_envelope, "portrait saved-drafts result")[1],
                                "Portrait saved-drafts result")
    values = drafts.get("drafts")
    if (drafts.get("draftCount") != 1 or drafts.get("unavailableRecordCount") != 0 or
            not isinstance(values, list) or len(values) != 1 or not isinstance(values[0], dict) or
            values[0].get("timelineId") != match.group(1) or
            values[0].get("name") != TIMELINE_NAME):
        raise BinderError("The isolated Library must contain exactly one canonical portrait draft with the created ID.")
    return resource_id, match.group(1), resource, saved_path


def validate_library_intent(path):
    arguments = _intent(path, "timeline.list_saved_drafts", "Portrait saved-drafts intent")
    if arguments != {}:
        raise BinderError("The saved-drafts proof must use the unfiltered Library listing.")


def validate_document_intent(path):
    arguments = _intent(path, "app.inspect_command", "Portrait ready-document intent")
    if arguments != {
            "commandId": "timeline.method.inspectAgentDocument()",
            "values": {},
    }:
        raise BinderError("The ready-document proof must be the native inspectAgentDocument call.")


def validate_document_proof(document_envelope, resource_id, saved_path):
    body = _native_structured(_read_json(document_envelope, "portrait document inspection")[1],
                              "Portrait document inspection")
    if (body.get("ready") is not True or body.get("resourceId") != resource_id or
            body.get("savedDocumentPath") != saved_path or body.get("width") != 1080 or
            body.get("height") != 1920 or body.get("frameRate") != 30):
        raise BinderError("The ready document inspection does not match the created portrait Library draft.")
    _check_empty_clips(body.get("clips"), body.get("tracks"), "The ready portrait document")
    document_hash = _valid_sha(body.get("documentHash"), "Portrait documentHash")
    return body, document_hash


def validate_admission_proof(admit_intent, admit_arguments_path, admit_envelope,
                             plan, resource_id, document_hash):
    arguments = _intent(admit_intent, "timeline.admit_task", "Portrait task-admission intent")
    _, recorded_arguments = _read_json(admit_arguments_path, "portrait admission arguments")
    if recorded_arguments != arguments:
        raise BinderError("The admission arguments file differs from the exact native admission intent.")
    expected = admission_request(plan, resource_id)["arguments"]
    if arguments != expected:
        raise BinderError("The fresh admission intent does not match the portrait resource and exact 104-path scope.")
    body = _native_structured(_read_json(admit_envelope, "portrait task-admission result")[1],
                              "Portrait task-admission result")
    task_id = body.get("taskId")
    if (body.get("status") != "running" or body.get("active") is not True or
            body.get("targetResourceId") != resource_id or
            not isinstance(task_id, str) or
            not re.fullmatch(r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}", task_id)):
        raise BinderError("The admission receipt does not prove a fresh running task for the portrait resource.")
    scope = body.get("nativeTimelineScope")
    if (not isinstance(scope, dict) or scope.get("targetIds") != [resource_id] or
            scope.get("assetPaths") != plan.get("assetPaths") or
            scope.get("outputDirectory") != plan.get("outputDirectory")):
        raise BinderError("The admitted native scope differs from the exact 104 portrait asset paths or output directory.")
    resources = body.get("resources")
    if not isinstance(resources, list) or len(resources) != 1:
        raise BinderError("The admission receipt must checkpoint exactly one Timeline resource.")
    row = resources[0]
    if (not isinstance(row, dict) or row.get("id") != resource_id or row.get("pending") is not False or
            row.get("beforeHash") != document_hash or row.get("currentHash") != document_hash):
        raise BinderError("Admission beforeHash/currentHash must both equal the ready portrait document hash.")
    return task_id


def _required_step(plan, step_id, command_id):
    matches = [step for step in plan["steps"] if step.get("id") == step_id]
    if len(matches) != 1 or matches[0].get("commandId") != command_id:
        raise BinderError(f"The frozen source plan is missing its exact {step_id} action.")
    return matches[0]


def _apply_three_opening_corrections(plan):
    label = _required_step(plan, "opening/desktop-handoff-label", "timeline.action.createTimelineTitle")
    headline = _required_step(plan, "opening/desktop-handoff-headline", "timeline.action.createTimelineTitle")
    image_frame = _required_step(plan, "opening/desktop-source-image-frame", "timeline.action.setTimelineClipFrame")
    label_values = label.get("values", {}).get("values", {})
    headline_values = headline.get("values", {}).get("values", {})
    if (label_values.get("positionX") != 65.0 / 1080.0 or
            headline_values.get("positionX") != 65.0 / 1080.0 or
            image_frame.get("values", {}).get("objectFit") != "fit"):
        raise BinderError("An opening geometry input changed; refusing to apply the reviewed three corrections.")
    label_values["positionX"] = label_values["positionX"] + 2.9 / 1920.0
    headline_values["positionX"] = headline_values["positionX"] + 5.8 / 1920.0
    image_frame["values"]["objectFit"] = "stretch"
    return [
        {"stepId": label["id"], "field": "values.values.positionX", "delta": 2.9 / 1920.0},
        {"stepId": headline["id"], "field": "values.values.positionX", "delta": 5.8 / 1920.0},
        {"stepId": image_frame["id"], "field": "values.objectFit", "from": "fit", "to": "stretch"},
    ]


def _assert_absent_output(path, output_directory):
    candidate = Path(path)
    if not candidate.is_absolute() or candidate.parent != Path(output_directory):
        raise BinderError("The new portrait native output filename must be a direct child of the admitted outputDirectory.")
    if candidate.name != "looksy-native-full-portrait-v7.json":
        raise BinderError("The bound plan must use the new looksy-native-full-portrait-v7.json native filename.")
    if candidate.exists() or candidate.is_symlink():
        raise BinderError("The new portrait native output filename already exists; refusing overwrite.")


def _assert_new_plan_path(path):
    candidate = Path(path)
    if candidate.name != DEFAULT_BOUND_PLAN.name or not candidate.is_absolute():
        raise BinderError("The new bound plan must be full-portrait-v7.commands.json at an absolute path.")
    try:
        parent = candidate.parent.resolve(strict=True)
        expected_parent = HERE.resolve(strict=True)
    except OSError as error:
        raise BinderError("The new bound plan directory must already exist.") from error
    if parent != expected_parent or candidate.exists() or candidate.is_symlink():
        raise BinderError("The full-portrait-v7 plan path must be new in the native-recreation-v4 directory.")


def _write_new_json(path, value):
    destination = Path(path)
    if destination.exists() or destination.is_symlink():
        raise BinderError(f"Output already exists; refusing overwrite: {destination}")
    payload = (json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n").encode("utf-8")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    fd = None
    try:
        fd = os.open(destination, flags, 0o600)
        with os.fdopen(fd, "wb") as output:
            fd = None
            output.write(payload)
            output.flush()
            os.fsync(output.fileno())
    except OSError as error:
        if fd is not None:
            os.close(fd)
        raise BinderError(f"Could not create new output exclusively: {error}") from error


def bind_portrait_plan(parent_path, create_intent_path, create_result_path,
                       drafts_intent_path, drafts_result_path, document_intent_path,
                       document_result_path, admit_intent_path, admit_arguments_path,
                       admit_result_path,
                       new_plan_path=DEFAULT_BOUND_PLAN,
                       native_output_path=DEFAULT_NATIVE_OUTPUT):
    parent_raw, parent = _read_parent(parent_path)
    resource_id, timeline_id, resource, saved_path = validate_create_proof(
        create_intent_path, create_result_path, drafts_result_path)
    validate_library_intent(drafts_intent_path)
    validate_document_intent(document_intent_path)
    document, document_hash = validate_document_proof(
        document_result_path, resource_id, saved_path)
    if resource.get("documentHash") != document_hash:
        raise BinderError("The ready portrait document hash differs from the fresh create_draft baseline.")
    task_id = validate_admission_proof(
        admit_intent_path, admit_arguments_path, admit_result_path,
        parent, resource_id, document_hash)
    _assert_absent_output(native_output_path, parent["outputDirectory"])
    _assert_new_plan_path(new_plan_path)

    bound = deepcopy(parent)
    corrections = _apply_three_opening_corrections(bound)
    bound["resourceId"] = resource_id
    bound["taskId"] = task_id
    bound["startingDocumentHash"] = document_hash
    bound["outputPath"] = str(native_output_path)
    save_step = _required_step(bound, "delivery/save-editable-project", "timeline.action.saveTimeline")
    save_step["values"]["outputPath"] = str(native_output_path)

    proof_paths = {
        "createIntent": create_intent_path,
        "createResult": create_result_path,
        "savedDraftsIntent": drafts_intent_path,
        "savedDrafts": drafts_result_path,
        "admissionIntent": admit_intent_path,
        "admissionArguments": admit_arguments_path,
        "admissionResult": admit_result_path,
        "documentInspectIntent": document_intent_path,
        "documentInspect": document_result_path,
    }
    bound["bindingProvenance"] = {
        "schema": "looksy.native-portrait-bootstrap-binding/v1",
        "status": "bootstrap_proved_plan_unexecuted",
        "parentPlanPath": str(Path(parent_path).resolve()),
        "parentPlanSha256": _sha256(parent_raw),
        "proofSha256": {
            name: _sha256(Path(path).read_bytes()) for name, path in proof_paths.items()
        },
        "createdTimelineId": timeline_id,
        "resourceId": resource_id,
        "taskId": task_id,
        "savedLibraryPath": saved_path,
        "startingDocumentHash": document_hash,
        "openingGeometryCorrections": corrections,
        "executionReceipt": None,
        "nativeAuthoringExecuted": False,
    }
    _write_new_json(new_plan_path, bound)
    return bound


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--create-result", type=Path)
    parser.add_argument("--saved-drafts-intent", type=Path)
    parser.add_argument("--saved-drafts-result", type=Path)
    parser.add_argument("--document-intent", type=Path)
    parser.add_argument("--document-result", type=Path)
    parser.add_argument("--source-plan", type=Path, default=PARENT_PLAN)
    parser.add_argument("--create-intent", type=Path)
    parser.add_argument("--admission-intent", type=Path)
    parser.add_argument("--admission-arguments", type=Path)
    parser.add_argument("--admission-result", type=Path)
    parser.add_argument("--new-plan", type=Path, default=DEFAULT_BOUND_PLAN)
    parser.add_argument("--native-output", type=Path, default=DEFAULT_NATIVE_OUTPUT)
    args = parser.parse_args(argv)

    try:
        required = (args.create_intent, args.create_result, args.saved_drafts_intent,
                    args.saved_drafts_result, args.document_intent, args.document_result,
                    args.admission_intent, args.admission_arguments, args.admission_result)
        if any(value is None for value in required):
            raise BinderError("Binding requires create, Library, ready-document, and admission intent/result proofs.")
        bind_portrait_plan(args.source_plan, args.create_intent, args.create_result,
                           args.saved_drafts_intent, args.saved_drafts_result,
                           args.document_intent, args.document_result,
                           args.admission_intent, args.admission_arguments,
                           args.admission_result,
                           args.new_plan, args.native_output)
        print(f"Bound unexecuted portrait plan: {args.new_plan}")
        return 0
    except BinderError as error:
        print(f"portrait binding refused: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
