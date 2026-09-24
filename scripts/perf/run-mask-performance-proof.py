#!/usr/bin/env python3
"""Run the focused masking performance proof checks."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BUILD_DIR = REPO_ROOT / "build/dev-macos-debug"
DEFAULT_OUTPUT_ROOT = REPO_ROOT / "perf-artifacts"
REQUIRED_TESTS = (
    "editor_preview_controller_test",
)
DESKTOP_SMOKE_TESTS = (
    "desktop_shell_brush_workflow_test",
    "desktop_shell_retouch_hud_test",
    "desktop_shell_selection_geometry_test",
    "desktop_shell_tool_exit_test",
    "desktop_shell_layer_hover_test",
)
REQUIRED_SOURCE_MARKERS = {
    "canvas_raster_preview_disabled": {
        "path": "apps/desktop/qml/workspaces/MaskCanvasOverlay.qml",
        "present": [
            'maskRasterPreviewPolicy: "editor-canvas-raster-mask-disabled|clean-exterior-vector-outline:v2"',
            "function cleanVectorMaskFillVisible()",
            'source: ""',
        ],
        "absent": ["activeMaskPreviewSource"],
    },
    "shared_raster_preview_disabled": {
        "path": "apps/desktop/qml/workspaces/SharedMaskOverlay.qml",
        "present": [
            'maskRasterPreviewPolicy: "editor-canvas-raster-mask-disabled|clean-exterior-vector-outline:v2"',
            'readonly property string previewSource: ""',
        ],
        "absent": ["activeMaskPreviewSource"],
    },
    "settled_outline_only_overlay": {
        "path": "apps/desktop/qml/workspaces/MaskCanvasOverlay.qml",
        "present": [
            'settledMaskOutlineContract: "outline-only|fragment-filtered|no-fill:v1"',
            "root.strokeOutlinePaths(ctx, outlinePaths, root.resolvedMaskOverlayColor(), false)",
        ],
        "absent": ["root.fillOutlinePaths(ctx, outlinePaths, overlayAccent, 0.18)"],
    },
    "shared_outline_only_overlay": {
        "path": "apps/desktop/qml/workspaces/SharedMaskOverlay.qml",
        "present": [
            'settledMaskOutlineContract: "outline-only|fragment-filtered|no-fill:v1"',
            "root.strokeOutlinePaths(ctx, outlinePaths)",
        ],
        "absent": ["root.fillOutlinePaths(ctx, outlinePaths)"],
    },
    "source_free_preview_function": {
        "path": "apps/desktop/src/editor_preview_controller.cpp",
        "present": [
            "QString EditorPreviewController::activeMaskPreviewSource() const",
            "if (!mask_preview_mode_uses_raster_matte(preview_mode))",
            "++active_mask_preview_source_skip_count_",
            "return {};",
        ],
    },
    "fast_brush_outline": {
        "path": "apps/desktop/src/editor_preview_controller.cpp",
        "present": [
            "QVariantList fast_outline_paths_for_brush_mask",
            "outline_path_for_brush_samples",
            "if (kind == QStringLiteral(\"brush\") && !mask_selection_refinement_active(mask))",
        ],
    },
    "refinement_outline_gate": {
        "path": "apps/desktop/src/editor_preview_controller.cpp",
        "present": [
            "preview_mode != MaskPreviewMode::RefinementEdge",
            "clean_canvas_outline_paths_for_mask(mask)",
            "mask_selection_refinement_active(mask)",
        ],
    },
    "qml_outline_cache": {
        "path": "apps/desktop/qml/workspaces/MaskCanvasOverlay.qml",
        "present": [
            "readonly property var normalizedOutlinePathsCache",
            "const outlinePaths = activeSelectionOutline.normalizedOutlinePathsCache",
        ],
    },
    "shared_qml_outline_cache": {
        "path": "apps/desktop/qml/workspaces/SharedMaskOverlay.qml",
        "present": [
            "readonly property var normalizedOutlinePathsCache",
            "const outlinePaths = activeSelectionOutline.normalizedOutlinePathsCache",
        ],
    },
    "live_brush_independent": {
        "path": "apps/desktop/qml/workspaces/SharedMaskOverlay.qml",
        "present": [
            "readonly property string liveBrushPreviewSource",
            "BrushLivePreviewItem",
            "vectorLiveBrushPreviewVisible",
        ],
    },
    "pending_brush_preview_points": {
        "path": "apps/desktop/src/editor_preview_controller.cpp",
        "present": [
            "pending_brush_commit_preview_points_",
            "pending_brush_commit_preview_mask_id_",
        ],
    },
    "brush_commit_qtconcurrent": {
        "path": "apps/desktop/src/editor_preview_controller.cpp",
        "present": [
            "brush_raster_commit_watcher_.setFuture",
            "QtConcurrent::run([request]() { return compute_brush_raster_commit_result(request); })",
            "commit_deferred",
        ],
    },
    "smart_commit_qtconcurrent": {
        "path": "apps/desktop/src/editor_preview_controller.cpp",
        "present": [
            "smart_select_commit_watcher_.setFuture",
            "QtConcurrent::run([request]() { return compute_smart_select_commit_result(request); })",
        ],
    },
    "active_outline_cache": {
        "path": "apps/desktop/src/editor_preview_controller.cpp",
        "present": [
            "active_mask_outline_cache_key_",
            "clean-outline",
            "active_mask_outline_fast_key_count_",
        ],
    },
    "skip_matte_telemetry": {
        "path": "apps/desktop/src/editor_preview_controller.h",
        "present": ["active_mask_preview_source_skip_count_"],
    },
    "canvas_repaint_coalescing": {
        "path": "apps/desktop/qml/workspaces/MaskCanvasOverlay.qml",
        "present": [
            "pendingCanvasPaints",
            "canvasPaintTimer",
            "if (pendingCanvasPaints.indexOf(canvas) >= 0)",
        ],
    },
    "marching_ants_budget": {
        "path": "apps/desktop/qml/workspaces/StudioWorkspace.qml",
        "present": [
            "selectionOutlineAnimationPointBudget",
            "selectionOutlineAnimationShouldRun",
            "pointCount >= 2 && pointCount <= root.selectionOutlineAnimationPointBudget",
        ],
    },
    "mask_outline_point_budget": {
        "path": "apps/desktop/src/editor_preview_controller.cpp",
        "present": [
            "kMaxTracedOutlinePaths",
            "kMaxTracedOutlinePoints",
            "bounded_closed_outline_points",
        ],
    },
    "tiny_contour_filter": {
        "path": "apps/desktop/src/editor_preview_controller.cpp",
        "present": [
            "kMinSecondaryOutlineAreaRatio",
            "kMinSecondaryOutlineAreaPixels",
            "minimum_secondary_area",
        ],
    },
    "cursor_separate_from_canvas": {
        "path": "apps/desktop/qml/workspaces/MaskCanvasOverlay.qml",
        "present": [
            'brushCursorRepaintContract: "cursor-item-position-binding|no-static-canvas-repaint-on-hover:v1"',
            "brushCursorPositionKnown",
        ],
    },
    "mask_sets_metadata_cache": {
        "path": "apps/desktop/src/editor_preview_controller.cpp",
        "present": [
            "mask_group_cache_rebuild_count_",
            "mask_thumbnail_cache_dirty_",
            "maskSets() const",
        ],
    },
    "thumbnail_defer": {
        "path": "apps/desktop/src/editor_preview_controller.cpp",
        "present": [
            "mask_thumbnail_refresh_deferred_count_",
            "brushStrokeActive()",
            "mask_thumbnail_cache_dirty_",
        ],
    },
    "active_preview_lru": {
        "path": "apps/desktop/src/editor_preview_controller.cpp",
        "present": [
            "active_mask_preview_lru_cache_",
            "active_mask_preview_lru_order_",
            "kMaxActiveMaskPreviewLruEntries",
        ],
    },
    "scaled_matte_cache": {
        "path": "apps/desktop/src/editor_preview_controller.cpp",
        "present": [
            "ScaledMaskMatteCache",
            "scaled_mask_matte_cache_key",
            "scaledMattePurpose",
        ],
    },
    "skip_noop_resampling": {
        "path": "apps/desktop/src/editor_preview_controller.cpp",
        "present": [
            "source_width == target_width",
            "source_height == target_height",
            "scaledMattePurpose",
        ],
    },
    "overlay_only_no_full_res": {
        "path": "tests/editor_preview_controller_test.cpp",
        "present": [
            "schedule_full_resolution",
            "mask creation should stay source-free in normal overlay mode without queueing a full-resolution preview render",
        ],
    },
    "brush_commit_coalescing": {
        "path": "apps/desktop/src/editor_preview_controller.cpp",
        "present": [
            "can_merge_commit",
            "coalesced_stroke_count",
            "while (!pending_brush_commits_.isEmpty()",
        ],
    },
    "brush_stale_replay": {
        "path": "apps/desktop/src/editor_preview_controller.cpp",
        "present": [
            "stale_brush_raster_commit",
            "pending_brush_commits_.prepend",
        ],
    },
    "source_free_manual_brush": {
        "path": "tests/editor_preview_controller_test.cpp",
        "present": [
            "source-free manual brush rasterization",
            "source_decoded",
        ],
    },
    "smart_hover_source_free": {
        "path": "tests/editor_preview_controller_test.cpp",
        "present": [
            "smart select should expose a source-free clean realtime hover outline before committing the selection",
            "smartSelectPreviewSource().isEmpty()",
        ],
    },
    "smart_raster_disabled": {
        "path": "apps/desktop/qml/workspaces/MaskCanvasOverlay.qml",
        "present": [
            'smartPreviewRasterContract: "disabled|outline-only-candidate|no-speckled-matte:v1"',
            "id: smartPreviewMatte",
            'source: ""',
        ],
    },
    "smart_outline_cache": {
        "path": "apps/desktop/src/editor_preview_controller.cpp",
        "present": [
            "smart_select_preview_outline_cache_valid_",
            "smart_select_preview_outline_cache_hit_count_",
            "smart_select_preview_outline_cache_miss_count_",
        ],
    },
    "smart_hover_prewarm": {
        "path": "apps/desktop/qml/workspaces/MaskCanvasOverlay.qml",
        "present": [
            'smartSelectHoverPrewarmContract: "pointer-pause|jitter-gated|no-enter-prewarm:v1"',
            "smartSelectPreviewPauseMs: 180",
            "smartSelectPreviewPauseTimer.restart()",
        ],
    },
    "bounded_auto_mask_decode": {
        "path": "apps/desktop/src/editor_preview_controller.cpp",
        "present": [
            "source_decode_bounded",
            "source_decode_edge",
            "request.auto_mask",
        ],
    },
    "auto_mask_edge_short_circuit": {
        "path": "apps/desktop/src/editor_preview_controller.cpp",
        "present": [
            "auto_mask_candidate_pixels",
            "color_gate <= 1e-6",
            "auto_mask_edge_evaluation_pixels",
        ],
    },
    "normal_overlay_smoke_assertion": {
        "path": "tests/desktop_shell_smoke_test.cpp",
        "present": [
            "activeMaskPreviewSource().isEmpty()",
            "maskRasterPreviewPolicy",
        ],
    },
    "proof_script": {
        "path": "scripts/perf/run-mask-performance-proof.py",
        "present": [
            "mask_performance_proof_gate",
            "REQUIRED_TESTS",
        ],
    },
}

ITEM_EVIDENCE_RULES = (
    ("Stop normal `overlay` mode from drawing", ("canvas_raster_preview_disabled", "source_free_preview_function")),
    ("Stop `edge`, `refinement-edge`, and `marching-ants`", ("source_free_preview_function", "normal_overlay_smoke_assertion")),
    ("Remove settled raster matte image display", ("canvas_raster_preview_disabled", "settled_outline_only_overlay")),
    ("Avoid reading `activeMaskPreviewSource` from `SharedMaskOverlay`", ("shared_raster_preview_disabled",)),
    ("Avoid binding hidden `Image.source` to `activeMaskPreviewSource` in `SharedMaskOverlay`", ("shared_raster_preview_disabled",)),
    ("Avoid binding hidden `Image.source` to `activeMaskPreviewSource` in `MaskCanvasOverlay`", ("canvas_raster_preview_disabled",)),
    ("Preserve clean canvas overlays while skipping raster matte image generation", ("source_free_preview_function", "settled_outline_only_overlay")),
    ("Make `activeMaskPreviewSource()` return empty", ("source_free_preview_function",)),
    ("Use fast brush-stroke sample outlines", ("fast_brush_outline",)),
    ("Keep expensive composite/refinement outline generation only", ("refinement_outline_gate",)),
    ("Reuse one QML outline-path binding", ("qml_outline_cache", "shared_qml_outline_cache")),
    ("Keep live brush visual feedback independent", ("live_brush_independent",)),
    ("Keep pending brush commit points visible", ("pending_brush_preview_points",)),
    ("Move brush raster commit work off the UI thread", ("brush_commit_qtconcurrent",)),
    ("Move smart selection commit work off the UI thread", ("smart_commit_qtconcurrent",)),
    ("Add controller-level caching for fast outline paths", ("active_outline_cache",)),
    ("Add telemetry counters for skipped matte-source generation", ("skip_matte_telemetry", "source_free_preview_function")),
    ("Coalesce repeated `maskOverlayChanged` canvas repaints", ("canvas_repaint_coalescing",)),
    ("Throttle marching-ants animation", ("marching_ants_budget",)),
    ("Disable marching-ants animation automatically", ("marching_ants_budget",)),
    ("Cache normalized outline paths in QML", ("qml_outline_cache", "shared_qml_outline_cache")),
    ("Add a maximum point/path budget", ("mask_outline_point_budget",)),
    ("Drop tiny secondary traced raster contours", ("tiny_contour_filter",)),
    ("Split brush cursor repaint", ("cursor_separate_from_canvas",)),
    ("Avoid clearing and redrawing full overlay canvases", ("cursor_separate_from_canvas",)),
    ("Cache grouped mask-set metadata", ("mask_sets_metadata_cache",)),
    ("Avoid building mask thumbnails during interactive brush strokes", ("thumbnail_defer",)),
    ("Defer mask thumbnail refresh", ("thumbnail_defer",)),
    ("Remove noisy affected-pixel raster mattes", ("source_free_preview_function", "settled_outline_only_overlay")),
    ("Add a small LRU cache", ("active_preview_lru",)),
    ("Cache low-resolution scaled mask mattes", ("scaled_matte_cache",)),
    ("Skip no-op matte resampling", ("skip_noop_resampling",)),
    ("Prove brush-stroke mask preview edits do not request full-resolution", ("overlay_only_no_full_res",)),
    ("Coalesce queued brush strokes", ("brush_commit_coalescing",)),
    ("Preserve separate brush stroke history entries", ("brush_commit_coalescing",)),
    ("Cancel and replay stale in-flight brush raster jobs", ("brush_stale_replay",)),
    ("Skip source image decode for source-free manual brush commits", ("source_free_manual_brush",)),
    ("Keep smart-selection hover previews source-free", ("smart_hover_source_free",)),
    ("Draw clean vector fill/outline overlays instead of noisy smart-selection raster", ("smart_raster_disabled", "smart_hover_source_free")),
    ("Cache smart-selection hover outline extraction", ("smart_outline_cache",)),
    ("Prewarm smart selection instance maps on hover", ("smart_hover_prewarm",)),
    ("Bound auto-mask brush commit source decodes", ("bounded_auto_mask_decode",)),
    ("Use a lightweight active-outline cache key", ("active_outline_cache",)),
    ("Skip auto-mask edge-barrier evaluation", ("auto_mask_edge_short_circuit",)),
    ("Add a smoke assertion that normal overlay mode", ("normal_overlay_smoke_assertion",)),
    ("Generate mask thumbnails from the low-resolution matte cache", ("scaled_matte_cache", "thumbnail_defer")),
    ("Skip full-resolution preview scheduling", ("overlay_only_no_full_res",)),
    ("Merge pending brush commits", ("brush_commit_coalescing",)),
    ("Make smart-selection hover previews use clean vector", ("smart_raster_disabled", "smart_hover_source_free")),
    ("Keep active clean-outline cache hits", ("active_outline_cache",)),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run the focused proof for masking performance and UX rendering work. "
            "The gate verifies that the masking checklist has 50 implemented items, "
            "source markers are present, and the masking smoke/controller tests pass."
        )
    )
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR)
    parser.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument(
        "--report",
        type=Path,
        help="Write a JSON proof report. Defaults under --output-root.",
    )
    parser.add_argument("--ctest-timeout", type=int, default=360)
    parser.add_argument(
        "--include-desktop-smoke",
        action="store_true",
        help=(
            "Also run the full desktop shell smoke test. The default mask proof uses "
            "focused source checks plus editor_preview_controller_test because the "
            "desktop smoke covers unrelated UI workflows and can exceed this proof's "
            "interactive performance budget."
        ),
    )
    parser.add_argument(
        "--skip-ctest",
        action="store_true",
        help="Only validate source/checklist contracts. Intended for script self-tests.",
    )
    return parser.parse_args()


def default_report_path(output_root: Path) -> Path:
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    return output_root.expanduser().resolve() / f"mask-performance-proof-{timestamp}.json"


def checklist_items(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8")
    match = re.search(r"^## Implemented\s*(.*?)(?:^## |\Z)", text, re.M | re.S)
    if not match:
        raise SystemExit("Unable to find ## Implemented section in masking performance checklist")
    return [
        line.strip()[6:].strip()
        for line in match.group(1).splitlines()
        if line.strip().startswith("- [x]")
    ]


def source_marker_results(markers: dict[str, dict[str, Any]]) -> dict[str, dict[str, Any]]:
    results: dict[str, dict[str, Any]] = {}
    for key, spec in markers.items():
        relative_path = str(spec["path"])
        path = REPO_ROOT / relative_path
        text = path.read_text(encoding="utf-8") if path.is_file() else ""
        present_checks = [
            {"needle": needle, "found": needle in text}
            for needle in spec.get("present", ())
        ]
        absent_checks = [
            {"needle": needle, "absent": needle not in text}
            for needle in spec.get("absent", ())
        ]
        passed = path.is_file() and all(check["found"] for check in present_checks) and all(
            check["absent"] for check in absent_checks
        )
        results[key] = {
            "path": relative_path,
            "present": present_checks,
            "absent": absent_checks,
            "passed": passed,
        }
    return results


def item_evidence_results(
    items: list[str], marker_results: dict[str, dict[str, Any]]
) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []
    for item in items:
        evidence_keys: tuple[str, ...] | None = None
        for prefix, keys in ITEM_EVIDENCE_RULES:
            if item.startswith(prefix):
                evidence_keys = keys
                break
        if evidence_keys is None:
            results.append(
                {
                    "item": item,
                    "evidence_keys": [],
                    "passed": False,
                    "missing_reason": "unmapped checklist item",
                }
            )
            continue

        missing_keys = [key for key in evidence_keys if key not in marker_results]
        failing_keys = [
            key
            for key in evidence_keys
            if key in marker_results and not marker_results[key]["passed"]
        ]
        missing_reason = ""
        if missing_keys:
            missing_reason = "missing evidence keys: " + ", ".join(missing_keys)
        elif failing_keys:
            missing_reason = "failing evidence keys: " + ", ".join(failing_keys)

        results.append(
            {
                "item": item,
                "evidence_keys": list(evidence_keys),
                "passed": not missing_keys and not failing_keys,
                "missing_reason": missing_reason,
            }
        )
    return results


def run_command(command: list[str], *, timeout: int) -> subprocess.CompletedProcess[str]:
    print("+ " + " ".join(command), flush=True)
    result = subprocess.run(
        command,
        cwd=REPO_ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
    )
    if result.stdout:
        print(result.stdout, end="" if result.stdout.endswith("\n") else "\n")
    return result


def ctest_exists(build_dir: Path, test_name: str) -> bool:
    result = run_command(
        ["ctest", "--test-dir", str(build_dir), "-N", "-R", f"^{test_name}$"],
        timeout=60,
    )
    return result.returncode == 0 and test_name in result.stdout


def run_ctest(build_dir: Path, test_name: str, timeout: int) -> dict[str, Any]:
    if not ctest_exists(build_dir, test_name):
        return {
            "command": ["ctest", "--test-dir", str(build_dir), "-R", f"^{test_name}$"],
            "exit_code": 2,
            "passed": False,
            "output": f"required test missing: {test_name}",
        }
    command = [
        "ctest",
        "--test-dir",
        str(build_dir),
        "-R",
        f"^{test_name}$",
        "--output-on-failure",
    ]
    result = run_command(command, timeout=timeout)
    return {
        "command": command,
        "exit_code": result.returncode,
        "passed": result.returncode == 0
        and "100% tests passed" in result.stdout
        and "Skipped" not in result.stdout,
        "output": result.stdout,
    }


def proof_status(
    *,
    implemented_count: int,
    marker_results: dict[str, dict[str, Any]],
    item_results: list[dict[str, Any]],
    test_results: dict[str, dict[str, Any]],
    skip_ctest: bool,
) -> str:
    markers_passed = all(result["passed"] for result in marker_results.values())
    items_passed = implemented_count >= 50 and all(result["passed"] for result in item_results)
    tests_passed = skip_ctest or all(result["passed"] for result in test_results.values())
    if markers_passed and items_passed and tests_passed:
        return "passed" if not skip_ctest else "source_only_passed"
    return "failed"


def write_report(path: Path, report: dict[str, Any]) -> None:
    resolved = path.expanduser().resolve()
    resolved.parent.mkdir(parents=True, exist_ok=True)
    resolved.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"mask_performance_proof_report={resolved}")


def main() -> int:
    args = parse_args()
    build_dir = args.build_dir.expanduser().resolve()
    checklist_path = REPO_ROOT / "docs/masking-performance-improvements.md"
    implemented = checklist_items(checklist_path)
    markers = source_marker_results(REQUIRED_SOURCE_MARKERS)
    item_results = item_evidence_results(implemented, markers)

    tests: dict[str, dict[str, Any]] = {}
    if not args.skip_ctest:
        if not build_dir.is_dir():
            raise SystemExit(f"build directory does not exist: {build_dir}")
        required_tests = list(REQUIRED_TESTS)
        if args.include_desktop_smoke:
            required_tests.extend(DESKTOP_SMOKE_TESTS)
        for test_name in required_tests:
            tests[test_name] = run_ctest(build_dir, test_name, args.ctest_timeout)

    status = proof_status(
        implemented_count=len(implemented),
        marker_results=markers,
        item_results=item_results,
        test_results=tests,
        skip_ctest=args.skip_ctest,
    )
    report = {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "gate": status,
        "implemented_count": len(implemented),
        "implemented_items": implemented,
        "item_evidence": item_results,
        "failed_items": [result for result in item_results if not result["passed"]],
        "source_markers": markers,
        "tests": tests,
        "inputs": {
            "build_dir": str(build_dir),
            "checklist": str(checklist_path),
            "include_desktop_smoke": args.include_desktop_smoke,
            "skip_ctest": args.skip_ctest,
        },
    }
    write_report(args.report or default_report_path(args.output_root), report)
    print(f"mask_performance_proof_gate={status}")
    print(f"mask_performance_proof_implemented_count={len(implemented)}")
    return 0 if status in {"passed", "source_only_passed"} else 1


if __name__ == "__main__":
    raise SystemExit(main())
