#!/usr/bin/env python3
"""Capture repeatable real-photo Optical Diffusion review variants."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Any


STYLES: dict[str, dict[str, float]] = {
    "restrained": {"radius": 20, "strength": 45, "warmth": 10, "protection": 80},
    "default": {"radius": 24, "strength": 80, "warmth": 20, "protection": 0},
    "expressive": {"radius": 32, "strength": 140, "warmth": 35, "protection": 20},
}
ASSETS = (
    "public-obama-portrait.jpg",
    "night-chicago-building.jpg",
    "product-glass-bottle.jpg",
    "landscape-mountain.jpg",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(command: list[str], *, cwd: Path, env: dict[str, str], log_path: Path) -> str:
    completed = subprocess.run(
        command,
        cwd=cwd,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    log_path.write_text(completed.stdout, encoding="utf-8")
    print(completed.stdout, end="")
    if completed.returncode:
        raise RuntimeError(f"command failed with exit code {completed.returncode}: {command[0]}")
    return completed.stdout


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True, help="Built photo_effect_workflow_test executable")
    parser.add_argument("--corpus-root", type=Path, required=True, help="Prepared licensed creative-tools corpus")
    parser.add_argument("--output-dir", type=Path, required=True, help="Directory for review renders and receipt")
    parser.add_argument("--asset", choices=ASSETS, action="append", help="Limit capture to a selected corpus photo; repeat as needed")
    parser.add_argument("--style", choices=tuple(STYLES), action="append", help="Limit capture to a review strength; repeat as needed")
    parser.add_argument("--threshold", type=float, default=0.8, help="Highlight threshold for this photo set (default: 0.8)")
    parser.add_argument("--fit-preview", action="store_true", help="Allow the workflow test to scale inputs to 640x512")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[2]
    binary = args.binary.expanduser().resolve()
    corpus_root = args.corpus_root.expanduser().resolve()
    output_dir = args.output_dir.expanduser().resolve()
    if not binary.is_file():
        parser.error(f"workflow test binary does not exist: {binary}")
    if not (corpus_root / "manifest.json").is_file():
        parser.error(f"creative-tools corpus manifest is missing under {corpus_root}")
    output_dir.mkdir(parents=True, exist_ok=True)

    run(
        [sys.executable, "-B", str(repo / "scripts/perf/prepare-creative-tools-corpus.py"), str(corpus_root), "--verify-only"],
        cwd=repo,
        env=os.environ.copy(),
        log_path=output_dir / "corpus-verify.log",
    )
    run(
        [sys.executable, "-B", str(repo / "scripts/perf/check-real-image-corpus.py"), str(corpus_root / "manifest.json"), "--creative-tools"],
        cwd=repo,
        env=os.environ.copy(),
        log_path=output_dir / "corpus-contract.log",
    )
    if not args.fit_preview:
        os.environ["PHOTO_EFFECT_TEST_PRESERVE_RESOLUTION"] = "1"

    manifest = json.loads((corpus_root / "manifest.json").read_text(encoding="utf-8"))
    asset_map = {Path(str(item.get("path", ""))).name: item for item in manifest.get("assets", [])}
    selected_assets = args.asset or list(ASSETS)
    selected_styles = args.style or list(STYLES)
    if any(name not in asset_map for name in selected_assets):
        raise RuntimeError("the requested F1 review photo is absent from the verified corpus")

    records: list[dict[str, Any]] = []
    for name in selected_assets:
        asset = asset_map[name]
        source = corpus_root / str(asset["path"])
        for style_name in selected_styles:
            parameters = {**STYLES[style_name], "highlightThreshold": args.threshold}
            capture_dir = output_dir / style_name / Path(name).stem
            capture_dir.mkdir(parents=True, exist_ok=True)
            env = os.environ.copy()
            env.update(
                {
                    "QT_QPA_PLATFORM": "offscreen",
                    "PHOTO_EFFECT_TEST_NO_MASK": "1",
                    "PHOTO_EFFECT_REVIEW_ONLY": "1",
                    "PHOTO_EFFECT_TEST_SOURCE": str(source),
                    "PHOTO_EFFECT_EVIDENCE_DIR": str(capture_dir),
                    "PHOTO_EFFECT_REVIEW_RADIUS": str(parameters["radius"]),
                    "PHOTO_EFFECT_REVIEW_STRENGTH": str(parameters["strength"]),
                    "PHOTO_EFFECT_REVIEW_WARMTH": str(parameters["warmth"]),
                    "PHOTO_EFFECT_REVIEW_PROTECTION": str(parameters["protection"]),
                    "PHOTO_EFFECT_REVIEW_THRESHOLD": str(args.threshold),
                }
            )
            if not args.fit_preview:
                env["PHOTO_EFFECT_TEST_PRESERVE_RESOLUTION"] = "1"
            else:
                env.pop("PHOTO_EFFECT_TEST_PRESERVE_RESOLUTION", None)
            output = run(
                [str(binary)],
                cwd=repo,
                env=env,
                log_path=capture_dir / "workflow.log",
            )
            metric_path = capture_dir / "diffusion-only-metrics.json"
            output_path = capture_dir / "diffusion-only-export.png"
            if not metric_path.is_file() or not output_path.is_file():
                raise RuntimeError(f"the isolated effect evidence is incomplete for {name}/{style_name}")
            metrics = json.loads(metric_path.read_text(encoding="utf-8"))
            if int(metrics.get("changedPixels", 0)) <= 0:
                raise RuntimeError(f"Optical Diffusion produced no output change for {name}/{style_name}")
            if not args.fit_preview:
                dimensions = subprocess.run(
                    ["sips", "-g", "pixelWidth", "-g", "pixelHeight", str(source)],
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                )
                width = re.search(r"pixelWidth:\s*(\d+)", dimensions.stdout)
                height = re.search(r"pixelHeight:\s*(\d+)", dimensions.stdout)
                if dimensions.returncode or not width or not height:
                    raise RuntimeError(f"could not read source dimensions for {name}")
                if (int(metrics["width"]), int(metrics["height"])) != (int(width.group(1)), int(height.group(1))):
                    raise RuntimeError(f"full-resolution capture dimensions do not match the licensed source for {name}/{style_name}")
            records.append(
                {
                    "assetId": asset["asset_id"],
                    "filename": name,
                    "sourceSha256": asset["sha256"],
                    "style": style_name,
                    "parameters": parameters,
                    "metrics": metrics,
                    "output": str(output_path.relative_to(output_dir)),
                    "outputSha256": sha256(output_path),
                    "baselineSha256": sha256(capture_dir / "diffusion-only-baseline.png"),
                    "differenceImage": str((capture_dir / "diffusion-only-difference.png").relative_to(output_dir)),
                    "workflowPassed": "exact PNG export passed" in output,
                }
            )

    receipt_path = output_dir / "optical-diffusion-review-capture.json"
    previous_records: list[dict[str, Any]] = []
    if receipt_path.is_file():
        previous_receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
        if bool(previous_receipt.get("fullResolutionInputs")) != (not args.fit_preview):
            raise RuntimeError("cannot mix fit-preview and full-resolution captures in one review receipt")
        previous_records = list(previous_receipt.get("assets", []))
    records_by_variant = {
        (str(record.get("filename")), str(record.get("style"))): record
        for record in previous_records
    }
    records_by_variant.update(
        {(str(record.get("filename")), str(record.get("style"))): record for record in records}
    )
    all_records = [records_by_variant[key] for key in sorted(records_by_variant)]
    receipt = {
        "schema": "looksy-optical-diffusion-review-capture-v1",
        "fullResolutionInputs": not args.fit_preview,
        "humanReviewRequired": True,
        "notAQualificationReceipt": True,
        "assets": all_records,
    }
    receipt_path.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    required = [record for record in all_records if record["filename"] in ASSETS]
    print(
        f"Optical Diffusion required review renders: {len(required)}/{len(ASSETS) * len(STYLES)}; "
        f"{len(all_records)} total retained including supplemental photos; {len(records)} refreshed"
    )
    print(f"receipt={receipt_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"Optical Diffusion review capture failed: {error}", file=sys.stderr)
        raise SystemExit(1)
