#!/usr/bin/env python3
"""Repeat the 112-title comparison with bounded PNG decoder threads."""
from __future__ import annotations

import bench_112_affine_graph as benchmark

benchmark.OUT = benchmark.OUT / "bounded-decoder-policy"
_command = benchmark.command


def command(frame: int, kind: str):
    args, result_path, counts, estimates = _command(frame, kind)
    for index in range(len(args) - 1, -1, -1):
        if args[index] == "-i":
            args[index:index] = ["-threads", "1"]
    return args, result_path, counts, estimates


benchmark.command = command

if __name__ == "__main__":
    benchmark.main()
