#include "photo_editor/pipeline/photo_optical_effects.h"

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/cpu_work_scheduler.h"
#include "photo_editor/pipeline/photo_effects.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <numbers>
#include <new>
#include <sstream>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

namespace photo_editor::pipeline {
namespace {

constexpr double kMinimumScale = 1.0 / 4096.0;
constexpr double kMaximumScale = 4096.0;
constexpr double kMaximumSupport = 4096.0;
constexpr std::string_view kCodecPrefix = "optical-photo-effect:v1";

using Fields = std::span<const PhotoEffectParameterField>;

constexpr std::array<PhotoEffectParameterField, 5> kHalationFields{{
    {"radius", "Halo radius (document px)", 0.25, 256.0, 0.25},
    {"threshold", "Highlight threshold (linear)", 0.0, 16.0, 0.01},
    {"knee", "Threshold softness (linear)", 0.0, 4.0, 0.01},
    {"strength", "Halo strength", 0.0, 2.0, 0.01},
    {"warmth", "Halo warmth", -1.0, 1.0, 0.01},
}};
constexpr std::array<PhotoEffectParameterField, 7> kStreakFields{{
    {"half_length", "Streak half length (document px)", 1.0, 1024.0, 1.0},
    {"width", "Streak width (document px)", 0.25, 32.0, 0.25},
    {"angle_degrees", "Streak angle (degrees)", -180.0, 180.0, 0.1},
    {"threshold", "Highlight threshold (linear)", 0.0, 16.0, 0.01},
    {"knee", "Threshold softness (linear)", 0.0, 4.0, 0.01},
    {"strength", "Streak strength", 0.0, 2.0, 0.01},
    {"warmth", "Streak warmth", -1.0, 1.0, 0.01},
}};
constexpr std::array<PhotoEffectParameterField, 8> kStarburstFields{{
    {"radius", "Ray length (document px)", 1.0, 512.0, 1.0},
    {"width", "Ray width (document px)", 0.25, 24.0, 0.25},
    {"angle_degrees", "Ray rotation (degrees)", -180.0, 180.0, 0.1},
    {"ray_count", "Ray pairs", 2.0, 12.0, 1.0},
    {"threshold", "Highlight threshold (linear)", 0.0, 16.0, 0.01},
    {"knee", "Threshold softness (linear)", 0.0, 4.0, 0.01},
    {"strength", "Starburst strength", 0.0, 2.0, 0.01},
    {"warmth", "Starburst warmth", -1.0, 1.0, 0.01},
}};
constexpr std::array<PhotoEffectParameterField, 7> kBokehFields{{
    {"radius", "Aperture radius (document px)", 0.25, 96.0, 0.25},
    {"aperture_blades", "Aperture blades", 3.0, 12.0, 1.0},
    {"rotation_degrees", "Aperture rotation (degrees)", -180.0, 180.0, 0.1},
    {"integration_samples", "Aperture integration samples", 16.0, 256.0, 1.0},
    {"threshold", "Highlight threshold (linear)", 0.0, 16.0, 0.01},
    {"knee", "Threshold softness (linear)", 0.0, 4.0, 0.01},
    {"strength", "Highlight boost", 0.0, 2.0, 0.01},
}};
constexpr std::array<PhotoEffectParameterField, 6> kTiltShiftFields{{
    {"center_x", "Focus center X (frame fraction)", 0.0, 1.0, 0.001},
    {"center_y", "Focus center Y (frame fraction)", 0.0, 1.0, 0.001},
    {"angle_degrees", "Focus line angle (degrees)", -180.0, 180.0, 0.1},
    {"focus_width", "Focus band width (short-side fraction)", 0.005, 1.0, 0.005},
    {"transition_width", "Blur transition (short-side fraction)", 0.001, 1.0, 0.005},
    {"blur_radius", "Defocus sigma (document px)", 0.25, 128.0, 0.25},
}};
constexpr std::array<PhotoEffectParameterField, 4> kZoomFields{{
    {"center_x", "Zoom center X (frame fraction)", 0.0, 1.0, 0.001},
    {"center_y", "Zoom center Y (frame fraction)", 0.0, 1.0, 0.001},
    {"radius", "Radial trail length (document px)", 0.0, 1024.0, 0.25},
    {"samples", "Integration samples", 4.0, 128.0, 1.0},
}};
constexpr std::array<PhotoEffectParameterField, 4> kSpinFields{{
    {"center_x", "Spin center X (frame fraction)", 0.0, 1.0, 0.001},
    {"center_y", "Spin center Y (frame fraction)", 0.0, 1.0, 0.001},
    {"angle_degrees", "Shutter angle (degrees)", 0.0, 180.0, 0.1},
    {"samples", "Integration samples", 4.0, 128.0, 1.0},
}};
constexpr std::array<PhotoEffectParameterField, 2> kSurfaceFields{{
    {"radius", "Surface radius (document px)", 0.25, 12.0, 0.25},
    {"range_sigma", "Color similarity (linear RGB)", 0.0001, 4.0, 0.0001},
}};
constexpr std::array<PhotoEffectParameterField, 2> kHighPassFields{{
    {"radius", "Detail scale sigma (document px)", 0.25, 128.0, 0.25},
    {"strength", "Detail strength", 0.0, 4.0, 0.01},
}};

void set_error(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
}

Fields fields_for(std::string_view type_id) {
  if (type_id == "halation") return kHalationFields;
  if (type_id == "anamorphic_streaks") return kStreakFields;
  if (type_id == "starburst") return kStarburstFields;
  if (type_id == "lens_bokeh") return kBokehFields;
  if (type_id == "tilt_shift") return kTiltShiftFields;
  if (type_id == "zoom_blur") return kZoomFields;
  if (type_id == "spin_blur") return kSpinFields;
  if (type_id == "surface_blur") return kSurfaceFields;
  if (type_id == "high_pass") return kHighPassFields;
  return {};
}

bool is_integer_field(std::string_view type_id, std::string_view key) {
  return (type_id == "starburst" && key == "ray_count") ||
      (type_id == "lens_bokeh" &&
       (key == "aperture_blades" || key == "integration_samples")) ||
      ((type_id == "zoom_blur" || type_id == "spin_blur") && key == "samples");
}

double value_for(std::string_view type_id,
    const OpticalPhotoEffectParameters& p, std::string_view key) {
  if (type_id == "halation") {
    if (key == "radius") return p.halation.radius;
    if (key == "threshold") return p.halation.threshold;
    if (key == "knee") return p.halation.knee;
    if (key == "strength") return p.halation.strength;
    if (key == "warmth") return p.halation.warmth;
  } else if (type_id == "anamorphic_streaks") {
    if (key == "half_length") return p.anamorphic_streaks.half_length;
    if (key == "width") return p.anamorphic_streaks.width;
    if (key == "angle_degrees") return p.anamorphic_streaks.angle_degrees;
    if (key == "threshold") return p.anamorphic_streaks.threshold;
    if (key == "knee") return p.anamorphic_streaks.knee;
    if (key == "strength") return p.anamorphic_streaks.strength;
    if (key == "warmth") return p.anamorphic_streaks.warmth;
  } else if (type_id == "starburst") {
    if (key == "radius") return p.starburst.radius;
    if (key == "width") return p.starburst.width;
    if (key == "angle_degrees") return p.starburst.angle_degrees;
    if (key == "ray_count") return static_cast<double>(p.starburst.ray_count);
    if (key == "threshold") return p.starburst.threshold;
    if (key == "knee") return p.starburst.knee;
    if (key == "strength") return p.starburst.strength;
    if (key == "warmth") return p.starburst.warmth;
  } else if (type_id == "lens_bokeh") {
    if (key == "radius") return p.lens_bokeh.radius;
    if (key == "aperture_blades") return static_cast<double>(p.lens_bokeh.aperture_blades);
    if (key == "rotation_degrees") return p.lens_bokeh.rotation_degrees;
    if (key == "integration_samples") return static_cast<double>(p.lens_bokeh.integration_samples);
    if (key == "threshold") return p.lens_bokeh.threshold;
    if (key == "knee") return p.lens_bokeh.knee;
    if (key == "strength") return p.lens_bokeh.strength;
  } else if (type_id == "tilt_shift") {
    if (key == "center_x") return p.tilt_shift.center_x;
    if (key == "center_y") return p.tilt_shift.center_y;
    if (key == "angle_degrees") return p.tilt_shift.angle_degrees;
    if (key == "focus_width") return p.tilt_shift.focus_width;
    if (key == "transition_width") return p.tilt_shift.transition_width;
    if (key == "blur_radius") return p.tilt_shift.blur_radius;
  } else if (type_id == "zoom_blur") {
    if (key == "center_x") return p.zoom_blur.center_x;
    if (key == "center_y") return p.zoom_blur.center_y;
    if (key == "radius") return p.zoom_blur.radius;
    if (key == "samples") return static_cast<double>(p.zoom_blur.samples);
  } else if (type_id == "spin_blur") {
    if (key == "center_x") return p.spin_blur.center_x;
    if (key == "center_y") return p.spin_blur.center_y;
    if (key == "angle_degrees") return p.spin_blur.angle_degrees;
    if (key == "samples") return static_cast<double>(p.spin_blur.samples);
  } else if (type_id == "surface_blur") {
    if (key == "radius") return p.surface_blur.radius;
    if (key == "range_sigma") return p.surface_blur.range_sigma;
  } else if (type_id == "high_pass") {
    if (key == "radius") return p.high_pass.radius;
    if (key == "strength") return p.high_pass.strength;
  }
  return std::numeric_limits<double>::quiet_NaN();
}

bool assign_value(std::string_view type_id, std::string_view key, double value,
    OpticalPhotoEffectParameters* p) {
  if (p == nullptr) return false;
  if (type_id == "halation") {
    if (key == "radius") p->halation.radius = value;
    else if (key == "threshold") p->halation.threshold = value;
    else if (key == "knee") p->halation.knee = value;
    else if (key == "strength") p->halation.strength = value;
    else if (key == "warmth") p->halation.warmth = value;
    else return false;
  } else if (type_id == "anamorphic_streaks") {
    if (key == "half_length") p->anamorphic_streaks.half_length = value;
    else if (key == "width") p->anamorphic_streaks.width = value;
    else if (key == "angle_degrees") p->anamorphic_streaks.angle_degrees = value;
    else if (key == "threshold") p->anamorphic_streaks.threshold = value;
    else if (key == "knee") p->anamorphic_streaks.knee = value;
    else if (key == "strength") p->anamorphic_streaks.strength = value;
    else if (key == "warmth") p->anamorphic_streaks.warmth = value;
    else return false;
  } else if (type_id == "starburst") {
    if (key == "radius") p->starburst.radius = value;
    else if (key == "width") p->starburst.width = value;
    else if (key == "angle_degrees") p->starburst.angle_degrees = value;
    else if (key == "ray_count") p->starburst.ray_count = static_cast<int>(value);
    else if (key == "threshold") p->starburst.threshold = value;
    else if (key == "knee") p->starburst.knee = value;
    else if (key == "strength") p->starburst.strength = value;
    else if (key == "warmth") p->starburst.warmth = value;
    else return false;
  } else if (type_id == "lens_bokeh") {
    if (key == "radius") p->lens_bokeh.radius = value;
    else if (key == "aperture_blades") p->lens_bokeh.aperture_blades = static_cast<int>(value);
    else if (key == "rotation_degrees") p->lens_bokeh.rotation_degrees = value;
    else if (key == "integration_samples") p->lens_bokeh.integration_samples = static_cast<int>(value);
    else if (key == "threshold") p->lens_bokeh.threshold = value;
    else if (key == "knee") p->lens_bokeh.knee = value;
    else if (key == "strength") p->lens_bokeh.strength = value;
    else return false;
  } else if (type_id == "tilt_shift") {
    if (key == "center_x") p->tilt_shift.center_x = value;
    else if (key == "center_y") p->tilt_shift.center_y = value;
    else if (key == "angle_degrees") p->tilt_shift.angle_degrees = value;
    else if (key == "focus_width") p->tilt_shift.focus_width = value;
    else if (key == "transition_width") p->tilt_shift.transition_width = value;
    else if (key == "blur_radius") p->tilt_shift.blur_radius = value;
    else return false;
  } else if (type_id == "zoom_blur") {
    if (key == "center_x") p->zoom_blur.center_x = value;
    else if (key == "center_y") p->zoom_blur.center_y = value;
    else if (key == "radius") p->zoom_blur.radius = value;
    else if (key == "samples") p->zoom_blur.samples = static_cast<int>(value);
    else return false;
  } else if (type_id == "spin_blur") {
    if (key == "center_x") p->spin_blur.center_x = value;
    else if (key == "center_y") p->spin_blur.center_y = value;
    else if (key == "angle_degrees") p->spin_blur.angle_degrees = value;
    else if (key == "samples") p->spin_blur.samples = static_cast<int>(value);
    else return false;
  } else if (type_id == "surface_blur") {
    if (key == "radius") p->surface_blur.radius = value;
    else if (key == "range_sigma") p->surface_blur.range_sigma = value;
    else return false;
  } else if (type_id == "high_pass") {
    if (key == "radius") p->high_pass.radius = value;
    else if (key == "strength") p->high_pass.strength = value;
    else return false;
  } else {
    return false;
  }
  return true;
}

bool validate_values(std::string_view type_id,
    const OpticalPhotoEffectParameters& parameters, std::string* error) {
  const auto fields = fields_for(type_id);
  if (fields.empty()) {
    set_error(error, "Unsupported optical photo effect type.");
    return false;
  }
  for (const auto& field : fields) {
    const auto value = value_for(type_id, parameters, field.key);
    if (!std::isfinite(value) || value < field.minimum || value > field.maximum ||
        (is_integer_field(type_id, field.key) && std::floor(value) != value)) {
      set_error(error, "Optical parameter is outside its finite supported range: " +
          std::string(field.key));
      return false;
    }
  }
  return true;
}

double clamp01(double value) {
  return std::clamp(value, 0.0, 1.0);
}

double smoothstep(double low, double high, double value) {
  if (high <= low) return value < low ? 0.0 : 1.0;
  const auto t = clamp01((value - low) / (high - low));
  return t * t * (3.0 - 2.0 * t);
}

double highlight_energy(double luminance, double threshold, double knee) {
  if (luminance <= 0.0) return 0.0;
  if (knee <= 1e-12) return std::max(0.0, luminance - threshold);
  // The soft-knee region joins zero below T-K to the ordinary highlight excess
  // above T+K. In particular, the knee must not add a permanent +K bias to
  // every highlight once it is fully above the threshold.
  const auto knee_distance = std::clamp(luminance - (threshold - knee), 0.0,
      2.0 * knee);
  const auto soft = knee_distance * knee_distance / (4.0 * knee);
  return std::max(std::max(0.0, luminance - threshold), soft);
}

std::array<double, 3> warmth_tint(double warmth,
    const std::array<double, 3>& luminance_weights) {
  std::array<double, 3> tint{1.0 + 0.36 * warmth,
      1.0 - 0.04 * std::abs(warmth), std::max(0.1, 1.0 - 0.58 * warmth)};
  const auto luminance = tint[0] * luminance_weights[0] +
      tint[1] * luminance_weights[1] + tint[2] * luminance_weights[2];
  if (!std::isfinite(luminance) || luminance <= 1.0e-12) return {1.0, 1.0, 1.0};
  for (auto& channel : tint) channel /= luminance;
  return tint;
}

bool checked_ceil(double value, int* output) {
  if (output == nullptr || !std::isfinite(value) || value < 0.0 ||
      value > static_cast<double>(std::numeric_limits<int>::max() - 2)) return false;
  *output = static_cast<int>(std::ceil(value));
  return true;
}

bool image_count(int width, int height, std::size_t* count) {
  if (count == nullptr || width <= 0 || height <= 0) return false;
  const auto w = static_cast<std::size_t>(width);
  const auto h = static_cast<std::size_t>(height);
  if (w > std::numeric_limits<std::size_t>::max() / h) return false;
  *count = w * h;
  return true;
}

bool scratch_fits(std::size_t count, std::size_t image_count,
    std::size_t extra_bytes, std::size_t limit) {
  if (image_count != 0 && count >
      std::numeric_limits<std::size_t>::max() / (image_count * sizeof(ColorPixel))) return false;
  const auto image_bytes = count * image_count * sizeof(ColorPixel);
  return extra_bytes <= limit && image_bytes <= limit - extra_bytes;
}

template <typename Work>
bool parallel_rows(int height, CancellationToken cancellation, Work&& work) {
  if (height <= 0 || cancellation.cancelled()) return false;
  run_cpu_parallel_for(height, [&](int row) {
    if (!cancellation.cancelled()) work(row);
  });
  return !cancellation.cancelled();
}

bool valid_common_options(const PhotoEffectRenderOptions& options,
    int width, int height, bool check_raster) {
  double luminance_weight_sum = 0.0;
  for (const auto weight : options.luminance_weights) {
    if (!std::isfinite(weight) || std::abs(weight) > 4.0) return false;
    luminance_weight_sum += weight;
  }
  if (!std::isfinite(options.scale_x) || !std::isfinite(options.scale_y) ||
      options.scale_x < kMinimumScale || options.scale_y < kMinimumScale ||
      options.scale_x > kMaximumScale || options.scale_y > kMaximumScale ||
      options.origin_x < 0 || options.origin_y < 0 ||
      options.frame_width < 0 || options.frame_height < 0 ||
      ((options.frame_width == 0) != (options.frame_height == 0)) ||
      !std::isfinite(luminance_weight_sum) ||
      std::abs(luminance_weight_sum - 1.0) > 1.0e-4) return false;
  if (check_raster && (options.origin_x > std::numeric_limits<int>::max() - width ||
      options.origin_y > std::numeric_limits<int>::max() - height)) return false;
  if (options.frame_width > 0 && check_raster &&
      (options.origin_x + width > options.frame_width ||
       options.origin_y + height > options.frame_height)) return false;
  return true;
}

PhotoEffectResult invalid_result(std::string message) {
  return {PhotoEffectStatus::Invalid, std::move(message)};
}

int sample_coordinate(int value, int size) {
  return std::clamp(value, 0, size - 1);
}

ColorPixel premultiplied(ColorPixel p) {
  return {p.red * p.alpha, p.green * p.alpha, p.blue * p.alpha, p.alpha};
}

ColorPixel straight(ColorPixel p) {
  if (p.alpha <= 1.0e-12F) return {0.0F, 0.0F, 0.0F, 0.0F};
  return {p.red / p.alpha, p.green / p.alpha, p.blue / p.alpha, p.alpha};
}

ColorPixel lerp_pixel(ColorPixel a, ColorPixel b, double t) {
  const auto mix = [t](float x, float y) {
    return static_cast<float>(std::lerp(static_cast<double>(x),
        static_cast<double>(y), t));
  };
  return {mix(a.red, b.red), mix(a.green, b.green), mix(a.blue, b.blue),
      mix(a.alpha, b.alpha)};
}

ColorPixel bilinear_sample(const std::vector<ColorPixel>& image,
    int width, int height, double x, double y, bool is_premultiplied) {
  x = std::clamp(x, 0.0, static_cast<double>(width - 1));
  y = std::clamp(y, 0.0, static_cast<double>(height - 1));
  const auto x0 = static_cast<int>(std::floor(x));
  const auto y0 = static_cast<int>(std::floor(y));
  const auto x1 = sample_coordinate(x0 + 1, width);
  const auto y1 = sample_coordinate(y0 + 1, height);
  const auto fx = x - static_cast<double>(x0);
  const auto fy = y - static_cast<double>(y0);
  const auto read = [&](int sx, int sy) {
    const auto p = image[static_cast<std::size_t>(sy) *
        static_cast<std::size_t>(width) + static_cast<std::size_t>(sx)];
    return is_premultiplied ? p : premultiplied(p);
  };
  const auto top = lerp_pixel(read(x0, y0), read(x1, y0), fx);
  const auto bottom = lerp_pixel(read(x0, y1), read(x1, y1), fx);
  return lerp_pixel(top, bottom, fy);
}

std::vector<double> gaussian_weights(double sigma, int radius) {
  std::vector<double> weights(static_cast<std::size_t>(radius * 2 + 1), 1.0);
  if (radius == 0 || sigma <= 1.0e-12) return weights;
  double total = 0.0;
  for (int i = -radius; i <= radius; ++i) {
    const auto q = static_cast<double>(i) / sigma;
    const auto value = std::exp(-0.5 * q * q);
    weights[static_cast<std::size_t>(i + radius)] = value;
    total += value;
  }
  for (auto& weight : weights) weight /= total;
  return weights;
}

bool convolve_axis(const std::vector<ColorPixel>& input,
    std::vector<ColorPixel>* output, int width, int height,
    const std::vector<double>& weights, int radius, bool horizontal,
    bool input_is_premultiplied, CancellationToken cancellation) {
  if (output == nullptr) return false;
  const auto count = input.size();
  output->resize(count);
  return parallel_rows(height, cancellation, [&](int y) {
    for (int x = 0; x < width; ++x) {
      if ((x & 63) == 0 && cancellation.cancelled()) return;
      std::array<double, 4> sum{};
      for (int offset = -radius; offset <= radius; ++offset) {
        const auto sx = horizontal ? sample_coordinate(x + offset, width) : x;
        const auto sy = horizontal ? y : sample_coordinate(y + offset, height);
        const auto p0 = input[static_cast<std::size_t>(sy) *
            static_cast<std::size_t>(width) + static_cast<std::size_t>(sx)];
        const auto p = input_is_premultiplied ? p0 : premultiplied(p0);
        const auto weight = weights[static_cast<std::size_t>(offset + radius)];
        sum[0] += static_cast<double>(p.red) * weight;
        sum[1] += static_cast<double>(p.green) * weight;
        sum[2] += static_cast<double>(p.blue) * weight;
        sum[3] += static_cast<double>(p.alpha) * weight;
      }
      (*output)[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
          static_cast<std::size_t>(x)] = {static_cast<float>(sum[0]),
          static_cast<float>(sum[1]), static_cast<float>(sum[2]),
          static_cast<float>(sum[3])};
    }
  });
}

bool gaussian_blur(const std::vector<ColorPixel>& input,
    std::vector<ColorPixel>* output, int width, int height,
    double sigma_x, double sigma_y, int radius_x, int radius_y,
    CancellationToken cancellation, bool input_is_premultiplied = false) {
  if (output == nullptr) return false;
  std::vector<ColorPixel> horizontal;
  const auto x_weights = gaussian_weights(sigma_x, radius_x);
  const auto y_weights = gaussian_weights(sigma_y, radius_y);
  return convolve_axis(input, &horizontal, width, height, x_weights,
             radius_x, true, input_is_premultiplied, cancellation) &&
      convolve_axis(horizontal, output, width, height, y_weights,
             radius_y, false, true, cancellation);
}

std::vector<ColorPixel> extract_highlights(const std::vector<ColorPixel>& input,
    int width, int height, double threshold, double knee,
    const std::array<double, 3>& luminance_weights,
    CancellationToken cancellation, bool* ok) {
  std::vector<ColorPixel> result(input.size());
  if (ok != nullptr) *ok = false;
  const auto completed = parallel_rows(height, cancellation, [&](int y) {
    const auto row = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
    for (int x = 0; x < width; ++x) {
      if ((x & 63) == 0 && cancellation.cancelled()) return;
      const auto i = row + static_cast<std::size_t>(x);
      const auto& p = input[i];
      const auto luminance = luminance_weights[0] * static_cast<double>(p.red) +
          luminance_weights[1] * static_cast<double>(p.green) +
          luminance_weights[2] * static_cast<double>(p.blue);
      const auto energy = highlight_energy(luminance, threshold, knee);
      if (energy <= 0.0 || luminance <= 1.0e-12) continue;
      const auto factor = energy / luminance;
      const auto alpha = static_cast<double>(p.alpha);
      result[i] = {static_cast<float>(p.red * factor * alpha),
          static_cast<float>(p.green * factor * alpha),
          static_cast<float>(p.blue * factor * alpha),
          static_cast<float>(alpha * smoothstep(threshold - knee,
              threshold + std::max(knee, 1.0e-12), luminance))};
    }
  });
  if (ok != nullptr) *ok = completed;
  return completed ? std::move(result) : std::vector<ColorPixel>{};
}

bool emit_glow(const std::vector<ColorPixel>& input,
    std::vector<ColorPixel>* output, const std::vector<ColorPixel>& glow,
    int width, int height, double strength, double warmth,
    const std::array<double, 3>& luminance_weights,
    CancellationToken cancellation) {
  if (output == nullptr || output->size() != input.size() || glow.size() != input.size())
    return false;
  const auto tint = warmth_tint(warmth, luminance_weights);
  const auto max_float = static_cast<double>(std::numeric_limits<float>::max());
  std::atomic_bool valid{true};
  const auto completed = parallel_rows(height, cancellation, [&](int y) {
    const auto row = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
    for (int x = 0; x < width; ++x) {
      if ((x & 63) == 0 && cancellation.cancelled()) return;
      const auto i = row + static_cast<std::size_t>(x);
      const auto& src = input[i];
      const auto& bloom = glow[i];
      const auto alpha = static_cast<double>(src.alpha);
      const auto added_alpha = clamp01(static_cast<double>(bloom.alpha) * strength);
      const auto out_alpha = alpha + added_alpha * (1.0 - alpha);
      const auto inv_alpha = out_alpha > 1.0e-12 ? 1.0 / out_alpha : 0.0;
      const std::array<double, 3> rgb{
          (static_cast<double>(src.red) * alpha + bloom.red * strength * tint[0]) * inv_alpha,
          (static_cast<double>(src.green) * alpha + bloom.green * strength * tint[1]) * inv_alpha,
          (static_cast<double>(src.blue) * alpha + bloom.blue * strength * tint[2]) * inv_alpha};
      if (!std::isfinite(rgb[0]) || !std::isfinite(rgb[1]) ||
          !std::isfinite(rgb[2]) || std::abs(rgb[0]) > max_float ||
          std::abs(rgb[1]) > max_float || std::abs(rgb[2]) > max_float) {
        valid.store(false, std::memory_order_relaxed);
        return;
      }
      (*output)[i] = {static_cast<float>(rgb[0]), static_cast<float>(rgb[1]),
          static_cast<float>(rgb[2]), static_cast<float>(out_alpha)};
    }
  });
  return completed && valid.load(std::memory_order_relaxed);
}

std::vector<double> line_weights(double sigma, int radius) {
  std::vector<double> weights(static_cast<std::size_t>(radius * 2 + 1));
  double total = 0.0;
  for (int i = -radius; i <= radius; ++i) {
    const auto q = static_cast<double>(i) / std::max(sigma, 1.0e-12);
    const auto weight = std::exp(-0.5 * q * q);
    weights[static_cast<std::size_t>(i + radius)] = weight;
    total += weight;
  }
  if (total <= 0.0) return {};
  for (auto& weight : weights) weight /= total;
  return weights;
}

bool convolve_direction(const std::vector<ColorPixel>& input,
    std::vector<ColorPixel>* output, int width, int height,
    double direction_x, double direction_y, double document_step,
    const std::vector<double>& weights, int radius,
    CancellationToken cancellation) {
  if (output == nullptr) return false;
  output->resize(input.size());
  return parallel_rows(height, cancellation, [&](int y) {
    for (int x = 0; x < width; ++x) {
      if ((x & 31) == 0 && cancellation.cancelled()) return;
      std::array<double, 4> sum{};
      for (int tap = -radius; tap <= radius; ++tap) {
        if ((tap & 255) == 0 && cancellation.cancelled()) return;
        const auto offset = static_cast<double>(tap) * document_step;
        const auto sample = bilinear_sample(input, width, height,
            static_cast<double>(x) + direction_x * offset,
            static_cast<double>(y) + direction_y * offset, true);
        const auto weight = weights[static_cast<std::size_t>(tap + radius)];
        sum[0] += sample.red * weight;
        sum[1] += sample.green * weight;
        sum[2] += sample.blue * weight;
        sum[3] += sample.alpha * weight;
      }
      (*output)[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
          static_cast<std::size_t>(x)] = {static_cast<float>(sum[0]),
          static_cast<float>(sum[1]), static_cast<float>(sum[2]),
          static_cast<float>(sum[3])};
    }
  });
}

bool all_pixels_finite(std::span<const ColorPixel> pixels,
    CancellationToken cancellation) {
  for (std::size_t i = 0; i < pixels.size(); ++i) {
    if ((i & 4095U) == 0 && cancellation.cancelled()) return false;
    const auto& p = pixels[i];
    if (!std::isfinite(p.red) || !std::isfinite(p.green) ||
        !std::isfinite(p.blue) || !std::isfinite(p.alpha) ||
        p.alpha < 0.0F || p.alpha > 1.0F) return false;
  }
  return true;
}

bool aperture_samples(const OpticalPhotoEffectParameters::LensBokeh& p,
    double scale_x, double scale_y,
    std::vector<std::array<double, 2>>* samples) {
  if (samples == nullptr) return false;
  samples->clear();
  samples->reserve(static_cast<std::size_t>(p.integration_samples));
  constexpr double golden = 0.6180339887498948482;
  constexpr double golden2 = 0.7548776662466927600;
  const auto rotation = p.rotation_degrees * std::numbers::pi / 180.0;
  const auto radius_x = p.radius * scale_x;
  const auto radius_y = p.radius * scale_y;
  for (int i = 0; i < p.integration_samples; ++i) {
    const auto u = (static_cast<double>(i) + 0.5) /
        static_cast<double>(p.integration_samples);
    const auto sector_position = u * p.aperture_blades;
    const auto sector = static_cast<int>(std::floor(sector_position)) % p.aperture_blades;
    const auto v0_angle = rotation + 2.0 * std::numbers::pi *
        static_cast<double>(sector) / p.aperture_blades;
    const auto v1_angle = rotation + 2.0 * std::numbers::pi *
        static_cast<double>(sector + 1) / p.aperture_blades;
    const auto q = std::sqrt(std::fmod((static_cast<double>(i) + 0.5) * golden, 1.0));
    const auto r = std::fmod((static_cast<double>(i) + 0.5) * golden2, 1.0);
    const auto a = q * (1.0 - r);
    const auto b = q * r;
    const auto x = a * std::cos(v0_angle) + b * std::cos(v1_angle);
    const auto y = a * std::sin(v0_angle) + b * std::sin(v1_angle);
    samples->push_back({x * radius_x, y * radius_y});
  }
  return true;
}

bool bokeh_integrate(const std::vector<ColorPixel>& highlights,
    std::vector<ColorPixel>* output, int width, int height,
    const std::vector<std::array<double, 2>>& samples,
    CancellationToken cancellation) {
  if (output == nullptr || samples.empty()) return false;
  output->resize(highlights.size());
  const auto inverse = 1.0 / static_cast<double>(samples.size());
  return parallel_rows(height, cancellation, [&](int y) {
    const auto row = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
    for (int x = 0; x < width; ++x) {
      if ((x & 31) == 0 && cancellation.cancelled()) return;
      std::array<double, 4> sum{};
      for (std::size_t sample_index = 0; sample_index < samples.size(); ++sample_index) {
        if ((sample_index & 31U) == 0U && cancellation.cancelled()) return;
        const auto& offset = samples[sample_index];
        const auto p = bilinear_sample(highlights, width, height,
            static_cast<double>(x) + offset[0],
            static_cast<double>(y) + offset[1], true);
        sum[0] += p.red;
        sum[1] += p.green;
          sum[2] += p.blue;
          sum[3] += p.alpha;
      }
      (*output)[row + static_cast<std::size_t>(x)] = {static_cast<float>(sum[0] * inverse),
          static_cast<float>(sum[1] * inverse), static_cast<float>(sum[2] * inverse),
          static_cast<float>(sum[3] * inverse)};
    }
  });
}

PhotoEffectResult validate_support(std::string_view type_id,
    const OpticalPhotoEffectParameters& p, const PhotoEffectRenderOptions& options,
    PhotoEffectSupport* support) {
  if (support == nullptr) return invalid_result("Missing optical effect support output.");
  *support = {};
  std::string error;
  if (!validate_values(type_id, p, &error)) {
    return fields_for(type_id).empty()
        ? PhotoEffectResult{PhotoEffectStatus::Unsupported, std::move(error)}
        : PhotoEffectResult{PhotoEffectStatus::Invalid, std::move(error)};
  }
  if (!valid_common_options(options, 0, 0, false))
    return invalid_result("Optical effect scale, origin, or frame dimensions are invalid.");
  const auto sx = options.scale_x;
  const auto sy = options.scale_y;
  const auto max_scale = std::max(sx, sy);
  int x_support = 0;
  int y_support = 0;
  const auto set_symmetric = [&](double x, double y) {
    return checked_ceil(x, &x_support) && checked_ceil(y, &y_support);
  };

  if (type_id == "halation") {
    const auto& q = p.halation;
    if (q.strength == 0.0) return {PhotoEffectStatus::Identity, {}};
    const auto sigma_x = q.radius * sx;
    const auto sigma_y = q.radius * sy;
    if (3.0 * std::max(sigma_x, sigma_y) > kMaximumSupport ||
        !set_symmetric(std::ceil(3.0 * sigma_x), std::ceil(3.0 * sigma_y)))
      return invalid_result("Halation support exceeds the CPU limit.");
  } else if (type_id == "anamorphic_streaks") {
    const auto& q = p.anamorphic_streaks;
    if (q.strength == 0.0) return {PhotoEffectStatus::Identity, {}};
    const auto angle = q.angle_degrees * std::numbers::pi / 180.0;
    const auto c = std::abs(std::cos(angle));
    const auto s = std::abs(std::sin(angle));
    const auto x = c * q.half_length * sx + s * 3.0 * q.width * sy;
    const auto y = s * q.half_length * sy + c * 3.0 * q.width * sx;
    if (std::max(x, y) > kMaximumSupport || !set_symmetric(x + 1.0, y + 1.0))
      return invalid_result("Anamorphic streak support exceeds the CPU limit.");
  } else if (type_id == "starburst") {
    const auto& q = p.starburst;
    if (q.strength == 0.0) return {PhotoEffectStatus::Identity, {}};
    const auto support_px = q.radius * max_scale + 3.0 * q.width * max_scale + 1.0;
    if (support_px > kMaximumSupport || !set_symmetric(support_px, support_px))
      return invalid_result("Starburst support exceeds the CPU limit.");
  } else if (type_id == "lens_bokeh") {
    const auto& q = p.lens_bokeh;
    const auto x = q.radius * sx + 1.0;
    const auto y = q.radius * sy + 1.0;
    if (std::max(x, y) > kMaximumSupport || !set_symmetric(x, y))
      return invalid_result("Lens bokeh support exceeds the CPU limit.");
  } else if (type_id == "tilt_shift") {
    const auto sigma_x = p.tilt_shift.blur_radius * sx;
    const auto sigma_y = p.tilt_shift.blur_radius * sy;
    if (3.0 * std::max(sigma_x, sigma_y) > kMaximumSupport ||
        !set_symmetric(std::ceil(3.0 * sigma_x), std::ceil(3.0 * sigma_y)))
      return invalid_result("Tilt-shift support exceeds the CPU limit.");
  } else if (type_id == "zoom_blur") {
    if (p.zoom_blur.radius == 0.0) return {PhotoEffectStatus::Identity, {}};
    if (options.frame_width > 0) {
      x_support = options.frame_width;
      y_support = options.frame_height;
    }
  } else if (type_id == "spin_blur") {
    if (p.spin_blur.angle_degrees == 0.0) return {PhotoEffectStatus::Identity, {}};
    if (options.frame_width > 0) {
      x_support = options.frame_width;
      y_support = options.frame_height;
    }
  } else if (type_id == "surface_blur") {
    const auto rx = p.surface_blur.radius * sx;
    const auto ry = p.surface_blur.radius * sy;
    if (std::max(rx, ry) > 24.0 || !set_symmetric(std::ceil(rx), std::ceil(ry)))
      return invalid_result("Surface blur radius exceeds the CPU limit at this scale.");
  } else if (type_id == "high_pass") {
    const auto sigma_x = p.high_pass.radius * sx;
    const auto sigma_y = p.high_pass.radius * sy;
    if (3.0 * std::max(sigma_x, sigma_y) > kMaximumSupport ||
        !set_symmetric(std::ceil(3.0 * sigma_x), std::ceil(3.0 * sigma_y)))
      return invalid_result("High-pass support exceeds the CPU limit.");
  }
  support->x = x_support;
  support->y = y_support;
  return {PhotoEffectStatus::Applied, {}};
}

}  // namespace

std::span<const PhotoEffectParameterField> optical_photo_effect_fields(
    std::string_view type_id) {
  return fields_for(type_id);
}

PhotoEffectParameterValues optical_photo_effect_values(
    std::string_view type_id, const OpticalPhotoEffectParameters& parameters) {
  PhotoEffectParameterValues result;
  for (const auto& field : fields_for(type_id))
    result.emplace(std::string(field.key), value_for(type_id, parameters, field.key));
  return result;
}

bool set_optical_photo_effect_values(std::string_view type_id,
    const PhotoEffectParameterValues& values,
    OpticalPhotoEffectParameters* parameters, std::string* error) {
  if (error != nullptr) error->clear();
  if (parameters == nullptr) {
    set_error(error, "Missing optical effect parameter output.");
    return false;
  }
  const auto fields = fields_for(type_id);
  if (fields.empty()) {
    set_error(error, "Unsupported optical photo effect type.");
    return false;
  }
  auto candidate = *parameters;
  for (const auto& [key, value] : values) {
    const auto field = std::find_if(fields.begin(), fields.end(), [&](const auto& item) {
      return item.key == key;
    });
    if (field == fields.end() || !std::isfinite(value) || value < field->minimum ||
        value > field->maximum || (is_integer_field(type_id, key) && std::floor(value) != value)) {
      set_error(error, "Unknown or invalid optical parameter: " + key);
      return false;
    }
    if (!assign_value(type_id, key, value, &candidate)) {
      set_error(error, "Could not assign optical parameter: " + key);
      return false;
    }
  }
  if (!validate_values(type_id, candidate, error)) return false;
  *parameters = std::move(candidate);
  return true;
}

std::string serialize_optical_photo_effect_parameters(
    std::string_view type_id,
    const OpticalPhotoEffectParameters& parameters) {
  std::string error;
  if (!validate_values(type_id, parameters, &error)) return {};
  const auto fields = fields_for(type_id);
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << kCodecPrefix << ' ' << type_id << ' ' << fields.size()
      << std::setprecision(std::numeric_limits<double>::max_digits10);
  for (const auto& field : fields)
    stream << ' ' << value_for(type_id, parameters, field.key);
  return stream.str();
}

bool deserialize_optical_photo_effect_parameters(std::string_view type_id,
    std::string_view payload, OpticalPhotoEffectParameters* parameters,
    std::string* error) {
  if (error != nullptr) error->clear();
  if (parameters == nullptr || payload.size() > 16384) {
    set_error(error, "Missing parameter output or optical payload is too large.");
    return false;
  }
  const auto fields = fields_for(type_id);
  if (fields.empty()) {
    set_error(error, "Unsupported optical photo effect type.");
    return false;
  }
  std::istringstream stream{std::string(payload)};
  stream.imbue(std::locale::classic());
  std::string magic;
  std::string encoded_type;
  std::size_t count = 0;
  if (!(stream >> magic >> encoded_type >> count) || magic != kCodecPrefix ||
      encoded_type != type_id || count != fields.size()) {
    set_error(error, "Optical effect payload header or field count is invalid.");
    return false;
  }
  PhotoEffectParameterValues values;
  for (const auto& field : fields) {
    double value = 0.0;
    if (!(stream >> value) || !std::isfinite(value)) {
      set_error(error, "Optical effect payload contains a missing or non-finite value.");
      return false;
    }
    values.emplace(std::string(field.key), value);
  }
  stream >> std::ws;
  if (!stream.eof()) {
    set_error(error, "Optical effect payload has trailing data.");
    return false;
  }
  auto candidate = *parameters;
  if (!set_optical_photo_effect_values(type_id, values, &candidate, error)) return false;
  *parameters = std::move(candidate);
  return true;
}

bool optical_photo_effect_requires_full_frame(std::string_view type_id,
    const OpticalPhotoEffectParameters& parameters) noexcept {
  return (type_id == "zoom_blur" && parameters.zoom_blur.radius > 0.0) ||
      (type_id == "spin_blur" && parameters.spin_blur.angle_degrees > 0.0);
}

PhotoEffectResult optical_photo_effect_support(std::string_view type_id,
    const OpticalPhotoEffectParameters& parameters,
    const PhotoEffectRenderOptions& options, PhotoEffectSupport* support) {
  return validate_support(type_id, parameters, options, support);
}

PhotoEffectResult apply_optical_photo_effect(std::vector<ColorPixel>* pixels,
    int width, int height, std::string_view type_id,
    const OpticalPhotoEffectParameters& p,
    const PhotoEffectRenderOptions& options) {
  std::size_t count = 0;
  if (pixels == nullptr || !image_count(width, height, &count) ||
      pixels->size() != count || !valid_common_options(options, width, height, true))
    return invalid_result("Invalid optical photo effect image dimensions or render options.");
  PhotoEffectSupport support;
  const auto supported = optical_photo_effect_support(type_id, p, options, &support);
  if (!supported.ok()) return supported;
  if (options.cancellation.cancelled())
    return {PhotoEffectStatus::Cancelled, "Optical photo effect cancelled."};
  if (supported.status == PhotoEffectStatus::Identity) return supported;
  if (!all_pixels_finite(*pixels, options.cancellation)) {
    if (options.cancellation.cancelled())
      return {PhotoEffectStatus::Cancelled, "Optical photo effect cancelled."};
    return invalid_result("Optical photo effect requires finite RGB and unit alpha.");
  }
  const auto whole_frame = optical_photo_effect_requires_full_frame(type_id, p);
  const auto frame_width = options.frame_width > 0 ? options.frame_width : width;
  const auto frame_height = options.frame_height > 0 ? options.frame_height : height;
  if (whole_frame && (options.origin_x != 0 || options.origin_y != 0 ||
      width != frame_width || height != frame_height))
    return {PhotoEffectStatus::Unsupported,
        "Zoom and spin blur require an admitted whole-frame raster."};

  std::size_t image_scratch_count = 1;
  std::size_t extra_scratch = 0;
  const auto add_kernel_bytes = [&](int radius_x, int radius_y) {
    const auto taps_x = static_cast<std::size_t>(radius_x) * 2U + 1U;
    const auto taps_y = static_cast<std::size_t>(radius_y) * 2U + 1U;
    if (taps_x > std::numeric_limits<std::size_t>::max() - taps_y ||
        taps_x + taps_y > (std::numeric_limits<std::size_t>::max() - extra_scratch) /
            sizeof(double)) return false;
    extra_scratch += (taps_x + taps_y) * sizeof(double);
    return true;
  };
  if (type_id == "halation" || type_id == "anamorphic_streaks" ||
      type_id == "starburst") image_scratch_count = type_id == "starburst" ? 5U : 4U;
  else if (type_id == "lens_bokeh") {
    // Output/defocused rasters plus optional extracted highlights and bloom.
    image_scratch_count = p.lens_bokeh.strength > 0.0 ? 4U : 2U;
    extra_scratch = static_cast<std::size_t>(p.lens_bokeh.integration_samples) *
        sizeof(std::array<double, 2>);
  } else if (type_id == "tilt_shift" || type_id == "high_pass") image_scratch_count = 3U;
  else if (type_id == "surface_blur") image_scratch_count = 1U;
  else if (type_id == "zoom_blur" || type_id == "spin_blur") image_scratch_count = 1U;
  if (type_id == "halation" || type_id == "tilt_shift" || type_id == "high_pass") {
    if (!add_kernel_bytes(support.x, support.y))
      return {PhotoEffectStatus::ResourceLimit, "Optical kernel exceeds the scratch budget."};
  } else if (type_id == "anamorphic_streaks") {
    const auto along = static_cast<int>(std::ceil(p.anamorphic_streaks.half_length));
    const auto across = static_cast<int>(std::ceil(3.0 * p.anamorphic_streaks.width));
    if (!add_kernel_bytes(along, across))
      return {PhotoEffectStatus::ResourceLimit, "Optical kernel exceeds the scratch budget."};
  } else if (type_id == "starburst") {
    const auto along = static_cast<int>(std::ceil(p.starburst.radius));
    const auto across = static_cast<int>(std::ceil(3.0 * p.starburst.width));
    if (!add_kernel_bytes(along, across))
      return {PhotoEffectStatus::ResourceLimit, "Optical kernel exceeds the scratch budget."};
  }
  if (!scratch_fits(count, image_scratch_count, extra_scratch,
      options.max_scratch_bytes))
    return {PhotoEffectStatus::ResourceLimit,
        "Optical photo effect exceeds the scratch budget; render a supported padded region."};

  try {
    const auto sx = options.scale_x;
    const auto sy = options.scale_y;
    std::vector<ColorPixel> output = *pixels;
    const auto cancelled = [&]() {
      return PhotoEffectResult{PhotoEffectStatus::Cancelled,
          "Optical photo effect cancelled."};
    };

    if (type_id == "halation") {
      bool extraction_ok = false;
      auto highlights = extract_highlights(*pixels, width, height, p.halation.threshold,
          p.halation.knee, options.luminance_weights, options.cancellation,
          &extraction_ok);
      if (!extraction_ok) return cancelled();
      std::vector<ColorPixel> bloom;
      const auto sigma_x = p.halation.radius * sx;
      const auto sigma_y = p.halation.radius * sy;
      if (!gaussian_blur(highlights, &bloom, width, height, sigma_x, sigma_y,
          support.x, support.y, options.cancellation, true)) return cancelled();
      if (!emit_glow(*pixels, &output, bloom, width, height, p.halation.strength,
          p.halation.warmth, options.luminance_weights, options.cancellation))
        return options.cancellation.cancelled() ? cancelled() : invalid_result(
            "Halation produced a non-finite result.");
    } else if (type_id == "anamorphic_streaks") {
      bool extraction_ok = false;
      auto highlights = extract_highlights(*pixels, width, height,
          p.anamorphic_streaks.threshold,
          p.anamorphic_streaks.knee, options.luminance_weights,
          options.cancellation, &extraction_ok);
      if (!extraction_ok) return cancelled();
      const auto angle = p.anamorphic_streaks.angle_degrees * std::numbers::pi / 180.0;
      std::vector<ColorPixel> bloom;
      const auto cross_support = static_cast<int>(std::ceil(
          3.0 * p.anamorphic_streaks.width));
      std::vector<ColorPixel> along;
      std::vector<ColorPixel> across;
      const auto along_support = static_cast<int>(std::ceil(
          p.anamorphic_streaks.half_length));
      const auto along_weights = line_weights(
          std::max(0.5, p.anamorphic_streaks.half_length * 0.46), along_support);
      const auto across_weights = gaussian_weights(p.anamorphic_streaks.width, cross_support);
      const auto c = std::cos(angle);
      const auto s = std::sin(angle);
      const bool axis_aligned_unit_steps = p.anamorphic_streaks.angle_degrees == 0.0 &&
          sx == 1.0 && sy == 1.0;
      const auto along_ok = axis_aligned_unit_steps
          ? convolve_axis(highlights, &along, width, height, along_weights,
                along_support, true, true, options.cancellation)
          : convolve_direction(highlights, &along, width, height,
                c * sx, s * sy, 1.0, along_weights, along_support,
                options.cancellation);
      const auto across_ok = along_ok && (axis_aligned_unit_steps
          ? convolve_axis(along, &across, width, height, across_weights,
                cross_support, false, true, options.cancellation)
          : convolve_direction(along, &across, width, height,
                -s * sx, c * sy, 1.0, across_weights, cross_support,
                options.cancellation));
      if (!across_ok) return cancelled();
      bloom = std::move(across);
      if (!emit_glow(*pixels, &output, bloom, width, height,
          p.anamorphic_streaks.strength,
          p.anamorphic_streaks.warmth, options.luminance_weights,
          options.cancellation))
        return options.cancellation.cancelled() ? cancelled() : invalid_result(
            "Anamorphic streaks produced a non-finite result.");
    } else if (type_id == "starburst") {
      bool extraction_ok = false;
      auto highlights = extract_highlights(*pixels, width, height,
          p.starburst.threshold,
          p.starburst.knee, options.luminance_weights, options.cancellation,
          &extraction_ok);
      if (!extraction_ok) return cancelled();
      std::vector<ColorPixel> accumulator(count);
      std::vector<ColorPixel> along;
      std::vector<ColorPixel> ray;
      const auto along_radius = static_cast<int>(std::ceil(p.starburst.radius));
      const auto cross_radius = static_cast<int>(std::ceil(3.0 * p.starburst.width));
      const auto along_weights = line_weights(p.starburst.radius * 0.42, along_radius);
      const auto cross_weights = gaussian_weights(p.starburst.width, cross_radius);
      const auto base_angle = p.starburst.angle_degrees * std::numbers::pi / 180.0;
      const auto ray_weight = 1.0 / static_cast<double>(p.starburst.ray_count);
      for (int ray_index = 0; ray_index < p.starburst.ray_count; ++ray_index) {
        if (options.cancellation.cancelled()) return cancelled();
        const auto angle = base_angle + std::numbers::pi * ray_index /
            static_cast<double>(p.starburst.ray_count);
        const auto c = std::cos(angle);
        const auto s = std::sin(angle);
        if (!convolve_direction(highlights, &along, width, height, c * sx, s * sy, 1.0,
                along_weights, along_radius, options.cancellation) ||
            !convolve_direction(along, &ray, width, height, -s * sx, c * sy, 1.0,
                cross_weights, cross_radius, options.cancellation)) return cancelled();
        for (std::size_t i = 0; i < count; ++i) {
          if ((i & 4095U) == 0 && options.cancellation.cancelled()) return cancelled();
          accumulator[i].red += static_cast<float>(ray[i].red * ray_weight);
          accumulator[i].green += static_cast<float>(ray[i].green * ray_weight);
          accumulator[i].blue += static_cast<float>(ray[i].blue * ray_weight);
          accumulator[i].alpha += static_cast<float>(ray[i].alpha * ray_weight);
        }
      }
      if (!emit_glow(*pixels, &output, accumulator, width, height,
          p.starburst.strength,
          p.starburst.warmth, options.luminance_weights,
          options.cancellation))
        return options.cancellation.cancelled() ? cancelled() : invalid_result(
            "Starburst produced a non-finite result.");
    } else if (type_id == "lens_bokeh") {
      std::vector<std::array<double, 2>> aperture;
      if (!aperture_samples(p.lens_bokeh, sx, sy, &aperture))
        return invalid_result("Could not create the aperture integration kernel.");
      // Defocus the complete image through the polygon aperture, including
      // dim edges and non-highlight texture. The optional strength control
      // adds a second, highlight-only bokeh pass over that defocused base.
      const auto premultiplied_ready = parallel_rows(height, options.cancellation,
          [&](int y) {
        const auto row = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        for (int x = 0; x < width; ++x) {
          if ((x & 63) == 0 && options.cancellation.cancelled()) return;
          const auto i = row + static_cast<std::size_t>(x);
          output[i] = premultiplied((*pixels)[i]);
        }
      });
      if (!premultiplied_ready) return cancelled();
      std::vector<ColorPixel> defocused;
      if (!bokeh_integrate(output, &defocused, width, height, aperture,
          options.cancellation)) return cancelled();
      const auto straightened = parallel_rows(height, options.cancellation,
          [&](int y) {
        const auto row = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        for (int x = 0; x < width; ++x) {
          if ((x & 63) == 0 && options.cancellation.cancelled()) return;
          const auto i = row + static_cast<std::size_t>(x);
          output[i] = straight(defocused[i]);
        }
      });
      if (!straightened) return cancelled();
      if (p.lens_bokeh.strength > 0.0) {
        bool extraction_ok = false;
        auto highlights = extract_highlights(*pixels, width, height,
            p.lens_bokeh.threshold, p.lens_bokeh.knee,
            options.luminance_weights, options.cancellation, &extraction_ok);
        if (!extraction_ok) return cancelled();
        std::vector<ColorPixel> bloom;
        if (!bokeh_integrate(highlights, &bloom, width, height, aperture,
            options.cancellation)) return cancelled();
        if (!emit_glow(output, &output, bloom, width, height,
            p.lens_bokeh.strength, 0.0, options.luminance_weights,
            options.cancellation))
          return options.cancellation.cancelled() ? cancelled() : invalid_result(
              "Lens bokeh highlight boost produced a non-finite result.");
      }
    } else if (type_id == "tilt_shift") {
      std::vector<ColorPixel> blurred;
      const auto sigma_x = p.tilt_shift.blur_radius * sx;
      const auto sigma_y = p.tilt_shift.blur_radius * sy;
      if (!gaussian_blur(*pixels, &blurred, width, height, sigma_x, sigma_y,
          support.x, support.y, options.cancellation)) return cancelled();
      const auto frame_w = options.frame_width > 0 ? options.frame_width : width;
      const auto frame_h = options.frame_height > 0 ? options.frame_height : height;
      const auto short_side = static_cast<double>(std::max(1, std::min(frame_w, frame_h)));
      const auto angle = p.tilt_shift.angle_degrees * std::numbers::pi / 180.0;
      const auto nx = -std::sin(angle);
      const auto ny = std::cos(angle);
      for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
          if ((x & 63) == 0 && options.cancellation.cancelled()) return cancelled();
          const auto gx = (static_cast<double>(options.origin_x + x) + 0.5) / frame_w;
          const auto gy = (static_cast<double>(options.origin_y + y) + 0.5) / frame_h;
          const auto distance = std::abs(((gx - p.tilt_shift.center_x) * frame_w * nx +
              (gy - p.tilt_shift.center_y) * frame_h * ny) / short_side);
          const auto blur_mix = smoothstep(p.tilt_shift.focus_width * 0.5,
              p.tilt_shift.focus_width * 0.5 + p.tilt_shift.transition_width, distance);
          const auto i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
              static_cast<std::size_t>(x);
          const auto src = premultiplied((*pixels)[i]);
          const auto dst = lerp_pixel(src, blurred[i], blur_mix);
          output[i] = straight(dst);
        }
      }
    } else if (type_id == "zoom_blur") {
      const auto center_x = p.zoom_blur.center_x * frame_width - 0.5;
      const auto center_y = p.zoom_blur.center_y * frame_height - 0.5;
      const auto radius_x = p.zoom_blur.radius * sx;
      const auto radius_y = p.zoom_blur.radius * sy;
      const auto inv_samples = 1.0 / static_cast<double>(p.zoom_blur.samples);
      for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
          if ((x & 31) == 0 && options.cancellation.cancelled()) return cancelled();
          const auto gx = static_cast<double>(options.origin_x + x) - center_x;
          const auto gy = static_cast<double>(options.origin_y + y) - center_y;
          const auto length = std::hypot(gx, gy);
          const auto ux = length > 1.0e-12 ? gx / length : 0.0;
          const auto uy = length > 1.0e-12 ? gy / length : 0.0;
          std::array<double, 4> sum{};
          for (int sample = 0; sample < p.zoom_blur.samples; ++sample) {
            const auto t = (static_cast<double>(sample) + 0.5) * inv_samples;
            const auto src_x = static_cast<double>(x) - ux * radius_x * t;
            const auto src_y = static_cast<double>(y) - uy * radius_y * t;
            const auto q = bilinear_sample(*pixels, width, height, src_x, src_y, false);
            sum[0] += q.red * inv_samples;
            sum[1] += q.green * inv_samples;
            sum[2] += q.blue * inv_samples;
            sum[3] += q.alpha * inv_samples;
          }
          const auto i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
              static_cast<std::size_t>(x);
          output[i] = straight({static_cast<float>(sum[0]), static_cast<float>(sum[1]),
              static_cast<float>(sum[2]), static_cast<float>(sum[3])});
        }
      }
    } else if (type_id == "spin_blur") {
      const auto center_x = p.spin_blur.center_x * frame_width - 0.5;
      const auto center_y = p.spin_blur.center_y * frame_height - 0.5;
      const auto half_angle = p.spin_blur.angle_degrees * std::numbers::pi / 360.0;
      const auto inv_samples = 1.0 / static_cast<double>(p.spin_blur.samples);
      for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
          if ((x & 31) == 0 && options.cancellation.cancelled()) return cancelled();
          const auto gx = static_cast<double>(options.origin_x + x) - center_x;
          const auto gy = static_cast<double>(options.origin_y + y) - center_y;
          std::array<double, 4> sum{};
          for (int sample = 0; sample < p.spin_blur.samples; ++sample) {
            const auto t = (static_cast<double>(sample) + 0.5) * inv_samples - 0.5;
            const auto angle = t * 2.0 * half_angle;
            const auto c = std::cos(angle);
            const auto s = std::sin(angle);
            const auto src_dx = c * gx + s * gy;
            const auto src_dy = -s * gx + c * gy;
            const auto src_x = center_x + src_dx - options.origin_x;
            const auto src_y = center_y + src_dy - options.origin_y;
            const auto q = bilinear_sample(*pixels, width, height, src_x, src_y, false);
            sum[0] += q.red * inv_samples;
            sum[1] += q.green * inv_samples;
            sum[2] += q.blue * inv_samples;
            sum[3] += q.alpha * inv_samples;
          }
          const auto i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
              static_cast<std::size_t>(x);
          output[i] = straight({static_cast<float>(sum[0]), static_cast<float>(sum[1]),
              static_cast<float>(sum[2]), static_cast<float>(sum[3])});
        }
      }
    } else if (type_id == "surface_blur") {
      const auto radius_x = static_cast<int>(std::ceil(p.surface_blur.radius * sx));
      const auto radius_y = static_cast<int>(std::ceil(p.surface_blur.radius * sy));
      const auto range_variance = 2.0 * p.surface_blur.range_sigma *
          p.surface_blur.range_sigma;
      const auto spatial_variance = 2.0 * p.surface_blur.radius * p.surface_blur.radius;
      for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
          if ((x & 31) == 0 && options.cancellation.cancelled()) return cancelled();
          const auto center_index = static_cast<std::size_t>(y) *
              static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
          const auto& center = (*pixels)[center_index];
          // Straight RGB at zero coverage is undefined. Do not use it as the
          // bilateral range reference or allow nearby visible pixels to turn
          // that hidden color into newly visible output.
          if (center.alpha <= 0.0F) {
            output[center_index] = center;
            continue;
          }
          const std::array<double, 3> center_rgb{center.red, center.green, center.blue};
          std::array<double, 4> sum{};
          double total = 0.0;
          for (int oy = -radius_y; oy <= radius_y; ++oy) {
            const auto syi = sample_coordinate(y + oy, height);
            for (int ox = -radius_x; ox <= radius_x; ++ox) {
              if (((ox + radius_x) & 127) == 0 && options.cancellation.cancelled())
                return cancelled();
              const auto dx_doc = static_cast<double>(ox) / sx;
              const auto dy_doc = static_cast<double>(oy) / sy;
              const auto spatial_sq = dx_doc * dx_doc + dy_doc * dy_doc;
              if (spatial_sq > p.surface_blur.radius * p.surface_blur.radius) continue;
              const auto sxi = sample_coordinate(x + ox, width);
              const auto& q = (*pixels)[static_cast<std::size_t>(syi) *
                  static_cast<std::size_t>(width) + static_cast<std::size_t>(sxi)];
              if (q.alpha <= 0.0F) continue;
              const auto dr = static_cast<double>(q.red) - center_rgb[0];
              const auto dg = static_cast<double>(q.green) - center_rgb[1];
              const auto db = static_cast<double>(q.blue) - center_rgb[2];
              const auto range_sq = dr * dr + dg * dg + db * db;
              const auto weight = std::exp(-spatial_sq / spatial_variance -
                  range_sq / range_variance);
              total += weight;
              sum[0] += q.red * q.alpha * weight;
              sum[1] += q.green * q.alpha * weight;
              sum[2] += q.blue * q.alpha * weight;
              sum[3] += q.alpha * weight;
            }
          }
          output[center_index] = sum[3] > 1.0e-12
              ? ColorPixel{static_cast<float>(sum[0] / sum[3]),
                    static_cast<float>(sum[1] / sum[3]),
                    static_cast<float>(sum[2] / sum[3]),
                    static_cast<float>(sum[3] / total)}
              : center;
        }
      }
    } else if (type_id == "high_pass") {
      std::vector<ColorPixel> blurred;
      const auto sigma_x = p.high_pass.radius * sx;
      const auto sigma_y = p.high_pass.radius * sy;
      if (!gaussian_blur(*pixels, &blurred, width, height, sigma_x, sigma_y,
          support.x, support.y, options.cancellation)) return cancelled();
      const auto max_float = static_cast<double>(std::numeric_limits<float>::max());
      for (std::size_t i = 0; i < count; ++i) {
        if ((i & 4095U) == 0 && options.cancellation.cancelled()) return cancelled();
        const auto src = (*pixels)[i];
        const auto low = straight(blurred[i]);
        const std::array<double, 3> values{
            0.5 + p.high_pass.strength * (static_cast<double>(src.red) - low.red),
            0.5 + p.high_pass.strength * (static_cast<double>(src.green) - low.green),
            0.5 + p.high_pass.strength * (static_cast<double>(src.blue) - low.blue)};
        if (!std::isfinite(values[0]) || !std::isfinite(values[1]) ||
            !std::isfinite(values[2]) || std::abs(values[0]) > max_float ||
            std::abs(values[1]) > max_float || std::abs(values[2]) > max_float)
          return invalid_result("High-pass result is outside finite float range.");
        output[i] = {static_cast<float>(values[0]), static_cast<float>(values[1]),
            static_cast<float>(values[2]), src.alpha};
      }
    } else {
      return {PhotoEffectStatus::Unsupported, "Unsupported optical photo effect type."};
    }

    if (options.cancellation.cancelled()) return cancelled();
    if (!all_pixels_finite(output, options.cancellation)) {
      if (options.cancellation.cancelled()) return cancelled();
      return invalid_result("Optical photo effect produced non-finite RGB or invalid alpha.");
    }
    pixels->swap(output);
    return {PhotoEffectStatus::Applied, {}};
  } catch (const std::bad_alloc&) {
    return {PhotoEffectStatus::ResourceLimit,
        "Could not allocate optical photo effect scratch."};
  }
}

}  // namespace photo_editor::pipeline
