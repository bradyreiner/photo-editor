#!/usr/bin/env python3
"""Exercise fail-closed CMake architecture dependency boundaries."""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = (REPO_ROOT / "cmake").as_posix()
# Production event-loop pumping is legacy debt, never a pattern for new code.
# New call sites are forbidden until these explicitly reviewed synchronous adapters are removed.
LEGACY_PRODUCTION_PROCESS_EVENT_COUNTS = {
    "apps/desktop/src/agent_edit_controller.cpp": 7,
    "apps/desktop/src/agent_local_model.cpp": 3,
    "apps/desktop/src/agent_mcp_client.cpp": 4,
    "apps/desktop/src/catalog_asset_model.cpp": 1,
}


def configure(source: Path, build: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["cmake", "-S", str(source), "-B", str(build)],
        check=False,
        capture_output=True,
        text=True,
    )


def write_project(root: Path, source_text: str, link_qt: bool = False) -> None:
    (root / "engine.cpp").write_text(source_text, encoding="utf-8")
    qt_setup = ""
    qt_link = ""
    if link_qt:
        qt_setup = "add_library(Qt6::Core INTERFACE IMPORTED)\n"
        qt_link = "target_link_libraries(boundary_engine PRIVATE Qt6::Core)\n"
    (root / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.21)\n"
        "project(architecture_boundary_fixture LANGUAGES CXX)\n"
        f'list(APPEND CMAKE_MODULE_PATH "{MODULE_PATH}")\n'
        f"{qt_setup}"
        "add_library(boundary_engine STATIC engine.cpp)\n"
        f"{qt_link}"
        "include(ArchitectureBoundaries)\n"
        "photo_editor_enforce_qt_free_target(boundary_engine)\n",
        encoding="utf-8",
    )


def check_maintainability_boundaries() -> list[str]:
    errors: list[str] = []
    controller = (
        REPO_ROOT / "apps/desktop/src/editor_preview_controller.cpp"
    ).read_text(encoding="utf-8")
    controller_header = (
        REPO_ROOT / "apps/desktop/src/editor_preview_controller.h"
    ).read_text(encoding="utf-8")
    photo_render_utils = (
        REPO_ROOT / "apps/desktop/src/photo_render_utils.cpp"
    ).read_text(encoding="utf-8")
    local_mask_compiler_header = (
        REPO_ROOT / "apps/desktop/src/local_mask_compiler.h"
    ).read_text(encoding="utf-8")
    thumbnail_canonical_renderer_header = (
        REPO_ROOT / "apps/desktop/src/thumbnail_canonical_renderer.h"
    ).read_text(encoding="utf-8")
    source_image_decode_header = (
        REPO_ROOT / "apps/desktop/src/source_image_decode.h"
    ).read_text(encoding="utf-8")
    desktop_cmake = (
        REPO_ROOT / "apps/desktop/CMakeLists.txt"
    ).read_text(encoding="utf-8")
    tests_cmake = (REPO_ROOT / "tests/CMakeLists.txt").read_text(
        encoding="utf-8"
    )
    render_plan_store_source = (
        REPO_ROOT / "apps/desktop/src/render_plan_store.cpp"
    ).read_text(encoding="utf-8")
    render_plan_store_header = (
        REPO_ROOT / "apps/desktop/src/render_plan_store.h"
    ).read_text(encoding="utf-8")
    render_plan_store_test = (
        REPO_ROOT / "tests/render_plan_store_test.cpp"
    ).read_text(encoding="utf-8")
    preview_color_pipeline_config = (
        REPO_ROOT
        / "apps/desktop/src/preview_color_pipeline_config.cpp"
    ).read_text(encoding="utf-8")
    desktop_pipeline_state_compiler = (
        REPO_ROOT
        / "apps/desktop/src/desktop_pipeline_state_compiler.cpp"
    ).read_text(encoding="utf-8")
    if '#include <QSettings>' in controller or "system_monitor_profile_cache()" in controller:
        errors.append(
            "EditorPreviewController must not reclaim display-profile settings or cache ownership"
        )
    if '#include "display_profile_service.h"' not in controller:
        errors.append(
            "EditorPreviewController must coordinate display profiles through DisplayProfileService"
        )
    if '#include "display_profile_discovery.h"' not in controller:
        errors.append(
            "EditorPreviewController must coordinate platform ICC discovery through display_profile_discovery"
        )
    if (
        '#include "preview_color_pipeline_config.h"' not in controller
        or "compile_preview_color_pipeline_config_for_display(" not in controller
        or "effective_state.preview_output_mode" in controller
        or 'QStringLiteral("Match Export")' not in preview_color_pipeline_config
        or 'QStringLiteral("Soft Proof")' not in preview_color_pipeline_config
        or "effective_state.soft_proof_enabled = true" not in
        preview_color_pipeline_config
        or "monitor_snapshot.display_target_valid()" not in
        preview_color_pipeline_config
        or "src/preview_color_pipeline_config.cpp" not in desktop_cmake
        or "preview_color_pipeline_config_test" not in tests_cmake
    ):
        errors.append(
            "preview mode, soft-proof, monitor ICC, and display-target policy must remain in the focused preview color configuration owner"
        )
    if (
        '#include "desktop_pipeline_state_compiler.h"' not in controller
        or '#include "desktop_pipeline_state_compiler.h"' not in photo_render_utils
        or "compile_desktop_pipeline_state(" not in controller
        or "DesktopRenderPurpose::Preview" not in controller
        or "compile_desktop_pipeline_state(" not in photo_render_utils
        or "DesktopRenderPurpose::Output" not in photo_render_utils
        or "compile_pipeline_local_mask_sets(" in controller
        or "compile_pipeline_retouch_masks(" in controller
        or "compile_pipeline_local_mask_sets(\n    const QVariantList" in
        photo_render_utils
        or "compile_pipeline_retouch_masks(\n    const QVariantList" in
        photo_render_utils
        or "compile_base_color_adjustment_state(" in controller
        or "compile_base_color_adjustment_state(" in photo_render_utils
        or "maskSetPreviewBypassed" not in desktop_pipeline_state_compiler
        or "maskSetSelectionEdgeContrast" not in desktop_pipeline_state_compiler
        or "src/desktop_pipeline_state_compiler.cpp" not in desktop_cmake
        or "desktop_pipeline_state_compiler.cpp" not in tests_cmake
    ):
        errors.append(
            "preview and output must share one purpose-aware desktop pipeline-state compiler with no controller/export mask assembly"
        )
    if (
        "struct SourceDecodeTelemetry final" not in source_image_decode_header
        or "bool source_image_supported(" not in source_image_decode_header
        or "QSize source_image_size(" not in source_image_decode_header
        or "bool decode_source_image(" not in source_image_decode_header
        or "bool decode_source_image(" not in photo_render_utils
        or "publication=source-linear-v2-camera-characterized-in-engine" not in
        photo_render_utils
        or "bool decode_source_image(" in controller
        or "decode_openexr_source_image(" in controller
        or "photo_editor::io::inspect_openexr(" in controller
        or "decode_native_linear_image(" in controller
        or "decode_raw_linear_image(" in controller
        or "struct DecodeSourceTelemetry final" in controller
        or '#include "source_image_decode.h"' not in controller
        or "source_image_supported(file_path)" not in controller
        or "source_image_size(file_path)" not in controller
        or "decode_source_image(" not in controller
        or "src/source_image_decode.h" not in desktop_cmake
    ):
        errors.append(
            "preview, thumbnail, and export source decoding must share the camera-linear photo_render_utils authority with common cache telemetry"
        )
    retired_controller_api_tokens = (
        "displayReferencePreviewOverlaySource",
        "debugInfo",
        "systemMonitorProfileIdentity",
        "refreshDebugInfo",
    )
    restored_controller_api_tokens = [
        token
        for token in retired_controller_api_tokens
        if token in controller or token in controller_header
    ]
    if restored_controller_api_tokens:
        errors.append(
            "EditorPreviewController must not restore properties retired after the active-caller audit: "
            + ", ".join(restored_controller_api_tokens)
        )
    legacy_discovery_tokens = (
        "CGDisplay",
        "GetICMProfile",
        "windows_advanced_color_monitor_profile",
        "kMaximumSystemMonitorProfileBytes",
    )
    reclaimed_tokens = [
        token for token in legacy_discovery_tokens if token in controller
    ]
    if reclaimed_tokens:
        errors.append(
            "EditorPreviewController must not reclaim platform ICC discovery: "
            + ", ".join(reclaimed_tokens)
        )
    output_wrapper_body = photo_render_utils.split(
        "QImage render_output_image(", 1
    )[1].split(
        "namespace {", 1
    )[0]
    if (
        "canonical_plan_required_for_aces2" not in output_wrapper_body
        or "canonical_plan_required_for_aces2" not in controller
    ):
        errors.append(
            "modern shared output and preview execution must fail closed without a canonical plan"
        )
    backend_bypass_sources = []
    for source_path in sorted(
        (REPO_ROOT / "apps/desktop/src").glob("*.cpp")
    ):
        if source_path.name == "photo_render_utils.cpp":
            continue
        if "render_output_image_with_backend(" in source_path.read_text(
            encoding="utf-8"
        ):
            backend_bypass_sources.append(source_path.name)
    if backend_bypass_sources:
        errors.append(
            "production desktop sources must not call the diagnostic backend renderer directly: "
            + ", ".join(backend_bypass_sources)
        )
    if (
        '#include "flattened_output_renderer.h"' not in controller
        or '#include "flattened_output_renderer.h"' not in
        (
            REPO_ROOT / "apps/desktop/src/photo_render_utils.h"
        ).read_text(encoding="utf-8")
    ):
        errors.append(
            "flattened export must retain its narrow interface instead of importing the broad render utility surface into the controller"
        )
    flattened_export_body = controller.split(
        "bool EditorPreviewController::exportFlattenedPhoto(", 1
    )[1].split(
        "QStringList EditorPreviewController::cropAspectPresets(", 1
    )[0]
    if (
        "render_flattened_output_file(" not in flattened_export_body
        or "render_output_image(" in flattened_export_body
        or "RenderPlanStore::shared().admit(" in
        flattened_export_body
        or "QImage source_image" in flattened_export_body
    ):
        errors.append(
            "EditorPreviewController flattened export must delegate decode, canonical admission, and execution"
        )
    if (
        "QImage render_flattened_output_file(" not in photo_render_utils
        or "RenderPlanStore::shared().admit(" not in
        photo_render_utils.split(
            "QImage render_flattened_output_file(", 1
        )[1]
        or "return render_output_file(" not in
        photo_render_utils.split(
            "QImage render_flattened_output_file(", 1
        )[1]
    ):
        errors.append(
            "flattened output must retain metadata-first canonical admission and executor-owned file decode"
        )
    if (
        '#include "local_mask_compiler.h"' not in controller
        or "compile_pipeline_local_mask(" not in controller
        or "LocalMask pipeline_local_mask(" in controller
        or "enum class MaskRasterPolicy" in controller
        or "compile_pipeline_local_mask(" not in photo_render_utils
        or "LocalMaskRasterPolicy" not in local_mask_compiler_header
        or "local_mask_compiler.h" not in desktop_cmake
    ):
        errors.append(
            "one shared local-mask compiler must own QVariant mask mapping and raster policy"
        )
    if (
        '#include "thumbnail_canonical_renderer.h"' not in controller
        or "render_canonical_thumbnail_image(" not in controller
        or "render_output_image(" in controller
        or "render_canonical_thumbnail_image(" not in photo_render_utils
        or "render_canonical_thumbnail_image(" not in
        thumbnail_canonical_renderer_header
        or "thumbnail_canonical_renderer.h" not in desktop_cmake
    ):
        errors.append(
            "preset thumbnails must use the narrow canonical thumbnail renderer without broad render API redeclarations"
        )
    plan_store_bypass_sources = []
    legacy_plan_admission_sources = []
    for source_path in sorted((REPO_ROOT / "apps/desktop/src").glob("*.cpp")):
        source = source_path.read_text(encoding="utf-8")
        if (
            source_path.name not in
            {"render_plan_store.cpp", "edit_recipe_v13_adapter.cpp"}
            and "compile_desktop_render_plan_with_derived_assets(" in source
        ):
            plan_store_bypass_sources.append(source_path.name)
        if "compile_canonical_render_plan_admission" in source:
            legacy_plan_admission_sources.append(source_path.name)
    for header_path in sorted((REPO_ROOT / "apps/desktop/src").glob("*.h")):
        if "compile_canonical_render_plan_admission" in header_path.read_text(
            encoding="utf-8"
        ):
            legacy_plan_admission_sources.append(header_path.name)
    if (
        "class RenderPlanStore final" not in render_plan_store_header
        or render_plan_store_source.count(
            "compile_desktop_render_plan_with_derived_assets("
        ) != 1
        or "canonical-plan-admission-cache:v1" not in render_plan_store_source
        or "QWaitCondition ready" not in render_plan_store_source
        or "src/render_plan_store.cpp" not in desktop_cmake
        or "src/raw_decode_policy.h" not in desktop_cmake
        or "render_plan_store_test.cpp" not in tests_cmake
        or "photo_editor_desktop_render_plan_store_sources" not in tests_cmake
        or "identical admissions must reuse one byte-identical cached plan"
        not in render_plan_store_test
        or (REPO_ROOT / "apps/desktop/src/canonical_render_plan_admission.h").exists()
        or plan_store_bypass_sources
        or legacy_plan_admission_sources
    ):
        errors.append(
            "RenderPlanStore must remain the sole canonical admission, cache, and compiler caller"
        )

    color_adjustments = (
        REPO_ROOT / "engine/src/pipeline/color_adjustments.cpp"
    ).read_text(encoding="utf-8")
    color_detail_runtime = (
        REPO_ROOT / "engine/src/pipeline/color_detail_runtime.cpp"
    ).read_text(encoding="utf-8")
    metal_backend = (
        REPO_ROOT / "engine/src/pipeline/metal/metal_pipeline_backend.mm"
    ).read_text(encoding="utf-8")
    metal_backend_header = (
        REPO_ROOT
        / "engine/include/photo_editor/pipeline/metal/metal_pipeline_backend.h"
    ).read_text(encoding="utf-8")
    metal_color_adjustments = (
        REPO_ROOT / "engine/src/pipeline/metal/color_adjustments.metal"
    ).read_text(encoding="utf-8")
    if (
        "core_color_control_semantics()" in color_adjustments
        or "color_control_semantics_for_key(" in color_adjustments
    ):
        errors.append(
            "color_adjustments.cpp must not reclaim color-control metadata ownership"
        )
    if (
        "class ColorAdjustmentScheduler" in color_adjustments
        or "std::thread::hardware_concurrency()" in color_adjustments
    ):
        errors.append(
            "color_adjustments.cpp must not reclaim worker scheduler ownership"
        )
    if (
        "void box_blur_into(" in color_adjustments
        or "struct BlurWorkspace final" in color_adjustments
        or "void gaussian_approximate_blur_into(" in color_adjustments
        or "void guided_luminance_blur_into(" in color_adjustments
        or '#include "color_blur_runtime.h"' not in color_adjustments
    ):
        errors.append(
            "color_adjustments.cpp must route reusable blur kernels through the extracted runtime"
        )
    if (
        "bool apply_spatial_detail_effects(" in color_adjustments
        or "void apply_fine_detail_effects(" in color_adjustments
        or '#include "color_detail_runtime.h"' not in color_adjustments
    ):
        errors.append(
            "color_adjustments.cpp must route spatial and fine detail execution through the extracted runtime"
        )
    if (
        "apply_spatial_tone_effects(" in color_detail_runtime
        or "apply_detail_effects(" in color_detail_runtime
    ):
        errors.append(
            "canonical Detail must not call the legacy approximate standalone Metal paths"
        )
    legacy_detail_tokens = (
        "apply_spatial_tone_effects",
        "apply_detail_effects",
        "extract_oklab_pipeline",
        "spatial_tone_pipeline",
        "compose_detail_pipeline",
        "struct DetailUniforms",
        "struct SpatialToneUniforms",
        "kernel void extract_oklab",
        "kernel void extract_luminance",
        "kernel void apply_spatial_tone",
        "kernel void apply_sharpen",
        "kernel void apply_luma_nr",
        "kernel void apply_color_nr",
        "kernel void compose_detail_pixels",
    )
    if any(
        token in source
        for token in legacy_detail_tokens
        for source in (metal_backend, metal_backend_header, metal_color_adjustments)
    ):
        errors.append(
            "obsolete standalone Metal Detail APIs, pipelines, and shaders must remain deleted"
        )
    if (
        "struct CurveEvaluationData final" in color_adjustments
        or "CurveEvaluationData build_curve_data(" in color_adjustments
        or "std::vector<float> build_perceptual_curve_lut(" in color_adjustments
        or "double evaluate_tone_curve_target(" in color_adjustments
        or '#include "color_curve_evaluator.h"' not in color_adjustments
    ):
        errors.append(
            "color_adjustments.cpp must not reclaim curve evaluator ownership"
        )
    if (
        "double smoothstep(" in color_adjustments
        or "double bell_weight(" in color_adjustments
        or "double circular_weight(" in color_adjustments
        or "double wrap_unit(" in color_adjustments
        or "double wrap_radians(" in color_adjustments
        or "double shortest_angular_delta(" in color_adjustments
        or '#include "color_math_runtime.h"' not in color_adjustments
    ):
        errors.append(
            "color_adjustments.cpp must route shared scalar and circular weighting math through the extracted runtime"
        )
    if (
        "struct PipelineColorContext final" in color_adjustments
        or "struct OklabColor final" in color_adjustments
        or "PipelineColorContext make_pipeline_color_context(" in color_adjustments
        or "ColorPixel apply_rgb_matrix(" in color_adjustments
        or "XyzColor pixel_to_xyz_native(" in color_adjustments
        or "OklabColor linear_rgb_to_oklab(" in color_adjustments
        or "ColorPixel oklab_to_linear_rgb(" in color_adjustments
        or "ColorPixel scale_pixel_to_luminance(" in color_adjustments
        or "ColorPixel fit_pixel_to_output_headroom(" in color_adjustments
        or "ColorPixel gamut_fit_oklch(" in color_adjustments
        or "ColorPixel gamut_fit_oklab(" in color_adjustments
        or '#include "color_working_space_runtime.h"' not in color_adjustments
    ):
        errors.append(
            "color_adjustments.cpp must route working-space context, conversion, and luminance scaling through the extracted runtime"
        )
    if (
        "bool lut_has_expected_table_size(" in color_adjustments
        or "bool lut_has_unit_domain(" in color_adjustments
        or "ColorPixel apply_lut_unchecked(" in color_adjustments
        or "ColorPixel apply_lut(" in color_adjustments
    ):
        errors.append(
            "color_adjustments.cpp must route 3D LUT validation and sampling through the extracted LUT runtime"
        )
    if (
        "struct ToneAnchor final" in color_adjustments
        or "apply_legacy_anchor_tone_curve(" in color_adjustments
        or "apply_monotone_log_luminance_tone(" in color_adjustments
        or "[[nodiscard]] double apply_global_tone(" in color_adjustments
        or "struct ToneLuminanceTableKey final" in color_adjustments
        or "double shoulder_threshold_for_max_luminance(" in color_adjustments
        or "double robust_highlight_reference_from_row_maxima(" in color_adjustments
        or "double apply_highlight_shoulder(" in color_adjustments
        or '#include "color_tone_runtime.h"' not in color_adjustments
        or "acquire_tone_luminance_table(" not in color_adjustments
        or "evaluate_tone_luminance_table(" not in color_adjustments
    ):
        errors.append(
            "color_adjustments.cpp must route scene tone through the extracted tone runtime"
        )
    if (
        "ColorPixel apply_post_tone_color_adjustments(" in color_adjustments
        or "double oklab_hue_for_rgb_hue(" in color_adjustments
        or "double highlight_chroma_compression_scale(" in color_adjustments
        or '#include "color_look_runtime.h"' not in color_adjustments
    ):
        errors.append(
            "color_adjustments.cpp must route post-tone creative color through the extracted look runtime"
        )
    if (
        "bool has_photo_layers(" in color_adjustments
        or "ColorPixel blend_layer_color(" in color_adjustments
        or "ColorAdjustmentState photo_layer_color_adjustment_state(" in color_adjustments
        or "void apply_photo_layers(" in color_adjustments
        or '#include "color_layer_runtime.h"' not in color_adjustments
    ):
        errors.append(
            "color_adjustments.cpp must route photo-layer evaluation and composition through the extracted layer runtime"
        )
    if (
        "double rgb_hue_for_mixer_selection(" in color_adjustments
        or "void apply_selective_color_adjustment(" in color_adjustments
        or "bool has_rgb_channel_mixer_adjustments(" in color_adjustments
        or "ColorPixel apply_rgb_channel_mixer(" in color_adjustments
        or "ColorPixel apply_black_and_white_mixer(" in color_adjustments
        or "double channel_weight_for_selection(" in color_adjustments
        or '#include "color_mixer_runtime.h"' not in color_adjustments
    ):
        errors.append(
            "color_adjustments.cpp must route color mixing through the extracted mixer runtime"
        )
    if (
        "constexpr double kCameraLinearHighlightClip" in color_adjustments
        or "double normalized_raw_channel(" in color_adjustments
        or "double opposed_channel_reference(" in color_adjustments
        or "HighlightReconstructionAnalysis analyze_highlight_reconstruction_impl(" in color_adjustments
        or "ColorPixel apply_camera_linear_highlight_reconstruction(" in color_adjustments
        or '#include "color_highlight_runtime.h"' not in color_adjustments
    ):
        errors.append(
            "color_adjustments.cpp must route sensor-domain highlight reconstruction through the extracted runtime"
        )
    if (
        "ColorPixel sample_bilinear(" in color_adjustments
        or "ColorPixel sample_bilinear_crop_fill(" in color_adjustments
        or "void apply_lens_vignette_correction(" in color_adjustments
        or "void apply_geometry_corrections(" in color_adjustments
        or '#include "color_spatial_runtime.h"' not in color_adjustments
    ):
        errors.append(
            "color_adjustments.cpp must route spatial sampling and optical corrections through the extracted runtime"
        )
    if (
        "bool black_point_compensation_affects_output(" in color_adjustments
        or "ColorPixel apply_black_point_compensation(" in color_adjustments
        or "OutputTransformResult apply_output_transform(" in color_adjustments
        or "void apply_output_transform(" in color_adjustments
        or "void apply_output_gamut_mapping_in_working_space(" in color_adjustments
        or "CompiledAces2OutputLutApplication compile_aces2_output_lut_application(" in color_adjustments
        or "ColorPixel apply_aces2_output_lut_target_application(" in color_adjustments
        or "GamutWarningAnalysis analyze_gamut_warnings(" in color_adjustments
        or '#include "color_output_runtime.h"' not in color_adjustments
    ):
        errors.append(
            "color_adjustments.cpp must route black-point, output conversion, gamut mapping, and ACES LUT application through the extracted output runtime"
        )
    if (
        "double local_mask_weight(" in color_adjustments
        or "double local_mask_set_weight(" in color_adjustments
        or "double composite_local_mask_weight(" in color_adjustments
        or "void apply_local_removal(" in color_adjustments
        or "void apply_local_clone(" in color_adjustments
        or "void apply_local_inpaint_patches(" in color_adjustments
        or "std::uint64_t refined_local_mask_hash_impl(" in color_adjustments
        or "std::vector<std::uint8_t> rasterize_local_mask_set_alpha_impl(" in color_adjustments
        or '#include "color_mask_runtime.h"' not in color_adjustments
    ):
        errors.append(
            "color_adjustments.cpp must route mask refinement, rasterization, and retouch execution through the extracted mask runtime"
        )
    if (
        "Matrix3 calibration_saturation_matrix(" in color_adjustments
        or "Matrix3 camera_profile_matrix(" in color_adjustments
        or "Matrix3 white_balance_matrix(" in color_adjustments
        or "InputStageParameters make_input_stage_parameters(" in color_adjustments
        or "ColorPixel apply_normalize_raw_stage(" in color_adjustments
        or "ColorPixel apply_camera_characterization_stage(" in color_adjustments
        or "ColorPixel apply_scene_adjustments_stage(" in color_adjustments
        or '#include "color_input_runtime.h"' not in color_adjustments
    ):
        errors.append(
            "color_adjustments.cpp must route RAW normalization, camera characterization, white balance, and scene input through the extracted input runtime"
        )
    if (
        "void apply_local_adjustment_mask(" in color_adjustments
        or "ColorPixel apply_global_color_pipeline(" in color_adjustments
        or "ColorPixel apply_master_rgb_point_curve(" in color_adjustments
        or "ColorPixel apply_luminance_point_curve(" in color_adjustments
        or "double perceptual_delta(" in color_adjustments
        or "ColorLut3D build_color_lut(" in color_adjustments
        or '#include "color_adjustment_kernel_runtime.h"' not in color_adjustments
    ):
        errors.append(
            "color_adjustments.cpp must route shared global/local edit evaluation and LUT construction through the extracted adjustment kernel"
        )
    if (
        "bool apply_local_adjustments_to_pixels_impl(" in color_adjustments
        or "bool apply_local_adjustments_to_look_pixels(" in color_adjustments
        or "bool apply_color_layers_stage(" in color_adjustments
        or "LocalAdjustmentFastPathResult apply_single_brush_exposure_fast_path(" in color_adjustments
        or '#include "color_local_layer_runtime.h"' not in color_adjustments
    ):
        errors.append(
            "color_adjustments.cpp must route local fast paths and Layers-stage coordination through the extracted local/layer runtime"
        )
    engine_cmake = (REPO_ROOT / "engine/CMakeLists.txt").read_text(encoding="utf-8")
    if "src/pipeline/color_control_semantics.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the extracted color-control semantics unit"
        )
    if "src/pipeline/color_adjustment_scheduler.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the extracted color-adjustment scheduler"
        )
    if "src/pipeline/color_adjustment_kernel_runtime.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the extracted shared adjustment kernel"
        )
    if "src/pipeline/color_blur_runtime.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the extracted reusable blur runtime"
        )
    if "src/pipeline/color_detail_runtime.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the extracted detail-stage runtime"
        )
    if "src/pipeline/color_fine_detail_reference.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the retained fine Detail reference"
        )
    if "src/pipeline/color_curve_evaluator.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the extracted color curve evaluator"
        )
    if "src/pipeline/color_highlight_runtime.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the extracted RAW highlight runtime"
        )
    if "src/pipeline/color_input_runtime.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the extracted camera and input-stage runtime"
        )
    if "src/pipeline/color_layer_runtime.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the extracted photo-layer runtime"
        )
    if "src/pipeline/color_look_runtime.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the extracted post-tone look runtime"
        )
    if "src/pipeline/color_local_layer_runtime.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the extracted local and Layers-stage runtime"
        )
    if "src/pipeline/color_lut_runtime.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the extracted creative LUT runtime"
        )
    if "src/pipeline/color_user_lut_program.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the extracted creative LUT program"
        )
    if "src/pipeline/color_geometry_program.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the retained Geometry program"
        )
    if "src/pipeline/color_fill_layer_program.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the retained fill-layer program"
        )
    if "src/pipeline/color_fill_layer_reference.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the retained fill-layer CPU reference"
        )
    if "src/pipeline/color_fill_layer_chroma_reference.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the retained local-chroma CPU reference"
        )
    if "src/pipeline/color_fill_layer_admission.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile retained layer admission policy"
        )
    if "src/pipeline/color_fill_layer_curve_program.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile retained layer curve program"
        )
    if "src/pipeline/color_fill_layer_look_program.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile retained layer downstream Look program"
        )
    if "src/pipeline/color_hdr_local_tone_program.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the retained HDR local-tone program"
        )
    if "src/pipeline/color_math_runtime.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the extracted shared color math runtime"
        )
    if "src/pipeline/color_working_space_runtime.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the extracted working-space color runtime"
        )
    if "src/pipeline/color_spatial_runtime.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the extracted spatial and optical runtime"
        )
    if "src/pipeline/color_spatial_detail_program.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the retained spatial Detail program"
        )
    if "src/pipeline/color_mixer_runtime.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the extracted color mixer runtime"
        )
    if "src/pipeline/color_output_runtime.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the extracted output-rendering runtime"
        )
    if "src/pipeline/color_mask_runtime.cpp" not in engine_cmake:
        errors.append(
            "photo_engine must compile the extracted mask and retouch runtime"
        )
    tone_runtime = (
        REPO_ROOT / "engine/src/pipeline/color_tone_runtime.cpp"
    ).read_text(encoding="utf-8")
    if (
        "src/pipeline/color_tone_runtime.cpp" not in engine_cmake
        or "uses_legacy_fixed_anchor_tone_runtime(" not in tone_runtime
        or "model == PhotographicRenderingModel::LegacyOklchV1" not in tone_runtime
        or "return apply_legacy_anchor_tone_curve(value, state);" not in tone_runtime
        or "return apply_monotone_log_luminance_tone(value, state);" not in tone_runtime
    ):
        errors.append(
            "the extracted tone runtime must admit fixed anchors only for explicit LegacyOklchV1 state"
        )

    recipe_header = (
        REPO_ROOT / "apps/desktop/src/edit_recipe_state.h"
    ).read_text(encoding="utf-8")
    if "#include <QSaveFile>" in recipe_header or "QSaveFile file(" in recipe_header:
        errors.append(
            "edit_recipe_state.h must not reclaim recipe sidecar file I/O"
        )
    if (
        "recipe_diff_value_summary(" in recipe_header
        or "append_recipe_json_diffs(" in recipe_header
    ):
        errors.append(
            "edit_recipe_state.h must not reclaim recursive recipe diff execution"
        )
    if "result.target_version = target_version;" in recipe_header:
        errors.append(
            "edit_recipe_state.h must not reclaim recipe migration execution"
        )
    if (
        "kMaximumPersistedDcpProfileBytes" in recipe_header
        or "kMaximumPersistedIccProfileBytes" in recipe_header
    ):
        errors.append(
            "edit_recipe_state.h must not reclaim ICC or DCP asset validation policy"
        )
    if (
        "history.snapshots[%1].adjustmentPatch must be an object" in recipe_header
        or "payload size does not match dimensions or exceeds bounds" in recipe_header
    ):
        errors.append(
            "edit_recipe_state.h must not reclaim history or mask payload validation"
        )
    if (
        "recipe JSON parse error at offset %1: %2" in recipe_header
        or "rawAdjustments.highlightReconstructionModel must be a string"
        in recipe_header
    ):
        errors.append(
            "edit_recipe_state.h must not reclaim recipe document validation"
        )
    if (
        "state.payload_sha256.trimmed().toLower()" in recipe_header
        or 'object.insert(QStringLiteral("schemaVersion"), state.schema_version)'
        in recipe_header
    ):
        errors.append(
            "edit_recipe_state.h must not reclaim ICC or DCP profile codec execution"
        )
    if (
        "parsed_points.reserve(points.size())" in recipe_header
        or "serialized_points.append(object)" in recipe_header
    ):
        errors.append(
            "edit_recipe_state.h must not reclaim curve point codec execution"
        )
    if (
        "normalize_photo_layer_payloads(" in recipe_header
        or "encode_photo_layer_payloads(" in recipe_header
    ):
        errors.append(
            "edit_recipe_state.h must not reclaim photo-layer payload codec execution"
        )
    if (
        "return legacy.value(legacy_key).toDouble(default_value)" in recipe_header
        or "QByteArray::fromBase64(object.value(key)" in recipe_header
    ):
        errors.append(
            "edit_recipe_state.h must not reclaim generic JSON reader execution"
        )
    if (
        "const auto output_transform = detail::object_value(" in recipe_header
        or "const auto primary_sections = [&state]" in recipe_header
        or "QJsonDocument(build_edit_recipe_object(state))" in recipe_header
        or "#include <QJsonDocument>" in recipe_header
    ):
        errors.append(
            "edit_recipe_state.h must not reclaim document parse or serialization execution"
        )
    if (
        'QStringLiteral("%1-snapshot-%2")' in recipe_header
        or "next.recipe_snapshots.append(snapshot.toVariantMap())" in recipe_header
    ):
        errors.append(
            "edit_recipe_state.h must not reclaim named snapshot serialization"
        )
    if (
        'normalized == QStringLiteral("legacy-oklch-v1")' in recipe_header
        or 'normalized == QStringLiteral("legacy-working-rgb-v1")'
        in recipe_header
        or "inline QString normalized_output_color_space_name(" in recipe_header
        or "inline QString normalized_input_color_profile_name(" in recipe_header
        or "inline QString normalized_raw_demosaic_quality_name(" in recipe_header
        or "#include <QFileInfo>" in recipe_header
    ):
        errors.append(
            "edit_recipe_state.h must not reclaim renderer or color-policy normalization"
        )
    if (
        "normalized_groups.push_back(normalized)" in recipe_header
        or "for (const auto& candidate : all_adjustment_groups())"
        in recipe_header
        or "merged.input_color_profile = source.input_color_profile" in recipe_header
    ):
        errors.append(
            "edit_recipe_state.h must not reclaim adjustment-group normalization or merging"
        )
    qimage_conversion_owner = (
        REPO_ROOT / "apps/desktop/src/qimage_color_conversion.h"
    )
    direct_qimage_conversion_tokens = (
        ".convertedToColorSpace(",
        ".convertToColorSpace(",
    )
    for path in (REPO_ROOT / "apps/desktop/src").rglob("*"):
        if path.suffix not in {".cpp", ".h", ".mm"} or path == qimage_conversion_owner:
            continue
        source = path.read_text(encoding="utf-8")
        if any(token in source for token in direct_qimage_conversion_tokens):
            errors.append(
                f"{path.relative_to(REPO_ROOT)} bypasses the explicit-purpose QImage color conversion boundary"
            )
    qimage_conversion_source = qimage_conversion_owner.read_text(encoding="utf-8")
    if qimage_conversion_source.count(".convertedToColorSpace(") != 2:
        errors.append(
            "qimage_color_conversion.h must remain the sole two-overload Qt color conversion owner"
        )
    desktop_cmake = (
        REPO_ROOT / "apps/desktop/CMakeLists.txt"
    ).read_text(encoding="utf-8")
    preview_controller_source = "\n".join(
        (
            (
                REPO_ROOT / "apps/desktop/src/editor_preview_controller.cpp"
            ).read_text(encoding="utf-8"),
            (
                REPO_ROOT
                / "apps/desktop/src/editor_preview_histogram_controller.cpp"
            ).read_text(encoding="utf-8"),
        )
    )
    preview_display_publication = (
        REPO_ROOT
        / "apps/desktop/src/preview_display_publication.cpp"
    ).read_text(encoding="utf-8")
    preview_output_color_space = (
        REPO_ROOT
        / "apps/desktop/src/preview_output_color_space.h"
    ).read_text(encoding="utf-8")
    if (
        "QImage normalize_preview_color_space(QImage" in preview_controller_source
        or "apply_output_color_space_transform(" in preview_controller_source
        or "QColorSpace preview_output_color_space_for_name(" in
        preview_controller_source
        or "QColorSpace preview_output_color_space_for_name(" in
        photo_render_utils
        or "publish_preview_image_to_display(" not in preview_controller_source
        or "QImageColorConversionPurpose::OutputPublication" not in
        preview_display_publication
        or "QImageColorConversionPurpose::MonitorPublication" not in
        preview_display_publication
        or "resolve_icc_profile_asset(" not in preview_display_publication
        or "!state.soft_proof_enabled" not in preview_display_publication
        or "preview_output_color_space_for_name(" not in
        preview_output_color_space
        or "src/preview_display_publication.cpp" not in desktop_cmake
        or "src/preview_output_color_space.h" not in desktop_cmake
        or "preview_display_publication_test" not in tests_cmake
    ):
        errors.append(
            "preview output and monitor ICC publication must remain in the focused final-display owner with one shared output-space mapper"
        )
    brush_tile_admission = preview_controller_source.split(
        "bool state_supports_brush_adjusted_tile_patch", 1
    )
    brush_tile_admission_body = (
        brush_tile_admission[1].split(
            "EditState base_state_for_brush_adjusted_tile_patch", 1
        )[0]
        if len(brush_tile_admission) == 2
        else ""
    )
    exact_cover_patch = preview_controller_source.split(
        "TiledWorkingImageRenderResult qimage_from_exact_cover_dirty_working_patch", 1
    )
    exact_cover_patch_body = (
        exact_cover_patch[1].split(
            "TiledWorkingImageRenderResult qimage_from_dirty_working_tile_overrides", 1
        )[0]
        if len(exact_cover_patch) == 2
        else ""
    )
    native_output_publication = (
        REPO_ROOT
        / "apps/desktop/src/native_output_surface_publication.h"
    ).read_text(encoding="utf-8")
    if (
        'QStringLiteral("aces2-ocio-v1")' not in brush_tile_admission_body
        or 'QStringLiteral("legacy-oklch-v1")' in brush_tile_admission_body
        or "publish_working_patch(" not in exact_cover_patch_body
        or "qimage_from_working_image(" not in native_output_publication
        or "publish_native_aces_srgb8_surface(" not in native_output_publication
        or "working_image_from_qimage(linear_patch" in exact_cover_patch_body
        or "converted_to_color_space(linear_patch" in exact_cover_patch_body
    ):
        errors.append(
            "localized brush tile reuse must admit the shipping ACES renderer and execute its photographic output from float working pixels"
        )
    adjustment_patch_source = (
        REPO_ROOT
        / "apps/desktop/src/edit_adjustment_patch.cpp"
    ).read_text(encoding="utf-8")
    render_plan_store_source = (
        REPO_ROOT
        / "apps/desktop/src/render_plan_store.cpp"
    ).read_text(encoding="utf-8")
    recipe_v13_adapter_source = (
        REPO_ROOT
        / "apps/desktop/src/edit_recipe_v13_adapter.cpp"
    ).read_text(encoding="utf-8")
    history_snapshot_store = (
        REPO_ROOT
        / "apps/desktop/src/edit_history_snapshot_store.cpp"
    ).read_text(encoding="utf-8")
    if (
        "preset_object.insert(QStringLiteral(\"recipeJson\")"
        in preview_controller_source
        or "default_object.insert(QStringLiteral(\"recipeJson\")"
        in preview_controller_source
        or "photo-editor-adjustment-patch"
        not in adjustment_patch_source
        or "build_edit_adjustment_state_object("
        not in adjustment_patch_source
        or "parse_edit_adjustment_state_object(state_object)"
        not in adjustment_patch_source
        or "build_edit_recipe_object(" in adjustment_patch_source
        or "serialize_edit_recipe_json(" in adjustment_patch_source
        or adjustment_patch_source.count("parse_edit_recipe_json(") != 2
        or "serialize_edit_recipe_json(" in render_plan_store_source
        or "build_edit_recipe_object(" in render_plan_store_source
        or "serialize_edit_recipe_json(" in recipe_v13_adapter_source
        or "build_edit_recipe_object(" in recipe_v13_adapter_source
        or "Adjustment patch cannot embed a legacy recipe."
        not in adjustment_patch_source
        or "src/edit_adjustment_patch.cpp" not in desktop_cmake
    ):
        errors.append(
            "modern patches and render identities must use the versionless adjustment-state codec; recipeJson is compatibility-read-only"
        )
    if (
        "recipeJsonForState(snapshot.state)"
        in preview_controller_source
        or "photo-editor-edit-history-snapshots"
        not in history_snapshot_store
        or "AdjustmentPatchScope::AssetBound"
        not in history_snapshot_store
        or "src/edit_history_snapshot_store.cpp"
        not in desktop_cmake
    ):
        errors.append(
            "edit history snapshots must write asset-bound patches through the extracted store"
        )
    render_executor_owner = (
        REPO_ROOT / "apps/desktop/src/render_executor_service.cpp"
    )
    for path in (REPO_ROOT / "apps/desktop/src").rglob("*"):
        if path.suffix not in {".cpp", ".h", ".mm"} or path == render_executor_owner:
            continue
        source = path.read_text(encoding="utf-8")
        if (
            "RenderExecutor executor(" in source
            or "preview_render_executor() {" in source
            or "export_render_executor() {" in source
            or "prewarm_default_aces2_output_luts() {" in source
        ):
            errors.append(
                f"{path.relative_to(REPO_ROOT)} bypasses centralized render executor ownership"
            )
    render_executor_source = render_executor_owner.read_text(encoding="utf-8")
    if (
        render_executor_source.count("RenderExecutor executor(") != 2
        or "kPreviewRenderExecutorCacheBytes" not in render_executor_source
        or "kExportRenderExecutorCacheBytes" not in render_executor_source
        or "src/render_executor_service.cpp" not in desktop_cmake
    ):
        errors.append(
            "render_executor_service must remain the published preview/export executor owner"
        )
    canonical_v13_persistence_owner = (
        REPO_ROOT
        / "apps/desktop/src/edit_recipe_v13_persistence.cpp"
    )
    for path in (REPO_ROOT / "apps/desktop/src").rglob("*"):
        if (
            path.suffix not in {".cpp", ".h", ".mm"}
            or path == canonical_v13_persistence_owner
        ):
            continue
        source = path.read_text(encoding="utf-8")
        if (
            "serialize_edit_recipe_v13(" in source
            or "edit_recipe_v13_cache_key(" in source
        ):
            errors.append(
                f"{path.relative_to(REPO_ROOT)} bypasses canonical v13 persistence"
            )
    canonical_v13_persistence_source = (
        canonical_v13_persistence_owner.read_text(
            encoding="utf-8"
        )
    )
    if (
        "adapt_desktop_recipe_to_v13(" not in
        canonical_v13_persistence_source
        or "validate_edit_recipe_v13(" not in
        canonical_v13_persistence_source
        or "serialize_edit_recipe_v13(" not in
        canonical_v13_persistence_source
        or "edit_recipe_v13_cache_key(" not in
        canonical_v13_persistence_source
        or "serialize_desktop_recipe_as_canonical_v13_with_assets(" not in
        canonical_v13_persistence_source
        or "project_canonical_v13_to_desktop_state_with_assets(" not in
        canonical_v13_persistence_source
        or "src/edit_recipe_v13_persistence.cpp" not in
        desktop_cmake
    ):
        errors.append(
            "edit_recipe_v13_persistence must remain the published fail-closed canonical writer"
        )
    desktop_persistence_service = (
        REPO_ROOT
        / "apps/desktop/src/desktop_recipe_persistence_service.cpp"
    ).read_text(encoding="utf-8")
    editor_persistence_launcher = (
        REPO_ROOT
        / "apps/desktop/src/editor_recipe_persistence_launcher.h"
    ).read_text(encoding="utf-8")
    save_recipe_helper = (
        REPO_ROOT
        / "rust/crates/catalog/src/bin/save_edit_recipe.rs"
    ).read_text(encoding="utf-8")
    accept_recipe_migration_helper = (
        REPO_ROOT
        / "rust/crates/catalog/src/bin/accept_recipe_migration.rs"
    ).read_text(encoding="utf-8")
    packaging_cmake = (
        REPO_ROOT / "cmake/Packaging.cmake"
    ).read_text(encoding="utf-8")
    macos_runtime_pruner = (
        REPO_ROOT / "cmake/PruneQtMacosRuntime.cmake"
    ).read_text(encoding="utf-8")
    macos_bundle_signer = (
        REPO_ROOT / "scripts/sign-macos-bundle.sh"
    ).read_text(encoding="utf-8")
    desktop_qml_source = "\n".join(
        path.read_text(encoding="utf-8")
        for path in sorted((REPO_ROOT / "apps/desktop/qml").rglob("*.qml"))
    )
    desktop_native_source = "\n".join(
        path.read_text(encoding="utf-8")
        for suffix in ("*.cpp", "*.h")
        for path in sorted((REPO_ROOT / "apps/desktop/src").rglob(suffix))
    )
    catalog_helpers_manifest = (
        REPO_ROOT / "cmake/CatalogHelpers.txt"
    ).read_text(encoding="utf-8").splitlines()
    expected_catalog_helpers = {
        "accept_recipe_migration",
        "catalog_carousel_documents",
        "catalog_export_jobs",
        "catalog_integrity",
        "catalog_local_db",
        "catalog_migrate",
        "catalog_migration_inventory",
        "catalog_move_library",
        "catalog_snapshot",
        "catalog_state",
        "catalog_timeline_documents",
        "import_catalog",
        "save_edit_recipe",
    }
    if set(catalog_helpers_manifest) != expected_catalog_helpers:
        errors.append(
            "the canonical packaged catalog-helper manifest must match the supported helper fleet"
        )
    required_macos_pruned_runtime = {
        "libqpdf.dylib",
        "QtPdf.framework",
        "libqicns.dylib",
        "libqico.dylib",
        "libqjp2.dylib",
        "libqmng.dylib",
        "libqtga.dylib",
        "libqwbmp.dylib",
        "libmng.2.dylib",
        "libqtvirtualkeyboardplugin.dylib",
        "QtVirtualKeyboard.framework",
        "QtVirtualKeyboardQml.framework",
        "libqglib.dylib",
        "libgio-2.0.0.dylib",
        "libgobject-2.0.0.dylib",
        "libgmodule-2.0.0.dylib",
        "libqopensslbackend.dylib",
    }
    if (
        not all(marker in macos_runtime_pruner for marker in required_macos_pruned_runtime)
        or "framehouse_prune_deployed_qt_macos_runtime" not in packaging_cmake
        or "framehouse_reseal_macos_bundle_at_path" not in packaging_cmake
        or "sign-macos-bundle.sh" not in macos_runtime_pruner
        or "sign-macos-bundle.sh" not in (
            REPO_ROOT / "scripts/sign-notarize-macos.sh"
        ).read_text(encoding="utf-8")
        or "find \"${bundle}\" -type f" not in macos_bundle_signer
        or "sort -rz" not in macos_bundle_signer
        or "codesign --verify --deep --strict" not in macos_bundle_signer
        or "QtQuick.VirtualKeyboard" in desktop_qml_source
        or "QPdfDocument" in desktop_native_source
        or "QPdfView" in desktop_native_source
    ):
        errors.append(
            "macOS Qt runtime pruning must stay coupled to the no-PDF-import, no-virtual-keyboard capability boundary"
        )
    catalog_state_helper = (
        REPO_ROOT
        / "rust/crates/catalog/src/bin/catalog_state.rs"
    ).read_text(encoding="utf-8")
    catalog_library = (
        REPO_ROOT / "rust/crates/catalog/src/lib.rs"
    ).read_text(encoding="utf-8")
    local_database_manager = (
        REPO_ROOT / "rust/crates/catalog/src/local_database.rs"
    ).read_text(encoding="utf-8")
    catalog_library_move = (
        REPO_ROOT / "rust/crates/catalog/src/bin/catalog_move_library.rs"
    ).read_text(encoding="utf-8")
    release_builder = (
        REPO_ROOT / "scripts/build-release.sh"
    ).read_text(encoding="utf-8")
    macos_release_verifier = (
        REPO_ROOT / "scripts/verify-macos-release.sh"
    ).read_text(encoding="utf-8")
    postgres_runtime_preparer = (
        REPO_ROOT / "scripts/prepare-postgres-runtime.sh"
    ).read_text(encoding="utf-8")
    macos_postgres_runtime_preparer = (
        REPO_ROOT / "scripts/prepare-macos-postgres-runtime.sh"
    ).read_text(encoding="utf-8")
    release_checklist = (
        REPO_ROOT / "docs/release-checklist.md"
    ).read_text(encoding="utf-8")
    shipping_guide = (
        REPO_ROOT / "docs/shipping-guide.md"
    ).read_text(encoding="utf-8")
    ci_workflow = (
        REPO_ROOT / ".github/workflows/ci.yml"
    ).read_text(encoding="utf-8")
    release_workflow = (
        REPO_ROOT / ".github/workflows/release.yml"
    ).read_text(encoding="utf-8")
    postgres_runtime_proof = (
        REPO_ROOT / "scripts/test-postgres-catalog-runtime.sh"
    ).read_text(encoding="utf-8")
    postgres_large_library_proof = (
        REPO_ROOT / "scripts/test-postgres-large-library.py"
    ).read_text(encoding="utf-8")
    windows_postgres_runtime_proof = (
        REPO_ROOT / "scripts/test-postgres-catalog-runtime.ps1"
    ).read_text(encoding="utf-8")
    postgres_upgrade_rehearsal = (
        REPO_ROOT / "scripts/rehearse-postgres-catalog-upgrade.sh"
    ).read_text(encoding="utf-8")
    postgres_rendering_migration_rehearsal = (
        REPO_ROOT
        / "scripts/rehearse-postgres-rendering-migration-batch.sh"
    ).read_text(encoding="utf-8")
    rendering_migration_assessor = (
        REPO_ROOT
        / "apps/desktop/src/rendering_migration_asset_assessor.cpp"
    ).read_text(encoding="utf-8")
    rendering_migration_batch_report = (
        REPO_ROOT
        / "apps/desktop/src/rendering_migration_batch_report.cpp"
    ).read_text(encoding="utf-8")
    rendering_migration_batch_command = (
        REPO_ROOT
        / "apps/desktop/src/rendering_migration_batch_command.cpp"
    ).read_text(encoding="utf-8")
    rendering_migration_batch_runner = (
        REPO_ROOT
        / "apps/desktop/src/rendering_migration_batch_runner.cpp"
    ).read_text(encoding="utf-8")
    rendering_migration_chunk_receipt = (
        REPO_ROOT
        / "apps/desktop/src/rendering_migration_chunk_receipt.cpp"
    ).read_text(encoding="utf-8")
    desktop_main = (
        REPO_ROOT / "apps/desktop/src/main.cpp"
    ).read_text(encoding="utf-8")
    rendering_migration_review_packet = (
        REPO_ROOT
        / "scripts/color/rendering-migration-review-packet.py"
    ).read_text(encoding="utf-8")
    rendering_migration_refresh_planner = (
        REPO_ROOT
        / "scripts/color/plan-rendering-migration-refresh.py"
    ).read_text(encoding="utf-8")
    rendering_migration_report_identity = (
        REPO_ROOT
        / "scripts/color/rendering_migration_report_identity.py"
    ).read_text(encoding="utf-8")
    rendering_migration_chunk_validator = (
        REPO_ROOT
        / "scripts/color/validate-rendering-migration-refresh-chunk.py"
    ).read_text(encoding="utf-8")
    rendering_migration_chunk_authorizer = (
        REPO_ROOT
        / "scripts/color/authorize-rendering-migration-refresh-chunk.py"
    ).read_text(encoding="utf-8")
    rendering_migration_pair_diagnosis = (
        REPO_ROOT
        / "scripts/color/diagnose-rendering-migration-pairs.py"
    ).read_text(encoding="utf-8")
    color_pipeline_header = (
        REPO_ROOT
        / "engine/include/photo_editor/pipeline/color_pipeline.h"
    ).read_text(encoding="utf-8")
    canonical_recipe_source = (
        REPO_ROOT / "engine/src/pipeline/edit_recipe_v13.cpp"
    ).read_text(encoding="utf-8")
    recipe_compiler_source = (
        REPO_ROOT / "engine/src/pipeline/recipe_compiler.cpp"
    ).read_text(encoding="utf-8")
    engine_render_executor_source = (
        REPO_ROOT / "engine/src/pipeline/render_executor.cpp"
    ).read_text(encoding="utf-8")
    editor_rendering_migration = (
        REPO_ROOT / "apps/desktop/src/editor_rendering_migration.inc"
    ).read_text(encoding="utf-8")
    rendering_migration_workflow_qml = (
        REPO_ROOT / "apps/desktop/qml/workspaces/StudioWorkspace.qml"
    ).read_text(encoding="utf-8")
    bounded_qimage_reader = (
        REPO_ROOT
        / "apps/desktop/src/bounded_qimage_reader.h"
    ).read_text(encoding="utf-8")
    hdr_avif_source = (
        REPO_ROOT / "engine/src/io/hdr_avif_io.cpp"
    ).read_text(encoding="utf-8")
    export_image_settings = (
        REPO_ROOT / "apps/desktop/src/export_image_settings.h"
    ).read_text(encoding="utf-8")
    catalog_recipe_projection = (
        REPO_ROOT
        / "apps/desktop/src/catalog_recipe_projection.cpp"
    ).read_text(encoding="utf-8")
    catalog_recipe_projection_header = (
        REPO_ROOT
        / "apps/desktop/src/catalog_recipe_projection.h"
    ).read_text(encoding="utf-8")
    if (
        "prepare_desktop_recipe_persistence(" not in
        desktop_persistence_service
        or "serialize_desktop_recipe_as_canonical_v13_with_assets(" not in
        desktop_persistence_service
        or "prepare_desktop_recipe_persistence(" not in
        editor_persistence_launcher
        or "upsert_edit_recipe_compatibility_pair(" not in
        save_recipe_helper
        or 'ok_or("missing canonical_recipe_payload argument")' not in
        save_recipe_helper
        or '"set-edit-recipe" =>' in
        catalog_state_helper
        or '"set-recipe-migration" =>' not in
        catalog_state_helper
        or "upsert_recipe_migration_record(" not in catalog_library
        or "upsert_edit_recipe_compatibility_pair(" not in catalog_library
        or "catalog.backup(&backup_path)" not in
        accept_recipe_migration_helper
        or "verify_evidence_identity(evidence_bytes, evidence_hash)" not in
        accept_recipe_migration_helper
        or ".accept_recipe_migration(" not in
        accept_recipe_migration_helper
        or "accept_recipe_migration" not in
        editor_persistence_launcher
        or "CatalogHelpers.txt" not in packaging_cmake
        or "project_canonical_v13_document_to_desktop_state_with_assets(" not in
        catalog_recipe_projection
        or "serialize_adjustment_patch_json(" not in catalog_recipe_projection
        or "serialize_edit_recipe_json(" in catalog_recipe_projection
        or "project_catalog_adjustments_for_desktop(" not in
        catalog_recipe_projection_header
        or "adjustment_patch_json" not in catalog_recipe_projection_header
        or "recipe_json" in catalog_recipe_projection_header
        or "src/catalog_recipe_projection.cpp" not in
        desktop_cmake
        or "src/desktop_recipe_persistence_service.cpp" not in
        desktop_cmake
    ):
        errors.append(
            "desktop/catalog writes must remain canonical-v13-first with no legacy-only helper command"
        )
    if (
        "repository: CatalogService" not in catalog_library
        or '"postgresql"' not in catalog_library
        or "canonical.provenance_payload::TEXT AS canonical_provenance_payload" not in
        catalog_library
        or "LocalCatalogDatabaseManager::from_env()" not in catalog_library
        or "ensure_ready_database_url(" not in local_database_manager
        or "PHOTO_EDITOR_DATABASE_URL" not in local_database_manager
        or "pg_dump" not in local_database_manager
        or "self.publish_backup_archive(" not in local_database_manager
        or "fs::hard_link(&partial_path, destination)" not in local_database_manager
        or "fs::rename(partial_path" in local_database_manager
        or "interrupted_backup_removes_partial_archive_and_never_publishes_destination" not in
        local_database_manager
        or "completed_backup_is_owner_only_atomically_published_and_never_overwritten" not in
        local_database_manager
        or "LOCAL_POSTGRES_LEGACY_CREDENTIAL_FILE" not in local_database_manager
        or "recover_legacy_credential_if_needed" not in local_database_manager
        or "refusing to generate replacement authority" not in local_database_manager
        or "invalid_state_cannot_replace_existing_postgres_authority" not in
        local_database_manager
        or "PHOTO_EDITOR_POSTGRES_RUNTIME_DIR" not in packaging_cmake
        or "USE_SOURCE_PERMISSIONS" not in packaging_cmake
        or "foreach(postgres_tool IN ITEMS initdb pg_ctl pg_dump pg_restore postgres psql)" not in
        packaging_cmake
        or "PHOTO_EDITOR_MACOS_APPCAST_URL" not in packaging_cmake
        or "PHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY" not in packaging_cmake
        or "CatalogHelpers.txt" not in macos_release_verifier
        or "for postgres_tool in initdb pg_ctl pg_dump pg_restore postgres psql" not in
        macos_release_verifier
        or "scripts/test-postgres-catalog-runtime.sh" not in macos_release_verifier
        or 'PHOTO_EDITOR_CATALOG_BIN_DIR="${app_path}/Contents/MacOS"' not in
        macos_release_verifier
        or 'PHOTO_EDITOR_POSTGRES_RUNTIME_DIR="${postgres_root}"' not in
        macos_release_verifier
        or "Contents/Frameworks/Sparkle.framework" not in macos_release_verifier
        or "plutil -extract SUFeedURL raw" not in macos_release_verifier
        or "plutil -extract SUPublicEDKey raw" not in macos_release_verifier
        or "A PostgreSQL runtime containing initdb, pg_ctl, pg_dump, pg_restore, postgres, and psql" not in
        release_builder
        or "postgres_runtime_has_forbidden_macos_paths" not in release_builder
        or "prepare-macos-postgres-runtime.sh" not in release_builder
        or "Relocating PostgreSQL and its non-system dylibs" not in release_builder
        or "share/**/postgres.bki" not in release_builder
        or "Set FRAMEHOUSE_LEGAL_PUBLISHER" not in release_builder
        or "Production macOS packaging requires PHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR" not in
        release_builder
        or "Unable to locate a PostgreSQL runtime root containing initdb, pg_ctl, pg_dump, pg_restore, postgres, psql, and share/**/postgres.bki" not in
        postgres_runtime_preparer
        or "install_name_tool -change" not in macos_postgres_runtime_preparer
        or "codesign --force --sign -" not in macos_postgres_runtime_preparer
        or "for tool in postgres initdb pg_ctl pg_dump pg_restore psql" not in
        macos_postgres_runtime_preparer
        or "-name '*.a' -delete" not in macos_postgres_runtime_preparer
        or "-name pkgconfig" not in macos_postgres_runtime_preparer
        or "-name '*.dylib' -delete" not in macos_postgres_runtime_preparer
        or "-name doc -o -name man" not in macos_postgres_runtime_preparer
        or "relocated runtime retains package-manager load paths" not in
        macos_postgres_runtime_preparer
        or "Prepare relocatable macOS PostgreSQL runtime" not in release_workflow
        or "scripts/prepare-macos-postgres-runtime.sh" not in release_workflow
        or 'destination_runtime="${RUNNER_TEMP}/looksy-postgres-macos"' not in
        release_workflow
        or "SqliteCatalogRepository" in catalog_library
        or "sqlite_catalog_repository" in catalog_library
        or (REPO_ROOT / "rust/crates/catalog/src/sqlite_catalog_repository.rs").exists()
        or (REPO_ROOT / "rust/crates/catalog/src/bin/catalog_migrate_local.rs").exists()
        or "licensed self-contained PostgreSQL runtime" not in release_checklist
        or "no rejected SQLite authority" not in release_checklist
        or "no legacy PostgreSQL runtime" in release_checklist
        or 'mode: "local-managed"' not in shipping_guide
        or 'healthy: true' not in shipping_guide
        or "schema version 10" in shipping_guide
        or "libsqlite3-dev" in ci_workflow
        or "Prove managed and external PostgreSQL catalog authority" not in ci_workflow
        or "scripts/test-postgres-catalog-runtime.sh" not in ci_workflow
        or "pg_config --bindir" not in ci_workflow
        or "postgres_catalog_managed_backup_restore=passed" not in postgres_runtime_proof
        or "postgres_catalog_credential_recovery=passed" not in postgres_runtime_proof
        or "postgres_catalog_pre_migration_rollback=passed" not in postgres_runtime_proof
        or "postgres_catalog_corrupt_restore_guard=passed" not in postgres_runtime_proof
        or "postgres_catalog_interrupted_backup_guard=passed" not in postgres_runtime_proof
        or "postgres_catalog_failed_restore_rollback=passed" not in postgres_runtime_proof
        or "postgres_catalog_media_relink_dedup=passed" not in postgres_runtime_proof
        or "postgres_catalog_managed_library_move=passed" not in postgres_runtime_proof
        or "TemporaryDirectoryCleanup" not in catalog_library_move
        or "fs::rename(&staging_root, &new_root)" not in catalog_library_move
        or "postgres_catalog_external_authority=passed" not in postgres_runtime_proof
        or "postgres_catalog_external_restore_guard=passed" not in postgres_runtime_proof
        or "postgres_catalog_corrupt_decode_guard=passed" not in postgres_runtime_proof
        or "photo-editor-postgres-large-library-proof:v1" not in
        postgres_large_library_proof
        or "DEFAULT_ASSET_COUNT = 100_000" not in postgres_large_library_proof
        or "DEFAULT_EDITED_COUNT = 10_000" not in postgres_large_library_proof
        or "timeout=45.0" not in postgres_large_library_proof
        or '"peakRssBytes"' not in postgres_large_library_proof
        or '"scriptSha256"' not in postgres_large_library_proof
        or '"fixtureSqlSha256"' not in postgres_large_library_proof
        or "postgres_large_library_proof=passed" not in postgres_large_library_proof
        or "Prove 100k-asset PostgreSQL catalog scale" not in ci_workflow
        or "scripts/test-postgres-large-library.py" not in ci_workflow
        or "postgres-large-library-${{ github.sha }}" not in ci_workflow
        or "test-postgres-large-library.py" not in release_checklist
        or "PHOTO_EDITOR_POSTGRES_RUNTIME_DIR" not in postgres_runtime_proof
        or "scripts/test-postgres-catalog-runtime.sh" not in release_checklist
        or "postgres_catalog_managed_backup_restore=passed" not in
        windows_postgres_runtime_proof
        or "postgres_catalog_credential_recovery=passed" not in
        windows_postgres_runtime_proof
        or "postgres_catalog_pre_migration_rollback=passed" not in
        windows_postgres_runtime_proof
        or "postgres_catalog_corrupt_restore_guard=passed" not in
        windows_postgres_runtime_proof
        or "postgres_catalog_interrupted_backup_guard=passed" not in
        windows_postgres_runtime_proof
        or "postgres_catalog_failed_restore_rollback=passed" not in
        windows_postgres_runtime_proof
        or "postgres_catalog_media_relink_dedup=passed" not in
        windows_postgres_runtime_proof
        or "postgres_catalog_managed_library_move=passed" not in
        windows_postgres_runtime_proof
        or "postgres_catalog_external_authority=passed" not in
        windows_postgres_runtime_proof
        or "postgres_catalog_external_restore_guard=passed" not in
        windows_postgres_runtime_proof
        or "postgres_catalog_corrupt_decode_guard=passed" not in
        windows_postgres_runtime_proof
        or "Assert-CatalogIntegrityFailure" not in
        windows_postgres_runtime_proof
        or '"catalog_integrity"' not in windows_postgres_runtime_proof
        or "recipe_migration_records_profile_identities_array_check" not in
        windows_postgres_runtime_proof
        or '"corrupt-export", "corrupt-timeline", "corrupt-carousel"' not in
        windows_postgres_runtime_proof
        or "postgres_catalog_real_backup_upgrade_rehearsal=passed" not in
        postgres_upgrade_rehearsal
        or "postgres_catalog_real_backup_rollback_rehearsal=passed" not in
        postgres_upgrade_rehearsal
        or 'jq -r \'.maximum\')" = "14"' not in postgres_upgrade_rehearsal
        or "upgrade did not reach schema migration 14" not in
        postgres_upgrade_rehearsal
        or "catalog_migration_inventory" not in postgres_upgrade_rehearsal
        or "DROP DATABASE ${database_name}" not in postgres_upgrade_rehearsal
        or "postgres_rendering_migration_rehearsal=passed" not in
        postgres_rendering_migration_rehearsal
        or "DELETE FROM edit_recipes WHERE asset_id NOT IN" not in
        postgres_rendering_migration_rehearsal
        or 'if [ "${asset_id_count}" -gt 0 ]; then' not in
        postgres_rendering_migration_rehearsal
        or "--rendering-migration-plan-only" not in
        postgres_rendering_migration_rehearsal
        or "apply_candidate_structural_migrations(" not in
        rendering_migration_assessor
        or "structuralSemanticMigrations" not in
        rendering_migration_assessor
        or "test-postgres-catalog-runtime.ps1" not in release_workflow
        or "Verify Windows PostgreSQL catalog runtime" not in release_workflow
    ):
        errors.append(
            "PostgreSQL must remain the sole desktop catalog authority with managed/external startup, backups, executable packaged runtime support, complete release verification, and no SQLite production authority"
        )
    if (
        'encoder.get(), "chroma", "444"' not in hdr_avif_source
        or "heif_encoder_set_lossless(encoder.get(), 1)" not in
        hdr_avif_source
        or ".jpeg_quality = 100" not in export_image_settings
    ):
        errors.append(
            "10-bit HDR AVIF delivery must require quality-100 lossless 4:4:4 encoding instead of chroma-subsampled migration evidence"
        )
    if (
        "merge_rendering_migration_batch_reports(" not in
        rendering_migration_batch_report
        or "componentReportSha256s" not in
        rendering_migration_batch_report
        or "overlay migration report changed source identity" not in
        rendering_migration_batch_report
        or "photo-editor-rendering-migration-batch-report:v2" not in
        rendering_migration_batch_report
        or "photo-editor-rendering-migration-batch-producer:v1" not in
        rendering_migration_batch_report
        or "migration reports were produced by different executables" not in
        rendering_migration_batch_report
        or "QCoreApplication::applicationFilePath()" not in
        rendering_migration_batch_command
        or ".executable_sha256 = executable_sha256" not in
        rendering_migration_batch_command
        or "validate_rendering_migration_component_receipts(" not in
        rendering_migration_batch_command
        or "rendering migration requires an approved chunk authorization" not in
        rendering_migration_batch_command
        or "authorization.asset_ids" not in rendering_migration_batch_command
        or "migration authorization contains assets absent from the catalog" not in
        rendering_migration_batch_runner
        or "photo-editor-rendering-migration-refresh-chunk-authorization:v1" not in
        rendering_migration_chunk_receipt
        or "migration chunk authorization identity is invalid" not in
        rendering_migration_chunk_receipt
        or "photo-editor-rendering-migration-refresh-chunk-authorization:v1" not in
        rendering_migration_chunk_authorizer
        or "validator.validate_plan(plan_path, executable)" not in
        rendering_migration_chunk_authorizer
        or "photo-editor-rendering-migration-refresh-chunk-receipt:v1" not in
        rendering_migration_chunk_receipt
        or "one receipt for every overlay component" not in
        rendering_migration_chunk_receipt
        or "receipt does not match its report" not in
        rendering_migration_chunk_receipt
        or "receipts do not belong to one refresh plan" not in
        rendering_migration_chunk_receipt
        or "duplicate chunk index" not in rendering_migration_chunk_receipt
        or "--rendering-migration-component-receipt" not in desktop_main
        or "--rendering-migration-chunk-authorization" not in desktop_main
        or "--chunk-authorization" not in postgres_rendering_migration_rehearsal
        or "photo-editor-rendering-migration-review-packet:v1" not in
        rendering_migration_review_packet
        or "photo-editor-rendering-migration-review-ratings:v1" not in
        rendering_migration_review_packet
        or "photo-editor-rendering-migration-review-ui:v2" not in
        rendering_migration_review_packet
        or "human review is incomplete" not in
        rendering_migration_review_packet
        or 'commands.add_parser("archive")' not in
        rendering_migration_review_packet
        or 'commands.add_parser("validate-archive")' not in
        rendering_migration_review_packet
        or 'commands.add_parser("record-ratings")' not in
        rendering_migration_review_packet
        or 'validate.add_argument("--ratings"' not in
        rendering_migration_review_packet
        or 'validate.add_argument("--require-approved"' not in
        rendering_migration_review_packet
        or "human review is not approved" not in
        rendering_migration_review_packet
        or "REVIEW_ARTIFACT_ROLES" not in rendering_migration_review_packet
        or "packet must contain every required review artifact" not in
        rendering_migration_review_packet
        or "completed_ratings_sha256=" not in
        rendering_migration_review_packet
        or "os.link(temporary_path, output)" not in
        rendering_migration_review_packet
        or "review packet archive contains missing or unexpected files" not in
        rendering_migration_review_packet
        or "packet artifact path is unsafe" not in
        rendering_migration_review_packet
        or "from rendering_migration_report_identity import validate_report_identity" not in
        rendering_migration_review_packet
        or "rendering_migration_review_packet_test" not in tests_cmake
        or "photo-editor-rendering-migration-refresh-plan:v2" not in
        rendering_migration_refresh_planner
        or "single-asset-explicit-overage" not in
        rendering_migration_refresh_planner
        or "from rendering_migration_report_identity import validate_report_identity" not in
        rendering_migration_refresh_planner
        or "rendering-migration report identity is invalid" not in
        rendering_migration_report_identity
        or "raw.count(marker) != 1" not in rendering_migration_report_identity
        or "hashlib.sha256(canonical).hexdigest() != claimed" not in
        rendering_migration_report_identity
        or "allow_historical_v1=True" not in rendering_migration_refresh_planner
        or "historical v1 report cannot prove an executable identity" not in
        rendering_migration_report_identity
        or "validate_human_approval(" not in rendering_migration_refresh_planner
        or "unanimously approved independent review is required" not in
        rendering_migration_refresh_planner
        or '"humanApprovalGate"' not in rendering_migration_refresh_planner
        or "os.O_EXCL" not in rendering_migration_refresh_planner
        or "publish_new_file" not in rendering_migration_refresh_planner
        or '"ratingsFileSha256"' not in rendering_migration_refresh_planner
        or '"validatorScriptSha256"' not in rendering_migration_refresh_planner
        or '"status": "unanimously-approved"' not in
        rendering_migration_refresh_planner
        or "plan_rendering_migration_refresh_test" not in tests_cmake
        or "photo-editor-rendering-migration-refresh-chunk-receipt:v1" not in
        rendering_migration_chunk_validator
        or "refresh plan lacks unanimous independent approval" not in
        rendering_migration_chunk_validator
        or "component report does not contain exactly the planned chunk assets" not in
        rendering_migration_chunk_validator
        or "component artifact directory contains missing or unexpected files" not in
        rendering_migration_chunk_validator
        or "validate_rendering_migration_refresh_chunk_test" not in tests_cmake
        or "photo-editor-rendering-migration-pair-diagnosis:v3" not in
        rendering_migration_pair_diagnosis
        or "IEC-61966-2-1-sRGB-decode-then-Rec709-linear-luminance-ratio" not in
        rendering_migration_pair_diagnosis
        or "relativeLinearExposureRangeStops" not in
        rendering_migration_pair_diagnosis
        or "minimumExposureBandFraction" not in
        rendering_migration_pair_diagnosis
        or "allObservedLinearExposureRangeStops" not in
        rendering_migration_pair_diagnosis
        or "diagnose_rendering_migration_pairs_test" not in tests_cmake
        or "RendererTransitionLook" not in color_pipeline_header
        or "photo-editor-edit-recipe-v13.2" not in canonical_recipe_source
        or "photographic-output-aces2-v4" not in recipe_compiler_source
        or "aces2-output-lut:v2" not in (
            REPO_ROOT / "engine/src/pipeline/output_rendering.cpp"
        ).read_text(encoding="utf-8")
        or "rendererSemanticMigrations" not in rendering_migration_assessor
        or "preserve-legacy-direct-luminance-v1" not in
        rendering_migration_assessor
        or "preserve-legacy-direct-luminance-v1" not in
        editor_rendering_migration
        or "rendering-upgrade:v2" not in rendering_migration_workflow_qml
        or "transition:preserve-legacy-direct-luminance-v1" not in
        rendering_migration_workflow_qml
        or "canonical plan and photographic output transition look differ"
        not in engine_render_executor_source
        or "std::filesystem::create_hard_link(" not in
        rendering_migration_batch_command
        or "materialize_artifact(" not in
        rendering_migration_batch_command
        or "bounded_qimage_allocation_limit_megabytes(" not in
        bounded_qimage_reader
        or "maximum_megabytes = 1024" not in
        bounded_qimage_reader
        or "QImageReader::setAllocationLimit" not in
        bounded_qimage_reader
    ):
        errors.append(
            "migration reports must bind the exact producer executable, reject cross-binary merges, support identity-checked blocker-reducing overlays with verified artifacts, and bound large-image readback"
        )
    recipe_asset_store_source = (
        REPO_ROOT
        / "apps/desktop/src/recipe_asset_store.cpp"
    ).read_text(encoding="utf-8")
    if (
        "QSaveFile file(path)" not in
        recipe_asset_store_source
        or "QCryptographicHash::Sha256" not in
        recipe_asset_store_source
        or "digest_for_payload(payload) != digest" not in
        recipe_asset_store_source
        or "src/recipe_asset_store.cpp" not in desktop_cmake
    ):
        errors.append(
            "recipe_asset_store must remain the published atomic content-addressed v13 asset owner"
        )
    studio_workspace = (
        REPO_ROOT / "apps/desktop/qml/workspaces/StudioWorkspace.qml"
    ).read_text(encoding="utf-8")
    studio_image_viewport = (
        REPO_ROOT / "apps/desktop/qml/components/StudioImageViewport.qml"
    ).read_text(encoding="utf-8")
    studio_surface = studio_workspace + "\n" + studio_image_viewport
    edit_render_policy = (
        REPO_ROOT / "apps/desktop/src/edit_render_policy.h"
    ).read_text(encoding="utf-8")
    preview_publisher = (
        REPO_ROOT / "apps/desktop/src/preview_publisher.h"
    ).read_text(encoding="utf-8")
    preview_session = (
        REPO_ROOT / "apps/desktop/src/preview_session.h"
    ).read_text(encoding="utf-8")
    mask_session = (
        REPO_ROOT / "apps/desktop/src/mask_session.h"
    ).read_text(encoding="utf-8")
    layer_session = (
        REPO_ROOT / "apps/desktop/src/layer_session.h"
    ).read_text(encoding="utf-8")
    mask_worker_coordinator = (
        REPO_ROOT / "apps/desktop/src/mask_worker_coordinator.h"
    ).read_text(encoding="utf-8")
    thumbnail_refresh_coordinator = (
        REPO_ROOT / "apps/desktop/src/thumbnail_refresh_coordinator.h"
    ).read_text(encoding="utf-8")
    thumbnail_refresh_queue_service_header = (
        REPO_ROOT / "apps/desktop/src/thumbnail_refresh_queue_service.h"
    ).read_text(encoding="utf-8")
    thumbnail_refresh_queue_service_source = (
        REPO_ROOT / "apps/desktop/src/thumbnail_refresh_queue_service.cpp"
    ).read_text(encoding="utf-8")
    thumbnail_worker_renderer_factory_source = (
        REPO_ROOT / "apps/desktop/src/thumbnail_worker_renderer_factory.cpp"
    ).read_text(encoding="utf-8")
    thumbnail_worker_renderer_factory_header = (
        REPO_ROOT / "apps/desktop/src/thumbnail_worker_renderer_factory.h"
    ).read_text(encoding="utf-8")
    mask_alpha_provider_header = (
        REPO_ROOT / "apps/desktop/src/mask_alpha_provider.h"
    ).read_text(encoding="utf-8")
    mask_alpha_provider_source = (
        REPO_ROOT / "apps/desktop/src/mask_alpha_provider.cpp"
    ).read_text(encoding="utf-8")
    scaled_mask_matte_service_header = (
        REPO_ROOT / "apps/desktop/src/scaled_mask_matte_service.h"
    ).read_text(encoding="utf-8")
    scaled_mask_matte_service_source = (
        REPO_ROOT / "apps/desktop/src/scaled_mask_matte_service.cpp"
    ).read_text(encoding="utf-8")
    mask_source_revision = (
        REPO_ROOT / "apps/desktop/src/mask_source_revision.h"
    ).read_text(encoding="utf-8")
    raw_decode_policy = (
        REPO_ROOT / "apps/desktop/src/raw_decode_policy.h"
    ).read_text(encoding="utf-8")
    editor_preview_pixel_zoom_test = (
        REPO_ROOT / "tests/editor_preview_pixel_zoom_test.cpp"
    ).read_text(encoding="utf-8")
    canonical_composite_mask_alpha_source = (
        REPO_ROOT / "apps/desktop/src/canonical_composite_mask_alpha.cpp"
    ).read_text(encoding="utf-8")
    mask_thumbnail_payload_service_source = (
        REPO_ROOT / "apps/desktop/src/mask_thumbnail_payload_service.cpp"
    ).read_text(encoding="utf-8")
    thumbnail_shared_preview_adapter = (
        REPO_ROOT / "apps/desktop/src/thumbnail_shared_preview_adapter.h"
    ).read_text(encoding="utf-8")
    thumbnail_render_job = (
        REPO_ROOT / "apps/desktop/src/thumbnail_render_job.h"
    ).read_text(encoding="utf-8")
    photo_layer_compiler = (
        REPO_ROOT / "apps/desktop/src/photo_layer_compiler.h"
    ).read_text(encoding="utf-8")
    composite_mask_alpha_preview_parts = preview_controller_source.split(
        "MaskAlphaEvaluationResult evaluate_composite_mask_alpha_for_preview(", 1
    )
    composite_mask_alpha_preview_body = (
        composite_mask_alpha_preview_parts[1].split(
            "QString composite_mask_preview_source(", 1
        )[0]
        if len(composite_mask_alpha_preview_parts) == 2
        else ""
    )
    photo_layer_adjustment_recipe_source = (
        REPO_ROOT / "apps/desktop/src/photo_layer_adjustment_recipe.cpp"
    ).read_text(encoding="utf-8")
    thumbnail_cache_session = (
        REPO_ROOT / "apps/desktop/src/thumbnail_cache_session.h"
    ).read_text(encoding="utf-8")
    thumbnail_presentation_service = (
        REPO_ROOT / "apps/desktop/src/thumbnail_presentation_service.h"
    ).read_text(encoding="utf-8")
    thumbnail_presentation_service_source = (
        REPO_ROOT / "apps/desktop/src/thumbnail_presentation_service.cpp"
    ).read_text(encoding="utf-8")
    mask_presentation_schema = (
        REPO_ROOT / "apps/desktop/src/mask_presentation_schema.h"
    ).read_text(encoding="utf-8")
    generated_mask_cleanup_header = (
        REPO_ROOT / "apps/desktop/src/generated_mask_cleanup.h"
    ).read_text(encoding="utf-8")
    generated_mask_cleanup_source = (
        REPO_ROOT / "apps/desktop/src/generated_mask_cleanup.cpp"
    ).read_text(encoding="utf-8")
    generated_mask_metadata_header = (
        REPO_ROOT / "apps/desktop/src/generated_mask_metadata.h"
    ).read_text(encoding="utf-8")
    generated_mask_metadata_source = (
        REPO_ROOT / "apps/desktop/src/generated_mask_metadata.cpp"
    ).read_text(encoding="utf-8")
    generated_mask_object_remove_header = (
        REPO_ROOT / "apps/desktop/src/generated_mask_object_remove_assist.h"
    ).read_text(encoding="utf-8")
    generated_mask_object_remove_source = (
        REPO_ROOT / "apps/desktop/src/generated_mask_object_remove_assist.cpp"
    ).read_text(encoding="utf-8")
    generated_mask_quality_header = (
        REPO_ROOT / "apps/desktop/src/generated_mask_quality.h"
    ).read_text(encoding="utf-8")
    generated_mask_quality_source = (
        REPO_ROOT / "apps/desktop/src/generated_mask_quality.cpp"
    ).read_text(encoding="utf-8")
    segmentation_mask_ops_header = (
        REPO_ROOT / "apps/desktop/src/segmentation_mask_ops.h"
    ).read_text(encoding="utf-8")
    segmentation_mask_ops_source = (
        REPO_ROOT / "apps/desktop/src/segmentation_mask_ops.cpp"
    ).read_text(encoding="utf-8")
    sensor_dust_detection_header = (
        REPO_ROOT / "apps/desktop/src/sensor_dust_detection.h"
    ).read_text(encoding="utf-8")
    sensor_dust_detection_source = (
        REPO_ROOT / "apps/desktop/src/sensor_dust_detection.cpp"
    ).read_text(encoding="utf-8")
    sensor_dust_map_store_header = (
        REPO_ROOT / "apps/desktop/src/sensor_dust_map_store.h"
    ).read_text(encoding="utf-8")
    sensor_dust_map_store_source = (
        REPO_ROOT / "apps/desktop/src/sensor_dust_map_store.cpp"
    ).read_text(encoding="utf-8")
    retouch_mask_detection_header = (
        REPO_ROOT / "apps/desktop/src/retouch_mask_detection.h"
    ).read_text(encoding="utf-8")
    retouch_mask_detection_source = (
        REPO_ROOT / "apps/desktop/src/retouch_mask_detection.cpp"
    ).read_text(encoding="utf-8")
    smart_selection_geometry_header = (
        REPO_ROOT / "apps/desktop/src/smart_selection_geometry.h"
    ).read_text(encoding="utf-8")
    smart_selection_geometry_source = (
        REPO_ROOT / "apps/desktop/src/smart_selection_geometry.cpp"
    ).read_text(encoding="utf-8")
    smart_selection_instance_policy_header = (
        REPO_ROOT / "apps/desktop/src/smart_selection_instance_policy.h"
    ).read_text(encoding="utf-8")
    smart_selection_instance_policy_source = (
        REPO_ROOT / "apps/desktop/src/smart_selection_instance_policy.cpp"
    ).read_text(encoding="utf-8")
    smart_selection_polygon_geometry_header = (
        REPO_ROOT / "apps/desktop/src/smart_selection_polygon_geometry.h"
    ).read_text(encoding="utf-8")
    smart_selection_polygon_geometry_source = (
        REPO_ROOT / "apps/desktop/src/smart_selection_polygon_geometry.cpp"
    ).read_text(encoding="utf-8")
    smart_selection_prompt_schema_header = (
        REPO_ROOT / "apps/desktop/src/smart_selection_prompt_schema.h"
    ).read_text(encoding="utf-8")
    smart_selection_prompt_schema_source = (
        REPO_ROOT / "apps/desktop/src/smart_selection_prompt_schema.cpp"
    ).read_text(encoding="utf-8")
    segmentation_result_schema_header = (
        REPO_ROOT / "apps/desktop/src/segmentation_result_schema.h"
    ).read_text(encoding="utf-8")
    segmentation_result_schema_source = (
        REPO_ROOT / "apps/desktop/src/segmentation_result_schema.cpp"
    ).read_text(encoding="utf-8")
    segmentation_request_policy_header = (
        REPO_ROOT / "apps/desktop/src/segmentation_request_policy.h"
    ).read_text(encoding="utf-8")
    segmentation_request_policy_source = (
        REPO_ROOT / "apps/desktop/src/segmentation_request_policy.cpp"
    ).read_text(encoding="utf-8")
    object_pick_policy_header = (
        REPO_ROOT / "apps/desktop/src/object_pick_policy.h"
    ).read_text(encoding="utf-8")
    object_pick_policy_source = (
        REPO_ROOT / "apps/desktop/src/object_pick_policy.cpp"
    ).read_text(encoding="utf-8")
    localized_object_refinement_header = (
        REPO_ROOT / "apps/desktop/src/localized_object_refinement.h"
    ).read_text(encoding="utf-8")
    localized_object_refinement_source = (
        REPO_ROOT / "apps/desktop/src/localized_object_refinement.cpp"
    ).read_text(encoding="utf-8")
    brush_input_schema_header = (
        REPO_ROOT / "apps/desktop/src/brush_input_schema.h"
    ).read_text(encoding="utf-8")
    brush_input_schema_source = (
        REPO_ROOT / "apps/desktop/src/brush_input_schema.cpp"
    ).read_text(encoding="utf-8")
    brush_stroke_sampling_header = (
        REPO_ROOT / "apps/desktop/src/brush_stroke_sampling.h"
    ).read_text(encoding="utf-8")
    brush_stroke_sampling_source = (
        REPO_ROOT / "apps/desktop/src/brush_stroke_sampling.cpp"
    ).read_text(encoding="utf-8")
    brush_rasterizer_header = (
        REPO_ROOT / "apps/desktop/src/brush_rasterizer.h"
    ).read_text(encoding="utf-8")
    brush_rasterizer_source = (
        REPO_ROOT / "apps/desktop/src/brush_rasterizer.cpp"
    ).read_text(encoding="utf-8")
    brush_matte_rebuild_header = (
        REPO_ROOT / "apps/desktop/src/brush_matte_rebuild.h"
    ).read_text(encoding="utf-8")
    brush_matte_rebuild_source = (
        REPO_ROOT / "apps/desktop/src/brush_matte_rebuild.cpp"
    ).read_text(encoding="utf-8")
    brush_outline_geometry_header = (
        REPO_ROOT / "apps/desktop/src/brush_outline_geometry.h"
    ).read_text(encoding="utf-8")
    brush_outline_geometry_source = (
        REPO_ROOT / "apps/desktop/src/brush_outline_geometry.cpp"
    ).read_text(encoding="utf-8")
    mask_outline_simplification_header = (
        REPO_ROOT / "apps/desktop/src/mask_outline_simplification.h"
    ).read_text(encoding="utf-8")
    mask_outline_simplification_source = (
        REPO_ROOT / "apps/desktop/src/mask_outline_simplification.cpp"
    ).read_text(encoding="utf-8")
    mask_outline_presentation_header = (
        REPO_ROOT / "apps/desktop/src/mask_outline_presentation.h"
    ).read_text(encoding="utf-8")
    mask_outline_presentation_source = (
        REPO_ROOT / "apps/desktop/src/mask_outline_presentation.cpp"
    ).read_text(encoding="utf-8")
    mask_outline_tracing_header = (
        REPO_ROOT / "apps/desktop/src/mask_outline_tracing.h"
    ).read_text(encoding="utf-8")
    mask_outline_tracing_source = (
        REPO_ROOT / "apps/desktop/src/mask_outline_tracing.cpp"
    ).read_text(encoding="utf-8")
    mask_range_preview_math_header = (
        REPO_ROOT / "apps/desktop/src/mask_range_preview_math.h"
    ).read_text(encoding="utf-8")
    mask_range_preview_math_source = (
        REPO_ROOT / "apps/desktop/src/mask_range_preview_math.cpp"
    ).read_text(encoding="utf-8")
    preset_service = (
        REPO_ROOT / "apps/desktop/src/preset_service.h"
    ).read_text(encoding="utf-8")
    preset_registry = (
        REPO_ROOT / "apps/desktop/src/preset_registry.cpp"
    ).read_text(encoding="utf-8")
    history_service = (
        REPO_ROOT / "apps/desktop/src/history_service.h"
    ).read_text(encoding="utf-8")
    histogram_scope_service_header = (
        REPO_ROOT / "apps/desktop/src/histogram_scope_service.h"
    ).read_text(encoding="utf-8")
    histogram_scope_service_source = (
        REPO_ROOT / "apps/desktop/src/histogram_scope_service.cpp"
    ).read_text(encoding="utf-8")
    histogram_scope_coordinator = (
        REPO_ROOT / "apps/desktop/src/histogram_scope_coordinator.h"
    ).read_text(encoding="utf-8")
    histogram_scope_session = (
        REPO_ROOT / "apps/desktop/src/histogram_scope_session.h"
    ).read_text(encoding="utf-8")
    render_scheduler = (
        REPO_ROOT / "apps/desktop/src/render_scheduler.h"
    ).read_text(encoding="utf-8")
    studio_color_management = (
        REPO_ROOT
        / "apps/desktop/qml/components/StudioColorManagementPanel.qml"
    ).read_text(encoding="utf-8")
    studio_detail_texture = (
        REPO_ROOT
        / "apps/desktop/qml/components/StudioDetailTextureControl.qml"
    ).read_text(encoding="utf-8")
    studio_lens_correction = (
        REPO_ROOT
        / "apps/desktop/qml/components/StudioLensCorrectionControl.qml"
    ).read_text(encoding="utf-8")
    studio_thumbnail_card = (
        REPO_ROOT
        / "apps/desktop/qml/components/StudioThumbnailCard.qml"
    ).read_text(encoding="utf-8")
    studio_retouch_overlay = (
        REPO_ROOT
        / "apps/desktop/qml/components/StudioRetouchOverlayCommandSurface.qml"
    ).read_text(encoding="utf-8")
    studio_viewport_input = (
        REPO_ROOT
        / "apps/desktop/qml/components/StudioViewportInputCommandSurface.qml"
    ).read_text(encoding="utf-8")
    catalog_asset_model = (
        REPO_ROOT / "apps/desktop/src/catalog_asset_model.cpp"
    ).read_text(encoding="utf-8")
    catalog_asset_header = (
        REPO_ROOT / "apps/desktop/src/catalog_asset_model.h"
    ).read_text(encoding="utf-8")
    agent_edit_controller = (
        REPO_ROOT
        / "apps/desktop/src/agent_edit_controller.cpp"
    ).read_text(encoding="utf-8")
    photo_automation_bridge = (
        REPO_ROOT
        / "apps/desktop/src/photo_automation_bridge.cpp"
    ).read_text(encoding="utf-8")
    photo_mcp_server = (
        REPO_ROOT
        / "apps/desktop/src/photo_mcp_server.cpp"
    ).read_text(encoding="utf-8")
    if (
        "applyRecipeJsonToAssetIds" in studio_workspace
        or "applyPresetRecipeJsonToAssetIds" in studio_workspace
        or "copiedSettingsRecipeJson" in studio_workspace
        or "presetPreviewRecipeJson" in studio_workspace
        or "applyAdjustmentPatchJsonToAssetIds" not in
        studio_workspace
        or "parse_adjustment_patch_json(" not in
        catalog_asset_model
    ):
        errors.append(
            "interactive preset, clipboard, and batch handoffs must use adjustment patches instead of v12 recipe JSON"
        )
    if (
        "currentRecipeJson()" in studio_workspace
        or "asset.recipeJson" in studio_workspace
    ):
        errors.append(
            "Studio comparisons must use adjustment-patch projections instead of v12 recipe JSON"
        )
    quality_function = studio_image_viewport.split(
        "function updatePublishedPreviewQuality()", 1
    )
    quality_body = (
        quality_function[1].split("function updatePublishedPreviewViewport(", 1)[0]
        if len(quality_function) == 2
        else ""
    )
    if (
        "setPreviewPresentationMetrics(" not in quality_body
        or any(
            token in quality_body
            for token in (
                "previewTargetEdge",
                "qualityFloor",
                "supersample",
                "Math.max",
                "2560",
                "3072",
                "2.5",
            )
        )
        or "preview_target_edge_for_presentation(" not in edit_render_policy
        or "kDefaultInteractivePreviewEdge" not in edit_render_policy
        or "kMinimumInteractivePreviewEdge" not in edit_render_policy
        or "kMaximumInteractivePreviewEdge" not in edit_render_policy
        or "setPreviewPresentationMetrics(" not in preview_controller_source
        or "preview_target_edge_for_presentation(" not in preview_controller_source
    ):
        errors.append(
            "Studio QML must publish presentation facts while edit_render_policy owns preview quality numerics"
        )
    viewport_function = studio_image_viewport.split(
        "function updatePublishedPreviewViewport(", 1
    )
    viewport_body = (
        viewport_function[1].split(
            "function schedulePublishedPreviewViewportUpdate(", 1
        )[0]
        if len(viewport_function) == 2
        else ""
    )
    if (
        "schedulePreviewViewportPublication(" not in viewport_body
        or any(
            token in studio_surface
            for token in (
                "publishedPreviewViewportSettleTimer",
                "lastPublishedPreviewViewportSignature",
                "publishedPreviewViewportUpdateQueued",
                "previewViewportPatchEdgeForInspection(",
            )
        )
        or "class PreviewPublisher final" not in preview_publisher
        or "settleDelayMs(" not in preview_publisher
        or "patchEdgeForPresentation(" not in preview_publisher
        or "last_published_" not in preview_publisher
        or "preview_publisher_.schedule(" not in preview_controller_source
        or "src/preview_publisher.h" not in desktop_cmake
    ):
        errors.append(
            "PreviewPublisher must own viewport settling, patch sizing, and duplicate suppression"
        )
    if (
        "class RenderScheduler final" not in render_scheduler
        or "scheduleInteractive(" not in render_scheduler
        or "scheduleFull(" not in render_scheduler
        or "schedulePendingDispatchIfIdle(" not in render_scheduler
        or "QTimer interactive_timer_" not in render_scheduler
        or "QTimer full_timer_" not in render_scheduler
        or "QTimer pending_dispatch_timer_" not in render_scheduler
        or "render_scheduler_.scheduleInteractive(" not in preview_controller_source
        or "render_scheduler_.scheduleFull(" not in preview_controller_source
        or "render_scheduler_.schedulePendingDispatchIfIdle(" not in preview_controller_source
        or "QTimer interactive_render_timer_" in controller_header
        or "QTimer full_render_timer_" in controller_header
        or "src/render_scheduler.h" not in desktop_cmake
    ):
        errors.append(
            "RenderScheduler must remain the sole interactive/full preview timer owner"
        )
    if (
        "class PreviewSession final" not in preview_session
        or "advanceGeneration()" not in preview_session
        or "canPublish(" not in preview_session
        or "markPublished(" not in preview_session
        or "advancePublicationRevision()" not in preview_session
        or "PreviewSession preview_session_;" not in controller_header
        or "preview_session_.advanceGeneration()" not in preview_controller_source
        or "preview_session_.canPublish(" not in preview_controller_source
        or "render_generation_" in controller_header
        or "latest_render_generation_" in controller_header
        or "last_interactive_preview_generation_" in controller_header
        or "last_full_preview_generation_" in controller_header
        or "src/preview_session.h" not in desktop_cmake
        or "preview_session_test.cpp" not in tests_cmake
    ):
        errors.append(
            "PreviewSession must own render cancellation identity, stale publication ordering, and preview revision"
        )
    if (
        "class MaskSession final" not in mask_session
        or "advanceWorkRevision()" not in mask_session
        or "advanceBrushRasterGeneration()" not in mask_session
        or "advanceSmartSelectionGeneration()" not in mask_session
        or "advanceSmartPreviewGeneration()" not in mask_session
        or "advanceInpaintGeneration()" not in mask_session
        or "replaceStack(" not in mask_session
        or "setActiveIndex(" not in mask_session
        or "setSmartSelectionRetry(" not in mask_session
        or "setInpaintRetry(" not in mask_session
        or "clearRetry()" not in mask_session
        or "previewCacheLookup(" not in mask_session
        or "storePreviewCacheEntry(" not in mask_session
        or "outlineCacheLookup(" not in mask_session
        or "storeOutlineCache(" not in mask_session
        or "colorToolPreviewLookup(" not in mask_session
        or "smartSelectionPreviewLookup(" not in mask_session
        or "MaskSession mask_session_;" not in controller_header
        or "mask_session_.advanceWorkRevision()" not in preview_controller_source
        or "mask_session_.recordSmartSelectionCancellation()" not in
        preview_controller_source
        or any(
            token in controller_header
            for token in (
                "mask_work_state_revision_",
                "brush_raster_job_generation_",
                "brush_raster_cancelled_count_",
                "smart_selection_job_generation_",
                "smart_selection_cancelled_count_",
                "smart_select_preview_generation_",
                "inpaint_job_generation_",
                "inpaint_cancelled_count_",
                "QVariantList mask_stack_",
                "int active_mask_index_",
                "QString mask_preview_mode_",
                "QString mask_retry_action_",
                "active_mask_cache_dirty_",
                "active_mask_preview_lru_cache_",
                "active_mask_outline_cache_key_",
                "color_tool_mask_preview_cache_key_",
                "smart_select_preview_cache_key_",
            )
        )
        or "src/mask_session.h" not in desktop_cmake
        or "mask_session_test.cpp" not in tests_cmake
    ):
        errors.append(
            "MaskSession must own mask work revisions, worker generations, stale identity, and cancellation telemetry"
        )
    if (
        "class LayerSession final" not in layer_session
        or "replaceLayers(" not in layer_session
        or "appendVisibilityState(" not in layer_session
        or "removeVisibilityState(" not in layer_session
        or "copyAdjustments(" not in layer_session
        or "LayerSession layer_session_;" not in controller_header
        or "layer_session_.replaceLayers(" not in preview_controller_source
        or "layer_session_.appendVisibilityState(" not in preview_controller_source
        or "layer_session_.copyAdjustments(" not in preview_controller_source
        or any(
            token in controller_header
            for token in (
                "QVariantList photo_layers_",
                "QVariantList photo_layer_visibility_states_",
                "QVariantMap copied_photo_layer_adjustments_",
                "bool has_copied_photo_layer_adjustments_",
            )
        )
        or "src/layer_session.h" not in desktop_cmake
        or "layer_session_test.cpp" not in tests_cmake
    ):
        errors.append(
            "LayerSession must own durable layer state, saved visibility states, and the adjustment clipboard"
        )
    if (
        "class MaskWorkerCoordinator final" not in mask_worker_coordinator
        or "smartSelectPreviewWatcher()" not in mask_worker_coordinator
        or "smartSelectCommitWatcher()" not in mask_worker_coordinator
        or "brushRasterCommitWatcher()" not in mask_worker_coordinator
        or "inpaintApplyWatcher()" not in mask_worker_coordinator
        or "inpaintRegenerateWatcher()" not in mask_worker_coordinator
        or "activeMaskOutlineWatcher()" not in mask_worker_coordinator
        or "outlineWorkerPool()" not in mask_worker_coordinator
        or "setPendingActiveMaskOutline(" not in mask_worker_coordinator
        or "latestActiveMaskOutlineJob()" not in mask_worker_coordinator
        or "setPendingSmartSelectPreview(" not in mask_worker_coordinator
        or "setPendingSmartSelectCommit(" not in mask_worker_coordinator
        or "latestBrushRasterGeneration()" not in mask_worker_coordinator
        or "MaskWorkerCoordinator mask_worker_coordinator_;" not in controller_header
        or "mask_worker_coordinator_.smartSelectPreviewWatcher()" not in
        preview_controller_source
        or "mask_worker_coordinator_.setPendingSmartSelectCommit(" not in
        preview_controller_source
        or "mask_worker_coordinator_.activeMaskOutlineWatcher()" not in
        preview_controller_source
        or "mask_worker_coordinator_.setPendingActiveMaskOutline(" not in
        preview_controller_source
        or "outline_execution_mode\"), QStringLiteral(\"async_worker\")" not in
        preview_controller_source
        or "kSynchronousOutlinePixelCeiling" not in preview_controller_source
        or "fast_outline_paths_for_brush_mask(fallback_mask)" not in
        preview_controller_source
        or any(
            token in controller_header
            for token in (
                "smart_select_preview_watcher_",
                "smart_select_commit_watcher_",
                "brush_raster_commit_watcher_",
                "inpaint_apply_watcher_",
                "inpaint_regenerate_watcher_",
                "pending_smart_select_preview_request_",
                "pending_smart_select_commit_request_",
                "latest_brush_raster_job_",
                "latest_inpaint_job_",
                "active_mask_outline_watcher_",
                "outline_worker_pool_",
                "pending_active_mask_outline_request_",
                "latest_active_mask_outline_job_",
            )
        )
        or "src/mask_worker_coordinator.h" not in desktop_cmake
        or "mask_worker_coordinator_test.cpp" not in tests_cmake
    ):
        errors.append(
            "MaskWorkerCoordinator must own mask future watchers, pending smart-selection payloads, and latest worker identities"
        )
    if (
        "class ThumbnailRefreshCoordinator final" not in
        thumbnail_refresh_coordinator
        or "void schedule(" not in thumbnail_refresh_coordinator
        or "void defer()" not in thumbnail_refresh_coordinator
        or "resultIsCurrent(" not in thumbnail_refresh_coordinator
        or "recordStaleResult(" not in thumbnail_refresh_coordinator
        or "void complete(" not in thumbnail_refresh_coordinator
        or "consumeStaleWarning(" not in thumbnail_refresh_coordinator
        or "struct AdmissionFacts" not in thumbnail_refresh_coordinator
        or "admissionBlockers(" not in thumbnail_refresh_coordinator
        or "ThumbnailRefreshCoordinator thumbnail_refresh_coordinator_;" not in
        controller_header
        or "thumbnail_refresh_coordinator_.schedule(" not in
        preview_controller_source
        or "thumbnail_refresh_coordinator_.startWorker(" not in
        preview_controller_source
        or "thumbnail_refresh_coordinator_.complete(" not in
        preview_controller_source
        or "ThumbnailRefreshCoordinator::admissionBlockers(" not in
        preview_controller_source
        or any(
            token in controller_header
            for token in (
                "thumbnail_refresh_queue_timer_",
                "thumbnail_refresh_queue_pending_",
                "thumbnail_refresh_queue_reason_",
                "thumbnail_refresh_queue_signature_",
                "thumbnail_refresh_queue_generation_",
                "thumbnail_refresh_queue_pending_generation_",
                "thumbnail_refresh_queue_active_job_id_",
                "thumbnail_refresh_queue_completion_",
                "thumbnail_refresh_queue_cancellation_",
                "thumbnail_refresh_queue_watcher_",
                "thumbnail_stale_warning_signatures_",
            )
        )
        or "src/thumbnail_refresh_coordinator.h" not in desktop_cmake
        or "thumbnail_refresh_coordinator_test.cpp" not in tests_cmake
    ):
        errors.append(
            "ThumbnailRefreshCoordinator must own shared thumbnail queue timing, cancellation, identity, completion, and telemetry"
        )
    if (
        "struct ThumbnailRefreshQueueRequestFacts final" not in
        thumbnail_refresh_queue_service_header
        or "make_thumbnail_refresh_queue_request(" not in
        thumbnail_refresh_queue_service_header
        or "publish_thumbnail_refresh_queue_entries(" not in
        thumbnail_refresh_queue_service_header
        or "ThumbnailRefreshQueueRequest make_thumbnail_refresh_queue_request(" not in
        thumbnail_refresh_queue_service_source
        or "publish_thumbnail_refresh_queue_entries(" not in
        thumbnail_refresh_queue_service_source
        or "make_thumbnail_refresh_queue_request(" not in
        preview_controller_source
        or "publish_thumbnail_refresh_queue_entries(" not in
        preview_controller_source
        or "make_thumbnail_worker_renderers(" not in preview_controller_source
        or ".adjustment_thumbnail_source_image" in preview_controller_source
        or "rendered_mask_thumbnail_entries(" in preview_controller_source
        or "rendered_masked_adjustment_layer_thumbnail_source(" in
        preview_controller_source
        or "adjusted_layer_thumbnail_color(" in preview_controller_source
        or "blended_layer_thumbnail_color(" in preview_controller_source
        or "adjustment_layer_effect_preview_image(" in preview_controller_source
        or "insert_adjustment_layer_effect_preview_fields(" in
        preview_controller_source
        or "thumbnail_mask_maps_for_selection_layer(" in
        preview_controller_source
        or "make_thumbnail_worker_renderers(" not in
        thumbnail_worker_renderer_factory_source
        or "render_mask_thumbnail_entries(" not in
        thumbnail_worker_renderer_factory_source
        or "render_adjustment_layer_thumbnail_source(" not in
        thumbnail_worker_renderer_factory_source
        or "insert_adjustment_layer_effect_preview_fields(" not in
        thumbnail_worker_renderer_factory_source
        or "thumbnail_mask_maps_for_selection_layer(" not in
        thumbnail_worker_renderer_factory_source
        or "canonical_composite_mask_thumbnail_alpha_metadata(" not in
        thumbnail_worker_renderer_factory_source
        or "canonical_mask_thumbnail_payload" not in
        thumbnail_worker_renderer_factory_source
        or "rasterize_local_mask_set_alpha(" not in
        canonical_composite_mask_alpha_source
        or "working_image_from_qimage(" not in
        canonical_composite_mask_alpha_source
        or "evaluate_canonical_composite_mask_alpha(" not in
        composite_mask_alpha_preview_body
        or "evaluate_mask_alpha_cached(" in
        composite_mask_alpha_preview_body
        or "evaluate_canonical_composite_mask_alpha(" not in
        mask_thumbnail_payload_service_source
        or "grayscale_mask_alpha_image(" not in
        mask_thumbnail_payload_service_source
        or "solid_color_mask_alpha_image(" not in
        mask_thumbnail_payload_service_source
        or ".alpha_metadata" in thumbnail_worker_renderer_factory_header
        or ".alpha_metadata" in preview_controller_source
        or ".thumbnail_payload" in thumbnail_worker_renderer_factory_header
        or ".thumbnail_payload" in preview_controller_source
        or "mask_thumbnail_payload_for_masks(" in preview_controller_source
        or "mask_thumbnail_overlay_contract(" in preview_controller_source
        or ".shared_render = render_thumbnail_shared_preview_request" not in
        preview_controller_source
        or "[](const ThumbnailAdjustmentSharedRenderRequest& request)" in
        preview_controller_source
        or "execute_preview_render_request(RenderRequest{" not in
        thumbnail_shared_preview_adapter
        or "result.adjustment_pipeline_elapsed_ms" not in
        thumbnail_shared_preview_adapter
        or "RenderResult execute_preview_render_request(" not in
        preview_controller_source
        or "composite_mask_thumbnail_alpha_metadata(" in
        preview_controller_source
        or "composite_mask_grayscale_alpha_thumbnail_source(" in
        preview_controller_source
        or "composite_mask_solid_color_alpha_thumbnail_source(" in
        preview_controller_source
        or "fallback:fail-closed" not in thumbnail_worker_renderer_factory_source
        or "decode_source_image(" not in thumbnail_worker_renderer_factory_source
        or "apply_crop(" not in thumbnail_worker_renderer_factory_source
        or "make_thumbnail_refresh_queue_job(" in preview_controller_source
        or "QString thumbnail_state_signature(" in preview_controller_source
        or "edit_state_signature(" in thumbnail_refresh_queue_service_source
        or "thumbnail_refresh_queue_service_test.cpp" not in tests_cmake
        or "src/thumbnail_worker_renderer_factory.cpp" not in desktop_cmake
        or "src/canonical_composite_mask_alpha.cpp" not in desktop_cmake
        or "src/mask_thumbnail_payload_service.cpp" not in desktop_cmake
        or "src/thumbnail_shared_preview_adapter.h" not in desktop_cmake
        or "src/thumbnail_render_job.h" not in desktop_cmake
        or "src/photo_layer_compiler.h" not in desktop_cmake
        or "thumbnail_worker_renderer_factory.cpp" not in tests_cmake
        or "canonical_composite_mask_alpha.cpp" not in tests_cmake
        or "mask_thumbnail_payload_service.cpp" not in tests_cmake
    ):
        errors.append(
            "thumbnail workers must own revision-only request identity, canonical engine mask-alpha evaluation, immutable request construction, and ordered publication without controller rendering callbacks"
        )
    if (
        "make_thumbnail_adjustment_render_job(" not in thumbnail_render_job
        or "photo_layer_thumbnail" not in thumbnail_render_job
        or "%1-1-%2" not in thumbnail_render_job
        or "shared_preview_thumbnail" not in thumbnail_render_job
        or "adjustment_patch_identity_json(state)" not in thumbnail_render_job
        or "make_thumbnail_adjustment_render_job(" not in
        thumbnail_worker_renderer_factory_source
        or "photo_layer_thumbnail_recipe_hash" not in
        thumbnail_worker_renderer_factory_source
        or "photo_layer_thumbnail_entry_recipe_hash" not in
        thumbnail_worker_renderer_factory_source
        or "preview_white_balance_multipliers(" not in
        thumbnail_worker_renderer_factory_source
        or "ThumbnailAdjustmentRecipeDependencies" in
        thumbnail_worker_renderer_factory_header
        or "make_render_job" in thumbnail_worker_renderer_factory_header
        or "std::function<std::array<double, 3>" in
        thumbnail_worker_renderer_factory_header
        or "ThumbnailAdjustmentRecipeDependencies" in preview_controller_source
        or ".make_render_job =" in preview_controller_source
        or "ThumbnailMaskRendererDependencies" in
        thumbnail_worker_renderer_factory_header
        or "ThumbnailMaskRendererDependencies" in preview_controller_source
    ):
        errors.append(
            "thumbnail workers must own immutable job identity, canonical recipe hashes, and white-balance policy without controller callbacks"
        )
    if (
        "QVariantMap byte_payload_fingerprint(" not in mask_alpha_provider_header
        or "QVariantMap byte_payload_fingerprint(" not in mask_alpha_provider_source
        or "QVariantMap mask_alpha_trace_fields(" not in mask_alpha_provider_source
        or "void insert_mask_alpha_trace_fields(" not in mask_alpha_provider_source
        or "QVariantMap composite_mask_alpha_trace_fields(" not in
        mask_alpha_provider_source
        or "mask_presentation_schema.h" not in mask_alpha_provider_source
        or "QVariantMap byte_payload_fingerprint(" in preview_controller_source
        or "QVariantMap mask_alpha_trace_fields(" in preview_controller_source
        or "void insert_mask_alpha_trace_fields(" in preview_controller_source
        or "QVariantMap composite_mask_alpha_trace_fields(" in
        preview_controller_source
        or "insert_source_alpha_trace_fields" in
        thumbnail_worker_renderer_factory_header
        or "composite_alpha_trace_fields" in
        thumbnail_worker_renderer_factory_header
        or "composite_mask_alpha_trace_fields(" not in
        thumbnail_worker_renderer_factory_source
        or "insert_mask_alpha_trace_fields" not in
        thumbnail_worker_renderer_factory_source
    ):
        errors.append(
            "mask-alpha trace identity must be owned by the shared provider and consumed directly by thumbnail workers without controller callbacks"
        )
    if (
        "mask_with_scaled_segmentation_matte(" not in
        scaled_mask_matte_service_header
        or "scaled_mask_matte_metadata(" not in
        scaled_mask_matte_service_header
        or "scaled_mask_matte_cache_snapshot(" not in
        scaled_mask_matte_service_header
        or "mask_with_rebuilt_brush_matte(" not in
        scaled_mask_matte_service_source
        or "resample_segmentation_mask(" not in scaled_mask_matte_service_source
        or "mask_tile_source_revision(" not in mask_source_revision
        or "raw_decode_settings_key_for_state(" not in mask_source_revision
        or "mask_with_scaled_segmentation_matte(" not in
        thumbnail_worker_renderer_factory_source
        or "scaled_mask_matte_metadata" not in
        thumbnail_worker_renderer_factory_source
        or "mask_tile_source_revision(" not in
        thumbnail_worker_renderer_factory_source
        or "QVariantMap mask_with_scaled_segmentation_matte(" in
        preview_controller_source
        or "QVariantMap mask_thumbnail_metadata(" in preview_controller_source
        or "QString mask_tile_source_revision(" in preview_controller_source
        or ".scaled_mask_source =" in preview_controller_source
        or ".thumbnail_metadata =" in preview_controller_source
        or ".source_revision =" in preview_controller_source
        or "scaledMaskForTesting" in preview_controller_source
        or "scaledMaskForTesting" in controller_header
        or "ScaledMaskMatteCacheForTesting" in preview_controller_source
        or "ScaledMaskMatteCacheForTesting" in controller_header
        or "mask_with_scaled_segmentation_matte(" not in
        editor_preview_pixel_zoom_test
        or "scaled_mask_matte_cache_snapshot(" not in
        editor_preview_pixel_zoom_test
        or "src/scaled_mask_matte_service.cpp" not in desktop_cmake
        or "src/mask_source_revision.h" not in desktop_cmake
        or "scaled_mask_matte_service.cpp" not in tests_cmake
    ):
        errors.append(
            "scaled mask materialization, metadata, cache policy, and source revision must be focused services consumed directly by thumbnail workers"
        )
    if (
        "RawDecodeSettings raw_decode_settings_for_state(" not in raw_decode_policy
        or "QString raw_decode_settings_key_for_state(" not in raw_decode_policy
        or "raw_decode_settings_cache_key(raw_decode_settings_for_state(state))"
        not in raw_decode_policy
        or "raw_decode_settings_key_for_state(state)" not in mask_source_revision
        or "#include \"raw_decode_policy.h\"" not in preview_controller_source
        or "RawDecodeSettings raw_decode_settings_for_state(" in
        preview_controller_source
        or "QString raw_decode_settings_key(" in preview_controller_source
        or "raw_decode_settings_key_for_state(" not in preview_controller_source
    ):
        errors.append(
            "preview, thumbnail, and mask source identity must share one RAW decode settings mapper and cache-key policy"
        )
    if (
        "compile_pipeline_photo_layers(" not in photo_layer_compiler
        or "compile_pipeline_photo_layers(" in preview_controller_source
        or "compile_desktop_pipeline_state(" not in preview_controller_source
        or "compile_pipeline_photo_layers(" not in desktop_pipeline_state_compiler
        or "rasterize_local_mask_set_alpha(" not in photo_render_utils
        or "local_mask_set_weight_for_layer_mask(" in preview_controller_source
        or "populate_photo_layer_selection_mask(" in preview_controller_source
        or "local_mask_weight_for_preview(" in preview_controller_source
        or "accumulate_composite_weight_for_preview(" in preview_controller_source
        or "range_mask_weight_for_preview_pixel(" in preview_controller_source
    ):
        errors.append(
            "preview and export photo layers must share the canonical source-pixel-aware compiler, and controller-local mask evaluators must stay deleted"
        )
    if (
        "class ThumbnailCacheSession final" not in thumbnail_cache_session
        or "bool entryLookup(" not in thumbnail_cache_session
        or "void storeEntry(" not in thumbnail_cache_session
        or "void clearEntries()" not in thumbnail_cache_session
        or "entryEvictionCount()" not in thumbnail_cache_session
        or "photoLayerRevision()" not in thumbnail_cache_session
        or "latestMaskJob()" not in thumbnail_cache_session
        or "latestPhotoLayerJob()" not in thumbnail_cache_session
        or "ThumbnailCacheSession thumbnail_cache_session_;" not in
        controller_header
        or "thumbnail_cache_session_.entryLookup(" not in
        preview_controller_source
        or "thumbnail_cache_session_.storeEntry(" not in
        preview_controller_source
        or any(
            token in controller_header
            for token in (
                "mask_thumbnail_cache_dirty_",
                "photo_layer_thumbnail_cache_dirty_",
                "mask_thumbnail_refresh_defer_counted_",
                "photo_layer_thumbnail_refresh_defer_counted_",
                "cached_mask_sets_with_thumbnails_",
                "cached_photo_layers_with_thumbnails_",
                "cached_photo_layer_thumbnail_asset_key_",
                "thumbnail_entry_cache_",
                "latest_mask_thumbnail_job_",
                "latest_photo_layer_thumbnail_job_",
                "photo_layer_thumbnail_refresh_count_",
            )
        )
        or "src/thumbnail_cache_session.h" not in desktop_cmake
        or "thumbnail_cache_session_test.cpp" not in tests_cmake
    ):
        errors.append(
            "ThumbnailCacheSession must own thumbnail dirty state, derived snapshots, bounded entries, worker identities, and cache telemetry"
        )
    if (
        "class ThumbnailPresentationService final" not in
        thumbnail_presentation_service
        or "groupMaskSets(" not in thumbnail_presentation_service
        or "groupRetouchStack(" not in thumbnail_presentation_service
        or "struct MaskSetGroupingDependencies final" in
        thumbnail_presentation_service
        or "MaskSetGroupingDependencies" in preview_controller_source
        or "mask_set_grouping_dependencies(" in preview_controller_source
        or "mask_alpha_availability_fields(" not in mask_presentation_schema
        or "selection_mask_is_empty_or_invalid(" not in mask_presentation_schema
        or "mask_source_badge_label(" not in mask_presentation_schema
        or "presentMaskSets(" not in thumbnail_presentation_service
        or "presentPhotoLayers(" not in thumbnail_presentation_service
        or "layer_recipe_hash" in thumbnail_presentation_service
        or "layer_entry_recipe_hash" in thumbnail_presentation_service
        or "photo_layer_thumbnail_recipe_hash(layer)" not in
        thumbnail_presentation_service_source
        or "photo_layer_thumbnail_entry_recipe_hash(layer)" not in
        thumbnail_presentation_service_source
        or "photo_layer_adjustment_recipe_keys()" not in
        photo_layer_adjustment_recipe_source
        or "replace_photo_layer_adjustment_recipe(" not in
        photo_layer_adjustment_recipe_source
        or "QString photo_layer_thumbnail_recipe_hash(" not in
        thumbnail_refresh_queue_service_source
        or "QString photo_layer_thumbnail_entry_recipe_hash(" not in
        thumbnail_refresh_queue_service_source
        or "QString photo_layer_thumbnail_recipe_hash(" in
        preview_controller_source
        or "QStringList photo_layer_adjustment_recipe_keys()" in
        preview_controller_source
        or "ThumbnailPresentationService thumbnail_presentation_service_;" not in
        controller_header
        or "thumbnail_presentation_service_.presentMaskSets(" not in
        preview_controller_source
        or "thumbnail_presentation_service_.presentPhotoLayers(" not in
        preview_controller_source
        or "thumbnail_presentation_service_.groupMaskSets(" not in
        preview_controller_source
        or "thumbnail_presentation_service_.groupRetouchStack(" not in
        preview_controller_source
        or "QVariantList grouped_mask_sets(" in preview_controller_source
        or "QVariantList grouped_retouch_stack(" in preview_controller_source
        or any(
            token in preview_controller_source
            for token in (
                "insert_thumbnail_progressive_fields(",
                "mask_thumbnail_cache_key(",
                "mask_sets_with_reused_thumbnail_fields(",
                "photo_layers_with_reused_thumbnail_fields(",
            )
        )
        or "src/thumbnail_presentation_service.cpp" not in desktop_cmake
        or "src/photo_layer_adjustment_recipe.cpp" not in desktop_cmake
        or "src/mask_presentation_schema.h" not in desktop_cmake
        or "thumbnail_presentation_service_test.cpp" not in tests_cmake
        or "photo_layer_adjustment_recipe_test.cpp" not in tests_cmake
    ):
        errors.append(
            "ThumbnailPresentationService must own mask-set derivation, cache-only mask/layer row assembly, progressive reuse, and contained thumbnail failure presentation"
        )
    if (
        "struct GeneratedMaskCleanupResult final" not in generated_mask_cleanup_header
        or "cleanup_generated_mask_components(" not in generated_mask_cleanup_source
        or "apply_generated_mask_cleanup(" not in generated_mask_cleanup_source
        or "generated_mask_cleanup_summary_for_mask(" not in generated_mask_cleanup_source
        or "std::queue<QPoint>" not in generated_mask_cleanup_source
        or "static_cast<void>(apply_generated_mask_cleanup(mask));" not in
        preview_controller_source
        or "struct GeneratedMaskCleanupResult final" in preview_controller_source
        or "int cleanup_component_limit(" in preview_controller_source
        or "src/generated_mask_cleanup.cpp" not in desktop_cmake
        or "generated_mask_cleanup_test.cpp" not in tests_cmake
    ):
        errors.append(
            "generated mask cleanup must own component traversal, deterministic source replay, mutation metadata, and summary outside EditorPreviewController"
        )
    if (
        "struct GeneratedMaskMetadataFacts final" not in
        generated_mask_metadata_header
        or "make_generated_mask_metadata(" not in generated_mask_metadata_source
        or "stamp_generated_mask_metadata(" not in generated_mask_metadata_source
        or "generated_mask_metadata_is_stale(" not in
        generated_mask_metadata_source
        or "return make_generated_mask_metadata(" not in preview_controller_source
        or "return generated_mask_metadata_is_stale(" not in
        preview_controller_source
        or "const QStringList required_keys" in preview_controller_source
        or "src/generated_mask_metadata.cpp" not in desktop_cmake
        or "generated_mask_metadata_test.cpp" not in tests_cmake
    ):
        errors.append(
            "generated mask metadata must own immutable provenance construction, required-key validation, and dependency staleness outside EditorPreviewController"
        )
    if (
        "struct GeneratedMaskRasterStats final" not in
        generated_mask_quality_header
        or "generated_mask_raster_stats(" not in generated_mask_quality_source
        or "generated_mask_quality_report_for_mask(" not in
        generated_mask_quality_source
        or "generated_mask_quality_report_for_mask(mask," not in
        preview_controller_source
        or "struct GeneratedMaskRasterStats final" in preview_controller_source
        or "mean_confidence < 0.55" in preview_controller_source
        or "src/generated_mask_quality.cpp" not in desktop_cmake
        or "generated_mask_quality_test.cpp" not in tests_cmake
    ):
        errors.append(
            "generated mask quality must own raster statistics, bounded scoring, warnings, and presentation summary outside EditorPreviewController"
        )
    if (
        "struct ObjectRemoveExpansionResult final" not in
        generated_mask_object_remove_header
        or "expand_object_remove_mask_for_shadow_reflection(" not in
        generated_mask_object_remove_source
        or "apply_object_remove_assisted_expansion(" not in
        generated_mask_object_remove_source
        or "static_cast<void>(apply_object_remove_assisted_expansion(" not in
        preview_controller_source
        or "struct ObjectRemoveExpansionResult final" in
        preview_controller_source
        or "shadow_reflection_extent_px = std::clamp(" in
        preview_controller_source
        or "src/generated_mask_object_remove_assist.cpp" not in desktop_cmake
        or "generated_mask_object_remove_assist_test.cpp" not in tests_cmake
    ):
        errors.append(
            "generated object-removal assistance must own alpha expansion, edge support, shadow/reflection extension, and provenance outside EditorPreviewController"
        )
    if (
        "selected_pixel_count_for_mask_data(" not in segmentation_mask_ops_header
        or "union_segmentation_masks(" not in segmentation_mask_ops_source
        or "subtract_segmentation_mask(" not in segmentation_mask_ops_source
        or "resample_segmentation_mask(" not in segmentation_mask_ops_source
        or "#include \"segmentation_mask_ops.h\"" not in
        preview_controller_source
        or "QByteArray union_segmentation_masks(" in preview_controller_source
        or "QByteArray subtract_segmentation_mask(" in preview_controller_source
        or "QByteArray resample_segmentation_mask(" in preview_controller_source
        or "int selected_pixel_count_for_mask_data(" in
        preview_controller_source
        or "src/segmentation_mask_ops.cpp" not in desktop_cmake
        or "segmentation_mask_ops_test.cpp" not in tests_cmake
    ):
        errors.append(
            "segmentation mask operations must own pixel counting, union, subtraction, and bilinear resampling outside EditorPreviewController"
        )
    if (
        "struct DustSpotCandidate final" not in sensor_dust_detection_header
        or "detect_dust_spots(" not in sensor_dust_detection_source
        or "dust_spot_mask_data(" not in sensor_dust_detection_source
        or "#include \"sensor_dust_detection.h\"" not in
        preview_controller_source
        or "struct DustSpotCandidate final" in preview_controller_source
        or "double dust_luminance_for_pixel(" in preview_controller_source
        or "std::vector<DustSpotCandidate> detect_dust_spots(" in
        preview_controller_source
        or "src/sensor_dust_detection.cpp" not in desktop_cmake
        or "sensor_dust_detection_test.cpp" not in tests_cmake
    ):
        errors.append(
            "sensor dust detection must own luminance analysis, candidate clustering, suppression, and matte rasterization outside EditorPreviewController"
        )
    if (
        "struct SensorDustMapPayload final" not in sensor_dust_map_store_header
        or "sensor_dust_map_key(" not in sensor_dust_map_store_source
        or "write_sensor_dust_map(" not in sensor_dust_map_store_source
        or "read_sensor_dust_map(" not in sensor_dust_map_store_source
        or "AppBootstrap::appDataLocation(), camera_label_, lens_model_" not in
        preview_controller_source
        or "struct SensorDustMapPayload final" in preview_controller_source
        or "QString sensor_dust_map_key(" in preview_controller_source
        or "bool write_sensor_dust_map(" in preview_controller_source
        or "src/sensor_dust_map_store.cpp" not in desktop_cmake
        or "sensor_dust_map_store_test.cpp" not in tests_cmake
    ):
        errors.append(
            "sensor dust map storage must own canonical camera/lens identity, explicit-root paths, atomic persistence, and fail-closed decoding outside EditorPreviewController"
        )
    if (
        "struct RedEyeMaskResult final" not in retouch_mask_detection_header
        or "struct SkinMaskResult final" not in retouch_mask_detection_header
        or "detect_red_eye_mask(" not in retouch_mask_detection_source
        or "detect_skin_cleanup_mask(" not in retouch_mask_detection_source
        or "#include \"retouch_mask_detection.h\"" not in
        preview_controller_source
        or "struct RedEyeMaskResult final" in preview_controller_source
        or "struct SkinMaskResult final" in preview_controller_source
        or "RedEyeMaskResult detect_red_eye_mask(" in
        preview_controller_source
        or "src/retouch_mask_detection.cpp" not in desktop_cmake
        or "retouch_mask_detection_test.cpp" not in tests_cmake
    ):
        errors.append(
            "retouch mask detection must own red-eye component analysis and skin cleanup matte classification outside EditorPreviewController"
        )
    if (
        "instance_id_from_map(" not in smart_selection_geometry_header
        or "instance_pixel_count_in_map(" not in
        smart_selection_geometry_source
        or "smart_select_seed_points(" not in smart_selection_geometry_source
        or "#include \"smart_selection_geometry.h\"" not in
        preview_controller_source
        or "int instance_id_from_map(" in preview_controller_source
        or "QVector<QPoint> smart_select_seed_points(" in
        preview_controller_source
        or "src/smart_selection_geometry.cpp" not in desktop_cmake
        or "smart_selection_geometry_test.cpp" not in tests_cmake
    ):
        errors.append(
            "smart selection geometry must own bounded instance-map lookup, counting, and orientation seed construction outside EditorPreviewController"
        )
    if (
        "smart_select_instance_candidates_from_map(" not in
        smart_selection_instance_policy_header
        or "smart_select_instance_candidates_for_path(" not in
        smart_selection_instance_policy_source
        or "segmentation_mask_for_instances(" not in
        smart_selection_instance_policy_source
        or "sorted_nearby_candidates(" not in
        smart_selection_instance_policy_source
        or "#include \"smart_selection_instance_policy.h\"" not in
        preview_controller_source
        or "QVector<int> smart_select_instance_candidates_from_map(" in
        preview_controller_source
        or "QVector<int> smart_select_instance_candidates_for_path(" in
        preview_controller_source
        or "QVariantMap segmentation_mask_for_instances(" in
        preview_controller_source
        or "src/smart_selection_instance_policy.cpp" not in desktop_cmake
        or "smart_selection_instance_policy_test.cpp" not in tests_cmake
    ):
        errors.append(
            "smart selection instance policy must own direct/nearby/flipped arbitration, path voting, stable ranking, and selected-instance mask materialization outside EditorPreviewController"
        )
    if (
        "single_smart_select_candidate_instance_ids(" not in
        smart_selection_polygon_geometry_header
        or "polygon_points_self_intersect(" not in
        smart_selection_polygon_geometry_source
        or "distance_to_segment(" not in
        smart_selection_polygon_geometry_source
        or "simplified_closed_polygon_points(" not in
        smart_selection_polygon_geometry_source
        or "smoothed_closed_polygon_points(" not in
        smart_selection_polygon_geometry_source
        or "#include \"smart_selection_polygon_geometry.h\"" not in
        preview_controller_source
        or "QVector<int> single_smart_select_candidate_instance_ids(" in
        preview_controller_source
        or "bool polygon_points_self_intersect(" in preview_controller_source
        or "QVector<QPointF> simplified_closed_polygon_points(" in
        preview_controller_source
        or "src/smart_selection_polygon_geometry.cpp" not in desktop_cmake
        or "smart_selection_polygon_geometry_test.cpp" not in tests_cmake
    ):
        errors.append(
            "smart selection polygon geometry must own candidate bounds, normalization, intersection, distance, simplification, smoothing, and variant publication outside EditorPreviewController"
        )
    if (
        "normalized_smart_select_prompt_map(" not in
        smart_selection_prompt_schema_header
        or "quantized_smart_select_preview_path(" not in
        smart_selection_prompt_schema_source
        or "smart_select_prompts_for_path(" not in
        smart_selection_prompt_schema_source
        or "smart_select_prompt_metadata_for_path(" not in
        smart_selection_prompt_schema_source
        or "merge_smart_select_prompt_metadata(" not in
        smart_selection_prompt_schema_source
        or "bounds_for_normalized_variant_points(" not in
        smart_selection_prompt_schema_source
        or "#include \"smart_selection_prompt_schema.h\"" not in
        preview_controller_source
        or "QString normalized_smart_select_prompt_type(" in
        preview_controller_source
        or "QVector<SmartSelectPrompt> smart_select_prompts_for_path(" in
        preview_controller_source
        or "QVariantMap smart_select_prompt_metadata_for_path(" in
        preview_controller_source
        or "void merge_smart_select_prompt_metadata(" in
        preview_controller_source
        or "QRectF bounds_for_normalized_variant_points(" in
        preview_controller_source
        or "src/smart_selection_prompt_schema.cpp" not in desktop_cmake
        or "smart_selection_prompt_schema_test.cpp" not in tests_cmake
    ):
        errors.append(
            "smart selection prompt schema must own canonical prompt normalization, path expansion, quantization, sampling, runtime prompt conversion, and audit metadata merging outside EditorPreviewController"
        )
    if (
        "segmentation_mask_variant(" not in segmentation_result_schema_header
        or "apply_generated_confidence_map(" not in
        segmentation_result_schema_source
        or "segmentation_result_bounds(" not in segmentation_result_schema_source
        or "subject_segmentation_result_is_thin_fragment(" not in
        segmentation_result_schema_source
        or "#include \"segmentation_result_schema.h\"" not in
        preview_controller_source
        or "QVariantMap segmentation_mask_variant(" in preview_controller_source
        or "void apply_generated_confidence_map(" in preview_controller_source
        or "QRect segmentation_result_bounds(" in preview_controller_source
        or "src/segmentation_result_schema.cpp" not in desktop_cmake
        or "segmentation_result_schema_test.cpp" not in tests_cmake
    ):
        errors.append(
            "segmentation result schema must own variant publication, confidence provenance, sampling, statistics, bounds, and fragment admission outside EditorPreviewController"
        )
    if (
        "normalized_generated_mask_request_kind(" not in
        segmentation_request_policy_header
        or "is_unsupported_landscape_semantic_mask_kind(" not in
        segmentation_request_policy_source
        or "actionable_segmentation_failure_status(" not in
        segmentation_request_policy_source
        or "segmentation_failure_reason_code(" not in
        segmentation_request_policy_source
        or "#include \"segmentation_request_policy.h\"" not in
        preview_controller_source
        or "QString normalized_generated_mask_request_kind(" in
        preview_controller_source
        or "QString actionable_segmentation_failure_status(" in
        preview_controller_source
        or "src/segmentation_request_policy.cpp" not in desktop_cmake
        or "segmentation_request_policy_test.cpp" not in tests_cmake
    ):
        errors.append(
            "segmentation request policy must own semantic aliases, unsupported-family labels, actionable failure guidance, and stable reason codes outside EditorPreviewController"
        )
    if (
        "should_prefer_prompt_object_pick_result(" not in
        object_pick_policy_header
        or "localized_object_pick_bounds(" not in object_pick_policy_source
        or "segmentation_result_is_localized_square_failure(" not in
        object_pick_policy_source
        or "remap_localized_segmentation_result(" not in
        object_pick_policy_source
        or "single_point_prompt_result_is_plausible(" not in
        object_pick_policy_source
        or "#include \"object_pick_policy.h\"" not in
        preview_controller_source
        or "bool should_prefer_prompt_object_pick_result(" in
        preview_controller_source
        or "QRect localized_object_pick_bounds(" in preview_controller_source
        or "bool single_point_prompt_result_is_plausible(" in
        preview_controller_source
        or "src/object_pick_policy.cpp" not in desktop_cmake
        or "object_pick_policy_test.cpp" not in tests_cmake
    ):
        errors.append(
            "object pick policy must own candidate arbitration, localized crop geometry, crop-shaped failure rejection, bounded remapping, and single-point plausibility outside EditorPreviewController"
        )
    if (
        "refine_localized_object_mask_by_seed_color(" not in
        localized_object_refinement_header
        or "average_local_patch_color(" not in
        localized_object_refinement_source
        or "average_local_perimeter_color(" not in
        localized_object_refinement_source
        or "std::queue<QPoint>" not in localized_object_refinement_source
        or "#include \"localized_object_refinement.h\"" not in
        preview_controller_source
        or "struct LocalColorSample final" in preview_controller_source
        or "SegmentationResult refine_localized_object_mask_by_seed_color(" in
        preview_controller_source
        or "src/localized_object_refinement.cpp" not in desktop_cmake
        or "localized_object_refinement_test.cpp" not in tests_cmake
    ):
        errors.append(
            "localized object refinement must own color sampling, adaptive thresholding, bounded flood fill, and deterministic mask narrowing outside EditorPreviewController"
        )
    if (
        "ensure_brush_raster_mask(" not in brush_input_schema_header
        or "brush_point_variant(" not in brush_input_schema_source
        or "brush_stroke_variant(" not in brush_input_schema_source
        or "brush_preview_point(" not in brush_input_schema_source
        or "brush_points_for_target_raster_replay(" not in
        brush_input_schema_source
        or "dirty_region_for_brush_points(" not in brush_input_schema_source
        or "auto_mask_source_decode_edge_for_brush_commit(" not in
        brush_input_schema_source
        or "#include \"brush_input_schema.h\"" not in preview_controller_source
        or "bool ensure_brush_raster_mask(" in preview_controller_source
        or "QVariantMap brush_stroke_variant(" in preview_controller_source
        or "BrushPreviewPoint brush_preview_point(" in preview_controller_source
        or "QVector<BrushPreviewPoint> brush_points_for_target_raster_replay(" in
        preview_controller_source
        or "src/brush_input_schema.cpp" not in desktop_cmake
        or "brush_input_schema_test.cpp" not in tests_cmake
    ):
        errors.append(
            "brush input schema must own raster admission, physical-input normalization, stroke serialization, dirty geometry, and bounded auto-mask decode sizing outside EditorPreviewController"
        )
    if (
        "normalized_brush_dab_spacing(" not in brush_stroke_sampling_header
        or "sampled_brush_points_for_segment(" not in
        brush_stroke_sampling_source
        or "interpolated_brush_preview_point(" not in
        brush_stroke_sampling_source
        or "resampled_committed_brush_stroke(" not in
        brush_stroke_sampling_source
        or "#include \"brush_stroke_sampling.h\"" not in
        preview_controller_source
        or "double normalized_brush_dab_spacing(" in preview_controller_source
        or "QVector<BrushPreviewPoint> sampled_brush_points_for_segment(" in
        preview_controller_source
        or "QVector<BrushPreviewPoint> resampled_committed_brush_stroke(" in
        preview_controller_source
        or "src/brush_stroke_sampling.cpp" not in desktop_cmake
        or "brush_stroke_sampling_test.cpp" not in tests_cmake
    ):
        errors.append(
            "brush stroke sampling must own spacing, smoothing, interpolation, and committed-stroke resampling outside EditorPreviewController"
        )
    if (
        "rasterize_brush_point_into_mask(" not in brush_rasterizer_header
        or "AutoMaskPerceptualColor" not in brush_rasterizer_source
        or "auto_mask_perceptual_distance(" not in brush_rasterizer_source
        or "auto_mask_edge_barrier_strength(" not in brush_rasterizer_source
        or "#include \"brush_rasterizer.h\"" not in preview_controller_source
        or "struct AutoMaskPerceptualColor final" in preview_controller_source
        or "QRect rasterize_brush_point_into_mask(" in preview_controller_source
        or "src/brush_rasterizer.cpp" not in desktop_cmake
        or "brush_rasterizer_test.cpp" not in tests_cmake
    ):
        errors.append(
            "brush rasterizer must own alpha dab math, perceptual auto-mask gating, edge barriers, cancellation, and bounded mutation outside EditorPreviewController"
        )
    if (
        "mask_with_rebuilt_brush_matte(" not in brush_matte_rebuild_header
        or "scaledMatteRebuiltFromBrushStrokes" not in
        brush_matte_rebuild_source
        or "full_resolution_matte_rebuild_pixels" not in
        brush_matte_rebuild_source
        or "#include \"brush_matte_rebuild.h\"" not in
        scaled_mask_matte_service_source
        or "QVariantMap mask_with_rebuilt_brush_matte(" in
        preview_controller_source
        or "src/brush_matte_rebuild.cpp" not in desktop_cmake
        or "brush_matte_rebuild_test.cpp" not in tests_cmake
    ):
        errors.append(
            "brush matte rebuild must own ordered target-resolution replay and complete rebuild telemetry outside EditorPreviewController"
        )
    if (
        "outline_path_for_brush_samples(" not in
        brush_outline_geometry_header
        or "append_outline_path(" not in brush_outline_geometry_header
        or "fast_outline_paths_for_brush_mask(" not in
        brush_outline_geometry_source
        or "kCircleSegments = 40" not in brush_outline_geometry_source
        or "#include \"brush_outline_geometry.h\"" not in
        preview_controller_source
        or "QVariantList outline_path_for_brush_samples(" in
        preview_controller_source
        or "QVariantList fast_outline_paths_for_brush_mask(" in
        preview_controller_source
        or "src/brush_outline_geometry.cpp" not in desktop_cmake
        or "brush_outline_geometry_test.cpp" not in tests_cmake
    ):
        errors.append(
            "brush outline geometry must own normalized circle/stroke construction, erase filtering, and flat-point fallback outside EditorPreviewController"
        )
    if (
        "struct OutlineSimplificationStats final" not in
        mask_outline_simplification_header
        or "normalized_outline_display_scale(" not in
        mask_outline_simplification_source
        or "tolerance_simplified_closed_outline_points(" not in
        mask_outline_simplification_source
        or "bounded_closed_outline_points(" not in
        mask_outline_simplification_source
        or "simplify_outline_paths_with_pixel_tolerance(" not in
        mask_outline_simplification_source
        or "#include \"mask_outline_simplification.h\"" not in
        preview_controller_source
        or "struct OutlineSimplificationStats final" in
        preview_controller_source
        or "double normalized_outline_display_scale(" in
        preview_controller_source
        or "QVariantList simplify_outline_paths_with_pixel_tolerance(" in
        preview_controller_source
        or "src/mask_outline_simplification.cpp" not in desktop_cmake
        or "mask_outline_simplification_test.cpp" not in tests_cmake
    ):
        errors.append(
            "mask outline simplification must own scale quantization, topology-safe pixel simplification, error measurement, point budgets, nested paths, and aggregate stats outside EditorPreviewController"
        )
    if (
        "hull_points_for_segmentation_mask(" not in mask_outline_tracing_header
        or "bounding_box_points_for_segmentation_mask(" not in
        mask_outline_tracing_header
        or "traced_outline_paths_for_alpha_mask(" not in
        mask_outline_tracing_source
        or "traced_outline_paths_for_alpha_mask_with_edge_padding(" not in
        mask_outline_tracing_source
        or "kMinSecondaryOutlineAreaPixels = 16.0" not in
        mask_outline_tracing_source
        or "QSet<QPair<quint64, quint64>> visited_edges" not in
        mask_outline_tracing_source
        or "#include \"mask_outline_tracing.h\"" not in
        preview_controller_source
        or "QVariantList hull_points_for_segmentation_mask(" in
        preview_controller_source
        or "QVariantList bounding_box_points_for_segmentation_mask(" in
        preview_controller_source
        or "QVariantList traced_outline_paths_for_alpha_mask(" in
        preview_controller_source
        or "src/mask_outline_tracing.cpp" not in desktop_cmake
        or "mask_outline_tracing_test.cpp" not in tests_cmake
    ):
        errors.append(
            "mask outline tracing must own validated raster admission, deterministic contour walking, hull/bounds fallbacks, loop filtering, simplification, and edge-padding recovery outside EditorPreviewController"
        )
    if (
        "struct OutlineNoiseFilterStats final" not in
        mask_outline_presentation_header
        or "high_resolution_brush_outline_size(" not in
        mask_outline_presentation_source
        or "clean_canvas_outline_matches_final_single_mask(" not in
        mask_outline_presentation_source
        or "filter_minor_composite_outline_paths(" not in
        mask_outline_presentation_source
        or "#include \"mask_outline_presentation.h\"" not in
        preview_controller_source
        or "bool mask_selection_refinement_active(" in
        preview_controller_source
        or "QVariantList outline_paths_for_mask(" in
        preview_controller_source
        or "struct OutlineNoiseFilterStats final" in
        preview_controller_source
        or "QVariantList filter_minor_composite_outline_paths(" in
        preview_controller_source
        or "src/mask_outline_presentation.cpp" not in desktop_cmake
        or "mask_outline_presentation_test.cpp" not in tests_cmake
    ):
        errors.append(
            "mask outline presentation must own vector/brush/raster routing, clean-canvas eligibility, source-dependent brush policy, path selection, and composite noise filtering outside EditorPreviewController"
        )
    if (
        "update_color_range_constraints_from_samples(" not in
        mask_range_preview_math_header
        or "preview_white_balance_multipliers(" not in
        mask_range_preview_math_source
        or "depth_refine_value_for_preview(" not in
        mask_range_preview_math_source
        or "apply_full_refine_to_preview_weight(" not in
        mask_range_preview_math_source
        or "#include \"mask_range_preview_math.h\"" not in
        preview_controller_source
        or "double circular_weight(" in preview_controller_source
        or "void update_color_range_constraints_from_samples(" in
        preview_controller_source
        or "std::array<double, 3> preview_white_balance_multipliers(" in
        preview_controller_source
        or "double apply_full_refine_to_preview_weight(" in
        preview_controller_source
        or "src/mask_range_preview_math.cpp" not in desktop_cmake
        or "mask_range_preview_math_test.cpp" not in tests_cmake
    ):
        errors.append(
            "mask range preview math must own circular color sampling, luminance/depth constraints, preview white balance, selection refinement, depth interpolation, and combined range weighting outside EditorPreviewController"
        )
    current_render_request_parts = preview_controller_source.split(
        "RenderRequest EditorPreviewController::currentRenderRequest(bool full_resolution) const {",
        1,
    )
    current_render_request_body = (
        current_render_request_parts[1].split(
            "void EditorPreviewController::setStatus(", 1
        )[0]
        if len(current_render_request_parts) == 2
        else ""
    )
    grouped_mask_sets_parts = thumbnail_presentation_service_source.split(
        "QVariantList ThumbnailPresentationService::groupMaskSets(",
        1,
    )
    grouped_mask_sets_body = (
        grouped_mask_sets_parts[1].split(
            "void ThumbnailPresentationService::copyThumbnailFields(", 1
        )[0]
        if len(grouped_mask_sets_parts) == 2
        else ""
    )
    if (
        "render_request_state_signature(" not in current_render_request_body
        or "deferred_layer_composite_mask_alpha_trace_fields(" not in
        current_render_request_body
        or "publish_viewport_overlay_only" not in current_render_request_body
        or ".overlay_only = publish_viewport_overlay_only" not in
        current_render_request_body
        or "edit_state_signature(" in current_render_request_body
        or "= layer_composite_mask_alpha_trace_fields(" in
        current_render_request_body
        or "mask_alpha_availability_fields(" not in grouped_mask_sets_body
        or "mask_alpha_trace_deferred" not in grouped_mask_sets_body
        or "mask_source_hash" in grouped_mask_sets_body
        or "mask_alpha_trace_hash" in grouped_mask_sets_body
        or "mask_alpha_trace_deferred" not in preview_controller_source
        or "request_has_deferred_mask_payload_identity(" not in
        preview_controller_source
    ):
        errors.append(
            "render request admission and grouped mask presentation must preserve immediate native-texture overlay feedback while deferring mask pixel hashing, exact alpha traces, and cache identities to workers"
        )
    if (
        "class PresetService final" not in preset_service
        or "replaceBuiltIns(" not in preset_service
        or "upsertUser(" not in preset_service
        or "renameUser(" not in preset_service
        or "moveUserToPack(" not in preset_service
        or "removeUser(" not in preset_service
        or "setFavorite(" not in preset_service
        or "setIntensity(" not in preset_service
        or "setCameraDefault(" not in preset_service
        or "PresetService preset_service_;" not in controller_header
        or "preset_service_.replaceBuiltIns(" not in preview_controller_source
        or "preset_service_.upsertUser(" not in preview_controller_source
        or "preset_service_.renameUser(" not in preview_controller_source
        or "PHOTO_EDITOR_BUILT_IN_PRESET_REGISTRY_BEGIN" not in preset_registry
        or "PHOTO_EDITOR_BUILT_IN_PRESET_REGISTRY_END" not in preset_registry
        or "built_in_preset_definitions()" not in preset_registry
        or "built_in_preset_map()" not in preset_registry
        or "PHOTO_EDITOR_BUILT_IN_PRESET_REGISTRY_BEGIN" in preview_controller_source
        or "struct BuiltInPresetDefinition" in preview_controller_source
        or any(
            token in controller_header
            for token in (
                "built_in_presets_",
                "built_in_preset_packs_",
                "built_in_preset_categories_",
                "built_in_preset_groups_",
                "built_in_preset_adaptive_rules_",
                "built_in_preset_contracts_",
                "QMap<QString, EditState> presets_",
                "user_preset_packs_",
                "user_preset_categories_",
                "user_preset_groups_",
                "user_preset_adaptive_rules_",
                "user_preset_contracts_",
                "favorite_presets_",
                "preset_intensity_",
                "camera_default_states_",
            )
        )
        or "src/preset_service.h" not in desktop_cmake
        or "src/preset_registry.cpp" not in desktop_cmake
        or "preset_service_test.cpp" not in tests_cmake
        or "preset_registry_test.cpp" not in tests_cmake
    ):
        errors.append(
            "PresetService must own built-in and user presets, metadata, favorites, intensity, and camera defaults"
        )
    if (
        "class HistoryService final" not in history_service
        or "undoStack(" not in history_service
        or "redoStack(" not in history_service
        or "clearAssetHistory(" not in history_service
        or "snapshotStack(" not in history_service
        or "replaceSnapshots(" not in history_service
        or "touchAsset(" not in history_service
        or "beginApplyingState()" not in history_service
        or "beginTransaction(" not in history_service
        or "captureTransactionUndo(" not in history_service
        or "endTransaction()" not in history_service
        or "HistoryService history_service_;" not in controller_header
        or "history_service_.beginTransaction(" not in preview_controller_source
        or "history_service_.captureTransactionUndo(" not in preview_controller_source
        or "history_service_.replaceSnapshots(" not in preview_controller_source
        or any(
            token in controller_header
            for token in (
                "asset_states_",
                "asset_undo_states_",
                "asset_redo_states_",
                "asset_undo_labels_",
                "asset_redo_labels_",
                "asset_history_snapshots_",
                "asset_access_order_",
                "applying_history_state_",
                "edit_transaction_depth_",
                "edit_transaction_persist_requested_",
                "edit_transaction_asset_id_",
                "edit_transaction_undo_state_",
                "edit_transaction_undo_label_",
                "edit_transaction_undo_state_valid_",
            )
        )
        or "src/history_service.h" not in desktop_cmake
        or "history_service_test.cpp" not in tests_cmake
    ):
        errors.append(
            "HistoryService must own cached asset states, undo/redo labels, snapshots, application guards, eviction, and edit transactions"
        )
    if (
        "kHistogramScopeServiceContract" not in histogram_scope_service_header
        or "compute_histogram_scope_data(" not in histogram_scope_service_header
        or "HistogramComputationResult compute_histogram_scope_data(" not in
        histogram_scope_service_source
        or "constexpr int kInternalBins = 4096" not in histogram_scope_service_source
        or "constexpr int kScopeColumns = 128" not in histogram_scope_service_source
        or "constexpr int kScopeLevels = 64" not in histogram_scope_service_source
        or "constexpr int kVectorscopeSampleTarget = 1800" not in
        histogram_scope_service_source
        or "#include \"histogram_scope_service.h\"" not in controller_header
        or "compute_histogram_scope_data(" not in preview_controller_source
        or "compute_histogram_data(" in preview_controller_source
        or "struct HistogramScratch" in preview_controller_source
        or "src/histogram_scope_service.cpp" not in desktop_cmake
        or "histogram_scope_service_test.cpp" not in tests_cmake
    ):
        errors.append(
            "HistogramScopeService must own histogram, waveform, density, vectorscope, and clipping computation outside EditorPreviewController"
        )
    if (
        "class HistogramScopeCoordinator final" not in histogram_scope_coordinator
        or "HistogramScopeSubmitDisposition submit(" not in
        histogram_scope_coordinator
        or "std::optional<HistogramScopeRequest> pending_request_;" not in
        histogram_scope_coordinator
        or "QFutureWatcher<HistogramComputationResult> watcher_;" not in
        histogram_scope_coordinator
        or "HistogramScopeCoordinator histogram_scope_coordinator_;" not in
        controller_header
        or "histogram_scope_coordinator_.submit(" not in
        preview_controller_source
        or "histogram_scope_coordinator_.complete()" not in
        preview_controller_source
        or any(
            token in controller_header
            for token in (
                "histogram_watcher_",
                "pending_histogram_image_",
                "pending_histogram_generation_",
                "pending_histogram_full_resolution_",
                "pending_histogram_raw_clip_sample_count_",
                "pending_histogram_raw_shadow_clip_count_",
                "pending_histogram_raw_highlight_clip_count_",
                "pending_histogram_render_reason_",
                "pending_histogram_interaction_mode_",
                "histogram_update_pending_",
                "histogram_update_active_",
            )
        )
        or "histogram_scope_coordinator_test.cpp" not in tests_cmake
    ):
        errors.append(
            "HistogramScopeCoordinator must own the async watcher, latest-request coalescing snapshot, and active lifecycle outside EditorPreviewController"
        )
    if (
        "class HistogramScopeSession final" not in histogram_scope_session
        or "void publish(const HistogramComputationResult& result)" not in
        histogram_scope_session
        or "void updateRawClipping(" not in histogram_scope_session
        or "normalized_histogram_scope_domain(" not in histogram_scope_session
        or "histogram_scope_domain_label(" not in histogram_scope_session
        or "bool setVisible(bool visible)" not in histogram_scope_session
        or "bool setDomain(" not in histogram_scope_session
        or "bool reconcileProofAvailability(" not in histogram_scope_session
        or "HistogramScopeSession histogram_scope_session_;" not in
        controller_header
        or "histogram_scope_session_.publish(result);" not in
        preview_controller_source
        or "histogram_scope_session_.updateRawClipping(" not in
        preview_controller_source
        or "histogram_scope_session_.setVisible(" not in
        preview_controller_source
        or "histogram_scope_session_.setDomain(" not in
        preview_controller_source
        or "histogram_scope_session_.reconcileProofAvailability(" not in
        preview_controller_source
        or "normalized_scope_domain_name(" in preview_controller_source
        or "scope_domain_label_for_name(" in preview_controller_source
        or any(
            token in controller_header
            for token in (
                "histogram_red_",
                "histogram_green_",
                "histogram_blue_",
                "histogram_luminance_",
                "scope_waveform_red_",
                "scope_density_red_",
                "scope_vectorscope_points_",
                "histogram_shadow_clip_count_",
                "histogram_highlight_clip_count_",
                "histogram_sample_count_",
                "raw_clip_sample_count_",
                "raw_shadow_clip_count_",
                "raw_highlight_clip_count_",
                "scopes_visible_",
                "scope_domain_",
            )
        )
        or "histogram_scope_session_test.cpp" not in tests_cmake
    ):
        errors.append(
            "HistogramScopeSession must atomically own typed publication, visibility, normalized domain labels, and proof-availability policy outside EditorPreviewController"
        )
    if (
        'object.insert(QStringLiteral("recipeJson"), item.recipe_json)'
        in catalog_asset_model
        or 'map.insert(QStringLiteral("recipeJson"), item.recipe_json)'
        in catalog_asset_model
        or "RecipeJsonRole" in
        catalog_asset_header
        or "selectedRecipeJson" in
        catalog_asset_header
        or "QString recipe_json;" in
        catalog_asset_header
        or "before_recipe_jsons" in
        catalog_asset_header
        or "after_recipe_jsons" in
        catalog_asset_header
        or "parse_edit_recipe_json(" in
        catalog_asset_model
        or '{AdjustmentPatchJsonRole, "adjustmentPatchJson"}'
        not in catalog_asset_model
        or "QString adjustment_patch_json;" not in
        catalog_asset_header
        or "before_adjustment_patch_jsons" not in
        catalog_asset_header
        or "after_adjustment_patch_jsons" not in
        catalog_asset_header
        or "selectedAdjustmentPatchJson" not in
        catalog_asset_model
    ):
        errors.append(
            "catalog recovery and export projections must write asset-bound adjustment patches"
        )
    if (
        "photo-v4-canonical-metadata" in
        catalog_asset_model
        or "photo-v5-canonical-adjustments" not in
        catalog_asset_model
        or "thumbnail_adjustment_identity(" not in
        catalog_asset_model
        or "canonical_asset_bound_adjustment_patch_json("
        not in catalog_asset_model
    ):
        errors.append(
            "catalog thumbnail cache identities must derive from canonical adjustment patches, not v12 recipe bytes"
        )
    if (
        "applyRecipeJsonToAssetIds(" in
        agent_edit_controller
        or "applyRecipeJsonToAssetIds(" in
        catalog_asset_model
        or "applyRecipeJsonToAssetIds(" in
        catalog_asset_header
        or "applyPresetRecipeJsonToAssetIds(" in
        catalog_asset_model
        or "setImportPresetRecipeJson" in
        catalog_asset_header
        or "clearImportPresetRecipeJson" in
        catalog_asset_header
        or "setImportPresetAdjustmentPatchJson" not in
        catalog_asset_header
        or "clearImportPresetAdjustmentPatchJson" not in
        catalog_asset_header
        or "applyAdjustmentPatchJsonToAssetIds(" not in
        agent_edit_controller
        or "selectedRecipeJson()" in
        agent_edit_controller
        or "serialize_edit_recipe_json(" in
        agent_edit_controller
        or agent_edit_controller.count(
            'value(QStringLiteral("recipeJson"))') != 1
        or "compatibility_adjustments_for_asset(" not in
        agent_edit_controller
        or "draft_original_recipes_" in
        agent_edit_controller
        or "draft_original_recipes_" in
        (
            REPO_ROOT
            / "apps/desktop/src/agent_edit_controller.h"
        ).read_text(encoding="utf-8")
        or "draft_original_adjustment_patches_" not in
        agent_edit_controller
        or "current_recipe_json" in
        (
            REPO_ROOT
            / "apps/desktop/src/agent_local_model.h"
        ).read_text(encoding="utf-8")
        or "current_adjustment_patch_json" not in
        (
            REPO_ROOT
            / "apps/desktop/src/agent_local_model.h"
        ).read_text(encoding="utf-8")
    ):
        errors.append(
            "catalog and agent read/write/model surfaces must use adjustment patches outside the isolated legacy compatibility reader"
        )
    if (
        "render_agent_proof_image(" not in agent_edit_controller
        or "RenderPlanStore::shared().admit(" not in
        agent_edit_controller
        or agent_edit_controller.count("render_output_image(") != 1
    ):
        errors.append(
            "agent image-producing surfaces must route modern proofs through one canonical compiler/executor helper"
        )
    if (
        "applyColorAdjustments(" in controller
        or "applyColorAdjustments(" in controller_header
    ):
        errors.append(
            "EditorPreviewController must not expose a request-less direct color-adjustment bypass"
        )
    if (
        "photo_read_adjustments" not in
        photo_automation_bridge
        or "selectedAdjustmentPatchJson()" not in
        photo_automation_bridge
        or '"photo.read_adjustments"' not in
        photo_mcp_server
    ):
        errors.append(
            "automation and MCP must expose canonical asset-bound adjustments as the primary read surface"
        )
    export_controller = (
        REPO_ROOT
        / "apps/desktop/src/export_job_controller.cpp"
    ).read_text(encoding="utf-8")
    job_runner = (
        REPO_ROOT / "apps/desktop/src/job_runner.cpp"
    ).read_text(encoding="utf-8")
    job_store = (
        REPO_ROOT / "apps/desktop/src/job_store.cpp"
    ).read_text(encoding="utf-8")
    if (
        "canonical_asset_bound_adjustment_patch_json("
        not in export_controller
        or "parse_edit_recipe_json(" in export_controller
        or "parse_edit_recipe_json(" in job_runner
        or "adjustment_payload" not in job_store
        or 'value(QStringLiteral("recipeJson"))' in export_controller
        or 'value(QStringLiteral("adjustmentPatchJson"))' not in export_controller
    ):
        errors.append(
            "new export jobs and batch asset handoffs must use asset-bound patches and render without direct v12 parsing"
        )
    main_qml = (
        REPO_ROOT / "apps/desktop/qml/Main.qml"
    ).read_text(encoding="utf-8")
    all_qml = "\n".join(
        path.read_text(encoding="utf-8")
        for path in (
            REPO_ROOT / "apps/desktop/qml"
        ).rglob("*.qml")
    )
    library_qml = (
        REPO_ROOT / "apps/desktop/qml/workspaces/LibraryView.qml"
    ).read_text(encoding="utf-8")
    main_cpp = (
        REPO_ROOT / "apps/desktop/src/main.cpp"
    ).read_text(encoding="utf-8")
    app_shell = (
        REPO_ROOT / "apps/desktop/src/app_shell_controller.cpp"
    ).read_text(encoding="utf-8")
    app_shell_header = (
        REPO_ROOT / "apps/desktop/src/app_shell_controller.h"
    ).read_text(encoding="utf-8")
    carousel = (
        REPO_ROOT
        / "apps/desktop/src/carousel_document_controller.cpp"
    ).read_text(encoding="utf-8")
    carousel_header = (
        REPO_ROOT
        / "apps/desktop/src/carousel_document_controller.h"
    ).read_text(encoding="utf-8")
    if (
        "recoveredRecipeJson()" in main_qml
        or "recoveredRecipeJson" in app_shell
        or "recoveredRecipeJson" in app_shell_header
        or "recovered_recipe_json_" in app_shell
        or "recovered_recipe_json_" in app_shell_header
        or "applyRecipeJsonToAssetIds" in main_qml
        or "currentRecipeJson()" in main_cpp
        or 'root.insert(QStringLiteral("recipeJson")' in app_shell
        or "recoveredAdjustmentPatchJson()" not in main_qml
        or "currentAssetBoundAdjustmentPatchJson()" not in main_cpp
        or "recipeJson" in library_qml
        or "adjustmentPatchJson:" not in library_qml
    ):
        errors.append(
            "new crash-session writes and recovery handoffs must use asset-bound adjustment patches"
        )
    if any(
        token in all_qml
        for token in (
            "selectedRecipeJson",
            "currentRecipeJson()",
            "applyRecipeJsonToAssetIds",
            "applyRecipeToPhotoEditTarget",
            "asset.recipeJson",
            "applyExternalRecipeEdit",
            "previousRecipeJsonByAssetId",
            "committedRecipeJsonByAssetId",
        )
    ):
        errors.append(
            "production QML must exchange adjustment patches instead of v12 recipe JSON"
        )
    if (
        "raw_corpus_test" in tests_cmake
        or "raw_corpus_test" in (
            REPO_ROOT / "scripts/run-mask-release-gate.sh"
        ).read_text(encoding="utf-8")
        or not (
            REPO_ROOT / "tests/synthetic_raw_pipeline_invariant_test.cpp"
        ).is_file()
        or "synthetic_raw_pipeline_invariant_test" not in tests_cmake
        or not (
            REPO_ROOT / "scripts/color/publish-camera-support-report.py"
        ).is_file()
    ):
        errors.append(
            "constructed RAW patches must remain explicitly synthetic and real-camera support publication must retain its evidence tool"
        )
    if (
        "applyExternalRecipeEdit" in
        (
            REPO_ROOT
            / "apps/desktop/src/editor_preview_controller.h"
        ).read_text(encoding="utf-8")
        or any(
            token in
            (
                REPO_ROOT
                / "apps/desktop/src/editor_preview_controller.h"
            ).read_text(encoding="utf-8")
            for token in (
                "currentRecipeJson",
                "copiedSettingsRecipeJson",
                "presetRecipeJson",
                "presetPreviewRecipeJson",
                "currentCompatibilityRecipeJson",
                "compatibilityRecipeJsonForState",
            )
        )
        or "applyExternalAdjustmentPatchEdit" not in
        (
            REPO_ROOT
            / "apps/desktop/src/editor_preview_controller.h"
        ).read_text(encoding="utf-8")
        or "stateFromAdjustmentPayload" not in
        (
            REPO_ROOT
            / "apps/desktop/src/editor_preview_controller.h"
        ).read_text(encoding="utf-8")
        or "currentAssetBoundAdjustmentPatchJson" not in
        (
            REPO_ROOT
            / "apps/desktop/src/editor_preview_controller.h"
        ).read_text(encoding="utf-8")
        or "serialize_edit_recipe_json(" in
        preview_controller_source
    ):
        errors.append(
            "editor public state and agent history handoffs must use adjustment patches; v12 serialization is migration-reader-only"
        )
    if (
        "applyRecipeToPhotoEditTarget(" in main_qml
        or "currentRecipeJson()" in main_qml
        or 'object.insert(QStringLiteral("recipeJson"), media.recipe_json)' in carousel
        or 'object.insert(QStringLiteral("recipeJson"), item.recipe_json)' in carousel
        or "serialize_edit_recipe_json(" in carousel
        or "recipe_json" in carousel
        or "recipe_json" in carousel_header
        or "recipeStateFromJson" in carousel
        or "recipeStateFromJson" in carousel_header
        or "applyCanonicalRecipeToPhotoEditTarget" in carousel
        or "applyCanonicalRecipeToPhotoEditTarget" in carousel_header
        or carousel.count(
            'value(QStringLiteral("recipeJson"))') != 2
        or "adjustment_patch_json" not in carousel_header
        or "applyAdjustmentPatchToPhotoEditTarget(" not in main_qml
        or 'QStringLiteral("adjustmentPatch")' not in carousel
    ):
        errors.append(
            "new Carousel editor handoffs and document writes must use asset-bound adjustment patches"
        )
    if any(
        token in studio_workspace
        for token in (
            "component RowRule:",
            "component SectionTitle:",
            "component Panel:",
            "component TinyInput:",
            "component RgbMixerMatrix: Rectangle",
            "component HueRangeControl: Rectangle",
            "component DetailTextureControl: Rectangle",
            "component LensCorrectionControl: Rectangle",
            "component ScopeTile: Rectangle",
            "component ScopeGrid: GridLayout",
            "component ScopeDockContent: ColumnLayout",
            "component ThumbnailCard: Rectangle",
            "component ImageViewport: Rectangle",
            "function openIccProfileDialog(",
            'objectName: "studioPreviewOutputModeCombo"',
        )
    ):
        errors.append(
            "StudioWorkspace.qml must not reclaim extracted structural, shared-control, or color-management components"
        )
    if (
        "qml/components/StudioRowRule.qml" not in desktop_cmake
        or "qml/components/StudioSectionTitle.qml" not in desktop_cmake
        or "qml/components/StudioPanel.qml" not in desktop_cmake
        or "qml/components/StudioTinyInput.qml" not in desktop_cmake
        or "qml/components/StudioColorManagementPanel.qml" not in desktop_cmake
        or "qml/components/StudioRgbMixerMatrix.qml" not in desktop_cmake
        or "qml/components/StudioHueRangeControl.qml" not in desktop_cmake
        or "qml/components/StudioDetailTextureControl.qml" not in desktop_cmake
        or "qml/components/StudioLensCorrectionControl.qml" not in desktop_cmake
        or "qml/components/StudioScopeTile.qml" not in desktop_cmake
        or "qml/components/StudioScopeGrid.qml" not in desktop_cmake
        or "qml/components/StudioScopeDockContent.qml" not in desktop_cmake
        or "qml/components/StudioThumbnailCard.qml" not in desktop_cmake
        or "qml/components/StudioImageViewport.qml" not in desktop_cmake
        or "qml/components/StudioPixelGridOverlay.qml" not in desktop_cmake
        or "qml/components/StudioDiagnosticOverlayStack.qml" not in desktop_cmake
        or "qml/components/StudioViewportStatusStack.qml" not in desktop_cmake
        or "qml/components/StudioViewportLoupe.qml" not in desktop_cmake
        or "qml/components/StudioViewportBrushCursors.qml" not in desktop_cmake
        or "qml/components/StudioRetouchOverlayCommandSurface.qml" not in desktop_cmake
        or "qml/components/StudioViewportInputCommandSurface.qml" not in desktop_cmake
        or "studio_bounded_input_test.cpp" not in tests_cmake
        or "StudioColorManagementPanel {" not in studio_workspace
        or "component RgbMixerMatrix: StudioRgbMixerMatrix" not in studio_workspace
        or "component HueRangeControl: StudioHueRangeControl" not in studio_workspace
        or "component DetailTextureControl: StudioDetailTextureControl" not in studio_workspace
        or "component LensCorrectionControl: StudioLensCorrectionControl" not in studio_workspace
        or "component ScopeTile: StudioScopeTile" not in studio_workspace
        or "component ScopeGrid: StudioScopeGrid" not in studio_workspace
        or "component ScopeDockContent: StudioScopeDockContent" not in studio_workspace
        or "component ThumbnailCard: StudioThumbnailCard" not in studio_workspace
        or "StudioPixelGridOverlay {" not in studio_image_viewport
        or "StudioDiagnosticOverlayStack {" not in studio_image_viewport
        or "StudioViewportStatusStack {" not in studio_image_viewport
        or "StudioViewportLoupe {" not in studio_image_viewport
        or "StudioViewportBrushCursors {" not in studio_image_viewport
        or "StudioRetouchOverlayCommandSurface {" not in studio_image_viewport
        or "StudioViewportInputCommandSurface {" not in studio_image_viewport
        or 'objectName: "studioPreviewOutputModeCombo"'
        not in studio_color_management
        or "function openIccProfileDialog(slot, relink)"
        not in studio_color_management
        or "function openDcpProfileDialog(relink)"
        not in studio_color_management
    ):
        errors.append(
            "the desktop QML module must publish extracted Studio components"
        )
    if (
        "id: cloneOffsetLine" in studio_workspace
        or "id: patchSourceRegion" in studio_workspace
        or "id: cloneTargetPreview" in studio_workspace
        or "id: cloneSourcePin" in studio_workspace
        or "previewController.setActiveMaskPatchSourceRegion(" not in studio_retouch_overlay
        or "previewController.setActiveMaskCloneSource(" not in studio_retouch_overlay
        or "viewport.normalizedPointForViewport(mapped.x, mapped.y)" not in studio_retouch_overlay
    ):
        errors.append(
            "Studio retouch overlay geometry and mapped clone/patch commands must remain in the extracted command surface"
        )
    if (
        "id: viewportMouse" in studio_workspace
        or 'inputHandlerRole: "pan-zoom"' in studio_workspace
        or 'inputHandlerRole: "pan-zoom"' not in studio_viewport_input
        or "viewport.handleSamplingPointerPressed(mouse)" not in studio_viewport_input
        or "viewport.handleMaskPointerPressed(mouse)" not in studio_viewport_input
        or "viewport.wheelZoomFactor(wheel)" not in studio_viewport_input
    ):
        errors.append(
            "Studio pan, zoom, sampling, and tool input dispatch must remain in the extracted viewport input command surface"
        )
    if (
        "luminanceRangeSamplePatch" in studio_thumbnail_card
        or 'objectName: "luminanceRangeSamplePatch"' not in studio_image_viewport
        or "viewport.viewRectForImage(" not in studio_image_viewport
    ):
        errors.append(
            "luminance range sampling presentation must remain owned by ImageViewport, not ThumbnailCard"
        )
    if (
        "property int editCommitIntervalMs: 16" not in studio_detail_texture
        or "property var pendingValues: ({})" not in studio_detail_texture
        or "function queueValueEdited(key, value)" not in studio_detail_texture
        or "function flushValueEdits()" not in studio_detail_texture
        or 'queueValueEdited("sharpenRadius", nextRadius)' not in studio_detail_texture
        or 'queueValueEdited("sharpening", nextAmount)' not in studio_detail_texture
        or "detailControl.flushValueEdits()\n                        detailControl.interactionFinished()"
        not in studio_detail_texture
    ):
        errors.append(
            "Studio detail input must retain keyed 16 ms leading/trailing edit delivery and flush before finishing an interaction"
        )
    if (
        "property int editCommitIntervalMs: 16" not in studio_lens_correction
        or "property var pendingValues: ({})" not in studio_lens_correction
        or "function queueValueEdited(key, value)" not in studio_lens_correction
        or "function flushValueEdits()" not in studio_lens_correction
        or 'queueValueEdited("lensDistortion"' not in studio_lens_correction
        or 'queueValueEdited("lensVignette"' not in studio_lens_correction
        or 'queueValueEdited("chromaticAberration"' not in studio_lens_correction
        or "lensControl.flushValueEdits(); lensControl.interactionFinished()"
        not in studio_lens_correction
    ):
        errors.append(
            "Studio lens input must retain keyed 16 ms leading/trailing delivery for geometry and chromatic edits and flush before finishing"
        )
    if (
        "qml/workspaces/EditView.qml" in desktop_cmake
        or "qml/workspaces/EditWorkspace.qml" in desktop_cmake
        or "qml/workspaces/EditWorkspaceHost.qml" in desktop_cmake
        or (
            REPO_ROOT / "apps/desktop/qml/workspaces/EditView.qml"
        ).exists()
        or (
            REPO_ROOT / "apps/desktop/qml/workspaces/EditWorkspace.qml"
        ).exists()
        or (
            REPO_ROOT / "apps/desktop/qml/workspaces/EditWorkspaceHost.qml"
        ).exists()
        or 'id: editView\n\n        StudioWorkspace {' not in main_qml
    ):
        errors.append(
            "the shipped edit workspace must use StudioWorkspace and all obsolete Edit workspace surfaces must stay deleted"
        )
    if "src/edit_recipe_sidecar.cpp" not in desktop_cmake:
        errors.append(
            "the desktop app must compile the extracted recipe sidecar service"
        )
    if "src/edit_recipe_diff.cpp" not in desktop_cmake:
        errors.append(
            "the desktop app must compile the extracted recipe diff service"
        )
    if "src/edit_recipe_document_codec.cpp" not in desktop_cmake:
        errors.append(
            "the desktop app must compile the extracted recipe document codec"
        )
    if "src/edit_recipe_migration.cpp" not in desktop_cmake:
        errors.append(
            "the desktop app must compile the extracted recipe migration service"
        )
    if "src/edit_recipe_profile_validation.cpp" not in desktop_cmake:
        errors.append(
            "the desktop app must compile the extracted recipe profile validator"
        )
    if "src/edit_recipe_content_validation.cpp" not in desktop_cmake:
        errors.append(
            "the desktop app must compile the extracted recipe content validator"
        )
    if "src/edit_recipe_validation.cpp" not in desktop_cmake:
        errors.append(
            "the desktop app must compile the extracted recipe document validator"
        )
    if "src/edit_recipe_profile_codec.cpp" not in desktop_cmake:
        errors.append(
            "the desktop app must compile the extracted recipe profile codec"
        )
    if "src/edit_recipe_curve_codec.cpp" not in desktop_cmake:
        errors.append(
            "the desktop app must compile the extracted recipe curve codec"
        )
    if "src/edit_recipe_layer_codec.cpp" not in desktop_cmake:
        errors.append(
            "the desktop app must compile the extracted recipe layer codec"
        )
    if "src/edit_recipe_mask_schema.h" not in desktop_cmake:
        errors.append(
            "the desktop app must publish the extracted recipe mask schema"
        )
    if "src/edit_recipe_json_readers.cpp" not in desktop_cmake:
        errors.append(
            "the desktop app must compile the extracted recipe JSON readers"
        )
    if "src/edit_recipe_history.cpp" not in desktop_cmake:
        errors.append(
            "the desktop app must compile the extracted recipe history service"
        )
    if "src/edit_recipe_model_normalization.cpp" not in desktop_cmake:
        errors.append(
            "the desktop app must compile the extracted recipe model normalizer"
        )
    if "src/edit_recipe_group_normalization.cpp" not in desktop_cmake:
        errors.append(
            "the desktop app must compile the extracted recipe group normalizer"
        )
    if "src/qimage_color_conversion.h" not in desktop_cmake:
        errors.append(
            "the desktop app must publish the explicit-purpose QImage color conversion boundary"
        )
    return errors


def check_production_event_loop_pumping() -> list[str]:
    errors: list[str] = []
    desktop_source = REPO_ROOT / "apps" / "desktop" / "src"
    observed: dict[str, int] = {}
    for path in desktop_source.rglob("*"):
        if path.suffix not in {".cpp", ".h", ".mm"}:
            continue
        text = path.read_text(encoding="utf-8")
        count = text.count("QCoreApplication::processEvents") + text.count(
            "QApplication::processEvents"
        )
        if count:
            observed[path.relative_to(REPO_ROOT).as_posix()] = count

    for relative_path, count in observed.items():
        maximum = LEGACY_PRODUCTION_PROCESS_EVENT_COUNTS.get(relative_path)
        if maximum is None:
            errors.append(
                f"{relative_path} introduces production event-loop pumping; use an asynchronous adapter"
            )
        elif count > maximum:
            errors.append(
                f"{relative_path} added an unreviewed processEvents call ({count} found, {maximum} allowed)"
            )
    return errors


def check_bounded_process_wait_ownership() -> list[str]:
    errors: list[str] = []
    owner = REPO_ROOT / "apps/desktop/src/bounded_process_wait.h"
    if not owner.exists() or "inline bool bounded_process_succeeded" not in owner.read_text(
        encoding="utf-8"
    ):
        errors.append("bounded process waiting must remain owned by bounded_process_wait.h")
        return errors
    for path in (REPO_ROOT / "apps/desktop/src").glob("*.cpp"):
        if "bool bounded_process_succeeded(" in path.read_text(encoding="utf-8"):
            errors.append(
                f"{path.relative_to(REPO_ROOT).as_posix()} duplicates bounded process wait policy"
            )
    return errors


def check_catalog_helper_process_ownership() -> list[str]:
    errors: list[str] = []
    bootstrap_header = (
        REPO_ROOT / "apps/desktop/src/app_bootstrap.h"
    ).read_text(encoding="utf-8")
    bootstrap_source = (
        REPO_ROOT / "apps/desktop/src/app_bootstrap.cpp"
    ).read_text(encoding="utf-8")
    if (
        "static bool runCatalogHelper(" not in bootstrap_header
        or "bool AppBootstrap::runCatalogHelper(" not in bootstrap_source
    ):
        errors.append(
            "catalog helper process execution must remain owned by AppBootstrap::runCatalogHelper"
        )

    delegated_owners = (
        "apps/desktop/src/job_store.cpp",
        "apps/desktop/src/timeline_asset_store.cpp",
        "apps/desktop/src/carousel_document_controller.cpp",
    )
    for relative_path in delegated_owners:
        source = (REPO_ROOT / relative_path).read_text(encoding="utf-8")
        if "AppBootstrap::runCatalogHelper(" not in source:
            errors.append(
                f"{relative_path} must delegate catalog helper execution to AppBootstrap"
            )
        if "prepareProcessEnvironment(" in source:
            errors.append(
                f"{relative_path} duplicates catalog helper environment/process policy"
            )
    return errors


def main() -> int:
    debt_errors = (
        check_maintainability_boundaries()
        + check_production_event_loop_pumping()
        + check_bounded_process_wait_ownership()
        + check_catalog_helper_process_ownership()
    )
    if debt_errors:
        for error in debt_errors:
            print(f"architecture: error: {error}", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory() as temporary:
        fixture = Path(temporary) / "fixture"
        fixture.mkdir()
        write_project(fixture, "#include <vector>\nint boundary_value() { return 1; }\n")
        clean = configure(fixture, Path(temporary) / "build-clean")
        if clean.returncode != 0:
            print(clean.stdout + clean.stderr, file=sys.stderr)
            return 1

        write_project(fixture, "#include <QImage>\nint boundary_value() { return 1; }\n")
        qt_import = configure(fixture, Path(temporary) / "build-import")
        if (
            qt_import.returncode == 0
            or "imports a Qt header" not in qt_import.stdout + qt_import.stderr
        ):
            print("Qt source imports must fail the engine boundary", file=sys.stderr)
            return 1

        write_project(
            fixture,
            "#include <vector>\nint boundary_value() { return 1; }\n",
            link_qt=True,
        )
        qt_link = configure(fixture, Path(temporary) / "build-link")
        if (
            qt_link.returncode == 0
            or "is Qt-independent but links Qt6::Core"
            not in qt_link.stdout + qt_link.stderr
        ):
            print("Qt link dependencies must fail the engine boundary", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
