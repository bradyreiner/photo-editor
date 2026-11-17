#!/usr/bin/env python3
"""Build a read-only, receipt-bindable native plan for P22 Photo proof."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[4]
POLISH = ROOT / "marketing/native-recreation-v4/polish-pass-2026-10-30"
OUT = POLISH / "p22-photo-proof"
PROJECT = POLISH / "native-projects/looksy-polish-pass21-film-polish.json"
P21_AUDIT = POLISH / "p21-film-layout/pass21-film-polish-native-saved-audit.json"
P21_CHECKPOINT = POLISH / "checkpoint-pass21-film-polish-result.json"
FRAMES = POLISH / "full-p16-in-progress-frame-review"
REFERENCES = POLISH / "p17-photo-chapter-parity/reference"
RESOURCE = "timeline:3db4e4fb-6737-415c-b6ee-dde766e7c237"
EXPECTED_DOC_HASH = "8c125de95668c44c2a578721ae5ece46bad59b155391f9bf7655efca677a3c86"
EXPECTED_PROJECT_SHA = "7a99aa8d2fe790a58a20ebced6ff04f0e912dc584e2f34b1bdcfef7a1cbc0ef9"


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def canonical_sha(value: Any) -> str:
    return sha256_bytes(
        json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()
    )


def smooth(value: float, start: float, end: float) -> float:
    u = max(0.0, min(1.0, (value - start) / (end - start)))
    return u * u * u * (10 + u * (-15 + 6 * u))


def out(value: float, start: float, end: float) -> float:
    u = max(0.0, min(1.0, (value - start) / (end - start)))
    return 1.0 - (1.0 - u) ** 4


def rel(path: Path) -> str:
    return path.resolve().relative_to(ROOT.resolve()).as_posix()


def find_clip(project: dict[str, Any], clip_id: str) -> tuple[dict[str, Any], dict[str, Any]]:
    for track in project["tracks"]:
        for clip in track["clips"]:
            if clip.get("clipId") == clip_id:
                return track, clip
    raise ValueError(f"missing P21 clip {clip_id}")


def title_snapshot(track: dict[str, Any], clip: dict[str, Any]) -> dict[str, Any]:
    variant = clip["outputVariantOverrides"]["landscape"]
    keyframes = clip.get("keyframes", [])
    return {
        "clipId": clip["clipId"],
        "trackId": track["trackId"],
        "displayLabel": track.get("displayLabel"),
        "mediaType": clip.get("mediaType"),
        "startSeconds": clip.get("startSeconds"),
        "durationSeconds": clip.get("durationSeconds"),
        "titleText": clip.get("titleText"),
        "titleFontFamily": clip.get("titleFontFamily"),
        "titleFontSize": clip.get("titleFontSize"),
        "titleFontWeight": clip.get("titleFontWeight"),
        "titleLetterSpacing": clip.get("titleLetterSpacing"),
        "titleColor": clip.get("titleColor"),
        "titlePositionX": clip.get("titlePositionX"),
        "titlePositionY": clip.get("titlePositionY"),
        "titleAlignment": clip.get("titleAlignment"),
        "titleBackgroundColor": clip.get("titleBackgroundColor"),
        "captionBackgroundPadding": clip.get("captionBackgroundPadding"),
        "captionBackgroundRadius": clip.get("captionBackgroundRadius"),
        "opacity": clip.get("opacity"),
        "opacityKeyframeCount": sum(k.get("parameter") == "opacity" for k in keyframes),
        "keyframesSha256": canonical_sha(keyframes),
        "landscapeTitleStyle": {
            k: variant.get(k)
            for k in (
                "titleText",
                "titleFontFamily",
                "titleFontSize",
                "titleFontWeight",
                "titleLetterSpacing",
                "titleColor",
                "titlePositionX",
                "titlePositionY",
                "titleAlignment",
                "titleBackgroundColor",
                "captionBackgroundPadding",
                "captionBackgroundRadius",
            )
        },
        "landscapeNonTitleGeometrySha256": canonical_sha(
            {
                k: v
                for k, v in variant.items()
                if not k.startswith("title")
                and k
                not in {"captionBackgroundPadding", "captionBackgroundRadius"}
            }
        ),
    }


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    project_bytes = PROJECT.read_bytes()
    project = json.loads(project_bytes)
    if sha256_bytes(project_bytes) != EXPECTED_PROJECT_SHA:
        raise SystemExit("P21 project file hash changed; re-audit before generating a plan")
    checkpoint = json.loads(P21_CHECKPOINT.read_text())
    if checkpoint.get("documentHash") != EXPECTED_DOC_HASH or checkpoint.get("fileSha256") != EXPECTED_PROJECT_SHA:
        raise SystemExit("P21 checkpoint receipt changed; re-audit before generating a plan")
    if project.get("activeOutputVariant") != "landscape":
        raise SystemExit("P21 is not landscape; rederive title geometry before generating")

    p21_audit = json.loads(P21_AUDIT.read_text())
    if not p21_audit.get("saved", {}).get("readbackVerified"):
        raise SystemExit("P21 saved readback audit is not verified")
    if p21_audit["saved"]["documentHash"] != EXPECTED_DOC_HASH:
        raise SystemExit("P21 saved-audit receipt does not match project hash")

    source_paths = [
        ROOT / "marketing/animated-trailer/src/creative-launch/Photo.tsx",
        ROOT / "marketing/animated-trailer/src/creative-launch/ProofTools.tsx",
        ROOT / "marketing/animated-trailer/src/creative-launch/Kit.tsx",
        ROOT / "marketing/animated-trailer/src/creative-launch/State.ts",
        ROOT / "marketing/animated-trailer/src/rebuild/Kit.tsx",
        ROOT / "marketing/animated-trailer/src/rebuild/Type.ts",
        ROOT / "apps/desktop/qml/workspaces/TimelineView.qml",
        ROOT / "marketing/native-recreation-v4/polish-pass-2026-10-30/schema-setTimelineClipTitle.json",
    ]
    source_files = [{"path": str(p), "sha256": sha256_file(p)} for p in source_paths]

    frame_pairs = []
    for frame, seconds in ((600, 20.0), (720, 24.0), (900, 30.0), (1140, 38.0)):
        native_meta_path = FRAMES / f"frame-{frame:06d}.json"
        native_path = FRAMES / f"frame-{frame:06d}.png"
        reference_path = REFERENCES / f"reference-frame-{frame:04d}.png"
        native_meta = json.loads(native_meta_path.read_text())
        if native_meta["globalFrame"] != frame or native_meta["sequenceSeconds"] != seconds:
            raise SystemExit(f"native frame metadata mismatch for {frame}")
        frame_pairs.append(
            {
                "globalFrame": frame,
                "seconds": seconds,
                "native": {
                    "path": rel(native_path),
                    "sha256": sha256_file(native_path),
                    "documentHash": native_meta["documentHash"],
                    "renderRequestId": native_meta["requestId"],
                    "binarySha256": native_meta["binarySha256"],
                    "chunkSha256": native_meta["chunkSha256"],
                    "qualification": "Exact decoded frame from a closed P16 native chunk; full P16 render/mux qualification is separate.",
                },
                "reference": {
                    "path": rel(reference_path),
                    "sha256": sha256_file(reference_path),
                },
            }
        )

    badge_targets = [
        ("before-16", "4b549bc7-eea3-432d-9691-a734a217cf8c", "before"),
        ("after-16", "4bb2d188-1130-4d4d-91c6-05a6f700c4ad", "after"),
        ("before-22", "a33a64c2-e93b-42a4-9c39-f214021e3b8e", "before"),
        ("after-22", "9257b5be-fefa-47ce-8ab9-113bbd681393", "after"),
        ("before-27", "b7ba9972-2479-465d-93ae-52d8377ac2f3", "before"),
        ("after-27", "6e5a0e84-c988-490a-b83e-de1252682e24", "after"),
        ("before-32", "1006d4a5-1c14-47ce-a2bb-4ae6d649fb6c", "before"),
        ("after-32", "9039363b-e3a5-46b3-aa02-2834f5b97b6e", "after"),
    ]
    targets = []
    steps = []
    for step_id, clip_id, side in badge_targets:
        track, clip = find_clip(project, clip_id)
        snapshot = title_snapshot(track, clip)
        targets.append(snapshot)
        if side == "before":
            values = {
                "text": "BEFORE",
                "alignment": "left",
                "fontSize": 16.5,
                "letterSpacing": 1.98,
                "color": "#fff1efdf",
                "positionX": 0.35,
                "positionY": 0.194,
                "backgroundColor": "#dc0b1324",
                "backgroundPadding": 14,
                "backgroundRadius": 5.5,
            }
        else:
            values = {
                "text": "AFTER / THIS EDIT",
                "alignment": "right",
                "fontSize": 16.5,
                "letterSpacing": 1.98,
                "color": "#ffd7ff48",
                "positionX": 0.9,
                "positionY": 0.194,
                "backgroundColor": "#dc0b1324",
                "backgroundPadding": 14,
                "backgroundRadius": 5.5,
            }
        steps.append(
            {
                "id": f"style-{step_id}-source-inset",
                "commandId": "timeline.action.setTimelineClipTitle",
                "values": {"clipId": clip_id, "values": values},
            }
        )

    # Full-frame coordinates measured on the 1920 x 1080 landscape reference.
    # Source design-space x positions are Stage(650,120) + scale(1.1).
    labels = [
        ("camera", "CAMERA RAW", "graphics-c046e23c-67d8-4e1f-8daa-7d373186a716", 662, "#fff1efdf", 0.0, 0.5),
        ("arrow-raw-linear", "→", "graphics-eb7e6d54-2ca3-4706-9cbb-43921b7b6155", 804, "#ffd7ff48", 0.0, 0.5),
        ("linear", "LINEAR LIGHT", "graphics-f22fb59f-ab73-49e4-8b0d-ea5be673dd6e", 845, "#fff1efdf", 0.2, 0.7),
        ("arrow-linear-looksy", "→", "graphics-fb54f7d0-b187-49ca-9fae-581b17b72f0a", 1013, "#ffd7ff48", 0.2, 0.7),
        ("looksy", "LOOKSY COLOR", "graphics-f5ffc4e9-1959-453e-bc21-4697936fe170", 1054, "#ffd7ff48", 0.4, 0.9),
        ("arrow-looksy-srgb", "→", "graphics-59c87009-7634-4729-80c1-4cb9b08ecc5a", 1223, "#ffd7ff48", 0.4, 0.9),
        ("srgb", "sRGB", "graphics-b9a8e84b-c1f4-483f-943c-8f12fabe443d", 1261, "#fff1efdf", 0.6, 1.1),
    ]
    track_guards = []
    create_steps = []
    animation_steps = []
    for label_index, label in enumerate(labels):
        slug, text, track_id, x_px, color, fade_start, fade_end = label
        track = next(t for t in project["tracks"] if t["trackId"] == track_id)
        occupied = [
            c["clipId"]
            for c in track["clips"]
            if c.get("startSeconds", 0) < 32
            and c.get("startSeconds", 0) + c.get("durationSeconds", 0) > 27
        ]
        if occupied or track.get("locked") or not track.get("visible"):
            raise SystemExit(f"rail destination not available: {track_id}")
        if not any(t["trackId"] == track_id for t in track_guards):
            track_guards.append(
                {
                    "trackId": track_id,
                    "displayLabel": track.get("displayLabel"),
                    "laneIndex": track.get("laneIndex"),
                    "kind": track.get("kind"),
                    "role": track.get("role"),
                    "visible": track.get("visible"),
                    "locked": track.get("locked"),
                    "existingClipCount": len(track["clips"]),
                    "requiredEmptyHalfOpenIntervalSeconds": [27.0, 32.0],
                    "overlappingClipIds": occupied,
                }
            )
        create_id = f"create-rail-{slug}"
        position_x = x_px / 1920.0
        is_arrow = slug.startswith("arrow-")
        values = {
            "text": text,
            "fontFamily": "Space Mono" if not is_arrow else "Space Grotesk",
            "fontSize": 17.6,
            "fontWeight": 400,
            "letterSpacing": 1.98 if not is_arrow else 0,
            "lineHeight": 1.0,
            "alignment": "left",
            "casing": "none",
            "color": color,
            "positionX": position_x,
            "positionY": 0.8564814814814815,
            "backgroundColor": "#00000000",
            "backgroundPadding": 0,
            "backgroundRadius": 0,
            "safeMarginX": 0,
            "safeMarginY": 0,
        }
        create_steps.append(
            {
                "id": create_id,
                "commandId": "timeline.action.createTimelineTitle",
                "values": {
                    "trackId": track_id,
                    "templateKey": "basic",
                    "startSeconds": 27.0,
                    "durationSeconds": 5.0,
                    "values": values,
                },
            }
        )
        # Sparse source samples every three 30fps frames reproduce the product
        # of entry, stagger and exit opacity without keying every output frame.
        sample_times = {0.0, fade_start, fade_end}
        for offset in (0.1, 0.2, 0.3, 0.4):
            t = round(fade_start + offset, 6)
            if fade_start < t < fade_end:
                sample_times.add(t)
        fade_keyframes = []
        for t in sorted(sample_times):
            value = out(t, 0.0, 0.45) * out(t, fade_start, fade_end)
            fade_keyframes.append(
                {
                    "localSeconds": t,
                    "value": value,
                    "interpolation": "linear" if t < fade_end else "hold",
                }
            )
        fade_keyframes.extend(
            [
                {
                    "localSeconds": 140 / 30,
                    "value": 1.0 - smooth(140 / 30, 4.65, 5.0),
                    "interpolation": "linear",
                },
                {
                    "localSeconds": 145 / 30,
                    "value": 1.0 - smooth(145 / 30, 4.65, 5.0),
                    "interpolation": "linear",
                },
                {"localSeconds": 5.0, "value": 0.0, "interpolation": "hold"},
            ]
        )
        animation_steps.append(
            {
                "id": f"animate-rail-{slug}",
                "commandId": "timeline.action.setTimelineClipAnimation",
                "values": {
                    "clipId": {"result": create_id, "field": "createdClipId"},
                    "channels": [
                        {
                            "parameter": "opacity",
                            "keyframes": fade_keyframes,
                        }
                    ],
                },
            }
        )

    steps.extend(create_steps)
    steps.extend(animation_steps)

    if len(steps) != 22:
        raise SystemExit(f"expected 22 native operations, generated {len(steps)}")

    reference_video_path = "/Users/bradyreiner/Downloads/landscape.mp4"
    reference_video_sha256 = "9ec7b8b422f96818e61a1f2d3987cc3422027537bb6280c671a689c88e3e8640"
    plan = {
        "schema": "looksy.native-authoring-plan/v1",
        "name": "P22 Photo native proof rail and comparison insets",
        "status": "proposed-unbound-not-applied",
        "productionCallsMade": False,
        "resourceId": RESOURCE,
        "taskId": None,
        "taskIdRequiredAtApply": True,
        "startingDocumentHash": EXPECTED_DOC_HASH,
        "startingDocumentHashRequiredAtApply": True,
        "planningSnapshot": {
            "path": str(PROJECT),
            "sha256": sha256_bytes(project_bytes),
            "documentHash": EXPECTED_DOC_HASH,
            "trackCount": len(project["tracks"]),
            "clipCount": sum(len(t["clips"]) for t in project["tracks"]),
            "activeOutputVariant": project["activeOutputVariant"],
            "savedAudit": {
                "path": str(P21_AUDIT),
                "sha256": sha256_file(P21_AUDIT),
                "readbackVerified": p21_audit["saved"]["readbackVerified"],
                "untouchedOriginalClipCount": p21_audit["nativeInventory"]["untouchedOriginalClipCount"],
            },
        },
        "creativeReference": {
            "sourceFiles": source_files,
            "phaseGlobalIntervalsSeconds": [[16, 22], [22, 27], [27, 32], [32, 39]],
            "frameRate": 30,
            "composition": {"width": 1920, "height": 1080},
            "railSource": {
                "file": "marketing/animated-trailer/src/creative-launch/Photo.tsx",
                "expression": "opacity = out(s, 0, .45) * out(s, i*.2, .5+i*.2) * (1-smooth(s,4.65,5))",
                "phaseStartGlobalSeconds": 27,
                "rowDesignSpace": {"left": 10, "top": 720, "gap": 18, "childGap": 16},
                "stageTransform": {"left": 650, "top": 120, "scale": 1.1},
                "referencePixelMeasurements": {
                    "labelGlyphStartsX": [662, 845, 1054, 1261],
                    "arrowGlyphStartEstimatesX": [804, 1013, 1223],
                    "glyphRowsY": [919, 931],
                    "positionYCenterPixels": 925,
                    "measurementNote": "Label x values and rail glyph y bounds were measured on closed reference frame 900. Arrow placements are source-flex derived and should be judged by native A/B render; they are deliberately separate editable title clips.",
                },
                "opacityInterpolation": "Source opacity is evaluated at sparse 100ms (three-frame) knots through each staggered half-second entry and at frames 140, 145, 150 for the tail. Native linear interpolation hits those source values and remains smooth at 30fps without per-frame sampling.",
            },
            "comparisonBadgeSource": {
                "file": "marketing/animated-trailer/src/creative-launch/ProofTools.tsx",
                "sourceCss": {
                    "background": "#0b1324dc",
                    "radius": 5,
                    "padding": {"horizontal": 14, "vertical": 10},
                    "beforeTextColor": "#f1efdf",
                    "afterText": "AFTER / THIS EDIT",
                    "afterTextColor": "#d7ff48",
                    "position": {"beforeLeftTop": [20, 20], "afterRightTop": [20, 20]},
                },
                "alphaColorTranslation": "CSS RGBA #0b1324dc becomes native QColor ARGB #dc0b1324.",
                "positionConversion": "Surface top is source-stage y=40; stage top=120 and scale=1.1. Reference badge text glyph center is y=209.5px, so native title center is positionY=.194. Before left edge is x=650+(20*1.1)=672 (.35); After right edge is x=650+(1000-20)*1.1=1728 (.9).",
            },
            "sourceFilesUnchanged": True,
        },
        "nativeActionContract": {
            "commands": [
                "timeline.action.setTimelineClipTitle",
                "timeline.action.createTimelineTitle",
                "timeline.action.setTimelineClipAnimation",
            ],
            "stepCount": len(steps),
            "batching": "Apply 15 actions in batch 1 (8 existing-label setters, then 7 title creates), then 7 opacity setters in batch 2. Each batch stays within the 16-operation synchronous native draft limit. Batch 2 clip IDs must come from batch-1 create receipts.",
            "executionBatches": [
                {"batch": 1, "stepIds": [s["id"] for s in steps[:15]]},
                {"batch": 2, "stepIds": [s["id"] for s in steps[15:]]},
            ],
            "createdClipIdBinding": "Each opacity setter resolves {result,field} to the exact createTimelineTitle createdClipId receipt. If a create or setter is uncertain, inspect the task and append-only receipt; never replay a create blindly.",
            "noNewTracks": True,
        },
        "executionPreconditions": {
            "freshNativeDocumentHashAndTaskRequired": True,
            "resourceMustMatch": RESOURCE,
            "activeOutputVariantMustBe": "landscape",
            "planningTargetFieldsMustMatchExactly": True,
            "preserveExistingKeyframesBySha256": True,
            "preserveAllOutputVariantOverridesExceptActiveLandscapeTitleStyleForEightTargets": True,
            "targetPreconditions": targets,
            "destinationTrackPreconditions": track_guards,
            "abortOnAnyAmbiguousTaskMutation": True,
            "permitUnrelatedAcceptedEditsOutsideTargets": False,
            "staleGuard": "Require exact P21 document hash and each target/track snapshot at binding. If the live hash differs from the P21 saved checkpoint, stop and regenerate from a fresh project read; do not reuse these IDs or free-lane claims blindly.",
        },
        "steps": steps,
        "postconditions": {
            "existingTargets": 8,
            "createdTitles": 7,
            "newTrackCount": 0,
            "allEightBadgeOpacityAndOtherKeyframesUnchanged": True,
            "allSevenRailTitlesAreNativeEditableTitlesOnExistingFreeLanes": True,
            "railCoversOnlyGlobal27To32": True,
            "onlyActiveLandscapeTitleFieldsChangeOnEightExistingBadges": True,
            "allOtherClipsTracksMediaPathsGradesEffectsAndKeyframesRemainUnchanged": True,
            "renderReviewTimesSeconds": [20.0, 21.5, 24.0, 26.8, 27.0, 27.2, 27.7, 29.0, 31.7, 32.0, 38.0],
            "nativeAbaReview": "Compare actual output at the listed frames to P16/reference pairs and exact Remotion key timing. Verify badges have bounded dark insets, After copy/green color matches, rail builds left-to-right below the photo at frame 810 onward, remains readable without overlap, and exits by frame 960. Preserve existing sliders, numeric highlights/shadows readouts, image plates, masks, curve tool and all accepted motion.",
        },
        "preserve": [
            "All P17/P21 approved photo composition, fonts, titles, image plates, source transforms, grade/effect state, masks, grid, crop, opacity curves, and controls.",
            "All existing before/after opacity keyframes and phase timing; only text/style geometry fields in the eight exact comparison title clips are writable.",
            "The numeric HIGHLIGHTS/SHADOWS readout clips and all P17 RAW pixel-grid and flower motion.",
            "All non-landscape output variants and every non-target clip/track.",
        ],
        "claimLimit": "The rail is an editable native visual explanation of the source color pipeline. It does not prove that the current native photo plates are graded live by the RAW/color engine; the P16 22–39 comparison plates are image assets. This plan adds no image effect or claim of live grading.",
        "framePairs": frame_pairs,
        "referenceMovie": {
            "path": reference_video_path,
            "sha256": reference_video_sha256,
        },
    }

    fragment_path = OUT / "p22-photo-proof.fragment.json"
    fragment_path.write_text(json.dumps(plan, indent=2, ensure_ascii=False) + "\n")
    md = f"""# P22 Photo proof rail and comparison insets

Status: guarded native proposal only; no app/MCP calls or native project writes.

## Baseline and evidence

- P21 checkpoint: `{rel(PROJECT)}` (project SHA-256 `{EXPECTED_PROJECT_SHA}`, native document hash `{EXPECTED_DOC_HASH}`).
- P21 readback audit: `{rel(P21_AUDIT)}`; saved copy verified, 196 tracks, 1,414 clips, 1,398 unchanged original clips. Render parity remains a separate review.
- Closed P16/reference pairs reviewed: 20s (frame 600), 24s (720), 30s (900), 38s (1140). P16 was rendered against the earlier P16 document hash; the P21 guard uses its own saved hash.

## Source-correct findings

1. At 20s, the boat-photo comparison is composed cleanly in both images, but native BEFORE/AFTER copy floats over the plate. Reference uses dark inset cards; the After label is lime and reads “AFTER / THIS EDIT.” P21 still has transparent backgrounds, zero padding, and a white “AFTER” title. The fragment styles the eight existing label clips across 16–39s, reusing their existing opacity curves.
2. At 24s, the numeric HIGHLIGHTS −12 / SHADOWS +12 cards are part of the accepted photo UI work. They are outside this fragment; all those clips remain unchanged.
3. At 30s, the image and copy remain legible but the reference-only proof rail is missing. The source shows “CAMERA RAW → LINEAR LIGHT → LOOKSY COLOR → sRGB” below the image, with Looksy Color and arrows in lime. The proposal creates seven short editable title clips, staggered and faded over the exact 27–32s phase, using G20–G26, whose P21 clips have no overlap in [27,32).
4. At 38s, the curve graph is the correct phase-specific tool. The comparison labels remain but no proof rail should persist after 32s.

Badge coordinates use the source Stage (650,120) at scale 1.1. The measured reference text glyph center is y≈209.5px; set native `positionY=.194`. Before is left anchored at x=672 (`.35`), After right anchored at x=1728 (`.9`). Source CSS `#0b1324dc` is stored as native ARGB `#dc0b1324`; `backgroundPadding=14` and radius5.5 reproduce the source inset without adding shape tracks.

The seven rail items are native title clips. Their positions are derived from the source flex row and reference pixel runs (label starts 662, 845, 1054, 1261; y glyph rows 919–931). Sparse native easing approximates the source quartic/quintic opacity curves; review entry and exit frames in a native render before accepting.

## Native edit bounds

- 22 operations, split into 15 existing setters/title creates and 7 receipt-bound opacity setters.
- 8 existing comparison labels get only source text/style/background updates. Their opacity and other keyframes must retain the stored SHA-256.
- 7 new native title clips; 0 new tracks. No bitmap overlays, masks, generated images, or global reorders.
- The plan binds exact P21 hash and track/clip snapshots. Rebind from a fresh document if the live hash changes.

## Limitation

This rail is a native editable explanation of the color path. It does not close the live photo-grade/RAW engine proof gap. P16 comparison states 22–39 are static image plates, and the native photo grade implementation needs its own engine/pixel proof.

Executable fragment: `{rel(fragment_path)}`.
"""
    (OUT / "p22-photo-proof-audit.md").write_text(md)

    print(
        json.dumps(
            {
                "fragment": rel(fragment_path),
                "fragmentSha256": sha256_file(fragment_path),
                "stepCount": len(steps),
                "existingBadgeTargets": len(targets),
                "newTitles": len(labels),
                "destinationTracks": len(track_guards),
                "p21DocumentHash": EXPECTED_DOC_HASH,
                "audit": rel(OUT / "p22-photo-proof-audit.md"),
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
