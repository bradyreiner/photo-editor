#!/usr/bin/env python3
"""Create binder-compatible P23 v2 and run pure placement/target guards."""

from __future__ import annotations

import copy
import hashlib
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
POLISH = HERE.parent
ROOT = POLISH.parents[2]
sys.path.insert(0, str(POLISH))
from native_fragment_placement import validate_clip_references, verify_placement  # noqa: E402

V1 = HERE / "p23-late-chapter-proof.fragment.json"
V2 = HERE / "p23-late-chapter-proof.fragment.v2.json"
P22 = POLISH / "native-projects/looksy-polish-pass22-photo-proof.json"
P20 = POLISH / "native-projects/looksy-polish-pass20-carousel-complete.json"
P22_CHECKPOINT = POLISH / "checkpoint-pass22-photo-proof-result.json"
P20_CHECKPOINT = POLISH / "checkpoint-pass20-carousel-complete-result.json"
P22_SHA = "b6fc44862237506bb661ce69f3b5cb82e67806fe573d4274dfaa1952b0d4f37b"
P22_HASH = "c2fb601cf1808116a7aaaa87aaecfb22c340b4f4bf719876cb9b8be932918b0c"
P20_SHA = "2728aaec37df78003811f0aab81ac54ffc4142b986c0fa503826eb79b2dc23b1"
P20_HASH = "801fea1560cd6ecd77a940d7926e63948d367598d8ec81b79eef1ffa319ae799"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def clip_index(project: dict) -> tuple[dict, dict]:
    clips, tracks = {}, {}
    for track in project["tracks"]:
        tracks[track["trackId"]] = track
        for clip in track["clips"]:
            clips[clip["clipId"]] = clip
    return clips, tracks


def canonical(value) -> str:
    return json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(",", ":"))


def main() -> None:
    if digest(P22) != P22_SHA or digest(P20) != P20_SHA:
        raise SystemExit("P22/P20 project bytes changed; re-audit before preparing P23 v2")
    p22_receipt, p20_receipt = json.loads(P22_CHECKPOINT.read_text()), json.loads(P20_CHECKPOINT.read_text())
    if p22_receipt.get("fileSha256") != P22_SHA or p22_receipt.get("documentHash") != P22_HASH:
        raise SystemExit("P22 checkpoint mismatch")
    if p20_receipt.get("fileSha256") != P20_SHA or p20_receipt.get("documentHash") != P20_HASH:
        raise SystemExit("P20 checkpoint mismatch")

    plan = json.loads(V1.read_text())
    plan["revision"] = 2
    plan["requiresFreshBinding"] = True
    plan["taskId"] = None
    plan["taskIdRequiredAtApply"] = True
    plan["startingDocumentHash"] = P22_HASH
    plan["startingDocumentHashRequiredAtApply"] = True
    plan["planningSnapshot"] = {
        "path": str(P22.resolve()),
        "sha256": P22_SHA,
        "documentHash": P22_HASH,
        "checkpointPath": str(P22_CHECKPOINT.resolve()),
        "checkpointSha256": digest(P22_CHECKPOINT),
        "trackCount": 196,
        "clipCount": 1421,
        "activeOutputVariant": "landscape",
    }
    target_ids = sorted({
        step["values"]["clipId"] for step in plan["steps"]
        if isinstance(step.get("values", {}).get("clipId"), str)
    })
    dest_ids = sorted({
        step["values"]["trackId"] for step in plan["steps"]
        if isinstance(step.get("values", {}).get("trackId"), str)
        and step["commandId"] in {
            "timeline.action.createTimelineTitle",
            "timeline.action.createTimelineGraphic",
        }
    })
    plan["placementGuards"] = {
        "halfOpenIntervalSeconds": [259.0, 273.0],
        "destinationTrackIds": dest_ids,
        "targetClipIds": target_ids,
        "existingTargetClipIds": target_ids,
        "expectedTrackOrderUnchanged": True,
        "requireAllDestinationTracksVisibleUnlockedGraphics": True,
        "requireDestinationTracksEmptyInInterval": True,
    }
    plan["applyGuard"] = {
        "existingTargetIds": target_ids,
        "destinationTrackIds": dest_ids,
        "requireExactCurrentDocumentHashAfterBinding": True,
        "requirePlanningTargetSnapshotsUnchanged": True,
        "requireDestinationTracksFree259To273": True,
        "neverReplayAmbiguousMutation": True,
    }

    p22 = json.loads(P22.read_text())
    p20 = json.loads(P20.read_text())
    p22_clips, p22_tracks = clip_index(p22)
    p20_clips, p20_tracks = clip_index(p20)
    # Match the binder's serialized target fields, then strengthen the offline
    # check with keyframe/override and source/track identity checks.
    binder_fields = [
        "mediaType", "assetId", "filePath", "startSeconds", "durationSeconds", "keyframes",
        "trimStartSeconds", "trimEndSeconds", "playbackRate", "opacity",
        "frameRotationDegrees", "clipMask", "outputVariantOverrides", "frameScale",
        "frameOffsetX", "frameOffsetY", "frameWidth", "frameHeight", "titleText",
        "titlePositionX", "titlePositionY", "titleFontSize", "titleShapeType",
        "titleShapeWidth", "titleShapeHeight", "titleShapeCornerRadius",
        "titleShapeStrokeWidth", "titleShapeStrokeColor", "titleBackgroundColor",
        "titleColor", "titleFontFamily", "titleFontWeight", "titleAlignment",
        "titleLineHeight", "titleLetterSpacing", "captionBackgroundPadding",
        "captionBackgroundRadius", "frameMode", "sourceCropLeft", "sourceCropTop",
        "sourceCropRight", "sourceCropBottom", "effectSettings", "effectStack", "blendMode",
    ]
    target_diffs = []
    for cid in target_ids:
        if cid not in p22_clips or cid not in p20_clips:
            raise SystemExit(f"target missing across P22/P20: {cid}")
        p22_track = next(t["trackId"] for t in p22["tracks"] if any(c["clipId"] == cid for c in t["clips"]))
        p20_track = next(t["trackId"] for t in p20["tracks"] if any(c["clipId"] == cid for c in t["clips"]))
        if p22_track != p20_track:
            raise SystemExit(f"target track changed between P22/P20: {cid}")
        changed = [field for field in binder_fields if p22_clips[cid].get(field) != p20_clips[cid].get(field)]
        if changed:
            target_diffs.append({"clipId": cid, "changedFields": changed})
    if target_diffs:
        raise SystemExit("P23 target fields changed since P22: " + json.dumps(target_diffs))
    p22_order = [t["trackId"] for t in p22["tracks"]]
    p20_order = [t["trackId"] for t in p20["tracks"]]
    if p22_order != p20_order:
        raise SystemExit("P20/P22 global track order differs")
    for tid in dest_ids:
        if tid not in p22_tracks or tid not in p20_tracks:
            raise SystemExit(f"destination track missing P22/P20: {tid}")
        overlaps = [c["clipId"] for c in p20_tracks[tid]["clips"]
                    if c["startSeconds"] < 273 and
                    c["startSeconds"] + (c["trimEndSeconds"] - c["trimStartSeconds"]) /
                    c.get("playbackRate", 1) > 259]
        if overlaps:
            raise SystemExit(f"P20 lane is no longer free in [259,273): {tid}: {overlaps}")

    existing_targets, create_refs = validate_clip_references(plan)
    if existing_targets != set(target_ids):
        raise SystemExit("placement guards do not match existing target clip references")
    placement = verify_placement(plan, p22, p20)
    plan["preflightEvidence"] = {
        "offlineOnly": True,
        "validateClipReferences": {
            "result": "passed", "existingTargets": len(existing_targets),
            "receiptBoundCreatedClipReferences": len(create_refs),
        },
        "verifyPlacement": placement,
        "p22PlanningBaseline": {"path": str(P22.resolve()), "fileSha256": P22_SHA,
                                "documentHash": P22_HASH, "clipCount": 1421},
        "p20LatestLiveCandidate": {"path": str(P20.resolve()), "fileSha256": P20_SHA,
                                   "documentHash": P20_HASH, "clipCount": 1453},
        "targetFieldsUnchangedP22ToP20": len(target_ids),
        "destinationTracksFree259To273": len(dest_ids),
        "targetDifferences": [],
    }
    with V2.open("x") as stream:
        json.dump(plan, stream, indent=2, ensure_ascii=False)
        stream.write("\n")
    evidence = {
        "schema": "looksy.p23-offline-preflight/v1",
        "result": "passed",
        "noMcpCallsOrProjectWrites": True,
        "v1Path": str(V1.resolve()), "v1Sha256": digest(V1),
        "v2Path": str(V2.resolve()), "v2Sha256": digest(V2),
        "validation": plan["preflightEvidence"],
        "targetIdCount": len(target_ids),
        "destinationTrackIds": dest_ids,
    }
    evidence_path = HERE / "p23-preflight-validation-v2.json"
    evidence_path.write_text(json.dumps(evidence, indent=2) + "\n")
    print(json.dumps(evidence, indent=2))


if __name__ == "__main__":
    main()
