#!/usr/bin/env python3
"""Build an offline, P18-bound native film-caption proposal and readback audit."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[4]
POLISH = ROOT / "marketing/native-recreation-v4/polish-pass-2026-10-30"
OUT = POLISH / "p21-film-layout"
PROJECT = POLISH / "native-projects/looksy-polish-pass18-library-base-fixed.json"
CHECKPOINT = POLISH / "checkpoint-pass18-library-base-fixed-result.json"
P16 = POLISH / "native-projects/looksy-polish-pass16-source-label.json"
P15 = POLISH / "native-projects/looksy-polish-pass15-final-graphics.json"
SOURCE_FILES = {
    "FilmPolish.tsx": ROOT / "marketing/animated-trailer/src/creative-launch/FilmPolish.tsx",
    "Kit.tsx": ROOT / "marketing/animated-trailer/src/creative-launch/Kit.tsx",
    "CreativeFilm.tsx": ROOT / "marketing/animated-trailer/src/creative-launch/CreativeFilm.tsx",
}

RESOURCE_ID = "timeline:3db4e4fb-6737-415c-b6ee-dde766e7c237"
EXPECTED_DOC_HASH = "fab7d7b461d52e134aca1b29afaf49f8a5d778f20e284d4076c1910e653aa1a9"
EXPECTED_PROJECT_SHA = "d31b7c3e3e9101334a13774c3ed976ff8162a7303e040645d55fe37f05873419"

# Existing clip IDs are stable P18 identities. The setter action only touches
# title text / center Y; full curve hashes are captured below as hard guards.
TARGETS = [
    ("phase1-heading", "4d69b135-5c47-40b8-96cf-a81ff09265c4", {
        "text": "Build the\nedit.\nKeep it\nmoving.", "positionY": 0.4508,
    }),
    ("phase1-body", "0547ade9-5644-46f2-8ff1-607575ef2f64", {
        "positionY": 0.7111,
    }),
    ("phase2-heading", "b45c9653-8b4f-452f-a1af-fc00a0126e19", {
        "text": "Motion, with\nprecise\ncontrol.", "positionY": 0.4082,
    }),
    ("phase2-body", "289dc200-16c2-4f06-9f09-278ce4ebeea3", {
        "text": "Position, scale, opacity and\neasing. The animation\nfollows your keyframes.",
        "positionY": 0.6260,
    }),
    ("phase3-heading", "513cee1e-eb98-48a8-af8f-dae107d42add", {
        "text": "Build the\nlook.\nLayer the\neffects.", "positionY": 0.4508, "fontSize": 91,
    }),
    ("phase3-body", "0c40d5fd-86c5-400b-a810-d6a1ded97088", {
        "text": "A native color grade and a\nmonochrome effect. The\nsame footage, visibly\ntransformed.",
        "positionY": 0.7111,
    }),
    ("phase4-heading", "0ff7f817-bb6a-4416-b5a8-baf4a3a4ed23", {
        "text": "Picture\nmeets\nsound.", "positionY": 0.4082,
    }),
    ("phase4-body", "ad9fafba-ee94-483b-a1a8-7e56cfc77335", {
        "text": "Pull down the music and\nhear the location. Shape\nthe mix alongside the edit.",
        "positionY": 0.6260,
    }),
    ("phase6-body", "242435a0-3709-46c3-8866-ddabe68a4119", {
        "text": "Gaussian scenes with a\nmoving camera. Another\ndimension in the creative\ntoolbox.",
        "positionY": 0.5622,
    }),
    ("phase7-body", "dd7ef618-d8c2-4948-b9fe-7b18e6c04277", {
        "text": "Landscape productions\nand independently framed\nvertical videos.",
        "positionY": 0.5409,
    }),
]

FOOTERS = [
    {
        "id": "create-default-footer-left",
        "trackId": "graphics-d77585ff-bc9d-42e5-9908-a2494476c9b1",
        "text": "FROM FIRST CUT TO FINAL FRAME.",
        "alignment": "left",
        "positionX": 0.042708333333333334,
    },
    {
        "id": "create-default-footer-right",
        "trackId": "graphics-8e70fbee-995f-47ca-b8aa-644b39cbe9f3",
        "text": "COLOR · MOTION · LAYERS · AUDIO",
        "alignment": "right",
        "positionX": 0.9572916666666667,
    },
]

FIELD_GUARD = [
    "mediaType", "assetId", "startSeconds", "durationSeconds", "trimStartSeconds",
    "trimEndSeconds", "opacity", "titleText", "titlePositionX", "titlePositionY",
    "titleFontFamily", "titleFontSize", "titleFontWeight", "titleItalic",
    "titleLetterSpacing", "titleLineHeight", "titleColor", "titleBackgroundColor",
    "titleAlignment", "titleShapeType", "titleShapeWidth", "titleShapeHeight",
    "keyframes", "outputVariantOverrides",
]


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def canonical_sha(value: Any) -> str:
    payload = json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(",", ":"))
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def all_clips(doc: dict[str, Any]) -> tuple[dict[str, dict[str, Any]], dict[str, str]]:
    clips: dict[str, dict[str, Any]] = {}
    parent_track: dict[str, str] = {}
    for track in doc["tracks"]:
        for clip in track.get("clips", []):
            clips[clip["clipId"]] = clip
            parent_track[clip["clipId"]] = track["trackId"]
    return clips, parent_track


def range_clips(doc: dict[str, Any], start: float, end: float) -> dict[str, tuple[str, dict[str, Any]]]:
    result: dict[str, tuple[str, dict[str, Any]]] = {}
    for track in doc["tracks"]:
        for clip in track.get("clips", []):
            clip_start = float(clip["startSeconds"])
            clip_end = clip_start + float(clip["durationSeconds"])
            if clip_start < end and clip_end > start:
                result[clip["clipId"]] = (track["trackId"], clip)
    return result


def normalized_record(record: tuple[str, dict[str, Any]]) -> dict[str, Any]:
    track_id, clip = record
    return {"trackId": track_id, "clip": clip}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def footer_values(text: str, alignment: str, x: float) -> dict[str, Any]:
    return {
        "text": text,
        "fontFamily": "Space Mono",
        "fontSize": 17,
        "fontWeight": 400,
        "italic": False,
        "letterSpacing": 1,
        "lineHeight": 1.2,
        "color": "#ffaeb6c3",
        "alignment": alignment,
        "positionX": x,
        "positionY": 0.9796296296296296,
        "backgroundColor": "#00000000",
        "shadowEnabled": False,
        "shadowColor": "#99000000",
        "shadowOffsetX": 2,
        "shadowOffsetY": 3,
        "backgroundPadding": 0,
        "backgroundRadius": 8,
        "safeMarginX": 0.08,
        "safeMarginY": 0.08,
    }


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    project_bytes = PROJECT.read_bytes()
    doc = json.loads(project_bytes)
    checkpoint = json.loads(CHECKPOINT.read_text())
    project_sha = hashlib.sha256(project_bytes).hexdigest()
    require(project_sha == EXPECTED_PROJECT_SHA, "P18 planning snapshot file hash changed.")
    require(checkpoint["documentHash"] == EXPECTED_DOC_HASH, "P18 document hash receipt changed.")
    require(checkpoint["fileSha256"] == project_sha and checkpoint["readbackVerified"],
            "P18 checkpoint receipt does not prove the exact planning file.")
    require(checkpoint["resourceId"] == RESOURCE_ID, "Unexpected P18 timeline resource.")
    require(doc["activeOutputVariant"] == "landscape", "P18 is not bound to landscape output.")
    require(doc["sequenceFormat"] == {"audioSampleRate": 48000, "frameRate": 30,
                                      "height": 1080, "width": 1920},
            "Unexpected sequence geometry for this caption mapping.")

    clips, parent_track = all_clips(doc)
    require(len(doc["tracks"]) == 196, "P18 track inventory changed.")
    require(len(clips) == 1363, "P18 clip inventory changed.")
    targets = []
    steps = []
    for step_id, clip_id, requested in TARGETS:
        require(clip_id in clips, f"Missing P18 target {clip_id}.")
        clip = clips[clip_id]
        require(clip["mediaType"] == "title", f"Target is no longer a title: {clip_id}.")
        require(clip["startSeconds"] in (196, 208, 218, 226, 240, 244),
                f"Unexpected chapter start for {clip_id}.")
        require("landscape" in clip.get("outputVariantOverrides", {}),
                f"Target lacks the landscape title override: {clip_id}.")
        snapshot = {field: clip.get(field) for field in FIELD_GUARD if field != "keyframes"}
        snapshot["trackId"] = parent_track[clip_id]
        snapshot["keyframesSha256"] = canonical_sha(clip.get("keyframes", []))
        snapshot["landscapeOverrideSha256"] = canonical_sha(
            clip["outputVariantOverrides"]["landscape"])
        targets.append({"clipId": clip_id, "stepId": step_id, "preconditionSnapshot": snapshot})
        steps.append({
            "id": step_id,
            "commandId": "timeline.action.setTimelineClipTitle",
            "values": {"clipId": clip_id, "values": requested},
        })

    # The candidate lanes contain earlier chapters, but neither has a clip
    # crossing into the 196..232 footer span. They are existing visible video
    # lanes and must remain unlocked when the root later binds this plan.
    track_by_id = {track["trackId"]: track for track in doc["tracks"]}
    track_preconditions = []
    for item in FOOTERS:
        track = track_by_id.get(item["trackId"])
        require(track is not None, f"Missing footer destination lane {item['trackId']}.")
        require(track["kind"] == "video" and not track["locked"] and track["visible"],
                f"Footer destination lane is not writable/visible: {item['trackId']}.")
        overlaps = []
        for clip in track.get("clips", []):
            clip_start = float(clip["startSeconds"])
            clip_end = clip_start + float(clip["durationSeconds"])
            if clip_start < 232 and clip_end > 196:
                overlaps.append(clip["clipId"])
        require(not overlaps, f"Footer lane is occupied in 196..232: {item['trackId']} {overlaps}.")
        track_preconditions.append({
            "trackId": track["trackId"],
            "laneIndex": track["laneIndex"],
            "kind": track["kind"],
            "locked": track["locked"],
            "visible": track["visible"],
            "requiredEmptyInterval": [196.0, 232.0],
        })
        steps.append({
            "id": item["id"],
            "commandId": "timeline.action.createTimelineTitle",
            "values": {
                "trackId": item["trackId"],
                "templateKey": "basic",
                "startSeconds": 196.0,
                "durationSeconds": 36.0,
                "values": footer_values(item["text"], item["alignment"], item["positionX"]),
            },
        })

    existing_exact_footers = []
    for track in doc["tracks"]:
        for clip in track.get("clips", []):
            text = clip.get("titleText", "")
            if text in {"FROM FIRST CUT TO FINAL FRAME.", "COLOR · MOTION · LAYERS · AUDIO"}:
                existing_exact_footers.append({
                    "clipId": clip["clipId"], "trackId": track["trackId"],
                    "startSeconds": clip["startSeconds"],
                    "durationSeconds": clip["durationSeconds"], "text": text,
                })
    require(not any(row["startSeconds"] < 232 and row["startSeconds"] + row["durationSeconds"] > 196
                    for row in existing_exact_footers),
            "An exact default footer already overlaps the proposed span.")

    p16_doc = json.loads(P16.read_text())
    p15_doc = json.loads(P15.read_text())
    p18_range = range_clips(doc, 184, 252)
    p16_range = range_clips(p16_doc, 184, 252)
    p15_range = range_clips(p15_doc, 184, 252)
    require(set(p18_range) == set(p16_range), "P16 proof inventory differs from P18 in the target span.")
    p16_differences = [clip_id for clip_id in p18_range
                       if normalized_record(p18_range[clip_id]) != normalized_record(p16_range[clip_id])]
    require(not p16_differences, f"P16 proof target state differs from P18: {p16_differences}.")
    p15_differences = [clip_id for clip_id in p18_range
                       if clip_id not in p15_range or
                       normalized_record(p18_range[clip_id]) != normalized_record(p15_range[clip_id])]

    source_hashes = {name: sha256(path) for name, path in SOURCE_FILES.items()}
    plan = {
        "schema": "looksy.native-authoring-plan/v1",
        "name": "P21 Film caption line-wrap, vertical rhythm, and missing footer repair",
        "status": "proposed-unbound-not-applied",
        "productionCallsMade": False,
        "resourceId": RESOURCE_ID,
        "taskId": None,
        "taskIdRequiredAtApply": True,
        "startingDocumentHash": None,
        "startingDocumentHashRequiredAtApply": True,
        "planningSnapshot": {
            "path": str(PROJECT), "sha256": project_sha,
            "documentHash": EXPECTED_DOC_HASH,
            "trackCount": len(doc["tracks"]), "clipCount": len(clips),
            "activeOutputVariant": "landscape",
        },
        "creativeReference": {
            "sourceFiles": [
                {"path": str(path), "sha256": source_hashes[name]}
                for name, path in SOURCE_FILES.items()
            ],
            "chapterGlobalStartSeconds": 184,
            "phaseBoundariesGlobalSeconds": [184, 196, 208, 218, 226, 232, 240, 244, 252],
            "frameRate": 30,
            "captionMethod": "Keep source copy/type styling and existing native clips; add hard line breaks wherever Remotion's 470px/445px browser text boxes wrap so native full-canvas title rendering matches those source lines.",
            "bodyCenterYMethod": "Place each title/body text block by the source Caption flow: 254px top, 21px label line, 24px heading gap, 28px detail gap; use the source type line heights and the observed source line counts. The phase3 native heading was 76px although the source Caption is fixed at 91px; set it to the source 91px. Values are first-pass A/B targets, not pixel-proof claims.",
            "noChange": [
                "No clip timing, source clock, playhead, image, grade, effect, audio, footage, mask, frame, or animation channel changes.",
                "No edits in the special 184-196 opening or 232-240 delivery-proof phase.",
                "No change to the phase6 custom left credit or the already-present phase6/phase7 right footers.",
            ],
        },
        "nativeActionContract": {
            "commands": ["timeline.action.setTimelineClipTitle", "timeline.action.createTimelineTitle"],
            "stepCount": len(steps),
            "batching": "The 12 typed actions fit the existing 16-operation synchronous native batch contract used by the P13/P17 authoring receipts. If the current registered schema differs, stop and inspect; do not translate this fragment to raw project-file edits.",
            "createdClipIdBinding": "The two createTimelineTitle results are receipt-derived. If either create outcome is uncertain, inspect task and append-only receipt before recovery; never replay the create call blindly.",
            "saveVariantRequirement": "Bind only while activeOutputVariant is landscape. The native save serializes the live title fields into the active landscape title overrides; verify the saved readback's landscape titlePositionY fields match the main values. Preserve every non-landscape variant byte-for-byte.",
        },
        "executionPreconditions": {
            "freshNativeDocumentHashAndTaskRequired": True,
            "resourceMustMatch": RESOURCE_ID,
            "activeOutputVariantMustBe": "landscape",
            "planningTargetFieldsMustMatchExactly": True,
            "preserveExistingKeyframesBySha256": True,
            "preserveAllExistingOutputVariantOverridesExceptActiveLandscapePositionYForTargets": True,
            "footerDestinationTracks": track_preconditions,
            "exactFooterSearchResult": existing_exact_footers,
            "abortOnAnyAmbiguousTaskMutation": True,
            "permitUnrelatedAcceptedEditsOutsideTargets": True,
            "mustNotRequireP18WholeFileHashAtApply": "Capture the live document hash and task ID after the parent has finished any accepted edits; allow unrelated outside-range state only after confirming each target snapshot and both empty footer intervals still match.",
        },
        "targetPreconditions": targets,
        "steps": steps,
        "postconditions": {
            "existingTargets": 10,
            "createdFooterTitles": 2,
            "newTrackCount": 0,
            "expectedFooterSpan": [196.0, 232.0],
            "expectedCaptionPhases": {
                "196-208": {"headingLines": 4, "bodyLines": 3},
                "208-218": {"headingLines": 3, "bodyLines": 3},
                "218-226": {"headingLines": 4, "headingFontSize": 91, "bodyLines": 4},
                "226-232": {"headingLines": 3, "bodyLines": 3},
                "240-244": {"headingLines": 2, "bodyLines": 4},
                "244-252": {"headingLines": 2, "bodyLines": 3},
            },
            "mainAndLandscapeTitlePositionsAgreeAfterSave": True,
            "allExistingTargetKeyframeHashesUnchanged": True,
            "outsideTargetClipsAndAllTrackMetadataUnchanged": True,
        },
        "verificationStoryboard": [
            {"seconds": 184.0, "frame": 5520, "purpose": "opening-to-Film boundary; both source/native show only the light field at this exact cut"},
            {"seconds": 199.5, "frame": 5985, "purpose": "phase1 settled caption and footer; compare title wrapping/body center"},
            {"seconds": 206.0, "frame": 6180, "purpose": "late phase1 caption remains legible over After Dark"},
            {"seconds": 210.0, "frame": 6300, "purpose": "phase2 title/body line breaks and keyframe demo"},
            {"seconds": 218.5, "frame": 6555, "purpose": "phase3 four-line title and four-line detail"},
            {"seconds": 229.0, "frame": 6870, "purpose": "phase4 title/body and audio lane readout"},
            {"seconds": 231.9667, "frame": 6959, "purpose": "last phase4 frame; both default footers still visible"},
            {"seconds": 232.0, "frame": 6960, "purpose": "DeliveryProof begins; the two default footer clips are out of range"},
            {"seconds": 240.5, "frame": 7215, "purpose": "phase6 custom left credit, default right footer, four-line detail"},
            {"seconds": 244.5, "frame": 7335, "purpose": "phase7 default footer pair and corrected detail copy"},
            {"seconds": 247.5, "frame": 7425, "purpose": "settled phase7 wrap/body vertical comparison"},
            {"seconds": 251.9667, "frame": 7559, "purpose": "last phase7 frame; ensure footers persist through cut"},
            {"seconds": 252.0, "frame": 7560, "purpose": "next chapter begins; no phase7 caption/footer overhang"},
        ],
    }
    (OUT / "p21-film-caption-layout.commands-fragment.json").write_text(
        json.dumps(plan, indent=2, ensure_ascii=False) + "\n")

    evidence_dirs = {
        "P16ClosedNative": OUT / "native-proof-png",
        "FullResolutionSource": OUT / "native-reference-png",
    }
    evidence_hashes = {}
    for label, directory in evidence_dirs.items():
        for path in sorted(directory.glob("*.png")):
            evidence_hashes[f"{label}/{path.name}"] = sha256(path)
    audit = {
        "schema": "looksy.p21-film-caption-layout-audit/v1",
        "productionCallsMade": False,
        "projectSnapshot": str(PROJECT),
        "projectFileSha256": project_sha,
        "documentHash": EXPECTED_DOC_HASH,
        "timelineResourceId": RESOURCE_ID,
        "activeOutputVariant": doc["activeOutputVariant"],
        "trackCount": len(doc["tracks"]),
        "clipCount": len(clips),
        "phaseSpan": [184, 252],
        "P16P18TargetClipRecordDifferences": p16_differences,
        "P15P18TargetClipRecordDifferenceIds": p15_differences,
        "P16NativeProof": {
            "documentHash": "438df6c817d17155464f532f50dffb8cd718718fa4a86c83615e3fa1b342f24a",
            "workerPid": 98633,
            "binarySha256": "7630662c8d35f37bc10a1b68983bdbae4a736eac7007b236f8e73c4ff2dfa8a3",
            "note": "Partial full render only; P18 has identical full clip records over 184..252. Not a completed film receipt.",
        },
        "verifiedDefaultFooterOccurrences": existing_exact_footers,
        "footerDestinationTracks": track_preconditions,
        "schemaEvidence": {
            "setterSchema": str(POLISH / "schema-setTimelineClipTitle.json"),
            "createSchemaReceipt": str(POLISH / "pass17-photo-v2.execution.ndjson"),
            "createCallEvidenceLine": "P17 shows createTimelineTitle in a 16-operation native typed batch; command fields in this plan match that accepted shape.",
        },
        "sourceSha256": source_hashes,
        "evidencePngSha256": evidence_hashes,
        "conclusion": "Correct the native title clips with source-observed hard wraps and source-flow vertical centers, restore the default footer pair only for the normal Caption phase interval 196..232 on already-existing empty video lanes, and correct source-copy line breaks for phases2,3,4,6,7. Phase3 also restores the source Caption's fixed 91px heading size from the current native 76px. No changes to accepted footage, clocks, animation curves, effects, audio, or special DeliveryProof chapter.",
        "visualReviewStatus": "proposal only; root must apply against a fresh native task/hash, render A/B, and compare listed frames before accepting",
    }
    (OUT / "p21-film-caption-layout-audit.json").write_text(
        json.dumps(audit, indent=2, ensure_ascii=False) + "\n")

    print(json.dumps({
        "plan": str(OUT / "p21-film-caption-layout.commands-fragment.json"),
        "audit": str(OUT / "p21-film-caption-layout-audit.json"),
        "actions": len(steps),
        "existingTargets": len(targets),
        "createdFooters": len(FOOTERS),
        "p16P18TargetDifferences": len(p16_differences),
        "p15P18TargetDifferences": p15_differences,
        "projectSha256": project_sha,
    }, indent=2))


if __name__ == "__main__":
    main()
