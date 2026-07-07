#!/usr/bin/env python3
"""Run or validate accepted preview pipeline regression gates."""

from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_APP = REPO_ROOT / "build/dev-macos-debug/tests/preview_benchmark_driver"
DEFAULT_CORPUS = REPO_ROOT / "perf-corpora/preview-synthetic-v1/manifest.json"
DEFAULT_OUTPUT_ROOT = REPO_ROOT / "perf-artifacts"


@dataclass(frozen=True)
class PipelineCase:
    name: str
    driver_flag: str
    event: str
    elapsed_max_ms: int
    render_filters: tuple[str, ...]
    render_elapsed_max_ms: int
    driver_extra_flags: tuple[str, ...] = ()
    event_field: str = "elapsed_ms"
    event_metric_max: tuple[str, ...] = ()
    event_metric_min: tuple[str, ...] = ()
    event_metric_eq: tuple[str, ...] = ()
    render_metric_max: tuple[str, ...] = ()
    render_metric_min: tuple[str, ...] = ()
    render_metric_eq: tuple[str, ...] = ()
    cancel_filters: tuple[str, ...] = ()
    cancel_elapsed_max_ms: int | None = None
    cancel_metric_max: tuple[str, ...] = ()
    cancel_metric_min: tuple[str, ...] = ()
    cancel_metric_eq: tuple[str, ...] = ()
    cancel_count: int = 1
    forbidden_render_filters: tuple[str, ...] = ()
    settled_latency_filters: tuple[str, ...] = ()
    settled_elapsed_max_ms: int | None = None
    settled_metric_max: tuple[str, ...] = ()
    settled_metric_min: tuple[str, ...] = ()
    settled_metric_eq: tuple[str, ...] = ()
    settled_count: int = 1
    texture_upload_filters: tuple[str, ...] = ()
    texture_upload_elapsed_max_ms: int | None = None
    texture_upload_metric_max: tuple[str, ...] = ()
    texture_upload_metric_min: tuple[str, ...] = ()
    texture_upload_metric_eq: tuple[str, ...] = ()
    texture_preserve_filters: tuple[str, ...] = ()
    texture_preserve_count_min: int = 1
    texture_preserve_metric_max: tuple[str, ...] = ()
    texture_preserve_metric_min: tuple[str, ...] = ()
    texture_preserve_metric_eq: tuple[str, ...] = ()
    iterations: int = 1
    sample_event: str | None = None
    sample_field: str = "elapsed_ms"
    sample_elapsed_p95_max_ms: int | None = None
    sample_metric_max: tuple[str, ...] = ()
    sample_metric_min: tuple[str, ...] = ()
    sample_metric_eq: tuple[str, ...] = ()
    render_count: int = 1
    texture_upload_count: int = 1
    timeout_min_seconds: int | None = None


PIPELINE_CASES = (
    PipelineCase(
        name="global-burst",
        driver_flag="--global-burst",
        event="benchmark.preview_global_burst.completed",
        elapsed_max_ms=950,
        event_metric_max=(
            "iterations=30",
            "p95_ms=1100",
            "p99_ms=1100",
            "max_ms=1100",
        ),
        event_metric_min=("iterations=30",),
        render_filters=("render_mode=max_preview", "render_reason=edit_change"),
        render_elapsed_max_ms=50,
        render_metric_max=("decode_ms=0", "edit_ms=0", "encode_ms=0"),
        render_metric_eq=(
            "backend=rendered_cache",
            "jpeg_encode_requested=false",
            "publish_path=in_memory_image_provider",
        ),
        iterations=30,
        sample_event="benchmark.preview_global_burst.sample_completed",
        sample_elapsed_p95_max_ms=950,
        sample_metric_max=("iterations=30",),
        sample_metric_min=("iterations=30",),
        render_count=30,
    ),
    PipelineCase(
        name="output-profile-toggle",
        driver_flag="--output-profile-toggle",
        event="benchmark.preview_output_profile_toggle.completed",
        elapsed_max_ms=800,
        event_metric_max=(
            "iterations=30",
            "p95_ms=800",
            "p99_ms=800",
            "max_ms=800",
        ),
        event_metric_min=("iterations=30",),
        render_filters=("render_mode=max_preview", "dirty_domain=display_transform"),
        render_elapsed_max_ms=50,
        render_metric_max=("decode_ms=0", "edit_ms=0", "encode_ms=0"),
        render_metric_eq=(
            "backend=rendered_cache",
            "jpeg_encode_requested=false",
            "publish_path=in_memory_image_provider",
        ),
        iterations=30,
        sample_event="benchmark.preview_output_profile_toggle.sample_completed",
        sample_elapsed_p95_max_ms=800,
        sample_metric_max=("iterations=30",),
        sample_metric_min=("iterations=30",),
        render_count=30,
    ),
    PipelineCase(
        name="repeat-full-cache",
        driver_flag="--repeat-full-cache",
        event="benchmark.preview_full_cache_repeat.completed",
        elapsed_max_ms=150,
        event_metric_max=(
            "iterations=30",
            "p95_ms=150",
            "p99_ms=150",
            "max_ms=150",
        ),
        event_metric_min=("iterations=30",),
        render_filters=("render_mode=max_preview", "render_reason=preview_retry"),
        render_elapsed_max_ms=50,
        render_metric_max=("decode_ms=0", "edit_ms=0", "encode_ms=0"),
        render_metric_eq=("disk_cache_hit=true", "publish_path=disk_jpeg_cache"),
        iterations=30,
        sample_event="benchmark.preview_full_cache_repeat.sample_completed",
        sample_elapsed_p95_max_ms=150,
        sample_metric_max=("iterations=30",),
        sample_metric_min=("iterations=30",),
        render_count=30,
    ),
    PipelineCase(
        name="rapid-supersede",
        driver_flag="--rapid-supersede",
        event="benchmark.preview_rapid_supersede.completed",
        elapsed_max_ms=30000,
        event_metric_max=(
            "iterations=30",
            "p95_ms=30000",
            "p99_ms=30000",
            "max_ms=30000",
            "missed_count=0",
        ),
        event_metric_min=(
            "iterations=30",
            "cancelled_delta=30",
            "full_cancelled_delta=30",
        ),
        render_filters=(
            "render_mode=max_preview",
            "render_reason=edit_change",
            "dirty_domain=global_tone_color",
        ),
        render_elapsed_max_ms=50,
        render_metric_max=("decode_ms=0", "edit_ms=0", "encode_ms=0"),
        render_metric_eq=(
            "backend=rendered_cache",
            "jpeg_encode_requested=false",
            "publish_path=in_memory_image_provider",
        ),
        cancel_filters=(
            "stage=full",
            "render_mode=max_preview",
            "render_reason=edit_change",
        ),
        cancel_elapsed_max_ms=50,
        cancel_metric_min=("cancel_count=1",),
        cancel_metric_max=("decode_ms=0", "edit_ms=0", "encode_ms=0"),
        cancel_count=30,
        forbidden_render_filters=(
            "stage=interactive",
            "render_mode=settled_preview",
            "render_reason=edit_change",
            "generation=6",
        ),
        settled_latency_filters=(
            "stage=interactive",
            "render_mode=settled_preview",
            "render_reason=edit_change",
            "generation=7",
            "max_edge=4096",
        ),
        settled_elapsed_max_ms=4200,
        settled_metric_max=(
            "edit_ms=4100",
            "adjustment_pipeline_ms=3200",
            "adjustment_spatial_ms=1500",
            "adjustment_output_ms=250",
            "output_stage_ms=250",
            "output_linear_pack_ms=150",
            "output_color_convert_ms=150",
            "output_stitch_ms=100",
            "output_tile_cache_ms=100",
            "decode_ms=25",
            "encode_ms=0",
        ),
        settled_metric_min=(
            "output_linear_pack_ms=1",
            "output_color_convert_ms=1",
        ),
        settled_metric_eq=(
            "backend=cpu",
            "publish_path=in_memory_image_provider",
        ),
        settled_count=30,
        iterations=30,
        sample_event="benchmark.preview_rapid_supersede.sample_completed",
        sample_elapsed_p95_max_ms=30000,
        sample_metric_min=(
            "iterations=30",
            "cancelled_delta=1",
            "full_cancelled_delta=1",
        ),
        sample_metric_max=("iterations=30",),
        render_count=30,
        timeout_min_seconds=420,
    ),
    PipelineCase(
        name="viewport-max-tile",
        driver_flag="--viewport-max-tile",
        event="benchmark.preview_viewport_max_tile.completed",
        elapsed_max_ms=220,
        event_metric_max=(
            "viewport_schedule_delta=1",
            "visible_prepass_delta=1",
            "patch_upload_delta=1",
            "viewport_schedule_delta_total=30",
            "visible_prepass_delta_total=30",
            "patch_upload_delta_total=30",
            "p95_ms=260",
            "p99_ms=500",
            "max_ms=500",
            "iterations=30",
            "patch_x=1536",
            "patch_y=1536",
            "patch_width=512",
            "patch_height=512",
        ),
        event_metric_min=(
            "viewport_schedule_delta=1",
            "visible_prepass_delta=1",
            "patch_upload_delta=1",
            "patch_x=1536",
            "patch_y=1536",
            "patch_width=512",
            "patch_height=512",
            "iterations=30",
        ),
        render_filters=(
            "render_mode=max_preview_visible_tile",
            "render_reason=viewport_change",
            "dirty_domain=publish_texture",
        ),
        render_elapsed_max_ms=100,
        render_metric_max=(
            "decode_ms=0",
            "encode_ms=0",
            "output_stage_ms=40",
            "output_linear_pack_ms=20",
            "output_color_convert_ms=20",
        ),
        render_metric_eq=(
            "backend=adjusted_cache",
            "executed_tile_count=1",
            "executed_visible_tile_count=1",
            "executed_background_tile_count=0",
            "executed_tile_pixels=262144",
            "texture_patch_only=true",
            "texture_upload_image=true",
            "texture_upload_pixels=262144",
            "partial_texture_upload_candidate=true",
            "first_executed_tile_priority=visible",
            "publish_path=in_memory_image_provider",
        ),
        texture_upload_filters=(
            "render_mode=max_preview_visible_tile",
            "render_reason=viewport_change",
            "dirty_domain=publish_texture",
        ),
        texture_upload_elapsed_max_ms=10,
        texture_upload_metric_eq=(
            "patch_upload=true",
            "partial_upload=true",
            "partial_upload_candidate=true",
            "upload_x=1536",
            "upload_y=1536",
            "upload_width=512",
            "upload_height=512",
            "upload_pixels=262144",
        ),
        iterations=30,
        sample_event="benchmark.preview_viewport_max_tile.sample_completed",
        sample_elapsed_p95_max_ms=260,
        sample_metric_max=(
            "viewport_schedule_delta=1",
            "visible_prepass_delta=1",
            "patch_upload_delta=1",
            "patch_x=1536",
            "patch_y=1536",
            "patch_width=512",
            "patch_height=512",
            "iterations=30",
        ),
        sample_metric_min=(
            "viewport_schedule_delta=1",
            "visible_prepass_delta=1",
            "patch_upload_delta=1",
            "patch_x=1536",
            "patch_y=1536",
            "patch_width=512",
            "patch_height=512",
            "iterations=30",
        ),
        render_count=31,
        texture_upload_count=31,
    ),
    PipelineCase(
        name="viewport-pan-heavy-edit",
        driver_flag="--viewport-pan-heavy-edit",
        event="benchmark.preview_viewport_pan_heavy_edit.completed",
        elapsed_max_ms=30000,
        event_metric_max=(
            "viewport_schedule_delta=1",
            "visible_prepass_delta=1",
            "patch_upload_delta=1",
            "viewport_schedule_delta_total=30",
            "visible_prepass_delta_total=30",
            "patch_upload_delta_total=30",
            "missed_count=0",
            "p95_ms=30000",
            "p99_ms=30000",
            "max_ms=30000",
            "iterations=30",
            "patch_x=1536",
            "patch_y=1536",
            "patch_width=512",
            "patch_height=512",
        ),
        event_metric_min=(
            "viewport_schedule_delta=1",
            "visible_prepass_delta=1",
            "patch_upload_delta=1",
            "viewport_schedule_delta_total=30",
            "visible_prepass_delta_total=30",
            "patch_upload_delta_total=30",
            "cancelled_delta=30",
            "full_cancelled_delta=30",
            "iterations=30",
            "patch_x=1536",
            "patch_y=1536",
            "patch_width=512",
            "patch_height=512",
        ),
        render_filters=(
            "render_mode=max_preview_visible_tile",
            "render_reason=viewport_change",
            "dirty_domain=publish_texture",
        ),
        render_elapsed_max_ms=150,
        render_metric_max=(
            "decode_ms=0",
            "encode_ms=0",
            "output_stage_ms=40",
            "output_linear_pack_ms=20",
            "output_color_convert_ms=20",
        ),
        render_metric_eq=(
            "backend=adjusted_cache",
            "executed_tile_count=1",
            "executed_visible_tile_count=1",
            "executed_background_tile_count=0",
            "executed_tile_pixels=262144",
            "texture_patch_only=true",
            "texture_upload_image=true",
            "texture_upload_pixels=262144",
            "partial_texture_upload_candidate=true",
            "first_executed_tile_priority=visible",
            "publish_path=in_memory_image_provider",
        ),
        cancel_filters=(
            "stage=full",
            "render_mode=max_preview",
            "render_reason=edit_change",
        ),
        cancel_elapsed_max_ms=50,
        cancel_metric_min=("cancel_count=1",),
        cancel_metric_max=("decode_ms=0", "edit_ms=0", "encode_ms=0"),
        cancel_count=30,
        texture_upload_filters=(
            "render_mode=max_preview_visible_tile",
            "render_reason=viewport_change",
            "dirty_domain=publish_texture",
        ),
        texture_upload_elapsed_max_ms=10,
        texture_upload_metric_eq=(
            "patch_upload=true",
            "partial_upload=true",
            "partial_upload_candidate=true",
            "upload_x=1536",
            "upload_y=1536",
            "upload_width=512",
            "upload_height=512",
            "upload_pixels=262144",
        ),
        texture_preserve_filters=(
            "render_mode=settled_preview",
            "render_reason=edit_change",
        ),
        texture_preserve_count_min=1,
        texture_preserve_metric_eq=(
            "native_width=2048",
            "native_height=2048",
            "result_width=960",
            "result_height=960",
            "preserve_reason=native_texture_higher_resolution",
        ),
        iterations=30,
        sample_event="benchmark.preview_viewport_pan_heavy_edit.sample_completed",
        sample_elapsed_p95_max_ms=30000,
        sample_metric_max=(
            "viewport_schedule_delta=1",
            "visible_prepass_delta=1",
            "patch_upload_delta=1",
            "patch_x=1536",
            "patch_y=1536",
            "patch_width=512",
            "patch_height=512",
            "iterations=30",
        ),
        sample_metric_min=(
            "viewport_schedule_delta=1",
            "visible_prepass_delta=1",
            "patch_upload_delta=1",
            "cancelled_delta=1",
            "full_cancelled_delta=1",
            "patch_x=1536",
            "patch_y=1536",
            "patch_width=512",
            "patch_height=512",
            "iterations=30",
        ),
        render_count=30,
        texture_upload_count=30,
    ),
    PipelineCase(
        name="brush-tile-cache",
        driver_flag="--brush-tile-cache",
        driver_extra_flags=("--brush-texture-patch-only",),
        event="benchmark.preview_brush_tile_cache.completed",
        event_field="brush_elapsed_ms",
        elapsed_max_ms=100,
        event_metric_max=(
            "iterations=30",
            "brush_p95_ms=100",
            "brush_p99_ms=150",
            "brush_max_ms=150",
            "brush_max_decode_ms=0",
            "adjusted_stage_p95_ms=50",
            "output_stage_p95_ms=20",
            "parity_max_channel_delta=4",
            "adjusted_stage_tiles=1",
            "adjusted_stage_pixels=262144",
            "output_tiles=1",
            "output_pixels=262144",
            "texture_upload_pixels=262144",
            "texture_patch_base_copy_pixels=0",
            "reference_output_pixels=4194304",
        ),
        event_metric_min=(
            "iterations=30",
            "speedup_ratio=10",
            "texture_upload_pixels=1",
        ),
        event_metric_eq=(
            "brush_texture_patch_only=true",
            "partial_texture_upload_candidate=true",
            "texture_upload_image=true",
        ),
        render_filters=(
            "backend=adjusted_tile_cache",
            "dirty_domain=mask_rasterization",
            "texture_patch_only=true",
        ),
        render_elapsed_max_ms=100,
        render_metric_max=(
            "decode_ms=0",
            "encode_ms=0",
            "adjusted_stage_ms=50",
            "output_stage_ms=20",
            "adjusted_stage_tile_count=1",
            "adjusted_stage_pixels=262144",
            "executed_tile_count=1",
            "executed_tile_pixels=262144",
            "texture_upload_pixels=262144",
            "texture_patch_base_copy_pixels=0",
        ),
        render_metric_min=("texture_upload_pixels=1",),
        render_metric_eq=(
            "partial_texture_upload_candidate=true",
            "texture_upload_image=true",
            "publish_path=in_memory_image_provider",
        ),
        iterations=30,
        sample_event="benchmark.preview_brush_tile_cache.sample_completed",
        sample_field="brush_elapsed_ms",
        sample_elapsed_p95_max_ms=100,
        sample_metric_max=(
            "iterations=30",
            "brush_decode_ms=0",
            "adjusted_stage_ms=50",
            "output_stage_ms=20",
            "parity_max_channel_delta=4",
            "texture_upload_pixels=262144",
            "texture_patch_base_copy_pixels=0",
        ),
        sample_metric_min=(
            "iterations=30",
            "texture_upload_pixels=1",
        ),
        sample_metric_eq=(
            "brush_texture_patch_only=true",
            "partial_texture_upload_candidate=true",
            "texture_upload_image=true",
        ),
        render_count=30,
    ),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run or validate accepted preview pipeline performance gates."
    )
    parser.add_argument("--app", type=Path, default=DEFAULT_APP, help="preview_benchmark_driver path.")
    parser.add_argument("--corpus", type=Path, default=DEFAULT_CORPUS, help="Preview corpus manifest.")
    parser.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--timeout", type=int, default=120, help="Outer benchmark timeout seconds.")
    parser.add_argument("--timeout-ms", type=int, default=30000, help="Driver per-render timeout.")
    parser.add_argument("--global-burst-artifact", type=Path, help="Validate an existing artifact.")
    parser.add_argument("--output-profile-toggle-artifact", type=Path, help="Validate an existing artifact.")
    parser.add_argument("--repeat-full-cache-artifact", type=Path, help="Validate an existing artifact.")
    parser.add_argument("--rapid-supersede-artifact", type=Path, help="Validate an existing artifact.")
    parser.add_argument("--viewport-max-tile-artifact", type=Path, help="Validate an existing artifact.")
    parser.add_argument(
        "--viewport-pan-heavy-edit-artifact",
        type=Path,
        help="Validate an existing artifact.",
    )
    parser.add_argument("--brush-tile-cache-artifact", type=Path, help="Validate an existing artifact.")
    parser.add_argument(
        "--case",
        choices=tuple(pipeline_case.name for pipeline_case in PIPELINE_CASES),
        help="Run or validate a single pipeline case.",
    )
    return parser.parse_args()


def run_command(command: list[str]) -> subprocess.CompletedProcess[str]:
    print("+ " + " ".join(command), flush=True)
    result = subprocess.run(
        command,
        cwd=REPO_ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if result.stdout:
        print(result.stdout, end="" if result.stdout.endswith("\n") else "\n")
    return result


def parse_artifact_dir(output: str) -> Path:
    for line in output.splitlines():
        key, separator, value = line.partition("=")
        if separator and key == "artifact_dir" and value.strip():
            return Path(value.strip()).resolve()
    raise SystemExit("Benchmark completed without printing artifact_dir")


def run_pipeline_case(args: argparse.Namespace, pipeline_case: PipelineCase) -> Path:
    timeout_seconds = max(args.timeout, pipeline_case.timeout_min_seconds or args.timeout)
    command = [
        sys.executable,
        "-B",
        "scripts/perf/run-benchmark.py",
        "--scenario",
        "perf-preview-raster",
        "--app",
        str(args.app),
        "--output-root",
        str(args.output_root),
        "--corpus",
        str(args.corpus),
        "--event",
        pipeline_case.event,
        "--timeout",
        str(timeout_seconds),
        "--settle-seconds",
        "0",
        "--",
        "--corpus",
        str(args.corpus),
        "--limit",
        "1",
        pipeline_case.driver_flag,
        *pipeline_case.driver_extra_flags,
        "--iterations",
        str(pipeline_case.iterations),
        "--timeout-ms",
        str(args.timeout_ms),
    ]
    result = run_command(command)
    if result.returncode != 0:
        raise SystemExit(result.returncode)
    return parse_artifact_dir(result.stdout)


def validate_pipeline_case(artifact: Path, pipeline_case: PipelineCase) -> None:
    benchmark_checker = [
        sys.executable,
        "-B",
        "scripts/perf/check-summary-thresholds.py",
        str(artifact),
        "--event",
        pipeline_case.event,
        "--field",
        pipeline_case.event_field,
        "--min-count",
        "1",
        "--max-count",
        "1",
        "--max",
        str(pipeline_case.elapsed_max_ms),
    ]
    for metric in pipeline_case.event_metric_max:
        benchmark_checker.extend(["--metric-max", metric])
    for metric in pipeline_case.event_metric_min:
        benchmark_checker.extend(["--metric-min", metric])
    for metric in pipeline_case.event_metric_eq:
        benchmark_checker.extend(["--metric-eq", metric])
    result = run_command(benchmark_checker)
    if result.returncode != 0:
        raise SystemExit(result.returncode)

    render_checker = [
        sys.executable,
        "-B",
        "scripts/perf/check-summary-thresholds.py",
        str(artifact),
        "--event",
        "preview.render.completed",
        "--field",
        "elapsed_ms",
        "--min-count",
        str(pipeline_case.render_count),
        "--max-count",
        str(pipeline_case.render_count),
        "--max",
        str(pipeline_case.render_elapsed_max_ms),
    ]
    for event_filter in pipeline_case.render_filters:
        render_checker.extend(["--where", event_filter])
    for metric in pipeline_case.render_metric_max:
        render_checker.extend(["--metric-max", metric])
    render_checker.extend(["--metric-max", "gpu_readback_count=0"])
    for metric in pipeline_case.render_metric_min:
        render_checker.extend(["--metric-min", metric])
    for metric in pipeline_case.render_metric_eq:
        render_checker.extend(["--metric-eq", metric])
    result = run_command(render_checker)
    if result.returncode != 0:
        raise SystemExit(result.returncode)

    if pipeline_case.cancel_filters:
        cancel_checker = [
            sys.executable,
            "-B",
            "scripts/perf/check-summary-thresholds.py",
            str(artifact),
            "--event",
            "preview.render.cancelled",
            "--field",
            "elapsed_ms",
            "--min-count",
            str(pipeline_case.cancel_count),
            "--max-count",
            str(pipeline_case.cancel_count),
        ]
        if pipeline_case.cancel_elapsed_max_ms is not None:
            cancel_checker.extend(["--max", str(pipeline_case.cancel_elapsed_max_ms)])
        for event_filter in pipeline_case.cancel_filters:
            cancel_checker.extend(["--where", event_filter])
        for metric in pipeline_case.cancel_metric_max:
            cancel_checker.extend(["--metric-max", metric])
        for metric in pipeline_case.cancel_metric_min:
            cancel_checker.extend(["--metric-min", metric])
        for metric in pipeline_case.cancel_metric_eq:
            cancel_checker.extend(["--metric-eq", metric])
        result = run_command(cancel_checker)
        if result.returncode != 0:
            raise SystemExit(result.returncode)

    if pipeline_case.forbidden_render_filters:
        forbidden_checker = [
            sys.executable,
            "-B",
            "scripts/perf/check-summary-thresholds.py",
            str(artifact),
            "--event",
            "preview.render.completed",
            "--field",
            "elapsed_ms",
            "--min-count",
            "0",
            "--max-count",
            "0",
        ]
        for event_filter in pipeline_case.forbidden_render_filters:
            forbidden_checker.extend(["--where", event_filter])
        result = run_command(forbidden_checker)
        if result.returncode != 0:
            raise SystemExit(result.returncode)

    if pipeline_case.settled_latency_filters:
        if pipeline_case.settled_elapsed_max_ms is None:
            raise SystemExit(f"{pipeline_case.name} settled latency filters require a max threshold")
        settled_checker = [
            sys.executable,
            "-B",
            "scripts/perf/check-summary-thresholds.py",
            str(artifact),
            "--event",
            "preview.render.completed",
            "--field",
            "elapsed_ms",
            "--min-count",
            str(pipeline_case.settled_count),
            "--max-count",
            str(pipeline_case.settled_count),
            "--max",
            str(pipeline_case.settled_elapsed_max_ms),
        ]
        for event_filter in pipeline_case.settled_latency_filters:
            settled_checker.extend(["--where", event_filter])
        for metric in pipeline_case.settled_metric_max:
            settled_checker.extend(["--metric-max", metric])
        settled_checker.extend(["--metric-max", "gpu_readback_count=0"])
        for metric in pipeline_case.settled_metric_min:
            settled_checker.extend(["--metric-min", metric])
        for metric in pipeline_case.settled_metric_eq:
            settled_checker.extend(["--metric-eq", metric])
        result = run_command(settled_checker)
        if result.returncode != 0:
            raise SystemExit(result.returncode)

    if pipeline_case.texture_upload_filters:
        texture_upload_checker = [
            sys.executable,
            "-B",
            "scripts/perf/check-summary-thresholds.py",
            str(artifact),
            "--event",
            "preview.texture.uploaded",
            "--field",
            "elapsed_ms",
            "--min-count",
            str(pipeline_case.texture_upload_count),
            "--max-count",
            str(pipeline_case.texture_upload_count),
        ]
        if pipeline_case.texture_upload_elapsed_max_ms is not None:
            texture_upload_checker.extend(
                ["--max", str(pipeline_case.texture_upload_elapsed_max_ms)]
            )
        for event_filter in pipeline_case.texture_upload_filters:
            texture_upload_checker.extend(["--where", event_filter])
        for metric in pipeline_case.texture_upload_metric_max:
            texture_upload_checker.extend(["--metric-max", metric])
        for metric in pipeline_case.texture_upload_metric_min:
            texture_upload_checker.extend(["--metric-min", metric])
        for metric in pipeline_case.texture_upload_metric_eq:
            texture_upload_checker.extend(["--metric-eq", metric])
        result = run_command(texture_upload_checker)
        if result.returncode != 0:
            raise SystemExit(result.returncode)

    if pipeline_case.texture_preserve_filters:
        texture_preserve_checker = [
            sys.executable,
            "-B",
            "scripts/perf/check-summary-thresholds.py",
            str(artifact),
            "--event",
            "preview.texture.upload_preserved",
            "--field",
            "result_width",
            "--min-count",
            str(pipeline_case.texture_preserve_count_min),
        ]
        for event_filter in pipeline_case.texture_preserve_filters:
            texture_preserve_checker.extend(["--where", event_filter])
        for metric in pipeline_case.texture_preserve_metric_max:
            texture_preserve_checker.extend(["--metric-max", metric])
        for metric in pipeline_case.texture_preserve_metric_min:
            texture_preserve_checker.extend(["--metric-min", metric])
        for metric in pipeline_case.texture_preserve_metric_eq:
            texture_preserve_checker.extend(["--metric-eq", metric])
        result = run_command(texture_preserve_checker)
        if result.returncode != 0:
            raise SystemExit(result.returncode)

    if pipeline_case.sample_event is not None:
        sample_checker = [
            sys.executable,
            "-B",
            "scripts/perf/check-summary-thresholds.py",
            str(artifact),
            "--event",
            pipeline_case.sample_event,
            "--field",
            pipeline_case.sample_field,
            "--min-count",
            str(pipeline_case.iterations),
            "--max-count",
            str(pipeline_case.iterations),
        ]
        if pipeline_case.sample_elapsed_p95_max_ms is not None:
            sample_checker.extend(["--p95-max", str(pipeline_case.sample_elapsed_p95_max_ms)])
        for metric in pipeline_case.sample_metric_max:
            sample_checker.extend(["--metric-max", metric])
        for metric in pipeline_case.sample_metric_min:
            sample_checker.extend(["--metric-min", metric])
        for metric in pipeline_case.sample_metric_eq:
            sample_checker.extend(["--metric-eq", metric])
        result = run_command(sample_checker)
        if result.returncode != 0:
            raise SystemExit(result.returncode)


def main() -> int:
    args = parse_args()
    supplied_artifacts = {
        "global-burst": args.global_burst_artifact,
        "output-profile-toggle": args.output_profile_toggle_artifact,
        "repeat-full-cache": args.repeat_full_cache_artifact,
        "rapid-supersede": args.rapid_supersede_artifact,
        "viewport-max-tile": args.viewport_max_tile_artifact,
        "viewport-pan-heavy-edit": args.viewport_pan_heavy_edit_artifact,
        "brush-tile-cache": args.brush_tile_cache_artifact,
    }
    artifacts: dict[str, Path] = {}
    selected_cases = (
        tuple(pipeline_case for pipeline_case in PIPELINE_CASES if pipeline_case.name == args.case)
        if args.case is not None
        else PIPELINE_CASES
    )
    for pipeline_case in selected_cases:
        supplied = supplied_artifacts[pipeline_case.name]
        artifacts[pipeline_case.name] = (
            supplied.expanduser().resolve()
            if supplied is not None
            else run_pipeline_case(args, pipeline_case)
        )
        validate_pipeline_case(artifacts[pipeline_case.name], pipeline_case)

    print("preview_pipeline_gate=passed")
    for name, artifact in artifacts.items():
        print(f"{name}_artifact={artifact}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
