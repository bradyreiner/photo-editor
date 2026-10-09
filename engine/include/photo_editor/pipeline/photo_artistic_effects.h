#pragma once

#include "photo_editor/pipeline/photo_effect_parameter.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace photo_editor::pipeline {

struct ColorPixel;
struct PhotoEffectRenderOptions;
struct PhotoEffectSupport;
struct PhotoEffectResult;

struct ArtisticGradientStop final {
  double position = 0.0;
  std::array<float, 3> linear_rgb{0.0F, 0.0F, 0.0F};
  bool operator==(const ArtisticGradientStop&) const = default;
};

enum class ArtisticDitherMode : std::uint8_t { OrderedBayer = 0, FloydSteinberg = 1 };
enum class ArtisticMosaicShape : std::uint8_t { Square = 0, Hexagonal = 1, IrregularVoronoi = 2 };
enum class ArtisticDisplacementEdgeMode : std::uint8_t { Extend = 0, Transparent = 1 };

// Values are normalized to the layer/document coordinate system where noted.
// The displacement raster is a transient immutable binding: asset_ref is the
// lowercase SHA-256 of its canonical PNG, and width/height identify the map
// raster. rgba is resolved by the caller and has four normalized encoded data
// floats per map pixel. It is sampled bilinearly in normalized full-frame
// coordinates. Red and green are sampled as data, with 0.5 neutral; alpha zero
// is neutral before resampling.
struct ArtisticPhotoEffectParameters final {
  struct GradientMap final {
    std::vector<ArtisticGradientStop> stops{{0.0, {0.0F, 0.0F, 0.0F}},
        {1.0, {1.0F, 1.0F, 1.0F}}};
    bool operator==(const GradientMap&) const = default;
  } gradient_map;
  struct Solarization final {
    double threshold = 0.5;
    double softness = 0.0;
    bool operator==(const Solarization&) const = default;
  } solarization;
  struct ColorHalftone final {
    double cell_size = 8.0;
    double max_dot = 0.92;
    std::array<double, 3> angles{15.0, 75.0, 0.0};
    bool operator==(const ColorHalftone&) const = default;
  } color_halftone;
  struct Dither final {
    int levels = 4;
    ArtisticDitherMode mode = ArtisticDitherMode::OrderedBayer;
    int matrix_size = 4;
    bool operator==(const Dither&) const = default;
  } dither;
  struct Crosshatch final {
    double spacing = 10.0;
    double line_width = 0.12;
    double ink_strength = 0.9;
    std::array<double, 4> angles{45.0, -45.0, 0.0, 90.0};
    std::array<double, 4> thresholds{0.76, 0.58, 0.40, 0.22};
    bool operator==(const Crosshatch&) const = default;
  } crosshatch;
  struct InkOutline final {
    int radius = 1;
    double threshold = 0.08;
    double strength = 0.9;
    bool operator==(const InkOutline&) const = default;
  } ink_outline;
  struct Watercolor final {
    int radius = 2;
    double edge_darkening = 0.35;
    double pigment_granulation = 0.15;
    std::uint32_t seed = 1;
    bool operator==(const Watercolor&) const = default;
  } watercolor;
  struct OilPaint final {
    int radius = 2;
    int levels = 16;
    bool operator==(const OilPaint&) const = default;
  } oil_paint;
  struct PixelMosaic final {
    double cell_size = 8.0;
    ArtisticMosaicShape shape = ArtisticMosaicShape::Square;
    double jitter = 0.2;
    std::uint32_t seed = 1;
    bool operator==(const PixelMosaic&) const = default;
  } pixel_mosaic;
  struct RgbGlitch final {
    double shift_x = 8.0;
    double shift_y = 0.0;
    double band_height = 8.0;
    double band_period = 32.0;
    std::uint32_t seed = 1;
    bool operator==(const RgbGlitch&) const = default;
  } rgb_glitch;
  struct DisplacementMap final {
    std::string asset_ref;
    int width = 0;
    int height = 0;
    double maximum_displacement = 12.0;
    ArtisticDisplacementEdgeMode edge_mode = ArtisticDisplacementEdgeMode::Extend;
    std::shared_ptr<const std::vector<float>> rgba;
    bool operator==(const DisplacementMap& other) const {
      return asset_ref == other.asset_ref && width == other.width && height == other.height &&
          maximum_displacement == other.maximum_displacement && edge_mode == other.edge_mode;
    }
  } displacement_map;

  // The raster binding is intentionally excluded from parameter equality.
  bool operator==(const ArtisticPhotoEffectParameters& other) const {
    return gradient_map == other.gradient_map && solarization == other.solarization &&
        color_halftone == other.color_halftone && dither == other.dither &&
        crosshatch == other.crosshatch && ink_outline == other.ink_outline &&
        watercolor == other.watercolor && oil_paint == other.oil_paint &&
        pixel_mosaic == other.pixel_mosaic && rgb_glitch == other.rgb_glitch &&
        displacement_map == other.displacement_map;
  }
};

[[nodiscard]] std::span<const PhotoEffectParameterField> artistic_photo_effect_fields(
    std::string_view type_id);
[[nodiscard]] PhotoEffectParameterValues artistic_photo_effect_values(
    std::string_view type_id, const ArtisticPhotoEffectParameters& parameters);
// Partial updates are accepted. Unknown keys, non-finite values, fractional
// integers, and out-of-range values reject the entire update transactionally.
[[nodiscard]] bool set_artistic_photo_effect_values(std::string_view type_id,
    const PhotoEffectParameterValues& values, ArtisticPhotoEffectParameters* parameters,
    std::string* error = nullptr);
// Exact, versioned text payload for persisted numeric and structured settings.
// The resolved displacement raster is never serialized; its managed asset_ref is.
[[nodiscard]] std::string serialize_artistic_photo_effect_parameters(
    std::string_view type_id, const ArtisticPhotoEffectParameters& parameters);
[[nodiscard]] bool deserialize_artistic_photo_effect_parameters(std::string_view type_id,
    std::string_view payload, ArtisticPhotoEffectParameters* parameters, std::string* error = nullptr);

[[nodiscard]] bool artistic_photo_effect_requires_full_frame(std::string_view type_id,
    const ArtisticPhotoEffectParameters& parameters) noexcept;
[[nodiscard]] PhotoEffectResult artistic_photo_effect_support(std::string_view type_id,
    const ArtisticPhotoEffectParameters& parameters, const PhotoEffectRenderOptions& options,
    PhotoEffectSupport* support);
// Applies the complete operator but deliberately omits stack amount/mask mixing.
// The operation is transactional: failures and cancellation leave pixels intact.
[[nodiscard]] PhotoEffectResult apply_artistic_photo_effect(std::vector<ColorPixel>* pixels,
    int width, int height, std::string_view type_id,
    const ArtisticPhotoEffectParameters& parameters, const PhotoEffectRenderOptions& options);

}  // namespace photo_editor::pipeline
