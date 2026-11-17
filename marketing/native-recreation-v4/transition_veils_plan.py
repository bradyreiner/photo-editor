"""Plan the reference's global, editable chapter-transition layer.

Run after all chapter plans so the single native graphics track is above them.
This builds typed MCP commands; it never writes Timeline project internals.
"""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parent
BOUNDARIES = (12, 50, 62, 92, 133, 160, 184, 252, 280)


def build(width: int, height: int) -> dict:
    steps = [{
        "id": "global-transition-track",
        "commandId": "timeline.action.createTimelineTrack",
        "values": {"kind": "graphics", "name": "Global / Travelling light veils",
                   "audioRole": ""},
    }]
    for boundary in BOUNDARIES:
        steps.append({
            "id": f"global-transition-{boundary}",
            "commandId": "timeline.action.createTimelineGenerator",
            "values": {
                "trackId": {"result": "global-transition-track", "field": "trackId"},
                "generatorKind": "transition-veil",
                "startSeconds": boundary - 0.5,
                "durationSeconds": 1.0,
                "parameters": {"opacity": 1.0},
            },
        })
    return {
        "reference": "portrait.mp4" if height > width else "landscape.mp4",
        "referenceComposition": "creative-launch/CreativeFilm.tsx",
        "referenceTransition": "creative-launch/TransitionVeil.tsx",
        "canvas": {"width": width, "height": height, "fps": 30},
        "durationSeconds": 298,
        "boundariesSeconds": list(BOUNDARIES),
        "authoringOrder": "After every chapter plan; above all chapter layers.",
        "nativeBehavior": {
            "matte": "Editable native curved travelling matte, not an imported video.",
            "lightClock": "Absolute sequence clock; native default gather 0.18 and field opacity 0.42.",
            "localClock": "Native generator local time is sequence time minus clip start minus 0.5.",
        },
        "duplicatePolicy": "Chapter v2 plans must not also create boundary veils.",
        "proofStatus": "Generated only; live MCP authoring and reference comparison pending.",
        "steps": steps,
    }


if __name__ == "__main__":
    for name, width, height in (("landscape", 1920, 1080), ("portrait", 1080, 1920)):
        path = ROOT / f"global-transition-veils-v2-{name}.commands.json"
        path.write_text(json.dumps(build(width, height), indent=2) + "\n")
        print(f"{path.name}: 1 native track, {len(BOUNDARIES)} travelling veils")
