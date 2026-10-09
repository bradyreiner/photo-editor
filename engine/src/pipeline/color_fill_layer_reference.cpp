#include "photo_editor/pipeline/color_fill_layer_program.h"
#include "color_fill_layer_blend_reference.h"
#include "color_fill_layer_chroma_reference.h"
#include "color_fill_layer_detail_program.h"
#include "color_fill_layer_effect_reference.h"
#include "color_layer_composite.h"
#include "color_layer_sampling.h"

#include <algorithm>
#include <cmath>

namespace photo_editor::pipeline {

namespace {

[[nodiscard]] bool raster_source_at(
    const CompiledFillLayerProgram& program,
    const CompiledFillLayer& layer,
    std::uint32_t x,
    std::uint32_t y,
    ColorPixel* source) {
  const auto center_x = static_cast<float>(layer.x) +
      static_cast<float>(layer.pixel_width) * layer.transform_scale * 0.5F;
  const auto center_y = static_cast<float>(layer.y) +
      static_cast<float>(layer.pixel_height) * layer.transform_scale * 0.5F;
  const auto dx = static_cast<float>(x) + 0.5F - center_x;
  const auto dy = static_cast<float>(y) + 0.5F - center_y;
  const auto cosine = std::cos(layer.transform_rotation);
  const auto sine = std::sin(layer.transform_rotation);
  const auto local_x = (dx * cosine - dy * sine) / layer.transform_scale +
      static_cast<float>(layer.pixel_width) * 0.5F - 0.5F;
  const auto local_y = (dx * sine + dy * cosine) / layer.transform_scale +
      static_cast<float>(layer.pixel_height) * 0.5F - 0.5F;
  *source = sample_layer_filtered(
      program.pixel_data_view().subspan(layer.pixel_offset,
          static_cast<std::size_t>(layer.pixel_width) * layer.pixel_height),
      static_cast<int>(layer.pixel_width), static_cast<int>(layer.pixel_height), local_x, local_y, layer.transform_scale);
  return true;
}

[[nodiscard]] float mask_weight_at(
    const CompiledFillLayerProgram& program,
    const CompiledFillLayer& layer,
    std::uint32_t x,
    std::uint32_t y) {
  if (!layer.has_mask) return 1.0F;
  const auto nx = program.width > 1U
      ? static_cast<double>(x) / static_cast<double>(program.width - 1U) : 0.0;
  const auto ny = program.height > 1U
      ? static_cast<double>(y) / static_cast<double>(program.height - 1U) : 0.0;
  const auto mx = std::min(
      static_cast<std::uint32_t>(std::lround(
          nx * static_cast<double>(layer.mask_width - 1U))),
      layer.mask_width - 1U);
  const auto my = std::min(
      static_cast<std::uint32_t>(std::lround(
          ny * static_cast<double>(layer.mask_height - 1U))),
      layer.mask_height - 1U);
  auto weight = static_cast<float>(program.mask_data[
      layer.mask_offset + my * layer.mask_width + mx]) / 255.0F;
  if (layer.mask_inverted) weight = 1.0F - weight;
  return std::clamp(
      std::min(weight, layer.mask_density) * layer.mask_opacity, 0.0F, 1.0F);
}

[[nodiscard]] ColorPixel adjustment_source_at(
    const CompiledFillLayerProgram& program, const CompiledFillLayer& layer,
    std::size_t index, std::uint32_t x, std::uint32_t y, ColorPixel source) {
  if (layer.has_white_balance) {
    source = ColorPixel{
        source.red * layer.white_balance[0] + source.green * layer.white_balance[1] +
            source.blue * layer.white_balance[2],
        source.red * layer.white_balance[3] + source.green * layer.white_balance[4] +
            source.blue * layer.white_balance[5],
        source.red * layer.white_balance[6] + source.green * layer.white_balance[7] +
            source.blue * layer.white_balance[8], source.alpha};
  }
  source.red *= layer.exposure_gain;
  source.green *= layer.exposure_gain;
  source.blue *= layer.exposure_gain;
  if (layer.has_tone)
    source = apply_compiled_tone_stage_lut(program.tone_luts[index], source);
  if (layer.has_local_chroma)
    source = apply_compiled_fill_layer_chroma(program, index, source);
  else if (layer.has_look)
    source = apply_compiled_look_stage_program(program.look_programs[index], source);
  if (layer.has_advanced_tone)
    source = apply_compiled_tone_stage_lut(program.advanced_tone_luts[index], source);
  if (layer.has_curves)
    source = apply_compiled_curve_stage_luts(program.curve_luts[index], source);
  if (layer.has_downstream_look)
    source = apply_compiled_look_stage_program(
        program.downstream_look_programs[index], source);
  if (layer.has_user_lut)
    source = apply_compiled_user_lut_stage_program(
        program.user_lut_programs[index], source);
  return apply_compiled_fill_layer_point_effects(program, layer, x, y, source);
}

}  // namespace

bool apply_compiled_fill_layer_program(
    std::vector<ColorPixel>* pixels, const CompiledFillLayerProgram& program) {
  if (pixels == nullptr || !program.ready() ||
      pixels->size() != static_cast<std::size_t>(program.width) * program.height) {
    return false;
  }
  auto previous_layer_alpha = std::vector<float>(pixels->size(), 1.0F);
  for (auto index = std::uint32_t{0U}; index < program.layer_count; ++index) {
    const auto& layer = program.layers[index];
    const auto adjustment = layer.kind == 2U;
    auto current_layer_alpha = std::vector<float>(pixels->size(), 0.0F);
    auto adjusted_sources = std::vector<ColorPixel>{};
    if (adjustment &&
        (layer.has_geometry || layer.has_spatial_detail || layer.has_fine_detail)) {
      adjusted_sources = *pixels;
      for (auto y = std::uint32_t{0U}; y < program.height; ++y) {
        for (auto x = std::uint32_t{0U}; x < program.width; ++x) {
          const auto source_index = static_cast<std::size_t>(y) * program.width + x;
          adjusted_sources[source_index] = adjustment_source_at(
              program, layer, index, x, y, adjusted_sources[source_index]);
        }
      }
      if (layer.has_geometry && !apply_compiled_geometry_stage_program(
              &adjusted_sources, program.geometry_programs[index])) {
        return false;
      }
      if ((layer.has_spatial_detail || layer.has_fine_detail) &&
          !apply_compiled_fill_layer_detail(&adjusted_sources, program, index)) {
        return false;
      }
    }
    for (auto y = std::uint32_t{0U}; y < program.height; ++y) {
      for (auto x = std::uint32_t{0U}; x < program.width; ++x) {
        const auto pixel_index = static_cast<std::size_t>(y) * program.width + x;
        auto& pixel = (*pixels)[pixel_index];
        auto source = adjustment
            ? (adjusted_sources.empty()
                  ? adjustment_source_at(program, layer, index, x, y, pixel)
                  : adjusted_sources[pixel_index])
            : ColorPixel{layer.color[0], layer.color[1], layer.color[2], layer.color[3]};
        if (layer.kind == 1U && !raster_source_at(program, layer, x, y, &source)) {
          continue;
        }
        const auto composition_weight = std::clamp(
            layer.opacity * mask_weight_at(program, layer, x, y) *
                (layer.clipping_mask ? previous_layer_alpha[pixel_index] : 1.0F),
            0.0F, 1.0F);
        const auto alpha = adjustment
            ? composition_weight
            : std::clamp(source.alpha * composition_weight, 0.0F, 1.0F);
        current_layer_alpha[pixel_index] = alpha;
        if (layer.erase_base_image) {
          if (alpha > 1.0e-6F) {
            pixel.alpha *= 1.0F - alpha;
            if (pixel.alpha <= 1.0e-6F) pixel = ColorPixel{0.0F, 0.0F, 0.0F, 0.0F};
          }
          current_layer_alpha[pixel_index] = pixel.alpha;
          continue;
        }
        const auto blended = apply_retained_layer_blend(
            program, pixel, source, layer.blend_mode);
        if (adjustment) {
          pixel.red = std::lerp(pixel.red, blended.red, alpha);
          pixel.green = std::lerp(pixel.green, blended.green, alpha);
          pixel.blue = std::lerp(pixel.blue, blended.blue, alpha);
        } else pixel = composite_layer_source_over(pixel, source, blended, alpha);
      }
    }
    previous_layer_alpha = std::move(current_layer_alpha);
  }
  return true;
}

}  // namespace photo_editor::pipeline
