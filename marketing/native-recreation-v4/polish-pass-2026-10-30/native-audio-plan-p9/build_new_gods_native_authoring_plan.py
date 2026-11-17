#!/usr/bin/env python3
"""Build a receipt-driven native authoring plan from the reviewed P9 score data.

This script only reads existing audio manifests, source files, and the protected
P9 snapshot. It writes one author_native-compatible JSON plan; it never calls
MCP and never edits a Timeline project.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path


PLAN_DIR = Path(__file__).resolve().parent
POLISH_DIR = PLAN_DIR.parent
REPO_ROOT = Path(__file__).resolve().parents[4]
SNAPSHOT_PATH = POLISH_DIR / "native-projects" / "looksy-polish-pass9.json"
READBACK_PATH = POLISH_DIR / "pass9-native-readback-audit.json"
COMMAND_FRAGMENT = PLAN_DIR / "new-gods-native-audio-plan-p9.commands-fragment.json"
CUE_MANIFEST = PLAN_DIR / "new-gods-native-audio-cue-manifest-p9.json"
OUTPUT_PATH = PLAN_DIR / "new-gods-native-authoring-plan-p9.template.json"

EXPECTED_RESOURCE_ID = "timeline:3db4e4fb-6737-415c-b6ee-dde766e7c237"
EXPECTED_DOCUMENT_HASH = "4d8157be5d32d94ec6ad7c01b2705175adb09a637afedc6d151148f8e2e72a48"
EXPECTED_SNAPSHOT_SHA256 = "51531c69e912a50908cb99ce5a795d697c7fb22e24a2dcf90cbc886b9a35f0b4"
OLD_MUSIC_TRACK_ID = "music-754faa1f-2e2e-43b4-af95-a77bbef7c3c1"
OLD_MASTER_CLIP_ID = "7ec6c2d2-a845-414a-8856-a186344dd3ce"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def asset_guard(path_text: str, expected_hash: str) -> dict:
    path = Path(path_text).resolve(strict=True)
    if not path.is_file():
        raise ValueError(f"Not a regular audio source file: {path}")
    actual_hash = sha256_file(path)
    if actual_hash != expected_hash:
        raise ValueError(f"Audio source hash changed: {path}")
    return {"path": str(path), "sha256": actual_hash}


def gain_step(step_id: str, clip_step_id: str, keyframes: list[dict]) -> dict:
    return {
        "id": step_id,
        "commandId": "timeline.action.setTimelineClipAnimation",
        "values": {
            "clipId": {"result": clip_step_id, "field": "createdClipId"},
            "channels": [{"parameter": "audioGainDb", "keyframes": keyframes}],
        },
    }


def main() -> None:
    readback = json.loads(READBACK_PATH.read_text())
    checkpoint_hash = sha256_file(SNAPSHOT_PATH)
    if (readback.get("documentHash") != EXPECTED_DOCUMENT_HASH or
            readback.get("fileSha256") != EXPECTED_SNAPSHOT_SHA256 or
            checkpoint_hash != EXPECTED_SNAPSHOT_SHA256 or
            readback.get("resourceId", EXPECTED_RESOURCE_ID) != EXPECTED_RESOURCE_ID):
        raise SystemExit("P9 snapshot/readback no longer matches the reviewed native guard.")

    fragment = json.loads(COMMAND_FRAGMENT.read_text())
    manifest = json.loads(CUE_MANIFEST.read_text())
    assets = fragment["assets"]
    source_guards = [
        asset_guard(assets["newGods"]["path"], assets["newGods"]["sha256"]),
        asset_guard(assets["quickAirWoosh"]["path"], assets["quickAirWoosh"]["sha256"]),
        asset_guard(assets["interfaceClick"]["path"], assets["interfaceClick"]["sha256"]),
        asset_guard(assets["location"]["path"], assets["location"]["sha256"]),
    ]
    for clip in manifest["ambienceClips"]:
        if clip["kind"] == "procedural-swell":
            source_guards.append(asset_guard(clip["sourcePath"], clip["sourceSha256"]))
    source_guards = sorted(source_guards, key=lambda item: item["path"])
    if len(source_guards) != 6 or len({item["path"] for item in source_guards}) != 6:
        raise SystemExit("The native import scope must contain exactly six unique, verified source files.")

    steps: list[dict] = []
    for track_id, desired_name, desired_role in (
        ("track-new-gods", "New Gods", "music"),
        ("track-motion-fx", "Motion FX", "effects"),
        ("track-interface-fx", "Interface FX", "effects"),
        ("track-ambience", "Ambience", "ambience"),
    ):
        steps.append({
            "id": track_id,
            "commandId": "timeline.action.createTimelineTrack",
            "values": {"kind": "audio", "name": desired_name, "audioRole": desired_role},
        })

    track_slots = {
        "newGodsMusic": ("track-new-gods", "music"),
        "motionEffects": ("track-motion-fx", "effects"),
        "interfaceEffects": ("track-interface-fx", "effects"),
        "ambience": ("track-ambience", "ambience"),
    }
    for slot, (track_step, role) in track_slots.items():
        steps.append({
            "id": f"verify-role-{slot}",
            "commandId": "timeline.action.setTimelineTrackAudioRole",
            "values": {
                "trackId": {"result": track_step, "field": "trackId"},
                "audioRole": role,
                "expectedAudioRole": role,
            },
        })
    for slot, (track_step, _) in track_slots.items():
        steps.append({
            "id": f"mute-new-{slot}-during-import",
            "commandId": "timeline.action.setTimelineTrackMuted",
            "values": {
                "trackId": {"result": track_step, "field": "trackId"},
                "muted": True,
                "expectedMuted": False,
            },
        })

    fragment_ops = fragment["operations"]
    score = fragment_ops["scoreClipTemplate"]
    score_values = dict(score["values"])
    score_values["trackId"] = {"result": track_slots[score["trackSlot"]][0], "field": "trackId"}
    score_step_id = "import-new-gods-score"
    steps.append({"id": score_step_id, "commandId": "timeline.action.createTimelineClip", "values": score_values})

    clip_gain_curves: list[tuple[str, list[dict]]] = [
        (score_step_id, fragment_ops["animationPlan"]["musicAudioGainDbKeyframes"])
    ]
    cues = manifest["cues"]
    if len(cues) != 51:
        raise SystemExit(f"Expected 51 source-backed SFX cues, found {len(cues)}.")
    for index, cue in enumerate(cues, start=1):
        slot = cue["trackSlot"]
        if slot not in track_slots:
            raise SystemExit(f"Cue {cue['displayLabel']} has an unknown native lane slot: {slot}")
        clip_step_id = f"import-{cue['cueType']}-{round(cue['atSeconds'] * 1000):06d}ms-{index:02d}"
        values = {
            "trackId": {"result": track_slots[slot][0], "field": "trackId"},
            "assetId": cue["assetId"],
            "filePath": cue["sourcePath"],
            "displayLabel": cue["displayLabel"],
            "durationSeconds": cue["durationSeconds"],
            "trimStartSeconds": cue["sourceTrimStartSeconds"],
            "trimEndSeconds": cue["sourceTrimEndSeconds"],
            "startSeconds": cue["atSeconds"],
            "posterSource": "",
        }
        steps.append({"id": clip_step_id, "commandId": "timeline.action.createTimelineClip", "values": values})
        clip_gain_curves.append((clip_step_id, [{"localSeconds": 0.0, "value": cue["audioGainDb"], "interpolation": "hold"}]))

    location = next(clip for clip in manifest["ambienceClips"] if clip["kind"] == "location")
    location_step_id = "import-location-ambience"
    steps.append({
        "id": location_step_id,
        "commandId": "timeline.action.createTimelineClip",
        "values": {
            "trackId": {"result": track_slots[location["trackSlot"]][0], "field": "trackId"},
            "assetId": "looksy-audio:location/img-1268-2-to-8",
            "filePath": location["sourcePath"],
            "displayLabel": "Location — IMG_1268 02–08",
            "durationSeconds": location["durationSeconds"],
            "trimStartSeconds": 0.0,
            "trimEndSeconds": location["durationSeconds"],
            "startSeconds": location["startSeconds"],
            "posterSource": "",
        },
    })
    clip_gain_curves.append((location_step_id, fragment_ops["animationPlan"]["locationAudioGainDbKeyframes"]))

    swells = [clip for clip in manifest["ambienceClips"] if clip["kind"] == "procedural-swell"]
    if len(swells) != 2:
        raise SystemExit(f"Expected two exact generated swell stems, found {len(swells)}.")
    for index, swell in enumerate(swells, start=1):
        clip_step_id = f"import-swell-{round(swell['startSeconds'] * 1000):06d}ms-{index}"
        steps.append({
            "id": clip_step_id,
            "commandId": "timeline.action.createTimelineClip",
            "values": {
                "trackId": {"result": track_slots[swell["trackSlot"]][0], "field": "trackId"},
                "assetId": "looksy-audio:swells/new-gods-swell-" + str(round(swell["startSeconds"] * 10)),
                "filePath": swell["sourcePath"],
                "displayLabel": f"Transition swell {swell['startSeconds']:.1f}s",
                "durationSeconds": swell["durationSeconds"],
                "trimStartSeconds": 0.0,
                "trimEndSeconds": swell["durationSeconds"],
                "startSeconds": swell["startSeconds"],
                "posterSource": "",
            },
        })
        clip_gain_curves.append((clip_step_id, [{"localSeconds": 0.0, "value": swell["nativeGainDb"], "interpolation": "hold"}]))

    source_clip_steps = [step for step in steps if step["commandId"] == "timeline.action.createTimelineClip"]
    if len(source_clip_steps) != 55:
        raise SystemExit(f"Expected 55 total native audio clips, found {len(source_clip_steps)}.")

    for index, (clip_step_id, keyframes) in enumerate(clip_gain_curves, start=1):
        steps.append(gain_step(f"set-gain-{index:02d}", clip_step_id, keyframes))

    steps.append({
        "id": "mute-original-baked-mix",
        "commandId": "timeline.action.setTimelineTrackMuted",
        "values": {"trackId": OLD_MUSIC_TRACK_ID, "muted": True, "expectedMuted": False},
    })
    for slot, (track_step, _) in track_slots.items():
        steps.append({
            "id": f"enable-new-{slot}",
            "commandId": "timeline.action.setTimelineTrackMuted",
            "values": {
                "trackId": {"result": track_step, "field": "trackId"},
                "muted": False,
                "expectedMuted": True,
            },
        })

    if len(steps) != 127:
        raise SystemExit(f"Unexpected action count: {len(steps)} (expected 127).")

    plan = {
        "schema": "looksy.native-authoring-plan/v1",
        "name": "New Gods native editable score and cue rebuild from P9",
        "status": "proposed-not-applied",
        "productionCallsMade": False,
        "resourceId": EXPECTED_RESOURCE_ID,
        "startingDocumentHash": EXPECTED_DOCUMENT_HASH,
        "taskId": None,
        "taskIdRequiredAtApply": True,
        "sequence": {"width": 1920, "height": 1080, "frameRate": 30, "durationSeconds": 298.0},
        "planningSnapshot": {
            "path": str(SNAPSHOT_PATH.resolve()),
            "sha256": EXPECTED_SNAPSHOT_SHA256,
            "documentHash": EXPECTED_DOCUMENT_HASH,
        },
        "expectedCurrentAudioState": {
            "musicTrackId": OLD_MUSIC_TRACK_ID,
            "musicTrackRole": "music",
            "musicTrackBus": "music",
            "musicTrackMuted": False,
            "masterClipId": OLD_MASTER_CLIP_ID,
            "masterAssetId": "native-trailer:audio/creative-polish-v4-master.wav",
            "masterStartSeconds": 0.0,
            "masterDurationSeconds": 298.0,
            "masterTrimStartSeconds": 0.0,
            "masterTrimEndSeconds": 298.0,
            "audio1TrackId": "audio-1",
            "audio1Role": "dialogue",
            "audio1Bus": "main",
            "audio1ClipCount": 0,
        },
        "assetGuards": source_guards,
        "requiredAssetPathScope": [item["path"] for item in source_guards],
        "requireExactAssetPathScope": True,
        "sourceImportCount": 55,
        "sourceImportReceiptField": "createdClipId",
        "batchRules": {
            "createTimelineClipIsIndividualCall": True,
            "neverReplayUnresolvedCreateClipIntent": True,
            "setTimelineClipAnimationMaxOperations": 16,
            "noNativeProjectJsonWrites": True,
        },
        "sourceLicenseEvidence": {
            "newGods": assets["newGods"]["licenseEvidence"],
            "foley": assets["quickAirWoosh"]["licenseEvidence"],
            "location": assets["location"]["licenseEvidence"],
        },
        "steps": steps,
    }
    OUTPUT_PATH.write_text(json.dumps(plan, indent=2, ensure_ascii=False) + "\n")
    print(f"Wrote {OUTPUT_PATH} with {len(steps)} typed native steps and {len(source_guards)} verified asset guards.")


if __name__ == "__main__":
    main()
