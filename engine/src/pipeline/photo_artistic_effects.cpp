#include "photo_editor/pipeline/photo_artistic_effects.h"

#include "photo_editor/pipeline/photo_effects.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <numbers>
#include <sstream>
#include <string>
#include <utility>

namespace photo_editor::pipeline {
namespace {

constexpr std::string_view kPayloadMagic = "artistic-photo-effect:v1";
constexpr std::size_t kMaxGradientStops = 32U;
constexpr std::size_t kMaxPayloadBytes = 1024U * 1024U;
constexpr int kMaxRadius = 8;
constexpr int kMaxSupport = 12288;
constexpr std::size_t kMaxMapPixels = 268435456U;
constexpr double kMinScale = 1.0 / 4096.0;

using Field = PhotoEffectParameterField;

constexpr std::array<Field, 2> kSolarizationFields{{
    {"threshold", "Threshold", 0.0, 1.0, 0.01},
    {"softness", "Softness", 0.0, 0.5, 0.01}}};
constexpr std::array<Field, 5> kHalftoneFields{{
    {"cell_size", "Cell size", 2.0, 128.0, 0.5},
    {"max_dot", "Maximum dot", 0.25, 1.0, 0.01},
    {"red_angle", "Red screen angle", -180.0, 180.0, 1.0},
    {"green_angle", "Green screen angle", -180.0, 180.0, 1.0},
    {"blue_angle", "Blue screen angle", -180.0, 180.0, 1.0}}};
constexpr std::array<Field, 3> kDitherFields{{
    {"levels", "Tonal levels", 2.0, 16.0, 1.0},
    {"mode", "Mode", 0.0, 1.0, 1.0},
    {"matrix_size", "Bayer matrix", 2.0, 8.0, 2.0}}};
constexpr std::array<Field, 11> kCrosshatchFields{{
    {"spacing", "Spacing", 2.0, 128.0, 0.5},
    {"line_width", "Line width", 0.02, 0.45, 0.01},
    {"ink_strength", "Ink strength", 0.0, 1.0, 0.01},
    {"angle_0", "Hatch angle 1", -180.0, 180.0, 1.0},
    {"angle_1", "Hatch angle 2", -180.0, 180.0, 1.0},
    {"angle_2", "Hatch angle 3", -180.0, 180.0, 1.0},
    {"angle_3", "Hatch angle 4", -180.0, 180.0, 1.0},
    {"threshold_0", "Tone threshold 1", 0.0, 2.0, 0.01},
    {"threshold_1", "Tone threshold 2", 0.0, 2.0, 0.01},
    {"threshold_2", "Tone threshold 3", 0.0, 2.0, 0.01},
    {"threshold_3", "Tone threshold 4", 0.0, 2.0, 0.01}}};
constexpr std::array<Field, 3> kInkFields{{
    {"radius", "Edge radius", 1.0, 4.0, 1.0},
    {"threshold", "Edge threshold", 0.0, 2.0, 0.01},
    {"strength", "Ink strength", 0.0, 1.0, 0.01}}};
constexpr std::array<Field, 4> kWatercolorFields{{
    {"radius", "Wash radius", 1.0, 8.0, 1.0},
    {"edge_darkening", "Edge darkening", 0.0, 1.0, 0.01},
    {"pigment_granulation", "Pigment granulation", 0.0, 1.0, 0.01},
    {"seed", "Texture seed", 0.0, 4294967295.0, 1.0}}};
constexpr std::array<Field, 2> kOilFields{{
    {"radius", "Brush neighborhood", 1.0, 8.0, 1.0},
    {"levels", "Tone bins", 4.0, 64.0, 1.0}}};
constexpr std::array<Field, 4> kMosaicFields{{
    {"cell_size", "Cell size", 2.0, 128.0, 0.5},
    {"shape", "Cell shape", 0.0, 2.0, 1.0},
    {"jitter", "Irregularity", 0.0, 0.35, 0.01},
    {"seed", "Pattern seed", 0.0, 4294967295.0, 1.0}}};
constexpr std::array<Field, 5> kGlitchFields{{
    {"shift_x", "Horizontal split", -128.0, 128.0, 0.25},
    {"shift_y", "Vertical split", -128.0, 128.0, 0.25},
    {"band_height", "Band height", 1.0, 128.0, 0.5},
    {"band_period", "Band spacing", 1.0, 256.0, 0.5},
    {"seed", "Pattern seed", 0.0, 4294967295.0, 1.0}}};
constexpr std::array<Field, 2> kDisplacementFields{{
    {"maximum_displacement", "Maximum displacement", 0.0, 256.0, 0.25},
    {"edge_mode", "Edge mode", 0.0, 1.0, 1.0}}};

void set_error(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
}

bool known_type(std::string_view type_id) {
  return type_id == "gradient_map" || type_id == "solarization" || type_id == "color_halftone" ||
      type_id == "dither" || type_id == "crosshatch" || type_id == "ink_outline" ||
      type_id == "watercolor" || type_id == "oil_paint" || type_id == "pixel_mosaic" ||
      type_id == "rgb_glitch" || type_id == "displacement_map";
}

std::span<const Field> fields_for(std::string_view type_id) {
  if (type_id == "gradient_map") return {};
  if (type_id == "solarization") return kSolarizationFields;
  if (type_id == "color_halftone") return kHalftoneFields;
  if (type_id == "dither") return kDitherFields;
  if (type_id == "crosshatch") return kCrosshatchFields;
  if (type_id == "ink_outline") return kInkFields;
  if (type_id == "watercolor") return kWatercolorFields;
  if (type_id == "oil_paint") return kOilFields;
  if (type_id == "pixel_mosaic") return kMosaicFields;
  if (type_id == "rgb_glitch") return kGlitchFields;
  if (type_id == "displacement_map") return kDisplacementFields;
  return {};
}

bool is_integer_key(std::string_view key) {
  return key == "levels" || key == "mode" || key == "matrix_size" || key == "radius" ||
      key == "seed" || key == "shape" || key == "edge_mode";
}

PhotoEffectParameterValues scalar_values(std::string_view type_id,
    const ArtisticPhotoEffectParameters& p) {
  PhotoEffectParameterValues values;
  const auto put = [&values](std::string key, double value) { values.emplace(std::move(key), value); };
  if (type_id == "solarization") {
    put("threshold", p.solarization.threshold); put("softness", p.solarization.softness);
  } else if (type_id == "color_halftone") {
    put("cell_size", p.color_halftone.cell_size); put("max_dot", p.color_halftone.max_dot);
    put("red_angle", p.color_halftone.angles[0]); put("green_angle", p.color_halftone.angles[1]);
    put("blue_angle", p.color_halftone.angles[2]);
  } else if (type_id == "dither") {
    put("levels", p.dither.levels); put("mode", static_cast<double>(p.dither.mode));
    put("matrix_size", p.dither.matrix_size);
  } else if (type_id == "crosshatch") {
    put("spacing", p.crosshatch.spacing); put("line_width", p.crosshatch.line_width);
    put("ink_strength", p.crosshatch.ink_strength);
    for (std::size_t i = 0; i < p.crosshatch.angles.size(); ++i) {
      put("angle_" + std::to_string(i), p.crosshatch.angles[i]);
      put("threshold_" + std::to_string(i), p.crosshatch.thresholds[i]);
    }
  } else if (type_id == "ink_outline") {
    put("radius", p.ink_outline.radius); put("threshold", p.ink_outline.threshold);
    put("strength", p.ink_outline.strength);
  } else if (type_id == "watercolor") {
    put("radius", p.watercolor.radius); put("edge_darkening", p.watercolor.edge_darkening);
    put("pigment_granulation", p.watercolor.pigment_granulation); put("seed", p.watercolor.seed);
  } else if (type_id == "oil_paint") {
    put("radius", p.oil_paint.radius); put("levels", p.oil_paint.levels);
  } else if (type_id == "pixel_mosaic") {
    put("cell_size", p.pixel_mosaic.cell_size);
    put("shape", static_cast<double>(p.pixel_mosaic.shape));
    put("jitter", p.pixel_mosaic.jitter); put("seed", p.pixel_mosaic.seed);
  } else if (type_id == "rgb_glitch") {
    put("shift_x", p.rgb_glitch.shift_x); put("shift_y", p.rgb_glitch.shift_y);
    put("band_height", p.rgb_glitch.band_height); put("band_period", p.rgb_glitch.band_period);
    put("seed", p.rgb_glitch.seed);
  } else if (type_id == "displacement_map") {
    put("maximum_displacement", p.displacement_map.maximum_displacement);
    put("edge_mode", static_cast<double>(p.displacement_map.edge_mode));
  }
  return values;
}

bool assign_scalar(std::string_view type_id, std::string_view key, double value,
    ArtisticPhotoEffectParameters* p) {
  if (type_id == "solarization") {
    if (key == "threshold") p->solarization.threshold = value;
    else if (key == "softness") p->solarization.softness = value;
    else return false;
  } else if (type_id == "color_halftone") {
    if (key == "cell_size") p->color_halftone.cell_size = value;
    else if (key == "max_dot") p->color_halftone.max_dot = value;
    else if (key == "red_angle") p->color_halftone.angles[0] = value;
    else if (key == "green_angle") p->color_halftone.angles[1] = value;
    else if (key == "blue_angle") p->color_halftone.angles[2] = value;
    else return false;
  } else if (type_id == "dither") {
    if (key == "levels") p->dither.levels = static_cast<int>(value);
    else if (key == "mode") p->dither.mode = static_cast<ArtisticDitherMode>(static_cast<std::uint8_t>(value));
    else if (key == "matrix_size") p->dither.matrix_size = static_cast<int>(value);
    else return false;
  } else if (type_id == "crosshatch") {
    if (key == "spacing") p->crosshatch.spacing = value;
    else if (key == "line_width") p->crosshatch.line_width = value;
    else if (key == "ink_strength") p->crosshatch.ink_strength = value;
    else if (key.starts_with("angle_") && key.size() == 7U && key[6] >= '0' && key[6] <= '3')
      p->crosshatch.angles[static_cast<std::size_t>(key[6] - '0')] = value;
    else if (key.starts_with("threshold_") && key.size() == 11U && key[10] >= '0' && key[10] <= '3')
      p->crosshatch.thresholds[static_cast<std::size_t>(key[10] - '0')] = value;
    else return false;
  } else if (type_id == "ink_outline") {
    if (key == "radius") p->ink_outline.radius = static_cast<int>(value);
    else if (key == "threshold") p->ink_outline.threshold = value;
    else if (key == "strength") p->ink_outline.strength = value;
    else return false;
  } else if (type_id == "watercolor") {
    if (key == "radius") p->watercolor.radius = static_cast<int>(value);
    else if (key == "edge_darkening") p->watercolor.edge_darkening = value;
    else if (key == "pigment_granulation") p->watercolor.pigment_granulation = value;
    else if (key == "seed") p->watercolor.seed = static_cast<std::uint32_t>(value);
    else return false;
  } else if (type_id == "oil_paint") {
    if (key == "radius") p->oil_paint.radius = static_cast<int>(value);
    else if (key == "levels") p->oil_paint.levels = static_cast<int>(value);
    else return false;
  } else if (type_id == "pixel_mosaic") {
    if (key == "cell_size") p->pixel_mosaic.cell_size = value;
    else if (key == "shape") p->pixel_mosaic.shape = static_cast<ArtisticMosaicShape>(static_cast<std::uint8_t>(value));
    else if (key == "jitter") p->pixel_mosaic.jitter = value;
    else if (key == "seed") p->pixel_mosaic.seed = static_cast<std::uint32_t>(value);
    else return false;
  } else if (type_id == "rgb_glitch") {
    if (key == "shift_x") p->rgb_glitch.shift_x = value;
    else if (key == "shift_y") p->rgb_glitch.shift_y = value;
    else if (key == "band_height") p->rgb_glitch.band_height = value;
    else if (key == "band_period") p->rgb_glitch.band_period = value;
    else if (key == "seed") p->rgb_glitch.seed = static_cast<std::uint32_t>(value);
    else return false;
  } else if (type_id == "displacement_map") {
    if (key == "maximum_displacement") p->displacement_map.maximum_displacement = value;
    else if (key == "edge_mode") p->displacement_map.edge_mode = static_cast<ArtisticDisplacementEdgeMode>(static_cast<std::uint8_t>(value));
    else return false;
  } else {
    return false;
  }
  return true;
}

bool valid_stops(const std::vector<ArtisticGradientStop>& stops) {
  if (stops.size() < 2U || stops.size() > kMaxGradientStops) return false;
  double previous = -1.0;
  for (const auto& stop : stops) {
    if (!std::isfinite(stop.position) || stop.position < 0.0 || stop.position > 1.0 ||
        stop.position <= previous) return false;
    previous = stop.position;
    for (const auto channel : stop.linear_rgb)
      if (!std::isfinite(channel) || channel < -16.0F || channel > 64.0F) return false;
  }
  return true;
}

bool is_sha256_asset_ref(std::string_view asset_ref) {
  return asset_ref.size() == 64U && std::all_of(asset_ref.begin(), asset_ref.end(), [](char value) {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
  });
}

bool valid_map_identity(const ArtisticPhotoEffectParameters::DisplacementMap& map) {
  const bool unconfigured = map.asset_ref.empty() && map.width == 0 && map.height == 0 && !map.rgba;
  if (unconfigured) return true;
  if (!is_sha256_asset_ref(map.asset_ref) || map.width <= 0 || map.height <= 0 ||
      static_cast<std::size_t>(map.width) > std::numeric_limits<std::size_t>::max() /
          static_cast<std::size_t>(map.height)) return false;
  const auto count = static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height);
  return count <= kMaxMapPixels;
}

bool valid_parameters(std::string_view type_id, const ArtisticPhotoEffectParameters& p,
    bool require_map_raster, std::string* error) {
  if (!known_type(type_id)) { set_error(error, "Unsupported artistic photo effect type."); return false; }
  if (type_id == "gradient_map" && !valid_stops(p.gradient_map.stops)) {
    set_error(error, "Gradient map requires 2 to 32 ordered finite color stops."); return false;
  }
  if (!valid_map_identity(p.displacement_map)) {
    set_error(error, "Displacement map identity or dimensions are invalid."); return false;
  }
  const auto values = scalar_values(type_id, p);
  for (const auto& field : fields_for(type_id)) {
    const auto found = values.find(std::string(field.key));
    if (found == values.end() || !std::isfinite(found->second) ||
        found->second < field.minimum || found->second > field.maximum ||
        (is_integer_key(field.key) && std::floor(found->second) != found->second)) {
      set_error(error, "Artistic effect parameter is outside its supported range: " + std::string(field.key));
      return false;
    }
  }
  if (type_id == "dither" && p.dither.matrix_size != 2 && p.dither.matrix_size != 4 && p.dither.matrix_size != 8) {
    set_error(error, "Ordered dithering supports Bayer matrices of size 2, 4, or 8."); return false;
  }
  if (type_id == "rgb_glitch" && p.rgb_glitch.band_height > p.rgb_glitch.band_period) {
    set_error(error, "RGB glitch band height must not exceed its period."); return false;
  }
  if (type_id == "pixel_mosaic" && p.pixel_mosaic.shape == ArtisticMosaicShape::IrregularVoronoi &&
      p.pixel_mosaic.jitter <= 0.0) {
    set_error(error, "Irregular Voronoi mosaics require nonzero center jitter."); return false;
  }
  if (type_id == "displacement_map") {
    const auto& map = p.displacement_map;
    if (!valid_map_identity(map) || (map.edge_mode != ArtisticDisplacementEdgeMode::Extend &&
        map.edge_mode != ArtisticDisplacementEdgeMode::Transparent)) {
      set_error(error, "Displacement map parameters are invalid."); return false;
    }
    const bool unconfigured = map.asset_ref.empty() && map.width == 0 && map.height == 0 && !map.rgba;
    if (unconfigured) return true;
    if (!map.rgba) {
      if (require_map_raster) set_error(error, "The referenced displacement map raster has not been resolved.");
      return !require_map_raster;
    }
    const auto count = static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height);
    if (count > std::numeric_limits<std::size_t>::max() / 4U || map.rgba->size() != count * 4U) {
      set_error(error, "Resolved displacement map raster has incorrect dimensions."); return false;
    }
  }
  return true;
}

double smoothstep(double edge0, double edge1, double value) {
  if (edge1 <= edge0) return value < edge0 ? 0.0 : 1.0;
  const auto t = std::clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

double luminance(const ColorPixel& pixel, const std::array<double, 3>& weights) {
  return weights[0] * static_cast<double>(pixel.red) + weights[1] * static_cast<double>(pixel.green) +
      weights[2] * static_cast<double>(pixel.blue);
}

bool valid_luminance_weights(const PhotoEffectRenderOptions& options) {
  double sum = 0.0;
  for (const auto weight : options.luminance_weights) {
    if (!std::isfinite(weight) || std::abs(weight) > 4.0) return false;
    sum += weight;
  }
  return std::abs(sum - 1.0) <= 1.0e-4;
}

std::uint64_t hash_coord(std::uint32_t seed, std::int64_t x, std::int64_t y) {
  auto value = static_cast<std::uint64_t>(seed) ^
      (static_cast<std::uint64_t>(x) * 0x9e3779b185ebca87ULL) ^
      (static_cast<std::uint64_t>(y) * 0xc2b2ae3d27d4eb4fULL);
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

double hash_unit(std::uint32_t seed, std::int64_t x, std::int64_t y, std::uint32_t lane = 0U) {
  constexpr double inverse = 1.0 / 9007199254740992.0;
  return static_cast<double>(hash_coord(seed ^ (0x9e3779b9U * (lane + 1U)), x, y) >> 11U) * inverse;
}

std::int64_t floor_to_i64(double value) {
  constexpr double safe_limit = 8.0e15;
  if (!std::isfinite(value) || value < -safe_limit || value > safe_limit) return 0;
  return static_cast<std::int64_t>(std::floor(value));
}

int clamp_coord(int value, int size) { return std::clamp(value, 0, size - 1); }

ColorPixel read_edge(const std::vector<ColorPixel>& pixels, int width, int height,
    int x, int y, ArtisticDisplacementEdgeMode edge_mode) {
  if (x < 0 || y < 0 || x >= width || y >= height) {
    if (edge_mode == ArtisticDisplacementEdgeMode::Transparent) return {0.0F, 0.0F, 0.0F, 0.0F};
    x = clamp_coord(x, width); y = clamp_coord(y, height);
  }
  return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
      static_cast<std::size_t>(x)];
}

ColorPixel sample_premultiplied(const std::vector<ColorPixel>& pixels, int width, int height,
    double x, double y, ArtisticDisplacementEdgeMode edge_mode) {
  if (!std::isfinite(x) || !std::isfinite(y)) return {0.0F, 0.0F, 0.0F, 0.0F};
  const auto x0d = std::floor(x); const auto y0d = std::floor(y);
  if (std::abs(x0d) > 8.0e15 || std::abs(y0d) > 8.0e15)
    return {0.0F, 0.0F, 0.0F, 0.0F};
  const auto x0 = static_cast<int>(std::clamp(x0d, -1.0, static_cast<double>(width)));
  const auto y0 = static_cast<int>(std::clamp(y0d, -1.0, static_cast<double>(height)));
  const auto fx = std::clamp(x - x0d, 0.0, 1.0);
  const auto fy = std::clamp(y - y0d, 0.0, 1.0);
  const std::array<ColorPixel, 4> samples{{
      read_edge(pixels, width, height, x0, y0, edge_mode),
      read_edge(pixels, width, height, x0 + 1, y0, edge_mode),
      read_edge(pixels, width, height, x0, y0 + 1, edge_mode),
      read_edge(pixels, width, height, x0 + 1, y0 + 1, edge_mode)}};
  const auto interpolate = [fx, fy](double a, double b, double c, double d) {
    return std::lerp(std::lerp(a, b, fx), std::lerp(c, d, fx), fy);
  };
  const auto alpha = interpolate(samples[0].alpha, samples[1].alpha, samples[2].alpha, samples[3].alpha);
  if (alpha <= 1.0e-12) return {0.0F, 0.0F, 0.0F, 0.0F};
  const auto channel = [&](auto member) {
    return static_cast<float>(interpolate(
        static_cast<double>(samples[0].*member) * samples[0].alpha,
        static_cast<double>(samples[1].*member) * samples[1].alpha,
        static_cast<double>(samples[2].*member) * samples[2].alpha,
        static_cast<double>(samples[3].*member) * samples[3].alpha) / alpha);
  };
  return {channel(&ColorPixel::red), channel(&ColorPixel::green), channel(&ColorPixel::blue),
      static_cast<float>(std::clamp(alpha, 0.0, 1.0))};
}

bool cancelled(const PhotoEffectRenderOptions& options) { return options.cancellation.cancelled(); }

PhotoEffectResult validate_frame(const std::vector<ColorPixel>* pixels, int width, int height,
    const PhotoEffectRenderOptions& options) {
  if (pixels == nullptr || width <= 0 || height <= 0 ||
      width > std::numeric_limits<int>::max() - kMaxSupport ||
      height > std::numeric_limits<int>::max() - kMaxSupport ||
      static_cast<std::size_t>(width) > std::numeric_limits<std::size_t>::max() /
          static_cast<std::size_t>(height) || pixels->size() !=
          static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
    return {PhotoEffectStatus::Invalid, "Invalid artistic photo effect raster dimensions."};
  if (!std::isfinite(options.scale_x) || !std::isfinite(options.scale_y) ||
      options.scale_x < kMinScale || options.scale_y < kMinScale ||
      options.frame_width < 0 || options.frame_height < 0 ||
      (options.frame_width == 0) != (options.frame_height == 0) ||
      options.origin_x < -kMaxSupport || options.origin_y < -kMaxSupport ||
      options.origin_x > std::numeric_limits<int>::max() - width - kMaxSupport ||
      options.origin_y > std::numeric_limits<int>::max() - height - kMaxSupport ||
      !valid_luminance_weights(options))
    return {PhotoEffectStatus::Invalid, "Artistic effect scale or full-frame dimensions are invalid."};
  if (cancelled(options)) return {PhotoEffectStatus::Cancelled, "Artistic photo effect cancelled."};
  for (std::size_t i = 0; i < pixels->size(); ++i) {
    if ((i & 4095U) == 0U && cancelled(options))
      return {PhotoEffectStatus::Cancelled, "Artistic photo effect cancelled."};
    const auto& pixel = (*pixels)[i];
    if (!std::isfinite(pixel.red) || !std::isfinite(pixel.green) || !std::isfinite(pixel.blue) ||
        !std::isfinite(pixel.alpha) || pixel.alpha < 0.0F || pixel.alpha > 1.0F)
      return {PhotoEffectStatus::Invalid, "Artistic effect requires finite RGB and unit alpha."};
  }
  return {PhotoEffectStatus::Applied, {}};
}

bool add_support(double extent_x, double extent_y, PhotoEffectSupport* support) {
  if (!std::isfinite(extent_x) || !std::isfinite(extent_y) || extent_x < 0.0 || extent_y < 0.0 ||
      extent_x > kMaxSupport || extent_y > kMaxSupport) return false;
  support->x = static_cast<int>(std::ceil(extent_x));
  support->y = static_cast<int>(std::ceil(extent_y));
  return true;
}

std::array<double, 3> gradient_sample(const std::vector<ArtisticGradientStop>& stops, double t) {
  if (t <= stops.front().position) {
    const auto& c = stops.front().linear_rgb;
    return {c[0], c[1], c[2]};
  }
  if (t >= stops.back().position) {
    const auto& c = stops.back().linear_rgb;
    return {c[0], c[1], c[2]};
  }
  const auto upper = std::upper_bound(stops.begin(), stops.end(), t,
      [](double value, const ArtisticGradientStop& stop) { return value < stop.position; });
  const auto& right = *upper;
  const auto& left = *(upper - 1);
  const auto fraction = (t - left.position) / (right.position - left.position);
  return {std::lerp(static_cast<double>(left.linear_rgb[0]), static_cast<double>(right.linear_rgb[0]), fraction),
      std::lerp(static_cast<double>(left.linear_rgb[1]), static_cast<double>(right.linear_rgb[1]), fraction),
      std::lerp(static_cast<double>(left.linear_rgb[2]), static_cast<double>(right.linear_rgb[2]), fraction)};
}

int bayer_value(int x, int y, int size) {
  if (size == 2) {
    constexpr int base[2][2]{{0, 2}, {3, 1}};
    return base[y & 1][x & 1];
  }
  const auto half = size / 2;
  const auto quadrant = (y >= half ? (x >= half ? 1 : 3) : (x >= half ? 2 : 0));
  constexpr int offsets[4]{0, 2, 3, 1};
  return 4 * bayer_value(x % half, y % half, half) + offsets[quadrant];
}

double screen_distance(double coordinate) {
  const auto phase = coordinate - std::floor(coordinate);
  return std::abs(phase - 0.5) * 2.0;
}

std::int64_t nearest_int64(double value) {
  constexpr double safe_limit = 8.0e15;
  if (!std::isfinite(value) || value < -safe_limit || value > safe_limit) return 0;
  return static_cast<std::int64_t>(std::llround(value));
}

ColorPixel nearest_pixel(const std::vector<ColorPixel>& input, int width, int height,
    double x, double y) {
  const auto px = static_cast<int>(std::clamp<std::int64_t>(nearest_int64(x), -width, 2LL * width));
  const auto py = static_cast<int>(std::clamp<std::int64_t>(nearest_int64(y), -height, 2LL * height));
  return read_edge(input, width, height, px, py,
      ArtisticDisplacementEdgeMode::Extend);
}

std::pair<double, double> mosaic_center(double doc_x, double doc_y,
    const ArtisticPhotoEffectParameters::PixelMosaic& mosaic) {
  const auto cell = mosaic.cell_size;
  if (mosaic.shape == ArtisticMosaicShape::Square) {
    return {(std::floor(doc_x / cell) + 0.5) * cell,
        (std::floor(doc_y / cell) + 0.5) * cell};
  }
  if (mosaic.shape == ArtisticMosaicShape::Hexagonal) {
    constexpr double root3over2 = 0.86602540378443864676;
    const auto row_step = cell * 0.75;
    const auto col_step = cell * root3over2;
    const auto row0 = nearest_int64(doc_y / row_step);
    double best_distance = std::numeric_limits<double>::infinity();
    std::pair<double, double> best{};
    for (auto row = row0 - 1; row <= row0 + 1; ++row) {
      const auto shift = (row & 1LL) != 0 ? col_step * 0.5 : 0.0;
      const auto col0 = nearest_int64((doc_x - shift) / col_step);
      for (auto col = col0 - 1; col <= col0 + 1; ++col) {
        const auto cx = static_cast<double>(col) * col_step + shift;
        const auto cy = static_cast<double>(row) * row_step;
        const auto dx = cx - doc_x;
        const auto dy = cy - doc_y;
        const auto distance = dx * dx + dy * dy;
        if (distance < best_distance) { best_distance = distance; best = {cx, cy}; }
      }
    }
    return best;
  }
  const auto gx0 = floor_to_i64(doc_x / cell);
  const auto gy0 = floor_to_i64(doc_y / cell);
  double best_distance = std::numeric_limits<double>::infinity();
  std::pair<double, double> best{};
  for (auto gy = gy0 - 1; gy <= gy0 + 1; ++gy) {
    for (auto gx = gx0 - 1; gx <= gx0 + 1; ++gx) {
      const auto cx = (static_cast<double>(gx) + 0.5 +
          (hash_unit(mosaic.seed, gx, gy, 0U) * 2.0 - 1.0) * mosaic.jitter) * cell;
      const auto cy = (static_cast<double>(gy) + 0.5 +
          (hash_unit(mosaic.seed, gx, gy, 1U) * 2.0 - 1.0) * mosaic.jitter) * cell;
      const auto dx = cx - doc_x;
      const auto dy = cy - doc_y;
      const auto distance = dx * dx + dy * dy;
      if (distance < best_distance) { best_distance = distance; best = {cx, cy}; }
    }
  }
  return best;
}

std::array<double, 3> neighborhood_average(const std::vector<ColorPixel>& input, int width, int height,
    int center_x, int center_y, int radius_x, int radius_y, double sigma_doc,
    double scale_x, double scale_y, bool watercolor, std::uint32_t seed,
    std::int64_t global_x, std::int64_t global_y, double edge_darkening, double granulation,
    const std::array<double, 3>& luminance_weights) {
  const auto& center = input[static_cast<std::size_t>(center_y) * static_cast<std::size_t>(width) +
      static_cast<std::size_t>(center_x)];
  const auto center_luma = luminance(center, luminance_weights);
  std::array<double, 3> sum{};
  double total = 0.0;
  double edge = 0.0;
  for (int dy = -radius_y; dy <= radius_y; ++dy) {
    for (int dx = -radius_x; dx <= radius_x; ++dx) {
      const auto sx = std::clamp(center_x + dx, 0, width - 1);
      const auto sy = std::clamp(center_y + dy, 0, height - 1);
      const auto& sample = input[static_cast<std::size_t>(sy) * static_cast<std::size_t>(width) +
          static_cast<std::size_t>(sx)];
      if (sample.alpha <= 0.0F) continue;
      const auto doc_dx = static_cast<double>(dx) / scale_x;
      const auto doc_dy = static_cast<double>(dy) / scale_y;
      const auto dist2 = doc_dx * doc_dx + doc_dy * doc_dy;
      auto weight = std::exp(-dist2 / (2.0 * sigma_doc * sigma_doc));
      const auto delta = luminance(sample, luminance_weights) - center_luma;
      if (watercolor) {
        weight *= std::exp(-(delta * delta) / (2.0 * 0.11 * 0.11));
        if (std::abs(dx) + std::abs(dy) == 1) edge = std::max(edge, std::abs(delta));
      }
      weight *= sample.alpha;
      total += weight;
      sum[0] += static_cast<double>(sample.red) * weight;
      sum[1] += static_cast<double>(sample.green) * weight;
      sum[2] += static_cast<double>(sample.blue) * weight;
    }
  }
  if (total <= 1.0e-12) return {center.red, center.green, center.blue};
  for (auto& channel : sum) channel /= total;
  if (watercolor) {
    const auto darken = 1.0 - edge_darkening * smoothstep(0.035, 0.32, edge);
    const auto noise = (hash_unit(seed, global_x, global_y) * 2.0 - 1.0) * granulation * 0.025;
    for (auto& channel : sum) channel = channel * darken + noise;
  }
  return sum;
}

std::array<double, 3> oil_paint_average(const std::vector<ColorPixel>& input, int width, int height,
    int center_x, int center_y, int radius_x, int radius_y, int levels,
    const std::array<double, 3>& luminance_weights) {
  std::array<int, 64> counts{};
  std::array<std::array<double, 3>, 64> sums{};
  std::array<double, 64> alpha_sums{};
  for (int dy = -radius_y; dy <= radius_y; ++dy) {
    for (int dx = -radius_x; dx <= radius_x; ++dx) {
      const auto sx = std::clamp(center_x + dx, 0, width - 1);
      const auto sy = std::clamp(center_y + dy, 0, height - 1);
      const auto& pixel = input[static_cast<std::size_t>(sy) * static_cast<std::size_t>(width) +
          static_cast<std::size_t>(sx)];
      if (pixel.alpha <= 0.0F) continue;
      const auto tone = std::clamp(luminance(pixel, luminance_weights), 0.0, 1.0);
      const auto bin = std::clamp(static_cast<int>(tone * levels), 0, levels - 1);
      const auto index = static_cast<std::size_t>(bin);
      ++counts[index];
      alpha_sums[index] += pixel.alpha;
      sums[index][0] += static_cast<double>(pixel.red) * pixel.alpha;
      sums[index][1] += static_cast<double>(pixel.green) * pixel.alpha;
      sums[index][2] += static_cast<double>(pixel.blue) * pixel.alpha;
    }
  }
  const auto modal = static_cast<std::size_t>(std::distance(counts.begin(),
      std::max_element(counts.begin(), counts.begin() + levels)));
  if (counts[modal] == 0 || alpha_sums[modal] <= 1.0e-12)
    return {input[static_cast<std::size_t>(center_y) * static_cast<std::size_t>(width) +
        static_cast<std::size_t>(center_x)].red,
        input[static_cast<std::size_t>(center_y) * static_cast<std::size_t>(width) +
        static_cast<std::size_t>(center_x)].green,
        input[static_cast<std::size_t>(center_y) * static_cast<std::size_t>(width) +
        static_cast<std::size_t>(center_x)].blue};
  return {sums[modal][0] / alpha_sums[modal], sums[modal][1] / alpha_sums[modal],
      sums[modal][2] / alpha_sums[modal]};
}

bool displacement_sample(const ArtisticPhotoEffectParameters::DisplacementMap& map,
    int x, int y, int origin_x, int origin_y, int frame_width, int frame_height,
    std::array<double, 2>* channels) {
  const auto global_x = origin_x + x;
  const auto global_y = origin_y + y;
  const auto map_x = std::clamp((static_cast<double>(global_x) + 0.5) * map.width /
      frame_width - 0.5, 0.0, static_cast<double>(map.width - 1));
  const auto map_y = std::clamp((static_cast<double>(global_y) + 0.5) * map.height /
      frame_height - 0.5, 0.0, static_cast<double>(map.height - 1));
  const auto x0 = static_cast<int>(std::floor(map_x));
  const auto y0 = static_cast<int>(std::floor(map_y));
  const auto x1 = std::min(x0 + 1, map.width - 1);
  const auto y1 = std::min(y0 + 1, map.height - 1);
  const auto fraction_x = map_x - x0;
  const auto fraction_y = map_y - y0;
  const auto texel = [&](int map_pixel_x, int map_pixel_y, std::array<double, 2>* value) {
    const auto offset = (static_cast<std::size_t>(map_pixel_y) * static_cast<std::size_t>(map.width) +
        static_cast<std::size_t>(map_pixel_x)) * 4U;
    const auto red = static_cast<double>((*map.rgba)[offset]);
    const auto green = static_cast<double>((*map.rgba)[offset + 1U]);
    const auto blue = static_cast<double>((*map.rgba)[offset + 2U]);
    const auto alpha = static_cast<double>((*map.rgba)[offset + 3U]);
    if (!std::isfinite(red) || !std::isfinite(green) || !std::isfinite(blue) || !std::isfinite(alpha) ||
        red < 0.0 || red > 1.0 || green < 0.0 || green > 1.0 || blue < 0.0 || blue > 1.0 ||
        alpha < 0.0 || alpha > 1.0) return false;
    // Apply alpha in displacement space before resampling so RGB from transparent
    // map pixels cannot leak into neighboring samples.
    *value = {std::lerp(0.5, red, alpha), std::lerp(0.5, green, alpha)};
    return true;
  };
  std::array<double, 2> top_left{};
  std::array<double, 2> top_right{};
  std::array<double, 2> bottom_left{};
  std::array<double, 2> bottom_right{};
  if (!texel(x0, y0, &top_left) || !texel(x1, y0, &top_right) ||
      !texel(x0, y1, &bottom_left) || !texel(x1, y1, &bottom_right)) return false;
  for (std::size_t channel = 0; channel < channels->size(); ++channel) {
    const auto top = std::lerp(top_left[channel], top_right[channel], fraction_x);
    const auto bottom = std::lerp(bottom_left[channel], bottom_right[channel], fraction_x);
    (*channels)[channel] = std::lerp(top, bottom, fraction_y);
  }
  return true;
}

}  // namespace

std::span<const PhotoEffectParameterField> artistic_photo_effect_fields(std::string_view type_id) {
  return fields_for(type_id);
}

PhotoEffectParameterValues artistic_photo_effect_values(std::string_view type_id,
    const ArtisticPhotoEffectParameters& parameters) {
  return scalar_values(type_id, parameters);
}

bool set_artistic_photo_effect_values(std::string_view type_id,
    const PhotoEffectParameterValues& values, ArtisticPhotoEffectParameters* parameters,
    std::string* error) {
  if (error != nullptr) error->clear();
  if (parameters == nullptr || !known_type(type_id)) {
    set_error(error, "Unknown artistic effect or missing parameter output."); return false;
  }
  const auto fields = fields_for(type_id);
  ArtisticPhotoEffectParameters candidate = *parameters;
  for (const auto& [key, value] : values) {
    const auto field = std::find_if(fields.begin(), fields.end(), [&](const Field& item) { return item.key == key; });
    if (field == fields.end() || !std::isfinite(value) || value < field->minimum || value > field->maximum ||
        (is_integer_key(key) && std::floor(value) != value) || !assign_scalar(type_id, key, value, &candidate)) {
      set_error(error, "Unknown or invalid artistic effect parameter: " + key); return false;
    }
  }
  if (!valid_parameters(type_id, candidate, false, error)) return false;
  *parameters = std::move(candidate);
  return true;
}

std::string serialize_artistic_photo_effect_parameters(std::string_view type_id,
    const ArtisticPhotoEffectParameters& parameters) {
  if (!valid_parameters(type_id, parameters, false, nullptr)) return {};
  const auto values = scalar_values(type_id, parameters);
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(std::numeric_limits<double>::max_digits10)
      << kPayloadMagic << ' ' << values.size();
  for (const auto& [key, value] : values) stream << ' ' << std::quoted(key) << ' ' << value;
  static const std::vector<ArtisticGradientStop> no_stops;
  const auto& stops = type_id == "gradient_map" ? parameters.gradient_map.stops : no_stops;
  stream << ' ' << stops.size();
  for (const auto& stop : stops)
    stream << ' ' << stop.position << ' ' << stop.linear_rgb[0] << ' ' << stop.linear_rgb[1] << ' '
           << stop.linear_rgb[2];
  const auto& map = parameters.displacement_map;
  stream << ' ' << std::quoted(type_id == "displacement_map" ? map.asset_ref : std::string{})
      << ' ' << (type_id == "displacement_map" ? map.width : 0)
      << ' ' << (type_id == "displacement_map" ? map.height : 0);
  const auto result = stream.str();
  return result.size() <= kMaxPayloadBytes ? result : std::string{};
}

bool deserialize_artistic_photo_effect_parameters(std::string_view type_id,
    std::string_view payload, ArtisticPhotoEffectParameters* parameters, std::string* error) {
  if (error != nullptr) error->clear();
  if (parameters == nullptr || !known_type(type_id) || payload.empty() || payload.size() > kMaxPayloadBytes) {
    set_error(error, "Invalid artistic effect payload or output."); return false;
  }
  std::istringstream stream{std::string(payload)};
  stream.imbue(std::locale::classic());
  std::string magic;
  std::size_t field_count = 0;
  if (!(stream >> magic >> field_count) || magic != kPayloadMagic || field_count > 64U) {
    set_error(error, "Malformed artistic effect payload header."); return false;
  }
  PhotoEffectParameterValues values;
  for (std::size_t i = 0; i < field_count; ++i) {
    std::string key;
    double value = 0.0;
    if (!(stream >> std::quoted(key) >> value) || !values.emplace(std::move(key), value).second) {
      set_error(error, "Malformed or duplicate artistic effect field."); return false;
    }
  }
  std::size_t stop_count = 0;
  if (!(stream >> stop_count) || stop_count > kMaxGradientStops ||
      (type_id == "gradient_map" ? stop_count < 2U : stop_count != 0U)) {
    set_error(error, "Malformed artistic gradient-stop count."); return false;
  }
  std::vector<ArtisticGradientStop> stops;
  stops.reserve(stop_count);
  for (std::size_t i = 0; i < stop_count; ++i) {
    ArtisticGradientStop stop;
    if (!(stream >> stop.position >> stop.linear_rgb[0] >> stop.linear_rgb[1] >> stop.linear_rgb[2])) {
      set_error(error, "Malformed artistic gradient stop."); return false;
    }
    stops.push_back(stop);
  }
  std::string asset_ref;
  int map_width = 0;
  int map_height = 0;
  if (!(stream >> std::quoted(asset_ref) >> map_width >> map_height)) {
    set_error(error, "Malformed artistic displacement map identity."); return false;
  }
  stream >> std::ws;
  if (!stream.eof()) { set_error(error, "Trailing bytes in artistic effect payload."); return false; }

  ArtisticPhotoEffectParameters candidate;
  const auto expected_fields = scalar_values(type_id, candidate);
  if (values.size() != expected_fields.size()) {
    set_error(error, "Artistic effect payload has missing or unexpected fields."); return false;
  }
  if (!set_artistic_photo_effect_values(type_id, values, &candidate, error)) return false;
  if (type_id == "gradient_map") candidate.gradient_map.stops = std::move(stops);
  if (type_id == "displacement_map") {
    candidate.displacement_map.asset_ref = std::move(asset_ref);
    candidate.displacement_map.width = map_width;
    candidate.displacement_map.height = map_height;
  } else if (!asset_ref.empty() || map_width != 0 || map_height != 0) {
    set_error(error, "Unexpected displacement map identity in artistic effect payload."); return false;
  }
  if (!valid_parameters(type_id, candidate, false, error)) return false;
  *parameters = std::move(candidate);
  return true;
}

bool artistic_photo_effect_requires_full_frame(std::string_view type_id,
    const ArtisticPhotoEffectParameters& parameters) noexcept {
  return type_id == "dither" && parameters.dither.mode == ArtisticDitherMode::FloydSteinberg;
}

PhotoEffectResult artistic_photo_effect_support(std::string_view type_id,
    const ArtisticPhotoEffectParameters& parameters, const PhotoEffectRenderOptions& options,
    PhotoEffectSupport* support) {
  if (support == nullptr) return {PhotoEffectStatus::Invalid, "Missing artistic effect support output."};
  support->x = 0; support->y = 0;
  if (!known_type(type_id)) return {PhotoEffectStatus::Unsupported, "Unknown artistic photo effect type."};
  if (type_id == "displacement_map" && parameters.displacement_map.asset_ref.empty() &&
      parameters.displacement_map.width == 0 && parameters.displacement_map.height == 0 &&
      !parameters.displacement_map.rgba) return {PhotoEffectStatus::Identity, {}};
  std::string validation_error;
  // Persistent settings can be checked without resolving the managed map asset.
  // The renderer below refuses to run until the immutable raster is present.
  if (!valid_parameters(type_id, parameters, false, &validation_error))
    return {PhotoEffectStatus::Invalid, validation_error.empty() ? "Invalid artistic photo effect parameters." : validation_error};
  if (!std::isfinite(options.scale_x) || !std::isfinite(options.scale_y) ||
      options.scale_x < kMinScale || options.scale_y < kMinScale ||
      options.frame_width < 0 || options.frame_height < 0 ||
      ((options.frame_width == 0) != (options.frame_height == 0)) ||
      !valid_luminance_weights(options))
    return {PhotoEffectStatus::Invalid, "Artistic effect scale or frame dimensions are invalid."};
  if (artistic_photo_effect_requires_full_frame(type_id, parameters)) {
    // Actual frame dimensions are checked by apply; the origin is enough to
    // reject a causal tile during the earlier support-planning phase.
    if (options.origin_x != 0 || options.origin_y != 0)
      return {PhotoEffectStatus::Unsupported, "Floyd-Steinberg error diffusion requires the full raster from its first pixel."};
  }
  if (type_id == "displacement_map") {
    const auto& map = parameters.displacement_map;
    if (options.frame_width > 0 && (options.origin_x < 0 || options.origin_y < 0 ||
        options.origin_x >= options.frame_width || options.origin_y >= options.frame_height))
      return {PhotoEffectStatus::Invalid, "Displacement tile origin lies outside the full document raster."};
    if (!map.rgba) {
      const auto x_extent = std::ceil(map.maximum_displacement * options.scale_x) + 1.0;
      const auto y_extent = std::ceil(map.maximum_displacement * options.scale_y) + 1.0;
      if (!add_support(x_extent, y_extent, support))
        return {PhotoEffectStatus::ResourceLimit, "Displacement support exceeds the configured bound."};
      return {PhotoEffectStatus::Applied, {}};
    }
    const auto x_extent = std::ceil(map.maximum_displacement * options.scale_x) + 1.0;
    const auto y_extent = std::ceil(map.maximum_displacement * options.scale_y) + 1.0;
    if (!add_support(x_extent, y_extent, support))
      return {PhotoEffectStatus::ResourceLimit, "Displacement support exceeds the configured bound."};
    return {PhotoEffectStatus::Applied, {}};
  }
  double extent_x = 0.0;
  double extent_y = 0.0;
  if (type_id == "watercolor") {
    extent_x = (parameters.watercolor.radius + 1.0) * options.scale_x;
    extent_y = (parameters.watercolor.radius + 1.0) * options.scale_y;
  } else if (type_id == "oil_paint") {
    extent_x = parameters.oil_paint.radius * options.scale_x;
    extent_y = parameters.oil_paint.radius * options.scale_y;
  } else if (type_id == "ink_outline") {
    extent_x = (parameters.ink_outline.radius + 1.0) * options.scale_x;
    extent_y = (parameters.ink_outline.radius + 1.0) * options.scale_y;
  } else if (type_id == "pixel_mosaic") {
    const auto multiplier = parameters.pixel_mosaic.shape == ArtisticMosaicShape::IrregularVoronoi
        ? std::numbers::sqrt2 * (0.5 + parameters.pixel_mosaic.jitter) : 1.0;
    extent_x = parameters.pixel_mosaic.cell_size * multiplier * options.scale_x + 1.0;
    extent_y = parameters.pixel_mosaic.cell_size * multiplier * options.scale_y + 1.0;
  } else if (type_id == "rgb_glitch") {
    extent_x = std::abs(parameters.rgb_glitch.shift_x) * options.scale_x + 1.0;
    extent_y = std::abs(parameters.rgb_glitch.shift_y) * options.scale_y + 1.0;
  }
  if ((type_id == "watercolor" &&
       (std::ceil(parameters.watercolor.radius * options.scale_x) > kMaxRadius ||
        std::ceil(parameters.watercolor.radius * options.scale_y) > kMaxRadius)) ||
      (type_id == "oil_paint" &&
       (std::ceil(parameters.oil_paint.radius * options.scale_x) > kMaxRadius ||
        std::ceil(parameters.oil_paint.radius * options.scale_y) > kMaxRadius)))
    return {PhotoEffectStatus::ResourceLimit, "Artistic neighborhood radius exceeds the CPU sampling bound."};
  if (!add_support(extent_x, extent_y, support))
    return {PhotoEffectStatus::ResourceLimit, "Artistic effect support exceeds the configured bound."};
  return {PhotoEffectStatus::Applied, {}};
}

PhotoEffectResult apply_artistic_photo_effect(std::vector<ColorPixel>* pixels, int width, int height,
    std::string_view type_id, const ArtisticPhotoEffectParameters& parameters,
    const PhotoEffectRenderOptions& options) {
  auto result = validate_frame(pixels, width, height, options);
  if (!result.ok()) return result;
  PhotoEffectSupport support;
  result = artistic_photo_effect_support(type_id, parameters, options, &support);
  if (!result.ok() || result.status == PhotoEffectStatus::Identity) return result;
  if (type_id == "dither" && parameters.dither.mode == ArtisticDitherMode::FloydSteinberg) {
    const auto frame_width = options.frame_width > 0 ? options.frame_width : width;
    const auto frame_height = options.frame_height > 0 ? options.frame_height : height;
    if (options.origin_x != 0 || options.origin_y != 0 || frame_width != width || frame_height != height)
      return {PhotoEffectStatus::Unsupported, "Floyd-Steinberg error diffusion requires the full raster from its first pixel."};
  }
  if (type_id == "displacement_map") {
    if (!parameters.displacement_map.asset_ref.empty() && !parameters.displacement_map.rgba)
      return {PhotoEffectStatus::Invalid, "The referenced displacement map raster has not been resolved."};
    const auto frame_width = options.frame_width > 0 ? options.frame_width : width;
    const auto frame_height = options.frame_height > 0 ? options.frame_height : height;
    if (options.origin_x < 0 || options.origin_y < 0 ||
        options.origin_x > frame_width - width || options.origin_y > frame_height - height)
      return {PhotoEffectStatus::Invalid, "Displacement tile extends outside the full document raster."};
  }
  const auto count = pixels->size();
  const auto line_scratch = type_id == "dither" && parameters.dither.mode == ArtisticDitherMode::FloydSteinberg
      ? (static_cast<std::size_t>(width) + 2U) * 2U * 3U * sizeof(double) : 0U;
  if (count > (options.max_scratch_bytes >= line_scratch ?
      (options.max_scratch_bytes - line_scratch) / sizeof(ColorPixel) : 0U))
    return {PhotoEffectStatus::ResourceLimit, "Artistic effect exceeds the configured scratch budget."};

  // Keep the caller's raster read-only until the fully prepared output commits.
  // This alias avoids a second full-size input allocation for every operator.
  const auto& input = *pixels;
  std::vector<ColorPixel> output;
  try { output = input; }
  catch (const std::bad_alloc&) { return {PhotoEffectStatus::ResourceLimit, "Could not allocate artistic effect output."}; }
  const auto max_float = static_cast<double>(std::numeric_limits<float>::max());
  const auto doc_x_at = [&](int x) { return (static_cast<double>(options.origin_x + x) + 0.5) / options.scale_x; };
  const auto doc_y_at = [&](int y) { return (static_cast<double>(options.origin_y + y) + 0.5) / options.scale_y; };
  const auto valid_result_color = [max_float](const std::array<double, 3>& color) {
    return std::all_of(color.begin(), color.end(), [max_float](double value) {
      return std::isfinite(value) && std::abs(value) <= max_float;
    });
  };
  const auto write_color = [&](std::size_t i, const std::array<double, 3>& color, float alpha) {
    if (!valid_result_color(color)) return false;
    output[i] = {static_cast<float>(color[0]), static_cast<float>(color[1]),
        static_cast<float>(color[2]), alpha};
    return true;
  };
  std::array<double, 3> halftone_cos{};
  std::array<double, 3> halftone_sin{};
  for (std::size_t channel = 0; channel < halftone_cos.size(); ++channel) {
    const auto radians = parameters.color_halftone.angles[channel] * std::numbers::pi / 180.0;
    halftone_cos[channel] = std::cos(radians);
    halftone_sin[channel] = std::sin(radians);
  }
  std::array<double, 4> hatch_cos{};
  std::array<double, 4> hatch_sin{};
  for (std::size_t line = 0; line < hatch_cos.size(); ++line) {
    const auto radians = parameters.crosshatch.angles[line] * std::numbers::pi / 180.0;
    hatch_cos[line] = std::cos(radians);
    hatch_sin[line] = std::sin(radians);
  }

  if (type_id == "dither" && parameters.dither.mode == ArtisticDitherMode::FloydSteinberg) {
    std::vector<std::array<double, 3>> current;
    std::vector<std::array<double, 3>> next;
    try { current.resize(static_cast<std::size_t>(width) + 2U); next.resize(current.size()); }
    catch (const std::bad_alloc&) { return {PhotoEffectStatus::ResourceLimit, "Could not allocate dither error rows."}; }
    const auto levels = static_cast<double>(parameters.dither.levels - 1);
    for (int y = 0; y < height; ++y) {
      if (cancelled(options)) return {PhotoEffectStatus::Cancelled, "Artistic dither cancelled."};
      std::fill(next.begin(), next.end(), std::array<double, 3>{});
      for (int x = 0; x < width; ++x) {
        const auto i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
        const auto& source = input[i];
        if (source.alpha <= 0.0F) {
          current[static_cast<std::size_t>(x) + 1U] = {};
          continue;
        }
        const std::array<double, 3> channels{source.red, source.green, source.blue};
        std::array<double, 3> quantized{};
        for (std::size_t c = 0; c < 3U; ++c) {
          const auto adjusted = std::clamp(channels[c], 0.0, 1.0) + current[static_cast<std::size_t>(x) + 1U][c];
          quantized[c] = std::round(std::clamp(adjusted, 0.0, 1.0) * levels) / levels;
          const auto error = adjusted - quantized[c];
          if (x + 1 < width) current[static_cast<std::size_t>(x) + 2U][c] += error * (7.0 / 16.0);
          if (x > 0) next[static_cast<std::size_t>(x)][c] += error * (3.0 / 16.0);
          next[static_cast<std::size_t>(x) + 1U][c] += error * (5.0 / 16.0);
          if (x + 1 < width) next[static_cast<std::size_t>(x) + 2U][c] += error * (1.0 / 16.0);
        }
        if (source.alpha > 0.0F && !write_color(i, quantized, source.alpha))
          return {PhotoEffectStatus::Invalid, "Dither output exceeds finite color range."};
      }
      current.swap(next);
    }
    pixels->swap(output);
    return {PhotoEffectStatus::Applied, {}};
  }

  for (int y = 0; y < height; ++y) {
    if (cancelled(options)) return {PhotoEffectStatus::Cancelled, "Artistic photo effect cancelled."};
    for (int x = 0; x < width; ++x) {
      if ((x & 63) == 0 && cancelled(options)) return {PhotoEffectStatus::Cancelled, "Artistic photo effect cancelled."};
      const auto i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
      const auto& source = input[i];
      if (source.alpha <= 0.0F && type_id != "pixel_mosaic" && type_id != "displacement_map") continue;
      const std::array<double, 3> original{source.red, source.green, source.blue};
      std::array<double, 3> filtered = original;
      const auto luma = luminance(source, options.luminance_weights);
      if (type_id == "gradient_map") {
        filtered = gradient_sample(parameters.gradient_map.stops, std::clamp(luma, 0.0, 1.0));
      } else if (type_id == "solarization") {
        const auto& solar = parameters.solarization;
        const auto reflection = 2.0 * solar.threshold - luma;
        const auto amount = solar.softness == 0.0 ? (luma > solar.threshold ? 1.0 : 0.0) :
            smoothstep(solar.threshold - solar.softness * 0.5, solar.threshold + solar.softness * 0.5, luma);
        const auto mapped_luma = std::lerp(luma, reflection, amount);
        for (std::size_t c = 0; c < 3U; ++c) filtered[c] += mapped_luma - luma;
      } else if (type_id == "color_halftone") {
        const auto& halftone = parameters.color_halftone;
        const auto dx = doc_x_at(x);
        const auto dy = doc_y_at(y);
        for (std::size_t c = 0; c < 3U; ++c) {
          if (original[c] <= 0.0) { filtered[c] = 0.0; continue; }
          if (original[c] >= 1.0) { filtered[c] = 1.0; continue; }
          const auto u = (dx * halftone_cos[c] + dy * halftone_sin[c]) / halftone.cell_size;
          const auto v = (-dx * halftone_sin[c] + dy * halftone_cos[c]) / halftone.cell_size;
          const auto radius = 0.5 * halftone.max_dot * std::sqrt(1.0 - std::clamp(original[c], 0.0, 1.0));
          filtered[c] = std::hypot(screen_distance(u), screen_distance(v)) <= radius * 2.0 ? 0.0 : 1.0;
        }
      } else if (type_id == "dither") {
        const auto& dither = parameters.dither;
        const auto gx = floor_to_i64((static_cast<double>(options.origin_x + x) + 0.5) / options.scale_x);
        const auto gy = floor_to_i64((static_cast<double>(options.origin_y + y) + 0.5) / options.scale_y);
        const auto threshold = (static_cast<double>(bayer_value(static_cast<int>((gx % dither.matrix_size + dither.matrix_size) %
            dither.matrix_size), static_cast<int>((gy % dither.matrix_size + dither.matrix_size) %
            dither.matrix_size), dither.matrix_size)) + 0.5) /
            static_cast<double>(dither.matrix_size * dither.matrix_size);
        const auto levels = static_cast<double>(dither.levels - 1);
        for (std::size_t c = 0; c < 3U; ++c) {
          const auto value = std::clamp(original[c], 0.0, 1.0) * levels;
          filtered[c] = std::clamp(std::floor(value + threshold), 0.0, levels) / levels;
        }
      } else if (type_id == "crosshatch") {
        const auto& hatch = parameters.crosshatch;
        const auto doc_x = doc_x_at(x);
        const auto doc_y = doc_y_at(y);
        double ink = 0.0;
        for (std::size_t line = 0; line < 4U; ++line) {
          if (luma > hatch.thresholds[line]) continue;
          const auto phase = (doc_x * hatch_cos[line] + doc_y * hatch_sin[line]) / hatch.spacing;
          if (screen_distance(phase) <= hatch.line_width)
            ink = 1.0 - (1.0 - ink) * (1.0 - hatch.ink_strength);
        }
        const auto factor = 1.0 - ink;
        for (auto& channel : filtered) channel *= factor;
      } else if (type_id == "ink_outline") {
        const auto& outline = parameters.ink_outline;
        const auto radius_x = std::max(1, static_cast<int>(std::ceil(outline.radius * options.scale_x)));
        const auto radius_y = std::max(1, static_cast<int>(std::ceil(outline.radius * options.scale_y)));
        const auto sample_luma = [&](int sx, int sy) {
          const auto pixel = read_edge(input, width, height, sx, sy, ArtisticDisplacementEdgeMode::Extend);
          return luminance(pixel, options.luminance_weights) * pixel.alpha;
        };
        const auto tl = sample_luma(x - radius_x, y - radius_y);
        const auto tc = sample_luma(x, y - radius_y);
        const auto tr = sample_luma(x + radius_x, y - radius_y);
        const auto ml = sample_luma(x - radius_x, y);
        const auto mr = sample_luma(x + radius_x, y);
        const auto bl = sample_luma(x - radius_x, y + radius_y);
        const auto bc = sample_luma(x, y + radius_y);
        const auto br = sample_luma(x + radius_x, y + radius_y);
        const auto gx = -tl + tr - 2.0 * ml + 2.0 * mr - bl + br;
        const auto gy = -tl - 2.0 * tc - tr + bl + 2.0 * bc + br;
        const auto edge = std::hypot(gx, gy) * 0.25;
        const auto ink = outline.strength * smoothstep(outline.threshold, outline.threshold + 0.22, edge);
        for (auto& channel : filtered) channel *= 1.0 - ink;
      } else if (type_id == "watercolor") {
        const auto& watercolor = parameters.watercolor;
        const auto radius_x = std::min(kMaxRadius,
            std::max(1, static_cast<int>(std::ceil(watercolor.radius * options.scale_x))));
        const auto radius_y = std::min(kMaxRadius,
            std::max(1, static_cast<int>(std::ceil(watercolor.radius * options.scale_y))));
        const auto average = neighborhood_average(input, width, height, x, y, radius_x, radius_y,
            watercolor.radius * 0.62, options.scale_x, options.scale_y, true, watercolor.seed,
            floor_to_i64((static_cast<double>(options.origin_x) + x) / options.scale_x),
            floor_to_i64((static_cast<double>(options.origin_y) + y) / options.scale_y),
            watercolor.edge_darkening, watercolor.pigment_granulation, options.luminance_weights);
        filtered = average;
      } else if (type_id == "oil_paint") {
        const auto& oil = parameters.oil_paint;
        const auto radius_x = std::min(kMaxRadius,
            std::max(1, static_cast<int>(std::ceil(oil.radius * options.scale_x))));
        const auto radius_y = std::min(kMaxRadius,
            std::max(1, static_cast<int>(std::ceil(oil.radius * options.scale_y))));
        filtered = oil_paint_average(input, width, height, x, y, radius_x, radius_y, oil.levels,
            options.luminance_weights);
      } else if (type_id == "pixel_mosaic") {
        const auto doc_x = doc_x_at(x);
        const auto doc_y = doc_y_at(y);
        const auto center = mosaic_center(doc_x, doc_y, parameters.pixel_mosaic);
        const auto sample_x = center.first * options.scale_x - options.origin_x - 0.5;
        const auto sample_y = center.second * options.scale_y - options.origin_y - 0.5;
        const auto sample = nearest_pixel(input, width, height, sample_x, sample_y);
        filtered = sample.alpha > 0.0F ? std::array<double, 3>{sample.red, sample.green, sample.blue}
            : std::array<double, 3>{};
        output[i].alpha = sample.alpha;
      } else if (type_id == "rgb_glitch") {
        const auto& glitch = parameters.rgb_glitch;
        const auto doc_y = doc_y_at(y);
        const auto band = floor_to_i64(doc_y / glitch.band_period);
        const auto band_start = static_cast<double>(band) * glitch.band_period;
        const auto in_band = doc_y - band_start < glitch.band_height;
        double dx = 0.0;
        double dy = 0.0;
        if (in_band) {
          dx = (hash_unit(glitch.seed, band, 0, 0U) * 2.0 - 1.0) * glitch.shift_x * options.scale_x;
          dy = (hash_unit(glitch.seed, band, 0, 1U) * 2.0 - 1.0) * glitch.shift_y * options.scale_y;
        }
        const auto red = sample_premultiplied(input, width, height, x - dx, y - dy,
            ArtisticDisplacementEdgeMode::Extend);
        const auto green = sample_premultiplied(input, width, height, x, y + dy,
            ArtisticDisplacementEdgeMode::Extend);
        const auto blue = sample_premultiplied(input, width, height, x + dx * 0.83, y + dy,
            ArtisticDisplacementEdgeMode::Extend);
        filtered[0] = red.red;
        filtered[1] = green.green;
        filtered[2] = blue.blue;
      } else if (type_id == "displacement_map") {
        const auto& map = parameters.displacement_map;
        std::array<double, 2> map_channels{};
        const auto frame_width = options.frame_width > 0 ? options.frame_width : width;
        const auto frame_height = options.frame_height > 0 ? options.frame_height : height;
        if (!displacement_sample(map, x, y, options.origin_x, options.origin_y,
            frame_width, frame_height, &map_channels))
          return {PhotoEffectStatus::Invalid, "Displacement map must contain finite normalized RGBA data."};
        const auto dx = (map_channels[0] * 2.0 - 1.0) * map.maximum_displacement * options.scale_x;
        const auto dy = (map_channels[1] * 2.0 - 1.0) * map.maximum_displacement * options.scale_y;
        if (dx == 0.0 && dy == 0.0) {
          output[i] = source;
          continue;
        }
        const auto edge = map.edge_mode == ArtisticDisplacementEdgeMode::Extend
            ? ArtisticDisplacementEdgeMode::Extend : ArtisticDisplacementEdgeMode::Transparent;
        const auto sample = sample_premultiplied(input, width, height, x + dx, y + dy, edge);
        output[i] = sample;
        continue;
      }
      if (!write_color(i, filtered, output[i].alpha))
        return {PhotoEffectStatus::Invalid, "Artistic effect output exceeds the finite working-color range."};
    }
  }
  if (cancelled(options)) return {PhotoEffectStatus::Cancelled, "Artistic photo effect cancelled."};
  pixels->swap(output);
  return {PhotoEffectStatus::Applied, {}};
}

}  // namespace photo_editor::pipeline
