#include "photo_editor/pipeline/film_grain_profiles.h"
#include "photo_editor/pipeline/photo_effects.h"
#include "photo_editor/pipeline/photo_effect_codec.h"
#include "photo_editor/pipeline/color_spatial_detail_program.h"
#include "../engine/src/pipeline/color_detail_runtime.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <vector>

using namespace photo_editor::pipeline;
#define CHECK(condition, message) do { if (!(condition)) { std::cerr << message << '\n'; return 1; } } while (false)
namespace {
constexpr int width = 192, height = 128;
std::vector<ColorPixel> flat(float value = 0.18F) {
  return std::vector<ColorPixel>(static_cast<std::size_t>(width * height), {value, value, value, 0.7F});
}
double delta(const std::vector<ColorPixel>& a, const std::vector<ColorPixel>& b) {
  double result = 0;
  for (std::size_t i = 0; i < a.size(); ++i)
    result = std::max({result, std::abs(double(a[i].red - b[i].red)),
        std::abs(double(a[i].green - b[i].green)), std::abs(double(a[i].blue - b[i].blue))});
  return result;
}
double variance(const std::vector<ColorPixel>& pixels) {
  double sum = 0, square = 0;
  for (const auto& p : pixels) { sum += p.green; square += double(p.green) * p.green; }
  const auto count = static_cast<double>(pixels.size());
  return square / count - (sum / count) * (sum / count);
}
double correlation(const std::vector<ColorPixel>& pixels) {
  double square = 0, pair = 0;
  for (int y = 0; y < height; ++y) for (int x = 0; x + 1 < width; ++x) {
    const auto i = static_cast<std::size_t>(y * width + x);
    const auto a = pixels[i].green - 0.18;
    square += a * a; pair += a * (pixels[i + 1].green - 0.18);
  }
  return pair / square;
}
}
int main() {
  PhotoEffectInstance grain;
  grain.id = "film"; grain.type_id = "film_grain";
  grain.grain.seed = 20261022;
  auto legacy = flat();
  CHECK(apply_photo_effect(&legacy, width, height, grain).ok(), "Legacy grain renders");
  const auto legacy_payload = serialize_photo_effect_stack(std::vector{grain});
  CHECK(legacy_payload && deserialize_photo_effect_stack(*legacy_payload)->front() == grain, "Original four-field grain payload stays unchanged");
  std::vector<std::vector<ColorPixel>> stocks;
  for (const auto& stock : film_grain_profiles) {
    grain.grain.stock = stock.id;
    grain.grain.size = stock.size; grain.grain.strength = stock.strength;
    grain.grain.color = stock.color; grain.grain.roughness = stock.roughness;
    auto rendered = flat();
    CHECK(apply_photo_effect(&rendered, width, height, grain).ok(), "Every stock renders");
    auto repeated = flat();
    CHECK(apply_photo_effect(&repeated, width, height, grain).ok() && delta(rendered, repeated) == 0, "Seeded stocks are deterministic");
    CHECK(variance(rendered) > 1e-7, "Stock has visible density variation");
    double mean = 0;
    for (const auto& pixel : rendered) { mean += pixel.green; CHECK(pixel.alpha == 0.7F, "Grain preserves alpha"); }
    mean /= static_cast<double>(rendered.size());
    CHECK(std::abs(mean - 0.18) < 0.0015, "Stock does not introduce a significant exposure bias");
    const auto payload = serialize_photo_effect_stack(std::vector{grain});
    CHECK(payload && deserialize_photo_effect_stack(*payload)->front() == grain, "All stock and tone parameters roundtrip exactly");
    auto tile = std::vector<ColorPixel>(32U * 24U, {0.18F, 0.18F, 0.18F, 0.7F});
    PhotoEffectRenderOptions options; options.origin_x = 37; options.origin_y = 29;
    CHECK(apply_photo_effect(&tile, 32, 24, grain, options).ok(), "Document tile renders");
    for (int y = 0; y < 24; ++y) for (int x = 0; x < 32; ++x)
      CHECK(tile[static_cast<std::size_t>(y * 32 + x)].green == rendered[static_cast<std::size_t>((y + 29) * width + x + 37)].green, "No seams or reseeding at tile boundaries");
    auto masked = flat(); std::vector<float> mask(masked.size(), 0);
    CHECK(apply_photo_effect(&masked, width, height, grain, {}, mask).ok() && delta(masked, flat()) == 0, "Zero coverage is exact identity");
    stocks.push_back(std::move(rendered));
  }
  CHECK(variance(stocks[5]) > variance(stocks[3]) * 3, "Pushed silver has more density variation than fine silver");
  CHECK(correlation(stocks[5]) > correlation(stocks[3]) + 0.15, "Pushed silver is spatially coarser, not just stronger");
  // Same size, strength, coupling, and clumping: family coefficients still differ.
  grain.grain.size = 1.5; grain.grain.strength = .05; grain.grain.color = -1; grain.grain.roughness = .5;
  grain.grain.stock = "tabular_100"; auto fine = flat(); CHECK(apply_photo_effect(&fine, width, height, grain).ok(), "Fine family");
  grain.grain.stock = "classic_400"; auto coarse = flat(); CHECK(apply_photo_effect(&coarse, width, height, grain).ok(), "Classic family");
  CHECK(delta(fine, coarse) > .005 && std::abs(correlation(fine) - correlation(coarse)) > .01, "Stock structure differs at equal slider settings");
  grain.grain.shadows = .2; auto quiet = flat(.005F); CHECK(apply_photo_effect(&quiet, width, height, grain).ok(), "Quiet shadows");
  grain.grain.shadows = 2; auto loud = flat(.005F); CHECK(apply_photo_effect(&loud, width, height, grain).ok(), "Strong shadows");
  CHECK(variance(loud) > variance(quiet) * 3, "Shadow grain trim changes shadow response");
  grain.grain.stock = "not-a-stock";
  auto invalid = flat(); CHECK(!apply_photo_effect(&invalid, width, height, grain).ok() && delta(invalid, flat()) == 0, "Unknown stocks fail without publishing pixels");
  grain.grain.stock = "classic_400"; grain.grain.roughness = std::numeric_limits<double>::quiet_NaN();
  CHECK(!serialize_photo_effect_stack(std::vector{grain}), "Non-finite grain structure rejected");

  auto noisy = flat();
  for (std::size_t i = 0; i < noisy.size(); ++i) {
    const auto noise = static_cast<float>((i * 7919U + i * i * 31U) % 1021U) / 1020.F - .5F;
    noisy[i].red += noise * .035F; noisy[i].green += noise * .035F; noisy[i].blue += noise * .035F;
  }
  ColorAdjustmentState state; state.luminance_nr = 85;
  auto cleaned = noisy; apply_fine_detail_effects(&cleaned, width, height, state, {}, nullptr);
  CHECK(variance(cleaned) < variance(noisy) * .5, "Luminance cleanup suppresses flat-field noise");
  auto chroma = flat();
  for (std::size_t i = 0; i < chroma.size(); ++i) {
    const auto n = static_cast<float>((i * 3571U) % 1021U) / 1020.F - .5F;
    chroma[i].red += n * .035F; chroma[i].blue -= n * .035F;
  }
  state = {}; state.color_nr = 100;
  auto color_clean = chroma; apply_fine_detail_effects(&color_clean, width, height, state, {}, nullptr);
  double before = 0, after = 0;
  for (std::size_t i = 0; i < chroma.size(); ++i) {
    before += std::abs(chroma[i].red - chroma[i].blue); after += std::abs(color_clean[i].red - color_clean[i].blue);
  }
  CHECK(after < before * .5, "Color cleanup suppresses speckle");
  auto edge = flat();
  for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
    edge[static_cast<std::size_t>(y * width + x)] = x < width / 2 ? ColorPixel{.5F,.03F,.02F,1} : ColorPixel{.02F,.03F,.5F,1};
  auto color_edge = edge; apply_fine_detail_effects(&color_edge, width, height, state, {}, nullptr);
  CHECK(delta(edge, color_edge) < .025, "Color cleanup protects a strong color boundary");
  state = {}; state.sharpening = 70; state.sharpen_radius = 1.1;
  auto sharp_a = noisy; apply_fine_detail_effects(&sharp_a, width, height, state, {}, nullptr);
  state.sharpen_radius = 1.2; auto sharp_b = noisy; apply_fine_detail_effects(&sharp_b, width, height, state, {}, nullptr);
  CHECK(delta(sharp_a, sharp_b) > 1e-6, "Fractional radius changes rendering within one integer interval");
  state.sharpen_radius = 1.49; sharp_a = noisy; apply_fine_detail_effects(&sharp_a, width, height, state, {}, nullptr);
  state.sharpen_radius = 1.51; sharp_b = noisy; apply_fine_detail_effects(&sharp_b, width, height, state, {}, nullptr);
  CHECK(delta(sharp_a, sharp_b) < .002, "Radius no longer jumps at the rounding boundary");
  state.luminance_nr = 63; state.color_nr = 72; state.sharpen_masking = 35;
  auto combined = chroma; apply_fine_detail_effects(&combined, width, height, state, {}, nullptr);
  auto compiled = chroma;
  CHECK(apply_compiled_fine_detail_program(&compiled, compile_fine_detail_program(state, width, height)) && delta(compiled, combined) < 3e-5, "Compiled and canonical detail paths agree");
  for (const auto& pixel : combined) CHECK(pixel.alpha == .7F && std::isfinite(pixel.red), "Detail preserves alpha and finite pixels");
  std::cout << "Six stock families: structure, density, tonal response, seeds, tiles, masks, persistence; detail: denoising, color edges, fractional radius, compiled parity passed\n";
}
