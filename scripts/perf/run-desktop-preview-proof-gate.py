#!/usr/bin/env python3
"""Validate a real desktop max-resolution preview proof run."""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any


SYNTHETIC_PATH_MARKERS = (
    "PreviewBenchmarkDriver",
    "preview-synthetic",
    "preview-tile-source",
    "perf-corpora",
)


@dataclass(frozen=True)
class EventRecord:
    event: str
    fields: dict[str, Any]
    timestamp: datetime | None
    source: Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Validate the hardware desktop proof for max-resolution preview: "
            "zoomed viewport max tile first, native patch publication, preserved "
            "resident max-res texture during lower-res edits, cancellable stale "
            "background max-preview work, zero preview readback, and Metal-backed "
            "edit work."
        )
    )
    parser.add_argument(
        "input",
        type=Path,
        help=(
            "Observability run directory, benchmark artifact directory, desktop.jsonl, "
            "or logs/runs root."
        ),
    )
    parser.add_argument("--min-source-edge", type=int, default=2048)
    parser.add_argument("--min-viewport-schedules", type=int, default=1)
    parser.add_argument("--min-visible-tile-patches", type=int, default=1)
    parser.add_argument("--min-preserved-uploads", type=int, default=1)
    parser.add_argument("--min-cancellations", type=int, default=1)
    parser.add_argument("--max-visible-tile-p95-ms", type=float, default=150.0)
    parser.add_argument("--max-texture-upload-p95-ms", type=float, default=50.0)
    parser.add_argument("--max-cancel-p95-ms", type=float, default=150.0)
    parser.add_argument(
        "--allow-synthetic",
        action="store_true",
        help="Allow benchmark/synthetic image paths. This is only a local smoke mode.",
    )
    parser.add_argument(
        "--allow-non-metal",
        action="store_true",
        help="Do not require Metal backend/counter evidence. This is only a local smoke mode.",
    )
    parser.add_argument(
        "--expected-source-image",
        type=Path,
        help=(
            "Require release-mode render records to include this exact resolved source "
            "image path."
        ),
    )
    return parser.parse_args()


def jsonl_files_for_input(path: Path) -> list[Path]:
    resolved = path.expanduser().resolve()
    if resolved.is_file():
        if resolved.suffix == ".jsonl":
            return [resolved]
        raise SystemExit(f"Input file is not a JSONL log: {resolved}")
    if not resolved.is_dir():
        raise SystemExit(f"Input does not exist: {resolved}")

    manifest_path = resolved / "manifest.json"
    if manifest_path.is_file():
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as error:
            raise SystemExit(f"Invalid manifest JSON: {manifest_path}: {error}") from error
        desktop_log = manifest.get("desktop_log")
        if isinstance(desktop_log, str):
            candidate = Path(desktop_log).expanduser().resolve()
            if candidate.is_file():
                return [candidate]

    direct_jsonl = sorted(child for child in resolved.iterdir() if child.suffix == ".jsonl")
    if direct_jsonl:
        return direct_jsonl

    runs_root = resolved / "runs"
    if runs_root.is_dir():
        return sorted(runs_root.glob("*/*.jsonl"))

    nested_runs_root = resolved / "logs" / "runs"
    if nested_runs_root.is_dir():
        return sorted(nested_runs_root.glob("*/*.jsonl"))

    nested_jsonl = sorted(resolved.glob("*/*.jsonl"))
    if nested_jsonl:
        return nested_jsonl

    raise SystemExit(f"No JSONL logs found under {resolved}")


def parse_timestamp(raw: Any) -> datetime | None:
    if not isinstance(raw, str) or not raw:
        return None
    normalized = raw
    if normalized.endswith("Z"):
        normalized = f"{normalized[:-1]}+00:00"
    try:
        return datetime.fromisoformat(normalized)
    except ValueError:
        return None


def normalize_record(record: dict[str, Any], source: Path) -> EventRecord | None:
    fields = record.get("fields")
    if not isinstance(fields, dict):
        fields = {}
    event = record.get("event")
    if not isinstance(event, str) or not event:
        event = fields.get("event")
    if not isinstance(event, str) or not event:
        return None
    merged = dict(fields)
    for key in ("severity", "category", "timestamp", "pid", "thread_id", "session_id"):
        if key in record and key not in merged:
            merged[key] = record[key]
    if "reason" not in merged and "render_reason" in merged:
        merged["reason"] = merged["render_reason"]
    if "interactive" not in merged and isinstance(merged.get("full_resolution"), bool):
        merged["interactive"] = not merged["full_resolution"]
    return EventRecord(
        event=event,
        fields=merged,
        timestamp=parse_timestamp(record.get("timestamp")),
        source=source,
    )


def load_records(paths: list[Path]) -> list[EventRecord]:
    records: list[EventRecord] = []
    for path in paths:
        with path.open("r", encoding="utf-8") as handle:
            for line_number, line in enumerate(handle, start=1):
                try:
                    raw = json.loads(line)
                except json.JSONDecodeError as error:
                    raise SystemExit(f"Invalid JSONL at {path}:{line_number}: {error}") from error
                if not isinstance(raw, dict):
                    continue
                record = normalize_record(raw, path)
                if record is not None:
                    records.append(record)
    return records


def number(value: Any) -> float | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)) and math.isfinite(value):
        return float(value)
    if isinstance(value, str):
        try:
            parsed = float(value.strip())
        except ValueError:
            return None
        return parsed if math.isfinite(parsed) else None
    return None


def integer(value: Any) -> int | None:
    numeric = number(value)
    if numeric is None:
        return None
    return int(numeric)


def truthy(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        return value.strip().lower() == "true"
    return bool(value)


def percentile(values: list[float], fraction: float) -> float:
    if not values:
        raise SystemExit("Cannot compute percentile for empty value list")
    ordered = sorted(values)
    index = min(len(ordered) - 1, math.ceil(fraction * len(ordered)) - 1)
    return ordered[index]


def records_for(records: list[EventRecord], event: str) -> list[EventRecord]:
    return [record for record in records if record.event == event]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def has_synthetic_path(records: list[EventRecord]) -> bool:
    for record in records:
        path_value = record.fields.get("file_path")
        if not isinstance(path_value, str):
            continue
        if any(marker in path_value for marker in SYNTHETIC_PATH_MARKERS):
            return True
    return False


def nonempty_source_paths(records: list[EventRecord]) -> list[str]:
    paths: list[str] = []
    for record in records:
        path_value = record.fields.get("file_path")
        if isinstance(path_value, str) and path_value.strip():
            paths.append(path_value)
    return paths


def existing_source_paths(paths: list[str]) -> list[str]:
    existing: list[str] = []
    for path in paths:
        if Path(path).expanduser().exists():
            existing.append(path)
    return existing


def resolved_source_paths(paths: list[str]) -> set[str]:
    return {str(Path(path).expanduser().resolve()) for path in paths}


def validate_large_real_source(
    render_records: list[EventRecord],
    min_source_edge: int,
    allow_synthetic: bool,
    expected_source_image: Path | None,
) -> tuple[int, int]:
    max_source_width = 0
    max_source_height = 0
    for record in render_records:
        max_source_width = max(max_source_width, integer(record.fields.get("source_width")) or 0)
        max_source_height = max(max_source_height, integer(record.fields.get("source_height")) or 0)
    require(
        max(max_source_width, max_source_height) >= min_source_edge,
        (
            f"No render record proves a large source edge >= {min_source_edge}px "
            f"(observed {max_source_width}x{max_source_height})"
        ),
    )
    if not allow_synthetic:
        source_paths = nonempty_source_paths(render_records)
        require(
            source_paths,
            (
                "Desktop proof release mode requires at least one nonempty source "
                "file_path on render records."
            ),
        )
        require(
            not has_synthetic_path(render_records),
            (
                "Desktop proof uses benchmark/synthetic image paths. Re-run the real app "
                "with a real large image, or pass --allow-synthetic for local smoke only."
            ),
        )
        require(
            existing_source_paths(source_paths),
            (
                "Desktop proof release mode requires at least one source file_path "
                "that exists on this machine."
            ),
        )
        if expected_source_image is not None:
            resolved_expected_source = expected_source_image.expanduser().resolve()
            require(
                resolved_expected_source.is_file(),
                f"Expected source image does not exist: {resolved_expected_source}",
            )
            expected_path = str(resolved_expected_source)
            observed_paths = resolved_source_paths(source_paths)
            require(
                expected_path in observed_paths,
                (
                    "Desktop proof source paths do not include expected source image "
                    f"{expected_path}."
                ),
            )
    return max_source_width, max_source_height


def validate_viewport_schedule(records: list[EventRecord], min_count: int) -> list[EventRecord]:
    schedules = [
        record
        for record in records_for(records, "preview.render.viewport_max_preview_scheduled")
        if record.fields.get("render_mode") == "max_preview"
        and record.fields.get("dirty_domain") == "publish_texture"
        and (integer(record.fields.get("planned_visible_tile_count")) or 0) >= 1
        and (number(record.fields.get("tile_visible_region_width")) or 0.0) > 0.0
        and (number(record.fields.get("tile_visible_region_height")) or 0.0) > 0.0
    ]
    require(
        len(schedules) >= min_count,
        (
            "Missing viewport max-preview schedules with visible tiles "
            f"(needed {min_count}, found {len(schedules)})"
        ),
    )
    return schedules


def validate_visible_tile_renders(
    render_records: list[EventRecord],
    min_count: int,
    p95_limit_ms: float,
) -> list[EventRecord]:
    visible_tiles = [
        record
        for record in render_records
        if record.fields.get("stage") == "full"
        and record.fields.get("render_mode") == "max_preview_visible_tile"
        and record.fields.get("dirty_domain") == "publish_texture"
        and truthy(record.fields.get("tile_visible_region_active"))
        and truthy(record.fields.get("partial_texture_upload_candidate"))
        and truthy(record.fields.get("texture_patch_only"))
        and truthy(record.fields.get("texture_upload_image"))
        and (integer(record.fields.get("executed_visible_tile_count")) or 0) >= 1
        and (integer(record.fields.get("executed_tile_pixels")) or 0) > 0
        and (integer(record.fields.get("gpu_readback_count")) or 0) == 0
    ]
    require(
        len(visible_tiles) >= min_count,
        (
            "Missing visible max-preview tile render completions "
            f"(needed {min_count}, found {len(visible_tiles)})"
        ),
    )
    elapsed = [number(record.fields.get("elapsed_ms")) for record in visible_tiles]
    require(all(value is not None for value in elapsed), "Visible tile renders are missing elapsed_ms")
    p95 = percentile([value for value in elapsed if value is not None], 0.95)
    require(
        p95 <= p95_limit_ms,
        f"Visible max-tile p95 {p95:.1f} ms exceeds {p95_limit_ms:.1f} ms",
    )
    return visible_tiles


def validate_texture_patches(
    records: list[EventRecord],
    min_count: int,
    p95_limit_ms: float,
) -> list[EventRecord]:
    uploads = [
        record
        for record in records_for(records, "preview.texture.uploaded")
        if record.fields.get("render_mode") == "max_preview_visible_tile"
        and record.fields.get("dirty_domain") == "publish_texture"
        and truthy(record.fields.get("partial_upload"))
        and truthy(record.fields.get("patch_upload"))
        and (integer(record.fields.get("upload_pixels")) or 0) > 0
        and (integer(record.fields.get("full_pixels")) or 0) > (integer(record.fields.get("upload_pixels")) or 0)
        and (integer(record.fields.get("upload_width")) or 0) > 0
        and (integer(record.fields.get("upload_height")) or 0) > 0
    ]
    require(
        len(uploads) >= min_count,
        f"Missing native visible-tile patch uploads (needed {min_count}, found {len(uploads)})",
    )
    elapsed = [number(record.fields.get("elapsed_ms")) for record in uploads]
    require(all(value is not None for value in elapsed), "Texture patch uploads are missing elapsed_ms")
    p95 = percentile([value for value in elapsed if value is not None], 0.95)
    require(
        p95 <= p95_limit_ms,
        f"Texture patch upload p95 {p95:.1f} ms exceeds {p95_limit_ms:.1f} ms",
    )
    return uploads


def validate_preserved_low_res_uploads(records: list[EventRecord], min_count: int) -> list[EventRecord]:
    preserved = []
    for record in records_for(records, "preview.texture.upload_preserved"):
        native_width = integer(record.fields.get("native_width")) or 0
        native_height = integer(record.fields.get("native_height")) or 0
        result_width = integer(record.fields.get("result_width")) or 0
        result_height = integer(record.fields.get("result_height")) or 0
        if (
            record.fields.get("render_reason") == "edit_change"
            and truthy(record.fields.get("viewport_active"))
            and native_width > result_width > 0
            and native_height > result_height > 0
        ):
            preserved.append(record)
    require(
        len(preserved) >= min_count,
        (
            "Missing preserved low-resolution edit uploads while viewport is active "
            f"(needed {min_count}, found {len(preserved)})"
        ),
    )
    return preserved


def validate_cancellations(
    records: list[EventRecord],
    min_count: int,
    p95_limit_ms: float,
) -> list[EventRecord]:
    cancellations = []
    for record in records_for(records, "preview.render.cancelled"):
        generation = integer(record.fields.get("generation"))
        latest_generation = integer(record.fields.get("latest_generation"))
        if (
            record.fields.get("stage") == "full"
            and record.fields.get("render_mode") == "max_preview"
            and record.fields.get("render_reason") == "edit_change"
            and generation is not None
            and latest_generation is not None
            and latest_generation > generation
            and (integer(record.fields.get("encode_ms")) or 0) == 0
        ):
            cancellations.append(record)
    require(
        len(cancellations) >= min_count,
        f"Missing stale full max-preview cancellations (needed {min_count}, found {len(cancellations)})",
    )
    elapsed = [number(record.fields.get("elapsed_ms")) for record in cancellations]
    require(all(value is not None for value in elapsed), "Cancellation records are missing elapsed_ms")
    p95 = percentile([value for value in elapsed if value is not None], 0.95)
    require(
        p95 <= p95_limit_ms,
        f"Cancellation p95 {p95:.1f} ms exceeds {p95_limit_ms:.1f} ms",
    )
    return cancellations


def validate_no_preview_readback(render_records: list[EventRecord]) -> None:
    bad_records = [
        record
        for record in render_records
        if record.fields.get("render_mode")
        in {"live_preview", "settled_preview", "max_preview", "max_preview_visible_tile"}
        and (integer(record.fields.get("gpu_readback_count")) or 0) != 0
    ]
    require(
        not bad_records,
        f"Preview render records contain GPU readbacks: {len(bad_records)}",
    )


def validate_metal_edit_work(render_records: list[EventRecord], allow_non_metal: bool) -> int:
    metal_records = [
        record
        for record in render_records
        if record.fields.get("render_reason") == "edit_change"
        and record.fields.get("backend") == "metal"
        and (integer(record.fields.get("adjustment_gpu_command_buffer_count")) or 0) > 0
        and (integer(record.fields.get("adjustment_gpu_kernel_dispatch_count")) or 0) > 0
        and (integer(record.fields.get("adjustment_gpu_cpu_to_gpu_bytes")) or 0) > 0
        and (integer(record.fields.get("gpu_readback_count")) or 0) == 0
    ]
    if allow_non_metal:
        return len(metal_records)

    unavailable = [
        record
        for record in render_records
        if str(record.fields.get("fallback_reason", "")).startswith("metal_unavailable")
    ]
    require(
        not unavailable,
        (
            "Desktop proof contains metal_unavailable fallback records. Re-run on a "
            "Metal-capable Mac, or pass --allow-non-metal for local smoke only."
        ),
    )
    require(
        metal_records,
        "No edit render proves Metal backend work with positive GPU counters.",
    )
    return len(metal_records)


def validate_visible_before_background(
    visible_tiles: list[EventRecord],
    render_records: list[EventRecord],
) -> None:
    timestamped_visible = [record for record in visible_tiles if record.timestamp is not None]
    timestamped_background = [
        record
        for record in render_records
        if record.timestamp is not None
        and record.fields.get("stage") == "full"
        and record.fields.get("render_mode") == "max_preview"
        and truthy(record.fields.get("tile_visible_region_active"))
        and (integer(record.fields.get("planned_background_tile_count")) or 0) > 0
    ]
    if not timestamped_visible or not timestamped_background:
        return
    first_visible = min(record.timestamp for record in timestamped_visible if record.timestamp is not None)
    first_background = min(
        record.timestamp for record in timestamped_background if record.timestamp is not None
    )
    require(
        first_visible <= first_background,
        (
            "First visible max-preview tile patch was logged after the first matching "
            "background max-preview completion."
        ),
    )


def validate_single_evidence_log(groups: list[list[EventRecord]]) -> Path:
    sources = {
        record.source
        for group in groups
        for record in group
    }
    require(
        len(sources) == 1,
        (
            "Desktop proof release mode requires all accepted evidence records to "
            f"come from one JSONL log; observed {len(sources)} logs."
        ),
    )
    return next(iter(sources))


def main() -> int:
    args = parse_args()
    require(args.min_source_edge > 0, "--min-source-edge must be positive")

    jsonl_paths = jsonl_files_for_input(args.input)
    records = load_records(jsonl_paths)
    require(records, f"No event records loaded from {args.input}")

    startup_records = records_for(records, "desktop.startup.ready")
    startup_count = len(startup_records)
    require(startup_count > 0, "Missing desktop.startup.ready event")

    render_records = records_for(records, "preview.render.completed")
    require(render_records, "Missing preview.render.completed records")
    source_width, source_height = validate_large_real_source(
        render_records,
        args.min_source_edge,
        args.allow_synthetic,
        args.expected_source_image,
    )
    schedules = validate_viewport_schedule(records, args.min_viewport_schedules)
    visible_tiles = validate_visible_tile_renders(
        render_records,
        args.min_visible_tile_patches,
        args.max_visible_tile_p95_ms,
    )
    uploads = validate_texture_patches(
        records,
        args.min_visible_tile_patches,
        args.max_texture_upload_p95_ms,
    )
    preserved = validate_preserved_low_res_uploads(records, args.min_preserved_uploads)
    cancellations = validate_cancellations(
        records,
        args.min_cancellations,
        args.max_cancel_p95_ms,
    )
    validate_no_preview_readback(render_records)
    metal_edit_records = validate_metal_edit_work(render_records, args.allow_non_metal)
    validate_visible_before_background(visible_tiles, render_records)
    proof_log = None
    if not args.allow_synthetic and not args.allow_non_metal:
        proof_log = validate_single_evidence_log(
            [
                startup_records,
                render_records,
                schedules,
                visible_tiles,
                uploads,
                preserved,
                cancellations,
            ]
        )

    visible_elapsed = [
        value
        for value in (number(record.fields.get("elapsed_ms")) for record in visible_tiles)
        if value is not None
    ]
    upload_elapsed = [
        value
        for value in (number(record.fields.get("elapsed_ms")) for record in uploads)
        if value is not None
    ]
    cancel_elapsed = [
        value
        for value in (number(record.fields.get("elapsed_ms")) for record in cancellations)
        if value is not None
    ]
    release_eligible = not args.allow_synthetic and not args.allow_non_metal
    print(f"desktop_preview_proof_logs={','.join(str(path) for path in jsonl_paths)}")
    if proof_log is not None:
        print(f"desktop_preview_proof_evidence_log={proof_log}")
    if args.expected_source_image is not None:
        print(
            "desktop_preview_proof_expected_source_image="
            f"{args.expected_source_image.expanduser().resolve()}"
        )
    print(f"desktop_preview_proof_source_max={source_width}x{source_height}")
    print(f"desktop_preview_proof_viewport_schedules={len(schedules)}")
    print(
        "desktop_preview_proof_visible_tile_p95_ms="
        f"{percentile(visible_elapsed, 0.95):.1f}"
    )
    print(
        "desktop_preview_proof_texture_patch_p95_ms="
        f"{percentile(upload_elapsed, 0.95):.1f}"
    )
    print(f"desktop_preview_proof_preserved_uploads={len(preserved)}")
    print(
        "desktop_preview_proof_cancel_p95_ms="
        f"{percentile(cancel_elapsed, 0.95):.1f}"
    )
    print(f"desktop_preview_proof_metal_edit_records={metal_edit_records}")
    print(f"desktop_preview_proof_release_eligible={'true' if release_eligible else 'false'}")
    print(
        "desktop_preview_proof_gate="
        f"{'passed' if release_eligible else 'smoke_passed'}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
