#include "photo_editor/pipeline/color_adjustments.h"
#include "../engine/src/pipeline/color_highlight_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {
using namespace photo_editor::pipeline;

bool require(bool value, const char* message) {
  if (!value) std::cerr << message << '\n';
  return value;
}

bool same_rgb(const ColorPixel& left, const ColorPixel& right, float epsilon = 1.0e-6F) {
  return std::abs(left.red - right.red) <= epsilon &&
         std::abs(left.green - right.green) <= epsilon &&
         std::abs(left.blue - right.blue) <= epsilon && left.alpha == right.alpha;
}

InputTransformSettings settings() {
  InputTransformSettings result;
  result.raw_input_transform_enabled = true;
  result.highlight_recovery_enabled = true;
  result.highlight_reconstruction_mode = "Enabled";
  result.highlight_reconstruction_model = "camera-linear-opposed-v2";
  return result;
}
}  // namespace

int main() {
  // Canon 80D clipped sky: camera green reaches the sensor limit before the
  // unbalanced red/blue channels. Treating this as saturated scene color
  // creates magenta after the camera matrix and white balance.
  const auto neutral = std::array<double, 3>{0.44, 1.0, 0.60};
  const auto clipped_sky = ColorPixel{0.66F, 1.0F, 0.90F, 0.61F};
  const auto corrected_sky = reconstruct_camera_neutral_highlight(clipped_sky, neutral, 1.0);
  if (!require(corrected_sky.green > 1.4F && corrected_sky.green <= 1.5F &&
                   corrected_sky.red == clipped_sky.red && corrected_sky.blue == clipped_sky.blue &&
                   corrected_sky.alpha == clipped_sky.alpha,
               "single sensor-channel clipping must recover from calibrated surviving neutral anchors"))
    return EXIT_FAILURE;
  for (const auto color : {ColorPixel{1.0F, 0.2F, 0.1F, 0.5F},
           ColorPixel{0.1F, 1.0F, 0.2F, 0.5F}, ColorPixel{0.1F, 0.2F, 1.0F, 0.5F},
           ColorPixel{0.95F, 0.99F, 0.1F, 0.5F}, ColorPixel{-0.1F, 1.0F, 0.8F, 0.5F},
           ColorPixel{1.0F, 1.0F, 1.0F, 0.5F}, ColorPixel{0.40F, 0.90F, 0.50F, 0.5F},
           ColorPixel{0.8F, 1.2F, 0.9F, 0.5F}}) {
    if (!require(reconstruct_camera_neutral_highlight(color, neutral, 1.0) == color,
                 "colored, signed, fully clipped and unclipped samples must retain their exact RGB and alpha"))
      return EXIT_FAILURE;
  }
  if (!require(reconstruct_camera_neutral_highlight(clipped_sky, neutral, 0.0) == clipped_sky &&
                   reconstruct_camera_neutral_highlight(clipped_sky, {}, 1.0) == clipped_sky,
               "disabled recovery or missing camera calibration must leave pixels unchanged"))
    return EXIT_FAILURE;
  const auto input_settings = settings();
  const auto clipped_white = ColorPixel{1.0F, 1.0F, 1.0F, 0.61F};
  for (const auto background : {
           ColorPixel{0.80F, 0.10F, 0.10F, 1.0F},
           ColorPixel{0.10F, 0.80F, 0.10F, 1.0F},
           ColorPixel{0.10F, 0.10F, 0.80F, 1.0F}}) {
    auto pixels = std::vector<ColorPixel>(128U * 64U, background);
    pixels[16U * 128U + 16U] = clipped_white;
    auto analysis = analyze_highlight_reconstruction(pixels, 128, 64, input_settings);
    // Diagnostic/global chroma must never become recovery evidence, even if
    // an old retained context contains extreme scene-global ratios.
    analysis.chrominance_correction = {0.90, 0.05, 0.05};
    analysis.correction_reference_samples = {100U, 100U, 100U};
    const auto output = apply_camera_linear_highlight_reconstruction(
        clipped_white, 1.0, analysis, 16.5 / 128.0, 16.5 / 64.0);
    if (!require(output == clipped_white && analysis.reconstructed_channel_samples == 0U,
                 "fully clipped white must not acquire the unrelated scene's hue")) return EXIT_FAILURE;
  }

  constexpr int kWidth = 128;
  constexpr int kHeight = 64;
  constexpr std::size_t kTarget = 16U * 128U + 16U;
  auto source = std::vector<ColorPixel>(kWidth * kHeight, {0.90F, 0.85F, 0.20F, 1.0F});
  source[kTarget] = {1.0F, 1.0F, 0.40F, 0.37F};
  const auto local = analyze_highlight_reconstruction(source, kWidth, kHeight, input_settings);
  const auto recovered = apply_camera_linear_highlight_reconstruction(
      source[kTarget], 1.0, local, 16.5 / kWidth, 16.5 / kHeight);
  if (!require(local.local_reference_grid && recovered.red > source[kTarget].red &&
                   recovered.green > source[kTarget].green &&
                   recovered.red <= 1.25F && recovered.green <= 1.25F &&
                   recovered.blue == source[kTarget].blue && recovered.alpha == source[kTarget].alpha &&
                   local.reconstructed_channel_samples == 2U,
               "consistent bright local evidence should recover only clipped channels with bounded gain"))
    return EXIT_FAILURE;

  auto changed_distant_color = source;
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 64; x < kWidth; ++x)
      changed_distant_color[static_cast<std::size_t>(y * kWidth + x)] = {0.10F, 0.85F, 0.10F, 1.0F};
  }
  const auto distant = analyze_highlight_reconstruction(changed_distant_color, kWidth, kHeight, input_settings);
  const auto distant_output = apply_camera_linear_highlight_reconstruction(
      source[kTarget], 1.0, distant, 16.5 / kWidth, 16.5 / kHeight);
  if (!require(distant_output == recovered,
               "distant scene colors must not change local highlight recovery")) return EXIT_FAILURE;

  // Independently analyze sampling-equivalent draft/final inputs. The colored
  // strip lies outside the target's normalized neighborhood; a 32-pixel cell
  // rule incorrectly admitted it at the lower resolution only.
  auto low_resolution = source;
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 24; x < 32; ++x)
      low_resolution[static_cast<std::size_t>(y * kWidth + x)] = {0.10F, 0.90F, 0.10F, 1.0F};
  }
  auto high_resolution = std::vector<ColorPixel>(512U * 256U);
  for (int y = 0; y < 256; ++y) {
    for (int x = 0; x < 512; ++x)
      high_resolution[static_cast<std::size_t>(y * 512 + x)] =
          low_resolution[static_cast<std::size_t>((y / 4) * kWidth + x / 4)];
  }
  const auto low_analysis = analyze_highlight_reconstruction(low_resolution, kWidth, kHeight, input_settings);
  const auto high_analysis = analyze_highlight_reconstruction(high_resolution, 512, 256, input_settings);
  const auto low_output = apply_camera_linear_highlight_reconstruction(
      source[kTarget], 1.0, low_analysis, 16.5 / kWidth, 16.5 / kHeight);
  const auto high_output = apply_camera_linear_highlight_reconstruction(
      source[kTarget], 1.0, high_analysis, 16.5 / kWidth, 16.5 / kHeight);
  if (!require(same_rgb(low_output, high_output) && low_output.red > source[kTarget].red,
               "independent draft/final analyses must use the same normalized spatial support")) return EXIT_FAILURE;

  auto level_settings = input_settings;
  level_settings.raw_black_level = 0.10;
  level_settings.raw_white_level = 0.90;
  auto level_source = source;
  for (auto& pixel : level_source) {
    pixel.red = 0.10F + 0.80F * pixel.red;
    pixel.green = 0.10F + 0.80F * pixel.green;
    pixel.blue = 0.10F + 0.80F * pixel.blue;
  }
  const auto level_analysis = analyze_highlight_reconstruction(level_source, kWidth, kHeight, level_settings);
  if (!require(level_analysis.multi_channel_clipped_pixels == 1U &&
                   level_analysis.fully_clipped_pixels == 0U &&
                   level_analysis.reconstructed_channel_samples == 2U &&
                   same_rgb(apply_camera_linear_highlight_reconstruction(
                                source[kTarget], 1.0, level_analysis, 16.5 / kWidth, 16.5 / kHeight), recovered),
               "analysis must use the same overridden RAW black/white normalization as execution")) return EXIT_FAILURE;
  const auto unknown_position = apply_camera_linear_highlight_reconstruction(source[kTarget], 1.0, local);
  if (!require(unknown_position == source[kTarget],
               "recovery without a source position must use the conservative pixel fallback")) return EXIT_FAILURE;

  // These two reference cells contribute exactly one quarter of a bilinear
  // neighborhood. Algebraically equivalent crop/source coordinates may differ
  // by one double ULP; that roundoff must not switch recovery on or off.
  auto quarter_support = std::vector<ColorPixel>(128U * 128U, {0.10F, 0.10F, 0.10F, 1.0F});
  for (int y = 38; y < 42; ++y) {
    for (int x = 38; x < 40; ++x)
      quarter_support[static_cast<std::size_t>(y * 128 + x)] = {0.90F, 0.85F, 0.20F, 1.0F};
  }
  quarter_support[40U * 128U + 40U] = source[kTarget];
  const auto quarter_analysis = analyze_highlight_reconstruction(quarter_support, 128, 128, input_settings);
  const auto source_coordinate = 40.5 / 128.0;
  const auto quarter_exact = apply_camera_linear_highlight_reconstruction(
      source[kTarget], 1.0, quarter_analysis, source_coordinate, source_coordinate);
  for (const auto coordinate : {std::nextafter(source_coordinate, 0.0),
                                source_coordinate,
                                std::nextafter(source_coordinate, 1.0)}) {
    if (!require(quarter_exact.red > source[kTarget].red &&
                     same_rgb(apply_camera_linear_highlight_reconstruction(
                                  source[kTarget], 1.0, quarter_analysis, coordinate, coordinate), quarter_exact),
                 "one-ULP source-coordinate roundoff must not cross local evidence admission")) return EXIT_FAILURE;
  }

  for (const bool dark : {false, true}) {
    auto unreliable = source;
    for (std::size_t index = 0U; index < unreliable.size(); ++index) {
      unreliable[index] = dark ? ColorPixel{0.30F, 0.28F, 0.08F, 1.0F}
                              : (index % 2U == 0U ? ColorPixel{0.90F, 0.10F, 0.10F, 1.0F}
                                                 : ColorPixel{0.10F, 0.90F, 0.10F, 1.0F});
    }
    unreliable[kTarget] = source[kTarget];
    const auto analysis = analyze_highlight_reconstruction(unreliable, kWidth, kHeight, input_settings);
    if (!require(apply_camera_linear_highlight_reconstruction(
                     unreliable[kTarget], 1.0, analysis, 16.5 / kWidth, 16.5 / kHeight) == unreliable[kTarget] &&
                     analysis.reconstructed_channel_samples == 0U,
                 "dark or conflicting local evidence must not invent a recovery ratio")) return EXIT_FAILURE;
  }

  // One-channel saturation is valid color; fully clipped unequal HDR samples
  // may use their own opposed signal, without scene-global estimates.
  const std::array<ColorPixel, 3> edge_samples = {
      ColorPixel{1.80F, 0.30F, 0.12F, 0.42F},
      ColorPixel{-0.25F, 0.40F, 2.00F, 0.63F},
      ColorPixel{0.995F, 1.60F, 1.40F, 0.61F},
  };
  const auto edge_analysis = analyze_highlight_reconstruction(edge_samples, 3, 1, input_settings);
  if (!require(apply_camera_linear_highlight_reconstruction(edge_samples[0], 1.0, edge_analysis) == edge_samples[0] &&
                   apply_camera_linear_highlight_reconstruction(edge_samples[1], 1.0, edge_analysis) == edge_samples[1],
               "valid saturated color and signed working input must be preserved")) return EXIT_FAILURE;
  const auto hdr = apply_camera_linear_highlight_reconstruction(edge_samples[2], 1.0, edge_analysis);
  if (!require(hdr.red > edge_samples[2].red && hdr.red <= edge_samples[2].red * 1.25F &&
                   hdr.green == edge_samples[2].green && hdr.blue == edge_samples[2].blue &&
                   hdr.alpha == edge_samples[2].alpha && edge_analysis.reconstructed_channel_samples == 1U,
               "same-pixel HDR recovery and diagnostics must agree on the changed channel")) return EXIT_FAILURE;
  const auto one_ulp_up = std::nextafter(1.0F, 2.0F);
  const auto tiny_gain = std::array<ColorPixel, 1>{ColorPixel{1.0F, one_ulp_up, one_ulp_up, 0.61F}};
  const auto tiny_default = analyze_highlight_reconstruction(tiny_gain, 1, 1, input_settings);
  const auto tiny_extended = analyze_highlight_reconstruction(tiny_gain, 1, 1, input_settings, 0.50);
  if (!require(tiny_default.reconstructed_channel_samples == 0U &&
                   tiny_extended.reconstructed_channel_samples == 1U &&
                   apply_camera_linear_highlight_reconstruction(
                       tiny_gain[0], camera_linear_highlight_reconstruction_strength(input_settings), tiny_default) == tiny_gain[0],
               "telemetry must account for actual mode/headroom strength and float rounding")) return EXIT_FAILURE;
  const auto maximum = std::numeric_limits<float>::max();
  const auto huge = apply_camera_linear_highlight_reconstruction(
      {maximum / 2.0F, maximum, maximum, 0.73F}, 1.0, edge_analysis);
  if (!require(std::isfinite(huge.red) && std::isfinite(huge.green) && std::isfinite(huge.blue) &&
                   huge.alpha == 0.73F,
               "finite HDR recovery must remain finite and alpha-safe")) return EXIT_FAILURE;

  // Carry the exact full-source context through a crop, as preview/export do.
  // The shared context must resolve the original source coordinates, not the
  // crop's origin or a new scene-global correction.
  auto state = ColorAdjustmentState{};
  state.input_transform = input_settings;
  auto full = source;
  auto full_context = ColorPrefixStageContext{local, true};
  if (!apply_color_normalize_raw_stage(&full, kWidth, kHeight, state, &full_context)) return EXIT_FAILURE;
  auto crop = std::vector<ColorPixel>{};
  for (int y = 8; y < 40; ++y) {
    for (int x = 8; x < 40; ++x)
      crop.push_back(source[static_cast<std::size_t>(y * kWidth + x)]);
  }
  state.input_transform.source_area_x = 8.0 / kWidth;
  state.input_transform.source_area_y = 8.0 / kHeight;
  state.input_transform.source_area_width = 32.0 / kWidth;
  state.input_transform.source_area_height = 32.0 / kHeight;
  auto crop_context = ColorPrefixStageContext{local, true};
  if (!apply_color_normalize_raw_stage(&crop, 32, 32, state, &crop_context)) return EXIT_FAILURE;
  for (int y = 0; y < 32; ++y) {
    for (int x = 0; x < 32; ++x) {
      if (!require(same_rgb(crop[static_cast<std::size_t>(y * 32 + x)],
                            full[static_cast<std::size_t>((y + 8) * kWidth + x + 8)]),
                   "retained source analysis must make cropped and full-source recovery agree")) return EXIT_FAILURE;
    }
  }
  if (!require(crop_context.highlight_reconstruction.local_reference_grid == local.local_reference_grid,
               "compiled recovery contexts must share their bounded reference grid")) return EXIT_FAILURE;

  auto disabled_settings = input_settings;
  auto tiny_area_settings = input_settings;
  tiny_area_settings.source_area_width = std::numeric_limits<double>::denorm_min();
  const auto tiny_area = analyze_highlight_reconstruction(source, kWidth, kHeight, tiny_area_settings);
  const auto tiny_output = apply_camera_linear_highlight_reconstruction(source[kTarget], 1.0, tiny_area, 0.0, 0.5);
  if (!require(tiny_area.active && std::isfinite(tiny_output.red) &&
                   std::isfinite(tiny_output.green) && std::isfinite(tiny_output.blue) &&
                   tiny_output.alpha == source[kTarget].alpha,
               "subnormal source extents must keep the bounded reference grid finite")) return EXIT_FAILURE;

  disabled_settings.highlight_reconstruction_mode = "Disabled";
  const auto disabled = analyze_highlight_reconstruction(source, kWidth, kHeight, disabled_settings);
  disabled_settings = input_settings;
  disabled_settings.highlight_reconstruction_model = "legacy-working-rgb-v1";
  const auto legacy = analyze_highlight_reconstruction(source, kWidth, kHeight, disabled_settings);
  if (!require(!disabled.active && !legacy.active && !disabled.local_reference_grid &&
                   !legacy.local_reference_grid &&
                   apply_camera_linear_highlight_reconstruction(source[kTarget], 1.0, disabled) == source[kTarget],
               "disabled and historical working-RGB reconstruction must remain separate")) return EXIT_FAILURE;

  std::cout << "bounded local RAW highlight reconstruction regressions passed\n";
  return EXIT_SUCCESS;
}
