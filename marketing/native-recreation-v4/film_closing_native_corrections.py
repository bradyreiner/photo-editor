"""Prepare additive native frame-shadow corrections for FilmPolish phase 7.

This offline builder does not edit historical plans, receipts, or a live
Timeline. The output keeps symbolic plan-result references and remains
unexecuted until it is bound to the completed chapter authoring receipt.
"""

from copy import deepcopy
from pathlib import Path
import hashlib
import json


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT.parent / "animated-trailer/src/creative-launch/FilmPolish.tsx"


def _sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(orientation):
    if orientation not in ("landscape", "portrait"):
        raise ValueError("orientation must be landscape or portrait")
    plan_path = ROOT / f"film-closing-v3-{orientation}.commands.json"
    plan = json.loads(plan_path.read_text())
    steps = {step["id"]: step for step in plan["steps"]}
    scale = 1.1 if orientation == "landscape" else 1.0
    correction_specs = (
        ("framing-landscape-film", "framing-landscape-film-frame",
         "#88000000", 30.0),
        ("framing-social-film", "framing-social-film-frame",
         "#99000000", 25.0),
    )
    corrected_steps = []
    for clip_id, frame_step_id, color, offset_y in correction_specs:
        frame_step = steps.get(frame_step_id)
        if frame_step is None or frame_step["commandId"] != "timeline.action.setTimelineClipFrame":
            raise ValueError(f"Expected the native frame command {frame_step_id}")
        values = deepcopy(frame_step["values"])
        if values.get("clipId") != {"result": clip_id, "field": "createdClipId"}:
            raise ValueError(f"The frame command {frame_step_id} no longer targets {clip_id}")
        values["clipId"] = {
            "result": "film-closing/" + clip_id,
            "field": "createdClipId",
        }
        values["frameShadow"] = {
            "enabled": True,
            "color": color,
            "offsetX": 0.0,
            "offsetY": offset_y * scale,
            "blurRadius": 70.0 * scale,
            "spread": 0.0,
        }
        corrected_steps.append({
            "id": "film-closing-parity/" + clip_id + "-shadow",
            "commandId": "timeline.action.setTimelineClipFrame",
            "values": values,
        })
    return {
        "schema": "looksy.native-explicit-parity-repairs/v1",
        "parentPlanSha256": _sha256(plan_path),
        "parentReceiptSha256": None,
        "sourceSha256": _sha256(SOURCE),
        "reason": (
            "FilmPolish phase 7 uses CSS box shadows on the landscape frame and "
            "phone frame. Native frameShadow is now persisted and rendered in "
            "preview/export; values are mapped through the source Stage scale. "
            "Symbolic IDs must be bound against the completed authoring receipt."
        ),
        "steps": corrected_steps,
        "execution": "unexecuted; requires completed chapter receipt binding",
    }


if __name__ == "__main__":
    for orientation in ("landscape", "portrait"):
        output = ROOT / f"film-closing-shadow-corrections-v1-{orientation}.commands-fragment.json"
        if output.exists():
            raise SystemExit(f"Refusing to overwrite immutable correction fragment: {output}")
        output.write_text(json.dumps(build(orientation), indent=2) + "\n")
        print(output)
