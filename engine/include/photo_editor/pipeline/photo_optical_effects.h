#pragma once

#include "photo_editor/pipeline/photo_effect_parameter.h"

#include <span>
#include <string_view>
#include <vector>

namespace photo_editor::pipeline {

struct ColorPixel;
struct PhotoEffectRenderOptions;
struct PhotoEffectSupport;
struct PhotoEffectResult;

// Parameters for CPU optical and spatial effects. Distances are expressed in
// document pixels and are converted by PhotoEffectRenderOptions::scale_x/y.
// These are deterministic image-space models; they are not a calibrated lens,
// film-stock, or depth-of-field simulation.
struct OpticalPhotoEffectParameters final {
  struct Halation final {
    double radius = 10.0;
    double threshold = 1.0;
    double knee = 0.25;
    double strength = 0.7;
    double warmth = 0.85;
    bool operator==(const Halation&) const = default;
  } halation;

  struct AnamorphicStreaks final {
    double half_length = 80.0;
    double width = 1.5;
    double angle_degrees = 0.0;
    double threshold = 1.0;
    double knee = 0.25;
    double strength = 0.8;
    double warmth = 0.45;
    bool operator==(const AnamorphicStreaks&) const = default;
  } anamorphic_streaks;

  struct Starburst final {
    double radius = 64.0;
    double width = 1.25;
    double angle_degrees = 0.0;
    int ray_count = 6;
    double threshold = 1.0;
    double knee = 0.25;
    double strength = 0.65;
    double warmth = 0.1;
    bool operator==(const Starburst&) const = default;
  } starburst;

  struct LensBokeh final {
    double radius = 12.0;
    int aperture_blades = 6;
    double rotation_degrees = 0.0;
    int integration_samples = 64;
    double threshold = 0.85;
    double knee = 0.25;
    double strength = 0.8;
    bool operator==(const LensBokeh&) const = default;
  } lens_bokeh;

  struct TiltShift final {
    double center_x = 0.5;
    double center_y = 0.5;
    double angle_degrees = 0.0;
    double focus_width = 0.18;
    double transition_width = 0.16;
    double blur_radius = 12.0;
    bool operator==(const TiltShift&) const = default;
  } tilt_shift;

  struct ZoomBlur final {
    double center_x = 0.5;
    double center_y = 0.5;
    double radius = 36.0;
    int samples = 24;
    bool operator==(const ZoomBlur&) const = default;
  } zoom_blur;

  struct SpinBlur final {
    double center_x = 0.5;
    double center_y = 0.5;
    double angle_degrees = 12.0;
    int samples = 24;
    bool operator==(const SpinBlur&) const = default;
  } spin_blur;

  struct SurfaceBlur final {
    double radius = 4.0;
    double range_sigma = 0.12;
    bool operator==(const SurfaceBlur&) const = default;
  } surface_blur;

  struct HighPass final {
    double radius = 2.0;
    double strength = 1.0;
    bool operator==(const HighPass&) const = default;
  } high_pass;

  bool operator==(const OpticalPhotoEffectParameters&) const = default;
};

// Include photo_effects.h as well when consuming these functions; it defines
// PhotoEffectResult, PhotoEffectRenderOptions, and PhotoEffectSupport.
[[nodiscard]] PhotoEffectResult optical_photo_effect_support(
    std::string_view type_id, const OpticalPhotoEffectParameters& parameters,
    const PhotoEffectRenderOptions& options, PhotoEffectSupport* support);

// Applies the complete effect transactionally. Global amount and mask mixing
// belong to the caller. Input/output pixels are linear working RGB with straight
// alpha. Implementations use premultiplied alpha while filtering and preserve
// finite output. The caller owns the input raster; scratch is limited by options.
[[nodiscard]] PhotoEffectResult apply_optical_photo_effect(
    std::vector<ColorPixel>* pixels, int width, int height,
    std::string_view type_id,
    const OpticalPhotoEffectParameters& parameters,
    const PhotoEffectRenderOptions& options);

[[nodiscard]] std::span<const PhotoEffectParameterField>
optical_photo_effect_fields(std::string_view type_id);
[[nodiscard]] PhotoEffectParameterValues optical_photo_effect_values(
    std::string_view type_id, const OpticalPhotoEffectParameters& parameters);
// Applies a partial set of named values. Unknown keys, out-of-range/non-finite
// values, and non-integral discrete controls reject the entire update.
[[nodiscard]] bool set_optical_photo_effect_values(
    std::string_view type_id, const PhotoEffectParameterValues& values,
    OpticalPhotoEffectParameters* parameters, std::string* error = nullptr);
// Versioned, positional, locale-independent payload in field-schema order.
// Empty means that the type or parameter set is unsupported/invalid.
[[nodiscard]] std::string serialize_optical_photo_effect_parameters(
    std::string_view type_id,
    const OpticalPhotoEffectParameters& parameters);
[[nodiscard]] bool deserialize_optical_photo_effect_parameters(
    std::string_view type_id, std::string_view payload,
    OpticalPhotoEffectParameters* parameters, std::string* error = nullptr);

// Zoom and spin use source coordinates anywhere in the frame. They require an
// admitted whole-frame render; a padded tile approximation is not equivalent.
[[nodiscard]] bool optical_photo_effect_requires_full_frame(
    std::string_view type_id,
    const OpticalPhotoEffectParameters& parameters) noexcept;

}  // namespace photo_editor::pipeline
