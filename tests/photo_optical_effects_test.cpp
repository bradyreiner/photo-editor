#include "photo_editor/pipeline/photo_effects.h"
#include "photo_editor/pipeline/photo_optical_effects.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

using namespace photo_editor::pipeline;

#define CHECK(condition, message) do { \
  if (!(condition)) { std::cerr << message << '\n'; return 1; } \
} while (false)

int codec_contracts() {
  const std::array<std::string_view, 9> types{
      "halation", "anamorphic_streaks", "starburst", "lens_bokeh",
      "tilt_shift", "zoom_blur", "spin_blur", "surface_blur", "high_pass"};
  for (const auto type : types) {
    OpticalPhotoEffectParameters parameters;
    const auto encoded = serialize_optical_photo_effect_parameters(type, parameters);
    CHECK(!encoded.empty(), "Default optical parameters must serialize");
    OpticalPhotoEffectParameters decoded;
    std::string error;
    CHECK(deserialize_optical_photo_effect_parameters(type, encoded, &decoded, &error),
        "Optical parameter payload must deserialize");
    CHECK(decoded == parameters, "Optical parameter round trip must be exact");
    CHECK(!optical_photo_effect_fields(type).empty(), "Every optical effect needs controls");
  }

  OpticalPhotoEffectParameters parameters;
  CHECK(set_optical_photo_effect_values("starburst", {{"ray_count", 8.0}},
      &parameters), "Partial optical parameter update must work");
  CHECK(parameters.starburst.ray_count == 8, "Discrete values must update exactly");
  const auto saved = parameters;
  std::string error;
  CHECK(!set_optical_photo_effect_values("starburst", {{"ray_count", 8.5}},
      &parameters, &error), "Non-integral ray counts must be rejected");
  CHECK(parameters == saved, "Rejected parameter updates must be transactional");
  CHECK(!deserialize_optical_photo_effect_parameters("high_pass",
      "optical-photo-effect:v1 high_pass 2 2 1 trailing", &parameters, &error),
      "Strict optical codec must reject trailing bytes");
  CHECK(!deserialize_optical_photo_effect_parameters("high_pass",
      "optical-photo-effect:v1 high_pass 2 2 nan", &parameters, &error),
      "Strict optical codec must reject non-finite numbers");
  return 0;
}

int analytic_and_numeric_references() {
  // A constant raster is invariant under bilateral surface smoothing, including
  // fractional alpha; a flat image has no range edge to preserve differently.
  std::vector<ColorPixel> flat(9U * 7U, {0.2F, 0.4F, 0.8F, 0.25F});
  CHECK(apply_optical_photo_effect(&flat, 9, 7, "surface_blur", {}, {}).ok(),
      "Surface blur should accept a valid constant raster");
  for (const auto& pixel : flat) {
    CHECK(std::abs(pixel.red - 0.2F) < 1e-6F &&
        std::abs(pixel.green - 0.4F) < 1e-6F &&
        std::abs(pixel.blue - 0.8F) < 1e-6F &&
        std::abs(pixel.alpha - 0.25F) < 1e-6F,
        "Surface blur must preserve a constant premultiplied-alpha field");
  }

  // A normalized high-pass kernel has zero response to constant detail, so its
  // conventional neutral output is exactly linear 0.5 in each RGB channel.
  std::vector<ColorPixel> constant(11U * 5U, {0.35F, 0.5F, 1.4F, 0.3F});
  CHECK(apply_optical_photo_effect(&constant, 11, 5, "high_pass", {}, {}).ok(),
      "High-pass should accept a valid constant raster");
  for (const auto& pixel : constant) {
    CHECK(std::abs(pixel.red - 0.5F) < 1e-6F &&
        std::abs(pixel.green - 0.5F) < 1e-6F &&
        std::abs(pixel.blue - 0.5F) < 1e-6F &&
        std::abs(pixel.alpha - 0.3F) < 1e-6F,
        "High-pass constant-field reference must be neutral gray with alpha preserved");
  }

  // For an opaque single-pixel white impulse, the Gaussian halation halo one
  // pixel away is the product of the normalized separable sigma=1 horizontal
  // and vertical taps, times the configured 0.5 emission strength.
  constexpr int width = 9;
  constexpr int height = 9;
  std::vector<ColorPixel> impulse(static_cast<std::size_t>(width * height),
      {0.0F, 0.0F, 0.0F, 1.0F});
  impulse[static_cast<std::size_t>(4 * width + 4)] = {4.0F, 4.0F, 4.0F, 1.0F};
  OpticalPhotoEffectParameters parameters;
  parameters.halation.radius = 1.0;
  parameters.halation.threshold = 0.0;
  parameters.halation.knee = 0.0;
  parameters.halation.strength = 0.5;
  parameters.halation.warmth = 0.0;
  CHECK(apply_optical_photo_effect(&impulse, width, height, "halation", parameters, {}).ok(),
      "Halation should accept a valid impulse raster");
  const auto norm = 1.0 + 2.0 * (std::exp(-0.5) + std::exp(-2.0) + std::exp(-4.5));
  const auto expected_neighbor = 2.0 * std::exp(-0.5) / (norm * norm);
  CHECK(std::abs(impulse[static_cast<std::size_t>(4 * width + 5)].red -
      expected_neighbor) < 2e-5,
      "Halation impulse response must match the normalized Gaussian reference value");
  CHECK(impulse[static_cast<std::size_t>(4 * width + 5)].alpha == 1.0F,
      "Optical glow must preserve opaque alpha");
  return 0;
}

int optical_geometry_and_admission() {
  constexpr int size = 25;
  std::vector<ColorPixel> point(static_cast<std::size_t>(size * size),
      {0.0F, 0.0F, 0.0F, 1.0F});
  const auto center = size / 2;
  point[static_cast<std::size_t>(center * size + center)] = {8.0F, 8.0F, 8.0F, 1.0F};

  OpticalPhotoEffectParameters bokeh;
  bokeh.lens_bokeh.radius = 3.0;
  bokeh.lens_bokeh.aperture_blades = 4;
  bokeh.lens_bokeh.rotation_degrees = 0.0;
  bokeh.lens_bokeh.integration_samples = 256;
  bokeh.lens_bokeh.threshold = 0.0;
  bokeh.lens_bokeh.knee = 0.1;
  const auto bokeh_result = [&] {
    auto result = point;
    if (!apply_optical_photo_effect(&result, size, size, "lens_bokeh", bokeh, {}).ok())
      return std::vector<ColorPixel>{};
    return result;
  }();
  CHECK(!bokeh_result.empty(), "Lens bokeh should integrate its aperture kernel");
  const auto cardinal = bokeh_result[static_cast<std::size_t>(center * size + center + 2)].red;
  const auto corner = bokeh_result[static_cast<std::size_t>((center + 2) * size + center + 2)].red;
  CHECK(cardinal > corner + 1e-5F,
      "A four-blade aperture must retain its diamond-shaped point-spread footprint");
  CHECK(bokeh_result[static_cast<std::size_t>(center * size + center + 2)].alpha == 1.0F,
      "Aperture defocus must preserve opaque alpha");

  // Highlight boost is optional, but the aperture itself always defocuses.
  // A transparent impulse makes conservation measurable in premultiplied RGB.
  OpticalPhotoEffectParameters pure_defocus;
  pure_defocus.lens_bokeh = {3.0, 6, 0.0, 128, 16.0, 0.0, 0.0};
  std::vector<ColorPixel> transparent_impulse(static_cast<std::size_t>(size * size),
      {0.0F, 0.0F, 0.0F, 0.0F});
  transparent_impulse[static_cast<std::size_t>(center * size + center)] =
      {1.0F, 1.0F, 1.0F, 1.0F};
  PhotoEffectSupport bokeh_support;
  const auto support_result = optical_photo_effect_support(
      "lens_bokeh", pure_defocus, {}, &bokeh_support);
  CHECK(support_result.status == PhotoEffectStatus::Applied && bokeh_support.x >= 4,
      "Zero highlight boost must retain aperture support");
  CHECK(apply_optical_photo_effect(&transparent_impulse, size, size,
      "lens_bokeh", pure_defocus, {}).status == PhotoEffectStatus::Applied,
      "Zero highlight boost must still apply aperture defocus");
  double defocused_energy = 0.0;
  for (const auto& pixel : transparent_impulse)
    defocused_energy += static_cast<double>(pixel.red) * pixel.alpha;
  CHECK(std::abs(defocused_energy - 1.0) < 2e-5,
      "Normalized polygon defocus must conserve a centered impulse's premultiplied energy");
  CHECK(transparent_impulse[static_cast<std::size_t>(center * size + center)].alpha < 1.0F &&
      transparent_impulse[static_cast<std::size_t>(center * size + center + 1)].alpha > 0.0F,
      "Zero-boost bokeh must spread a transparent impulse through the aperture kernel");

  // A low-level edge below the highlight threshold is still optically defocused.
  pure_defocus.lens_bokeh.strength = 0.6;
  std::vector<ColorPixel> dim_edge(static_cast<std::size_t>(size * size),
      {0.0F, 0.0F, 0.0F, 1.0F});
  dim_edge[static_cast<std::size_t>(center * size + center)] =
      {0.2F, 0.2F, 0.2F, 1.0F};
  CHECK(apply_optical_photo_effect(&dim_edge, size, size,
      "lens_bokeh", pure_defocus, {}).status == PhotoEffectStatus::Applied,
      "Lens bokeh must blur dim detail below the highlight threshold");
  CHECK(dim_edge[static_cast<std::size_t>(center * size + center)].red < 0.2F &&
      dim_edge[static_cast<std::size_t>(center * size + center + 1)].red > 0.0F,
      "Dim edges must defocus even when highlight extraction contributes nothing");

  // A two-axis starburst sends energy down horizontal and vertical rays. The
  // diagonal sample at the same radius stays below the narrow ray profile.
  OpticalPhotoEffectParameters star;
  star.starburst.radius = 8.0;
  star.starburst.width = 0.5;
  star.starburst.ray_count = 2;
  star.starburst.angle_degrees = 0.0;
  star.starburst.threshold = 0.0;
  star.starburst.knee = 0.0;
  star.starburst.warmth = 0.0;
  auto burst = point;
  CHECK(apply_optical_photo_effect(&burst, size, size, "starburst", star, {}).ok(),
      "Starburst should apply a radial line-spread kernel");
  const auto horizontal_ray = burst[static_cast<std::size_t>(center * size + center + 5)].red;
  const auto diagonal = burst[static_cast<std::size_t>((center + 3) * size + center + 4)].red;
  CHECK(horizontal_ray > diagonal + 1e-4F,
      "Starburst ray profile must be directional rather than an isotropic glow");

  OpticalPhotoEffectParameters zoom;
  zoom.zoom_blur.radius = 4.0;
  PhotoEffectRenderOptions full_frame;
  full_frame.frame_width = size;
  full_frame.frame_height = size;
  PhotoEffectSupport support;
  CHECK(optical_photo_effect_requires_full_frame("zoom_blur", zoom),
      "Zoom blur must advertise whole-frame admission");
  CHECK(optical_photo_effect_support("zoom_blur", zoom, full_frame, &support).ok() &&
      support.x == size && support.y == size,
      "Whole-frame support must cover the complete raster");
  std::vector<ColorPixel> tile(static_cast<std::size_t>((size - 1) * size));
  for (int y = 0; y < size; ++y) {
    std::copy_n(point.begin() + static_cast<std::ptrdiff_t>(y * size), size - 1,
        tile.begin() + static_cast<std::ptrdiff_t>(y * (size - 1)));
  }
  const auto unchanged = tile;
  auto tiled_options = full_frame;
  tiled_options.origin_x = 1;
  CHECK(apply_optical_photo_effect(&tile, size - 1, size, "zoom_blur", zoom,
      tiled_options).status == PhotoEffectStatus::Unsupported && tile == unchanged,
      "Zoom blur must reject a tile rather than silently approximating its global mapping");
  return 0;
}

int working_space_luminance() {
  constexpr int width = 9;
  constexpr int height = 9;
  constexpr std::size_t impulse_index = 4U * width + 4U;
  constexpr std::size_t neighbor_index = 4U * width + 5U;
  std::vector<ColorPixel> source(static_cast<std::size_t>(width * height),
      {0.0F, 0.0F, 0.0F, 1.0F});
  source[impulse_index] = {0.0F, 0.0F, 4.0F, 1.0F};
  OpticalPhotoEffectParameters halation;
  halation.halation.radius = 1.0;
  halation.halation.threshold = 0.0001;
  halation.halation.knee = 0.0;
  halation.halation.strength = 1.0;
  halation.halation.warmth = 0.0;

  PhotoEffectRenderOptions ap0;
  ap0.luminance_weights = {0.3439664498, 0.7281660966, -0.0721325464};
  auto ap0_pixels = source;
  CHECK(apply_optical_photo_effect(&ap0_pixels, width, height, "halation",
      halation, ap0).ok(), "AP0's signed luminance coefficients must be accepted");
  CHECK(ap0_pixels[neighbor_index].blue == 0.0F,
      "A negative AP0 blue Y coefficient must not create a blue highlight halo");

  PhotoEffectRenderOptions prophoto;
  prophoto.luminance_weights = {0.2880402, 0.7118741, 0.0000857};
  auto prophoto_pixels = source;
  CHECK(apply_optical_photo_effect(&prophoto_pixels, width, height, "halation",
      halation, prophoto).ok(), "ProPhoto luminance coefficients must be accepted");
  CHECK(prophoto_pixels[neighbor_index].blue > 1.0e-5F,
      "ProPhoto's positive blue Y coefficient must participate in highlight extraction");

  auto invalid = ap0;
  invalid.luminance_weights = {2.0, 2.0, 2.0};
  CHECK(apply_optical_photo_effect(&ap0_pixels, width, height, "halation",
      halation, invalid).status == PhotoEffectStatus::Invalid,
      "Luminance coefficients must be a normalized working-space Y row");
  return 0;
}

int transactional_contracts_and_smoke() {
  constexpr int width = 12;
  constexpr int height = 10;
  std::vector<ColorPixel> source;
  source.reserve(static_cast<std::size_t>(width * height));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto value = (x > width / 2) ? 2.0F : 0.1F;
      source.push_back({value, value * 0.7F, value * 0.4F,
          static_cast<float>((x + y) % 4 + 1) / 4.0F});
    }
  }
  for (const auto type : {"halation", "anamorphic_streaks", "starburst",
      "lens_bokeh", "tilt_shift", "zoom_blur", "spin_blur", "surface_blur",
      "high_pass"}) {
    auto image = source;
    const auto result = apply_optical_photo_effect(&image, width, height, type, {}, {});
    CHECK(result.ok(), "Every registered optical operator must render its default parameters");
    CHECK(image.size() == source.size(), "Optical output dimensions must remain stable");
    for (const auto& p : image) {
      CHECK(std::isfinite(p.red) && std::isfinite(p.green) &&
          std::isfinite(p.blue) && p.alpha >= 0.0F && p.alpha <= 1.0F,
          "Optical output must stay finite with unit alpha");
    }
  }

  auto limited = source;
  PhotoEffectRenderOptions options;
  options.max_scratch_bytes = 1;
  const auto rejected = apply_optical_photo_effect(&limited, width, height,
      "halation", {}, options);
  CHECK(rejected.status == PhotoEffectStatus::ResourceLimit && limited == source,
      "Scratch rejection must preserve the input raster");

  auto cancelled = source;
  std::atomic_bool stop{true};
  options.max_scratch_bytes = 1024U * 1024U;
  options.cancellation.cancellation_requested = &stop;
  const auto stopped = apply_optical_photo_effect(&cancelled, width, height,
      "surface_blur", {}, options);
  CHECK(stopped.status == PhotoEffectStatus::Cancelled && cancelled == source,
      "Cancellation must preserve the input raster");
  return 0;
}

int main() {
  if (codec_contracts() != 0 || analytic_and_numeric_references() != 0 ||
      optical_geometry_and_admission() != 0 || working_space_luminance() != 0 ||
      transactional_contracts_and_smoke() != 0)
    return 1;
  std::cout << "Optical effect CPU models, aperture integration, alpha, support and codecs passed\n";
  return 0;
}
