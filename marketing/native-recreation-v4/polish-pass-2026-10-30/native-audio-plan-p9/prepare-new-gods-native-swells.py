#!/usr/bin/env python3
"""Write the two original procedural swells as independent native-import WAV stems.

This reproducible artifact writes only beneath this script's generated-assets folder.
The seed, random draw order, smoothing, envelope, and pan equations mirror
build-polish-v4-score.py; it does not read, overwrite, or bake the old master.
"""
from __future__ import annotations

import hashlib
import json
import subprocess
from pathlib import Path

import numpy as np

SAMPLE_RATE = 48_000
OUTPUT_DIR = Path(__file__).resolve().parent / "generated-assets"


def smooth(t: np.ndarray, start: float, end: float) -> np.ndarray:
    u = np.clip((t - start) / (end - start), 0.0, 1.0)
    return u**3 * (10.0 + u * (-15.0 + 6.0 * u))


def write_float_wav(path: Path, stereo: np.ndarray) -> None:
    payload = np.asarray(stereo, dtype="<f4").tobytes(order="C")
    subprocess.run(
        [
            "ffmpeg",
            "-hide_banner",
            "-loglevel",
            "error",
            "-f",
            "f32le",
            "-ar",
            str(SAMPLE_RATE),
            "-ac",
            "2",
            "-i",
            "pipe:0",
            "-c:a",
            "pcm_f32le",
            "-y",
            str(path),
        ],
        input=payload,
        check=True,
    )


def main() -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(28)
    records = []
    for start, duration, pan_moves in (
        (123.9, 1.8, False),
        (134.4, 2.9, True),
    ):
        count = round(duration * SAMPLE_RATE)
        t = np.arange(count) / SAMPLE_RATE
        noise = rng.normal(0.0, 1.0, count)
        shaped = (
            np.convolve(noise, np.ones(9) / 9.0, mode="same")
            * np.sin(np.linspace(0.0, np.pi, count)) ** 0.6
            * 0.034
        )
        pan = smooth(t, 0.0, duration) if pan_moves else np.full(count, 0.5)
        stereo = np.stack(
            [shaped * np.sqrt(1.0 - pan), shaped * np.sqrt(pan)], axis=1
        )
        filename = f"new-gods-swell-{start:.1f}.wav"
        output = OUTPUT_DIR / filename
        write_float_wav(output, stereo)
        records.append(
            {
                "sequenceStartSeconds": start,
                "durationSeconds": duration,
                "sampleRateHz": SAMPLE_RATE,
                "channels": 2,
                "codec": "pcm_f32le",
                "path": str(output),
                "sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
                "generatorSeed": 28,
                "panMovesLeftToRight": pan_moves,
                "originalMasterOutputGainDbToApplyNatively": 1.0,
            }
        )
    (OUTPUT_DIR / "manifest.json").write_text(json.dumps(records, indent=2) + "\n")
    print(f"Wrote {len(records)} native swell stems under {OUTPUT_DIR}")


if __name__ == "__main__":
    main()
