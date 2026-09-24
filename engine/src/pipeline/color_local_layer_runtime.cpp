#include "color_local_layer_runtime.h"
#include "photo_editor/pipeline/local_mask_sampling.h"

#include "color_adjustment_kernel_runtime.h"
#include "color_adjustment_scheduler.h"
#include "color_input_runtime.h"
#include "color_layer_runtime.h"
#include "color_local_fast_path_policy.h"
#include "color_mask_runtime.h"
#include "color_mask_coverage_cache.h"
#include "color_math_runtime.h"
#include "color_working_space_runtime.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <numbers>
#include <ranges>
#include <utility>
#include <vector>

namespace photo_editor::pipeline {

namespace {

constexpr double kPi = std::numbers::pi_v<double>;

template <typename Work>
void parallel_for(int count, Work&& work) {
  run_color_adjustment_parallel_for(
      count, std::function<void(int)>(std::forward<Work>(work)));
}

using StageClock = std::chrono::steady_clock;

[[nodiscard]] std::int64_t elapsed_stage_ms(StageClock::time_point start) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             StageClock::now() - start)
      .count();
}

enum class LocalAdjustmentFastPathResult {
  unsupported,
  applied,
  cancelled,
};

[[nodiscard]] LocalAdjustmentFastPathResult apply_single_brush_exposure_fast_path(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    const LocalAdjustmentCoordinateMapping& coordinate_mapping,
    CancellationToken cancellation) {
  if (state.local_mask_sets.size() != 1) {
    return LocalAdjustmentFastPathResult::unsupported;
  }
  const auto& mask_set = state.local_mask_sets.front();
  if (!mask_set.enabled || !local_mask_set_is_exposure_only(mask_set) ||
      mask_set.sources.size() != 1) {
    return LocalAdjustmentFastPathResult::unsupported;
  }
  const auto& source = mask_set.sources.front();
  if (!source.enabled || source.kind != "brush" || source.operation != "add" ||
      (source.blend_mode != "normal" && !source.blend_mode.empty()) ||
      !brush_mask_source_has_default_refinement(source)) {
    return LocalAdjustmentFastPathResult::unsupported;
  }
  const auto expected_mask_size =
      static_cast<std::size_t>(source.segmentation_width) *
      static_cast<std::size_t>(source.segmentation_height);
  if (source.segmentation_mask.empty() || source.segmentation_width <= 0 ||
      source.segmentation_height <= 0 ||
      source.segmentation_mask.size() != expected_mask_size) {
    return LocalAdjustmentFastPathResult::unsupported;
  }

  const auto safe_width = static_cast<std::size_t>(width);
  std::vector<double> normalized_x_values(safe_width, 0.5);
  for (int x = 0; x < width; ++x) {
    normalized_x_values[static_cast<std::size_t>(x)] =
        normalized_source_coordinate(
            x,
            coordinate_mapping.source_x,
            coordinate_mapping.source_width);
  }

  parallel_for(height, [&](int y) {
    if (cancellation.cancelled()) {
      return;
    }
    const auto normalized_y =
        normalized_source_coordinate(
            y,
            coordinate_mapping.source_y,
            coordinate_mapping.source_height);
    const auto row_offset = static_cast<std::size_t>(y) * safe_width;
    for (int x = 0; x < width; ++x) {
      const auto index = row_offset + static_cast<std::size_t>(x);
      auto& pixel = (*pixels)[index];
      const auto normalized_x = normalized_x_values[static_cast<std::size_t>(x)];
      const auto mask_weight = brush_mask_weight(normalized_x, normalized_y, source);
      if (mask_weight > 0.0) {
        const auto gain = std::pow(2.0, mask_set.exposure * mask_weight);
        pixel.red = static_cast<float>(static_cast<double>(pixel.red) * gain);
        pixel.green = static_cast<float>(static_cast<double>(pixel.green) * gain);
        pixel.blue = static_cast<float>(static_cast<double>(pixel.blue) * gain);
      }
    }
  });
  return cancellation.cancelled() ? LocalAdjustmentFastPathResult::cancelled
                                  : LocalAdjustmentFastPathResult::applied;
}

}  // namespace

namespace {

bool apply_local_adjustments_to_pixels_impl(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    const LocalAdjustmentCoordinateMapping& coordinate_mapping,
    CancellationToken cancellation,
    const std::vector<ColorPixel>* edge_reference = nullptr) {
  if (pixels == nullptr || width <= 0 || height <= 0) {
    return false;
  }
  const auto safe_width = static_cast<std::size_t>(width);
  const auto safe_height = static_cast<std::size_t>(height);
  if (safe_height > 0 && safe_width > std::numeric_limits<std::size_t>::max() / safe_height) {
    return false;
  }
  if (pixels->size() != safe_width * safe_height) {
    return false;
  }
  if (!local_adjustment_coordinate_mapping_is_valid(coordinate_mapping, width, height)) {
    return false;
  }
  if (state.local_mask_sets.empty()) {
    return true;
  }
  if (!state.local_masks.empty() || !state.retouch_masks.empty() ||
      has_photo_layers(state.photo_layers) || state.linear_mask_enabled ||
      state.radial_mask_enabled) {
    return false;
  }
  if (cancellation.cancelled()) {
    return false;
  }

  const auto color_context = make_pipeline_color_context(state);
  const auto fast_path_result = edge_reference != nullptr
      ? LocalAdjustmentFastPathResult::unsupported
      : apply_single_brush_exposure_fast_path(
          pixels,
          width,
          height,
          state,
          coordinate_mapping,
          cancellation);
  if (fast_path_result == LocalAdjustmentFastPathResult::applied) {
    return true;
  }
  if (fast_path_result == LocalAdjustmentFastPathResult::cancelled) {
    return false;
  }

  struct LocalMaskSetDescriptor final {
    const LocalMaskSet* mask_set = nullptr;
    bool has_adjustments = false;
    bool has_decontaminate = false;
    std::optional<CompiledLocalAdjustment> compiled;
    std::size_t prefix_size = 0;
    MaskCoverageLease coverage;
  };
  std::vector<LocalMaskSetDescriptor> mask_sets;
  mask_sets.reserve(state.local_mask_sets.size());
  for (const auto& mask_set : state.local_mask_sets) {
    const auto has_adjustments = local_mask_set_has_adjustments(mask_set);
    const auto has_decontaminate = has_selection_decontaminate(mask_set);
    if (has_adjustments || has_decontaminate) {
      mask_sets.push_back(LocalMaskSetDescriptor{
          .mask_set = &mask_set,
          .has_adjustments = has_adjustments,
          .has_decontaminate = has_decontaminate,
          .compiled = has_adjustments ? compile_local_adjustment(
              color_context, state.color_pipeline.photographic_rendering_model,
              LocalAdjustmentSettings{
                  .exposure = mask_set.exposure, .contrast = mask_set.contrast,
                  .highlights = mask_set.highlights, .shadows = mask_set.shadows,
                  .whites = mask_set.whites, .blacks = mask_set.blacks,
                  .temperature = mask_set.temperature, .tint = mask_set.tint,
                  .vibrance = mask_set.vibrance, .saturation = mask_set.saturation,
                  .hue_shift = mask_set.hue_shift, .saturation_shift = mask_set.saturation_shift,
                  .colorize_hue = mask_set.colorize_hue, .colorize_strength = mask_set.colorize_strength})
              : std::nullopt,
          .prefix_size = static_cast<std::size_t>(&mask_set - state.local_mask_sets.data()) + 1,
          .coverage = {},
      });
    }
  }
  if (mask_sets.empty()) {
    return true;
  }

  const auto needs_edge_matte_source =
      !state.local_masks.empty() ||
      !state.local_mask_sets.empty();
  const auto coverage_frame = acquire_mask_coverage_frame(*pixels, edge_reference, width, height,
      color_context, state.color_pipeline.photographic_rendering_model, coordinate_mapping, cancellation);
  auto edge_matte_source_pixels =
      needs_edge_matte_source && edge_reference == nullptr && !coverage_frame
          ? *pixels
          : std::vector<ColorPixel>{};
  const EdgeAwareMatteContext edge_matte_source_context{
      .pixels =
          coverage_frame ? &mask_coverage_edge_pixels(*coverage_frame) : needs_edge_matte_source
              ? (edge_reference != nullptr ? edge_reference : &edge_matte_source_pixels)
              : nullptr,
      .width = width,
      .height = height,
  };
  for (auto& descriptor : mask_sets) {
    descriptor.coverage = acquire_mask_coverage(coverage_frame,
        std::span(state.local_mask_sets).first(descriptor.prefix_size), cancellation);
  }
  parallel_for(height, [&](int y) {
    if (cancellation.cancelled()) {
      return;
    }
    const auto row_offset = static_cast<std::size_t>(y) * safe_width;
    for (int x = 0; x < width; ++x) {
      const auto index = row_offset + static_cast<std::size_t>(x);
      auto& pixel = (*pixels)[index];
      const auto normalized_x = normalized_source_coordinate(
          x,
          coordinate_mapping.source_x,
          coordinate_mapping.source_width);
      const auto normalized_y = normalized_source_coordinate(
          y,
          coordinate_mapping.source_y,
          coordinate_mapping.source_height);
      for (const auto& descriptor : mask_sets) {
        const auto& mask_set = *descriptor.mask_set;
        const auto mask_weight = descriptor.coverage.hit ? descriptor.coverage.weight(index) : local_mask_set_weight(
            normalized_x,
            normalized_y,
            pixel,
            color_context,
            mask_set,
            &edge_matte_source_context);
        descriptor.coverage.record(index, mask_weight);
        if (descriptor.has_adjustments) {
          apply_local_adjustment_mask(
              &pixel,
              color_context,
              state.color_pipeline.photographic_rendering_model,
              mask_weight,
              mask_set.exposure,
              mask_set.contrast,
              mask_set.highlights,
              mask_set.shadows,
              mask_set.whites,
              mask_set.blacks,
              mask_set.temperature,
              mask_set.tint,
              mask_set.vibrance,
              mask_set.saturation,
              mask_set.hue_shift,
              mask_set.saturation_shift,
              mask_set.colorize_hue,
              mask_set.colorize_strength,
              descriptor.compiled ? &*descriptor.compiled : nullptr);
        }
        if (descriptor.has_decontaminate) {
          apply_selection_decontaminate_stages(
              &pixel,
              normalized_x,
              normalized_y,
              color_context,
              mask_set,
              &edge_matte_source_context);
        }
      }
    }
  });
  for (const auto& descriptor : mask_sets)
    publish_mask_coverage(coverage_frame, descriptor.coverage, cancellation);
  return !cancellation.cancelled();
}

}  // namespace

bool apply_local_adjustments_to_look_pixels_with_context(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    const std::vector<ColorPixel>& edge_pixels,
    CancellationToken cancellation) {
  if (pixels == nullptr || pixels == &edge_pixels || pixels->size() != edge_pixels.size()) return false;
  return apply_local_adjustments_to_pixels_impl(
      pixels, width, height, state,
      default_local_adjustment_coordinate_mapping(width, height), cancellation, &edge_pixels);
}

bool apply_local_adjustments_to_look_pixels(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    CancellationToken cancellation) {
  return apply_local_adjustments_to_look_pixels(
      pixels,
      width,
      height,
      state,
      default_local_adjustment_coordinate_mapping(
          width,
          height),
      cancellation);
}

bool apply_local_adjustments_to_look_pixels(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    const LocalAdjustmentCoordinateMapping&
        coordinate_mapping,
    CancellationToken cancellation) {
  return apply_local_adjustments_to_pixels_impl(
      pixels,
      width,
      height,
      state,
      coordinate_mapping,
      cancellation);
}

bool apply_color_layers_stage(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    RemovalQuality removal_quality,
    CancellationToken cancellation,
    ColorAdjustmentTelemetry* telemetry) {
  if (pixels == nullptr || width <= 0 || height <= 0 ||
      pixels->size() !=
          static_cast<std::size_t>(width) *
              static_cast<std::size_t>(height) ||
      cancellation.cancelled()) {
    return false;
  }

  // A global-only edit has no layer work. Avoid copying and walking the full
  // preview surface before discovering that every local/layer collection is
  // empty. Retain the stage's finite-output contract for invalid upstream
  // data while making the common exposure-only path effectively free.
  if (state.local_mask_sets.empty() && state.local_masks.empty() &&
      state.retouch_masks.empty() && state.photo_layers.empty() &&
      !state.linear_mask_enabled && !state.radial_mask_enabled) {
    return std::ranges::all_of(
        *pixels,
        [](const ColorPixel& pixel) {
          return std::isfinite(pixel.red) &&
                 std::isfinite(pixel.green) &&
                 std::isfinite(pixel.blue) &&
                 std::isfinite(pixel.alpha);
        });
  }

  const auto color_context =
      make_pipeline_color_context(state);
  const auto edge_matte_source_pixels = *pixels;
  const EdgeAwareMatteContext edge_matte_source_context{
      .pixels = &edge_matte_source_pixels,
      .width = width,
      .height = height,
  };
  const auto normalized_coordinate =
      [](int coordinate, int dimension) {
        return dimension > 1
                   ? static_cast<double>(coordinate) /
                         static_cast<double>(dimension - 1)
                   : 0.5;
      };

  if (!state.local_mask_sets.empty()) {
    auto local_state = state;
    local_state.local_masks.clear();
    local_state.retouch_masks.clear();
    local_state.photo_layers.clear();
    local_state.linear_mask_enabled = false;
    local_state.radial_mask_enabled = false;
    if (!apply_local_adjustments_to_look_pixels(
            pixels,
            width,
            height,
            local_state,
            cancellation)) {
      return false;
    }
  } else if (!state.local_masks.empty()) {
    LocalAdjustmentSettings shared_adjustments;
    const auto stack_is_additive =
        local_masks_all_additive(state.local_masks);
    const auto stack_shares_adjustments =
        local_masks_share_adjustments(
            state.local_masks,
            &shared_adjustments);
    if (!stack_shares_adjustments) {
      (void)primary_local_mask_adjustments(
          state.local_masks,
          &shared_adjustments);
    }
    const auto use_composite_path =
        stack_shares_adjustments ||
        !stack_is_additive;
    const auto has_decontaminate =
        has_selection_decontaminate(
            state.local_masks);
    parallel_for(height, [&](int y) {
      if (cancellation.cancelled()) {
        return;
      }
      const auto normalized_y =
          normalized_coordinate(y, height);
      for (int x = 0; x < width; ++x) {
        const auto index =
            static_cast<std::size_t>(
                y * width + x);
        auto& pixel = (*pixels)[index];
        const auto normalized_x =
            normalized_coordinate(x, width);
        if (use_composite_path) {
          const auto weight =
              composite_local_mask_weight(
                  normalized_x,
                  normalized_y,
                  pixel,
                  color_context,
                  state.local_masks,
                  &edge_matte_source_context);
          apply_local_adjustment_mask(
              &pixel,
              color_context,
              state.color_pipeline
                  .photographic_rendering_model,
              weight,
              shared_adjustments.exposure,
              shared_adjustments.contrast,
              shared_adjustments.highlights,
              shared_adjustments.shadows,
              shared_adjustments.whites,
              shared_adjustments.blacks,
              shared_adjustments.temperature,
              shared_adjustments.tint,
              shared_adjustments.vibrance,
              shared_adjustments.saturation,
              shared_adjustments.hue_shift,
              shared_adjustments.saturation_shift,
              shared_adjustments.colorize_hue,
              shared_adjustments.colorize_strength);
        } else {
          for (const auto& mask :
               state.local_masks) {
            apply_local_adjustment_mask(
                &pixel,
                color_context,
                state.color_pipeline
                    .photographic_rendering_model,
                nested_local_mask_weight(
                    normalized_x,
                    normalized_y,
                    pixel,
                    color_context,
                    mask,
                    state.local_masks,
                    &edge_matte_source_context),
                mask.exposure,
                mask.contrast,
                mask.highlights,
                mask.shadows,
                mask.whites,
                mask.blacks,
                mask.temperature,
                mask.tint,
                mask.vibrance,
                mask.saturation,
                mask.hue_shift,
                mask.saturation_shift,
                mask.colorize_hue,
                mask.colorize_strength);
          }
        }
        if (has_decontaminate) {
          apply_selection_decontaminate_stages(
              &pixel,
              normalized_x,
              normalized_y,
              color_context,
              state.local_masks,
              &edge_matte_source_context);
        }
      }
    });
  } else {
    const auto gradient_radians =
        state.linear_mask_rotation * kPi / 180.0;
    const auto gradient_cosine =
        std::cos(gradient_radians);
    const auto gradient_sine =
        std::sin(gradient_radians);
    const auto gradient_feather =
        std::max(0.05, state.linear_mask_feather);
    parallel_for(height, [&](int y) {
      if (cancellation.cancelled()) {
        return;
      }
      const auto normalized_y =
          normalized_coordinate(y, height);
      for (int x = 0; x < width; ++x) {
        const auto index =
            static_cast<std::size_t>(
                y * width + x);
        auto& pixel = (*pixels)[index];
        const auto normalized_x =
            normalized_coordinate(x, width);
        if (state.linear_mask_enabled) {
          const auto axis_coordinate =
              ((normalized_x - 0.5) *
                   gradient_cosine +
               (normalized_y - 0.5) *
                   gradient_sine) +
              0.5;
          const auto weight =
              clamp_unit(
                  ((state.linear_mask_position -
                    axis_coordinate) /
                   gradient_feather) +
                  0.5);
          apply_local_adjustment_mask(
              &pixel,
              color_context,
              state.color_pipeline
                  .photographic_rendering_model,
              weight,
              state.linear_mask_exposure,
              0.0,
              0.0,
              0.0,
              0.0,
              0.0,
              state.linear_mask_temperature,
              0.0,
              0.0,
              0.0,
              0.0,
              0.0,
              0.0,
              0.0);
        }
        if (state.radial_mask_enabled) {
          apply_local_adjustment_mask(
              &pixel,
              color_context,
              state.color_pipeline
                  .photographic_rendering_model,
              radial_mask_weight(
                  normalized_x,
                  normalized_y,
                  state),
              state.radial_mask_exposure,
              0.0,
              0.0,
              0.0,
              0.0,
              0.0,
              state.radial_mask_temperature,
              0.0,
              0.0,
              0.0,
              0.0,
              0.0,
              0.0,
              0.0);
        }
      }
    });
  }
  if (cancellation.cancelled()) {
    return false;
  }

  const auto retouch_timer = StageClock::now();
  const auto& retouch_masks =
      state.retouch_masks.empty()
          ? state.local_masks
          : state.retouch_masks;
  if (has_local_inpaint_patch(retouch_masks)) {
    apply_local_inpaint_patches(
        pixels,
        width,
        height,
        color_context,
        retouch_masks);
  }
  if (has_local_mask_clone(retouch_masks)) {
    apply_local_clone(
        pixels,
        width,
        height,
        color_context,
        retouch_masks);
  }
  if (has_local_mask_red_eye(retouch_masks)) {
    apply_local_red_eye(
        pixels,
        width,
        height,
        color_context,
        retouch_masks);
  }
  if (has_local_mask_skin_cleanup(retouch_masks)) {
    apply_local_skin_cleanup(
        pixels,
        width,
        height,
        color_context,
        retouch_masks);
  }
  if (has_local_mask_frequency_smoothing(
          retouch_masks)) {
    apply_local_frequency_smoothing(
        pixels,
        width,
        height,
        color_context,
        retouch_masks);
  }
  if (cancellation.cancelled()) {
    return false;
  }

  if (has_local_mask_heal(retouch_masks)) {
    std::vector<double> heal_mask(
        pixels->size(),
        0.0);
    auto has_nonzero_heal = false;
    auto texture_sum = 0.0;
    auto color_sum = 0.0;
    auto weight_sum = 0.0;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const auto index =
            static_cast<std::size_t>(
                y * width + x);
        const auto heal = composite_local_heal(
            normalized_coordinate(x, width),
            normalized_coordinate(y, height),
            (*pixels)[index],
            color_context,
            retouch_masks);
        heal_mask[index] = heal.weight;
        has_nonzero_heal =
            has_nonzero_heal ||
            heal.weight > 1.0e-4;
        if (heal.weight > 1.0e-4) {
          texture_sum +=
              heal.weight *
              heal.texture_blend;
          color_sum +=
              heal.weight *
              heal.color_blend;
          weight_sum += heal.weight;
        }
      }
    }
    if (has_nonzero_heal) {
      const auto texture_blend =
          weight_sum > 1.0e-6
              ? texture_sum / weight_sum
              : 0.68;
      const auto color_blend =
          weight_sum > 1.0e-6
              ? color_sum / weight_sum
              : 0.46;
      apply_local_removal(
          pixels,
          width,
          height,
          heal_mask,
          0.35 +
              clamp_unit(texture_blend) *
                  0.30,
          removal_quality,
          texture_blend,
          color_blend,
          cancellation);
    }
  }
  if (has_local_mask_removal(retouch_masks)) {
    std::vector<double> removal_mask(
        pixels->size(),
        0.0);
    auto has_nonzero_removal = false;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const auto index =
            static_cast<std::size_t>(
                y * width + x);
        removal_mask[index] =
            composite_local_removal_weight(
                normalized_coordinate(
                    x,
                    width),
                normalized_coordinate(
                    y,
                    height),
                (*pixels)[index],
                color_context,
                retouch_masks,
                "");
        has_nonzero_removal =
            has_nonzero_removal ||
            removal_mask[index] > 1.0e-4;
      }
    }
    if (has_nonzero_removal) {
      apply_local_removal(
          pixels,
          width,
          height,
          removal_mask,
          0.0,
          removal_quality,
          1.0,
          1.0,
          cancellation);
    }
  }
  if (telemetry != nullptr) {
    telemetry->retouch_ms +=
        elapsed_stage_ms(retouch_timer);
  }
  if (cancellation.cancelled()) {
    return false;
  }

  std::string layer_error;
  if (!apply_standard_photo_layers(pixels, width, height, color_context,
          state.color_pipeline.photographic_rendering_model, state.photo_layers, cancellation, &layer_error)) {
    if (telemetry != nullptr) telemetry->failure_reason = layer_error;
    return false;
  }
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


}  // namespace photo_editor::pipeline
