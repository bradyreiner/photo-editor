#!/usr/bin/env python3
"""Create hook variants and render a repeatable queue without editing masters."""
import argparse
import copy
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--campaign", choices=["all", "creative-control", "one-shoot-many-deliverables"], default="all")
    parser.add_argument("--variant", default="baseline", help="baseline, founder, challenge, or all")
    parser.add_argument("--size", choices=["preview", "final"], default="preview")
    parser.add_argument("--mode", choices=["draft", "final"], default="draft")
    parser.add_argument("--aspect", choices=["all", "9:16", "4:5", "16:9"], default="all")
    parser.add_argument("--prepare-only", action="store_true", help="validate and save recipes without rendering")
    args = parser.parse_args()
    campaign_dirs = sorted((ROOT / "campaigns").glob("*/campaign.json"))
    if args.campaign != "all":
        campaign_dirs = [p for p in campaign_dirs if p.parent.name == args.campaign]
    for master_path in campaign_dirs:
        master = json.loads(master_path.read_text())
        variants = [{"id": "baseline"}] + json.loads((master_path.parent / "hooks.json").read_text())
        selected = variants if args.variant == "all" else [v for v in variants if v["id"] == args.variant]
        if not selected:
            parser.error(f"variant {args.variant!r} is not defined for {master['campaign_id']}")
        for variant in selected:
            recipe = copy.deepcopy(master)
            recipe["variant_id"] = variant["id"]
            if variant.get("headline"):
                recipe["scenes"][0]["headline"] = variant["headline"]
                recipe["scenes"][0]["body"] = variant.get("body", recipe["scenes"][0]["body"])
            destination = ROOT / "build" / master["campaign_id"] / variant["id"] / args.size
            destination.mkdir(parents=True, exist_ok=True)
            recipe_path = destination / "recipe.json"
            recipe_path.write_text(json.dumps(recipe, indent=2) + "\n")
            tool = ROOT / "tools/render_campaign.py"
            subprocess.run([sys.executable, str(tool), "validate", str(recipe_path), "--mode", args.mode], check=True)
            if args.prepare_only:
                continue
            if args.aspect == "all":
                command = ["batch", str(recipe_path), "--out", str(destination)]
            else:
                command = ["render", str(recipe_path), "--aspect", args.aspect,
                           "--out", str(destination / args.aspect.replace(":", "x"))]
            subprocess.run([sys.executable, str(tool), *command, "--size", args.size, "--mode", args.mode], check=True)
    print("Queue finished. Build the review library with tools/build_review.py.")


if __name__ == "__main__":
    main()
