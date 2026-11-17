"""Assemble native chapter commands without changing their historical receipts.

This is an offline preparation step. It does not contact Looksy or write native
project internals. The final project save is a task-owned native MCP command.
"""

import argparse
import copy
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parent


def rewrite_references(value, namespace, prior_ids):
    if isinstance(value, dict):
        if set(value) == {"result", "field"}:
            if value["result"] not in prior_ids:
                raise ValueError(f"Unknown or forward reference: {namespace}/{value['result']}")
            return {"result": namespace + "/" + value["result"], "field": value["field"]}
        return {k: rewrite_references(v, namespace, prior_ids) for k, v in value.items()}
    if isinstance(value, list):
        return [rewrite_references(v, namespace, prior_ids) for v in value]
    return value


def source_files(value):
    if isinstance(value, dict):
        for item in value.values():
            yield from source_files(item)
    elif isinstance(value, list):
        for item in value:
            yield from source_files(item)
    elif isinstance(value, str) and value.startswith("/"):
        path = Path(value)
        if not path.is_file():
            raise ValueError(f"The source is not an existing file: {value}")
        if path.name in {"landscape.mp4", "portrait.mp4"}:
            raise ValueError("The finished reference movie cannot be a source clip.")
        yield str(path.resolve(strict=True))


def assemble(orientation, project_path):
    size = (1920, 1080) if orientation == "landscape" else (1080, 1920)
    chapters = [
        ("opening", f"opening-v2-{orientation}.commands.json"),
        ("photo", f"photo-v2-{orientation}.commands.json"),
        ("library-carousel", f"library-carousel-v3-{orientation}.commands.json"),
        ("canvas-agent", f"canvas-agent-v3-{orientation}.commands.json"),
        ("film-closing", f"film-closing-v3-{orientation}.commands.json"),
        ("audio", "global-audio.commands.json"),
        ("global-veils", f"global-transition-veils-v2-{orientation}.commands.json"),
    ]
    missing = [name for _, name in chapters if not (ROOT / name).is_file()]
    if missing:
        raise ValueError("Chapter plans are still missing: " + ", ".join(missing))
    project_path = project_path.expanduser().absolute()
    if project_path.exists() or project_path.suffix != ".json":
        raise ValueError("Choose a new .json project path; existing files are never overwritten.")
    project_path = project_path.parent.resolve(strict=True) / project_path.name
    steps, provenance, assets, unresolved = [], [], set(), []
    for namespace, name in chapters:
        path = ROOT / name
        plan = json.loads(path.read_text())
        geometry = plan.get("sequence", plan.get("composition", plan.get("canvas")))
        if geometry and (geometry.get("width"), geometry.get("height")) != size:
            raise ValueError(f"{name} has the wrong output geometry.")
        ids = set()
        for step in plan["steps"]:
            if step["id"] in ids:
                raise ValueError(f"Duplicate step: {name}/{step['id']}")
            item = copy.deepcopy(step)
            item["id"] = namespace + "/" + step["id"]
            item["values"] = rewrite_references(step["values"], namespace, ids)
            if item["commandId"] in {
                "timeline.action.createTimelineTitle", "timeline.action.setTimelineClipTitle"
            }:
                style = item["values"].get("values", {})
                if style.get("backgroundColor") == "#00000000":
                    # The source DOM uses plain text boxes. Native titles have
                    # a 14px padded card by default, even with a clear fill.
                    # Explicit zero padding preserves the source anchor here.
                    style.setdefault("backgroundPadding", 0)
            assets.update(source_files(item["values"]))
            steps.append(item)
            ids.add(step["id"])
        for key in ("unresolvedParity", "unresolvedNativePrimitives"):
            for note in plan.get(key, []):
                unresolved.append({"chapter": namespace, "note": note})
        provenance.append({"plan": name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                           "steps": len(plan["steps"])})
    steps.append({"id": "delivery/save-editable-project", "commandId": "timeline.action.saveTimeline",
                  "values": {"outputPath": str(project_path)}})
    if len(assets) > 256:
        raise ValueError("The exact source allowlist exceeds native task admission limits.")
    return {"reference": f"{orientation}.mp4", "durationSeconds": 298,
            "composition": {"width": size[0], "height": size[1], "fps": 30},
            "proofStatus": "Offline assembled only; native execution and visual parity are unverified.",
            "sourcePlans": provenance, "assetPaths": sorted(assets),
            "plainTextLayout": "Transparent source text boxes explicitly use native backgroundPadding=0.",
            "outputDirectory": str(project_path.parent), "outputPath": str(project_path),
            "unresolvedParity": unresolved,
            "capabilityClarification": "Historical Canvas/Agent notes describe opaque compounds. "
                "The current controller has tested alpha-capable compounds; a durable MCP compound "
                "adapter and child-document lifecycle are still unavailable.",
            "steps": steps}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--orientation", choices=("landscape", "portrait"), required=True)
    parser.add_argument("--output-project", type=Path, required=True)
    parser.add_argument("--plan", type=Path, required=True)
    args = parser.parse_args()
    plan = assemble(args.orientation, args.output_project)
    if args.plan.exists():
        raise SystemExit("Choose a new plan filename to preserve existing execution receipts.")
    args.plan.write_text(json.dumps(plan, indent=2) + "\n")
    print(json.dumps({"plan": str(args.plan), "steps": len(plan["steps"]),
                      "sourceFiles": len(plan["assetPaths"]), "nativeExecution": "not started"}))
