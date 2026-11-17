#!/usr/bin/env python3
"""Build an offline, receipt-bindable P23 proposal from the saved P22 project."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[4]
POLISH = ROOT / "marketing/native-recreation-v4/polish-pass-2026-10-30"
OUT = POLISH / "p23-late-chapter-proof"
PROJECT = POLISH / "native-projects/looksy-polish-pass22-photo-proof.json"
CHECKPOINT = POLISH / "checkpoint-pass22-photo-proof-result.json"
P21 = POLISH / "native-projects/looksy-polish-pass21-film-polish.json"
P16 = POLISH / "p16-late-chapters-review"
RESOURCE = "timeline:3db4e4fb-6737-415c-b6ee-dde766e7c237"
P22_FILE_SHA = "b6fc44862237506bb661ce69f3b5cb82e67806fe573d4274dfaa1952b0d4f37b"
P22_DOC_HASH = "c2fb601cf1808116a7aaaa87aaecfb22c340b4f4bf719876cb9b8be932918b0c"
P21_FILE_SHA = "7a99aa8d2fe790a58a20ebced6ff04f0e912dc584e2f34b1bdcfef7a1cbc0ef9"
P16_MP4_SHA = "d37c0897374a1d59d06889a1de83a10e3ff5f5c3e23d5044fa86b508297f3c1e"
REFERENCE_SHA = "9ec7b8b422f96818e61a1f2d3987cc3422027537bb6280c671a689c88e3e8640"


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def canonical_sha(value: Any) -> str:
    return sha256_bytes(
        json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()
    )


def rel(path: Path) -> str:
    return path.resolve().relative_to(ROOT.resolve()).as_posix()


def index_project(project: dict[str, Any]) -> tuple[dict[str, tuple[dict, dict]], dict[str, dict]]:
    clips: dict[str, tuple[dict, dict]] = {}
    tracks = {}
    for track in project["tracks"]:
        tracks[track["trackId"]] = track
        for clip in track["clips"]:
            clips[clip["clipId"]] = (track, clip)
    return clips, tracks


TITLE_BASE_KEYS = (
    "titleText", "titleFontFamily", "titleFontSize", "titleFontWeight",
    "titleLetterSpacing", "titleLineHeight", "titleColor", "titlePositionX",
    "titlePositionY", "titleAlignment", "titleBackgroundColor",
    "captionBackgroundPadding", "captionBackgroundRadius", "titleShapeWidth",
    "titleShapeHeight", "titleShapeCornerRadius",
)
VARIANT_KEYS = (
    "titleText", "titleFontFamily", "titleFontSize", "titleFontWeight",
    "titleLetterSpacing", "titleLineHeight", "titleColor", "titlePositionX",
    "titlePositionY", "titleAlignment", "titleBackgroundColor",
    "captionBackgroundPadding", "captionBackgroundRadius", "titleShapeWidth",
    "titleShapeHeight", "titleShapeCornerRadius",
)


def snapshot(track: dict[str, Any], clip: dict[str, Any]) -> dict[str, Any]:
    overrides = clip.get("outputVariantOverrides", {})
    return {
        "clipId": clip["clipId"],
        "trackId": track["trackId"],
        "trackDisplayLabel": track.get("displayLabel"),
        "trackLaneIndex": track.get("laneIndex"),
        "mediaType": clip.get("mediaType"),
        "assetId": clip.get("assetId"),
        "startSeconds": clip.get("startSeconds"),
        "durationSeconds": clip.get("durationSeconds"),
        "titleFields": {k: clip.get(k) for k in TITLE_BASE_KEYS},
        "keyframesSha256": canonical_sha(clip.get("keyframes", [])),
        "outputVariantOverridesSha256": canonical_sha(overrides),
        "landscapeTitleFields": {
            k: overrides.get("landscape", {}).get(k) for k in VARIANT_KEYS
        },
        "nonLandscapeOverrideSha256": canonical_sha(
            {k: v for k, v in overrides.items() if k != "landscape"}
        ),
    }


def title_step(step_id: str, clip_id: str, values: dict[str, Any]) -> dict[str, Any]:
    return {
        "id": step_id,
        "commandId": "timeline.action.setTimelineClipTitle",
        "values": {"clipId": clip_id, "values": values},
    }


def alpha_keys(start: float, end: float) -> list[dict[str, Any]]:
    """Sparse 100 ms samples of source out(t,start,end), then a hold to clip end."""
    samples = {0.0, start, end, 7.0}
    t = start
    while t < end - 1e-9:
        t = round(t + 0.1, 6)
        if t < end:
            samples.add(t)
    frames = []
    for t in sorted(samples):
        u = max(0.0, min(1.0, (t - start) / (end - start)))
        value = 1.0 - (1.0 - u) ** 4
        frames.append({
            "localSeconds": t,
            "value": value,
            "interpolation": "hold" if t == 7.0 or t == end else "linear",
        })
    return frames


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    raw = PROJECT.read_bytes()
    if sha256_bytes(raw) != P22_FILE_SHA:
        raise SystemExit("P22 project bytes changed; stop and rederive the proposal")
    project = json.loads(raw)
    checkpoint = json.loads(CHECKPOINT.read_text())
    if checkpoint.get("fileSha256") != P22_FILE_SHA or checkpoint.get("documentHash") != P22_DOC_HASH:
        raise SystemExit("P22 checkpoint receipt does not match the saved project")
    if project.get("activeOutputVariant") != "landscape":
        raise SystemExit("P22 is not in landscape; geometry must be rederived")
    clips, tracks = index_project(project)

    # Verify the Gaussian label has not drifted since the P21 snapshot.
    p21_raw = P21.read_bytes()
    if sha256_bytes(p21_raw) != P21_FILE_SHA:
        raise SystemExit("P21 project bytes changed; stop and re-audit provenance")
    p21_clips, _ = index_project(json.loads(p21_raw))
    gaussian_id = "5d1be438-6ae9-44c2-97f7-5e6ae7aea683"
    if gaussian_id not in clips or gaussian_id not in p21_clips:
        raise SystemExit("Gaussian label stable ID missing in P21/P22")
    if snapshot(*clips[gaussian_id]) != snapshot(*p21_clips[gaussian_id]):
        raise SystemExit("Gaussian label changed between P21 and P22; re-audit before planning")

    source_files = [
        ROOT / "marketing/animated-trailer/src/creative-launch/AgentPolish.tsx",
        ROOT / "marketing/animated-trailer/src/creative-launch/CanvasPolish.tsx",
        ROOT / "marketing/animated-trailer/src/creative-launch/Kit.tsx",
        ROOT / "marketing/animated-trailer/src/rebuild/Kit.tsx",
        ROOT / "marketing/animated-trailer/src/rebuild/Type.ts",
        ROOT / "marketing/native-recreation-v4/evidence/timeline-motion-authoring-capability-schemas-current.json",
        ROOT / "marketing/native-recreation-v4/polish-pass-2026-10-30/schema-setTimelineClipTitle.json",
    ]
    source_hashes = [{"path": rel(p), "sha256": sha256_file(p)} for p in source_files]

    # Native P16 proof at 260/270 is a WIP render from the pre-P16 document.
    # Its late target clips were compared read-only across P16/P22; only the
    # P22 live state is used as the planning snapshot and the executor must rebind.
    frame_ids = [7202, 7800, 8100, 8550, 8850]
    frame_pairs = []
    for frame in frame_ids:
        p16_native = P16 / "native" / f"frame-{frame:06d}.png"
        p16_ref = P16 / "reference" / f"frame-{frame:06d}.png"
        if not p16_native.exists() or not p16_ref.exists():
            raise SystemExit(f"missing retained exact native/reference frame pair {frame}")
        frame_pairs.append({
            "globalFrame": frame,
            "seconds": frame / 30,
            "native": {"path": rel(p16_native), "sha256": sha256_file(p16_native),
                       "renderQualification": "P16 WIP render; not a P22 or final-render proof."},
            "reference": {"path": rel(p16_ref), "sha256": sha256_file(p16_ref)},
        })
    compare_path = OUT / "evidence" / "native-frame-007860.png"
    compare_ref = OUT / "evidence" / "reference-frame-007860.png"
    if compare_path.exists() and compare_ref.exists():
        frame_pairs.append({
            "globalFrame": 7860,
            "seconds": 262,
            "native": {"path": rel(compare_path), "sha256": sha256_file(compare_path),
                       "renderQualification": "Decoded from retained P16 WIP MP4; not a P22 render."},
            "reference": {"path": rel(compare_ref), "sha256": sha256_file(compare_ref)},
        })

    targets: dict[str, dict[str, Any]] = {}
    steps: list[dict[str, Any]] = []

    def add_title(clip_id: str, step_id: str, values: dict[str, Any]) -> None:
        if clip_id not in clips:
            raise SystemExit(f"missing P22 target {clip_id}")
        track, clip = clips[clip_id]
        if clip.get("mediaType") != "title" or track.get("locked"):
            raise SystemExit(f"P22 target is not an editable title on an unlocked track: {clip_id}")
        targets.setdefault(clip_id, snapshot(track, clip))
        steps.append(title_step(step_id, clip_id, values))

    add_title(gaussian_id, "gaussian-label-lime", {"color": "#ffd7ff48"})

    # Proposal panel: keep the existing left edge, width, title, and start.
    # Reference frame 7860 measures x≈716..1684, y≈192..904 (1920x1080).
    add_title("24d0f924-9a71-4122-840f-f91c5105cb53", "proposal-panel-source-height", {
        "shapeHeight": 712 / 1080,
        "positionY": 548 / 1080,
    })
    for slug, clip_id in (
        ("choose-scope-plate", "cf5eaba3-e502-41cb-aa03-2f358f67c3d0"),
        ("keep-control-plate", "5cc4c374-e504-4c08-8de7-2d8c22a943d6"),
        ("choose-scope-copy", "d42b2a2d-2cc9-451e-9439-9245c1518208"),
        ("keep-control-copy", "27e45a5f-333a-4fa4-9b68-22dc50837a08"),
    ):
        add_title(clip_id, f"{slug}-align-to-source", {"positionY": 835 / 1080})
    add_title("01052034-7705-4996-815d-7fbee877a66b", "proposal-third-row-source-wrap", {
        "text": "Keep the photo, mask and type\nseparate",
    })
    add_title("80bc6b38-e3cf-4662-aab5-9b5a930e20e3", "proposal-third-row-source-subtitle-y", {
        "positionY": 729 / 1080,
    })

    # The same source copy is preserved; explicit newlines reproduce the
    # reference's three-line Caption and five-line detail wrap in the title renderer.
    add_title("8f16bfb9-256e-4947-9a91-b1bc491c8ee3", "editable-heading-three-source-lines", {
        "text": "The work\nstays\neditable.",
        "positionY": 445 / 1080,
    })
    add_title("b4fc29a8-475f-42f3-a85b-b33962cc85cc", "editable-detail-source-wrap", {
        "text": "The same photographs,\ntext, masks and\nadjustments remain\navailable for your\nrefinement.",
        "positionY": 729 / 1080,
    })

    # Layer list outer geometry; preserve title, borders and source poster.
    add_title("321b821d-b802-4eb5-8ac0-c90552f670a9", "layers-panel-source-height", {
        "shapeHeight": 347 / 1080,
        "positionY": 608 / 1080,
    })
    add_title("fbb07681-3a51-4f95-8bca-b982284f17f8", "layers-header-source-center", {
        "positionY": 472 / 1080,
    })
    label_positions = {
        "71fd3d71-19e3-4f16-9f8e-0fd5a03cf58e": 534.5 / 1080,  # Subject cutout, row 1
        "3254300c-16d5-4d01-bcbf-eb4e60c78b90": 597.7 / 1080,  # Typography, row 2
        "740526e5-a6eb-4694-85b7-972f0e6c6e2b": 662.3 / 1080,  # selected row 3
        "2acfb2eb-080b-40b0-a346-9ba9deaa466f": 725.1 / 1080,  # Original photograph
    }
    for i, (clip_id, y) in enumerate(label_positions.items(), start=1):
        add_title(clip_id, f"layers-row-{i}-source-order-and-center", {
            "positionX": 1357 / 1920,
            "positionY": y,
        })

    # Eight source row-fill plates: four ordinary and four selected.
    row_shapes = [
        ("4cea0ea3-cc4d-4115-93a9-7277b753aa8a", 534.5),
        ("67f7a251-00fa-4665-a38f-ae6a0332aacf", 534.5),
        ("c49005de-9cd3-45fe-a342-33afc2def1cc", 597.7),
        ("0bcb1a90-241e-4701-9b5e-49db40b98360", 597.7),
        ("0de4f665-c620-4302-9eee-c2b9be75a3fc", 662.3),
        ("cce36a7f-f296-4efd-9ac4-b40fa21284e2", 662.3),
        ("59f47122-3077-4545-a810-e50257f9fff7", 725.1),
        ("5d9fda34-443f-44a8-a584-657a6f60e154", 725.1),
    ]
    for idx, (clip_id, y_px) in enumerate(row_shapes, start=1):
        add_title(clip_id, f"layers-row-plate-{idx}-source-inset", {
            "positionX": 1514.6 / 1920,
            "positionY": y_px / 1080,
            "shapeWidth": 336.6 / 1920,
            "shapeHeight": 63.8 / 1080,
        })

    if len(targets) != 24:
        raise SystemExit(f"expected 24 existing targets, got {len(targets)}")

    # Fixed, existing free graphics lanes. The exact lane IDs/order and all
    # destination overlaps are guarded from the P22 snapshot and rechecked at bind.
    marker_tracks = [
        "graphics-f08b6658-ee51-40b0-9d0b-b05794f9b924",  # G34
        "graphics-318303fc-ff15-4ed6-a88c-a8103b7c793c",  # G35
        "graphics-b9386fc8-eb72-437d-ae40-739f6ef82f73",  # G36
        "graphics-f12c6ee1-99a6-42d7-b08e-89beb5e71786",  # G37
        "graphics-dcc0e157-877f-4c9e-b0a4-25621b7572a9",  # G38 moving lime state
    ]
    layer_divider_tracks = [
        "graphics-da4059a4-4787-433d-bad7-1ea24dd5cfe0",  # G39
        "graphics-1484295e-992e-49a8-aaf7-81933eecde09",  # G40
        "graphics-f1ca486e-ae18-4b55-bf8d-434bc9c653b0",  # G41
        "graphics-4eeba747-ebb1-4635-9b46-111e033e5f16",  # G42
    ]
    proposal_divider_tracks = [
        "graphics-dbf28d3a-51aa-478c-a5cf-026fdbb53413",  # G43
        "graphics-8ddd7b2b-74a6-496a-a3f7-b93f9764d60b",  # G44
        "graphics-4faa8966-cfc3-49cc-87ae-81d3917a01de",  # G45
    ]
    destination_guards = []
    for track_id in marker_tracks + layer_divider_tracks + proposal_divider_tracks:
        track = tracks[track_id]
        if track.get("locked") or not track.get("visible") or track.get("kind") != "video":
            raise SystemExit(f"destination track is not a visible unlocked video graphics lane: {track_id}")
        if track_id not in {t["trackId"] for t in tracks.values()}:
            raise SystemExit(f"destination track missing: {track_id}")
        overlaps = [
            c["clipId"] for c in track["clips"]
            if c.get("startSeconds", 0) < 273 and c.get("startSeconds", 0) + c.get("durationSeconds", 0) > 259
        ]
        if overlaps:
            raise SystemExit(f"destination track {track_id} is occupied: {overlaps}")
        destination_guards.append({
            "trackId": track_id,
            "displayLabel": track.get("displayLabel"),
            "laneIndex": track.get("laneIndex"),
            "kind": track.get("kind"),
            "role": track.get("role"),
            "locked": track.get("locked"),
            "visible": track.get("visible"),
            "existingClipIdsAndIntervalsSha256": canonical_sha([
                {k: c.get(k) for k in ("clipId", "startSeconds", "durationSeconds")}
                for c in track["clips"]
            ]),
            "requiredFreeHalfOpenIntervalSeconds": [259.0, 273.0],
            "overlappingClipIds": [],
        })

    create_steps: list[dict[str, Any]] = []
    animation_steps: list[dict[str, Any]] = []
    marker_specs = [
        ("marker-subject", marker_tracks[0], 534.5 / 1080, "#ff8c9ba8"),
        ("marker-typography", marker_tracks[1], 597.7 / 1080, "#ff8c9ba8"),
        ("marker-color", marker_tracks[2], 662.3 / 1080, "#ff8c9ba8"),
        ("marker-original", marker_tracks[3], 725.1 / 1080, "#ff8c9ba8"),
    ]
    for slug, track_id, y, color in marker_specs:
        create_steps.append({
            "id": f"create-{slug}",
            "commandId": "timeline.action.createTimelineTitle",
            "values": {
                "trackId": track_id,
                "templateKey": "basic",
                "startSeconds": 266.0,
                "durationSeconds": 7.0,
                "values": {
                    "text": "◉", "fontFamily": "Space Grotesk", "fontSize": 23.1,
                    "fontWeight": 400, "letterSpacing": 0, "lineHeight": 1.0,
                    "alignment": "center", "casing": "none", "color": color,
                    "positionX": 1658 / 1920, "positionY": y,
                    "shapeWidth": 0.05, "shapeHeight": 0.05,
                    "backgroundColor": "#00000000", "backgroundPadding": 0,
                    "backgroundRadius": 0, "safeMarginX": 0, "safeMarginY": 0,
                },
            },
        })

    for i, (track_id, y_px) in enumerate(zip(layer_divider_tracks, (566.4, 630.2, 694.0, 757.8)), start=1):
        create_steps.append({
            "id": f"create-layer-divider-{i}",
            "commandId": "timeline.action.createTimelineGraphic",
            "values": {
                "trackId": track_id, "shapeType": "rectangle",
                "startSeconds": 266.0, "durationSeconds": 7.0,
                "values": {
                    "positionX": 1514.6 / 1920, "positionY": y_px / 1080,
                    "shapeWidth": 336.6 / 1920, "shapeHeight": 1.1 / 1080,
                    "shapeCornerRadius": 0, "backgroundColor": "#249dafc0",
                    "shapeStrokeColor": "#00000000", "shapeStrokeWidth": 0,
                },
            },
        })

    for i, (track_id, y_px, start, end) in enumerate(zip(
        proposal_divider_tracks, (425.0, 595.0, 770.0), (0.4, 0.75, 1.1), (1.1, 1.45, 1.8)
    ), start=1):
        create_id = f"create-proposal-divider-{i}"
        create_steps.append({
            "id": create_id,
            "commandId": "timeline.action.createTimelineGraphic",
            "values": {
                "trackId": track_id, "shapeType": "rectangle",
                "startSeconds": 259.0, "durationSeconds": 7.0,
                "values": {
                    "positionX": 1200 / 1920, "positionY": y_px / 1080,
                    "shapeWidth": 888 / 1920, "shapeHeight": 1.1 / 1080,
                    "shapeCornerRadius": 0, "backgroundColor": "#2bc0cbd5",
                    "shapeStrokeColor": "#00000000", "shapeStrokeWidth": 0,
                },
            },
        })
        animation_steps.append({
            "id": f"animate-proposal-divider-{i}",
            "commandId": "timeline.action.setTimelineClipAnimation",
            "values": {
                "clipId": {"result": create_id, "field": "createdClipId"},
                "channels": [{"parameter": "opacity", "keyframes": alpha_keys(start, end)}],
            },
        })

    create_steps.append({
        "id": "create-layer-selected-marker",
        "commandId": "timeline.action.createTimelineTitle",
        "values": {
            "trackId": marker_tracks[4], "templateKey": "basic",
            "startSeconds": 266.0, "durationSeconds": 7.0,
            "values": {
                "text": "◉", "fontFamily": "Space Grotesk", "fontSize": 23.1,
                "fontWeight": 400, "letterSpacing": 0, "lineHeight": 1.0,
                "alignment": "center", "casing": "none", "color": "#ffd7ff48",
                "positionX": 1658 / 1920, "positionY": 534.5 / 1080,
                "shapeWidth": 0.05, "shapeHeight": 0.05,
                "backgroundColor": "#00000000", "backgroundPadding": 0,
                "backgroundRadius": 0, "safeMarginX": 0, "safeMarginY": 0,
            },
        },
    })
    # Green selection marker follows EditableLayers(selected=floor(s/1.7)),
    # with reorder=1 swapping the first two source rows. Hold interpolation is
    # the source's discrete selected-row change, not a fabricated glide.
    animation_steps.append({
        "id": "animate-layer-selected-marker-source-steps",
        "commandId": "timeline.action.setTimelineClipAnimation",
        "values": {
            "clipId": {"result": "create-layer-selected-marker", "field": "createdClipId"},
            "channels": [{
                "parameter": "frameOffsetY",
                "keyframes": [
                    {"localSeconds": 0.0, "value": 63.8 / (1080 * 0.25), "interpolation": "hold"},
                    {"localSeconds": 1.7, "value": 0.0, "interpolation": "hold"},
                    {"localSeconds": 3.4, "value": 127.6 / (1080 * 0.25), "interpolation": "hold"},
                    {"localSeconds": 5.1, "value": 191.4 / (1080 * 0.25), "interpolation": "hold"},
                    {"localSeconds": 7.0, "value": 191.4 / (1080 * 0.25), "interpolation": "hold"},
                ],
            }],
        },
    })

    if len(create_steps) != 12 or len(animation_steps) != 4:
        raise SystemExit(f"unexpected generated counts creates={len(create_steps)} animations={len(animation_steps)}")
    steps.extend(create_steps)
    steps.extend(animation_steps)
    if len(steps) != 40:
        raise SystemExit(f"expected 40 operations, got {len(steps)}")
    batches = [steps[:16], steps[16:32], steps[32:]]
    if [len(b) for b in batches] != [16, 16, 8]:
        raise SystemExit("internal batch split changed")

    plan = {
        "schema": "looksy.native-authoring-plan/v1",
        "name": "P23 late chapters: Gaussian source-lime, proposal layout, editable layer list",
        "status": "proposed-unbound-not-applied",
        "productionCallsMade": False,
        "resourceId": RESOURCE,
        "taskId": None,
        "taskIdRequiredAtApply": True,
        "planningDocumentHash": P22_DOC_HASH,
        "planningProjectPath": rel(PROJECT),
        "planningProjectSha256": P22_FILE_SHA,
        "startingDocumentHash": None,
        "startingDocumentHashRequiredAtApply": True,
        "planningSnapshot": {
            "projectPath": rel(PROJECT), "projectSha256": P22_FILE_SHA,
            "documentHash": P22_DOC_HASH,
            "trackCount": len(project["tracks"]),
            "clipCount": sum(len(t["clips"]) for t in project["tracks"]),
            "activeOutputVariant": project["activeOutputVariant"],
            "checkpointPath": rel(CHECKPOINT),
            "checkpointSha256": sha256_file(CHECKPOINT),
            "p21ProjectSha256": P21_FILE_SHA,
            "gaussianTargetUnchangedBetweenP21P22": True,
        },
        "creativeReference": {
            "sourceHashes": source_hashes,
            "referenceMovie": {
                "path": "/Users/bradyreiner/Downloads/looksy-creative-polish-v4-landscape.mp4",
                "sha256": REFERENCE_SHA,
            },
            "nativeRender": {
                "path": rel(POLISH / "native-full-p16-landscape-review/looksy-native-landscape-p16-wip.mp4"),
                "sha256": P16_MP4_SHA,
                "documentHash": "438df6c817d17155464f532f50dffb8cd718718fa4a86c83615e3fa1b342f24a",
                "binarySha256": "7630662c8d35f37bc10a1b68983bdbae4a736eac7007b236f8e73c4ff2dfa8a3",
                "qualification": "P16 WIP render; native proof of the P22 result is not claimed. Late target properties were checked read-only and found stable in P22.",
            },
            "composition": {"width": 1920, "height": 1080, "fps": 30},
            "sourceGeometry": {
                "proposalPanel": {"source": "creative-launch/AgentPolish.tsx", "box": [60, 65, 880, 710], "stage": [650, 120, 1.1], "targetBoundsPx": [716, 192, 1684, 904]},
                "proposalRows": {"source": "creative-launch/AgentPolish.tsx", "rowTexts": ["Lift the subject by +0.55 EV", "Place the title behind the subject", "Keep the photo, mask and type separate"], "thirdRowWrap": "Keep the photo, mask and type\\nseparate", "buttonTextCenterYpx": 835, "panelDividerYpx": [425, 595, 770]},
                "layerList": {"source": "creative-launch/CanvasPolish.tsx", "stage": [650, 120, 1.1], "leftTop": [610, 285], "outerWidthPx": 385, "outerHeightPx": 347, "rowOrder": ["Subject cutout", "Typography", "Color adjustment", "Original photograph"], "rowCenterYpx": [534.5, 597.7, 662.3, 725.1], "rowFillPx": {"width": 336.6, "height": 63.8, "centerX": 1514.6}, "rowDividerYpx": [566.4, 630.2, 694, 757.8], "selectedIndex": "min(3,floor((globalSeconds-266)/1.7)) after source reorder=1", "markerGlyph": "◉"},
                "editableCaption": {"source": "creative-launch/AgentPolish.tsx + Kit.tsx Caption", "headingLines": ["The work", "stays", "editable."], "detailLines": ["The same photographs,", "text, masks and", "adjustments remain", "available for your", "refinement."]},
                "sourceColor": {"cssLime": "#d7ff48", "nativeArgb": "#ffd7ff48"},
                "sourceLineColorConversions": {"proposalCss": "#c0cbd52b", "nativeArgb": "#2bc0cbd5", "layerCss": "#9dafc024", "nativeArgb": "#249dafc0"},
                "evidenceFrames": frame_pairs,
            },
            "sourceFilesUnchanged": True,
        },
        "nativeActionContract": {
            "commands": ["timeline.action.setTimelineClipTitle", "timeline.action.createTimelineTitle", "timeline.action.createTimelineGraphic", "timeline.action.setTimelineClipAnimation"],
            "stepCount": len(steps),
            "batchSizes": [len(b) for b in batches],
            "executionBatches": [{"batch": i + 1, "stepIds": [s["id"] for s in b]} for i, b in enumerate(batches)],
            "createdClipIdBinding": "Animation clipId values resolve only to the createdClipId in the durable create-step receipt. On uncertainty, inspect the task and append-only journal; never repeat a create.",
            "receiptBinding": "Bind the fresh live document hash after P20 completion. Require all 24 existing target snapshots and all 12 destination lane/order/occupancy guards. The P22 hash is the planning provenance, not permission to execute against stale state.",
        },
        "executionPreconditions": {
            "freshNativeDocumentHashAndTaskRequired": True,
            "resourceMustMatch": RESOURCE,
            "activeOutputVariantMustBe": "landscape",
            "all24TargetSnapshotsMustMatchAtFreshBind": True,
            "all12DestinationTracksMustMatchAndBeFree259To273": True,
            "preserveExistingKeyframesBySha256": True,
            "preserveOtherOutputVariantsExactly": True,
            "targetPreconditions": list(targets.values()),
            "destinationTrackPreconditions": destination_guards,
            "abortOnAnyAmbiguousTaskMutation": True,
            "doNotApplyIfP20ChangedAnyTargetOrDestinationLane": True,
            "staleGuard": "P22 baseline records the source revision. Before applying, bind against a fresh project read and require the same target values, keyframe hashes, track order/metadata, and destination occupancy. Use that current document hash as the task guard. Abort on any mismatch or uncertain prior mutation.",
        },
        "steps": steps,
        "postconditions": {
            "existingTargets": 24,
            "createdTitles": 5,
            "createdGraphicDividers": 7,
            "createdClips": 12,
            "newTrackCount": 0,
            "existingClipStartAndDurationUnchanged": True,
            "allExistingTargetKeyframesUnchanged": True,
            "nonLandscapeOutputOverridesUnchanged": True,
            "poster13ElementGroupUnchanged": True,
            "GaussianEntranceAndAllOtherGaussianChannelsUnchanged": True,
            "sourceProposalAndLayerCopyPreserved": True,
            "sourceRowAndSelectionTimingPreserved": True,
            "targetedCaptionUsesExplicitSourceLineWraps": True,
            "allOtherClipsTracksAudioMediaPathsGradesEffectsAndTrackOrderUnchanged": True,
            "expectedNewClipIntervals": {"proposalDividers": [259, 266], "layerMarkersAndDividers": [266, 273]},
            "reviewFramesSeconds": [240.0666667, 260, 262, 270, 285, 295],
            "nativeABReview": "Render P22-bound target scenes at exact listed frames and review motion near 259–262 and 266–273. Confirm proposal dividers fade with their rows, all row text/pills fit inside the shortened panel, selection marker follows Subject/Typos/Color/Original at local 1.7s steps, layer fills align with text, and the 3-line heading/five-line detail remain readable. Compare to the retained P16 WIP/reference evidence; do not claim P22 or P23 visual parity until a new native render is reviewed.",
        },
        "preserve": [
            "All existing clip starts/durations, linked or source audio, global track order, track metadata, and the 196-track edit structure.",
            "All existing animation/keyframe arrays, including the Gaussian entrance and layer selected-row opacity timing.",
            "All output variants except the active landscape title/shape style fields explicitly written by setTimelineClipTitle.",
            "All photo, mask, poster, grade, effects and background assets; the 13-element HikerPoster group is intentionally deferred as a coherent transform unit.",
            "All accepted P17/P21/P22 changes and every non-target clip. No new media, generated photos, or tracks.",
        ],
        "claimLimit": "This is an offline native-edit proposal derived from exact source and P16 WIP/reference frames. It is not applied and does not claim post-P22 rendered parity. The HikerPoster group remains unmodified because independent rotation of one child would be incorrect.",
    }

    out_path = OUT / "p23-late-chapter-proof.fragment.json"
    out_path.write_text(json.dumps(plan, indent=2, ensure_ascii=False) + "\n")
    print(json.dumps({
        "fragment": rel(out_path),
        "fragmentSha256": sha256_file(out_path),
        "stepCount": len(steps),
        "batchSizes": [len(b) for b in batches],
        "existingTargets": len(targets),
        "newTitles": 5,
        "newGraphicDividers": 7,
        "animations": len(animation_steps),
        "p22FileSha256": P22_FILE_SHA,
        "p22DocumentHash": P22_DOC_HASH,
        "gaussianP21P22Unchanged": True,
    }, indent=2))


if __name__ == "__main__":
    main()
