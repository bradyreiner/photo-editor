#pragma once

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_geometry_program.h"
#include "photo_editor/pipeline/color_lut_program.h"
#include "photo_editor/pipeline/color_spatial_detail_program.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace photo_editor::pipeline {

struct CompiledFillLayer final {
  std::array<float, 4> color{};
  float opacity = 0.0F;
  float mask_opacity = 1.0F;
  float mask_density = 1.0F;
  std::uint32_t blend_mode = 0U;
  std::uint32_t mask_offset = 0U;
  std::uint32_t mask_width = 0U;
  std::uint32_t mask_height = 0U;
  std::uint32_t kind = 0U;
  std::uint32_t pixel_offset = 0U;
  std::uint32_t pixel_width = 0U;
  std::uint32_t pixel_height = 0U;
  std::int32_t x = 0, y = 0;
  float transform_scale = 1.0F;
  float transform_rotation = 0.0F;
  float exposure_gain = 1.0F;
  std::array<float, 9> white_balance = {
      1.0F, 0.0F, 0.0F,
      0.0F, 1.0F, 0.0F,
      0.0F, 0.0F, 1.0F,
  };
  std::uint32_t tone_offset = 0U, advanced_tone_offset = 0U;
  std::array<std::uint32_t, 6> curve_offsets{};
  std::uint32_t curve_active_mask = 0U;
  bool mask_inverted = false, has_mask = false, clipping_mask = false;
  bool erase_base_image = false;
  bool has_tone = false, has_advanced_tone = false, has_curves = false;
  bool has_white_balance = false, has_look = false, has_local_chroma = false;
  bool has_downstream_look = false, has_user_lut = false;
  bool has_geometry = false, has_spatial_detail = false, has_fine_detail = false;
  float hue_shift = 0.0F, saturation_shift = 0.0F;
  float colorize_hue = 0.0F, colorize_amount = 0.0F, lens_vignette = 0.0F, dehaze = 0.0F;
  float texture = 0.0F, clarity = 0.0F;
};

// A read-only, lifetime-pinned pixel payload already resident in a backend.
// Reuse is permitted only after exact byte comparison with every active raster.
struct CachedFillLayerPixels final {
  std::span<const ColorPixel> pixels;
  std::shared_ptr<const void> owner;
  std::uint64_t identity = 0U;
};

struct CompiledFillLayerProgram final {
  static constexpr std::size_t kMaximumLayers = 16U;
  static constexpr std::size_t kMaximumMaskBytes = 64U * 1024U * 1024U;
  static constexpr std::size_t kMaximumPixelBytes = 512U * 1024U * 1024U;
  static constexpr std::size_t kMaximumToneBytes = 4U * 1024U * 1024U;
  static constexpr std::size_t kMaximumCurveBytes = 2U * 1024U * 1024U;
  std::array<CompiledFillLayer, kMaximumLayers> layers{};
  std::vector<std::uint8_t> mask_data;
  std::vector<ColorPixel> pixel_data;
  std::shared_ptr<const CachedFillLayerPixels> cached_pixels;
  std::array<CompiledToneStageLut, kMaximumLayers> tone_luts{};
  std::array<CompiledToneStageLut, kMaximumLayers> advanced_tone_luts{};
  std::array<CompiledCurveStageLuts, kMaximumLayers> curve_luts{};
  std::array<CompiledLookStageProgram, kMaximumLayers> look_programs{};
  std::array<CompiledLookStageProgram, kMaximumLayers> downstream_look_programs{};
  std::array<CompiledUserLutStageProgram, kMaximumLayers> user_lut_programs{};
  std::array<CompiledGeometryStageProgram, kMaximumLayers> geometry_programs{};
  std::array<CompiledFineDetailProgram, kMaximumLayers> fine_detail_programs{};
  ColorMatrix3 rgb_to_xyz_d65, xyz_d65_to_rgb;
  ColorMatrix3 rgb_to_xyz_native;
  std::size_t tone_float_count = 0U;
  std::size_t curve_float_count = 0U;
  float blend_headroom = 1.0F, output_headroom = 1.0F;
  RenderIntent render_intent = RenderIntent::Perceptual;
  GamutMappingPolicy gamut_mapping_policy = GamutMappingPolicy::Perceptual;
  bool gamut_mapping_enabled = true;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::uint32_t layer_count = 0U;
  std::uint64_t mask_identity = 0U;
  std::uint64_t pixel_identity = 0U;
  std::uint64_t tone_identity = 0U;
  std::uint64_t curve_identity = 0U;
  std::uint64_t downstream_look_identity = 0U;
  bool valid = false;

  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] bool active() const noexcept;
  [[nodiscard]] std::span<const ColorPixel> pixel_data_view() const noexcept {
    return cached_pixels ? cached_pixels->pixels : std::span<const ColorPixel>(pixel_data);
  }
};

[[nodiscard]] bool retained_simple_layers_supported(
    const ColorAdjustmentState& state);
[[nodiscard]] CompiledFillLayerProgram compile_fill_layer_program(
    const ColorAdjustmentState& state,
    int width,
    int height,
    std::shared_ptr<const CachedFillLayerPixels> cached_pixels = {});
[[nodiscard]] bool apply_compiled_fill_layer_program(
    std::vector<ColorPixel>* pixels,
    const CompiledFillLayerProgram& program);

}  // namespace photo_editor::pipeline
