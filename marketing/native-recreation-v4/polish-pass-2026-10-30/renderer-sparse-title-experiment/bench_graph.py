#!/usr/bin/env python3
"""Reproduce the isolated 112-title one-frame FFmpeg parity/resource comparison."""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import time

ROOT = Path(__file__).resolve().parent
FIXTURES = ROOT / "fixtures"
OUT = ROOT / "rerun"


def input_rows():
    rows = []
    for line in (FIXTURES / "sparse-map.tsv").read_text().splitlines():
        path, x, y, width, height, is_sparse = line.split("\t")
        rows.append((ROOT / path, int(x), int(y), int(width), int(height), is_sparse == "1"))
    return rows


def command(kind: str):
    rows = input_rows()
    source_paths = [ROOT / line for line in (FIXTURES / "source-order.txt").read_text().splitlines()]
    assert len(rows) == len(source_paths) == 112
    args = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-filter_complex_threads", "1"]
    for index, (sparse_path, _x, _y, _width, _height, is_sparse) in enumerate(rows):
        path = sparse_path if kind == "crop-pad" and is_sparse else source_paths[index]
        args += ["-loop", "1", "-framerate", "30", "-i", str(path)]
    chains = []
    for index, (_path, x, y, _width, _height, is_sparse) in enumerate(rows):
        if kind == "crop-pad" and is_sparse:
            chains.append(
                f"[{index}:v]format=rgba,pad=1920:1080:{x}:{y}:color=black@0,format=rgba[t{index}]"
            )
        else:
            chains.append(f"[{index}:v]format=rgba[t{index}]")
    chains.append("[t0]null[base0]")
    last = "base0"
    for index in range(1, len(rows)):
        output = f"base{index}"
        chains.append(
            f"[{last}][t{index}]overlay=shortest=1:eof_action=pass:format=auto[{output}]"
        )
        last = output
    output_path = OUT / f"{kind}.framemd5"
    args += [
        "-filter_complex", ";".join(chains), "-map", f"[{last}]", "-frames:v", "1",
        "-pix_fmt", "rgba", "-f", "framemd5", "-y", str(output_path),
    ]
    return args


def run(kind: str):
    args = command(kind)
    stderr_path = OUT / f"{kind}.stderr.txt"
    OUT.mkdir(parents=True, exist_ok=True)
    start = time.perf_counter()
    pid = os.fork()
    if pid == 0:
        with stderr_path.open("wb") as stderr_file:
            os.dup2(stderr_file.fileno(), 2)
            os.execvp(args[0], args)
    _pid, status, usage = os.wait4(pid, 0)
    elapsed = time.perf_counter() - start
    if not os.WIFEXITED(status) or os.WEXITSTATUS(status) != 0:
        raise RuntimeError(f"{kind} FFmpeg failed; see {stderr_path}")
    frame_lines = [
        line for line in (OUT / f"{kind}.framemd5").read_text().splitlines()
        if line and not line.startswith("#")
    ]
    return {
        "case": kind,
        "wallSeconds": round(elapsed, 3),
        "maxRssBytes": int(usage.ru_maxrss),
        "frameRecord": frame_lines[-1] if frame_lines else None,
    }


def main():
    if shutil.which("ffmpeg") is None:
        raise SystemExit("ffmpeg not found on PATH")
    result = {"runs": [run("original"), run("crop-pad")]}
    result["frameHashesEqual"] = (
        result["runs"][0]["frameRecord"] == result["runs"][1]["frameRecord"]
    )
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
