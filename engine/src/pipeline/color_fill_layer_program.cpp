#include "photo_editor/pipeline/color_fill_layer_program.h"
#include "color_working_space_runtime.h"
#include "color_fill_layer_admission.h"
#include "color_fill_layer_blend_program.h"
#include "color_fill_layer_curve_program.h"
#include "color_fill_layer_geometry_program.h"
#include "color_fill_layer_look_program.h"
#include "color_fill_layer_lut_program.h"
#include "color_input_runtime.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <ranges>
namespace photo_editor::pipeline {
namespace {
constexpr auto kPi = 3.14159265358979323846;

[[nodiscard]] bool finite_layer_pixels(std::span<const ColorPixel> pixels) {
  // A reduction, rather than an early-exit branch per channel, lets the compiler
  // check blocks of floats together. Every component is still validated.
  unsigned invalid = 0U;
  for (const auto& pixel : pixels) {
    invalid |= static_cast<unsigned>(!std::isfinite(pixel.red)) |
        static_cast<unsigned>(!std::isfinite(pixel.green)) |
        static_cast<unsigned>(!std::isfinite(pixel.blue)) |
        static_cast<unsigned>(!std::isfinite(pixel.alpha));
  }
  return invalid == 0U;
}
}  // namespace
bool CompiledFillLayerProgram::ready() const noexcept {
  return valid && width > 0U && height > 0U &&
         layer_count <= kMaximumLayers &&
         mask_data.size() <= kMaximumMaskBytes &&
         pixel_data_view().size() <= kMaximumPixelBytes / sizeof(ColorPixel) &&
         (!cached_pixels || (cached_pixels->owner && cached_pixels->identity == pixel_identity && pixel_data.empty())) &&
         tone_float_count <= kMaximumToneBytes / sizeof(float) &&
         curve_float_count <= kMaximumCurveBytes / sizeof(float) &&
         std::isfinite(blend_headroom) && blend_headroom >= 1.0F &&
         std::isfinite(output_headroom) && output_headroom >= 1.0F &&
         std::ranges::all_of(rgb_to_xyz_d65.values, [](double value) {
           return std::isfinite(value);
         }) &&
         std::ranges::all_of(xyz_d65_to_rgb.values, [](double value) {
           return std::isfinite(value);
         }) &&
         std::ranges::all_of(rgb_to_xyz_native.values, [](double value) {
           return std::isfinite(value);
         }) &&
         std::ranges::all_of(
             layers.cbegin(), layers.cbegin() + layer_count,
             [this](const auto& layer) {
               return std::ranges::all_of(layer.color, [](float value) {
                        return std::isfinite(value);
                      }) &&
                      std::isfinite(layer.opacity) && layer.opacity >= 0.0F &&
                      layer.opacity <= 1.0F &&
                      std::isfinite(layer.mask_opacity) &&
                      std::isfinite(layer.mask_density) &&
                      layer.blend_mode <= 13U && layer.kind <= 2U &&
                      std::isfinite(layer.exposure_gain) &&
                      layer.exposure_gain > 0.0F &&
                      std::ranges::all_of(
                          layer.white_balance,
                          [](float value) { return std::isfinite(value); }) &&
                      std::isfinite(layer.hue_shift) &&
                      std::isfinite(layer.saturation_shift) &&
                      std::isfinite(layer.colorize_hue) &&
                      std::isfinite(layer.colorize_amount) &&
                      std::isfinite(layer.lens_vignette) &&
                      std::isfinite(layer.dehaze) &&
                      std::isfinite(layer.texture) &&
                      std::isfinite(layer.clarity) &&
                      (!layer.has_tone ||
                       (tone_identity != 0U && layer.kind == 2U &&
                        tone_luts[static_cast<std::size_t>(
                            &layer - layers.data())].ready() &&
                        static_cast<std::uint64_t>(layer.tone_offset) +
                                CompiledToneStageLut::kSampleCount <=
                            tone_float_count)) &&
                      (!layer.has_advanced_tone ||
                       (tone_identity != 0U && layer.kind == 2U &&
                        advanced_tone_luts[static_cast<std::size_t>(
                            &layer - layers.data())].ready() &&
                        static_cast<std::uint64_t>(layer.advanced_tone_offset) +
                                CompiledToneStageLut::kSampleCount <=
                            tone_float_count)) &&
                      retained_layer_curves_ready(
                          *this, layer, static_cast<std::size_t>(
                              &layer - layers.data())) &&
                      retained_layer_downstream_look_ready(
                          *this, layer, static_cast<std::size_t>(
                              &layer - layers.data())) &&
                      (!layer.has_user_lut ||
                       (layer.kind == 2U && user_lut_programs[
                           static_cast<std::size_t>(&layer - layers.data())].ready())) &&
                      (!layer.has_geometry ||
                       (layer.kind == 2U && geometry_programs[
                           static_cast<std::size_t>(&layer - layers.data())].ready() &&
                        geometry_programs[static_cast<std::size_t>(
                            &layer - layers.data())].active)) &&
                      (!layer.has_fine_detail ||
                       (layer.kind == 2U && fine_detail_programs[
                           static_cast<std::size_t>(&layer - layers.data())].ready())) &&
                      (!layer.has_look ||
                       (layer.kind == 2U &&
                        (layer.has_local_chroma ||
                        look_programs[static_cast<std::size_t>(
                            &layer - layers.data())].ready()))) &&
                      (layer.kind != 1U ||
                       (pixel_identity != 0U && layer.pixel_width > 0U &&
                        layer.pixel_height > 0U &&
                        static_cast<std::uint64_t>(layer.pixel_offset) +
                                static_cast<std::uint64_t>(layer.pixel_width) *
                                    layer.pixel_height <=
                            pixel_data_view().size())) &&
                      (!layer.has_mask ||
                       (mask_identity != 0U && layer.mask_width > 0U &&
                        layer.mask_height > 0U &&
                        static_cast<std::uint64_t>(layer.mask_offset) +
                                static_cast<std::uint64_t>(layer.mask_width) *
                                    layer.mask_height <=
                            mask_data.size()));
             });
}
bool CompiledFillLayerProgram::active() const noexcept { return layer_count > 0U; }
bool retained_simple_layers_supported(const ColorAdjustmentState& state) {
  const auto context = make_pipeline_color_context(state);
  auto active_count = std::size_t{0U};
  auto mask_bytes = std::size_t{0U};
  auto pixel_count = std::size_t{0U};
  for (const auto& layer : state.photo_layers) {
    if (!layer.enabled || layer.opacity <= 1.0e-6) continue;
    // The retained flat program has no isolated group surface. Keep nested
    // stacks on the compositor until it can preserve group opacity and blend.
    if (layer.kind == "group" || !layer.parent_id.empty()) return false;
    // Spatial effects are executed by the CPU layer path until native effect
    // kernels are connected. A retained LUT/program must not omit them.
    if (photo_layer_has_active_effects(layer)) return false;
    const auto has_mask = !layer.mask_data.empty();
    const auto mask_size = layer.mask_width > 0 && layer.mask_height > 0
        ? static_cast<std::size_t>(layer.mask_width) *
              static_cast<std::size_t>(layer.mask_height)
        : 0U;
    const auto is_fill = layer.kind == "fill";
    const auto is_raster = layer.kind == "pixel" || layer.kind == "text" ||
                           layer.kind == "source";
    const auto is_adjustment =
        retained_layer_adjustment_supported(layer, state, context);
    const auto exposure_gain = is_adjustment
        ? static_cast<float>(std::exp2(layer.exposure)) : 1.0F;
    const auto raster_size = layer.pixel_width > 0 && layer.pixel_height > 0
        ? static_cast<std::size_t>(layer.pixel_width) *
              static_cast<std::size_t>(layer.pixel_height)
        : 0U;
    if ((!is_fill && !is_raster && !is_adjustment) ||
        (is_adjustment &&
         (!std::isfinite(exposure_gain) || exposure_gain <= 0.0F)) ||
        (is_adjustment && retained_layer_tone_active(layer) &&
         !compile_tone_stage_lut(retained_layer_tone_state(layer, state)).ready()) ||
        (is_adjustment && retained_layer_advanced_tone_active(layer) &&
         !compile_retained_layer_advanced_tone(layer, state).ready()) ||
        (is_adjustment && !retained_layer_curves_supported(layer, state)) ||
        (is_adjustment && !retained_layer_downstream_look_supported(
                              layer, state, context)) ||
        (is_adjustment &&
         (std::abs(layer.vibrance) > 1.0e-9 ||
          std::abs(layer.saturation) > 1.0e-9) &&
         !compile_look_stage_program(retained_layer_look_state(layer, state)).ready()) ||
        (is_raster &&
         (raster_size != layer.pixels.size() || layer.pixel_width <= 0 ||
          layer.pixel_height <= 0 || !std::isfinite(layer.transform_scale) ||
          !std::isfinite(layer.transform_rotation) ||
          (!layer.text_material.empty() && layer.text_material != "none"))) ||
        (has_mask && (mask_size != layer.mask_data.size() ||
                      layer.mask_width <= 0 || layer.mask_height <= 0)) ||
        !std::isfinite(layer.opacity) ||
        !std::isfinite(layer.mask_opacity) ||
        !std::isfinite(layer.mask_density) ||
        (is_raster && !finite_layer_pixels(layer.pixels)) ||
        !std::ranges::all_of(
            std::array{layer.fill_color.red, layer.fill_color.green,
                       layer.fill_color.blue, layer.fill_color.alpha},
            [](float value) { return std::isfinite(value); }) ||
        retained_layer_blend_mode_id(layer.blend_mode) == 255U ||
        (has_mask &&
         (layer.mask_data.size() >
              CompiledFillLayerProgram::kMaximumMaskBytes - mask_bytes)) ||
        (is_raster &&
         (layer.pixels.size() >
              CompiledFillLayerProgram::kMaximumPixelBytes /
                      sizeof(ColorPixel) -
                  pixel_count)) ||
        ++active_count > CompiledFillLayerProgram::kMaximumLayers) {
      return false;
    }
    if (has_mask) mask_bytes += layer.mask_data.size();
    if (is_raster) pixel_count += layer.pixels.size();
  }
  return true;
}
CompiledFillLayerProgram compile_fill_layer_program(
    const ColorAdjustmentState& state, int width, int height,
    std::shared_ptr<const CachedFillLayerPixels> cached_pixels) {
  auto program = CompiledFillLayerProgram{};
  // Admission supports detail programs directly. Copying the entire state to
  // zero these controls duplicated every raster and hid invalid detail values.
  if (width <= 0 || height <= 0 ||
      !retained_simple_layers_supported(state)) return program;
  // A pointer or recipe revision cannot prove immutable content: callers can
  // change pixels in place. Compare every byte against the pinned GPU payload.
  if (cached_pixels && cached_pixels->owner && cached_pixels->identity != 0U &&
      cached_pixels->pixels.size() <= CompiledFillLayerProgram::kMaximumPixelBytes / sizeof(ColorPixel)) {
    auto offset = std::size_t{0};
    bool matches = true;
    for (const auto& layer : state.photo_layers) {
      if (!layer.enabled || layer.opacity <= 1.0e-6 ||
          (layer.kind != "pixel" && layer.kind != "source" && layer.kind != "text")) continue;
      if (layer.pixels.size() > cached_pixels->pixels.size()-offset ||
          std::memcmp(layer.pixels.data(), cached_pixels->pixels.data()+offset,
              layer.pixels.size()*sizeof(ColorPixel)) != 0) { matches = false; break; }
      offset += layer.pixels.size();
    }
    if (matches && offset == cached_pixels->pixels.size() && offset != 0U) {
      program.cached_pixels = std::move(cached_pixels);
      program.pixel_identity = program.cached_pixels->identity;
    }
  }
  const auto context = make_pipeline_color_context(state);
  program.blend_headroom = static_cast<float>(context.layer_blend_headroom);
  program.output_headroom = static_cast<float>(context.output_headroom);
  program.render_intent = context.render_intent;
  program.gamut_mapping_policy = context.output_gamut_mapping_policy;
  program.gamut_mapping_enabled = context.output_gamut_mapping_enabled;
  program.rgb_to_xyz_d65 = context.rgb_to_xyz_d65;
  program.xyz_d65_to_rgb = context.xyz_d65_to_rgb;
  program.rgb_to_xyz_native = context.rgb_to_xyz_native;
  program.width = static_cast<std::uint32_t>(width);
  program.height = static_cast<std::uint32_t>(height);
  auto raster_offset = std::size_t{0};
  for (const auto& layer : state.photo_layers) {
    if (layer.kind == "group" || !layer.enabled || layer.opacity <= 1.0e-6) continue;
    auto& compiled = program.layers[program.layer_count++];
    compiled.color = {layer.fill_color.red, layer.fill_color.green,
                      layer.fill_color.blue, layer.fill_color.alpha};
    compiled.opacity = static_cast<float>(std::clamp(layer.opacity, 0.0, 1.0));
    compiled.blend_mode = retained_layer_blend_mode_id(layer.blend_mode);
    compiled.mask_opacity = static_cast<float>(
        std::clamp(layer.mask_opacity, 0.0, 1.0));
    compiled.mask_density = static_cast<float>(
        std::clamp(layer.mask_density, 0.0, 1.0));
    compiled.mask_inverted = layer.mask_inverted;
    compiled.has_mask = !layer.mask_data.empty();
    compiled.clipping_mask = layer.clipping_mask;
    compiled.erase_base_image = layer.erase_base_image;
    const auto is_raster = layer.kind == "pixel" || layer.kind == "text" ||
                           layer.kind == "source";
    const auto is_adjustment = layer.kind == "adjustment";
    compiled.kind = is_adjustment ? 2U : (is_raster ? 1U : 0U);
    if (is_adjustment) {
      compiled.exposure_gain = static_cast<float>(std::exp2(layer.exposure));
      compiled.has_white_balance =
          std::abs(layer.temperature) > 1.0e-9 || std::abs(layer.tint) > 1.0e-9;
      if (compiled.has_white_balance) {
        const auto matrix = white_balance_matrix(
            layer.temperature, layer.tint, context);
        for (auto index = std::size_t{0U}; index < matrix.values.size(); ++index) {
          compiled.white_balance[index] =
              static_cast<float>(matrix.values[index]);
        }
      }
      compiled.has_look =
          std::abs(layer.vibrance) > 1.0e-9 ||
          std::abs(layer.saturation) > 1.0e-9 ||
          retained_layer_local_chroma_active(layer);
      compiled.has_local_chroma = retained_layer_local_chroma_active(layer);
      if (compiled.has_look) {
        program.look_programs[program.layer_count - 1U] =
            compile_look_stage_program(retained_layer_look_state(layer, state));
      }
      if (compiled.has_local_chroma) {
        compiled.hue_shift = static_cast<float>(
            std::clamp(layer.hue_shift, -100.0, 100.0) / 100.0 * kPi);
        compiled.saturation_shift = static_cast<float>(
            std::clamp(layer.saturation_shift, -100.0, 100.0) / 100.0 * 0.92);
        const auto hue = layer.colorize_hue - std::floor(layer.colorize_hue);
        compiled.colorize_hue = static_cast<float>(hue * 2.0 * kPi);
        compiled.colorize_amount = static_cast<float>(
            std::clamp(layer.colorize_strength / 100.0, 0.0, 1.0));
      }
      compiled.has_tone = retained_layer_tone_active(layer);
      if (compiled.has_tone) {
        auto tone = compile_tone_stage_lut(retained_layer_tone_state(layer, state));
        compiled.tone_offset = static_cast<std::uint32_t>(program.tone_float_count);
        program.tone_float_count += tone.samples.size();
        program.tone_luts[program.layer_count - 1U] = std::move(tone);
      }
      compiled.has_advanced_tone = retained_layer_advanced_tone_active(layer);
      if (compiled.has_advanced_tone) {
        auto tone = compile_retained_layer_advanced_tone(layer, state);
        compiled.advanced_tone_offset =
            static_cast<std::uint32_t>(program.tone_float_count);
        program.tone_float_count += tone.samples.size();
        program.advanced_tone_luts[program.layer_count - 1U] = std::move(tone);
      }
      append_retained_layer_curves(
          layer, state, program.layer_count - 1U, &program);
      append_retained_layer_downstream_look(
          layer, state, program.layer_count - 1U, &program);
      auto user_luts = compile_retained_layer_user_luts(layer, state);
      compiled.has_user_lut = user_luts.active();
      if (compiled.has_user_lut) {
        program.user_lut_programs[program.layer_count - 1U] =
            std::move(user_luts);
      }
      compiled.lens_vignette = static_cast<float>(
          std::clamp(layer.lens_vignette, -100.0, 100.0) / 100.0);
      compiled.dehaze = static_cast<float>(
          std::clamp(layer.dehaze, -100.0, 100.0) / 100.0);
      compiled.texture = static_cast<float>(
          std::clamp(layer.texture, -100.0, 100.0) / 100.0);
      compiled.clarity = static_cast<float>(
          std::clamp(layer.clarity, -100.0, 100.0) / 100.0);
      auto geometry = compile_retained_layer_geometry(
          layer, state, width, height);
      compiled.has_geometry = geometry.ready() && geometry.active;
      if (compiled.has_geometry) {
        program.geometry_programs[program.layer_count - 1U] =
            std::move(geometry);
      }
      compiled.has_spatial_detail = std::abs(compiled.texture) > 1.0e-6F ||
                                    std::abs(compiled.clarity) > 1.0e-6F;
      auto detail_state = ColorAdjustmentState{};
      detail_state.color_pipeline = state.color_pipeline;
      detail_state.input_transform = state.input_transform;
      detail_state.sharpening = layer.sharpening;
      detail_state.sharpen_radius = layer.sharpen_radius;
      detail_state.sharpen_detail = layer.sharpen_detail;
      detail_state.sharpen_masking = layer.sharpen_masking;
      detail_state.luminance_nr = layer.luminance_nr;
      detail_state.color_nr = layer.color_nr;
      auto fine_detail = compile_fine_detail_program(detail_state, width, height);
      compiled.has_fine_detail =
          std::abs(layer.sharpening) > 1.0e-6 ||
          std::abs(layer.luminance_nr) > 1.0e-6 ||
          std::abs(layer.color_nr) > 1.0e-6;
      if (compiled.has_fine_detail) {
        program.fine_detail_programs[program.layer_count - 1U] =
            std::move(fine_detail);
      }
    }
    if (is_raster) {
      compiled.pixel_offset = static_cast<std::uint32_t>(raster_offset);
      compiled.pixel_width = static_cast<std::uint32_t>(layer.pixel_width);
      compiled.pixel_height = static_cast<std::uint32_t>(layer.pixel_height);
      compiled.x = layer.x;
      compiled.y = layer.y;
      compiled.transform_scale = static_cast<float>(
          std::clamp(layer.transform_scale, 0.05, 32.0));
      compiled.transform_rotation = static_cast<float>(
          -layer.transform_rotation * kPi / 180.0);
      if (!program.cached_pixels) program.pixel_data.insert(
          program.pixel_data.end(), layer.pixels.cbegin(), layer.pixels.cend());
      raster_offset += layer.pixels.size();
    }
    if (compiled.has_mask) {
      compiled.mask_offset = static_cast<std::uint32_t>(program.mask_data.size());
      compiled.mask_width = static_cast<std::uint32_t>(layer.mask_width);
      compiled.mask_height = static_cast<std::uint32_t>(layer.mask_height);
      program.mask_data.insert(
          program.mask_data.end(), layer.mask_data.cbegin(), layer.mask_data.cend());
    }
  }
  program.valid = true;
  if (!program.mask_data.empty()) {
    auto identity = std::uint64_t{14695981039346656037ULL};
    for (const auto value : program.mask_data) {
      identity ^= value;
      identity *= 1099511628211ULL;
    }
    program.mask_identity = identity != 0U ? identity : 1U;
  }
  if (!program.pixel_data.empty()) {
    auto identity = std::uint64_t{14695981039346656037ULL};
    for (const auto& pixel : program.pixel_data) {
      for (const auto value : {pixel.red, pixel.green, pixel.blue, pixel.alpha}) {
        identity ^= std::bit_cast<std::uint32_t>(value);
        identity *= 1099511628211ULL;
      }
    }
    program.pixel_identity = identity != 0U ? identity : 1U;
  }
  if (program.tone_float_count > 0U) {
    auto identity = std::uint64_t{14695981039346656037ULL};
    for (auto index = std::size_t{0U}; index < program.layer_count; ++index) {
      if (program.layers[index].has_tone) {
        identity ^= program.tone_luts[index].cache_identity;
        identity *= 1099511628211ULL;
      }
      if (program.layers[index].has_advanced_tone) {
        identity ^= program.advanced_tone_luts[index].cache_identity;
        identity *= 1099511628211ULL;
      }
    }
    program.tone_identity = identity != 0U ? identity : 1U;
  }
  finish_retained_layer_curve_identity(&program);
  finish_retained_layer_downstream_look_identity(&program);
  if (!program.ready()) program.valid = false;
  return program;
}
}  // namespace photo_editor::pipeline
