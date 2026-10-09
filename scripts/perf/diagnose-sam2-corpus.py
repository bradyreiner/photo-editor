#!/usr/bin/env python3
"""Run pinned prompted-mask diagnostics across the local creative photo corpus."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any


PROMPTS: dict[str, str] = {
    "public-obama-portrait.jpg": "0.50,0.18,+;0.50,0.45,+;0.52,0.70,+;0.10,0.25,-;0.90,0.55,-",
    "portrait-close-woman.jpg": "0.54,0.50,+;0.58,0.68,+;0.05,0.20,-;0.05,0.72,-",
    "portrait-ursula-von-der-leyen.jpg": "0.50,0.31,+;0.53,0.68,+;0.05,0.45,-;0.95,0.15,-",
    "architecture-academic-building.jpg": "0.52,0.40,+;0.72,0.55,+;0.08,0.08,-;0.08,0.60,-",
    "architecture-glass-house.jpg": "0.22,0.34,+;0.28,0.55,+;0.92,0.30,-;0.70,0.88,-",
    "night-chicago-building.jpg": "0.52,0.32,+;0.51,0.57,+;0.90,0.08,-;0.14,0.68,-",
    "night-hong-kong-typhoon.jpg": "0.64,0.38,+;0.64,0.62,+;0.08,0.16,-;0.92,0.78,-;0.50,0.10,-;0.78,0.20,-",
    "product-glass-bottle.jpg": "0.50,0.47,+;0.52,0.66,+;0.05,0.10,-;0.94,0.90,-",
    "product-mirto-bottle.jpg": "0.50,0.39,+;0.50,0.61,+;0.08,0.10,-;0.92,0.85,-",
    "landscape-mountain-panorama.jpg": "0.38,0.65,+;0.50,0.70,+;0.62,0.65,+;0.45,0.20,-;0.55,0.20,-;0.10,0.30,-;0.90,0.30,-",
    "landscape-mountain.jpg": "0.50,0.48,+;0.66,0.53,+;0.90,0.08,-;0.08,0.90,-",
    "monochrome-lyskamm.jpg": "0.50,0.39,+;0.65,0.42,+;0.10,0.08,-;0.10,0.91,-",
}


def run(command: list[str], *, cwd: Path) -> str:
    completed = subprocess.run(
        command, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
    )
    print(completed.stdout, end="")
    if completed.returncode:
        raise RuntimeError(f"command failed with exit code {completed.returncode}: {command[0]}")
    return completed.stdout


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True, help="Built ai_model_runtime_inference_test binary")
    parser.add_argument("--corpus-root", type=Path, required=True, help="Prepared 12-photo corpus directory")
    parser.add_argument("--output-dir", type=Path, required=True, help="Directory for masks and diagnostic JSON")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[2]
    corpus_root = args.corpus_root.expanduser().resolve()
    output_dir = args.output_dir.expanduser().resolve()
    binary = args.binary.expanduser().resolve()
    if not binary.is_file():
        parser.error(f"inference test binary does not exist: {binary}")
    manifest_path = corpus_root / "manifest.json"
    run(
        [sys.executable, "-B", str(repo / "scripts/perf/prepare-creative-tools-corpus.py"), str(corpus_root), "--verify-only"],
        cwd=repo,
    )
    run(
        [sys.executable, "-B", str(repo / "scripts/perf/check-real-image-corpus.py"), str(manifest_path), "--creative-tools"],
        cwd=repo,
    )
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    assets: dict[str, dict[str, Any]] = {}
    for asset in manifest.get("assets", []):
        filename = Path(str(asset.get("path", ""))).name
        if filename in PROMPTS:
            assets[filename] = asset
    if set(assets) != set(PROMPTS):
        raise RuntimeError("the verified creative corpus does not match the pinned SAM2 prompt set")

    output_dir.mkdir(parents=True, exist_ok=True)
    records = []
    for filename in sorted(PROMPTS):
        asset = assets[filename]
        source_path = corpus_root / str(asset["path"])
        mask_path = output_dir / f"{Path(filename).stem}-mask.png"
        output = run(
            [
                str(binary), "--smart-select-photo", str(source_path),
                PROMPTS[filename], str(mask_path),
            ],
            cwd=repo,
        )
        match = re.search(
            r"photo=(\d+)x(\d+) coverage=([0-9.]+) selectedPixels=(\d+) elapsedMs=(\d+)",
            output,
        )
        prompt_lines = re.findall(
            r"prompt=(positive|negative) x=([0-9.]+) y=([0-9.]+) alpha=(\d+)", output
        )
        if not match or not mask_path.is_file():
            raise RuntimeError(f"missing SAM2 result or saved mask for {filename}")
        records.append(
            {
                "assetId": asset["asset_id"],
                "filename": filename,
                "sourceSha256": asset["sha256"],
                "prompts": [
                    {
                        "polarity": polarity,
                        "x": float(x),
                        "y": float(y),
                        "alpha": int(alpha),
                    }
                    for polarity, x, y, alpha in prompt_lines
                ],
                "width": int(match.group(1)),
                "height": int(match.group(2)),
                "coverageAtAlpha96": float(match.group(3)),
                "selectedPixelsAtAlpha96": int(match.group(4)),
                "elapsedMs": int(match.group(5)),
                "maskPath": mask_path.name,
                "maskSha256": sha256(mask_path),
            }
        )

    receipt = {
        "schema": "looksy-sam2-corpus-diagnostic-v1",
        "modelRevision": "2b467a10bfd7d49ba5fe6a00fbee4351f0b1a739",
        "notHumanVisualQualification": True,
        "checks": "Every explicit prompt sample matched its polarity, positive samples formed a connected component, and each mask coverage was between 0 and 92 percent.",
        "maskAlphaCalibration": "Per-candidate scale starts at 0.40 and increases only as needed to keep negative prompts below alpha 96 while positive prompts remain above alpha 96.",
        "assets": records,
    }
    receipt_path = output_dir / "sam2-corpus-diagnostic.json"
    receipt_path.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(f"SAM2 corpus diagnostic: {len(records)}/{len(PROMPTS)} prompt sets passed")
    print(f"receipt={receipt_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"SAM2 corpus diagnostic failed: {error}", file=sys.stderr)
        raise SystemExit(1)
