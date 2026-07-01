#!/usr/bin/env python3
"""Self-test the desktop preview proof gate."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from copy import deepcopy
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
GATE = REPO_ROOT / "scripts/perf/run-desktop-preview-proof-gate.py"


def event(name: str, timestamp: str, fields: dict[str, Any] | None = None) -> dict[str, Any]:
    payload = dict(fields or {})
    payload.setdefault("event", name)
    return {
        "timestamp": timestamp,
        "event": name,
        "fields": payload,
    }


def base_records(file_path: str = "/Users/test/Pictures/real-large-image.dng") -> list[dict[str, Any]]:
    return [
        event("desktop.startup.ready", "2026-05-29T00:00:00.000Z"),
        event(
            "preview.render.completed",
            "2026-05-29T00:00:00.100Z",
            {
                "stage": "interactive",
                "render_mode": "settled_preview",
                "render_reason": "edit_change",
                "dirty_domain": "detail_spatial",
                "backend": "metal",
                "source_width": 4096,
                "source_height": 3072,
                "output_width": 1440,
                "output_height": 1080,
                "file_path": file_path,
                "elapsed_ms": 32,
                "gpu_readback_count": 0,
                "adjustment_gpu_command_buffer_count": 1,
                "adjustment_gpu_kernel_dispatch_count": 4,
                "adjustment_gpu_cpu_to_gpu_bytes": 16777216,
            },
        ),
        event(
            "preview.render.viewport_max_preview_scheduled",
            "2026-05-29T00:00:00.200Z",
            {
                "render_mode": "max_preview",
                "render_reason": "viewport_change",
                "dirty_domain": "publish_texture",
                "planned_visible_tile_count": 1,
                "tile_visible_region_x": 0.5,
                "tile_visible_region_y": 0.5,
                "tile_visible_region_width": 0.125,
                "tile_visible_region_height": 0.125,
            },
        ),
        event(
            "preview.render.completed",
            "2026-05-29T00:00:00.250Z",
            {
                "stage": "full",
                "render_mode": "max_preview_visible_tile",
                "render_reason": "viewport_change",
                "dirty_domain": "publish_texture",
                "backend": "adjusted_cache",
                "source_width": 4096,
                "source_height": 3072,
                "file_path": file_path,
                "tile_visible_region_active": True,
                "partial_texture_upload_candidate": True,
                "texture_patch_only": True,
                "texture_upload_image": True,
                "executed_visible_tile_count": 1,
                "executed_tile_pixels": 262144,
                "gpu_readback_count": 0,
                "elapsed_ms": 18,
            },
        ),
        event(
            "preview.texture.uploaded",
            "2026-05-29T00:00:00.260Z",
            {
                "render_mode": "max_preview_visible_tile",
                "render_reason": "viewport_change",
                "dirty_domain": "publish_texture",
                "partial_upload": True,
                "patch_upload": True,
                "upload_pixels": 262144,
                "full_pixels": 12582912,
                "upload_width": 512,
                "upload_height": 512,
                "elapsed_ms": 1,
            },
        ),
        event(
            "preview.texture.upload_preserved",
            "2026-05-29T00:00:00.300Z",
            {
                "render_reason": "edit_change",
                "dirty_domain": "detail_spatial",
                "viewport_active": True,
                "native_width": 4096,
                "native_height": 3072,
                "result_width": 1440,
                "result_height": 1080,
            },
        ),
        event(
            "preview.render.cancelled",
            "2026-05-29T00:00:00.320Z",
            {
                "stage": "full",
                "render_mode": "max_preview",
                "render_reason": "edit_change",
                "dirty_domain": "detail_spatial",
                "generation": 7,
                "latest_generation": 8,
                "elapsed_ms": 2,
                "encode_ms": 0,
            },
        ),
        event(
            "preview.render.completed",
            "2026-05-29T00:00:00.500Z",
            {
                "stage": "full",
                "render_mode": "max_preview",
                "render_reason": "edit_change",
                "dirty_domain": "detail_spatial",
                "backend": "metal",
                "source_width": 4096,
                "source_height": 3072,
                "file_path": file_path,
                "tile_visible_region_active": True,
                "planned_background_tile_count": 16,
                "gpu_readback_count": 0,
                "elapsed_ms": 120,
                "adjustment_gpu_command_buffer_count": 1,
                "adjustment_gpu_kernel_dispatch_count": 4,
                "adjustment_gpu_cpu_to_gpu_bytes": 50331648,
            },
        ),
    ]


def write_jsonl(directory: Path, name: str, records: list[dict[str, Any]]) -> Path:
    path = directory / name
    with path.open("w", encoding="utf-8") as handle:
        for record in records:
            handle.write(json.dumps(record, sort_keys=True) + "\n")
    return path


def run_gate(log_path: Path, *extra: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "-B", str(GATE), str(log_path), *extra],
        cwd=REPO_ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )


def expect_pass(log_path: Path, *extra: str) -> None:
    result = run_gate(log_path, *extra)
    if result.returncode != 0:
        raise SystemExit(f"Expected desktop proof gate to pass for {log_path}:\n{result.stdout}")
    if "desktop_preview_proof_gate=passed" not in result.stdout and "desktop_preview_proof_gate=smoke_passed" not in result.stdout:
        raise SystemExit(f"Desktop proof pass output missing gate marker:\n{result.stdout}")


def expect_fail(log_path: Path, expected_message: str, *extra: str) -> None:
    result = run_gate(log_path, *extra)
    if result.returncode == 0:
        raise SystemExit(f"Expected desktop proof gate to fail for {log_path}, but it passed")
    if expected_message not in result.stdout:
        raise SystemExit(
            "Desktop proof gate failed with the wrong message for "
            f"{log_path}:\nexpected substring: {expected_message!r}\nactual:\n{result.stdout}"
        )


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="desktop-preview-proof-") as temporary:
        root = Path(temporary)
        real_source = root / "real-large-image.dng"
        real_source.write_bytes(b"proof-source")
        valid_records = base_records(str(real_source))
        valid_log = write_jsonl(root, "valid.jsonl", valid_records)
        expect_pass(valid_log)
        expect_pass(valid_log, "--expected-source-image", str(real_source))
        other_source = root / "other-real-large-image.dng"
        other_source.write_bytes(b"other-proof-source")
        expect_fail(
            valid_log,
            "Desktop proof source paths do not include expected source image",
            "--expected-source-image",
            str(other_source),
        )

        synthetic = base_records("/tmp/PreviewBenchmarkDriver/preview-tile-source.png")
        expect_fail(
            write_jsonl(root, "synthetic.jsonl", synthetic),
            "Desktop proof uses benchmark/synthetic image paths.",
        )
        expect_pass(
            write_jsonl(root, "synthetic-smoke.jsonl", synthetic),
            "--allow-synthetic",
            "--allow-non-metal",
        )

        non_metal = deepcopy(valid_records)
        non_metal[1]["fields"]["backend"] = "cpu"
        non_metal[1]["fields"]["fallback_reason"] = "metal_unavailable:no_system_metal_device"
        expect_fail(
            write_jsonl(root, "non-metal.jsonl", non_metal),
            "Desktop proof contains metal_unavailable fallback records.",
        )

        missing_source_path = deepcopy(valid_records)
        for record in missing_source_path:
            record["fields"].pop("file_path", None)
        expect_fail(
            write_jsonl(root, "missing-source-path.jsonl", missing_source_path),
            "Desktop proof release mode requires at least one nonempty source file_path",
        )

        nonexistent_source_path = base_records(str(root / "does-not-exist.dng"))
        expect_fail(
            write_jsonl(root, "nonexistent-source-path.jsonl", nonexistent_source_path),
            "Desktop proof release mode requires at least one source file_path that exists",
        )

        readback = deepcopy(valid_records)
        readback[1]["fields"]["gpu_readback_count"] = 1
        expect_fail(
            write_jsonl(root, "readback.jsonl", readback),
            "Preview render records contain GPU readbacks: 1",
        )

        wrong_order = deepcopy(valid_records)
        wrong_order[3]["timestamp"] = "2026-05-29T00:00:01.000Z"
        expect_fail(
            write_jsonl(root, "wrong-order.jsonl", wrong_order),
            "First visible max-preview tile patch was logged after",
        )

        split_root = root / "split-evidence"
        split_root.mkdir()
        write_jsonl(split_root, "startup-and-renders.jsonl", valid_records[:4])
        write_jsonl(split_root, "uploads-and-cancellation.jsonl", valid_records[4:])
        expect_fail(
            split_root,
            "Desktop proof release mode requires all accepted evidence records to come from one JSONL log",
        )

    print("desktop_preview_proof_gate_self_test=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
