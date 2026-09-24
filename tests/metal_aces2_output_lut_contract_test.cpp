#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] std::string read_file(const char* path) {
  std::ifstream stream(path, std::ios::binary);
  return std::string(
      std::istreambuf_iterator<char>(stream),
      std::istreambuf_iterator<char>());
}

[[nodiscard]] bool contains_all(
    const std::string& source,
    std::initializer_list<std::string_view> required_tokens) {
  for (const auto token : required_tokens) {
    if (source.find(token) == std::string::npos) {
      std::cerr << "missing required Metal ACES 2 integration token: "
                << token << '\n';
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool contains_none(
    const std::string& source,
    std::initializer_list<std::string_view> forbidden_tokens) {
  for (const auto token : forbidden_tokens) {
    if (source.find(token) != std::string::npos) {
      std::cerr << "forbidden full-frame staging token in Metal output path: "
                << token << '\n';
      return false;
    }
  }
  return true;
}

}  // namespace

int main() {
  const auto backend = read_file(PHOTO_EDITOR_METAL_PIPELINE_BACKEND_PATH);
  const auto backend_header =
      read_file(PHOTO_EDITOR_METAL_PIPELINE_BACKEND_HEADER_PATH);
  const auto color_adjustments =
      read_file(PHOTO_EDITOR_METAL_COLOR_ADJUSTMENTS_PATH);
  const auto native_publication =
      read_file(PHOTO_EDITOR_NATIVE_OUTPUT_PUBLICATION_PATH);
  const auto canonical_native_suffix =
      read_file(PHOTO_EDITOR_CANONICAL_NATIVE_PREVIEW_SUFFIX_PATH);
  const auto canonical_native_execution =
      read_file(PHOTO_EDITOR_CANONICAL_NATIVE_PREVIEW_EXECUTION_PATH);
  const auto canonical_native_working_fusion =
      read_file(PHOTO_EDITOR_CANONICAL_NATIVE_WORKING_FUSION_PATH);
  const auto render_executor = read_file(PHOTO_EDITOR_RENDER_EXECUTOR_PATH);
  const auto retained_preview =
      read_file(PHOTO_EDITOR_RETAINED_PREVIEW_SURFACE_PATH);
  const auto export_path = read_file(PHOTO_EDITOR_PHOTO_RENDER_UTILS_PATH);
  const auto preview_path =
      read_file(PHOTO_EDITOR_PREVIEW_CONTROLLER_PATH);
  const auto preview_texture =
      read_file(PHOTO_EDITOR_PREVIEW_TEXTURE_METAL_PATH);
  const auto working_image = read_file(PHOTO_EDITOR_WORKING_IMAGE_PATH);
  if (backend.empty() || backend_header.empty() || color_adjustments.empty() ||
      native_publication.empty() ||
      canonical_native_suffix.empty() || retained_preview.empty() ||
      canonical_native_execution.empty() ||
      canonical_native_working_fusion.empty() || render_executor.empty() ||
      export_path.empty() || preview_path.empty() ||
      preview_texture.empty() || working_image.empty()) {
    std::cerr << "failed to read Metal ACES 2 integration sources\n";
    return EXIT_FAILURE;
  }
  if (!contains_all(
          backend,
          {
              "kernel void apply_aces2_output_lut",
              "tetrahedral_sample",
              "shaper_maximum",
              "aces2_output_lut_buffers",
              "least_recently_used",
              "apply_aces2_output_lut_target",
              "aces2_output_lut_target_pipeline",
              "kernel void publish_aces2_output_lut_srgb8",
              "publish_aces2_output_lut_srgb8_surface",
              "source_width",
              "source_origin_x",
              "source_origin_y",
              "MetalWorkingSurface::copy_pixels",
              "publish_masks_and_color_lut_surface",
              "apply_scene_exposure",
              "scene_exposure_pipeline",
              "scene_exposure_gain",
              "apply_global_tone_luminance",
              "tone_stage_pipeline",
              "tone_lut_buffers",
              "kMaximumToneLutBufferEntries",
              "kMaximumToneLutBufferBytes",
              "tone_lut_cache_stats",
              "extract_retained_hdr_local_tone",
              "apply_retained_hdr_local_tone",
              "hdr_local_tone_extract_pipeline",
              "hdr_local_tone_apply_pipeline",
              "apply_retained_fill_layers",
              "fill_layer_pipeline",
              "layer_alpha_buffer",
              "layer_intermediate_buffer",
              "static_assert(sizeof(FillLayerUniforms) == 5248U)",
              "perceptual_blend_gamut_mode",
              "retained_fill_blend_pixel",
              "retained_blend_search_chroma",
              "apply_layer_spatial_detail",
              "apply_layer_fine_detail",
              "apply_layer_geometry",
              "apply_layer_geometry_program",
              "apply_layer_user_lut",
              "apply_layer_user_luts",
              "layer_user_lut_buffers",
              "fill_layer_program.user_lut_programs",
              "guided_blur",
              "apply_layer_fine",
              "fill_mask_buffers",
              "kMaximumFillMaskBufferBytes",
              "layer_pixel_buffers",
              "kMaximumLayerPixelBufferBytes",
              "layer_tone_buffers",
              "kMaximumLayerToneBufferBytes",
              "layer_curve_buffers",
              "kMaximumLayerCurveBufferBytes",
              "fill_layer_program.curve_luts[index]",
              "newBufferWithBytes:&active_uniforms",
              "fused_fill_layer_uniform_buffer_allocation_failed",
              "layer_look_buffers",
              "kMaximumLayerLookBufferBytes",
              "fill_layer_program.downstream_look_programs[index]",
              "fill_layer_program.layers[index].transform_rotation",
              "fill_layer_program.layers[index].exposure_gain",
              "fill_layer_program.layers[index].white_balance",
              "fill_layer_program.look_programs[index]",
              "fill_layer_program.advanced_tone_luts[index]",
              "apply_compiled_curve_stage",
              "curve_stage_pipeline",
              "curve_lut_buffers",
              "kMaximumCurveLutBufferEntries",
              "kMaximumCurveLutBufferBytes",
              "curve_lut_cache_stats",
              "apply_compiled_look_stage",
              "look_stage_pipeline",
              "apply_user_lut_stage",
              "user_lut_stage_pipeline",
              "apply_geometry_stage",
              "geometry_stage_pipeline",
              "GeometryStageUniforms",
              "final_pixel_buffer = geometry_buffer",
              "RetainedSpatialDetailUniforms",
              "extract_retained_spatial_luminance",
              "apply_retained_spatial_detail",
              "retained_spatial_detail_pipeline",
              "RetainedFineDetailUniforms",
              "extract_retained_fine_detail",
              "apply_retained_sharpen",
              "apply_retained_luminance_nr",
              "apply_retained_color_nr",
              "compose_retained_fine_detail",
              "guided_square_pipeline",
              "guided_coefficients_pipeline",
              "guided_compose_pipeline",
              "working_to_input",
              "output_to_working",
              "input_transfer",
              "grading_saturation",
              "grading_luminance",
              "mixer_saturation",
              "selective_cyan",
              "harmony_anchors",
              "replacement_target_oklab_hue",
              "replacement_preserve_luminance",
              "skin_strength",
              "aces2_output_native_working_surface_invalid",
              "encodeWaitForEvent:working_surface->impl_->ready_event",
              "MetalOutputSurface::copy_bgra8_pixels_for_testing",
              "MetalOutputSurface::producer_completed_successfully",
              "newSharedEvent",
              "encodeSignalEvent",
              "defer_completion",
              "aces2_output_native_surface_cancelled",
              "const CompiledAces2OutputLutApplication& application",
              "clamp(source.a, 0.0f, 1.0f)",
              "aces2_output_source_non_finite",
              "aces2_output_application_non_finite",
              "aces2_output_gpu_validation_failed",
              "waitUntilCompleted",
              "photo_editor_apply_ocio_project",
              "apply_ocio_project_gpu_program",
              "newLibraryWithSource",
              "ocio_project_gpu_pipelines",
              "program_cache_key",
              "ocio_project_gpu_output_validation_failed",
              "apply_masks_and_color_lut",
              "fused_masks_color_lut_command_buffer_failed",
              "fused_masks_color_lut_cancelled",
              "color_lut_buffers",
              "kMaximumColorLutBufferBytes",
              "gpu_command_buffer_count += 1",
              "gpu_kernel_dispatch_count += kernel_dispatch_count",
              "kernel void apply_icc_transform_lut",
              "kernel void publish_icc_transform_lut_bgra8",
              "kernel void count_icc_exact_fallback_pixels",
              "kernel void compact_icc_exact_fallback_pixels",
              "kernel void patch_icc_exact_fallback_bgra8",
              "publish_aces2_output_lut_icc_bgra8_surface",
              "aces2_icc_native_exact_subset_exceeds_budget",
              "icc_transform_lut_bgra8_pipeline",
              "icc_exact_count_pipeline",
              "icc_exact_compact_pipeline",
              "icc_exact_patch_bgra8_pipeline",
              "unsafe_cells",
              "icc_transform_lut_buffers",
              "IccTransformLutApplyStats",
              "exact_transform.apply",
          }) ||
      !contains_all(
          backend_header,
          {
              "class MetalOutputSurface final : public RenderSurfaceBacking",
              "std::string_view backend_identity() const noexcept override",
              "bool copy_pixels(",
          }) ||
      !contains_all(
          color_adjustments,
          {
              "kernel void apply_color_lut",
              "kernel void apply_scene_exposure",
              "kernel void apply_global_tone_luminance",
              "kernel void extract_retained_hdr_local_tone",
              "kernel void apply_retained_hdr_local_tone",
              "kernel void apply_retained_fill_layers",
              "device const uchar* masks",
              "device const float4* layer_pixels",
              "float(layer.pixel_width) * layer.transform_scale",
              "source.rgb *= layer.exposure_gain",
              "device const float* tone_samples",
              "retained_layer_tone",
              "layer.has_white_balance",
              "retained_layer_look",
              "retained_layer_local_chroma",
              "layer.has_advanced_tone",
              "device const float* curve_samples",
              "retained_layer_curves",
              "layer.has_curves",
              "constant LookStageUniforms* layer_looks",
              "device float* layer_alpha",
              "device float4* layer_intermediate",
              "uniforms.layer_begin",
              "uniforms.layer_end",
              "uniforms.source_only",
              "uniforms.composite_only",
              "uniforms.perceptual_blend_gamut_mode",
              "retained_fill_blend_pixel",
              "retained_layer_downstream_look",
              "layer.has_downstream_look",
              "retained_layer_point_effects",
              "layer.lens_vignette",
              "layer.dehaze",
              "uniforms.rgb_to_xyz_d65",
              "kernel void retained_guided_square",
              "kernel void retained_guided_coefficients",
              "kernel void retained_guided_compose",
              "kernel void apply_compiled_curve_stage",
              "kernel void apply_compiled_look_stage",
              "kernel void apply_user_lut_stage",
              "kernel void apply_geometry_stage",
              "geometry_sample_bilinear",
              "content_aware_crop_fill",
              "kernel void extract_retained_spatial_luminance",
              "kernel void apply_retained_spatial_detail",
              "retained_spatial_scale_to_luminance",
              "kernel void extract_retained_fine_detail",
              "kernel void apply_retained_sharpen",
              "kernel void apply_retained_luminance_nr",
              "kernel void apply_retained_color_nr",
              "kernel void compose_retained_fine_detail",
              "kernel void apply_masks",
          }) ||
      !contains_all(
          export_path,
          {
              "Aces2OutputLutQuality::Final",
              "execute_color_plan",
              "executor_submission_count",
              "photographic_output_executed",
          }) ||
      !contains_all(
          preview_path,
          {
              "compile_aces2_output_lut_application",
              "Aces2OutputLutQuality::Interactive",
              "Aces2OutputLutQuality::Final",
              "prewarm_default_aces2_output_luts",
              "execute_color_plan",
              "qt_supports_exact_target_linear_profile",
              "state_supports_native_srgb_surface",
              "allow_native_srgb_surface",
              "require_cpu_preview_image",
              "publish_native_tiled_working_patch",
              "publish_full_working_image",
              "qimage_patch_from_working_image_region",
              "exact_patch.patch_surface != nullptr",
              "patch.patch_surface != nullptr",
              "rendered_has_image_or_patch",
              "native_surface_upload",
              "native_surface_adopted",
              "native_surface_upload_pending",
              "adopt_native_full_surface_async",
              "candidate_upload_rect != image_bounds",
              "gpu_blit_count",
              "canonical_geometry_surface",
              "photographic_output_publish_fusion",
              "metal-native-canonical-srgb8",
              "allow_native_device_surface",
              "select_canonical_native_device_lut",
              "photographic_output_device_publish_fusion",
              "metal-native-canonical-device-icc8",
              "working_adjustments_geometry_fusion",
              "execute_canonical_native_working_fusion",
              "preview_working_fusion_fallback_reason",
              "compile_tone_stage_lut",
              "compile_curve_stage_luts",
              "compile_look_stage_program",
              "retained_geometry_surface",
          }) ||
      !contains_all(
          canonical_native_working_fusion,
          {
              "canonical_compiled_tone_supports_source",
              "unsupported_stage:Tone:compiled_tone_domain",
              "CompiledToneStageLut",
              "CompiledCurveStageLuts",
              "CompiledLookStageProgram",
          }) ||
      !contains_all(
          native_publication,
          {
              "publish_native_aces_srgb8_surface",
              "try_publish_native_working_subrect",
              "metal-native-srgb8-subrect",
              "publish_working_patch",
              "native_surface_matches_patch",
              "upload_native_surface_patch_async",
              "gpu_to_cpu_bytes = metal_telemetry.gpu_gpu_to_cpu_bytes",
              "dynamic_pointer_cast<const",
              "MetalWorkingSurface",
          }) ||
      !contains_all(
          retained_preview,
          {
              "publish_masks_and_color_lut_surface",
              "retained_preview_surface_is_eligible",
              "!request->require_cpu_preview_image",
              "!request->scopes_visible",
              "publish_retained_preview_surface",
          }) ||
      !contains_all(
          canonical_native_suffix,
          {
              "canonical_native_suffix_is_eligible",
              "backend_available &&",
              "canonical_native_suffix_backend_available",
              "shared_metal_pipeline_backend().available()",
              "canonical_native_device_suffix_may_be_eligible",
              "canonical_native_device_suffix_is_eligible",
              "canonical_geometry_surface",
          }) ||
      !contains_all(
          canonical_native_execution,
          {
              "publish_canonical_native_suffix",
              "publish_canonical_native_device_suffix",
              "RenderStageKind::PhotographicOutput",
              "RenderStageKind::Publish",
          }) ||
      !contains_all(
          canonical_native_working_fusion,
          {
              "execute_canonical_native_working_fusion",
              "build_canonical_working_stage_lut",
              "working_stage_state.input_transform = {}",
              "effective_input_exposure",
              "publish_masks_and_color_lut_surface",
              "metal-working-lut-unfused-parity:v1",
              "apply_color_scene_adjustments_stage",
              "apply_color_tone_stage",
              "apply_color_curves_stage",
              "apply_color_look_stage",
              "apply_color_layers_stage",
              "apply_color_detail_stage",
              "apply_color_geometry_stage",
              "cpu-reference-working-fused-fallback",
          }) ||
      !contains_all(
          render_executor,
          {
              "working_adjustments_geometry_fusion",
              "kWorkingStageOrder",
              "fused_working_stage_index",
              "fused_geometry_descriptor",
          }) ||
      !contains_all(
          preview_texture,
          {
              "supportsNativeOutputSurface",
              "uploadNativePatchAsync",
              "adoptNativeSurface",
              "adoptNativeSurfaceAsync",
              "addCompletedHandler",
              "dispatch_get_main_queue",
              "encodeWaitForEvent",
              "publication_serial_",
              "surface.device_registry_id()",
              "source.device.registryID",
              "copyFromTexture",
          }) ||
      !contains_none(
          preview_texture,
          {
              "bool uploadNativePatch(",
              "waitUntilCompleted",
          }) ||
      !contains_none(
          native_publication,
          {
              "upload_native_surface_patch(",
          }) ||
      !contains_all(
          working_image,
          {
              "schedule_icc_transform_lut_prewarm",
              "find_cached_icc_transform_lut",
              "shared_metal_pipeline_backend",
              "metal.apply_aces2_output_lut_target",
              "output_lut_applied_on_metal",
              "allow_metal_output_lut",
              "exact-cpu-fallback",
              "apply_aces2_output_lut_target_application",
              "metal-hybrid",
              "metal-failed-exact-fallback",
              "transform.apply(transformed_pixels)",
              "ensureCpuPixels",
              "adoptRetainedSurface",
          }) ||
      !contains_none(
          backend,
          {
              "std::vector<IccRgbaFloat>(pixels.begin(), pixels.end())",
              "MetalPipelineBackend::apply_color_lut(",
              "MetalPipelineBackend::apply_masks(",
              "uniforms.target_to_working",
              "apply_pre_tone_effects",
              "apply_post_color_effects",
              "post_color_pipeline",
              "pre_tone_input_pipeline",
              "pre_tone_output_pipeline",
              "apply_spatial_tone_effects",
              "apply_detail_effects",
              "extract_oklab_pipeline",
              "spatial_tone_pipeline",
              "compose_detail_pipeline",
          }) ||
      !contains_none(
          backend_header,
          {
              "apply_pre_tone_effects",
              "apply_post_color_effects",
              "apply_spatial_tone_effects",
              "apply_detail_effects",
          }) ||
      !contains_none(
          color_adjustments,
          {
              "kernel void apply_post_color",
              "kernel void compute_pre_tone_inputs",
              "kernel void apply_pre_tone_output",
              "struct DetailUniforms",
              "struct SpatialToneUniforms",
              "kernel void extract_oklab",
              "kernel void extract_luminance",
              "kernel void apply_spatial_tone",
              "kernel void apply_sharpen",
              "kernel void apply_luma_nr",
              "kernel void apply_color_nr",
              "kernel void compose_detail_pixels",
          })) {
    return EXIT_FAILURE;
  }
  const auto icc_publish_begin =
      backend.find("MetalPipelineBackend::publish_aces2_output_lut_icc_bgra8_surface(");
  const auto icc_publish_end = backend.find(
      "bool MetalPipelineBackend::apply_icc_transform_lut(", icc_publish_begin);
  if (icc_publish_begin == std::string::npos ||
      icc_publish_end == std::string::npos ||
      !contains_none(
          backend.substr(icc_publish_begin, icc_publish_end - icc_publish_begin),
          {
              "blitCommandEncoder",
              "copyFromBuffer",
              "gpu_blit_count += 1",
          })) {
    std::cerr << "retained ACES-to-device ICC publication must not stage a "
                 "full-frame GPU copy\n";
    return EXIT_FAILURE;
  }
  const auto dispatch_start =
      backend.find("auto kernel_dispatch_count = std::int64_t{0}");
  const auto tone_dispatch = backend.find(
      "[tone_encoder setComputePipelineState:impl_->tone_stage_pipeline]",
      dispatch_start);
  const auto curve_dispatch = backend.find(
      "[curve_encoder setComputePipelineState:impl_->curve_stage_pipeline]",
      dispatch_start);
  const auto hdr_local_tone_dispatch = backend.find(
      "impl_->hdr_local_tone_apply_pipeline", tone_dispatch);
  const auto look_dispatch = backend.find(
      "[look_encoder setComputePipelineState:impl_->look_stage_pipeline]",
      dispatch_start);
  const auto user_lut_dispatch = backend.find(
      "setComputePipelineState:impl_->user_lut_stage_pipeline",
      dispatch_start);
  const auto spatial_detail_dispatch = backend.find(
      "impl_->retained_spatial_detail_pipeline",
      user_lut_dispatch);
  const auto fill_layer_dispatch = backend.find(
      "impl_->fill_layer_pipeline,",
      user_lut_dispatch);
  const auto fine_detail_dispatch = backend.find(
      "impl_->retained_detail_compose_pipeline",
      spatial_detail_dispatch);
  const auto geometry_dispatch = backend.find(
      "[geometry_encoder\n          setComputePipelineState:impl_->geometry_stage_pipeline]",
      fine_detail_dispatch);
  if (dispatch_start == std::string::npos ||
      tone_dispatch == std::string::npos ||
      hdr_local_tone_dispatch == std::string::npos ||
      curve_dispatch == std::string::npos ||
      look_dispatch == std::string::npos ||
      user_lut_dispatch == std::string::npos ||
      fill_layer_dispatch == std::string::npos ||
      spatial_detail_dispatch == std::string::npos ||
      fine_detail_dispatch == std::string::npos ||
      geometry_dispatch == std::string::npos ||
      !(tone_dispatch < hdr_local_tone_dispatch &&
        hdr_local_tone_dispatch < curve_dispatch &&
        curve_dispatch < look_dispatch &&
        look_dispatch < user_lut_dispatch &&
        user_lut_dispatch < fill_layer_dispatch &&
        fill_layer_dispatch < spatial_detail_dispatch &&
        spatial_detail_dispatch < fine_detail_dispatch &&
        fine_detail_dispatch < geometry_dispatch)) {
    std::cerr <<
        "retained kernels must dispatch Tone, HDR local tone, Curves, Look, LUTs, Layers, Detail, then Geometry\n";
    return EXIT_FAILURE;
  }
  const auto look_start = color_adjustments.find(
      "static float3 look_apply_saturation_vibrance",
      color_adjustments.find("kernel void apply_compiled_curve_stage"));
  const auto replacement_operation = color_adjustments.find(
      "if ((uniforms.active_mask & 128u) != 0u)", look_start);
  const auto harmony_operation = color_adjustments.find(
      "if ((uniforms.active_mask & 64u) != 0u)", look_start);
  const auto grading_operation = color_adjustments.find(
      "if ((uniforms.active_mask & 8u) != 0u)", look_start);
  if (look_start == std::string::npos ||
      replacement_operation == std::string::npos ||
      harmony_operation == std::string::npos ||
      grading_operation == std::string::npos ||
      !(replacement_operation < harmony_operation &&
        harmony_operation < grading_operation)) {
    std::cerr << "compiled Metal Look must apply replacement, harmony, then grading\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
