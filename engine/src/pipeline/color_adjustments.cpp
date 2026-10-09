#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/output_rendering.h"
#include "photo_editor/pipeline/color_transform.h"
#include "color_adjustment_kernel_runtime.h"
#include "color_adjustment_scheduler.h"
#include "color_blur_runtime.h"
#include "color_curve_evaluator.h"
#include "color_detail_runtime.h"
#include "color_highlight_runtime.h"
#include "color_input_runtime.h"
#include "color_input_stage_identity.h"
#include "color_layer_runtime.h"
#include "color_look_runtime.h"
#include "color_local_layer_runtime.h"
#include "color_lut_runtime.h"
#include "color_mask_runtime.h"
#include "color_math_runtime.h"
#include "color_mixer_runtime.h"
#include "color_output_runtime.h"
#include "color_spatial_runtime.h"
#include "color_tone_runtime.h"
#include "color_working_space_runtime.h"
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#endif

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <deque>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

namespace photo_editor::pipeline {

namespace {

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
metal::MetalPipelineBackend& metal_backend() {
  return metal::shared_metal_pipeline_backend();
}
#endif

template <typename Work>
void parallel_for(int count, Work&& work) {
  run_color_adjustment_parallel_for(
      count, std::function<void(int)>(std::forward<Work>(work)));
}

[[nodiscard]] bool finite_color_stage_surface(
    const std::vector<ColorPixel>& pixels,
    bool input_finite_validated = false) {
  return input_finite_validated ||
         color_surface_is_finite(std::span<const ColorPixel>{pixels});
}

using StageClock = std::chrono::steady_clock;
[[nodiscard]] std::int64_t elapsed_stage_ms(StageClock::time_point start) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(StageClock::now() - start).count();
}

[[nodiscard]] std::vector<CompiledUserLut> user_luts_from_state(
    const ColorAdjustmentState& state,
    const PipelineColorContext& context) {
  return compile_user_lut_runtimes(
      state,
      context.working_space,
      context.chromatic_adaptation_method);
}

[[nodiscard, maybe_unused]] std::string metal_color_pipeline_incompatibility_reason_impl(
    const ColorAdjustmentState& state) {
  if (state.color_pipeline.camera_dcp_profile_asset.ready() &&
      state.color_pipeline.camera_dcp_profile_asset.profile
          ->profile_gain_table_map.configured()) {
    return "dcp_profile_gain_table_map_requires_cpu";
  }
  if (state.color_pipeline.camera_dcp_profile_asset.ready() &&
      state.color_pipeline.camera_dcp_profile_asset.profile
          ->rgb_tables.configured()) {
    return "dcp_rgb_tables_requires_cpu";
  }
  if (state.input_transform.raw_input_transform_enabled &&
      state.input_transform.highlight_recovery_enabled &&
      state.input_transform.highlight_reconstruction_mode != "Disabled" &&
      state.input_transform.highlight_reconstruction_model ==
          "camera-linear-opposed-v2") {
    return "camera_linear_highlight_reconstruction_requires_cpu";
  }
  if (state.input_transform.raw_input_transform_enabled &&
      state.color_pipeline.camera_profile.valid) {
    return "camera_linear_lut_fidelity_requires_cpu";
  }
  const auto has_local_white_balance = std::any_of(
      state.local_masks.cbegin(),
      state.local_masks.cend(),
      [](const LocalMask& mask) {
        return std::abs(mask.temperature) > 1.0e-9 || std::abs(mask.tint) > 1.0e-9;
      }) || std::any_of(
      state.local_mask_sets.cbegin(),
      state.local_mask_sets.cend(),
      [](const LocalMaskSet& mask_set) {
        return std::abs(mask_set.temperature) > 1.0e-9 || std::abs(mask_set.tint) > 1.0e-9;
      });

  // The accelerated pre-tone kernel currently has only diagonal RGB white-
  // balance multipliers. Use the CPU path whenever a Bradford/CAT16 matrix is
  // required so preview and export cannot disagree on adapted colors.
  if (std::abs(effective_input_temperature(state)) > 1.0e-9 ||
      std::abs(effective_input_tint(state)) > 1.0e-9) {
    return "input_chromatic_adaptation_requires_cpu";
  }
  if (std::abs(state.linear_mask_temperature) > 1.0e-9 ||
      std::abs(state.radial_mask_temperature) > 1.0e-9 || has_local_white_balance) {
    return "local_chromatic_adaptation_requires_cpu";
  }
  if (state.color_pipeline.working_profile.kind != WorkingTransformKind::LinearSrgb) {
    return "working_profile_not_linear_srgb";
  }
  if (state.color_pipeline.render_intent != RenderIntent::Perceptual) {
    return "render_intent_not_perceptual";
  }
  if (!state.color_pipeline.output_gamut_mapping_enabled ||
      state.color_pipeline.output_gamut_mapping_policy != GamutMappingPolicy::Perceptual) {
    return "perceptual_output_mapping_disabled";
  }
  if (std::abs(state.color_pipeline.output_headroom - 1.0) > 1.0e-9) {
    return "output_headroom_not_sdr";
  }
  if (black_point_compensation_affects_output(state.color_pipeline)) {
    return "black_point_compensation_requires_cpu";
  }
  return {};
}

[[nodiscard, maybe_unused]] bool metal_color_pipeline_supported(
    const ColorAdjustmentState& state) {
  return metal_color_pipeline_incompatibility_reason_impl(state).empty();
}
}  // namespace

bool metal_color_backend_available() {
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  return metal_backend().available();
#else
  return false;
#endif
}

std::string metal_color_backend_unavailable_reason() {
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  return metal_backend().unavailable_reason();
#else
  return "not_compiled";
#endif
}

bool metal_color_pipeline_compatible(const ColorAdjustmentState& state) {
  return metal_color_pipeline_incompatibility_reason_impl(state).empty();
}

std::string metal_color_pipeline_incompatibility_reason(
    const ColorAdjustmentState& state) {
  return metal_color_pipeline_incompatibility_reason_impl(state);
}

bool apply_color_geometry_stage(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    CancellationToken cancellation,
    ColorAdjustmentTelemetry* telemetry,
    bool input_finite_validated) {
  if (pixels == nullptr || width <= 0 || height <= 0 ||
      pixels->size() !=
          static_cast<std::size_t>(width) *
              static_cast<std::size_t>(height) ||
      cancellation.cancelled()) {
    return false;
  }
  if (geometry_corrections_are_identity(state)) {
    return finite_color_stage_surface(*pixels, input_finite_validated);
  }
  const auto geometry_timer = StageClock::now();
  apply_geometry_corrections(pixels, width, height, state);
  if (cancellation.cancelled()) {
    return false;
  }
  if (telemetry != nullptr) {
    telemetry->geometry_ms +=
        elapsed_stage_ms(geometry_timer);
  }
  return finite_color_stage_surface(*pixels);
}

bool apply_color_photographic_output_stage(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorPipelineConfig& config,
    GamutWarningTarget target,
    Aces2OutputLutQuality quality,
    CancellationToken cancellation,
    ColorAdjustmentTelemetry* telemetry,
    std::span<const PhotoLayer> display_referred_text_layers,
    bool display_referred_text_overlay_enabled) {
  if (pixels == nullptr || width <= 0 || height <= 0 ||
      pixels->size() !=
          static_cast<std::size_t>(width) *
              static_cast<std::size_t>(height) ||
      cancellation.cancelled()) {
    return false;
  }
  const auto output_timer = StageClock::now();
  const auto application =
      compile_aces2_output_lut_application(
          config,
          target,
          quality);
  if (application.ready()) {
    apply_aces2_output_lut_target_application(
        *pixels,
        application);
  } else {
    apply_output_transform(
        *pixels,
        config,
        target);
  }
  if (!display_referred_text_layers.empty() &&
      display_referred_text_overlay_enabled) {
    const auto& display_target =
        target == GamutWarningTarget::PreviewDisplay
            ? config.preview_display_target
            : config.export_display_target;
    if (!application.ready() || display_target.eotf != DisplayEotf::Srgb ||
        display_target.mastering_mode != DisplayMasteringMode::Sdr) {
      if (telemetry != nullptr) {
        telemetry->failure_reason =
            "Display-referred text overlay requires the verified SDR ACES 2 output path.";
      }
      return false;
    }

    // PhotographicOutput produces target-linear pixels here. Convert only the
    // glyph rasters from the working profile, then use the same tested layer
    // compositor for placement, opacity, alpha and source-over ordering.
    const auto working_to_display = invert_matrix3(application.target_to_working);
    std::vector<PhotoLayer> display_text_layers(
        display_referred_text_layers.begin(),
        display_referred_text_layers.end());
    for (auto& layer : display_text_layers) {
      if (layer.kind != "text" || !layer.parent_id.empty() ||
          layer.blend_mode != "normal" || layer.clipping_mask ||
          !layer.effects.empty() || !layer.effect_masks.empty() ||
          ((layer.mask_width > 0 || layer.mask_height > 0 ||
            !layer.mask_data.empty()) &&
           (layer.mask_width <= 0 || layer.mask_height <= 0 ||
            layer.mask_data.size() !=
                static_cast<std::size_t>(layer.mask_width) *
                    static_cast<std::size_t>(layer.mask_height) ||
            static_cast<std::int64_t>(layer.mask_width) * height !=
                static_cast<std::int64_t>(layer.mask_height) * width)) ||
          !layer.text_material.empty() ||
          layer.pixel_width <= 0 || layer.pixel_height <= 0 ||
          layer.pixels.size() != static_cast<std::size_t>(layer.pixel_width) *
                                     static_cast<std::size_t>(layer.pixel_height)) {
        if (telemetry != nullptr) {
          telemetry->failure_reason =
              "Display-referred text layer exceeded the compiled plain-text policy.";
        }
        return false;
      }
      for (auto& pixel : layer.pixels) {
        pixel = apply_rgb_matrix(pixel, working_to_display);
      }
    }

    ColorAdjustmentState display_state;
    display_state.color_pipeline = config;
    display_state.color_pipeline.working_profile.color_space =
        display_target.color_space;
    display_state.color_pipeline.output_headroom =
        display_target_linear_headroom(display_target);
    display_state.input_transform.chromatic_adaptation_method = "Bradford";
    const auto display_context = make_pipeline_color_context(display_state);
    std::string layer_error;
    if (!apply_standard_photo_layers(
            pixels, width, height, display_context,
            config.photographic_rendering_model, display_text_layers,
            cancellation, &layer_error)) {
      if (telemetry != nullptr) {
        telemetry->failure_reason = layer_error.empty()
            ? "Display-referred text composition failed."
            : layer_error;
      }
      return false;
    }
    if (cancellation.cancelled()) return false;
    if (telemetry != nullptr) {
      telemetry->display_referred_text_color_domain = "display-linear";
      telemetry->display_referred_text_eligibility =
          "eligible-sdr-top-text-suffix";
      telemetry->display_referred_text_eligibility_reason = "applied";
      telemetry->display_referred_text_layer_count =
          static_cast<std::int64_t>(display_referred_text_layers.size());
      telemetry->display_referred_text_applied_layer_count =
          static_cast<std::int64_t>(display_referred_text_layers.size());
    }
  }
  if (cancellation.cancelled() || !finite_color_stage_surface(*pixels)) {
    return false;
  }
  if (telemetry != nullptr) {
    telemetry->photographic_output_ms +=
        elapsed_stage_ms(output_timer);
  }
  return true;
}

bool apply_color_publish_stage(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const DisplayTarget& display_target,
    CancellationToken cancellation,
    ColorAdjustmentTelemetry* telemetry) {
  if (pixels == nullptr || width <= 0 || height <= 0 ||
      pixels->size() !=
          static_cast<std::size_t>(width) *
              static_cast<std::size_t>(height) ||
      cancellation.cancelled()) {
    return false;
  }
  const auto publish_timer = StageClock::now();
  parallel_for(height, [&](int y) {
    if (cancellation.cancelled()) {
      return;
    }
    const auto row_offset =
        static_cast<std::size_t>(y) *
        static_cast<std::size_t>(width);
    for (int x = 0; x < width; ++x) {
      auto& pixel =
          (*pixels)[
              row_offset +
              static_cast<std::size_t>(x)];
      pixel.red = static_cast<float>(
          encode_display_eotf(
              static_cast<double>(pixel.red),
              display_target));
      pixel.green = static_cast<float>(
          encode_display_eotf(
              static_cast<double>(pixel.green),
              display_target));
      pixel.blue = static_cast<float>(
          encode_display_eotf(
              static_cast<double>(pixel.blue),
              display_target));
      pixel.alpha = static_cast<float>(
          clamp_unit(
              static_cast<double>(pixel.alpha)));
    }
  });
  const auto valid =
      !cancellation.cancelled() &&
      color_surface_is_finite(std::span<const ColorPixel>{*pixels});
  if (valid && telemetry != nullptr) {
    telemetry->publish_ms +=
        elapsed_stage_ms(publish_timer);
  }
  return valid;
}

bool apply_color_compatibility_output_stage(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    CancellationToken cancellation,
    ColorAdjustmentTelemetry* telemetry) {
  if (pixels == nullptr || width <= 0 || height <= 0 ||
      pixels->size() !=
          static_cast<std::size_t>(width) *
              static_cast<std::size_t>(height) ||
      cancellation.cancelled()) {
    return false;
  }
  const auto safe_width = static_cast<std::size_t>(width);
  const auto color_context = make_pipeline_color_context(state);
  const auto final_output_headroom =
      sanitized_output_headroom(state);
  const auto final_output_timer = StageClock::now();
  const auto global_look_requires_gamut_fit =
      has_rgb_channel_mixer_adjustments(state) ||
      state.black_and_white_enabled ||
      has_post_tone_color_adjustments(
          state,
          clamp_signed_percent(state.saturation) / 100.0,
          clamp_signed_percent(state.vibrance) / 100.0) ||
      !user_luts_from_state(state, color_context).empty();
  const auto layer_look_requires_gamut_fit =
      std::ranges::any_of(
          state.photo_layers,
          [&](const PhotoLayer& layer) {
            return photo_layer_requires_gamut_fit(layer, color_context);
          });
  const auto local_adjustment_requires_gamut_fit =
      [](const auto& local_adjustment) {
        return local_adjustment.enabled &&
               (std::abs(local_adjustment.vibrance) > 1.0e-9 ||
                std::abs(local_adjustment.saturation) > 1.0e-9 ||
                std::abs(local_adjustment.hue_shift) > 1.0e-9 ||
                std::abs(local_adjustment.saturation_shift) > 1.0e-9 ||
                std::abs(local_adjustment.colorize_strength) > 1.0e-9);
      };
  const auto local_look_requires_gamut_fit =
      std::ranges::any_of(
          state.local_masks,
          local_adjustment_requires_gamut_fit) ||
      std::ranges::any_of(
          state.local_mask_sets,
          local_adjustment_requires_gamut_fit);
  const auto compatibility_renderer_owns_gamut_fit =
      !photographic_rendering_model_uses_aces2(
          state.color_pipeline.photographic_rendering_model) ||
      state.color_pipeline.render_intent != RenderIntent::Perceptual;
  if (compatibility_renderer_owns_gamut_fit &&
      (global_look_requires_gamut_fit ||
       layer_look_requires_gamut_fit ||
       local_look_requires_gamut_fit)) {
    if (telemetry != nullptr) {
      telemetry->final_output_gamut_fit_pass_count += 1;
    }
    parallel_for(height, [&](int y) {
      if (cancellation.cancelled()) {
        return;
      }
      const auto row_offset =
          static_cast<std::size_t>(y) * safe_width;
      for (int x = 0; x < width; ++x) {
        const auto index =
            row_offset + static_cast<std::size_t>(x);
        const auto oklab =
            linear_rgb_to_oklab((*pixels)[index], color_context);
        (*pixels)[index] = gamut_fit_oklab(
            oklab.lightness,
            oklab.a,
            oklab.b,
            (*pixels)[index].alpha,
            color_context);
      }
    });
    if (cancellation.cancelled()) {
      return false;
    }
  }
  if (black_point_compensation_affects_output(
          state.color_pipeline)) {
    const auto black_point_timer = StageClock::now();
    parallel_for(height, [&](int y) {
      if (cancellation.cancelled()) {
        return;
      }
      const auto row_offset =
          static_cast<std::size_t>(y) * safe_width;
      for (int x = 0; x < width; ++x) {
        const auto index =
            row_offset + static_cast<std::size_t>(x);
        (*pixels)[index] = apply_black_point_compensation(
            (*pixels)[index],
            state.color_pipeline);
      }
    });
    if (cancellation.cancelled()) {
      return false;
    }
    if (telemetry != nullptr) {
      telemetry->final_output_black_point_ms +=
          elapsed_stage_ms(black_point_timer);
    }
  }

  const auto headroom_timer = StageClock::now();
  parallel_for(height, [&](int y) {
    if (cancellation.cancelled()) {
      return;
    }
    const auto row_offset =
        static_cast<std::size_t>(y) * safe_width;
    for (int x = 0; x < width; ++x) {
      const auto index =
          row_offset + static_cast<std::size_t>(x);
      (*pixels)[index] =
          fit_pixel_to_working_headroom_preserving_signed(
              (*pixels)[index],
              final_output_headroom);
    }
  });
  if (cancellation.cancelled()) {
    return false;
  }
  if (telemetry != nullptr) {
    telemetry->final_output_headroom_ms +=
        elapsed_stage_ms(headroom_timer);
    telemetry->final_output_ms +=
        elapsed_stage_ms(final_output_timer);
  }
  return true;
}

namespace {

[[nodiscard]] bool valid_color_stage_surface(
    const std::vector<ColorPixel>* pixels,
    int width,
    int height) {
  if (pixels == nullptr || width <= 0 || height <= 0) {
    return false;
  }
  const auto safe_width = static_cast<std::size_t>(width);
  const auto safe_height = static_cast<std::size_t>(height);
  return safe_height <=
             std::numeric_limits<std::size_t>::max() / safe_width &&
         pixels->size() == safe_width * safe_height;
}

}  // namespace

ColorPrefixStageContext prepare_color_prefix_stage_context(
    std::span<const ColorPixel> decoded_pixels,
    int width,
    int height,
    const ColorAdjustmentState& state) {
  auto context = ColorPrefixStageContext{};
  context.highlight_reconstruction =
      analyze_highlight_reconstruction(
          decoded_pixels,
          width,
          height,
          state.input_transform);
  context.prepared = true;
  return context;
}

bool apply_color_normalize_raw_stage(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    ColorPrefixStageContext* context,
    CancellationToken cancellation,
    ColorAdjustmentTelemetry* telemetry,
    bool input_finite_validated) {
  if (!valid_color_stage_surface(pixels, width, height) ||
      context == nullptr || cancellation.cancelled()) {
    return false;
  }
  const auto timer = StageClock::now();
  if (!context->prepared) {
    context->highlight_reconstruction =
        analyze_highlight_reconstruction(*pixels, width, height, state.input_transform);
    context->prepared = true;
  }
  if (telemetry != nullptr) {
    telemetry->highlight_reconstruction =
        context->highlight_reconstruction;
  }
  const auto color_context = make_pipeline_color_context(state);
  const auto input_stage = make_input_stage_parameters(
      state,
      color_context,
      &context->highlight_reconstruction);
  if (normalize_raw_stage_is_identity(input_stage)) {
    return finite_color_stage_surface(*pixels, input_finite_validated);
  }
  const auto safe_width = static_cast<std::size_t>(width);
  parallel_for(height, [&](int y) {
    if (cancellation.cancelled()) {
      return;
    }
    const auto row_offset = static_cast<std::size_t>(y) * safe_width;
    for (int x = 0; x < width; ++x) {
      const auto index = row_offset + static_cast<std::size_t>(x);
      (*pixels)[index] = apply_normalize_raw_stage(
          (*pixels)[index],
          input_stage,
          color_context);
    }
  });
  if (telemetry != nullptr) {
    const auto elapsed = elapsed_stage_ms(timer);
    telemetry->normalize_raw_ms += elapsed;
  }
  return !cancellation.cancelled() &&
         finite_color_stage_surface(*pixels);
}

bool apply_color_camera_characterization_stage(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    const ColorPrefixStageContext& context,
    CancellationToken cancellation,
    ColorAdjustmentTelemetry* telemetry,
    bool input_finite_validated) {
  if (!valid_color_stage_surface(pixels, width, height) ||
      !context.prepared || cancellation.cancelled()) {
    return false;
  }
  const auto timer = StageClock::now();
  const auto vignette_timer = StageClock::now();
  apply_lens_vignette_correction(
      pixels,
      width,
      height,
      state);
  if (cancellation.cancelled()) {
    return false;
  }
  if (telemetry != nullptr) {
    telemetry->input_vignette_ms +=
        elapsed_stage_ms(vignette_timer);
  }
  const auto color_context = make_pipeline_color_context(state);
  const auto input_stage = make_input_stage_parameters(
      state,
      color_context,
      &context.highlight_reconstruction);
  if (std::abs(state.lens_vignette) <= 1.0e-6 &&
      camera_characterization_stage_is_identity(input_stage)) {
    return finite_color_stage_surface(*pixels, input_finite_validated);
  }
  const auto safe_width = static_cast<std::size_t>(width);
  parallel_for(height, [&](int y) {
    if (cancellation.cancelled()) {
      return;
    }
    const auto row_offset = static_cast<std::size_t>(y) * safe_width;
    for (int x = 0; x < width; ++x) {
      const auto index = row_offset + static_cast<std::size_t>(x);
      (*pixels)[index] =
          apply_camera_characterization_stage(
              (*pixels)[index],
              input_stage,
              color_context,
              (static_cast<double>(x) + 0.5) /
                  static_cast<double>(width),
              (static_cast<double>(y) + 0.5) /
                  static_cast<double>(height));
    }
  });
  if (telemetry != nullptr) {
    telemetry->camera_characterization_ms +=
        elapsed_stage_ms(timer);
  }
  return !cancellation.cancelled() &&
         finite_color_stage_surface(*pixels);
}

bool apply_color_scene_adjustments_stage(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    CancellationToken cancellation,
    ColorAdjustmentTelemetry* telemetry,
    bool input_finite_validated) {
  if (!valid_color_stage_surface(pixels, width, height) ||
      cancellation.cancelled()) {
    return false;
  }
  const auto timer = StageClock::now();
  const auto color_context = make_pipeline_color_context(state);
  const auto input_stage =
      make_input_stage_parameters(state, color_context);
  if (scene_adjustments_stage_is_identity(input_stage)) {
    return finite_color_stage_surface(*pixels, input_finite_validated);
  }
  const auto safe_width = static_cast<std::size_t>(width);
  parallel_for(height, [&](int y) {
    if (cancellation.cancelled()) {
      return;
    }
    const auto row_offset = static_cast<std::size_t>(y) * safe_width;
    for (int x = 0; x < width; ++x) {
      const auto index = row_offset + static_cast<std::size_t>(x);
      (*pixels)[index] = apply_scene_adjustments_stage(
          (*pixels)[index],
          input_stage);
    }
  });
  if (telemetry != nullptr) {
    telemetry->scene_adjustments_ms +=
        elapsed_stage_ms(timer);
  }
  return !cancellation.cancelled() &&
         finite_color_stage_surface(*pixels);
}

bool apply_color_tone_stage(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    CancellationToken cancellation,
    ColorAdjustmentTelemetry* telemetry,
    bool input_finite_validated) {
  if (!valid_color_stage_surface(pixels, width, height) ||
      cancellation.cancelled()) {
    return false;
  }
  if (color_tone_stage_is_identity(state)) {
    return finite_color_stage_surface(*pixels, input_finite_validated);
  }
  const auto timer = StageClock::now();
  const auto safe_width = static_cast<std::size_t>(width);
  const auto safe_height = static_cast<std::size_t>(height);
  const auto color_context = make_pipeline_color_context(state);
  const auto output_headroom = sanitized_output_headroom(state);
  const auto hdr_local_tone_strength =
      clamp_unit(state.hdr_local_tone_mapping / 100.0);
  const auto has_tone_adjustments =
      std::abs(state.contrast) > 1.0e-9 ||
      std::abs(state.highlights) > 1.0e-9 ||
      std::abs(state.shadows) > 1.0e-9 ||
      std::abs(state.whites) > 1.0e-9 ||
      std::abs(state.blacks) > 1.0e-9 ||
      std::abs(state.brightness) > 1.0e-9 ||
      std::abs(state.filmic_shoulder) > 1.0e-9 ||
      std::abs(state.filmic_toe) > 1.0e-9 ||
      std::abs(state.adaptive_contrast) > 1.0e-9 ||
      std::abs(state.levels_black_point) > 1.0e-9 ||
      std::abs(state.levels_gamma) > 1.0e-9 ||
      std::abs(state.levels_white_point) > 1.0e-9 ||
      std::abs(state.zone_deep_shadows) > 1.0e-9 ||
      std::abs(state.zone_shadows) > 1.0e-9 ||
      std::abs(state.zone_midtones) > 1.0e-9 ||
      std::abs(state.zone_lights) > 1.0e-9 ||
      std::abs(state.zone_highlights) > 1.0e-9;
  const auto tone_table =
      has_tone_adjustments
          ? acquire_tone_luminance_table(state, telemetry)
          : std::shared_ptr<const ToneLuminanceTable>{};
  const auto modern_aces =
      photographic_rendering_model_uses_aces2(
          state.color_pipeline
              .photographic_rendering_model) &&
      state.color_pipeline.render_intent ==
          RenderIntent::Perceptual;
  const auto skip_automatic_shoulder =
      modern_aces &&
      std::abs(state.filmic_shoulder) <= 1.0e-9;
  if (hdr_local_tone_strength <= 1.0e-6 &&
      skip_automatic_shoulder) {
    parallel_for(height, [&](int y) {
      if (cancellation.cancelled()) {
        return;
      }
      const auto row_offset = static_cast<std::size_t>(y) * safe_width;
      for (int x = 0; x < width; ++x) {
        const auto index = row_offset + static_cast<std::size_t>(x);
        const auto source_luminance =
            luminance_for_pixel((*pixels)[index], color_context);
        const auto target =
            tone_table != nullptr
                ? evaluate_tone_luminance_table(
                      *tone_table,
                      source_luminance,
                      state)
                : apply_global_tone(source_luminance, state);
        (*pixels)[index] = scale_pixel_for_global_tone(
            (*pixels)[index],
            target,
            output_headroom,
            color_context);
      }
    });
    if (telemetry != nullptr) {
      const auto elapsed = elapsed_stage_ms(timer);
      telemetry->tone_ms += elapsed;
      telemetry->tone_mask_ms += elapsed;
    }
    return !cancellation.cancelled() &&
           finite_color_stage_surface(*pixels);
  }

  std::vector<double> target_luminances(
      safe_width * safe_height,
      0.0);
  std::vector<double> row_max_luminances(
      safe_height,
      0.0);
  parallel_for(height, [&](int y) {
    if (cancellation.cancelled()) {
      return;
    }
    auto row_max = 0.0;
    const auto row_offset = static_cast<std::size_t>(y) * safe_width;
    for (int x = 0; x < width; ++x) {
      const auto index = row_offset + static_cast<std::size_t>(x);
      const auto source_luminance =
          luminance_for_pixel(
              (*pixels)[index],
              color_context);
      const auto target =
          tone_table != nullptr
              ? evaluate_tone_luminance_table(
                    *tone_table,
                    source_luminance,
                    state)
              : apply_global_tone(
                    source_luminance,
                    state);
      target_luminances[index] = target;
      row_max = std::max(row_max, target);
    }
    row_max_luminances[static_cast<std::size_t>(y)] = row_max;
  });
  if (cancellation.cancelled()) {
    return false;
  }

  if (hdr_local_tone_strength > 1.0e-6 &&
      !target_luminances.empty()) {
    std::vector<float> log_luminances(
        target_luminances.size(),
        0.0F);
    parallel_for(height, [&](int y) {
      if (cancellation.cancelled()) {
        return;
      }
      const auto row_offset =
          static_cast<std::size_t>(y) * safe_width;
      for (int x = 0; x < width; ++x) {
        const auto index =
            row_offset + static_cast<std::size_t>(x);
        log_luminances[index] = static_cast<float>(
            std::log2(
                std::max(
                    1.0e-6,
                    target_luminances[index])));
      }
    });
    const auto radius =
        std::clamp(std::max(width, height) / 24, 1, 48);
    const auto local_log_luminance =
        gaussian_approximate_blur(
            log_luminances,
            width,
            height,
            radius,
            cancellation);
    if (cancellation.cancelled()) {
      return false;
    }
    constexpr auto compression_start = -0.4739311883324123;
    std::fill(
        row_max_luminances.begin(),
        row_max_luminances.end(),
        0.0);
    parallel_for(height, [&](int y) {
      if (cancellation.cancelled()) {
        return;
      }
      auto row_max = 0.0;
      const auto row_offset =
          static_cast<std::size_t>(y) * safe_width;
      for (int x = 0; x < width; ++x) {
        const auto index =
            row_offset + static_cast<std::size_t>(x);
        const auto target_log =
            static_cast<double>(log_luminances[index]);
        const auto local_log =
            static_cast<double>(
                local_log_luminance[index]);
        const auto local_excess =
            std::max(0.0, local_log - compression_start);
        const auto highlight_weight = smoothstep(
            compression_start,
            compression_start + 2.2,
            local_log);
        const auto detail_log = target_log - local_log;
        const auto mapped_log =
            target_log -
            local_excess * hdr_local_tone_strength * 0.58 +
            detail_log * hdr_local_tone_strength *
                highlight_weight * 0.45;
        target_luminances[index] =
            std::pow(2.0, mapped_log);
        row_max = std::max(
            row_max,
            target_luminances[index]);
      }
      row_max_luminances[static_cast<std::size_t>(y)] =
          row_max;
    });
    if (cancellation.cancelled()) {
      return false;
    }
  }

  const auto maximum_target =
      robust_highlight_reference_from_row_maxima(
          row_max_luminances);
  const auto shoulder_threshold =
      skip_automatic_shoulder
          ? output_headroom
          : shoulder_threshold_for_max_luminance(
                maximum_target,
                output_headroom,
                state.filmic_shoulder);
  parallel_for(height, [&](int y) {
    if (cancellation.cancelled()) {
      return;
    }
    const auto row_offset =
        static_cast<std::size_t>(y) * safe_width;
    for (int x = 0; x < width; ++x) {
      const auto index =
          row_offset + static_cast<std::size_t>(x);
      auto target = target_luminances[index];
      if (shoulder_threshold < output_headroom) {
        target = apply_highlight_shoulder(
            target,
            shoulder_threshold,
            output_headroom,
            state.filmic_shoulder);
      }
      (*pixels)[index] = scale_pixel_for_global_tone(
          (*pixels)[index],
          target,
          output_headroom,
          color_context);
    }
  });
  if (telemetry != nullptr) {
    const auto elapsed = elapsed_stage_ms(timer);
    telemetry->tone_ms += elapsed;
    telemetry->tone_mask_ms += elapsed;
  }
  return !cancellation.cancelled() &&
         finite_color_stage_surface(*pixels);
}

bool apply_color_look_stage(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    CancellationToken cancellation,
    ColorAdjustmentTelemetry* telemetry,
    bool input_finite_validated) {
  if (!valid_color_stage_surface(pixels, width, height) ||
      cancellation.cancelled()) {
    return false;
  }
  const auto timer = StageClock::now();
  const auto safe_width = static_cast<std::size_t>(width);
  const auto color_context = make_pipeline_color_context(state);
  const auto output_headroom = sanitized_output_headroom(state);
  const auto saturation_strength =
      clamp_signed_percent(state.saturation) / 100.0;
  const auto vibrance_strength =
      clamp_signed_percent(state.vibrance) / 100.0;
  const auto user_luts = user_luts_from_state(state, color_context);
  if (color_look_stage_is_identity(state, saturation_strength, vibrance_strength) &&
      user_luts.empty()) {
    return finite_color_stage_surface(*pixels, input_finite_validated);
  }
  parallel_for(height, [&](int y) {
    if (cancellation.cancelled()) {
      return;
    }
    const auto row_offset =
        static_cast<std::size_t>(y) * safe_width;
    for (int x = 0; x < width; ++x) {
      const auto index =
          row_offset + static_cast<std::size_t>(x);
      auto& pixel = (*pixels)[index];
      if (has_rgb_channel_mixer_adjustments(state)) {
        pixel = apply_rgb_channel_mixer(
            pixel,
            state.rgb_channel_mixer);
      }
      if (state.black_and_white_enabled) {
        pixel = apply_black_and_white_mixer(
            pixel,
            state,
            luminance_for_pixel(pixel, color_context));
      }
      pixel = apply_post_tone_color_adjustments(
          pixel,
          color_context,
          state,
          saturation_strength,
          vibrance_strength,
          0.0,
          output_headroom);
    }
  });
  if (cancellation.cancelled()) {
    return false;
  }
  if (!user_luts.empty()) {
    const auto user_lut_timer = StageClock::now();
    parallel_for(height, [&](int y) {
      if (cancellation.cancelled()) {
        return;
      }
      const auto row_offset =
          static_cast<std::size_t>(y) * safe_width;
      for (int x = 0; x < width; ++x) {
        const auto index =
            row_offset + static_cast<std::size_t>(x);
        (*pixels)[index] = apply_user_luts_to_pixel((*pixels)[index], user_luts);
      }
    });
    if (telemetry != nullptr) {
      telemetry->user_lut_ms +=
          elapsed_stage_ms(user_lut_timer);
    }
  }
  if (telemetry != nullptr) {
    const auto elapsed = elapsed_stage_ms(timer);
    telemetry->look_ms += elapsed;
    telemetry->post_color_ms += elapsed;
  }
  return !cancellation.cancelled() &&
         finite_color_stage_surface(*pixels);
}

void apply_color_adjustments_through_look_base(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    RemovalQuality,
    CancellationToken cancellation,
    ColorAdjustmentTelemetry* telemetry) {
  if (telemetry != nullptr) {
    *telemetry = {};
  }
  if (!valid_color_stage_surface(pixels, width, height) ||
      cancellation.cancelled()) {
    return;
  }
  auto context = prepare_color_prefix_stage_context(
      *pixels,
      width,
      height,
      state);
  if (!apply_color_normalize_raw_stage(
          pixels,
          width,
          height,
          state,
          &context,
          cancellation,
          telemetry) ||
      !apply_color_camera_characterization_stage(
          pixels,
          width,
          height,
          state,
          context,
          cancellation,
          telemetry) ||
      !apply_color_scene_adjustments_stage(
          pixels,
          width,
          height,
          state,
          cancellation,
          telemetry) ||
      !apply_color_tone_stage(
          pixels,
          width,
          height,
          state,
          cancellation,
          telemetry) ||
      !apply_color_curves_stage(
          pixels,
          width,
          height,
          state,
          cancellation,
          telemetry)) {
    return;
  }
  (void)apply_color_look_stage(
      pixels,
      width,
      height,
      state,
      cancellation,
      telemetry);
}

bool apply_color_adjustments_through_layers(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    RemovalQuality removal_quality,
    CancellationToken cancellation,
    ColorAdjustmentTelemetry* telemetry) {
  apply_color_adjustments_through_look_base(
      pixels,
      width,
      height,
      state,
      removal_quality,
      cancellation,
      telemetry);
  if (cancellation.cancelled()) {
    return false;
  }
  return apply_color_layers_stage(
      pixels,
      width,
      height,
      state,
      removal_quality,
      cancellation,
      telemetry);
}

bool apply_color_detail_stage(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    RemovalQuality removal_quality,
    CancellationToken cancellation,
    ColorAdjustmentTelemetry* telemetry,
    bool input_finite_validated) {
  const auto no_spatial_detail =
      std::abs(clamp_signed_percent(state.texture) / 100.0) <= 1e-6 &&
      std::abs(clamp_signed_percent(state.clarity) / 100.0) <= 1e-6 &&
      std::abs(clamp_signed_percent(state.dehaze) / 100.0) <= 1e-6;
  const auto no_fine_detail =
      std::abs(clamp_signed_percent(state.sharpening) / 100.0) <= 1e-6 &&
      std::abs(clamp_signed_percent(state.luminance_nr) / 100.0) <= 1e-6 &&
      std::abs(clamp_signed_percent(state.color_nr) / 100.0) <= 1e-6;
  if (no_spatial_detail && no_fine_detail) {
    return valid_color_stage_surface(pixels, width, height) &&
           !cancellation.cancelled() &&
           finite_color_stage_surface(*pixels, input_finite_validated);
  }
  if (!apply_spatial_detail_effects(
          pixels,
          width,
          height,
          state,
          removal_quality,
          cancellation,
          telemetry)) {
    return false;
  }
  apply_fine_detail_effects(
      pixels,
      width,
      height,
      state,
      cancellation,
      telemetry);
  return !cancellation.cancelled() &&
         std::ranges::all_of(
             *pixels,
             [](const ColorPixel& pixel) {
               return std::isfinite(pixel.red) &&
                      std::isfinite(pixel.green) &&
                      std::isfinite(pixel.blue) &&
                      std::isfinite(pixel.alpha);
             });
}

bool apply_color_adjustments_through_detail(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    RemovalQuality removal_quality,
    CancellationToken cancellation,
    ColorAdjustmentTelemetry* telemetry) {
  if (!apply_color_adjustments_through_layers(
      pixels,
      width,
      height,
      state,
      removal_quality,
      cancellation,
      telemetry)) return false;
  if (cancellation.cancelled()) {
    return false;
  }
  return apply_color_detail_stage(
      pixels,
      width,
      height,
      state,
      removal_quality,
      cancellation,
      telemetry);
}

bool apply_color_adjustments(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    RemovalQuality removal_quality,
    CancellationToken cancellation,
    ColorAdjustmentTelemetry* telemetry) {
  if (!apply_color_adjustments_through_detail(
      pixels,
      width,
      height,
      state,
      removal_quality,
      cancellation,
      telemetry)) return false;
  if (cancellation.cancelled() ||
      !apply_color_geometry_stage(
          pixels,
          width,
          height,
          state,
          cancellation,
          telemetry)) {
    return false;
  }
  if (photographic_rendering_model_uses_aces2(
          state.color_pipeline
              .photographic_rendering_model) &&
      state.color_pipeline.render_intent ==
          RenderIntent::Perceptual) {
    return true;
  }
  return apply_color_compatibility_output_stage(
      pixels,
      width,
      height,
      state,
      cancellation,
      telemetry);
}

}  // namespace photo_editor::pipeline
