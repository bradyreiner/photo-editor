"""Derive the crop-corrected, still-unexecuted portrait plan from bound v7."""

from copy import deepcopy
from pathlib import Path
import argparse
import hashlib
import json
import math
import os
import sys

import bind_fresh_portrait_plan as binder


HERE = Path(__file__).resolve().parent
BOUND_PLAN = HERE / "full-portrait-v7.commands.json"
BOUND_PLAN_SHA256 = "958a51bd2e501b9adcf2b8f3358df2cac38bc43b91687565889420ad55175893"
REFERENCE_LANDSCAPE_PLAN = HERE / "full-landscape-v14-crop-corrected-parent.commands.json"
REFERENCE_LANDSCAPE_SHA256 = "25dc84ce10b0468aefa0593d96d290ecbf01fd78f9a318217c0a01510682d4ce"
EXPECTED_SOURCE_HELPER_SHA256 = "3ae034b3f1a96987b20d30bebe74417b7c1a5b67880101ef9378a14c0252f969"
DEFAULT_OUTPUT_PLAN = HERE / "full-portrait-v8-crop-corrected.commands.json"
DEFAULT_NATIVE_OUTPUT = HERE / "projects" / "looksy-native-full-portrait-v8.json"
FRAME_COMMAND = "timeline.action.setTimelineClipFrame"
SAVE_COMMAND = "timeline.action.saveTimeline"
SAVE_STEP = "delivery/save-editable-project"
EXPECTED_COMPOSITION = {"width": 1080, "height": 1920, "fps": 30}


class CropPlanError(RuntimeError):
    """Raised when a bound portrait plan cannot be safely derived."""


def _sha256(data):
    return hashlib.sha256(data).hexdigest()


def _read_json(path, label):
    try:
        raw = Path(path).read_bytes()
        value = json.loads(raw)
    except (OSError, json.JSONDecodeError) as error:
        raise CropPlanError(f"Could not read {label}: {error}") from error
    if not isinstance(value, dict):
        raise CropPlanError(f"{label} must be a JSON object.")
    return raw, value


def _load_bound_v7(path=BOUND_PLAN):
    candidate = Path(path)
    if not candidate.is_absolute() or candidate.resolve() != BOUND_PLAN.resolve():
        raise CropPlanError("Crop derivation must use the immutable full-portrait-v7 bound plan.")
    raw, plan = _read_json(candidate, "bound portrait v7 plan")
    if _sha256(raw) != BOUND_PLAN_SHA256:
        raise CropPlanError("The proof-bound v7 plan changed; refusing to derive from a modified parent.")
    provenance = plan.get("bindingProvenance")
    if (not isinstance(provenance, dict) or
            provenance.get("schema") != "looksy.native-portrait-bootstrap-binding/v1" or
            provenance.get("status") != "bootstrap_proved_plan_unexecuted" or
            provenance.get("parentPlanSha256") != binder.PARENT_SHA256 or
            provenance.get("resourceId") != plan.get("resourceId") or
            provenance.get("taskId") != plan.get("taskId") or
            provenance.get("startingDocumentHash") != plan.get("startingDocumentHash") or
            provenance.get("nativeAuthoringExecuted") is not False or
            provenance.get("executionReceipt") is not None):
        raise CropPlanError("The v7 binding provenance is missing, inconsistent, or no longer unexecuted.")
    if (plan.get("composition") != EXPECTED_COMPOSITION or
            len(plan.get("steps", [])) != 3642 or
            plan.get("outputPath") != str(binder.DEFAULT_NATIVE_OUTPUT)):
        raise CropPlanError("The bound v7 plan no longer matches the portrait bootstrap contract.")
    return raw, plan


def _landscape_crop_reference(path=REFERENCE_LANDSCAPE_PLAN):
    candidate = Path(path)
    if not candidate.is_absolute() or candidate.resolve() != REFERENCE_LANDSCAPE_PLAN.resolve():
        raise CropPlanError("The crop rule must be grounded in the reviewed landscape v14 correction plan.")
    raw, reference = _read_json(candidate, "reviewed landscape crop correction")
    if _sha256(raw) != REFERENCE_LANDSCAPE_SHA256:
        raise CropPlanError("The reviewed landscape crop-correction reference changed.")
    provenance = reference.get("cropRepairProvenance")
    if (not isinstance(provenance, dict) or
            provenance.get("sourceHelperSha256") != EXPECTED_SOURCE_HELPER_SHA256):
        raise CropPlanError("The reviewed crop-correction reference has unexpected source provenance.")
    rows = provenance.get("changed")
    if (not isinstance(rows, list) or len(rows) != 16 or
            any(not isinstance(row, dict) or not isinstance(row.get("id"), str)
                for row in rows)):
        raise CropPlanError("The reviewed crop correction must name exactly 16 Canvas Agent frames.")
    ids = [row["id"] for row in rows]
    if len(set(ids)) != 16 or any(not value.startswith("canvas-agent/") for value in ids):
        raise CropPlanError("The reviewed correction contains duplicate or out-of-scope frame IDs.")
    return raw, provenance, rows


def _step_map(plan):
    result = {}
    for step in plan.get("steps", []):
        if isinstance(step, dict) and isinstance(step.get("id"), str):
            if step["id"] in result:
                raise CropPlanError(f"Duplicate stable action ID in portrait plan: {step['id']}")
            result[step["id"]] = step
    return result


def _validate_insets(values, row):
    insets = [values.get(name) for name in ("cropLeft", "cropTop", "cropRight", "cropBottom")]
    expected = row.get("oldInsets")
    if (not isinstance(expected, list) or len(expected) != 4 or insets != expected or
            any(isinstance(value, bool) or not isinstance(value, (int, float)) or
                not math.isfinite(value) or value < 0 or value > 1 for value in insets)):
        raise CropPlanError(f"Portrait crop insets differ from reviewed landscape evidence for {row.get('id')}.")
    return insets


def build_crop_corrected_plan(bound_plan, bound_raw, reference_provenance,
                              reference_rows, native_output_path=DEFAULT_NATIVE_OUTPUT):
    output = Path(native_output_path)
    if (not output.is_absolute() or output.parent != Path(bound_plan["outputDirectory"]) or
            output.name != "looksy-native-full-portrait-v8.json" or
            output.exists() or output.is_symlink()):
        raise CropPlanError("The new v8 native output must be an absent direct child of the admitted outputDirectory.")

    by_id = _step_map(bound_plan)
    reviewed_ids = {row["id"] for row in reference_rows}
    portrait_crop_ids = {
        step["id"] for step in bound_plan["steps"]
        if (step.get("commandId") == FRAME_COMMAND and
            isinstance(step.get("values"), dict) and
            step["id"].startswith("canvas-agent/") and
            any(step["values"].get(key, 0) != 0
                for key in ("cropLeft", "cropTop", "cropRight", "cropBottom")))
    }
    if portrait_crop_ids != reviewed_ids:
        raise CropPlanError("Portrait Canvas Agent crop IDs do not exactly match the 16 reviewed correction IDs.")

    bound = deepcopy(bound_plan)
    bound_steps = _step_map(bound)
    changed = []
    for row in reference_rows:
        step = bound_steps.get(row["id"])
        if (not isinstance(step, dict) or step.get("commandId") != FRAME_COMMAND or
                not isinstance(step.get("values"), dict)):
            raise CropPlanError(f"Reviewed crop target is not a typed native frame action: {row['id']}")
        values = step["values"]
        old_left, old_top, old_right, old_bottom = _validate_insets(values, row)
        expected_rectangle = [old_left, old_top, 1.0 - old_right, 1.0 - old_bottom]
        if row.get("sourceRectangle") != expected_rectangle:
            raise CropPlanError(f"Landscape crop provenance has an invalid source rectangle for {row['id']}.")
        values["cropRight"] = expected_rectangle[2]
        values["cropBottom"] = expected_rectangle[3]
        changed.append({
            "id": row["id"],
            "oldInsets": [old_left, old_top, old_right, old_bottom],
            "sourceRectangle": expected_rectangle,
        })

    bound["outputPath"] = str(output)
    save_matches = [step for step in bound["steps"] if step.get("id") == SAVE_STEP]
    if len(save_matches) != 1 or save_matches[0].get("commandId") != SAVE_COMMAND:
        raise CropPlanError("The bound plan has no unique native save action for the v8 output.")
    save_matches[0].setdefault("values", {})["outputPath"] = str(output)
    bound["cropRepairProvenance"] = {
        "schema": "looksy.native-canvas-inset-crop-conversion/v1",
        "status": "derived_from_proof_bound_v7_unexecuted",
        "parentBoundPlanPath": str(BOUND_PLAN),
        "parentBoundPlanSha256": _sha256(bound_raw),
        "sourcePortraitPlanPath": str(binder.PARENT_PLAN),
        "sourcePortraitPlanSha256": binder.PARENT_SHA256,
        "reviewedLandscapeReferencePath": str(REFERENCE_LANDSCAPE_PLAN),
        "reviewedLandscapeReferenceSha256": REFERENCE_LANDSCAPE_SHA256,
        "sourceHelperSha256": reference_provenance["sourceHelperSha256"],
        "rule": "cropRight=1-oldRight and cropBottom=1-oldBottom; preserve cropLeft and cropTop",
        "correctedFrameCount": len(changed),
        "changed": changed,
        "execution": "unexecuted corrections",
    }
    return bound


def _assert_new_plan_path(path):
    candidate = Path(path)
    if not candidate.is_absolute() or candidate.name != DEFAULT_OUTPUT_PLAN.name:
        raise CropPlanError("The derived plan must be full-portrait-v8-crop-corrected.commands.json.")
    try:
        parent = candidate.parent.resolve(strict=True)
        expected_parent = HERE.resolve(strict=True)
    except OSError as error:
        raise CropPlanError("The derived plan directory must already exist.") from error
    if parent != expected_parent or candidate.exists() or candidate.is_symlink():
        raise CropPlanError("The derived v8 plan must be new in the native-recreation-v4 directory.")


def derive_crop_corrected_plan(bound_path=BOUND_PLAN, output_plan_path=DEFAULT_OUTPUT_PLAN,
                               native_output_path=DEFAULT_NATIVE_OUTPUT):
    bound_raw, bound_plan = _load_bound_v7(bound_path)
    _, reference_provenance, reference_rows = _landscape_crop_reference()
    _assert_new_plan_path(output_plan_path)
    derived = build_crop_corrected_plan(bound_plan, bound_raw, reference_provenance,
                                        reference_rows, native_output_path)
    binder._write_new_json(output_plan_path, derived)
    return derived


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bound-plan", type=Path, default=BOUND_PLAN)
    parser.add_argument("--new-plan", type=Path, default=DEFAULT_OUTPUT_PLAN)
    parser.add_argument("--native-output", type=Path, default=DEFAULT_NATIVE_OUTPUT)
    args = parser.parse_args(argv)
    try:
        derive_crop_corrected_plan(args.bound_plan, args.new_plan, args.native_output)
        print(f"Wrote unexecuted crop-corrected portrait plan: {args.new_plan}")
        return 0
    except (CropPlanError, binder.BinderError) as error:
        print(f"portrait crop derivation refused: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
