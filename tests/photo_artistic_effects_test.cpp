#include "photo_editor/pipeline/photo_artistic_effects.h"
#include "photo_editor/pipeline/photo_effects.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

using namespace photo_editor::pipeline;

namespace {
int fail(std::string_view message) {
  std::cerr << message << '\n';
  return 1;
}

bool near(float left, float right, float tolerance = 1.0e-5F) {
  return std::abs(left - right) <= tolerance;
}

std::vector<ColorPixel> ramp(int width, int height) {
  std::vector<ColorPixel> pixels;
  pixels.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto red = static_cast<float>(x) / static_cast<float>(std::max(1, width - 1));
      const auto green = static_cast<float>(y) / static_cast<float>(std::max(1, height - 1));
      pixels.push_back({red, green, 0.5F * (red + green), 1.0F});
    }
  }
  return pixels;
}

bool same_rgb(const ColorPixel& left, const ColorPixel& right) {
  return near(left.red, right.red) && near(left.green, right.green) && near(left.blue, right.blue);
}

bool all_finite(const std::vector<ColorPixel>& pixels) {
  for (const auto& pixel : pixels)
    if (!std::isfinite(pixel.red) || !std::isfinite(pixel.green) || !std::isfinite(pixel.blue) ||
        !std::isfinite(pixel.alpha)) return false;
  return true;
}

bool tile_matches_full(std::string_view type, const ArtisticPhotoEffectParameters& parameters,
    const std::vector<ColorPixel>& source, int width, int height,
    double scale_x = 1.0, double scale_y = 1.0) {
  PhotoEffectRenderOptions full_options;
  full_options.frame_width = width;
  full_options.frame_height = height;
  full_options.scale_x = scale_x;
  full_options.scale_y = scale_y;
  PhotoEffectSupport support;
  if (!artistic_photo_effect_support(type, parameters, full_options, &support).ok()) return false;
  auto full = source;
  if (!apply_artistic_photo_effect(&full, width, height, type, parameters, full_options).ok()) return false;

  constexpr int roi_x = 11;
  constexpr int roi_y = 9;
  constexpr int roi_width = 10;
  constexpr int roi_height = 8;
  const auto tile_x = std::max(0, roi_x - support.x);
  const auto tile_y = std::max(0, roi_y - support.y);
  const auto tile_right = std::min(width, roi_x + roi_width + support.x);
  const auto tile_bottom = std::min(height, roi_y + roi_height + support.y);
  const auto tile_width = tile_right - tile_x;
  const auto tile_height = tile_bottom - tile_y;
  std::vector<ColorPixel> tile;
  tile.reserve(static_cast<std::size_t>(tile_width) * static_cast<std::size_t>(tile_height));
  for (int y = tile_y; y < tile_bottom; ++y)
    for (int x = tile_x; x < tile_right; ++x)
      tile.push_back(source[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
          static_cast<std::size_t>(x)]);
  PhotoEffectRenderOptions tile_options = full_options;
  tile_options.origin_x = tile_x;
  tile_options.origin_y = tile_y;
  if (!apply_artistic_photo_effect(&tile, tile_width, tile_height, type, parameters, tile_options).ok()) return false;
  for (int y = roi_y; y < roi_y + roi_height; ++y) {
    for (int x = roi_x; x < roi_x + roi_width; ++x) {
      const auto full_index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
          static_cast<std::size_t>(x);
      const auto tile_index = static_cast<std::size_t>(y - tile_y) * static_cast<std::size_t>(tile_width) +
          static_cast<std::size_t>(x - tile_x);
      const auto& a = full[full_index];
      const auto& b = tile[tile_index];
      if (!near(a.red, b.red) || !near(a.green, b.green) || !near(a.blue, b.blue) || !near(a.alpha, b.alpha))
        return false;
    }
  }
  return true;
}
}  // namespace

int main() {
  ArtisticPhotoEffectParameters parameters;
  parameters.gradient_map.stops = {{0.0, {0.0F, 0.0F, 1.0F}},
      {0.5, {1.0F, 0.0F, 0.0F}}, {1.0, {1.0F, 1.0F, 0.0F}}};
  std::vector<ColorPixel> pixels{{0.5F, 0.5F, 0.5F, 0.6F}};
  if (!apply_artistic_photo_effect(&pixels, 1, 1, "gradient_map", parameters, {}).ok() ||
      !near(pixels[0].red, 1.0F) || !near(pixels[0].green, 0.0F) || !near(pixels[0].blue, 0.0F) ||
      !near(pixels[0].alpha, 0.6F)) return fail("Custom multi-stop gradient mapping or alpha preservation failed.");
  pixels = {{0.0F, 1.0F, 0.0F, 1.0F}};
  PhotoEffectRenderOptions gamut_options;
  gamut_options.luminance_weights = {0.0, 1.0, 0.0};
  if (!apply_artistic_photo_effect(&pixels, 1, 1, "gradient_map", parameters, gamut_options).ok() ||
      !near(pixels[0].red, 1.0F) || !near(pixels[0].green, 1.0F) || !near(pixels[0].blue, 0.0F))
    return fail("Gradient map must use the working-gamut luminance coefficients.");
  pixels = {{0.0F, 1.0F, 0.0F, 1.0F}};
  gamut_options.luminance_weights = {1.1, -0.1, 0.0};
  if (!apply_artistic_photo_effect(&pixels, 1, 1, "gradient_map", parameters, gamut_options).ok() ||
      !near(pixels[0].red, 0.0F) || !near(pixels[0].green, 0.0F) || !near(pixels[0].blue, 1.0F))
    return fail("Signed wide-gamut Y coefficients must remain valid and drive gradient mapping.");
  gamut_options.luminance_weights = {0.0, 0.0, 0.0};
  if (apply_artistic_photo_effect(&pixels, 1, 1, "gradient_map", parameters, gamut_options).status !=
      PhotoEffectStatus::Invalid)
    return fail("Invalid luminance coefficients must be rejected.");

  parameters = {};
  pixels = {{0.75F, 0.75F, 0.75F, 1.0F}, {0.25F, 0.25F, 0.25F, 1.0F}};
  if (!apply_artistic_photo_effect(&pixels, 2, 1, "solarization", parameters, {}).ok() ||
      !near(pixels[0].red, 0.25F) || !near(pixels[1].red, 0.25F))
    return fail("Threshold tone reflection failed.");

  pixels.assign(4U, ColorPixel{0.0F, 0.0F, 0.0F, 1.0F});
  if (!apply_artistic_photo_effect(&pixels, 2, 2, "color_halftone", parameters, {}).ok())
    return fail("Color halftone failed.");
  for (const auto& pixel : pixels)
    if (pixel.red != 0.0F || pixel.green != 0.0F || pixel.blue != 0.0F)
      return fail("Black halftone should be filled with dots.");
  pixels.assign(4U, ColorPixel{1.0F, 1.0F, 1.0F, 1.0F});
  if (!apply_artistic_photo_effect(&pixels, 2, 2, "color_halftone", parameters, {}).ok()) return 1;
  for (const auto& pixel : pixels)
    if (pixel.red != 1.0F || pixel.green != 1.0F || pixel.blue != 1.0F)
      return fail("White halftone should contain no dots.");

  parameters = {};
  parameters.dither.levels = 2;
  parameters.dither.matrix_size = 2;
  pixels.assign(4U, ColorPixel{0.5F, 0.5F, 0.5F, 1.0F});
  if (!apply_artistic_photo_effect(&pixels, 2, 2, "dither", parameters, {}).ok() ||
      pixels[0].red != 0.0F || pixels[1].red != 1.0F || pixels[2].red != 1.0F || pixels[3].red != 0.0F)
    return fail("Bayer ordered dither does not match the 2x2 reference matrix.");
  parameters.dither.mode = ArtisticDitherMode::FloydSteinberg;
  pixels.assign(3U, ColorPixel{0.4F, 0.4F, 0.4F, 1.0F});
  if (!artistic_photo_effect_requires_full_frame("dither", parameters) ||
      !apply_artistic_photo_effect(&pixels, 3, 1, "dither", parameters, {}).ok() ||
      pixels[0].red != 0.0F || pixels[1].red != 1.0F || pixels[2].red != 0.0F)
    return fail("Floyd-Steinberg reference scan failed.");
  PhotoEffectRenderOptions causal_tile;
  causal_tile.origin_x = 1;
  PhotoEffectSupport support;
  if (artistic_photo_effect_support("dither", parameters, causal_tile, &support).status !=
      PhotoEffectStatus::Unsupported)
    return fail("Causal error diffusion must reject a non-origin tile.");

  parameters = {};
  auto hatch_input = ramp(32, 16);
  auto hatch = hatch_input;
  parameters.crosshatch.thresholds = {0.9, 0.7, 0.5, 0.3};
  if (!apply_artistic_photo_effect(&hatch, 32, 16, "crosshatch", parameters, {}).ok() ||
      !all_finite(hatch) || hatch == hatch_input)
    return fail("Crosshatch should add deterministic line screens to tonal input.");

  std::vector<ColorPixel> flat(49U, ColorPixel{0.4F, 0.6F, 0.2F, 1.0F});
  auto outline = flat;
  if (!apply_artistic_photo_effect(&outline, 7, 7, "ink_outline", parameters, {}).ok() || outline != flat)
    return fail("Ink outline must leave a constant field unchanged.");
  outline = ramp(7, 7);
  const auto unoutlined = outline;
  if (!apply_artistic_photo_effect(&outline, 7, 7, "ink_outline", parameters, {}).ok() || outline == unoutlined)
    return fail("Ink outline should respond to an image edge.");

  parameters.watercolor.pigment_granulation = 0.0;
  parameters.watercolor.edge_darkening = 0.0;
  auto wash = flat;
  if (!apply_artistic_photo_effect(&wash, 7, 7, "watercolor", parameters, {}).ok() || wash != flat)
    return fail("Watercolor bilateral wash should preserve a uniform field when texture is off.");
  wash = ramp(7, 7);
  wash[0].alpha = 0.0F;
  if (!apply_artistic_photo_effect(&wash, 7, 7, "watercolor", parameters, {}).ok() ||
      !near(wash[0].alpha, 0.0F) || !all_finite(wash))
    return fail("Watercolor must preserve alpha and finite colors.");

  parameters.oil_paint = {};
  auto oil = flat;
  if (!apply_artistic_photo_effect(&oil, 7, 7, "oil_paint", parameters, {}).ok() || oil != flat)
    return fail("Oil-paint dominant-bin filter should preserve a constant field.");
  parameters = {};
  parameters.oil_paint.radius = 1;
  parameters.oil_paint.levels = 4;
  PhotoEffectRenderOptions oil_luma;
  oil_luma.luminance_weights = {0.0, 1.0, 0.0};
  std::vector<ColorPixel> oil_reference{{1.0F, 0.1F, 0.0F, 1.0F},
      {0.0F, 0.2F, 1.0F, 0.5F}, {0.0F, 0.3F, 0.0F, 1.0F}};
  if (!apply_artistic_photo_effect(&oil_reference, 3, 1, "oil_paint", parameters, oil_luma).ok() ||
      !near(oil_reference[1].red, 2.0F / 3.0F) || !near(oil_reference[1].green, 0.2F / 1.5F) ||
      !near(oil_reference[1].blue, 1.0F / 3.0F) || !near(oil_reference[1].alpha, 0.5F))
    return fail("Oil paint must select the working-gamut modal bin and alpha-weight its RGB mean.");

  parameters = {};
  parameters.pixel_mosaic.cell_size = 4.0;
  auto square = ramp(8, 4);
  if (!apply_artistic_photo_effect(&square, 8, 4, "pixel_mosaic", parameters, {}).ok()) return 1;
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      const auto& cell = square[static_cast<std::size_t>(y) * 8U + static_cast<std::size_t>(x)];
      if (!same_rgb(cell, square[2U * 8U + 2U])) return fail("Square mosaic cell must hold its sampled center color.");
    }
  }
  auto transparent_mosaic = ramp(4, 4);
  transparent_mosaic[2U * 4U + 2U] = {1.0F, 0.5F, 0.25F, 0.0F};
  parameters.pixel_mosaic.cell_size = 4.0;
  if (!apply_artistic_photo_effect(&transparent_mosaic, 4, 4, "pixel_mosaic", parameters, {}).ok() ||
      transparent_mosaic[0].alpha != 0.0F || transparent_mosaic[0].red != 0.0F ||
      transparent_mosaic[0].green != 0.0F || transparent_mosaic[0].blue != 0.0F)
    return fail("A transparent mosaic sample must not carry hidden RGB into the output cell.");
  auto hex = ramp(16, 16);
  parameters.pixel_mosaic.shape = ArtisticMosaicShape::Hexagonal;
  if (!apply_artistic_photo_effect(&hex, 16, 16, "pixel_mosaic", parameters, {}).ok() || hex == ramp(16, 16))
    return fail("Hexagonal mosaic must use a distinct tessellation.");
  auto irregular = ramp(16, 16);
  parameters.pixel_mosaic.shape = ArtisticMosaicShape::IrregularVoronoi;
  parameters.pixel_mosaic.jitter = 0.3;
  if (!apply_artistic_photo_effect(&irregular, 16, 16, "pixel_mosaic", parameters, {}).ok() ||
      irregular == hex)
    return fail("Irregular mosaic must use seeded jittered Voronoi cells.");

  parameters = {};
  parameters.rgb_glitch.shift_x = 5.0;
  parameters.rgb_glitch.shift_y = 1.0;
  parameters.rgb_glitch.band_height = 4.0;
  parameters.rgb_glitch.band_period = 8.0;
  auto glitch_source = ramp(32, 16);
  auto glitch_a = glitch_source;
  auto glitch_b = glitch_source;
  if (!apply_artistic_photo_effect(&glitch_a, 32, 16, "rgb_glitch", parameters, {}).ok() ||
      !apply_artistic_photo_effect(&glitch_b, 32, 16, "rgb_glitch", parameters, {}).ok() ||
      glitch_a != glitch_b || glitch_a == glitch_source || !all_finite(glitch_a))
    return fail("RGB glitch should be deterministic and split channel registration.");

  parameters = {};
  parameters.displacement_map.asset_ref = std::string(64U, 'a');
  parameters.displacement_map.width = 3;
  parameters.displacement_map.height = 1;
  auto map = std::make_shared<std::vector<float>>(12U, 0.5F);
  (*map)[0] = 0.75F;
  (*map)[3] = 1.0F;
  (*map)[4] = 0.75F;
  (*map)[7] = 1.0F;
  (*map)[8] = 0.75F;
  (*map)[11] = 1.0F;
  parameters.displacement_map.rgba = map;
  PhotoEffectRenderOptions map_options;
  map_options.frame_width = 3;
  map_options.frame_height = 1;
  parameters.displacement_map.maximum_displacement = 1.0;
  std::vector<ColorPixel> displacement_input{{1.0F, 0.0F, 0.0F, 0.0F},
      {0.0F, 0.0F, 1.0F, 1.0F}, {0.0F, 1.0F, 0.0F, 1.0F}};
  auto displaced = displacement_input;
  if (!apply_artistic_photo_effect(&displaced, 3, 1, "displacement_map", parameters, map_options).ok() ||
      !near(displaced[0].alpha, 0.5F) || !near(displaced[0].red, 0.0F) ||
      !near(displaced[0].blue, 1.0F))
    return fail("Displacement must use the supplied map and interpolate premultiplied alpha.");
  parameters.displacement_map.width = 2;
  auto alpha_map = std::make_shared<std::vector<float>>(8U, 0.5F);
  (*alpha_map)[0] = 1.0F;
  (*alpha_map)[3] = 0.0F;
  (*alpha_map)[7] = 1.0F;
  parameters.displacement_map.rgba = alpha_map;
  parameters.displacement_map.maximum_displacement = 12.0;
  map_options.frame_width = 3;
  map_options.frame_height = 1;
  auto alpha_interpolated = ramp(3, 1);
  const auto alpha_interpolated_source = alpha_interpolated;
  if (!apply_artistic_photo_effect(&alpha_interpolated, 3, 1, "displacement_map", parameters,
          map_options).ok() || alpha_interpolated != alpha_interpolated_source)
    return fail("Bilinear map sampling must alpha-neutralize texels before interpolation.");

  parameters.displacement_map.width = 3;
  parameters.displacement_map.height = 1;
  auto neutral_map = std::make_shared<std::vector<float>>(12U, 0.5F);
  for (std::size_t i = 3U; i < neutral_map->size(); i += 4U) (*neutral_map)[i] = 0.0F;
  (*neutral_map)[0] = 1.0F;
  parameters.displacement_map.rgba = neutral_map;
  displaced = displacement_input;
  if (!apply_artistic_photo_effect(&displaced, 3, 1, "displacement_map", parameters, map_options).ok() ||
      displaced != displacement_input)
    return fail("A fully transparent displacement-map pixel must be neutral.");
  parameters.displacement_map.rgba.reset();
  PhotoEffectSupport map_support;
  if (!artistic_photo_effect_support("displacement_map", parameters, map_options, &map_support).ok() ||
      apply_artistic_photo_effect(&displaced, 3, 1, "displacement_map", parameters, map_options).status !=
          PhotoEffectStatus::Invalid)
    return fail("Codec support may validate an asset reference, while rendering requires its resolved raster.");
  parameters.displacement_map = {};
  if (artistic_photo_effect_support("displacement_map", parameters, map_options, &map_support).status !=
      PhotoEffectStatus::Identity)
    return fail("An unconfigured displacement map should be an identity stage.");
  parameters.displacement_map.asset_ref = std::string(64U, 'A');
  parameters.displacement_map.width = 3;
  parameters.displacement_map.height = 1;
  if (artistic_photo_effect_support("displacement_map", parameters, map_options, &map_support).status !=
          PhotoEffectStatus::Invalid || !serialize_artistic_photo_effect_parameters("displacement_map", parameters).empty())
    return fail("Displacement asset identity must be a lowercase SHA-256 of the canonical PNG.");
  parameters.displacement_map.asset_ref = std::string(64U, 'b');
  auto invalid_map = std::make_shared<std::vector<float>>(12U, 0.5F);
  (*invalid_map)[0] = std::numeric_limits<float>::quiet_NaN();
  parameters.displacement_map.rgba = invalid_map;
  displaced = displacement_input;
  if (apply_artistic_photo_effect(&displaced, 3, 1, "displacement_map", parameters, map_options).status !=
          PhotoEffectStatus::Invalid || displaced != displacement_input)
    return fail("Invalid sampled map data must fail without publishing partial output.");

  parameters = {};
  parameters.displacement_map.asset_ref = std::string(64U, 'e');
  parameters.displacement_map.width = 5;
  parameters.displacement_map.height = 3;
  parameters.displacement_map.maximum_displacement = 2.0;
  auto small_map = std::make_shared<std::vector<float>>(5U * 3U * 4U, 0.5F);
  for (int y = 0; y < 3; ++y) {
    for (int x = 0; x < 5; ++x) {
      const auto offset = (static_cast<std::size_t>(y) * 5U + static_cast<std::size_t>(x)) * 4U;
      (*small_map)[offset] = static_cast<float>(x) / 4.0F;
      (*small_map)[offset + 1U] = static_cast<float>(y) / 2.0F;
      (*small_map)[offset + 3U] = 1.0F;
    }
  }
  parameters.displacement_map.rgba = small_map;
  auto large_source = ramp(43, 29);
  const auto large_source_copy = large_source;
  PhotoEffectRenderOptions arbitrary_map_options;
  arbitrary_map_options.frame_width = 43;
  arbitrary_map_options.frame_height = 29;
  if (!apply_artistic_photo_effect(&large_source, 43, 29, "displacement_map", parameters,
          arbitrary_map_options).ok() || large_source == large_source_copy ||
      large_source[14U * 43U + 21U] != large_source_copy[14U * 43U + 21U] ||
      !tile_matches_full("displacement_map", parameters, large_source_copy, 43, 29))
    return fail("A 5x3 map must bilinearly map over a 43x29 frame with exact tile agreement.");

  const std::array<std::string_view, 11> types{{"gradient_map", "solarization", "color_halftone", "dither",
      "crosshatch", "ink_outline", "watercolor", "oil_paint", "pixel_mosaic", "rgb_glitch", "displacement_map"}};
  for (const auto type : types) {
    ArtisticPhotoEffectParameters original;
    if (type == "gradient_map") original.gradient_map.stops = {{0.0, {0.0F, 0.0F, 0.2F}},
        {0.32, {0.1F, 0.3F, 0.8F}}, {1.0, {1.0F, 0.9F, 0.4F}}};
    if (type == "displacement_map") {
      original.displacement_map.asset_ref = std::string(64U, 'c');
      original.displacement_map.width = 10;
      original.displacement_map.height = 9;
    }
    const auto payload = serialize_artistic_photo_effect_parameters(type, original);
    ArtisticPhotoEffectParameters decoded;
    std::string error;
    if (payload.empty() || !deserialize_artistic_photo_effect_parameters(type, payload, &decoded, &error) ||
      !(decoded == original)) {
      std::cerr << "Codec type: " << type << " error: " << error << " payload: " << payload << "\n";
      return fail("Artistic effect parameter codec failed for a registered operator.");
    }
    auto values = artistic_photo_effect_values(type, original);
    if (!set_artistic_photo_effect_values(type, values, &decoded, &error))
      return fail("Numeric artistic parameter bridge rejected its own values.");
    if (!values.empty()) {
      auto invalid = values;
      invalid.begin()->second = std::numeric_limits<double>::quiet_NaN();
      const auto before = decoded;
      if (set_artistic_photo_effect_values(type, invalid, &decoded, &error) || !(decoded == before))
        return fail("Invalid artistic parameter updates must be strict and transactional.");
    }
  }
  auto malformed = std::string("artistic-photo-effect:v1 1 \"levels\" 2.5 0 \"\" 0");
  ArtisticPhotoEffectParameters decoded;
  if (deserialize_artistic_photo_effect_parameters("dither", malformed, &decoded))
    return fail("The artistic parameter codec accepted a fractional integer field.");

  auto tile_source = ramp(34, 30);
  for (std::size_t i = 0; i < tile_source.size(); ++i)
    tile_source[i].alpha = static_cast<float>(i % 5U) * 0.25F;
  parameters = {};
  if (!tile_matches_full("dither", parameters, tile_source, 34, 30))
    return fail("Ordered dither pattern phase must remain stable in padded tiles.");
  parameters.crosshatch = {};
  if (!tile_matches_full("crosshatch", parameters, tile_source, 34, 30))
    return fail("Crosshatch phase must remain stable in padded tiles.");
  parameters = {};
  parameters.pixel_mosaic.cell_size = 6.0;
  parameters.pixel_mosaic.shape = ArtisticMosaicShape::IrregularVoronoi;
  if (!tile_matches_full("pixel_mosaic", parameters, tile_source, 34, 30))
    return fail("Jittered Voronoi sampling must remain stable in padded tiles.");
  parameters = {};
  parameters.watercolor.radius = 2;
  if (!tile_matches_full("watercolor", parameters, tile_source, 34, 30, 1.25, 0.75))
    return fail("Watercolor neighborhood must remain stable in padded tiles.");
  parameters = {};
  parameters.oil_paint.radius = 2;
  if (!tile_matches_full("oil_paint", parameters, tile_source, 34, 30, 1.25, 0.75))
    return fail("Oil-paint neighborhood must remain stable in padded tiles.");
  parameters = {};
  parameters.rgb_glitch.shift_x = 3.0;
  parameters.rgb_glitch.shift_y = 1.0;
  if (!tile_matches_full("rgb_glitch", parameters, tile_source, 34, 30, 1.25, 0.75))
    return fail("RGB glitch bands and offsets must remain stable in padded tiles.");

  parameters = {};
  parameters.displacement_map.asset_ref = std::string(64U, 'd');
  parameters.displacement_map.width = 34;
  parameters.displacement_map.height = 30;
  parameters.displacement_map.maximum_displacement = 2.0;
  auto tile_map = std::make_shared<std::vector<float>>(34U * 30U * 4U, 0.5F);
  for (int y = 0; y < 30; ++y) {
    for (int x = 0; x < 34; ++x) {
      const auto offset = (static_cast<std::size_t>(y) * 34U + static_cast<std::size_t>(x)) * 4U;
      (*tile_map)[offset] = static_cast<float>(x) / 33.0F;
      (*tile_map)[offset + 1U] = static_cast<float>(y) / 29.0F;
      (*tile_map)[offset + 3U] = 1.0F;
    }
  }
  parameters.displacement_map.rgba = tile_map;
  if (!tile_matches_full("displacement_map", parameters, tile_source, 34, 30))
    return fail("Map-driven displacement must remain stable in padded tiles.");

  std::atomic_bool cancel{true};
  PhotoEffectRenderOptions cancelled_options;
  cancelled_options.cancellation.cancellation_requested = &cancel;
  auto cancel_input = ramp(8, 8);
  const auto unchanged = cancel_input;
  if (apply_artistic_photo_effect(&cancel_input, 8, 8, "gradient_map", {}, cancelled_options).status !=
      PhotoEffectStatus::Cancelled || cancel_input != unchanged)
    return fail("Cancellation must not publish partially processed output.");
  PhotoEffectRenderOptions tiny_scratch;
  tiny_scratch.max_scratch_bytes = 0U;
  auto scratch_input = ramp(8, 8);
  const auto scratch_unchanged = scratch_input;
  if (apply_artistic_photo_effect(&scratch_input, 8, 8, "gradient_map", {}, tiny_scratch).status !=
      PhotoEffectStatus::ResourceLimit || scratch_input != scratch_unchanged)
    return fail("Scratch admission must fail transactionally.");
  const auto output_bytes = scratch_input.size() * sizeof(ColorPixel);
  PhotoEffectRenderOptions one_buffer_budget;
  one_buffer_budget.max_scratch_bytes = output_bytes;
  auto one_buffer_input = scratch_unchanged;
  if (!apply_artistic_photo_effect(&one_buffer_input, 8, 8, "gradient_map", {}, one_buffer_budget).ok())
    return fail("A full-frame color operator should fit with exactly one owned output raster.");
  PhotoEffectRenderOptions below_one_buffer_budget;
  below_one_buffer_budget.max_scratch_bytes = output_bytes - 1U;
  auto below_budget_input = scratch_unchanged;
  if (apply_artistic_photo_effect(&below_budget_input, 8, 8, "gradient_map", {},
          below_one_buffer_budget).status != PhotoEffectStatus::ResourceLimit ||
      below_budget_input != scratch_unchanged)
    return fail("One byte below the output-raster budget must fail without changing the input.");

  auto diffusion_input = ramp(3, 2);
  const auto diffusion_unchanged = diffusion_input;
  ArtisticPhotoEffectParameters diffusion_parameters;
  diffusion_parameters.dither.mode = ArtisticDitherMode::FloydSteinberg;
  const auto diffusion_rows = (3U + 2U) * 2U * 3U * sizeof(double);
  PhotoEffectRenderOptions diffusion_budget;
  diffusion_budget.max_scratch_bytes = diffusion_input.size() * sizeof(ColorPixel) + diffusion_rows;
  if (!apply_artistic_photo_effect(&diffusion_input, 3, 2, "dither", diffusion_parameters,
          diffusion_budget).ok())
    return fail("Full-frame error diffusion should fit with one output raster and its two scratch rows.");
  diffusion_input = diffusion_unchanged;
  diffusion_budget.max_scratch_bytes -= 1U;
  if (apply_artistic_photo_effect(&diffusion_input, 3, 2, "dither", diffusion_parameters,
          diffusion_budget).status != PhotoEffectStatus::ResourceLimit ||
      diffusion_input != diffusion_unchanged)
    return fail("Error diffusion must account for its row scratch and preserve input on budget failure.");

  std::cout << "11 artistic operators, deterministic patterns, map alpha, codec and failure atomicity passed\n";
  return 0;
}
