#!/usr/bin/env python3
"""Run app-wide performance and operability suites.

This wrapper composes the existing benchmark/proof tools, keeps their artifacts intact, and writes
one app-level manifest/report that makes performance and operability regressions easier to triage.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import uuid
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT_ROOT = REPO_ROOT / "perf-artifacts"
DEFAULT_PREVIEW_CORPUS = REPO_ROOT / "perf-corpora/preview-synthetic-v1/manifest.json"
DEFAULT_PREVIEW_APP = REPO_ROOT / "build/dev-macos-debug/tests/preview_benchmark_driver"
DEFAULT_THUMBNAIL_APP = REPO_ROOT / "build/dev-macos-debug/tests/thumbnail_benchmark_driver"
DEFAULT_COLOR_APP = REPO_ROOT / "build/dev-macos-debug/tests/color_adjustment_benchmark_driver"
DEFAULT_INTERACTION_APP = REPO_ROOT / "build/dev-macos-debug/tests/interaction_workflow_benchmark_driver"
DEFAULT_ACTUAL_APP_WORKFLOW_APP = (
    REPO_ROOT / "build/dev-macos-debug/tests/actual_app_workflow_benchmark_driver"
)
DEFAULT_DESKTOP_APP = (
    REPO_ROOT / "build/dev-macos-debug/apps/desktop/PhotoEditor.app/Contents/MacOS/PhotoEditor"
)


CommandBuilder = Callable[[argparse.Namespace, Path], list[str]]


@dataclass(frozen=True)
class SuiteDefinition:
    name: str
    description: str
    builder: CommandBuilder
    required_for_release: bool = False
    requires_corpus: bool = False


@dataclass
class SuiteResult:
    name: str
    description: str
    command: list[str]
    returncode: int | None
    log_file: str | None
    artifact_dirs: list[str]
    status: str
    started_at: str | None = None
    ended_at: str | None = None


@dataclass(frozen=True)
class PerformanceThreshold:
    metric: str
    limit: float
    unit: str
    direction: str = "max"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run an app-wide PhotoEditor performance and operability suite."
    )
    parser.add_argument(
        "--profile",
        choices=sorted(PROFILES),
        default="smoke",
        help="Curated suite profile to run.",
    )
    parser.add_argument(
        "--suite",
        action="append",
        choices=sorted(SUITE_DEFINITIONS),
        help="Run one or more explicit suites instead of the selected profile.",
    )
    parser.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--corpus", type=Path, default=DEFAULT_PREVIEW_CORPUS)
    parser.add_argument("--desktop-app", type=Path, default=DEFAULT_DESKTOP_APP)
    parser.add_argument("--preview-app", type=Path, default=DEFAULT_PREVIEW_APP)
    parser.add_argument("--thumbnail-app", type=Path, default=DEFAULT_THUMBNAIL_APP)
    parser.add_argument("--color-app", type=Path, default=DEFAULT_COLOR_APP)
    parser.add_argument("--interaction-app", type=Path, default=DEFAULT_INTERACTION_APP)
    parser.add_argument("--actual-app-workflow-app", type=Path, default=DEFAULT_ACTUAL_APP_WORKFLOW_APP)
    parser.add_argument("--iterations", type=int, default=10)
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument(
        "--baseline",
        type=Path,
        help="Optional prior app-suite artifact to compare at the report level.",
    )
    parser.add_argument("--list-suites", action="store_true", help="List suites and exit.")
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Write manifest/report without executing commands.",
    )
    parser.add_argument(
        "--keep-going",
        action="store_true",
        help="Continue running later suites after a suite fails.",
    )
    return parser.parse_args()


def python_command(script: str, *args: str) -> list[str]:
    return [sys.executable, "-B", script, *args]


def build_startup_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return python_command(
        "scripts/perf/run-benchmark.py",
        "--scenario",
        "perf-startup",
        "--app",
        str(args.desktop_app),
        "--output-root",
        str(suite_output_root),
        "--event",
        "desktop.startup.ready",
        "--timeout",
        str(args.timeout),
        "--settle-seconds",
        "0",
        "--allow-gui-unavailable",
    )


def build_preview_raster_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return python_command(
        "scripts/perf/run-benchmark.py",
        "--scenario",
        "perf-preview-raster",
        "--app",
        str(args.preview_app),
        "--output-root",
        str(suite_output_root),
        "--corpus",
        str(args.corpus),
        "--event",
        "benchmark.preview_corpus.completed",
        "--timeout",
        str(args.timeout),
        "--settle-seconds",
        "0",
        "--",
        "--corpus",
        str(args.corpus),
        "--limit",
        "2",
        "--timeout-ms",
        str(max(args.timeout * 1000, 15000)),
    )


def build_thumbnail_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return python_command(
        "scripts/perf/run-benchmark.py",
        "--scenario",
        "perf-thumbnail-cold-warm",
        "--app",
        str(args.thumbnail_app),
        "--output-root",
        str(suite_output_root),
        "--corpus",
        str(args.corpus),
        "--event",
        "benchmark.thumbnail_corpus.completed",
        "--timeout",
        str(args.timeout),
        "--settle-seconds",
        "0",
        "--",
        "--corpus",
        str(args.corpus),
        "--limit",
        "8",
        "--window-size",
        "6",
        "--timeout-ms",
        str(max(args.timeout * 1000, 15000)),
    )


def build_preview_pipeline_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return python_command(
        "scripts/perf/run-preview-pipeline-gate.py",
        "--app",
        str(args.preview_app),
        "--corpus",
        str(args.corpus),
        "--output-root",
        str(suite_output_root),
        "--timeout",
        str(args.timeout),
    )


def build_color_adjustment_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return python_command(
        "scripts/perf/run-color-adjustment-gate.py",
        "--app",
        str(args.color_app),
        "--output-root",
        str(suite_output_root),
        "--iterations",
        str(args.iterations),
        "--timeout",
        str(args.timeout),
    )


def build_interaction_workflow_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return python_command(
        "scripts/perf/run-interaction-workflow-gate.py",
        "--app",
        str(args.interaction_app),
        "--output-root",
        str(suite_output_root),
        "--iterations",
        str(args.iterations),
        "--timeout",
        str(args.timeout),
    )


def build_actual_app_workflow_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return python_command(
        "scripts/perf/run-actual-app-workflow-gate.py",
        "--app",
        str(args.actual_app_workflow_app),
        "--output-root",
        str(suite_output_root),
        "--iterations",
        str(args.iterations),
        "--timeout",
        str(args.timeout),
    )


def build_preview_frontier_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return python_command(
        "scripts/perf/run-preview-frontier-gate.py",
        "--app",
        str(args.preview_app),
        "--corpus",
        str(args.corpus),
        "--output-root",
        str(suite_output_root),
        "--iterations",
        str(args.iterations),
        "--timeout",
        str(args.timeout),
    )


def build_mask_performance_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return python_command(
        "scripts/perf/run-mask-performance-proof.py",
        "--output-root",
        str(suite_output_root),
    )


def build_metal_proof_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return python_command(
        "scripts/perf/run-metal-proof-gate.py",
        "--color-app",
        str(args.color_app),
        "--output-root",
        str(suite_output_root),
        "--iterations",
        str(args.iterations),
        "--timeout",
        str(args.timeout),
        "--allow-unavailable",
    )


def build_preview_scenario_command(
    args: argparse.Namespace,
    suite_output_root: Path,
    *,
    scenario: str,
    event: str,
    extra_app_args: list[str] | None = None,
) -> list[str]:
    command = python_command(
        "scripts/perf/run-benchmark.py",
        "--scenario",
        scenario,
        "--app",
        str(args.preview_app),
        "--output-root",
        str(suite_output_root),
        "--corpus",
        str(args.corpus),
        "--event",
        event,
        "--timeout",
        str(args.timeout),
        "--settle-seconds",
        "0",
        "--",
        "--corpus",
        str(args.corpus),
        "--limit",
        "1",
        "--iterations",
        str(args.iterations),
        "--timeout-ms",
        str(max(args.timeout * 1000, 15000)),
    )
    command.extend(extra_app_args or [])
    return command


def build_color_scenario_command(
    args: argparse.Namespace,
    suite_output_root: Path,
    *,
    scenario: str,
    extra_app_args: list[str] | None = None,
) -> list[str]:
    command = python_command(
        "scripts/perf/run-benchmark.py",
        "--scenario",
        scenario,
        "--app",
        str(args.color_app),
        "--output-root",
        str(suite_output_root),
        "--event",
        "benchmark.color_adjustment_suite.completed",
        "--timeout",
        str(args.timeout),
        "--settle-seconds",
        "0",
        "--",
        "--iterations",
        str(args.iterations),
    )
    command.extend(extra_app_args or [])
    return command


def build_interaction_scenario_command(
    args: argparse.Namespace,
    suite_output_root: Path,
    *,
    scenario: str,
) -> list[str]:
    return python_command(
        "scripts/perf/run-benchmark.py",
        "--scenario",
        scenario,
        "--app",
        str(args.interaction_app),
        "--output-root",
        str(suite_output_root),
        "--event",
        "benchmark.interaction_workflow.completed",
        "--timeout",
        str(args.timeout),
        "--settle-seconds",
        "0",
        "--",
        "--iterations",
        str(args.iterations),
        "--timeout-ms",
        str(max(args.timeout * 1000, 15000)),
    )


def build_actual_app_scenario_command(
    args: argparse.Namespace,
    suite_output_root: Path,
    *,
    scenario: str,
) -> list[str]:
    return python_command(
        "scripts/perf/run-benchmark.py",
        "--scenario",
        scenario,
        "--app",
        str(args.actual_app_workflow_app),
        "--output-root",
        str(suite_output_root),
        "--event",
        "benchmark.actual_app_workflow.completed",
        "--timeout",
        str(args.timeout),
        "--settle-seconds",
        "0",
        "--",
        "--iterations",
        str(args.iterations),
        "--timeout-ms",
        str(max(args.timeout * 1000, 15000)),
    )


def build_perf_edit_global_sliders_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return build_preview_scenario_command(
        args,
        suite_output_root,
        scenario="perf-edit-global-sliders",
        event="benchmark.preview_global_burst.completed",
        extra_app_args=["--global-burst"],
    )


def build_perf_edit_color_tools_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return build_color_scenario_command(args, suite_output_root, scenario="perf-edit-color-tools")


def build_perf_edit_tone_curves_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return build_preview_scenario_command(
        args,
        suite_output_root,
        scenario="perf-edit-tone-curves",
        event="benchmark.preview_edit_sweep.completed",
        extra_app_args=["--edit-sweep"],
    )


def build_perf_layer_adjustments_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return build_interaction_scenario_command(args, suite_output_root, scenario="perf-layer-adjustments")


def build_perf_retouch_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return build_interaction_scenario_command(args, suite_output_root, scenario="perf-retouch")


def build_perf_crop_geometry_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return build_preview_scenario_command(
        args,
        suite_output_root,
        scenario="perf-crop-geometry",
        event="benchmark.preview_edit_sweep.completed",
        extra_app_args=["--edit-sweep"],
    )


def build_perf_mask_brush_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return build_interaction_scenario_command(args, suite_output_root, scenario="perf-mask-brush")


def build_perf_mask_lasso_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return build_actual_app_scenario_command(args, suite_output_root, scenario="perf-mask-lasso")


def build_perf_mask_smart_select_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return build_actual_app_scenario_command(args, suite_output_root, scenario="perf-mask-smart-select")


def build_perf_export_batch_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return build_actual_app_scenario_command(args, suite_output_root, scenario="perf-export-batch")


def build_perf_import_large_folder_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return build_actual_app_scenario_command(args, suite_output_root, scenario="perf-import-large-folder")


def build_perf_library_scroll_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return build_actual_app_scenario_command(args, suite_output_root, scenario="perf-library-scroll")


def build_perf_timeline_edit_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return build_actual_app_scenario_command(args, suite_output_root, scenario="perf-timeline-edit")


def build_perf_carousel_preview_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return build_actual_app_scenario_command(args, suite_output_root, scenario="perf-carousel-preview")


def build_perf_workspace_switch_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return build_actual_app_scenario_command(args, suite_output_root, scenario="perf-workspace-switch")


SUITE_DEFINITIONS: dict[str, SuiteDefinition] = {
    "startup": SuiteDefinition(
        "startup",
        "Desktop launch to usable app readiness.",
        build_startup_command,
    ),
    "preview-raster": SuiteDefinition(
        "preview-raster",
        "Corpus-backed raster preview latency and render-stage telemetry.",
        build_preview_raster_command,
        requires_corpus=True,
    ),
    "thumbnail-cold-warm": SuiteDefinition(
        "thumbnail-cold-warm",
        "Cold and warm visible-window thumbnail readiness.",
        build_thumbnail_command,
        requires_corpus=True,
    ),
    "preview-pipeline": SuiteDefinition(
        "preview-pipeline",
        "Accepted preview pipeline regression gates.",
        build_preview_pipeline_command,
        requires_corpus=True,
    ),
    "color-adjustment": SuiteDefinition(
        "color-adjustment",
        "CPU/GPU color-adjustment stage performance gates.",
        build_color_adjustment_command,
    ),
    "interaction-workflow": SuiteDefinition(
        "interaction-workflow",
        "Open photo, brush page, brush stroke, and tool apply interaction speed gate.",
        build_interaction_workflow_command,
    ),
    "actual-app-workflow": SuiteDefinition(
        "actual-app-workflow",
        "Actual QML shell open-photo, import, masks, brush, and tool readiness speed gate.",
        build_actual_app_workflow_command,
    ),
    "preview-frontier": SuiteDefinition(
        "preview-frontier",
        "Brush tile-cache and max-preview frontier gates.",
        build_preview_frontier_command,
        required_for_release=True,
        requires_corpus=True,
    ),
    "mask-performance": SuiteDefinition(
        "mask-performance",
        "Focused mask interaction performance proof.",
        build_mask_performance_command,
        required_for_release=True,
    ),
    "metal-proof": SuiteDefinition(
        "metal-proof",
        "Hardware-dependent Metal parity and GPU-work proof.",
        build_metal_proof_command,
        required_for_release=True,
    ),
    "perf-edit-global-sliders": SuiteDefinition(
        "perf-edit-global-sliders",
        "Global slider burst benchmark archived under a stable per-tool scenario.",
        build_perf_edit_global_sliders_command,
        requires_corpus=True,
    ),
    "perf-edit-color-tools": SuiteDefinition(
        "perf-edit-color-tools",
        "Color adjustment benchmark archived under a stable color-tool scenario.",
        build_perf_edit_color_tools_command,
    ),
    "perf-edit-tone-curves": SuiteDefinition(
        "perf-edit-tone-curves",
        "Tone/edit sweep benchmark archived under a stable tone-curve scenario.",
        build_perf_edit_tone_curves_command,
        requires_corpus=True,
    ),
    "perf-layer-adjustments": SuiteDefinition(
        "perf-layer-adjustments",
        "Layer adjustment interaction workflow benchmark.",
        build_perf_layer_adjustments_command,
    ),
    "perf-retouch": SuiteDefinition(
        "perf-retouch",
        "Retouch interaction workflow benchmark.",
        build_perf_retouch_command,
    ),
    "perf-crop-geometry": SuiteDefinition(
        "perf-crop-geometry",
        "Crop and geometry edit sweep benchmark.",
        build_perf_crop_geometry_command,
        requires_corpus=True,
    ),
    "perf-mask-brush": SuiteDefinition(
        "perf-mask-brush",
        "Brush mask interaction workflow benchmark.",
        build_perf_mask_brush_command,
    ),
    "perf-mask-lasso": SuiteDefinition(
        "perf-mask-lasso",
        "Mask lasso surface benchmark through the actual app workflow.",
        build_perf_mask_lasso_command,
    ),
    "perf-mask-smart-select": SuiteDefinition(
        "perf-mask-smart-select",
        "Smart selection surface benchmark through the actual app workflow.",
        build_perf_mask_smart_select_command,
    ),
    "perf-export-batch": SuiteDefinition(
        "perf-export-batch",
        "Export surface benchmark through the actual app workflow.",
        build_perf_export_batch_command,
    ),
    "perf-import-large-folder": SuiteDefinition(
        "perf-import-large-folder",
        "Import surface benchmark through the actual app workflow.",
        build_perf_import_large_folder_command,
    ),
    "perf-library-scroll": SuiteDefinition(
        "perf-library-scroll",
        "Library surface benchmark through the actual app workflow.",
        build_perf_library_scroll_command,
    ),
    "perf-timeline-edit": SuiteDefinition(
        "perf-timeline-edit",
        "Timeline surface benchmark through the actual app workflow.",
        build_perf_timeline_edit_command,
    ),
    "perf-carousel-preview": SuiteDefinition(
        "perf-carousel-preview",
        "Carousel surface benchmark through the actual app workflow.",
        build_perf_carousel_preview_command,
    ),
    "perf-workspace-switch": SuiteDefinition(
        "perf-workspace-switch",
        "Workspace switching benchmark through the actual app workflow.",
        build_perf_workspace_switch_command,
    ),
}


PROFILES: dict[str, tuple[str, ...]] = {
    "smoke": ("startup", "preview-raster", "thumbnail-cold-warm"),
    "local-dev": (
        "startup",
        "preview-raster",
        "thumbnail-cold-warm",
        "preview-pipeline",
        "color-adjustment",
        "interaction-workflow",
        "actual-app-workflow",
        "perf-edit-global-sliders",
        "perf-edit-color-tools",
        "perf-edit-tone-curves",
        "perf-mask-brush",
        "perf-mask-lasso",
        "perf-mask-smart-select",
        "perf-layer-adjustments",
        "perf-retouch",
        "perf-crop-geometry",
        "perf-export-batch",
        "perf-import-large-folder",
        "perf-library-scroll",
        "perf-timeline-edit",
        "perf-carousel-preview",
        "perf-workspace-switch",
    ),
    "release-proof": (
        "startup",
        "preview-raster",
        "thumbnail-cold-warm",
        "preview-pipeline",
        "color-adjustment",
        "interaction-workflow",
        "actual-app-workflow",
        "preview-frontier",
        "mask-performance",
        "metal-proof",
        "perf-edit-global-sliders",
        "perf-edit-color-tools",
        "perf-edit-tone-curves",
        "perf-mask-brush",
        "perf-mask-lasso",
        "perf-mask-smart-select",
        "perf-layer-adjustments",
        "perf-retouch",
        "perf-crop-geometry",
        "perf-export-batch",
        "perf-import-large-folder",
        "perf-library-scroll",
        "perf-timeline-edit",
        "perf-carousel-preview",
        "perf-workspace-switch",
    ),
    "stress": (
        "startup",
        "preview-raster",
        "thumbnail-cold-warm",
        "preview-pipeline",
        "color-adjustment",
        "interaction-workflow",
        "actual-app-workflow",
        "preview-frontier",
        "mask-performance",
        "metal-proof",
        "perf-edit-global-sliders",
        "perf-edit-color-tools",
        "perf-edit-tone-curves",
        "perf-mask-brush",
        "perf-mask-lasso",
        "perf-mask-smart-select",
        "perf-layer-adjustments",
        "perf-retouch",
        "perf-crop-geometry",
        "perf-export-batch",
        "perf-import-large-folder",
        "perf-library-scroll",
        "perf-timeline-edit",
        "perf-carousel-preview",
        "perf-workspace-switch",
    ),
}


PERFORMANCE_THRESHOLDS: dict[str, tuple[PerformanceThreshold, ...]] = {
    "smoke": (
        PerformanceThreshold("preview_render_p95_ms", 500, "ms"),
        PerformanceThreshold("mask_brush_feedback_p95_ms", 75, "ms"),
        PerformanceThreshold("thumbnail_queue_wait_p95_ms", 150, "ms"),
        PerformanceThreshold("workspace_route_ready_p95_ms", 600, "ms"),
        PerformanceThreshold("render_cancellation_latency_p95_ms", 100, "ms"),
        PerformanceThreshold("memory_growth_delta_kb", 262144, "KiB"),
        PerformanceThreshold("worker_queue_wait_p95_ms", 250, "ms"),
        PerformanceThreshold("qml_jank_frames_max", 20, "frames"),
        PerformanceThreshold("qml_dropped_frames_max", 10, "frames"),
        PerformanceThreshold("preview_cache_hit_rate_avg", 0, "ratio", "min"),
    ),
    "local-dev": (
        PerformanceThreshold("preview_render_p95_ms", 250, "ms"),
        PerformanceThreshold("mask_brush_feedback_p95_ms", 32, "ms"),
        PerformanceThreshold("thumbnail_queue_wait_p95_ms", 75, "ms"),
        PerformanceThreshold("workspace_route_ready_p95_ms", 350, "ms"),
        PerformanceThreshold("render_cancellation_latency_p95_ms", 60, "ms"),
        PerformanceThreshold("memory_growth_delta_kb", 131072, "KiB"),
        PerformanceThreshold("worker_queue_wait_p95_ms", 120, "ms"),
        PerformanceThreshold("qml_jank_frames_max", 10, "frames"),
        PerformanceThreshold("qml_dropped_frames_max", 4, "frames"),
        PerformanceThreshold("preview_cache_hit_rate_avg", 0, "ratio", "min"),
    ),
    "release-proof": (
        PerformanceThreshold("preview_render_p95_ms", 160, "ms"),
        PerformanceThreshold("mask_brush_feedback_p95_ms", 20, "ms"),
        PerformanceThreshold("thumbnail_queue_wait_p95_ms", 50, "ms"),
        PerformanceThreshold("workspace_route_ready_p95_ms", 220, "ms"),
        PerformanceThreshold("render_cancellation_latency_p95_ms", 40, "ms"),
        PerformanceThreshold("memory_growth_delta_kb", 65536, "KiB"),
        PerformanceThreshold("worker_queue_wait_p95_ms", 80, "ms"),
        PerformanceThreshold("qml_jank_frames_max", 5, "frames"),
        PerformanceThreshold("qml_dropped_frames_max", 2, "frames"),
        PerformanceThreshold("preview_cache_hit_rate_avg", 0, "ratio", "min"),
    ),
    "stress": (
        PerformanceThreshold("preview_render_p95_ms", 750, "ms"),
        PerformanceThreshold("mask_brush_feedback_p95_ms", 125, "ms"),
        PerformanceThreshold("thumbnail_queue_wait_p95_ms", 300, "ms"),
        PerformanceThreshold("workspace_route_ready_p95_ms", 1000, "ms"),
        PerformanceThreshold("render_cancellation_latency_p95_ms", 200, "ms"),
        PerformanceThreshold("memory_growth_delta_kb", 524288, "KiB"),
        PerformanceThreshold("worker_queue_wait_p95_ms", 500, "ms"),
        PerformanceThreshold("qml_jank_frames_max", 40, "frames"),
        PerformanceThreshold("qml_dropped_frames_max", 20, "frames"),
        PerformanceThreshold("preview_cache_hit_rate_avg", 0, "ratio", "min"),
    ),
}


def timestamp_slug() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def suite_names_for_args(args: argparse.Namespace) -> list[str]:
    names = args.suite if args.suite else list(PROFILES[args.profile])
    seen: set[str] = set()
    ordered: list[str] = []
    for name in names:
        if name not in seen:
            seen.add(name)
            ordered.append(name)
    return ordered


def print_suite_listing() -> None:
    for profile, suites in PROFILES.items():
        print(f"{profile}: {', '.join(suites)}")
    print()
    for suite in SUITE_DEFINITIONS.values():
        release = " release-required" if suite.required_for_release else ""
        print(f"{suite.name}:{release} {suite.description}")


def selected_suites_need_corpus(suite_names: list[str]) -> bool:
    return any(SUITE_DEFINITIONS[name].requires_corpus for name in suite_names)


def ensure_synthetic_corpus(corpus: Path) -> None:
    resolved = corpus.expanduser().resolve()
    if resolved.is_file():
        return
    if resolved.name != "manifest.json":
        raise SystemExit(f"Corpus manifest does not exist: {resolved}")
    output_dir = resolved.parent
    command = python_command(
        "scripts/perf/create-preview-corpus.py",
        "--output",
        str(output_dir),
        "--count",
        "8",
        "--force",
    )
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
    if result.returncode != 0:
        raise SystemExit(result.returncode)
    if not resolved.is_file():
        raise SystemExit(f"Corpus generation did not create {resolved}")


def parse_artifact_dirs(output: str) -> list[str]:
    artifacts: list[str] = []
    for line in output.splitlines():
        key, separator, value = line.partition("=")
        if separator and key.endswith("artifact") and value.strip():
            artifacts.append(str(Path(value.strip()).expanduser().resolve()))
        if separator and key == "artifact_dir" and value.strip():
            artifacts.append(str(Path(value.strip()).expanduser().resolve()))
    return sorted(set(artifacts))


def status_for_output(returncode: int, output: str) -> str:
    if returncode != 0:
        return "failed"
    if "benchmark_status=skipped" in output or "metal_proof_gate=skipped" in output:
        return "skipped"
    return "passed"


def run_command(command: list[str], log_file: Path) -> tuple[int, str]:
    with log_file.open("w", encoding="utf-8") as handle:
        handle.write("+ " + " ".join(command) + "\n")
        handle.flush()
        process = subprocess.run(
            command,
            cwd=REPO_ROOT,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        if process.stdout:
            handle.write(process.stdout)
    return process.returncode, process.stdout or ""


def load_json(path: Path) -> dict[str, Any] | None:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    return data if isinstance(data, dict) else None


def summary_for_artifact(artifact: str) -> dict[str, Any] | None:
    summary_path = Path(artifact) / "summary.json"
    return load_json(summary_path)


def manifest_for_artifact(artifact: str) -> dict[str, Any] | None:
    manifest_path = Path(artifact) / "manifest.json"
    return load_json(manifest_path)


def event_timing_rows(summary: dict[str, Any] | None) -> list[dict[str, Any]]:
    if not summary:
        return []
    rows: list[dict[str, Any]] = []
    events = summary.get("events", {})
    if not isinstance(events, dict):
        return rows
    for event_name, event_summary in sorted(events.items()):
        if not isinstance(event_summary, dict):
            continue
        timings = event_summary.get("timings", {})
        if not isinstance(timings, dict):
            continue
        primary = "elapsed_ms" if "elapsed_ms" in timings else next(iter(timings), None)
        if not primary:
            continue
        values = timings.get(primary, {})
        if not isinstance(values, dict):
            continue
        rows.append(
            {
                "event": event_name,
                "field": primary,
                "count": values.get("count"),
                "p50": values.get("p50"),
                "p95": values.get("p95"),
                "max": values.get("max"),
            }
        )
    return rows


def render_stage_rows_for_artifact(summary: dict[str, Any] | None) -> list[dict[str, Any]]:
    if not summary:
        return []
    groups = summary.get("groups", {})
    stage_groups = groups.get("stage", {}) if isinstance(groups, dict) else {}
    if isinstance(stage_groups, dict) and stage_groups:
        rows: list[dict[str, Any]] = []
        for stage_name, stage_summary in sorted(stage_groups.items()):
            if not isinstance(stage_summary, dict):
                continue
            events = stage_summary.get("events", {})
            if isinstance(events, dict) and "render.stage.completed" not in events:
                continue
            timings = stage_summary.get("timings", {})
            if not isinstance(timings, dict):
                continue
            elapsed = timings.get("elapsed_ms", {})
            if not isinstance(elapsed, dict):
                continue
            rows.append(
                {
                    "stage": stage_name,
                    "count": elapsed.get("count"),
                    "p50": elapsed.get("p50"),
                    "p95": elapsed.get("p95"),
                    "max": elapsed.get("max"),
                    "field": "elapsed_ms",
                    "event": "render.stage.completed",
                }
            )
        if rows:
            return rows

    events = summary.get("events", {})
    if not isinstance(events, dict):
        return []
    stage_summary = events.get("render.stage.completed")
    if not isinstance(stage_summary, dict):
        return []
    timings = stage_summary.get("timings", {})
    if not isinstance(timings, dict):
        return []
    elapsed = timings.get("elapsed_ms", {})
    if not isinstance(elapsed, dict):
        return []
    categories = stage_summary.get("categories", {})
    stages = categories.get("stage", {}) if isinstance(categories, dict) else {}
    rows = []
    if isinstance(stages, dict) and stages:
        for stage_name, count in sorted(stages.items()):
            rows.append(
                {
                    "stage": stage_name,
                    "count": count,
                    "p50": elapsed.get("p50"),
                    "p95": elapsed.get("p95"),
                    "max": elapsed.get("max"),
                    "field": "elapsed_ms",
                    "event": "render.stage.completed",
                }
            )
    else:
        rows.append(
            {
                "stage": "all",
                "count": elapsed.get("count"),
                "p50": elapsed.get("p50"),
                "p95": elapsed.get("p95"),
                "max": elapsed.get("max"),
                "field": "elapsed_ms",
                "event": "render.stage.completed",
            }
        )
    return rows


def workflow_latency_rows_for_artifact(summary: dict[str, Any] | None) -> list[dict[str, Any]]:
    if not summary:
        return []
    groups = summary.get("groups", {})
    workflows = groups.get("workflow", {}) if isinstance(groups, dict) else {}
    if not isinstance(workflows, dict):
        return []
    rows: list[dict[str, Any]] = []
    for workflow_name, workflow_summary in sorted(workflows.items()):
        if not isinstance(workflow_summary, dict):
            continue
        timings = workflow_summary.get("timings", {})
        if not isinstance(timings, dict):
            continue
        elapsed = timings.get("elapsed_ms", {})
        if not isinstance(elapsed, dict):
            continue
        rows.append(
            {
                "workflow": workflow_name,
                "field": "elapsed_ms",
                "count": elapsed.get("count"),
                "p50": elapsed.get("p50"),
                "p95": elapsed.get("p95"),
                "max": elapsed.get("max"),
            }
        )
    return rows


def tool_latency_rows_for_artifact(summary: dict[str, Any] | None) -> list[dict[str, Any]]:
    if not summary:
        return []
    groups = summary.get("groups", {})
    tools = groups.get("tool", {}) if isinstance(groups, dict) else {}
    rows: list[dict[str, Any]] = []
    if isinstance(tools, dict) and tools:
        for tool_id, tool_summary in sorted(tools.items()):
            if not isinstance(tool_summary, dict):
                continue
            events = tool_summary.get("events", {})
            if isinstance(events, dict) and "tool.stage.completed" not in events:
                continue
            timings = tool_summary.get("timings", {})
            if not isinstance(timings, dict):
                continue
            elapsed = timings.get("elapsed_ms", {})
            if not isinstance(elapsed, dict):
                continue
            rows.append(
                {
                    "tool_id": tool_id,
                    "field": "elapsed_ms",
                    "count": elapsed.get("count"),
                    "p50": elapsed.get("p50"),
                    "p95": elapsed.get("p95"),
                    "max": elapsed.get("max"),
                }
            )
        if rows:
            return rows

    events = summary.get("events", {})
    if not isinstance(events, dict):
        return []
    event_summary = events.get("tool.stage.completed")
    if not isinstance(event_summary, dict):
        return []
    timings = event_summary.get("timings", {})
    elapsed = timings.get("elapsed_ms", {}) if isinstance(timings, dict) else {}
    if not isinstance(elapsed, dict):
        return []
    categories = event_summary.get("categories", {})
    tools_category = categories.get("tool_id", {}) if isinstance(categories, dict) else {}
    if isinstance(tools_category, dict) and tools_category:
        for tool_id, count in sorted(tools_category.items()):
            rows.append(
                {
                    "tool_id": tool_id,
                    "field": "elapsed_ms",
                    "count": count,
                    "p50": elapsed.get("p50"),
                    "p95": elapsed.get("p95"),
                    "max": elapsed.get("max"),
                }
            )
    return rows


def memory_growth_rows_for_artifact(summary: dict[str, Any] | None) -> list[dict[str, Any]]:
    if not summary:
        return []
    events = summary.get("events", {})
    if not isinstance(events, dict):
        return []
    resource_summary = events.get("workflow.resource_summary")
    if not isinstance(resource_summary, dict):
        return []
    numeric_fields = resource_summary.get("numeric_fields", {})
    if not isinstance(numeric_fields, dict):
        return []
    rows: list[dict[str, Any]] = []
    for field_name in ("rss_start_kb", "rss_peak_kb", "rss_end_kb", "rss_delta_kb"):
        summary_row = numeric_fields.get(field_name)
        if not isinstance(summary_row, dict):
            continue
        rows.append(
            {
                "metric": field_name,
                "count": summary_row.get("count"),
                "p50": summary_row.get("p50"),
                "p95": summary_row.get("p95"),
                "max": summary_row.get("max"),
                "avg": summary_row.get("avg"),
                "unit": "KiB",
            }
        )
    return rows


def cache_pressure_rows_for_artifact(summary: dict[str, Any] | None) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    cache_fields = (
        ("cache_bytes", "bytes", "max"),
        ("decoded_cache_bytes", "bytes", "max"),
        ("linear_cache_bytes", "bytes", "max"),
        ("adjusted_cache_bytes", "bytes", "max"),
        ("rendered_cache_bytes", "bytes", "max"),
        ("cache_hit_rate", "ratio", "avg"),
    )
    for field_name, unit, statistic in cache_fields:
        field_summary = numeric_field_summary(summary, "preview.render.completed", field_name)
        if not field_summary:
            continue
        value = field_summary.get(statistic)
        if isinstance(value, (int, float)):
            rows.append(
                {
                    "metric": field_name,
                    "value": value,
                    "unit": unit,
                    "statistic": statistic,
                    "count": field_summary.get("count"),
                }
            )
    mask_tile_hit_rate = numeric_field_summary(summary, "mask.tile_cache.sampled", "cache_hit_rate")
    if mask_tile_hit_rate:
        value = mask_tile_hit_rate.get("avg")
        if isinstance(value, (int, float)):
            rows.append(
                {
                    "metric": "mask_tile_cache_hit_rate",
                    "value": value,
                    "unit": "ratio",
                    "statistic": "avg",
                    "count": mask_tile_hit_rate.get("count"),
                }
            )
    for field_name in ("cache_hit_count", "cache_miss_count", "cache_lookup_count"):
        field_summary = numeric_field_summary(summary, "mask.tile_cache.sampled", field_name)
        if not field_summary:
            continue
        value = field_summary.get("max")
        if isinstance(value, (int, float)):
            rows.append(
                {
                    "metric": f"mask_tile_{field_name}",
                    "value": value,
                    "unit": "count",
                    "statistic": "max",
                    "count": field_summary.get("count"),
                }
            )
    return rows


def backend_fallback_rows_for_artifact(summary: dict[str, Any] | None) -> list[dict[str, Any]]:
    if not summary:
        return []
    events = summary.get("events", {})
    if not isinstance(events, dict):
        return []
    render_summary = events.get("preview.render.completed")
    if not isinstance(render_summary, dict):
        return []
    categories = render_summary.get("categories", {})
    if not isinstance(categories, dict):
        return []
    rows: list[dict[str, Any]] = []
    for field_name in ("backend", "decode_backend", "fallback_reason"):
        values = categories.get(field_name, {})
        if not isinstance(values, dict):
            continue
        for value, count in sorted(values.items()):
            rows.append(
                {
                    "field": field_name,
                    "value": value,
                    "count": count,
                }
            )
    return rows


def worker_pressure_rows_for_artifact(summary: dict[str, Any] | None) -> list[dict[str, Any]]:
    if not summary:
        return []
    events = summary.get("events", {})
    if not isinstance(events, dict):
        return []
    rows: list[dict[str, Any]] = []

    def category_value(event_summary: dict[str, Any], field_name: str) -> str:
        categories = event_summary.get("categories", {})
        if not isinstance(categories, dict):
            return "-"
        values = categories.get(field_name, {})
        if not isinstance(values, dict) or len(values) != 1:
            return "-"
        return next(iter(values))

    def append_row(
        *,
        scope: str,
        event: str,
        workflow: str,
        worker_pool: str,
        surface: str,
        count: Any,
        numeric_fields: dict[str, Any],
        timings: dict[str, Any],
    ) -> None:
        active = numeric_fields.get("active_worker_count")
        pending = numeric_fields.get("pending_worker_count")
        worker_limit = numeric_fields.get("worker_limit")
        completed = numeric_fields.get("completed_count")
        cancelled = numeric_fields.get("cancelled_count")
        queue_wait = timings.get("queue_wait_ms") or numeric_fields.get("queue_wait_ms")
        if not any(
            isinstance(value, dict)
            for value in (active, pending, worker_limit, queue_wait, completed, cancelled)
        ):
            return
        rows.append(
            {
                "scope": scope,
                "event": event,
                "workflow": workflow,
                "worker_pool": worker_pool,
                "surface": surface,
                "count": count,
                "active_worker_max": active.get("max") if isinstance(active, dict) else None,
                "pending_worker_max": pending.get("max") if isinstance(pending, dict) else None,
                "worker_limit_max": worker_limit.get("max") if isinstance(worker_limit, dict) else None,
                "queue_wait_p95": queue_wait.get("p95") if isinstance(queue_wait, dict) else None,
                "queue_wait_max": queue_wait.get("max") if isinstance(queue_wait, dict) else None,
                "completed_count_max": completed.get("max") if isinstance(completed, dict) else None,
                "cancelled_count_max": cancelled.get("max") if isinstance(cancelled, dict) else None,
            }
        )

    for event_name, event_summary in sorted(events.items()):
        if not isinstance(event_summary, dict):
            continue
        numeric_fields = event_summary.get("numeric_fields", {})
        timings = event_summary.get("timings", {})
        if not isinstance(numeric_fields, dict):
            numeric_fields = {}
        if not isinstance(timings, dict):
            timings = {}
        append_row(
            scope="event",
            event=event_name,
            workflow=category_value(event_summary, "workflow"),
            worker_pool=category_value(event_summary, "worker_pool")
            if category_value(event_summary, "worker_pool") != "-"
            else category_value(event_summary, "pool_id"),
            surface=category_value(event_summary, "surface"),
            count=event_summary.get("count"),
            numeric_fields=numeric_fields,
            timings=timings,
        )
    groups = summary.get("groups", {})
    if not isinstance(groups, dict):
        return rows
    for dimension, row_field, event_prefix in (
        ("workflow", "workflow", "worker.workflow"),
        ("worker_pool", "worker_pool", "worker.pool"),
    ):
        group_values = groups.get(dimension, {})
        if not isinstance(group_values, dict):
            continue
        for group_name, group_summary in sorted(group_values.items()):
            if not isinstance(group_summary, dict):
                continue
            numeric_fields = group_summary.get("numeric_fields", {})
            timings = group_summary.get("timings", {})
            if not isinstance(numeric_fields, dict):
                numeric_fields = {}
            if not isinstance(timings, dict):
                timings = {}
            append_row(
                scope=dimension,
                event=f"{event_prefix}:{group_name}",
                workflow=group_name if row_field == "workflow" else "-",
                worker_pool=group_name if row_field == "worker_pool" else "-",
                surface="-",
                count=group_summary.get("count"),
                numeric_fields=numeric_fields,
                timings=timings,
            )
    return rows


def mask_performance_rows_for_artifact(summary: dict[str, Any] | None) -> list[dict[str, Any]]:
    if not summary:
        return []
    events = summary.get("events", {})
    if not isinstance(events, dict):
        return []
    definitions = (
        ("mask.brush.feedback_completed", "brush_feedback", "input_to_overlay_ms"),
        ("mask.raster.completed", "mask_rasterization", "elapsed_ms"),
        ("mask.tile_cache.sampled", "mask_tile_cache", "cache_hit_rate"),
        ("mask.smart_select.completed", "smart_selection", "elapsed_ms"),
        ("mask.refine_edge.completed", "refine_edge", "elapsed_ms"),
        ("mask.inpaint.completed", "inpaint", "elapsed_ms"),
    )
    rows: list[dict[str, Any]] = []
    for event_name, operation, preferred_field in definitions:
        event_summary = events.get(event_name)
        if not isinstance(event_summary, dict):
            continue
        timings = event_summary.get("timings", {})
        numeric_fields = event_summary.get("numeric_fields", {})
        categories = event_summary.get("categories", {})
        if not isinstance(timings, dict):
            timings = {}
        if not isinstance(numeric_fields, dict):
            numeric_fields = {}
        if not isinstance(categories, dict):
            categories = {}
        summary_row = timings.get(preferred_field)
        unit = "ms"
        if not isinstance(summary_row, dict):
            summary_row = numeric_fields.get(preferred_field)
            if preferred_field == "cache_hit_rate":
                unit = "ratio"
        if not isinstance(summary_row, dict):
            summary_row = timings.get("elapsed_ms")
            unit = "ms"
        if not isinstance(summary_row, dict):
            continue
        rows.append(
            {
                "event": event_name,
                "operation": operation,
                "field": preferred_field if preferred_field in timings or preferred_field in numeric_fields else "elapsed_ms",
                "unit": unit,
                "count": summary_row.get("count", event_summary.get("count")),
                "p50": summary_row.get("p50"),
                "p95": summary_row.get("p95"),
                "max": summary_row.get("max"),
                "avg": summary_row.get("avg"),
                "cache_hit_rate_avg": (
                    numeric_fields.get("cache_hit_rate", {}).get("avg")
                    if isinstance(numeric_fields.get("cache_hit_rate"), dict)
                    else None
                ),
                "cache_hit_max": (
                    numeric_fields.get("cache_hit_count", {}).get("max")
                    if isinstance(numeric_fields.get("cache_hit_count"), dict)
                    else None
                ),
                "cache_miss_max": (
                    numeric_fields.get("cache_miss_count", {}).get("max")
                    if isinstance(numeric_fields.get("cache_miss_count"), dict)
                    else None
                ),
                "preview_matte_rebuilt": (
                    categories.get("preview_matte_rebuilt", {})
                    if isinstance(categories.get("preview_matte_rebuilt"), dict)
                    else {}
                ),
                "preview_matte_reused": (
                    categories.get("preview_matte_reused", {})
                    if isinstance(categories.get("preview_matte_reused"), dict)
                    else {}
                ),
            }
        )
    return rows


def cancellation_rows_for_artifact(summary: dict[str, Any] | None) -> list[dict[str, Any]]:
    if not summary:
        return []
    events = summary.get("events", {})
    if not isinstance(events, dict):
        return []
    rows: list[dict[str, Any]] = []
    tokens = (
        "cancelled",
        "canceled",
        "superseded",
        "skipped",
        "stale",
        "preempted",
    )
    for event_name, event_summary in sorted(events.items()):
        if not isinstance(event_summary, dict) or not any(token in event_name for token in tokens):
            continue
        timings = event_summary.get("timings", {})
        elapsed = timings.get("elapsed_ms", {}) if isinstance(timings, dict) else {}
        if not isinstance(elapsed, dict):
            elapsed = {}
        rows.append(
            {
                "event": event_name,
                "count": event_summary.get("count"),
                "p95": elapsed.get("p95"),
                "max": elapsed.get("max"),
                "categories": event_summary.get("categories", {}),
            }
        )
    return rows


def qml_frame_rows_for_artifact(summary: dict[str, Any] | None) -> list[dict[str, Any]]:
    if not summary:
        return []
    events = summary.get("events", {})
    if not isinstance(events, dict):
        return []
    rows: list[dict[str, Any]] = []
    for event_name, event_summary in sorted(events.items()):
        if not isinstance(event_summary, dict) or not (
            event_name.startswith("qml.frame") or event_name.startswith("qml.route")
        ):
            continue
        timings = event_summary.get("timings", {})
        numeric_fields = event_summary.get("numeric_fields", {})
        if not isinstance(timings, dict):
            timings = {}
        if not isinstance(numeric_fields, dict):
            numeric_fields = {}
        frame_interval = timings.get("frame_interval_ms") or numeric_fields.get("frame_interval_ms")
        elapsed = timings.get("elapsed_ms") or numeric_fields.get("elapsed_ms")
        jank_count = numeric_fields.get("jank_frame_count")
        dropped_count = numeric_fields.get("dropped_frame_count")
        rows.append(
            {
                "event": event_name,
                "count": event_summary.get("count"),
                "frame_p95": frame_interval.get("p95") if isinstance(frame_interval, dict) else None,
                "frame_max": frame_interval.get("max") if isinstance(frame_interval, dict) else None,
                "route_p95": elapsed.get("p95") if isinstance(elapsed, dict) else None,
                "route_max": elapsed.get("max") if isinstance(elapsed, dict) else None,
                "jank_max": jank_count.get("max") if isinstance(jank_count, dict) else None,
                "dropped_max": dropped_count.get("max") if isinstance(dropped_count, dict) else None,
            }
        )
    return rows


def numeric_field_summary(
    summary: dict[str, Any] | None,
    event_name: str,
    field_name: str,
) -> dict[str, Any] | None:
    if not summary:
        return None
    events = summary.get("events", {})
    if not isinstance(events, dict):
        return None
    event_summary = events.get(event_name)
    if not isinstance(event_summary, dict):
        return None
    numeric_fields = event_summary.get("numeric_fields", {})
    if not isinstance(numeric_fields, dict):
        return None
    field_summary = numeric_fields.get(field_name)
    return field_summary if isinstance(field_summary, dict) else None


def resource_rows_for_artifact(
    manifest: dict[str, Any] | None,
    summary: dict[str, Any] | None,
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    if manifest:
        rss_fields = (
            ("process_start_rss", "rss_start_kb", "start_rss_kb"),
            ("process_peak_rss", "rss_peak_kb", "peak_rss_kb"),
            ("process_end_rss", "rss_end_kb", "end_rss_kb"),
            ("process_rss_delta", "rss_delta_kb", "rss_delta_kb"),
        )
        for resource_name, field_name, fallback_field in rss_fields:
            value = manifest.get(field_name)
            if not isinstance(value, (int, float)) and fallback_field != field_name:
                value = manifest.get(fallback_field)
            if isinstance(value, (int, float)):
                rows.append(
                    {
                        "resource": resource_name,
                        "field": field_name,
                        "value": value,
                        "unit": "KiB",
                        "event": "benchmark.manifest",
                        "statistic": "sample",
                    }
                )
        gpu_memory_kb = manifest.get("gpu_memory_kb")
        if isinstance(gpu_memory_kb, (int, float)):
            rows.append(
                {
                    "resource": "gpu_memory",
                    "field": "gpu_memory_kb",
                    "value": gpu_memory_kb,
                    "unit": "KiB",
                    "event": "benchmark.manifest",
                    "statistic": "sample",
                }
            )

    cache_fields = (
        ("preview_cache_total", "cache_bytes"),
        ("decoded_image_cache", "decoded_cache_bytes"),
        ("linear_image_cache", "linear_cache_bytes"),
        ("adjusted_preview_cache", "adjusted_cache_bytes"),
        ("rendered_preview_cache", "rendered_cache_bytes"),
    )
    for resource_name, field_name in cache_fields:
        field_summary = numeric_field_summary(summary, "preview.render.completed", field_name)
        if not field_summary:
            continue
        value = field_summary.get("max")
        if isinstance(value, (int, float)):
            rows.append(
                {
                    "resource": resource_name,
                    "field": field_name,
                    "value": value,
                    "unit": "bytes",
                    "event": "preview.render.completed",
                    "statistic": "max",
                }
            )

    cache_hit_rate_summary = numeric_field_summary(
        summary,
        "preview.render.completed",
        "cache_hit_rate",
    )
    if cache_hit_rate_summary:
        value = cache_hit_rate_summary.get("avg")
        if isinstance(value, (int, float)):
            rows.append(
                {
                    "resource": "preview_cache_hit_rate",
                    "field": "cache_hit_rate",
                    "value": value,
                    "unit": "ratio",
                    "event": "preview.render.completed",
                    "statistic": "avg",
                }
            )

    thumbnail_cache_fields = (
        ("thumbnail_cache_bytes", "thumbnail_cache_bytes", "bytes", "max"),
        ("thumbnail_cache_entries", "thumbnail_cache_entries", "entries", "max"),
        ("thumbnail_cache_probe", "thumbnail_cache_probe_ms", "ms", "max"),
    )
    for resource_name, field_name, unit, statistic in thumbnail_cache_fields:
        field_summary = numeric_field_summary(summary, "thumbnail.completed", field_name)
        if not field_summary:
            continue
        value = field_summary.get(statistic)
        if isinstance(value, (int, float)):
            rows.append(
                {
                    "resource": resource_name,
                    "field": field_name,
                    "value": value,
                    "unit": unit,
                    "event": "thumbnail.completed",
                    "statistic": statistic,
                }
            )

    native_texture_fields = (
        ("gpu_memory", "preview.texture.uploaded", "gpu_memory_bytes", "bytes", "max"),
        ("native_texture", "preview.texture.uploaded", "native_texture_bytes", "bytes", "max"),
        ("native_texture_upload", "preview.texture.uploaded", "upload_bytes", "bytes", "max"),
        (
            "gpu_memory_estimate",
            "preview.texture.uploaded",
            "gpu_memory_estimate_bytes",
            "bytes",
            "max",
        ),
        (
            "gpu_memory_preserved",
            "preview.texture.upload_preserved",
            "gpu_memory_bytes",
            "bytes",
            "max",
        ),
        (
            "native_texture_preserved",
            "preview.texture.upload_preserved",
            "native_texture_bytes",
            "bytes",
            "max",
        ),
        (
            "gpu_memory_estimate_preserved",
            "preview.texture.upload_preserved",
            "gpu_memory_estimate_bytes",
            "bytes",
            "max",
        ),
    )
    for resource_name, event_name, field_name, unit, statistic in native_texture_fields:
        field_summary = numeric_field_summary(summary, event_name, field_name)
        if not field_summary:
            continue
        value = field_summary.get(statistic)
        if isinstance(value, (int, float)):
            rows.append(
                {
                    "resource": resource_name,
                    "field": field_name,
                    "value": value,
                    "unit": unit,
                    "event": event_name,
                    "statistic": statistic,
                }
            )
    return rows


def warning_error_counts(summary: dict[str, Any] | None) -> dict[str, int]:
    counts = {"warning": 0, "error": 0, "fatal": 0}
    if not summary:
        return counts
    events = summary.get("events", {})
    if not isinstance(events, dict):
        return counts
    for event_summary in events.values():
        if not isinstance(event_summary, dict):
            continue
        severities = event_summary.get("severities", {})
        if not isinstance(severities, dict):
            continue
        for severity in counts:
            value = severities.get(severity)
            if isinstance(value, int):
                counts[severity] += value
    return counts


def attention_event_count(summary: dict[str, Any] | None) -> int:
    if not summary:
        return 0
    events = summary.get("events", {})
    if not isinstance(events, dict):
        return 0
    count = 0
    for event_name in events:
        if any(token in event_name for token in ("failed", "failure", "cancelled", "canceled")):
            event_summary = events[event_name]
            if isinstance(event_summary, dict) and isinstance(event_summary.get("count"), int):
                count += event_summary["count"]
    return count


def numeric_delta(current: Any, baseline: Any) -> float | None:
    if not isinstance(current, (int, float)) or not isinstance(baseline, (int, float)):
        return None
    return float(current) - float(baseline)


def artifact_scenario(artifact: dict[str, Any]) -> str | None:
    manifest = artifact.get("manifest")
    if not isinstance(manifest, dict):
        return None
    scenario = manifest.get("scenario")
    return scenario if isinstance(scenario, str) and scenario else None


def max_numeric(values: list[Any]) -> float | None:
    numeric_values = [float(value) for value in values if isinstance(value, (int, float))]
    if not numeric_values:
        return None
    return max(numeric_values)


def threshold_metric_value(artifact: dict[str, Any], metric: str) -> float | None:
    if metric == "preview_render_p95_ms":
        return max_numeric(
            [
                row.get("p95")
                for row in artifact.get("timings", [])
                if row.get("event") == "preview.render.completed" and row.get("field") == "elapsed_ms"
            ]
        )
    if metric == "mask_brush_feedback_p95_ms":
        return max_numeric(
            [
                row.get("p95")
                for row in artifact.get("mask_performance", [])
                if row.get("operation") == "brush_feedback"
            ]
        )
    if metric == "thumbnail_queue_wait_p95_ms":
        return max_numeric(
            [
                row.get("queue_wait_p95")
                for row in artifact.get("worker_pressure", [])
                if row.get("event") == "thumbnail.completed"
            ]
        )
    if metric == "workspace_route_ready_p95_ms":
        return max_numeric(
            [
                row.get("route_p95")
                for row in artifact.get("qml_frames", [])
                if row.get("event") == "qml.route.ready"
            ]
        )
    if metric == "render_cancellation_latency_p95_ms":
        return max_numeric([row.get("p95") for row in artifact.get("cancellations", [])])
    if metric == "memory_growth_delta_kb":
        return max_numeric(
            [
                row.get("p95")
                for row in artifact.get("memory_growth", [])
                if row.get("metric") == "rss_delta_kb"
            ]
        )
    if metric == "worker_queue_wait_p95_ms":
        return max_numeric([row.get("queue_wait_p95") for row in artifact.get("worker_pressure", [])])
    if metric == "qml_jank_frames_max":
        return max_numeric([row.get("jank_max") for row in artifact.get("qml_frames", [])])
    if metric == "qml_dropped_frames_max":
        return max_numeric([row.get("dropped_max") for row in artifact.get("qml_frames", [])])
    if metric == "preview_cache_hit_rate_avg":
        return max_numeric(
            [
                row.get("value")
                for row in artifact.get("cache_pressure", [])
                if row.get("metric") == "cache_hit_rate"
            ]
        )
    return None


def threshold_rows_for_artifact(artifact: dict[str, Any], profile: str) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for threshold in PERFORMANCE_THRESHOLDS.get(profile, ()):
        value = threshold_metric_value(artifact, threshold.metric)
        if value is None:
            status = "missing"
        elif threshold.direction == "min":
            status = "passed" if value >= threshold.limit else "failed"
        else:
            status = "passed" if value <= threshold.limit else "failed"
        rows.append(
            {
                "profile": profile,
                "metric": threshold.metric,
                "value": value,
                "limit": threshold.limit,
                "unit": threshold.unit,
                "direction": threshold.direction,
                "status": status,
            }
        )
    return rows


def threshold_violation_count(report: dict[str, Any]) -> int:
    count = 0
    for suite in report.get("suites", []):
        if not isinstance(suite, dict):
            continue
        for artifact in suite.get("artifacts", []):
            if not isinstance(artifact, dict):
                continue
            for row in artifact.get("thresholds", []):
                if isinstance(row, dict) and row.get("status") == "failed":
                    count += 1
    return count


def bottleneck_summary_for_artifact(artifact: dict[str, Any]) -> list[dict[str, Any]]:
    backend_fallback_count = sum(
        int(row.get("count", 0))
        for row in artifact.get("backend_fallbacks", [])
        if isinstance(row, dict) and row.get("field") == "fallback_reason"
    )
    cancellation_count = sum(
        int(row.get("count", 0))
        for row in artifact.get("cancellations", [])
        if isinstance(row, dict) and isinstance(row.get("count"), int)
    )
    threshold_failed = sum(
        1
        for row in artifact.get("thresholds", [])
        if isinstance(row, dict) and row.get("status") == "failed"
    )
    threshold_missing = sum(
        1
        for row in artifact.get("thresholds", [])
        if isinstance(row, dict) and row.get("status") == "missing"
    )
    return [
        {
            "scenario": artifact_scenario(artifact) or "-",
            "render_p95_ms": threshold_metric_value(artifact, "preview_render_p95_ms"),
            "mask_brush_p95_ms": threshold_metric_value(artifact, "mask_brush_feedback_p95_ms"),
            "memory_delta_kb": threshold_metric_value(artifact, "memory_growth_delta_kb"),
            "worker_queue_p95_ms": threshold_metric_value(artifact, "worker_queue_wait_p95_ms"),
            "qml_frame_p95_ms": max_numeric(
                [row.get("frame_p95") for row in artifact.get("qml_frames", [])]
            ),
            "qml_route_p95_ms": threshold_metric_value(artifact, "workspace_route_ready_p95_ms"),
            "qml_jank_max": threshold_metric_value(artifact, "qml_jank_frames_max"),
            "cancellation_count": cancellation_count,
            "backend_fallback_count": backend_fallback_count,
            "threshold_failed": threshold_failed,
            "threshold_missing": threshold_missing,
        }
    ]


def baseline_artifacts_for_suite(
    baseline_report: dict[str, Any] | None,
    suite_name: str,
) -> list[dict[str, Any]]:
    if not baseline_report:
        return []
    suites = baseline_report.get("suites", [])
    if not isinstance(suites, list):
        return []
    for suite in suites:
        if not isinstance(suite, dict) or suite.get("name") != suite_name:
            continue
        artifacts = suite.get("artifacts", [])
        return [artifact for artifact in artifacts if isinstance(artifact, dict)]
    return []


def match_baseline_artifact(
    current_artifact: dict[str, Any],
    baseline_artifacts: list[dict[str, Any]],
) -> dict[str, Any] | None:
    if not baseline_artifacts:
        return None
    current_scenario = artifact_scenario(current_artifact)
    if current_scenario:
        for artifact in baseline_artifacts:
            if artifact_scenario(artifact) == current_scenario:
                return artifact
    return baseline_artifacts[0] if len(baseline_artifacts) == 1 else None


def index_rows(rows: Any, key_fields: tuple[str, ...]) -> dict[tuple[Any, ...], dict[str, Any]]:
    indexed: dict[tuple[Any, ...], dict[str, Any]] = {}
    if not isinstance(rows, list):
        return indexed
    for row in rows:
        if not isinstance(row, dict):
            continue
        key = tuple(row.get(field) for field in key_fields)
        if all(value is not None for value in key):
            indexed[key] = row
    return indexed


def append_latency_comparisons(
    comparisons: list[dict[str, Any]],
    current_artifact: dict[str, Any],
    baseline_artifact: dict[str, Any],
    category: str,
    row_name: str,
    key_fields: tuple[str, ...],
    label_field: str,
) -> None:
    baseline_rows = index_rows(baseline_artifact.get(row_name), key_fields)
    current_rows = current_artifact.get(row_name, [])
    if not isinstance(current_rows, list):
        return
    for current_row in current_rows:
        if not isinstance(current_row, dict):
            continue
        key = tuple(current_row.get(field) for field in key_fields)
        baseline_row = baseline_rows.get(key)
        if not baseline_row:
            continue
        comparisons.append(
            {
                "category": category,
                "metric": current_row.get(label_field),
                "field": current_row.get("field", "elapsed_ms"),
                "current_p50": current_row.get("p50"),
                "baseline_p50": baseline_row.get("p50"),
                "delta_p50": numeric_delta(current_row.get("p50"), baseline_row.get("p50")),
                "current_p95": current_row.get("p95"),
                "baseline_p95": baseline_row.get("p95"),
                "delta_p95": numeric_delta(current_row.get("p95"), baseline_row.get("p95")),
                "current_max": current_row.get("max"),
                "baseline_max": baseline_row.get("max"),
                "delta_max": numeric_delta(current_row.get("max"), baseline_row.get("max")),
            }
        )


def append_memory_comparisons(
    comparisons: list[dict[str, Any]],
    current_artifact: dict[str, Any],
    baseline_artifact: dict[str, Any],
) -> None:
    baseline_rows = index_rows(baseline_artifact.get("memory_growth"), ("metric",))
    current_rows = current_artifact.get("memory_growth", [])
    if not isinstance(current_rows, list):
        return
    for current_row in current_rows:
        if not isinstance(current_row, dict):
            continue
        baseline_row = baseline_rows.get((current_row.get("metric"),))
        if not baseline_row:
            continue
        comparisons.append(
            {
                "category": "memory_growth",
                "metric": current_row.get("metric"),
                "unit": current_row.get("unit"),
                "current_p95": current_row.get("p95"),
                "baseline_p95": baseline_row.get("p95"),
                "delta_p95": numeric_delta(current_row.get("p95"), baseline_row.get("p95")),
                "current_max": current_row.get("max"),
                "baseline_max": baseline_row.get("max"),
                "delta_max": numeric_delta(current_row.get("max"), baseline_row.get("max")),
            }
        )


def append_cache_comparisons(
    comparisons: list[dict[str, Any]],
    current_artifact: dict[str, Any],
    baseline_artifact: dict[str, Any],
) -> None:
    baseline_rows = index_rows(baseline_artifact.get("cache_pressure"), ("metric", "statistic"))
    current_rows = current_artifact.get("cache_pressure", [])
    if not isinstance(current_rows, list):
        return
    for current_row in current_rows:
        if not isinstance(current_row, dict):
            continue
        key = (current_row.get("metric"), current_row.get("statistic"))
        baseline_row = baseline_rows.get(key)
        if not baseline_row:
            continue
        comparisons.append(
            {
                "category": "cache_pressure",
                "metric": current_row.get("metric"),
                "statistic": current_row.get("statistic"),
                "unit": current_row.get("unit"),
                "current_value": current_row.get("value"),
                "baseline_value": baseline_row.get("value"),
                "delta_value": numeric_delta(current_row.get("value"), baseline_row.get("value")),
            }
        )


def append_worker_comparisons(
    comparisons: list[dict[str, Any]],
    current_artifact: dict[str, Any],
    baseline_artifact: dict[str, Any],
) -> None:
    baseline_rows = index_rows(baseline_artifact.get("worker_pressure"), ("event",))
    current_rows = current_artifact.get("worker_pressure", [])
    if not isinstance(current_rows, list):
        return
    for current_row in current_rows:
        if not isinstance(current_row, dict):
            continue
        baseline_row = baseline_rows.get((current_row.get("event"),))
        if not baseline_row:
            continue
        comparisons.append(
            {
                "category": "worker_pressure",
                "metric": current_row.get("event"),
                "current_queue_wait_p95": current_row.get("queue_wait_p95"),
                "baseline_queue_wait_p95": baseline_row.get("queue_wait_p95"),
                "delta_queue_wait_p95": numeric_delta(
                    current_row.get("queue_wait_p95"),
                    baseline_row.get("queue_wait_p95"),
                ),
                "current_active_worker_max": current_row.get("active_worker_max"),
                "baseline_active_worker_max": baseline_row.get("active_worker_max"),
                "delta_active_worker_max": numeric_delta(
                    current_row.get("active_worker_max"),
                    baseline_row.get("active_worker_max"),
                ),
                "current_pending_worker_max": current_row.get("pending_worker_max"),
                "baseline_pending_worker_max": baseline_row.get("pending_worker_max"),
                "delta_pending_worker_max": numeric_delta(
                    current_row.get("pending_worker_max"),
                    baseline_row.get("pending_worker_max"),
                ),
            }
        )


def append_backend_comparisons(
    comparisons: list[dict[str, Any]],
    current_artifact: dict[str, Any],
    baseline_artifact: dict[str, Any],
) -> None:
    baseline_rows = index_rows(baseline_artifact.get("backend_fallbacks"), ("field", "value"))
    current_rows = current_artifact.get("backend_fallbacks", [])
    if not isinstance(current_rows, list):
        return
    for current_row in current_rows:
        if not isinstance(current_row, dict):
            continue
        key = (current_row.get("field"), current_row.get("value"))
        baseline_row = baseline_rows.get(key)
        if not baseline_row:
            continue
        comparisons.append(
            {
                "category": "backend_fallback",
                "metric": f"{current_row.get('field')}={current_row.get('value')}",
                "current_count": current_row.get("count"),
                "baseline_count": baseline_row.get("count"),
                "delta_count": numeric_delta(current_row.get("count"), baseline_row.get("count")),
            }
        )


def baseline_comparison_for_artifact(
    current_artifact: dict[str, Any],
    baseline_artifact: dict[str, Any] | None,
) -> dict[str, Any]:
    if not baseline_artifact:
        return {"matched": False, "baseline_path": None, "rows": []}
    rows: list[dict[str, Any]] = []
    append_latency_comparisons(
        rows,
        current_artifact,
        baseline_artifact,
        "event_timing",
        "timings",
        ("event", "field"),
        "event",
    )
    append_latency_comparisons(
        rows,
        current_artifact,
        baseline_artifact,
        "workflow_latency",
        "workflow_latencies",
        ("workflow", "field"),
        "workflow",
    )
    append_latency_comparisons(
        rows,
        current_artifact,
        baseline_artifact,
        "tool_latency",
        "tool_latencies",
        ("tool_id", "field"),
        "tool_id",
    )
    append_latency_comparisons(
        rows,
        current_artifact,
        baseline_artifact,
        "render_stage",
        "render_stages",
        ("stage", "field"),
        "stage",
    )
    append_memory_comparisons(rows, current_artifact, baseline_artifact)
    append_cache_comparisons(rows, current_artifact, baseline_artifact)
    append_worker_comparisons(rows, current_artifact, baseline_artifact)
    append_backend_comparisons(rows, current_artifact, baseline_artifact)
    return {
        "matched": True,
        "baseline_path": baseline_artifact.get("path"),
        "rows": rows,
    }


def build_report_payload(
    results: list[SuiteResult],
    baseline: dict[str, Any] | None,
    threshold_profile: str = "local-dev",
) -> dict[str, Any]:
    suites: list[dict[str, Any]] = []
    baseline_report = baseline.get("report") if isinstance(baseline, dict) else None
    baseline_report = baseline_report if isinstance(baseline_report, dict) else None
    for result in results:
        artifacts: list[dict[str, Any]] = []
        baseline_artifacts = baseline_artifacts_for_suite(baseline_report, result.name)
        for artifact in result.artifact_dirs:
            summary = summary_for_artifact(artifact)
            manifest = manifest_for_artifact(artifact)
            artifact_entry = {
                "path": artifact,
                "manifest": manifest,
                "warning_error_counts": warning_error_counts(summary),
                "attention_event_count": attention_event_count(summary),
                "timings": event_timing_rows(summary),
                "workflow_latencies": workflow_latency_rows_for_artifact(summary),
                "tool_latencies": tool_latency_rows_for_artifact(summary),
                "render_stages": render_stage_rows_for_artifact(summary),
                "memory_growth": memory_growth_rows_for_artifact(summary),
                "cache_pressure": cache_pressure_rows_for_artifact(summary),
                "backend_fallbacks": backend_fallback_rows_for_artifact(summary),
                "worker_pressure": worker_pressure_rows_for_artifact(summary),
                "mask_performance": mask_performance_rows_for_artifact(summary),
                "cancellations": cancellation_rows_for_artifact(summary),
                "qml_frames": qml_frame_rows_for_artifact(summary),
                "resources": resource_rows_for_artifact(manifest, summary),
            }
            artifact_entry["thresholds"] = threshold_rows_for_artifact(
                artifact_entry,
                threshold_profile,
            )
            artifact_entry["bottlenecks"] = bottleneck_summary_for_artifact(artifact_entry)
            baseline_artifact = match_baseline_artifact(artifact_entry, baseline_artifacts)
            artifact_entry["baseline_comparison"] = baseline_comparison_for_artifact(
                artifact_entry,
                baseline_artifact,
            )
            artifacts.append(artifact_entry)
        suites.append(
            {
                "name": result.name,
                "description": result.description,
                "status": result.status,
                "returncode": result.returncode,
                "log_file": result.log_file,
                "artifact_dirs": result.artifact_dirs,
                "artifacts": artifacts,
            }
        )
    payload: dict[str, Any] = {
        "threshold_profile": threshold_profile,
        "threshold_violation_count": threshold_violation_count({"suites": suites}),
        "suites": suites,
    }
    if baseline is not None:
        baseline_manifest = baseline.get("manifest", {}) if isinstance(baseline, dict) else {}
        if not isinstance(baseline_manifest, dict):
            baseline_manifest = {}
        payload["baseline"] = {
            "path": baseline.get("path") if isinstance(baseline, dict) else None,
            "profile": baseline_manifest.get("profile"),
            "suite_count": len(baseline_manifest.get("suites", []))
            if isinstance(baseline_manifest.get("suites"), list)
            else 0,
        }
    return payload


def build_metrics_payload(report: dict[str, Any]) -> dict[str, Any]:
    metrics_suites: list[dict[str, Any]] = []
    for suite in report.get("suites", []):
        if not isinstance(suite, dict):
            continue
        suite_metrics: dict[str, Any] = {
            "name": suite.get("name"),
            "status": suite.get("status"),
            "artifacts": [],
        }
        for artifact in suite.get("artifacts", []):
            if not isinstance(artifact, dict):
                continue
            artifact_metrics = {
                "path": artifact.get("path"),
                "timings": artifact.get("timings", []),
                "workflow_latencies": artifact.get("workflow_latencies", []),
                "tool_latencies": artifact.get("tool_latencies", []),
                "render_stages": artifact.get("render_stages", []),
                "memory_growth": artifact.get("memory_growth", []),
                "cache_pressure": artifact.get("cache_pressure", []),
                "backend_fallbacks": artifact.get("backend_fallbacks", []),
                "worker_pressure": artifact.get("worker_pressure", []),
                "mask_performance": artifact.get("mask_performance", []),
                "cancellations": artifact.get("cancellations", []),
                "qml_frames": artifact.get("qml_frames", []),
                "resources": artifact.get("resources", []),
                "thresholds": artifact.get("thresholds", []),
                "bottlenecks": artifact.get("bottlenecks", []),
                "baseline_comparison": artifact.get("baseline_comparison", {}),
                "warning_error_counts": artifact.get("warning_error_counts", {}),
                "attention_event_count": artifact.get("attention_event_count", 0),
            }
            suite_metrics["artifacts"].append(artifact_metrics)
        metrics_suites.append(suite_metrics)
    return {
        "schema": "photo-editor-app-performance-metrics-v1",
        "threshold_profile": report.get("threshold_profile"),
        "threshold_violation_count": report.get("threshold_violation_count", 0),
        "suites": metrics_suites,
    }


def fmt_ms(value: Any) -> str:
    return "-" if value is None else f"{float(value):.1f}"


def fmt_number(value: Any) -> str:
    return "-" if value is None else f"{float(value):.1f}"


def fmt_categories(value: Any) -> str:
    if not isinstance(value, dict):
        return "-"
    fragments: list[str] = []
    for category_name, category_counts in sorted(value.items()):
        if not isinstance(category_counts, dict):
            continue
        counts = ", ".join(
            f"{category_value}={count}" for category_value, count in sorted(category_counts.items())
        )
        if counts:
            fragments.append(f"{category_name}: {counts}")
    return "<br>".join(fragments) if fragments else "-"


def write_markdown_report(path: Path, manifest: dict[str, Any], report: dict[str, Any]) -> None:
    lines = [
        "# App Performance Operability Report",
        "",
        f"- profile: `{manifest['profile']}`",
        f"- status: `{manifest['status']}`",
        f"- started: `{manifest['started_at']}`",
        f"- ended: `{manifest['ended_at']}`",
        f"- artifact: `{manifest['artifact_dir']}`",
        "",
        "## Suites",
        "",
        "| suite | status | artifacts | warnings | errors | attention events |",
        "| --- | --- | ---: | ---: | ---: | ---: |",
    ]
    for suite in report["suites"]:
        warnings = 0
        errors = 0
        attention = 0
        for artifact in suite["artifacts"]:
            counts = artifact["warning_error_counts"]
            warnings += counts["warning"]
            errors += counts["error"] + counts["fatal"]
            attention += artifact["attention_event_count"]
        lines.append(
            f"| {suite['name']} | {suite['status']} | {len(suite['artifact_dirs'])} | "
            f"{warnings} | {errors} | {attention} |"
        )

    lines.extend(["", "## Timings", ""])
    for suite in report["suites"]:
        lines.extend([f"### {suite['name']}", ""])
        if not suite["artifacts"]:
            lines.extend(["No benchmark artifacts were reported.", ""])
            continue
        lines.extend(["| artifact | event | field | n | p50 ms | p95 ms | max ms |", "| --- | --- | --- | ---: | ---: | ---: | ---: |"])
        for artifact in suite["artifacts"]:
            artifact_name = Path(artifact["path"]).name
            for row in artifact["timings"][:12]:
                lines.append(
                    f"| {artifact_name} | {row['event']} | {row['field']} | {row['count']} | "
                    f"{fmt_ms(row['p50'])} | {fmt_ms(row['p95'])} | {fmt_ms(row['max'])} |"
                )
        lines.append("")

    lines.extend(["", "## Workflow Latency", ""])
    has_workflow_rows = False
    lines.extend(
        [
            "| suite | artifact | workflow | field | n | p50 ms | p95 ms | max ms |",
            "| --- | --- | --- | --- | ---: | ---: | ---: | ---: |",
        ]
    )
    for suite in report["suites"]:
        for artifact in suite["artifacts"]:
            artifact_name = Path(artifact["path"]).name
            for row in artifact.get("workflow_latencies", []):
                has_workflow_rows = True
                lines.append(
                    f"| {suite['name']} | {artifact_name} | {row['workflow']} | {row['field']} | "
                    f"{row['count']} | {fmt_ms(row['p50'])} | {fmt_ms(row['p95'])} | "
                    f"{fmt_ms(row['max'])} |"
                )
    if not has_workflow_rows:
        lines.append("| - | - | - | - | - | - | - | - |")
    lines.append("")

    lines.extend(["", "## Tool Latency", ""])
    has_tool_rows = False
    lines.extend(
        [
            "| suite | artifact | tool | field | n | p50 ms | p95 ms | max ms |",
            "| --- | --- | --- | --- | ---: | ---: | ---: | ---: |",
        ]
    )
    for suite in report["suites"]:
        for artifact in suite["artifacts"]:
            artifact_name = Path(artifact["path"]).name
            for row in artifact.get("tool_latencies", []):
                has_tool_rows = True
                lines.append(
                    f"| {suite['name']} | {artifact_name} | {row['tool_id']} | {row['field']} | "
                    f"{row['count']} | {fmt_ms(row['p50'])} | {fmt_ms(row['p95'])} | "
                    f"{fmt_ms(row['max'])} |"
                )
    if not has_tool_rows:
        lines.append("| - | - | - | - | - | - | - | - |")
    lines.append("")

    lines.extend(["", "## Render Stages", ""])
    has_render_stage_rows = False
    lines.extend(
        [
            "| suite | artifact | stage | field | n | p50 ms | p95 ms | max ms |",
            "| --- | --- | --- | --- | ---: | ---: | ---: | ---: |",
        ]
    )
    for suite in report["suites"]:
        for artifact in suite["artifacts"]:
            artifact_name = Path(artifact["path"]).name
            for row in artifact.get("render_stages", []):
                has_render_stage_rows = True
                lines.append(
                    f"| {suite['name']} | {artifact_name} | {row['stage']} | {row['field']} | "
                    f"{row['count']} | {fmt_ms(row['p50'])} | {fmt_ms(row['p95'])} | "
                    f"{fmt_ms(row['max'])} |"
                )
    if not has_render_stage_rows:
        lines.append("| - | - | - | - | - | - | - | - |")
    lines.append("")

    lines.extend(["", "## Memory Growth", ""])
    has_memory_rows = False
    lines.extend(
        [
            "| suite | artifact | metric | n | p50 | p95 | max | avg | unit |",
            "| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | --- |",
        ]
    )
    for suite in report["suites"]:
        for artifact in suite["artifacts"]:
            artifact_name = Path(artifact["path"]).name
            for row in artifact.get("memory_growth", []):
                has_memory_rows = True
                lines.append(
                    f"| {suite['name']} | {artifact_name} | {row['metric']} | {row['count']} | "
                    f"{fmt_number(row['p50'])} | {fmt_number(row['p95'])} | "
                    f"{fmt_number(row['max'])} | {fmt_number(row['avg'])} | {row['unit']} |"
                )
    if not has_memory_rows:
        lines.append("| - | - | - | - | - | - | - | - | - |")
    lines.append("")

    lines.extend(["", "## Cache Pressure", ""])
    has_cache_rows = False
    lines.extend(
        [
            "| suite | artifact | metric | statistic | value | unit | n |",
            "| --- | --- | --- | --- | ---: | --- | ---: |",
        ]
    )
    for suite in report["suites"]:
        for artifact in suite["artifacts"]:
            artifact_name = Path(artifact["path"]).name
            for row in artifact.get("cache_pressure", []):
                has_cache_rows = True
                lines.append(
                    f"| {suite['name']} | {artifact_name} | {row['metric']} | "
                    f"{row['statistic']} | {fmt_number(row['value'])} | {row['unit']} | "
                    f"{row['count']} |"
                )
    if not has_cache_rows:
        lines.append("| - | - | - | - | - | - | - |")
    lines.append("")

    lines.extend(["", "## Backend Fallbacks", ""])
    has_backend_rows = False
    lines.extend(
        [
            "| suite | artifact | field | value | count |",
            "| --- | --- | --- | --- | ---: |",
        ]
    )
    for suite in report["suites"]:
        for artifact in suite["artifacts"]:
            artifact_name = Path(artifact["path"]).name
            for row in artifact.get("backend_fallbacks", []):
                has_backend_rows = True
                lines.append(
                    f"| {suite['name']} | {artifact_name} | {row['field']} | "
                    f"{row['value']} | {row['count']} |"
                )
    if not has_backend_rows:
        lines.append("| - | - | - | - | - |")
    lines.append("")

    lines.extend(["", "## Worker Pressure", ""])
    has_worker_rows = False
    lines.extend(
        [
            "| suite | artifact | scope | workflow | worker pool | surface | event | n | active max | pending max | worker limit | queue wait p95 ms | queue wait max ms | completed max | cancelled max |",
            "| --- | --- | --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
        ]
    )
    for suite in report["suites"]:
        for artifact in suite["artifacts"]:
            artifact_name = Path(artifact["path"]).name
            for row in artifact.get("worker_pressure", []):
                has_worker_rows = True
                lines.append(
                    f"| {suite['name']} | {artifact_name} | {row.get('scope', 'event')} | "
                    f"{row.get('workflow', '-')} | {row.get('worker_pool', '-')} | "
                    f"{row.get('surface', '-')} | {row['event']} | {row['count']} | "
                    f"{fmt_number(row['active_worker_max'])} | "
                    f"{fmt_number(row['pending_worker_max'])} | "
                    f"{fmt_number(row['worker_limit_max'])} | "
                    f"{fmt_ms(row['queue_wait_p95'])} | {fmt_ms(row['queue_wait_max'])} | "
                    f"{fmt_number(row.get('completed_count_max'))} | "
                    f"{fmt_number(row.get('cancelled_count_max'))} |"
                )
    if not has_worker_rows:
        lines.append("| - | - | - | - | - | - | - | - | - | - | - | - | - | - | - |")
    lines.append("")

    lines.extend(["", "## Mask Performance", ""])
    has_mask_rows = False
    lines.extend(
        [
            "| suite | artifact | operation | event | field | n | p50 | p95 | max | unit | cache hit avg | cache hit max | cache miss max |",
            "| --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: | --- | ---: | ---: | ---: |",
        ]
    )
    for suite in report["suites"]:
        for artifact in suite["artifacts"]:
            artifact_name = Path(artifact["path"]).name
            for row in artifact.get("mask_performance", []):
                has_mask_rows = True
                lines.append(
                    f"| {suite['name']} | {artifact_name} | {row['operation']} | "
                    f"{row['event']} | {row['field']} | {row['count']} | "
                    f"{fmt_number(row['p50'])} | {fmt_number(row['p95'])} | "
                    f"{fmt_number(row['max'])} | {row['unit']} | "
                    f"{fmt_number(row.get('cache_hit_rate_avg'))} | "
                    f"{fmt_number(row.get('cache_hit_max'))} | "
                    f"{fmt_number(row.get('cache_miss_max'))} |"
                )
    if not has_mask_rows:
        lines.append("| - | - | - | - | - | - | - | - | - | - | - | - | - |")
    lines.append("")

    lines.extend(["", "## Cancellations And Wasted Work", ""])
    has_cancellation_rows = False
    lines.extend(
        [
            "| suite | artifact | event | count | p95 ms | max ms | categories |",
            "| --- | --- | --- | ---: | ---: | ---: | --- |",
        ]
    )
    for suite in report["suites"]:
        for artifact in suite["artifacts"]:
            artifact_name = Path(artifact["path"]).name
            for row in artifact.get("cancellations", []):
                has_cancellation_rows = True
                lines.append(
                    f"| {suite['name']} | {artifact_name} | {row['event']} | "
                    f"{row['count']} | {fmt_ms(row.get('p95'))} | {fmt_ms(row.get('max'))} | "
                    f"{fmt_categories(row.get('categories'))} |"
                )
    if not has_cancellation_rows:
        lines.append("| - | - | - | - | - | - | - |")
    lines.append("")

    lines.extend(["", "## QML Frame And Jank", ""])
    has_qml_rows = False
    lines.extend(
        [
            "| suite | artifact | event | n | frame p95 ms | frame max ms | route p95 ms | route max ms | jank max | dropped max |",
            "| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
        ]
    )
    for suite in report["suites"]:
        for artifact in suite["artifacts"]:
            artifact_name = Path(artifact["path"]).name
            for row in artifact.get("qml_frames", []):
                has_qml_rows = True
                lines.append(
                    f"| {suite['name']} | {artifact_name} | {row['event']} | {row['count']} | "
                    f"{fmt_ms(row['frame_p95'])} | {fmt_ms(row['frame_max'])} | "
                    f"{fmt_ms(row['route_p95'])} | {fmt_ms(row['route_max'])} | "
                    f"{fmt_number(row['jank_max'])} | {fmt_number(row['dropped_max'])} |"
                )
    if not has_qml_rows:
        lines.append("| - | - | - | - | - | - | - | - | - | - |")
    lines.append("")

    lines.extend(["", "## Performance Thresholds", ""])
    lines.append(f"- threshold profile: `{report.get('threshold_profile', '-')}`")
    lines.append(f"- violations: `{report.get('threshold_violation_count', 0)}`")
    lines.append("")
    has_threshold_rows = False
    lines.extend(
        [
            "| suite | artifact | metric | value | direction | limit | unit | status |",
            "| --- | --- | --- | ---: | --- | ---: | --- | --- |",
        ]
    )
    for suite in report["suites"]:
        for artifact in suite["artifacts"]:
            artifact_name = Path(artifact["path"]).name
            for row in artifact.get("thresholds", []):
                has_threshold_rows = True
                lines.append(
                    f"| {suite['name']} | {artifact_name} | {row['metric']} | "
                    f"{fmt_number(row.get('value'))} | {row.get('direction', '-')} | "
                    f"{fmt_number(row.get('limit'))} | {row.get('unit', '-')} | "
                    f"{row.get('status', '-')} |"
                )
    if not has_threshold_rows:
        lines.append("| - | - | - | - | - | - | - | - |")
    lines.append("")

    lines.extend(["", "## Bottleneck Summary", ""])
    has_bottleneck_rows = False
    lines.extend(
        [
            "| suite | artifact | scenario | render p95 ms | mask p95 ms | memory delta KiB | worker queue p95 ms | QML frame p95 ms | QML route p95 ms | QML jank max | cancellations | backend fallbacks | failed gates | missing gates |",
            "| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
        ]
    )
    for suite in report["suites"]:
        for artifact in suite["artifacts"]:
            artifact_name = Path(artifact["path"]).name
            for row in artifact.get("bottlenecks", []):
                has_bottleneck_rows = True
                lines.append(
                    f"| {suite['name']} | {artifact_name} | {row.get('scenario', '-')} | "
                    f"{fmt_ms(row.get('render_p95_ms'))} | {fmt_ms(row.get('mask_brush_p95_ms'))} | "
                    f"{fmt_number(row.get('memory_delta_kb'))} | "
                    f"{fmt_ms(row.get('worker_queue_p95_ms'))} | "
                    f"{fmt_ms(row.get('qml_frame_p95_ms'))} | "
                    f"{fmt_ms(row.get('qml_route_p95_ms'))} | "
                    f"{fmt_number(row.get('qml_jank_max'))} | "
                    f"{fmt_number(row.get('cancellation_count'))} | "
                    f"{fmt_number(row.get('backend_fallback_count'))} | "
                    f"{fmt_number(row.get('threshold_failed'))} | "
                    f"{fmt_number(row.get('threshold_missing'))} |"
                )
    if not has_bottleneck_rows:
        lines.append("| - | - | - | - | - | - | - | - | - | - | - | - | - | - |")
    lines.append("")

    lines.extend(["", "## Baseline Deltas", ""])
    has_baseline_rows = False
    lines.extend(
        [
            "| suite | artifact | baseline artifact | category | metric | p50 delta | p95 delta | max delta | value delta | queue wait p95 delta | count delta |",
            "| --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |",
        ]
    )
    for suite in report["suites"]:
        for artifact in suite["artifacts"]:
            artifact_name = Path(artifact["path"]).name
            comparison = artifact.get("baseline_comparison", {})
            if not isinstance(comparison, dict):
                continue
            baseline_path = comparison.get("baseline_path")
            baseline_name = Path(str(baseline_path)).name if baseline_path else "-"
            rows = comparison.get("rows", [])
            if not isinstance(rows, list):
                continue
            for row in rows:
                if not isinstance(row, dict):
                    continue
                has_baseline_rows = True
                lines.append(
                    f"| {suite['name']} | {artifact_name} | {baseline_name} | "
                    f"{row.get('category', '-')} | {row.get('metric', '-')} | "
                    f"{fmt_number(row.get('delta_p50'))} | {fmt_number(row.get('delta_p95'))} | "
                    f"{fmt_number(row.get('delta_max'))} | {fmt_number(row.get('delta_value'))} | "
                    f"{fmt_ms(row.get('delta_queue_wait_p95'))} | "
                    f"{fmt_number(row.get('delta_count'))} |"
                )
    if not has_baseline_rows:
        lines.append("| - | - | - | - | - | - | - | - | - | - | - |")
    lines.append("")

    lines.extend(["", "## Resources", ""])
    has_resource_rows = False
    lines.extend(
        [
            "| suite | artifact | resource | event | field | statistic | value | unit |",
            "| --- | --- | --- | --- | --- | --- | ---: | --- |",
        ]
    )
    for suite in report["suites"]:
        for artifact in suite["artifacts"]:
            artifact_name = Path(artifact["path"]).name
            for row in artifact["resources"]:
                has_resource_rows = True
                lines.append(
                    f"| {suite['name']} | {artifact_name} | {row['resource']} | "
                    f"{row.get('event', '-')} | {row['field']} | {row.get('statistic', '-')} | "
                    f"{fmt_number(row['value'])} | {row['unit']} |"
                )
    if not has_resource_rows:
        lines.append("| - | - | - | - | - | - | - | - |")
    lines.append("")

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def load_baseline(path: Path | None) -> dict[str, Any] | None:
    if path is None:
        return None
    candidate = path.expanduser().resolve()
    artifact_dir = candidate if candidate.is_dir() else candidate.parent
    report_path: Path | None = None
    if candidate.is_dir():
        manifest_path = candidate / "manifest.json"
        report_path = candidate / "operability-report.json"
    elif candidate.name == "operability-report.json":
        manifest_path = candidate.parent / "manifest.json"
        report_path = candidate
    else:
        manifest_path = candidate
    manifest = load_json(manifest_path) or {}
    if report_path is None:
        manifest_report = manifest.get("report_json") if isinstance(manifest, dict) else None
        if isinstance(manifest_report, str) and manifest_report:
            report_path = Path(manifest_report).expanduser()
            if not report_path.is_absolute():
                report_path = artifact_dir / report_path
        else:
            report_path = artifact_dir / "operability-report.json"
    report = load_json(report_path) or {}
    return {
        "path": str(candidate),
        "manifest": manifest,
        "report": report,
    }


def main() -> int:
    args = parse_args()
    if args.list_suites:
        print_suite_listing()
        return 0
    if args.iterations <= 0:
        raise SystemExit("--iterations must be positive")
    if args.timeout <= 0:
        raise SystemExit("--timeout must be positive")

    selected_suites = suite_names_for_args(args)
    if selected_suites_need_corpus(selected_suites) and not args.dry_run:
        ensure_synthetic_corpus(args.corpus)
    session_id = uuid.uuid4().hex[:8]
    artifact_dir = (
        args.output_root.expanduser().resolve()
        / f"app-suite-{timestamp_slug()}-{args.profile}-{session_id}"
    )
    suite_output_root = artifact_dir / "benchmarks"
    log_dir = artifact_dir / "logs"
    artifact_dir.mkdir(parents=True, exist_ok=False)
    suite_output_root.mkdir(parents=True, exist_ok=True)
    log_dir.mkdir(parents=True, exist_ok=True)

    results: list[SuiteResult] = []
    status = "passed"
    started_at = iso_now()
    for suite_name in selected_suites:
        suite = SUITE_DEFINITIONS[suite_name]
        command = suite.builder(args, suite_output_root)
        log_file = log_dir / f"{suite.name}.log"
        result = SuiteResult(
            name=suite.name,
            description=suite.description,
            command=command,
            returncode=None,
            log_file=str(log_file),
            artifact_dirs=[],
            status="dry-run" if args.dry_run else "pending",
            started_at=iso_now(),
        )
        if args.dry_run:
            log_file.write_text("+ " + " ".join(command) + "\n", encoding="utf-8")
            result.ended_at = iso_now()
            results.append(result)
            continue

        returncode, output = run_command(command, log_file)
        result.returncode = returncode
        result.artifact_dirs = parse_artifact_dirs(output)
        result.status = status_for_output(returncode, output)
        result.ended_at = iso_now()
        results.append(result)
        if returncode != 0:
            status = "failed"

    ended_at = iso_now()
    if args.dry_run:
        status = "dry-run"
    elif any(result.status == "failed" for result in results):
        status = "failed"

    baseline = load_baseline(args.baseline)
    report = build_report_payload(results, baseline, args.profile)
    threshold_violations = threshold_violation_count(report)
    if threshold_violations > 0 and status != "dry-run":
        status = "failed"

    manifest = {
        "profile": args.profile,
        "selected_suites": selected_suites,
        "status": status,
        "started_at": started_at,
        "ended_at": ended_at,
        "artifact_dir": str(artifact_dir),
        "suite_output_root": str(suite_output_root),
        "baseline": str(args.baseline.expanduser().resolve()) if args.baseline else None,
        "corpus": str(args.corpus.expanduser().resolve()),
        "dry_run": args.dry_run,
        "threshold_profile": args.profile,
        "threshold_violation_count": threshold_violations,
        "suites": [result.__dict__ for result in results],
    }
    manifest["report_json"] = str(artifact_dir / "operability-report.json")
    manifest["report_markdown"] = str(artifact_dir / "operability-report.md")
    manifest["metrics_json"] = str(artifact_dir / "metrics.json")

    write_json(artifact_dir / "manifest.json", manifest)
    write_json(artifact_dir / "operability-report.json", report)
    write_json(artifact_dir / "metrics.json", build_metrics_payload(report))
    write_markdown_report(artifact_dir / "operability-report.md", manifest, report)

    print(f"artifact_dir={artifact_dir}")
    print(f"report={artifact_dir / 'operability-report.md'}")
    print(f"status={status}")
    return 0 if status in {"passed", "dry-run"} else 1


if __name__ == "__main__":
    raise SystemExit(main())
