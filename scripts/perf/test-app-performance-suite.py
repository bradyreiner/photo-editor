#!/usr/bin/env python3
"""Focused tests for the app-wide performance suite wrapper."""

from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPO_ROOT / "scripts/perf/run-app-performance-suite.py"
GOAL8_SUITES = [
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
]


def load_module():
    spec = importlib.util.spec_from_file_location("run_app_performance_suite", SCRIPT_PATH)
    assert spec is not None
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def assert_true(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


def test_profiles_and_dry_run() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        result = subprocess.run(
            [
                sys.executable,
                "-B",
                str(SCRIPT_PATH),
                "--profile",
                "smoke",
                "--output-root",
                temp_dir,
                "--dry-run",
            ],
            cwd=REPO_ROOT,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        assert_true(result.returncode == 0, result.stdout)
        artifact_line = next(
            line for line in result.stdout.splitlines() if line.startswith("artifact_dir=")
        )
        artifact = Path(artifact_line.partition("=")[2])
        manifest = json.loads((artifact / "manifest.json").read_text(encoding="utf-8"))
        assert_true(manifest["status"] == "dry-run", "dry-run status was not recorded")
        assert_true(
            manifest["selected_suites"]
            == ["startup", "preview-raster", "thumbnail-cold-warm"],
            "smoke profile selected the wrong suites",
        )
        assert_true((artifact / "operability-report.md").is_file(), "missing markdown report")
        assert_true((artifact / "operability-report.json").is_file(), "missing JSON report")
        assert_true((artifact / "metrics.json").is_file(), "missing metrics report")
        metrics = json.loads((artifact / "metrics.json").read_text(encoding="utf-8"))
        assert_true(
            metrics["schema"] == "photo-editor-app-performance-metrics-v1",
            "metrics schema was not recorded",
        )


def test_goal8_suites_can_be_selected_and_dry_run() -> None:
    module = load_module()
    missing = [name for name in GOAL8_SUITES if name not in module.SUITE_DEFINITIONS]
    assert_true(not missing, f"missing app-suite definitions: {missing}")
    for profile_name in ("local-dev", "release-proof"):
        profile_suites = set(module.PROFILES[profile_name])
        missing_from_profile = [name for name in GOAL8_SUITES if name not in profile_suites]
        assert_true(
            not missing_from_profile,
            f"{profile_name} profile missing Goal 8 suites: {missing_from_profile}",
        )
    assert_true("stress" in module.PROFILES, "missing optional stress profile")
    for profile_name in ("smoke", "local-dev", "release-proof", "stress"):
        threshold_metrics = {threshold.metric for threshold in module.PERFORMANCE_THRESHOLDS[profile_name]}
        for metric in (
            "preview_render_p95_ms",
            "mask_brush_feedback_p95_ms",
            "thumbnail_queue_wait_p95_ms",
            "workspace_route_ready_p95_ms",
            "render_cancellation_latency_p95_ms",
            "memory_growth_delta_kb",
            "worker_queue_wait_p95_ms",
            "qml_jank_frames_max",
        ):
            assert_true(metric in threshold_metrics, f"{profile_name} missing threshold {metric}")

    listing = subprocess.run(
        [sys.executable, "-B", str(SCRIPT_PATH), "--list-suites"],
        cwd=REPO_ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    assert_true(listing.returncode == 0, listing.stdout)
    for suite_name in GOAL8_SUITES:
        assert_true(suite_name in listing.stdout, f"{suite_name} missing from suite listing")

    with tempfile.TemporaryDirectory() as temp_dir:
        command = [
            sys.executable,
            "-B",
            str(SCRIPT_PATH),
            "--profile",
            "smoke",
            "--output-root",
            temp_dir,
            "--dry-run",
        ]
        for suite_name in GOAL8_SUITES:
            command.extend(["--suite", suite_name])
        result = subprocess.run(
            command,
            cwd=REPO_ROOT,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        assert_true(result.returncode == 0, result.stdout)
        artifact_line = next(
            line for line in result.stdout.splitlines() if line.startswith("artifact_dir=")
        )
        artifact = Path(artifact_line.partition("=")[2])
        manifest = json.loads((artifact / "manifest.json").read_text(encoding="utf-8"))
        assert_true(
            manifest["selected_suites"] == GOAL8_SUITES,
            "Goal 8 suites were not selected in requested order",
        )
        suite_names = [suite["name"] for suite in manifest["suites"]]
        assert_true(suite_names == GOAL8_SUITES, "Goal 8 suite results were not created")


def test_goal8_suite_command_mappings() -> None:
    module = load_module()
    args = SimpleNamespace(
        actual_app_workflow_app=Path("/tmp/actual-app"),
        color_app=Path("/tmp/color-app"),
        corpus=Path("/tmp/corpus/manifest.json"),
        interaction_app=Path("/tmp/interaction-app"),
        iterations=3,
        preview_app=Path("/tmp/preview-app"),
        timeout=7,
    )
    output_root = Path("/tmp/perf-suite")

    global_command = module.SUITE_DEFINITIONS["perf-edit-global-sliders"].builder(args, output_root)
    assert_true("--scenario" in global_command, "global sliders command missing scenario flag")
    assert_true("perf-edit-global-sliders" in global_command, "global sliders scenario missing")
    assert_true(
        "benchmark.preview_global_burst.completed" in global_command,
        "global sliders command should wait for global burst completion",
    )
    assert_true("--global-burst" in global_command, "global sliders command missing driver flag")

    color_command = module.SUITE_DEFINITIONS["perf-edit-color-tools"].builder(args, output_root)
    assert_true("perf-edit-color-tools" in color_command, "color tools scenario missing")
    assert_true(str(args.color_app) in color_command, "color tools should use color benchmark app")

    lasso_command = module.SUITE_DEFINITIONS["perf-mask-lasso"].builder(args, output_root)
    assert_true("perf-mask-lasso" in lasso_command, "mask lasso scenario missing")
    assert_true(str(args.actual_app_workflow_app) in lasso_command, "mask lasso should use actual app workflow")
    assert_true(
        "benchmark.actual_app_workflow.completed" in lasso_command,
        "mask lasso command should wait for actual app workflow completion",
    )

    workspace_command = module.SUITE_DEFINITIONS["perf-workspace-switch"].builder(args, output_root)
    assert_true("perf-workspace-switch" in workspace_command, "workspace switch scenario missing")
    assert_true(
        "benchmark.actual_app_workflow.completed" in workspace_command,
        "workspace switch command should wait for actual app workflow completion",
    )


def test_threshold_evaluator_reports_failures_and_missing_metrics() -> None:
    module = load_module()
    artifact = {
        "timings": [
            {
                "event": "preview.render.completed",
                "field": "elapsed_ms",
                "p95": 999,
            }
        ],
        "mask_performance": [],
        "worker_pressure": [],
        "qml_frames": [],
        "memory_growth": [],
        "cache_pressure": [],
        "cancellations": [],
    }
    rows = module.threshold_rows_for_artifact(artifact, "local-dev")
    preview_row = next(row for row in rows if row["metric"] == "preview_render_p95_ms")
    assert_true(preview_row["status"] == "failed", "over-limit preview p95 should fail")
    missing_row = next(row for row in rows if row["metric"] == "mask_brush_feedback_p95_ms")
    assert_true(missing_row["status"] == "missing", "unmeasured thresholds should be explicit")
    report = {"suites": [{"artifacts": [{"thresholds": rows}]}]}
    assert_true(module.threshold_violation_count(report) == 1, "failed threshold should be counted")


def test_skipped_suite_cannot_be_reported_as_a_release_pass() -> None:
    module = load_module()
    skipped = [SimpleNamespace(status="skipped")]
    passed = [SimpleNamespace(status="passed")]
    failed = [SimpleNamespace(status="failed")]
    assert_true(
        module.overall_status_for_results(skipped, "smoke", dry_run=False) == "skipped",
        "an explained local skip must remain visible in aggregate status",
    )
    assert_true(
        module.overall_status_for_results(skipped, "release-proof", dry_run=False)
        == "incomplete",
        "a release-proof skip must make the aggregate incomplete",
    )
    assert_true(
        module.overall_status_for_results(passed, "release-proof", dry_run=False) == "passed",
        "fully passing release proof should pass",
    )
    assert_true(
        module.overall_status_for_results(failed, "smoke", dry_run=False) == "failed",
        "a failed suite must fail every aggregate profile",
    )


def test_artifact_parsing_and_report_payload() -> None:
    module = load_module()
    output = "\n".join(
        [
            "artifact_dir=/tmp/photo-editor/a",
            "one_tile_artifact=/tmp/photo-editor/b",
            "ignored=value",
            "two-by-two_artifact=/tmp/photo-editor/b",
        ]
    )
    expected = [
        str(Path("/tmp/photo-editor/a").resolve()),
        str(Path("/tmp/photo-editor/b").resolve()),
    ]
    assert_true(module.parse_artifact_dirs(output) == expected, "artifact parser did not dedupe expected lines")

    with tempfile.TemporaryDirectory() as temp_dir:
        artifact = Path(temp_dir) / "bench"
        artifact.mkdir()
        (artifact / "manifest.json").write_text(
            json.dumps(
                {
                    "scenario": "demo",
                    "rss_start_kb": 10000,
                    "rss_peak_kb": 12345,
                    "rss_end_kb": 11000,
                    "rss_delta_kb": 1000,
                }
            )
            + "\n",
            encoding="utf-8",
        )
        (artifact / "summary.json").write_text(
            json.dumps(
                {
                    "events": {
                        "preview.render.completed": {
                            "count": 2,
                            "severities": {"info": 2},
                            "timings": {
                                "elapsed_ms": {
                                    "count": 2,
                                    "p50": 10,
                                    "p95": 20,
                                    "max": 25,
                                }
                            },
                            "categories": {
                                "backend": {"cpu": 1, "metal": 1},
                                "decode_backend": {"qt": 2},
                                "fallback_reason": {"metal_unavailable": 1},
                            },
                            "numeric_fields": {
                                "cache_bytes": {
                                    "count": 2,
                                    "p50": 1600,
                                    "p95": 1900,
                                    "max": 2000,
                                    "avg": 1800,
                                },
                                "decoded_cache_bytes": {
                                    "count": 2,
                                    "p50": 100,
                                    "p95": 190,
                                    "max": 200,
                                    "avg": 150,
                                },
                                "linear_cache_bytes": {
                                    "count": 2,
                                    "p50": 300,
                                    "p95": 390,
                                    "max": 400,
                                    "avg": 350,
                                },
                                "adjusted_cache_bytes": {
                                    "count": 2,
                                    "p50": 500,
                                    "p95": 590,
                                    "max": 600,
                                    "avg": 550,
                                },
                                "rendered_cache_bytes": {
                                    "count": 2,
                                    "p50": 700,
                                    "p95": 790,
                                    "max": 800,
                                    "avg": 750,
                                },
                                "cache_hit_rate": {
                                    "count": 2,
                                    "p50": 0.25,
                                    "p95": 0.475,
                                    "max": 0.5,
                                    "avg": 0.375,
                                },
                            },
                        },
                        "thumbnail.completed": {
                            "count": 2,
                            "severities": {"info": 2},
                            "timings": {
                                "elapsed_ms": {
                                    "count": 2,
                                    "p50": 8,
                                    "p95": 14,
                                    "max": 16,
                                },
                                "queue_wait_ms": {
                                    "count": 2,
                                    "p50": 3,
                                    "p95": 6,
                                    "max": 7,
                                },
                            },
                            "numeric_fields": {
                                "active_worker_count": {
                                    "count": 2,
                                    "p50": 2,
                                    "p95": 3,
                                    "max": 3,
                                    "avg": 2.5,
                                },
                                "pending_worker_count": {
                                    "count": 2,
                                    "p50": 1,
                                    "p95": 4,
                                    "max": 4,
                                    "avg": 2.5,
                                },
                                "worker_limit": {
                                    "count": 2,
                                    "p50": 8,
                                    "p95": 8,
                                    "max": 8,
                                    "avg": 8,
                                },
                                "thumbnail_cache_bytes": {
                                    "count": 2,
                                    "p50": 1024,
                                    "p95": 2048,
                                    "max": 4096,
                                    "avg": 2560,
                                },
                                "thumbnail_cache_entries": {
                                    "count": 2,
                                    "p50": 1,
                                    "p95": 2,
                                    "max": 3,
                                    "avg": 2,
                                },
                                "thumbnail_cache_probe_ms": {
                                    "count": 2,
                                    "p50": 1,
                                    "p95": 2,
                                    "max": 3,
                                    "avg": 2,
                                },
                            },
                        },
                        "mask.tile_cache.sampled": {
                            "count": 2,
                            "severities": {"info": 2},
                            "numeric_fields": {
                                "cache_hit_count": {
                                    "count": 2,
                                    "p50": 3,
                                    "p95": 5,
                                    "max": 5,
                                    "avg": 4,
                                },
                                "cache_miss_count": {
                                    "count": 2,
                                    "p50": 1,
                                    "p95": 2,
                                    "max": 2,
                                    "avg": 1.5,
                                },
                                "cache_lookup_count": {
                                    "count": 2,
                                    "p50": 4,
                                    "p95": 7,
                                    "max": 7,
                                    "avg": 5.5,
                                },
                                "cache_hit_rate": {
                                    "count": 2,
                                    "p50": 0.75,
                                    "p95": 0.80,
                                    "max": 0.83,
                                    "avg": 0.77,
                                },
                            },
                        },
                        "mask.brush.feedback_completed": {
                            "count": 2,
                            "severities": {"info": 2},
                            "timings": {
                                "input_to_overlay_ms": {
                                    "count": 2,
                                    "p50": 6,
                                    "p95": 11,
                                    "max": 12,
                                },
                                "elapsed_ms": {
                                    "count": 2,
                                    "p50": 6,
                                    "p95": 11,
                                    "max": 12,
                                },
                            },
                        },
                        "mask.smart_select.completed": {
                            "count": 1,
                            "severities": {"info": 1},
                            "timings": {
                                "elapsed_ms": {
                                    "count": 1,
                                    "p50": 31,
                                    "p95": 31,
                                    "max": 31,
                                },
                            },
                        },
                        "mask.refine_edge.completed": {
                            "count": 1,
                            "severities": {"info": 1},
                            "timings": {
                                "elapsed_ms": {
                                    "count": 1,
                                    "p50": 17,
                                    "p95": 17,
                                    "max": 17,
                                },
                                "refine_edge_worker_elapsed_ms": {
                                    "count": 1,
                                    "p50": 17,
                                    "p95": 17,
                                    "max": 17,
                                },
                            },
                        },
                        "mask.inpaint.completed": {
                            "count": 1,
                            "severities": {"info": 1},
                            "timings": {
                                "elapsed_ms": {
                                    "count": 1,
                                    "p50": 44,
                                    "p95": 44,
                                    "max": 44,
                                },
                            },
                        },
                        "worker.queue.summary": {
                            "count": 2,
                            "severities": {"info": 2},
                            "timings": {
                                "queue_wait_ms": {
                                    "count": 2,
                                    "p50": 5,
                                    "p95": 9,
                                    "max": 10,
                                },
                                "worker_elapsed_ms": {
                                    "count": 2,
                                    "p50": 14,
                                    "p95": 22,
                                    "max": 24,
                                },
                            },
                            "categories": {
                                "workflow": {"preview": 2},
                                "worker_pool": {"preview-render": 2},
                                "surface": {"edit": 2},
                            },
                            "numeric_fields": {
                                "active_worker_count": {
                                    "count": 2,
                                    "p50": 1,
                                    "p95": 2,
                                    "max": 2,
                                    "avg": 1.5,
                                },
                                "pending_worker_count": {
                                    "count": 2,
                                    "p50": 0,
                                    "p95": 1,
                                    "max": 1,
                                    "avg": 0.5,
                                },
                                "worker_limit": {
                                    "count": 2,
                                    "p50": 2,
                                    "p95": 2,
                                    "max": 2,
                                    "avg": 2,
                                },
                                "completed_count": {
                                    "count": 2,
                                    "p50": 1,
                                    "p95": 1,
                                    "max": 1,
                                    "avg": 1,
                                },
                                "cancelled_count": {
                                    "count": 2,
                                    "p50": 0,
                                    "p95": 1,
                                    "max": 1,
                                    "avg": 0.5,
                                },
                            },
                        },
                        "preview.render.superseded": {
                            "count": 2,
                            "severities": {"info": 2},
                            "categories": {
                                "reason": {
                                    "newer_interaction": 2,
                                }
                            },
                        },
                        "qml.frame.summary": {
                            "count": 1,
                            "severities": {"info": 1},
                            "timings": {
                                "frame_interval_ms": {
                                    "count": 120,
                                    "p50": 16,
                                    "p95": 24,
                                    "max": 42,
                                }
                            },
                            "numeric_fields": {
                                "jank_frame_count": {
                                    "count": 1,
                                    "p50": 3,
                                    "p95": 3,
                                    "max": 3,
                                    "avg": 3,
                                },
                                "dropped_frame_count": {
                                    "count": 1,
                                    "p50": 1,
                                    "p95": 1,
                                    "max": 1,
                                    "avg": 1,
                                },
                            },
                        },
                        "qml.route.ready": {
                            "count": 1,
                            "severities": {"info": 1},
                            "timings": {
                                "elapsed_ms": {
                                    "count": 1,
                                    "p50": 90,
                                    "p95": 90,
                                    "max": 90,
                                }
                            },
                            "categories": {
                                "surface": {
                                    "edit": 1,
                                }
                            },
                        },
                        "preview.texture.uploaded": {
                            "count": 2,
                            "severities": {"info": 2},
                            "timings": {
                                "elapsed_ms": {
                                    "count": 2,
                                    "p50": 2,
                                    "p95": 4,
                                    "max": 5,
                                }
                            },
                            "numeric_fields": {
                                "native_texture_bytes": {
                                    "count": 2,
                                    "p50": 8192,
                                    "p95": 12288,
                                    "max": 16384,
                                    "avg": 12288,
                                },
                                "upload_bytes": {
                                    "count": 2,
                                    "p50": 4096,
                                    "p95": 8192,
                                    "max": 8192,
                                    "avg": 6144,
                                },
                                "gpu_memory_estimate_bytes": {
                                    "count": 2,
                                    "p50": 8192,
                                    "p95": 12288,
                                    "max": 16384,
                                    "avg": 12288,
                                },
                                "gpu_memory_bytes": {
                                    "count": 2,
                                    "p50": 8192,
                                    "p95": 12288,
                                    "max": 16384,
                                    "avg": 12288,
                                },
                            },
                        },
                        "preview.texture.upload_preserved": {
                            "count": 1,
                            "severities": {"info": 1},
                            "numeric_fields": {
                                "native_texture_bytes": {
                                    "count": 1,
                                    "p50": 32768,
                                    "p95": 32768,
                                    "max": 32768,
                                    "avg": 32768,
                                },
                                "gpu_memory_estimate_bytes": {
                                    "count": 1,
                                    "p50": 32768,
                                    "p95": 32768,
                                    "max": 32768,
                                    "avg": 32768,
                                },
                                "gpu_memory_bytes": {
                                    "count": 1,
                                    "p50": 32768,
                                    "p95": 32768,
                                    "max": 32768,
                                    "avg": 32768,
                                }
                            },
                        },
                        "render.stage.completed": {
                            "count": 4,
                            "severities": {"info": 4},
                            "timings": {
                                "elapsed_ms": {
                                    "count": 4,
                                    "p50": 7,
                                    "p95": 18,
                                    "max": 20,
                                }
                            },
                            "categories": {
                                "stage": {
                                    "source_decode": 2,
                                    "native_texture_upload": 2,
                                }
                            },
                        },
                        "tool.stage.completed": {
                            "count": 2,
                            "severities": {"info": 2},
                            "timings": {
                                "elapsed_ms": {
                                    "count": 2,
                                    "p50": 12,
                                    "p95": 24,
                                    "max": 25,
                                }
                            },
                            "categories": {
                                "tool_id": {
                                    "global_sliders": 2,
                                }
                            },
                        },
                        "workflow.resource_summary": {
                            "count": 1,
                            "severities": {"info": 1},
                            "numeric_fields": {
                                "rss_start_kb": {
                                    "count": 1,
                                    "p50": 10000,
                                    "p95": 10000,
                                    "max": 10000,
                                    "avg": 10000,
                                },
                                "rss_peak_kb": {
                                    "count": 1,
                                    "p50": 12345,
                                    "p95": 12345,
                                    "max": 12345,
                                    "avg": 12345,
                                },
                                "rss_end_kb": {
                                    "count": 1,
                                    "p50": 11000,
                                    "p95": 11000,
                                    "max": 11000,
                                    "avg": 11000,
                                },
                                "rss_delta_kb": {
                                    "count": 1,
                                    "p50": 1000,
                                    "p95": 1000,
                                    "max": 1000,
                                    "avg": 1000,
                                },
                            },
                        },
                        "job.failed": {
                            "count": 1,
                            "severities": {"error": 1},
                            "timings": {"elapsed_ms": {"count": 1, "p50": 5, "p95": 5, "max": 5}},
                        },
                    },
                    "groups": {
                        "workflow": {
                            "perf-demo": {
                                "count": 2,
                                "events": {"preview.render.completed": 2},
                                "timings": {
                                    "elapsed_ms": {
                                        "count": 2,
                                        "p50": 10,
                                        "p95": 20,
                                        "max": 25,
                                    }
                                },
                                "numeric_fields": {
                                    "elapsed_ms": {
                                        "count": 2,
                                        "p50": 10,
                                        "p95": 20,
                                        "max": 25,
                                    }
                                },
                            },
                            "preview": {
                                "count": 2,
                                "events": {"worker.queue.summary": 2},
                                "timings": {
                                    "queue_wait_ms": {
                                        "count": 2,
                                        "p50": 5,
                                        "p95": 9,
                                        "max": 10,
                                    }
                                },
                                "numeric_fields": {
                                    "active_worker_count": {
                                        "count": 2,
                                        "p50": 1,
                                        "p95": 2,
                                        "max": 2,
                                    },
                                    "pending_worker_count": {
                                        "count": 2,
                                        "p50": 0,
                                        "p95": 1,
                                        "max": 1,
                                    },
                                    "worker_limit": {
                                        "count": 2,
                                        "p50": 2,
                                        "p95": 2,
                                        "max": 2,
                                    },
                                    "completed_count": {
                                        "count": 2,
                                        "p50": 1,
                                        "p95": 1,
                                        "max": 1,
                                    },
                                    "cancelled_count": {
                                        "count": 2,
                                        "p50": 0,
                                        "p95": 1,
                                        "max": 1,
                                    },
                                },
                            }
                        },
                        "tool": {
                            "global_sliders": {
                                "count": 2,
                                "events": {"tool.stage.completed": 2},
                                "timings": {
                                    "elapsed_ms": {
                                        "count": 2,
                                        "p50": 12,
                                        "p95": 24,
                                        "max": 25,
                                    }
                                },
                                "numeric_fields": {
                                    "elapsed_ms": {
                                        "count": 2,
                                        "p50": 12,
                                        "p95": 24,
                                        "max": 25,
                                    }
                                },
                            }
                        },
                        "stage": {
                            "source_decode": {
                                "count": 2,
                                "events": {"render.stage.completed": 2},
                                "timings": {
                                    "elapsed_ms": {
                                        "count": 2,
                                        "p50": 11,
                                        "p95": 19,
                                        "max": 20,
                                    }
                                },
                                "numeric_fields": {
                                    "elapsed_ms": {
                                        "count": 2,
                                        "p50": 11,
                                        "p95": 19,
                                        "max": 20,
                                    }
                                },
                            },
                            "native_texture_upload": {
                                "count": 2,
                                "events": {"render.stage.completed": 2},
                                "timings": {
                                    "elapsed_ms": {
                                        "count": 2,
                                        "p50": 3,
                                        "p95": 5,
                                        "max": 5,
                                    }
                                },
                                "numeric_fields": {
                                    "elapsed_ms": {
                                        "count": 2,
                                        "p50": 3,
                                        "p95": 5,
                                        "max": 5,
                                    }
                                },
                            },
                        },
                        "worker_pool": {
                            "preview-render": {
                                "count": 2,
                                "events": {"worker.queue.summary": 2},
                                "timings": {
                                    "queue_wait_ms": {
                                        "count": 2,
                                        "p50": 5,
                                        "p95": 9,
                                        "max": 10,
                                    }
                                },
                                "numeric_fields": {
                                    "active_worker_count": {
                                        "count": 2,
                                        "p50": 1,
                                        "p95": 2,
                                        "max": 2,
                                    },
                                    "pending_worker_count": {
                                        "count": 2,
                                        "p50": 0,
                                        "p95": 1,
                                        "max": 1,
                                    },
                                    "worker_limit": {
                                        "count": 2,
                                        "p50": 2,
                                        "p95": 2,
                                        "max": 2,
                                    },
                                    "completed_count": {
                                        "count": 2,
                                        "p50": 1,
                                        "p95": 1,
                                        "max": 1,
                                    },
                                    "cancelled_count": {
                                        "count": 2,
                                        "p50": 0,
                                        "p95": 1,
                                        "max": 1,
                                    },
                                },
                            }
                        }
                    },
                }
            )
            + "\n",
            encoding="utf-8",
        )
        result = module.SuiteResult(
            name="demo",
            description="demo suite",
            command=["true"],
            returncode=0,
            log_file=None,
            artifact_dirs=[str(artifact)],
            status="passed",
        )
        baseline_payload = {
            "path": str(Path(temp_dir) / "baseline"),
            "manifest": {
                "profile": "smoke",
                "suites": [{"name": "demo"}],
            },
            "report": {
                "suites": [
                    {
                        "name": "demo",
                        "artifacts": [
                            {
                                "path": str(Path(temp_dir) / "baseline" / "bench"),
                                "manifest": {"scenario": "demo"},
                                "timings": [
                                    {
                                        "event": "preview.render.completed",
                                        "field": "elapsed_ms",
                                        "p50": 5,
                                        "p95": 10,
                                        "max": 15,
                                    }
                                ],
                                "workflow_latencies": [
                                    {
                                        "workflow": "perf-demo",
                                        "field": "elapsed_ms",
                                        "p50": 8,
                                        "p95": 16,
                                        "max": 20,
                                    }
                                ],
                                "tool_latencies": [
                                    {
                                        "tool_id": "global_sliders",
                                        "field": "elapsed_ms",
                                        "p50": 10,
                                        "p95": 20,
                                        "max": 22,
                                    }
                                ],
                                "render_stages": [
                                    {
                                        "stage": "source_decode",
                                        "field": "elapsed_ms",
                                        "p50": 9,
                                        "p95": 15,
                                        "max": 16,
                                    }
                                ],
                                "memory_growth": [
                                    {
                                        "metric": "rss_delta_kb",
                                        "p95": 500,
                                        "max": 700,
                                        "unit": "KiB",
                                    }
                                ],
                                "cache_pressure": [
                                    {
                                        "metric": "cache_hit_rate",
                                        "statistic": "avg",
                                        "value": 0.25,
                                        "unit": "ratio",
                                    }
                                ],
                                "worker_pressure": [
                                    {
                                        "event": "thumbnail.completed",
                                        "queue_wait_p95": 4,
                                        "active_worker_max": 2,
                                        "pending_worker_max": 2,
                                    }
                                ],
                                "backend_fallbacks": [
                                    {
                                        "field": "fallback_reason",
                                        "value": "metal_unavailable",
                                        "count": 2,
                                    }
                                ],
                            }
                        ],
                    }
                ]
            },
        }
        report = module.build_report_payload([result], baseline_payload)
        assert_true(report["threshold_profile"] == "local-dev", "threshold profile should be recorded")
        assert_true(report["threshold_violation_count"] == 0, "fixture should not violate thresholds")
        suite = report["suites"][0]
        assert_true(suite["artifacts"][0]["warning_error_counts"]["error"] == 1, "missing error count")
        assert_true(suite["artifacts"][0]["attention_event_count"] == 1, "missing attention count")
        resource_names = {
            row["resource"] for row in suite["artifacts"][0]["resources"]
        }
        assert_true("process_start_rss" in resource_names, "missing start RSS resource row")
        assert_true("process_peak_rss" in resource_names, "missing peak RSS resource row")
        assert_true("process_end_rss" in resource_names, "missing end RSS resource row")
        assert_true("process_rss_delta" in resource_names, "missing RSS delta resource row")
        assert_true("preview_cache_total" in resource_names, "missing total cache resource row")
        assert_true("decoded_image_cache" in resource_names, "missing decoded cache resource row")
        assert_true("linear_image_cache" in resource_names, "missing linear cache resource row")
        assert_true("adjusted_preview_cache" in resource_names, "missing adjusted cache resource row")
        assert_true("rendered_preview_cache" in resource_names, "missing rendered cache resource row")
        assert_true("preview_cache_hit_rate" in resource_names, "missing cache hit-rate resource row")
        assert_true("thumbnail_cache_bytes" in resource_names, "missing thumbnail cache bytes row")
        assert_true("thumbnail_cache_entries" in resource_names, "missing thumbnail cache entries row")
        assert_true("thumbnail_cache_probe" in resource_names, "missing thumbnail cache probe row")
        assert_true("native_texture" in resource_names, "missing native texture row")
        assert_true("native_texture_upload" in resource_names, "missing native texture upload row")
        assert_true("native_texture_preserved" in resource_names, "missing preserved native texture row")
        assert_true("gpu_memory" in resource_names, "missing normalized GPU memory row")
        assert_true("gpu_memory_preserved" in resource_names, "missing preserved normalized GPU memory row")
        assert_true("gpu_memory_estimate" in resource_names, "missing GPU memory estimate row")
        assert_true(
            "gpu_memory_estimate_preserved" in resource_names,
            "missing preserved GPU memory estimate row",
        )
        assert_true(
            suite["artifacts"][0]["timings"][0]["event"] == "job.failed",
            "timing rows should be sorted by event name",
        )
        render_stage_names = {
            row["stage"] for row in suite["artifacts"][0]["render_stages"]
        }
        workflow_names = {
            row["workflow"] for row in suite["artifacts"][0]["workflow_latencies"]
        }
        assert_true("perf-demo" in workflow_names, "missing workflow latency row")
        tool_ids = {
            row["tool_id"] for row in suite["artifacts"][0]["tool_latencies"]
        }
        assert_true("global_sliders" in tool_ids, "missing tool latency row")
        global_tool_row = next(
            row for row in suite["artifacts"][0]["tool_latencies"] if row["tool_id"] == "global_sliders"
        )
        assert_true(global_tool_row["p95"] == 24, "tool latency should use grouped tool p95")
        memory_metrics = {
            row["metric"] for row in suite["artifacts"][0]["memory_growth"]
        }
        assert_true("rss_delta_kb" in memory_metrics, "missing memory growth row")
        cache_metrics = {
            row["metric"] for row in suite["artifacts"][0]["cache_pressure"]
        }
        assert_true("cache_bytes" in cache_metrics, "missing total cache pressure row")
        assert_true("cache_hit_rate" in cache_metrics, "missing cache pressure row")
        assert_true("mask_tile_cache_hit_rate" in cache_metrics, "missing mask tile cache hit-rate row")
        assert_true("mask_tile_cache_miss_count" in cache_metrics, "missing mask tile cache miss row")
        backend_values = {
            row["value"] for row in suite["artifacts"][0]["backend_fallbacks"]
        }
        assert_true("metal_unavailable" in backend_values, "missing backend fallback row")
        worker_events = {
            row["event"] for row in suite["artifacts"][0]["worker_pressure"]
        }
        assert_true("thumbnail.completed" in worker_events, "missing worker pressure row")
        assert_true("worker.queue.summary" in worker_events, "missing normalized worker summary row")
        assert_true(
            "worker.workflow:preview" in worker_events,
            "missing worker pressure row grouped by workflow",
        )
        assert_true(
            "worker.pool:preview-render" in worker_events,
            "missing worker pressure row grouped by pool",
        )
        thumbnail_worker_row = next(
            row for row in suite["artifacts"][0]["worker_pressure"]
            if row["event"] == "thumbnail.completed"
        )
        assert_true(
            thumbnail_worker_row["active_worker_max"] == 3,
            "worker pressure should expose active worker max",
        )
        assert_true(
            thumbnail_worker_row["pending_worker_max"] == 4,
            "worker pressure should expose pending worker max",
        )
        assert_true(
            thumbnail_worker_row["queue_wait_p95"] == 6,
            "worker pressure should expose queue wait p95",
        )
        normalized_worker_row = next(
            row for row in suite["artifacts"][0]["worker_pressure"]
            if row["event"] == "worker.queue.summary"
        )
        assert_true(
            normalized_worker_row["workflow"] == "preview",
            "worker pressure should expose workflow from normalized events",
        )
        assert_true(
            normalized_worker_row["worker_pool"] == "preview-render",
            "worker pressure should expose pool from normalized events",
        )
        assert_true(
            normalized_worker_row["completed_count_max"] == 1,
            "worker pressure should expose completed counts",
        )
        assert_true(
            normalized_worker_row["cancelled_count_max"] == 1,
            "worker pressure should expose cancelled counts",
        )
        mask_operations = {
            row["operation"] for row in suite["artifacts"][0]["mask_performance"]
        }
        assert_true("brush_feedback" in mask_operations, "missing mask brush feedback row")
        assert_true("mask_tile_cache" in mask_operations, "missing mask tile cache row")
        assert_true("smart_selection" in mask_operations, "missing smart selection row")
        assert_true("refine_edge" in mask_operations, "missing refine edge row")
        assert_true("inpaint" in mask_operations, "missing inpaint row")
        brush_feedback_row = next(
            row for row in suite["artifacts"][0]["mask_performance"]
            if row["operation"] == "brush_feedback"
        )
        assert_true(brush_feedback_row["p95"] == 11, "mask brush feedback p95 should be visible")
        mask_tile_row = next(
            row for row in suite["artifacts"][0]["mask_performance"]
            if row["operation"] == "mask_tile_cache"
        )
        assert_true(
            mask_tile_row["cache_hit_rate_avg"] == 0.77,
            "mask tile cache hit rate should be visible",
        )
        refine_row = next(
            row for row in suite["artifacts"][0]["mask_performance"]
            if row["operation"] == "refine_edge"
        )
        assert_true(refine_row["p95"] == 17, "refine edge timing should be visible")
        cancellation_events = {
            row["event"] for row in suite["artifacts"][0]["cancellations"]
        }
        assert_true(
            "preview.render.superseded" in cancellation_events,
            "missing cancellation/wasted-work row",
        )
        qml_events = {
            row["event"] for row in suite["artifacts"][0]["qml_frames"]
        }
        assert_true("qml.frame.summary" in qml_events, "missing QML frame summary row")
        assert_true("qml.route.ready" in qml_events, "missing QML route readiness row")
        qml_frame_row = next(
            row for row in suite["artifacts"][0]["qml_frames"]
            if row["event"] == "qml.frame.summary"
        )
        assert_true(qml_frame_row["frame_p95"] == 24, "QML frame p95 should be reported")
        assert_true(qml_frame_row["jank_max"] == 3, "QML jank count should be reported")
        qml_route_row = next(
            row for row in suite["artifacts"][0]["qml_frames"]
            if row["event"] == "qml.route.ready"
        )
        assert_true(qml_route_row["route_p95"] == 90, "QML route p95 should be reported")
        threshold_rows = suite["artifacts"][0]["thresholds"]
        threshold_metrics = {row["metric"] for row in threshold_rows}
        assert_true("preview_render_p95_ms" in threshold_metrics, "missing preview threshold row")
        assert_true("worker_queue_wait_p95_ms" in threshold_metrics, "missing worker threshold row")
        assert_true(
            any(row["status"] == "missing" for row in threshold_rows),
            "threshold report should call out missing measured rows",
        )
        assert_true(
            any(
                row["metric"] == "preview_render_p95_ms" and row["status"] == "passed"
                for row in threshold_rows
            ),
            "preview threshold should pass for the fixture",
        )
        bottleneck_rows = suite["artifacts"][0]["bottlenecks"]
        assert_true(len(bottleneck_rows) == 1, "artifact should have one bottleneck summary row")
        bottleneck_row = bottleneck_rows[0]
        assert_true(bottleneck_row["scenario"] == "demo", "bottleneck row should include scenario")
        assert_true(bottleneck_row["render_p95_ms"] == 20, "bottleneck row should include render p95")
        assert_true(bottleneck_row["mask_brush_p95_ms"] == 11, "bottleneck row should include mask p95")
        assert_true(bottleneck_row["memory_delta_kb"] == 1000, "bottleneck row should include memory delta")
        assert_true(
            bottleneck_row["worker_queue_p95_ms"] == 9,
            "bottleneck row should include worker pressure",
        )
        assert_true(
            bottleneck_row["qml_frame_p95_ms"] == 24,
            "bottleneck row should include QML frame pressure",
        )
        assert_true(
            bottleneck_row["backend_fallback_count"] == 1,
            "bottleneck row should include backend fallback count",
        )
        comparison = suite["artifacts"][0]["baseline_comparison"]
        assert_true(comparison["matched"], "baseline comparison should match the artifact")
        comparison_categories = {
            row["category"] for row in comparison["rows"]
        }
        assert_true("event_timing" in comparison_categories, "missing timing baseline delta")
        assert_true("workflow_latency" in comparison_categories, "missing workflow baseline delta")
        assert_true("tool_latency" in comparison_categories, "missing tool baseline delta")
        assert_true("render_stage" in comparison_categories, "missing render-stage baseline delta")
        assert_true("memory_growth" in comparison_categories, "missing memory baseline delta")
        assert_true("cache_pressure" in comparison_categories, "missing cache baseline delta")
        assert_true("worker_pressure" in comparison_categories, "missing worker baseline delta")
        assert_true("backend_fallback" in comparison_categories, "missing backend baseline delta")
        timing_delta = next(
            row for row in comparison["rows"]
            if row["category"] == "event_timing" and row["metric"] == "preview.render.completed"
        )
        assert_true(timing_delta["delta_p95"] == 10, "timing p95 delta should be reported")
        memory_delta = next(
            row for row in comparison["rows"]
            if row["category"] == "memory_growth" and row["metric"] == "rss_delta_kb"
        )
        assert_true(memory_delta["delta_p95"] == 500, "memory p95 delta should be reported")
        cache_delta = next(
            row for row in comparison["rows"]
            if row["category"] == "cache_pressure" and row["metric"] == "cache_hit_rate"
        )
        assert_true(
            abs(cache_delta["delta_value"] - 0.125) < 0.0001,
            "cache hit-rate delta should be reported",
        )
        worker_delta = next(
            row for row in comparison["rows"]
            if row["category"] == "worker_pressure" and row["metric"] == "thumbnail.completed"
        )
        assert_true(
            worker_delta["delta_queue_wait_p95"] == 2,
            "worker queue p95 delta should be reported",
        )
        backend_delta = next(
            row for row in comparison["rows"]
            if row["category"] == "backend_fallback"
            and row["metric"] == "fallback_reason=metal_unavailable"
        )
        assert_true(backend_delta["delta_count"] == -1, "backend fallback count delta should be reported")
        assert_true("source_decode" in render_stage_names, "missing source decode render stage")
        assert_true(
            "native_texture_upload" in render_stage_names,
            "missing native texture upload render stage",
        )
        source_decode_stage = next(
            row for row in suite["artifacts"][0]["render_stages"] if row["stage"] == "source_decode"
        )
        assert_true(source_decode_stage["p95"] == 19, "render stage should use grouped stage p95")
        metrics = module.build_metrics_payload(report)
        metric_resources = metrics["suites"][0]["artifacts"][0]["resources"]
        metric_workflows = metrics["suites"][0]["artifacts"][0]["workflow_latencies"]
        assert_true(
            any(row["workflow"] == "perf-demo" for row in metric_workflows),
            "metrics report should include workflow latency rows",
        )
        metric_tools = metrics["suites"][0]["artifacts"][0]["tool_latencies"]
        assert_true(
            any(row["tool_id"] == "global_sliders" for row in metric_tools),
            "metrics report should include tool latency rows",
        )
        metric_render_stages = metrics["suites"][0]["artifacts"][0]["render_stages"]
        assert_true(
            any(row["stage"] == "source_decode" for row in metric_render_stages),
            "metrics report should include render stages",
        )
        metric_worker_pressure = metrics["suites"][0]["artifacts"][0]["worker_pressure"]
        assert_true(
            any(row["event"] == "thumbnail.completed" for row in metric_worker_pressure),
            "metrics report should include worker pressure rows",
        )
        metric_mask_performance = metrics["suites"][0]["artifacts"][0]["mask_performance"]
        assert_true(
            any(row["operation"] == "brush_feedback" for row in metric_mask_performance),
            "metrics report should include mask performance rows",
        )
        metric_cancellations = metrics["suites"][0]["artifacts"][0]["cancellations"]
        assert_true(
            any(row["event"] == "preview.render.superseded" for row in metric_cancellations),
            "metrics report should include cancellation rows",
        )
        metric_qml_frames = metrics["suites"][0]["artifacts"][0]["qml_frames"]
        assert_true(
            any(row["event"] == "qml.frame.summary" for row in metric_qml_frames),
            "metrics report should include QML frame rows",
        )
        metric_thresholds = metrics["suites"][0]["artifacts"][0]["thresholds"]
        assert_true(
            any(row["metric"] == "preview_render_p95_ms" for row in metric_thresholds),
            "metrics report should include threshold rows",
        )
        assert_true(metrics["threshold_violation_count"] == 0, "metrics should include threshold count")
        metric_bottlenecks = metrics["suites"][0]["artifacts"][0]["bottlenecks"]
        assert_true(
            metric_bottlenecks[0]["worker_queue_p95_ms"] == 9,
            "metrics report should include bottleneck summary rows",
        )
        metric_comparison = metrics["suites"][0]["artifacts"][0]["baseline_comparison"]
        assert_true(
            metric_comparison["matched"],
            "metrics report should include baseline comparison",
        )
        assert_true(
            any(row["category"] == "cache_pressure" for row in metric_comparison["rows"]),
            "metrics report should include baseline cache deltas",
        )
        peak_resource = next(row for row in metric_resources if row["resource"] == "process_peak_rss")
        assert_true(peak_resource["value"] == 12345, "metrics report should include resource rows")
        decoded_resource = next(row for row in metric_resources if row["resource"] == "decoded_image_cache")
        assert_true(decoded_resource["value"] == 200, "metrics report should include decoded cache bytes")
        hit_rate_resource = next(row for row in metric_resources if row["resource"] == "preview_cache_hit_rate")
        assert_true(hit_rate_resource["value"] == 0.375, "metrics report should include cache hit rate")
        total_cache_resource = next(row for row in metric_resources if row["resource"] == "preview_cache_total")
        assert_true(total_cache_resource["value"] == 2000, "metrics report should include total cache bytes")
        thumbnail_bytes_resource = next(
            row for row in metric_resources if row["resource"] == "thumbnail_cache_bytes"
        )
        assert_true(
            thumbnail_bytes_resource["value"] == 4096,
            "metrics report should include thumbnail cache bytes",
        )
        thumbnail_entries_resource = next(
            row for row in metric_resources if row["resource"] == "thumbnail_cache_entries"
        )
        assert_true(
            thumbnail_entries_resource["value"] == 3,
            "metrics report should include thumbnail cache entries",
        )
        native_texture_resource = next(
            row for row in metric_resources if row["resource"] == "native_texture"
        )
        assert_true(
            native_texture_resource["value"] == 16384,
            "metrics report should include native texture bytes",
        )
        native_upload_resource = next(
            row for row in metric_resources if row["resource"] == "native_texture_upload"
        )
        assert_true(
            native_upload_resource["value"] == 8192,
            "metrics report should include native texture upload bytes",
        )
        gpu_estimate_resource = next(
            row for row in metric_resources if row["resource"] == "gpu_memory_estimate"
        )
        assert_true(
            gpu_estimate_resource["value"] == 16384,
            "metrics report should include GPU memory estimate bytes",
        )
        gpu_memory_resource = next(row for row in metric_resources if row["resource"] == "gpu_memory")
        assert_true(
            gpu_memory_resource["value"] == 16384,
            "metrics report should include normalized GPU memory bytes",
        )


def main() -> int:
    test_profiles_and_dry_run()
    test_goal8_suites_can_be_selected_and_dry_run()
    test_goal8_suite_command_mappings()
    test_threshold_evaluator_reports_failures_and_missing_metrics()
    test_skipped_suite_cannot_be_reported_as_a_release_pass()
    test_artifact_parsing_and_report_payload()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
