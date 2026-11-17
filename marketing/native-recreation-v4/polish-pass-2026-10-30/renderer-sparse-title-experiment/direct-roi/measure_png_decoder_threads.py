#!/usr/bin/env python3
"""Small controlled FFmpeg PNG decoder-thread/RSS comparison."""
from __future__ import annotations

import json
import os
from pathlib import Path
import time

from affine_roi_probe import get_clip_rows

OUT = Path(__file__).resolve().parent / "decoder-thread-probe"


def command(thread_count: int | None, count: int, output: Path) -> tuple[list[str], list[str]]:
    entries = get_clip_rows()
    fixture = json.loads((Path(__file__).resolve().parents[1] / "fixtures" / "clips.json").read_text())
    rows = [entries[row["clipId"]] for row in fixture[:count]]
    args = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-filter_complex_threads", "1"]
    chains = []
    for index, entry in enumerate(rows):
        args += ["-loop", "1", "-framerate", "30"]
        if thread_count is not None:
            args += ["-threads", str(thread_count)]
        args += ["-i", str(entry["source"])]
        chains.append(f"[{index}:v]format=rgba[t{index}]")
    chains.append("[t0]null[base0]")
    for index in range(1, count):
        chains.append(
            f"[base{index-1}][t{index}]overlay=shortest=1:eof_action=pass:format=auto[base{index}]"
        )
    args += [
        "-filter_complex", ";".join(chains), "-map", f"[base{count-1}]", "-frames:v", "1",
        "-pix_fmt", "rgba", "-f", "framemd5", "-y", str(output),
    ]
    return args, [entry["fixture"]["clipId"] for entry in rows]


def run(kind: str, count: int, thread_count: int | None) -> dict:
    OUT.mkdir(parents=True, exist_ok=True)
    output = OUT / f"{kind}-{count}.framemd5"
    stderr = OUT / f"{kind}-{count}.stderr.txt"
    args, clip_ids = command(thread_count, count, output)
    start = time.perf_counter()
    pid = os.fork()
    if pid == 0:
        with stderr.open("wb") as stream:
            os.dup2(stream.fileno(), 2)
            os.execvp(args[0], args)
    _pid, status, usage = os.wait4(pid, 0)
    elapsed = time.perf_counter() - start
    if not os.WIFEXITED(status) or os.WEXITSTATUS(status) != 0:
        raise RuntimeError(f"{kind} failed; inspect {stderr}")
    records = [line for line in output.read_text().splitlines() if line and not line.startswith("#")]
    return {
        "kind": kind,
        "inputCount": count,
        "decoderThreads": thread_count if thread_count is not None else "default",
        "wallSeconds": round(elapsed, 3),
        "maxRssBytes": int(usage.ru_maxrss),
        "frameRecord": records[-1] if records else None,
        "inputClipIds": clip_ids,
        "stderrPath": str(stderr),
    }


def main():
    count = 8
    cases = [run("default", count, None), run("threads1", count, 1)]
    cases[0]["exactFrameHashMatch"] = cases[0]["frameRecord"] == cases[1]["frameRecord"]
    cases[1]["exactFrameHashMatch"] = cases[0]["exactFrameHashMatch"]
    baseline, serial = cases
    result = {
        "decoderCapabilities": "FFmpeg `png` decoder reports `threads frame` on this host.",
        "scope": "Eight preserved 1920x1080 P8 title PNGs through the same simple RGBA overlay chain; no full native pad/crop graph.",
        "runs": cases,
        "peakRssChangePercent": round(100 * (serial["maxRssBytes"] / baseline["maxRssBytes"] - 1), 3),
        "wallChangePercent": round(100 * (serial["wallSeconds"] / baseline["wallSeconds"] - 1), 3),
        "limitations": [
            "A short decoder-thread probe cannot establish the effect in the full 112-input native project graph.",
            "The flags here are local FFmpeg experiments; no app sources or export settings were changed.",
        ],
    }
    (OUT / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
