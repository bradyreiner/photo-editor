#!/usr/bin/env python3
"""Prepare a separate P21 footer-anchor revision after bounded source/Qt review."""

from __future__ import annotations

import hashlib
import json
import math
import shutil
import subprocess
from pathlib import Path
from typing import Any


HERE = Path(__file__).resolve().parent
POLISH = HERE.parent
ROOT = POLISH.parents[2]
ORIGINAL = HERE / "p21-film-caption-layout.commands-fragment.json"
SOURCE_FRAME = HERE / "native-reference-png/reference-frame-199_5.png"
KIT = ROOT / "marketing/animated-trailer/src/creative-launch/Kit.tsx"
TYPE = ROOT / "marketing/animated-trailer/src/rebuild/Type.ts"
FONT = ROOT / "apps/desktop/assets/fonts/spacemono/SpaceMono-Regular.ttf"
APP_FONT_HEADER = ROOT / "apps/desktop/src/creative_fonts.h"
APP_MAIN = ROOT / "apps/desktop/src/main.cpp"
METRICS_HELPER = HERE / "p21_footer_qt_metrics.cpp"
V2_PATH = HERE / "p21-film-caption-layout-v2-footer-anchor.commands-fragment.json"
EVIDENCE_PATH = HERE / "p21-footer-anchor-v2-evidence.json"

EXPECTED_ORIGINAL_PLAN_SHA = "d5e860236fba3c57b9b22398602197611e46e4cf3a5f4579d37bd46c291e036c"
EXPECTED_FRAME_SHA = "cbe70efc668078670ca73dc138898eae7b840800573fc68fbad06a7b40e9bbd2"
EXPECTED_FONT_SHA = "95837e182baeeada83368f7748db28357f0a1b75c6b84ff7065b5edf933c8e18"
OLD_POSITION_Y = 0.9796296296296296
QT_LINE_SPACING = 25.15625
NEW_POSITION_Y = (1080.0 - 22.0 - QT_LINE_SPACING * 0.5) / 1080.0
PIXEL_THRESHOLD = {"minimumChannelExclusive": 118, "maxMinusMinExclusive": 48, "blueAtLeastRed": True}
FOOTERS = {
    "create-default-footer-left": {
        "trackId": "graphics-d77585ff-bc9d-42e5-9908-a2494476c9b1",
        "displayLabel": "G50",
        "laneIndex": 100049,
        "text": "FROM FIRST CUT TO FINAL FRAME.",
        "positionX": 0.042708333333333334,
    },
    "create-default-footer-right": {
        "trackId": "graphics-8e70fbee-995f-47ca-b8aa-644b39cbe9f3",
        "displayLabel": "G51",
        "laneIndex": 100050,
        "text": "COLOR · MOTION · LAYERS · AUDIO",
        "positionX": 0.9572916666666667,
    },
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def sha256(path: Path) -> str:
    return hashlib.file_digest(path.open("rb"), "sha256").hexdigest()


def measure_footer_ink() -> dict[str, Any]:
    ffmpeg = shutil.which("ffmpeg")
    require(ffmpeg is not None, "ffmpeg is needed to measure the existing source PNG.")
    raw = subprocess.run(
        [ffmpeg, "-v", "error", "-i", str(SOURCE_FRAME), "-frames:v", "1",
         "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
        check=True, capture_output=True,
    ).stdout
    width, height = 1920, 1080
    require(len(raw) == width * height * 3, "Source reference PNG is not 1920x1080 RGB.")
    regions = {"left": (50, 700), "right": (1200, 1880)}
    output: dict[str, Any] = {}
    for name, (x0, x1) in regions.items():
        xs: list[int] = []
        ys: list[int] = []
        for y in range(1000, 1080):
            for x in range(x0, x1):
                offset = (y * width + x) * 3
                red, green, blue = raw[offset:offset + 3]
                if (min(red, green, blue) > PIXEL_THRESHOLD["minimumChannelExclusive"] and
                        max(red, green, blue) - min(red, green, blue) < PIXEL_THRESHOLD["maxMinusMinExclusive"] and
                        blue >= red):
                    xs.append(x)
                    ys.append(y)
        require(xs and ys, f"No source footer ink found in the {name} scan region.")
        output[name] = {
            "bboxInclusive": [min(xs), min(ys), max(xs), max(ys)],
            "pixelCount": len(xs),
        }
    require(output["left"]["bboxInclusive"][1:] == [1040, 418, 1051] or
            output["left"]["bboxInclusive"] == [84, 1040, 418, 1051],
            f"Left source footer ink bounds changed: {output['left']}")
    require(output["right"]["bboxInclusive"] == [1486, 1040, 1835, 1051],
            f"Right source footer ink bounds changed: {output['right']}")
    return output


def main() -> None:
    original_sha = sha256(ORIGINAL)
    frame_sha = sha256(SOURCE_FRAME)
    font_sha = sha256(FONT)
    require(original_sha == EXPECTED_ORIGINAL_PLAN_SHA, "Frozen original P21 fragment changed.")
    require(frame_sha == EXPECTED_FRAME_SHA, "Bound source frame changed.")
    require(font_sha == EXPECTED_FONT_SHA, "App Space Mono font changed.")
    require(sha256(ROOT / "marketing/animated-trailer/public/fonts/rebuild/SpaceMono.ttf") == font_sha,
            "Remotion and app Space Mono font files no longer have identical bytes.")

    kit_text = KIT.read_text()
    type_text = TYPE.read_text()
    font_header = APP_FONT_HEADER.read_text()
    app_main = APP_MAIN.read_text()
    require('bottom: portrait ? 130 : 22' in kit_text and
            'fontSize: portrait ? 20 : 17' in kit_text and
            'letterSpacing: 1' in kit_text,
            "Source Footer CSS no longer matches the audited landscape footer contract.")
    require('url: staticFile("fonts/rebuild/SpaceMono.ttf")' in type_text and
            'family: "Looksy Mono"' in type_text,
            "Remotion is no longer using the audited local Space Mono font.")
    require("QFontDatabase::addApplicationFont(path)" in font_header and
            "register_creative_fonts()" in app_main,
            "Native app no longer registers its embedded creative fonts as expected.")

    ink = measure_footer_ink()
    old_y = 1080.0 * OLD_POSITION_Y
    block_center_y = 1080.0 - 22.0 - QT_LINE_SPACING * 0.5
    native_fixed_advance = 17.0 * 1.2
    native_text_height = native_fixed_advance + math.ceil(QT_LINE_SPACING) - native_fixed_advance

    plan = json.loads(ORIGINAL.read_text())
    require(plan["status"] == "proposed-unbound-not-applied" and plan.get("taskId") is None,
            "Original P21 proposal unexpectedly became bound or applied; preserve it and stop.")
    footer_steps = {step["id"]: step for step in plan["steps"] if step["id"] in FOOTERS}
    require(set(footer_steps) == set(FOOTERS), "Original proposal footer step IDs changed.")
    for step_id, expected in FOOTERS.items():
        step = footer_steps[step_id]
        values = step["values"]
        require(step["commandId"] == "timeline.action.createTimelineTitle" and
                values["trackId"] == expected["trackId"] and
                values["values"]["text"] == expected["text"] and
                values["values"]["positionX"] == expected["positionX"] and
                values["values"]["positionY"] == OLD_POSITION_Y,
                f"Frozen footer step differs from expected source/style guard: {step_id}")
        values["values"]["positionY"] = NEW_POSITION_Y

    for track_precondition in plan["executionPreconditions"]["footerDestinationTracks"]:
        for expected in FOOTERS.values():
            if track_precondition["trackId"] == expected["trackId"]:
                require(track_precondition["laneIndex"] == expected["laneIndex"],
                        f"Footer laneIndex changed for {expected['displayLabel']}.")
                track_precondition["displayLabel"] = expected["displayLabel"]
    plan["name"] = "P21 Film caption layout v2: CSS-matched footer block center"
    plan["footerAnchorRevision"] = {
        "revision": 2,
        "supersedes": "p21-film-caption-layout.commands-fragment.json only for the two new footer positionY values",
        "preserves": ["all ten title setters", "footer strings and style", "footer timing and tracks",
                      "all existing clip animation/keyframe data", "all other P21 operations"],
        "measurementEvidence": EVIDENCE_PATH.name,
        "oldPositionY": OLD_POSITION_Y,
        "oldPositionCenterPixels": old_y,
        "newPositionY": NEW_POSITION_Y,
        "newPositionCenterPixels": block_center_y,
        "noNativeProjectOrAdmittedRootPlanModified": True,
    }

    evidence = {
        "schema": "looksy.p21-footer-anchor-evidence/v1",
        "productionCallsMade": False,
        "sourceReference": {
            "path": str(SOURCE_FRAME.relative_to(ROOT)),
            "sha256": frame_sha,
            "frame": 5985,
            "seconds": 199.5,
            "resolution": [1920, 1080],
            "referencePngFooterInkBounds": ink,
            "pixelScan": {
                "method": "ffmpeg PNG decode to RGB24 followed by a direct Python RGB threshold scan",
                "threshold": PIXEL_THRESHOLD,
                "verticalScanRange": [1000, 1079],
                "leftScanXRange": [50, 699],
                "rightScanXRange": [1200, 1879],
            },
        },
        "sourceFooterCss": {
            "path": str(KIT.relative_to(ROOT)),
            "sha256": sha256(KIT),
            "landscapeBottomPixels": 22,
            "fontSizePixels": 17,
            "letterSpacingPixels": 1,
            "lineHeight": "CSS normal (not explicitly set)",
            "sourceFontAlias": "Looksy Mono",
            "blockLayout": "position:absolute; display:flex; align-items:end",
        },
        "fontParity": {
            "remotionTypeSource": str(TYPE.relative_to(ROOT)),
            "remotionTypeSha256": sha256(TYPE),
            "appFontPath": str(FONT.relative_to(ROOT)),
            "appFontSha256": font_sha,
            "remotionFontPath": "marketing/animated-trailer/public/fonts/rebuild/SpaceMono.ttf",
            "remotionFontSha256": sha256(ROOT / "marketing/animated-trailer/public/fonts/rebuild/SpaceMono.ttf"),
            "sameBytes": True,
            "nativeFontRegistration": {
                "helper": str(APP_FONT_HEADER.relative_to(ROOT)),
                "helperSha256": sha256(APP_FONT_HEADER),
                "startup": str(APP_MAIN.relative_to(ROOT)),
                "startupSha256": sha256(APP_MAIN),
                "startupRegistrationLine": 679,
            },
        },
        "qtFontMetrics": {
            "measuredBy": str(METRICS_HELPER.relative_to(ROOT)),
            "compileCommand": "clang++ -std=c++17 -F/opt/homebrew/lib -I/opt/homebrew/lib/QtGui.framework/Headers -I/opt/homebrew/lib/QtCore.framework/Headers p21_footer_qt_metrics.cpp -framework QtGui -framework QtCore -o /private/tmp/p21_footer_qt_metrics",
            "runCommand": "QT_QPA_PLATFORM=offscreen /private/tmp/p21_footer_qt_metrics apps/desktop/assets/fonts/spacemono/SpaceMono-Regular.ttf",
            "family": "Space Mono",
            "pixelSize": 17,
            "height": 25.15625,
            "ascent": 19.03125,
            "descent": 6.125,
            "lineSpacing": QT_LINE_SPACING,
            "lineAdvanceNativeTitle": native_fixed_advance,
            "nativeTitleTextHeight": native_text_height,
            "rendererMetricEvidence": "ensureTitleFrame: font.setPixelSize(titleFontSize); fixed_line_height=titleFontSize*titleLineHeight; line_height_offset=ceil(QFontMetricsF.height())-fixed_line_height; text_height=layout_height+max(line_height_offset,0).",
            "rendererSourcePath": "apps/desktop/src/timeline_document_controller.cpp",
            "rendererSourceLine": 50284,
        },
        "anchorCalculation": {
            "sourceBottomY": 1080 - 22,
            "sourceNormalLineBoxHeightEstimate": QT_LINE_SPACING,
            "sourceLineBoxTopY": 1080 - 22 - QT_LINE_SPACING,
            "sourceBlockCenterY": block_center_y,
            "currentOriginalCenterY": old_y,
            "correctionPixels": block_center_y - old_y,
            "correctedTitlePositionY": NEW_POSITION_Y,
            "reason": "The title position API anchors the text block center. The original value used the CSS bottom edge as that center. Source PNG pixels put both footer ink boxes at y1040..1051; the matched 25.15625px Space Mono line box has center y1045.421875 and is consistent with those observed glyph bounds.",
        },
        "nativeLaneIdentity": [
            {"displayLabel": "G50", "trackId": FOOTERS["create-default-footer-left"]["trackId"], "laneIndex": 100049},
            {"displayLabel": "G51", "trackId": FOOTERS["create-default-footer-right"]["trackId"], "laneIndex": 100050},
        ],
        "originalPlan": {"path": str(ORIGINAL.relative_to(ROOT)), "sha256": original_sha},
        "v2PlanPath": str(V2_PATH.relative_to(ROOT)),
        "scope": "Only the new left/right default footer title anchors change. The original fragment, root's admitted bound plan, and native project remain untouched.",
    }

    with V2_PATH.open("x") as stream:
        json.dump(plan, stream, indent=2, ensure_ascii=False)
        stream.write("\n")
    evidence["v2PlanSha256"] = sha256(V2_PATH)
    with EVIDENCE_PATH.open("x") as stream:
        json.dump(evidence, stream, indent=2, ensure_ascii=False)
        stream.write("\n")
    print(json.dumps({
        "v2Plan": str(V2_PATH),
        "evidence": str(EVIDENCE_PATH),
        "oldCenterY": old_y,
        "correctedCenterY": block_center_y,
        "correctedPositionY": NEW_POSITION_Y,
        "leftFooterInk": ink["left"],
        "rightFooterInk": ink["right"],
        "trackLabels": {"left": "G50", "right": "G51"},
    }, indent=2))


if __name__ == "__main__":
    main()
