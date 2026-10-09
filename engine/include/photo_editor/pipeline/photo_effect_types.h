#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "photo_editor/pipeline/photo_optical_effects.h"
#include "photo_editor/pipeline/photo_artistic_effects.h"

namespace photo_editor::pipeline {

enum class PhotoEffectEdgeMode : std::uint8_t { Transparent, Extend, Reflect };
enum class PhotoEffectInputScope : std::uint8_t { LayerContent, CompositeBelow };

struct PhotoEffectMaskRaster final {
  std::string id;
  std::vector<float> coverage;
};

// Algorithm v1 uses a normalized, separable Gaussian truncated at ceil(3*sigma).
// Sigma is measured in the owning content/document coordinate space, never UI pixels.
struct GaussianPhotoEffect final {
  double sigma = 8.0;
  PhotoEffectEdgeMode edge_mode = PhotoEffectEdgeMode::Transparent;
  bool operator==(const GaussianPhotoEffect&) const = default;
};

struct PhotoEffectInstance final {
  std::string id;
  std::string type_id = "gaussian_blur";
  std::uint32_t algorithm_version = 1;
  bool enabled = true;
  double amount = 1.0;
  PhotoEffectInputScope input_scope = PhotoEffectInputScope::LayerContent;
  std::string mask_ref;
  GaussianPhotoEffect gaussian;
  struct Box final {
    int radius = 8;
    PhotoEffectEdgeMode edge_mode = PhotoEffectEdgeMode::Transparent;
    bool operator==(const Box&) const = default;
  } box;
  int posterize_levels = 4;
  // Uniform, bilinearly sampled line exposure; clockwise angle in image coordinates.
  struct Motion final {
    double distance = 20.0;
    double angle = 0.0;
    PhotoEffectEdgeMode edge_mode = PhotoEffectEdgeMode::Extend;
    bool operator==(const Motion&) const = default;
  } motion;
  // Directional shutter drag with center-weighted falloff. A bound mask protects
  // covered subject pixels instead of restricting the effect to the subject.
  struct DraggedShutter final {
    double distance = 32.0;
    double angle = 0.0;
    double falloff = 0.65;
    double highlight_weighting = 0.0;
    PhotoEffectEdgeMode edge_mode = PhotoEffectEdgeMode::Extend;
    bool protect_subject = false;
    bool operator==(const DraggedShutter&) const = default;
  } dragged_shutter;
  // Alpha-normalized Gaussian detail, with threshold in linear working RGB units.
  struct Unsharp final {
    double sigma = 2.0;
    double strength = 1.0;
    double threshold = 0.0;
    PhotoEffectEdgeMode edge_mode = PhotoEffectEdgeMode::Reflect;
    bool operator==(const Unsharp&) const = default;
  } unsharp;
  // Optical diffusion scatters only highlights. Radius is in document pixels;
  // threshold/knee/strength operate in scene-linear working RGB.
  struct Diffusion final {
    double radius = 24.0;
    double threshold = 0.8;
    double knee = 0.25;
    double strength = 0.8;
    double warmth = 0.2;
    double highlight_protection = 0.0;
    bool operator==(const Diffusion&) const = default;
  } diffusion;
  // Seeded, document-space print plate treatment. Green is the registration anchor;
  // red/blue offsets are independent in source pixels.
  struct ImperfectPrint final {
    double paper_scale = 32.0;
    double paper_texture = 0.2;
    double ink_spread = 0.0;
    double ink_density = 1.0;
    double red_offset_x = 0.0;
    double red_offset_y = 0.0;
    double blue_offset_x = 0.0;
    double blue_offset_y = 0.0;
    std::uint32_t seed = 1;
    bool operator==(const ImperfectPrint&) const = default;
  } imperfect_print;
  // Seeded procedural grain in document coordinates. Size is the noise-cell
  // spacing in document pixels; strength is a scene-linear exposure scale.
  struct FilmGrain final {
    double size = 1.0;
    double strength = 0.02;
    double color = -0.8;
    std::uint32_t seed = 1;
    bool operator==(const FilmGrain&) const = default;
  } grain;
  // A broad, feathered light veil positioned in normalized full-frame space.
  struct LightLeak final {
    double center_x = 0.82;
    double center_y = 0.28;
    double angle = 0.0;
    double width = 0.3;
    double strength = 0.25;
    double warmth = 0.8;
    std::uint32_t seed = 1;
    bool operator==(const LightLeak&) const = default;
  } leak;
  struct SplitTone final {
    double shadow_hue = 32.0;
    double shadow_strength = 0.12;
    double highlight_hue = 215.0;
    double highlight_strength = 0.08;
    double balance = 0.0;
    bool operator==(const SplitTone&) const = default;
  } split_tone;
  struct Vignette final {
    double radius = 0.72;
    double roundness = 0.0;
    double feather = 0.36;
    double strength = 0.35;
    bool operator==(const Vignette&) const = default;
  } vignette;
  struct Duotone final {
    double shadow_hue = 250.0;
    double highlight_hue = 42.0;
    double strength = 0.8;
    double balance = 0.0;
    bool operator==(const Duotone&) const = default;
  } duotone;
  struct SoftFade final {
    double lift = 0.035;
    double contrast = 0.92;
    double saturation = 0.92;
    bool operator==(const SoftFade&) const = default;
  } soft_fade;
  OpticalPhotoEffectParameters optical;
  ArtisticPhotoEffectParameters artistic;
  // Unknown types/algorithm versions retain their exact parameter envelope.
  // They may be disabled and saved, but may not be silently rendered as Gaussian.
  std::string opaque_parameters;
  bool operator==(const PhotoEffectInstance& other) const {
    if (id != other.id || type_id != other.type_id || algorithm_version != other.algorithm_version ||
        enabled != other.enabled || amount != other.amount || input_scope != other.input_scope ||
        mask_ref != other.mask_ref || opaque_parameters != other.opaque_parameters) return false;
    if (algorithm_version == 1 && type_id == "posterize_linear_rgb") return posterize_levels == other.posterize_levels;
    if (algorithm_version == 1 && type_id == "box_blur") return box == other.box;
    if (algorithm_version == 1 && type_id == "motion_blur") return motion == other.motion;
    if (algorithm_version == 1 && type_id == "dragged_shutter") return dragged_shutter == other.dragged_shutter;
    if (algorithm_version == 1 && type_id == "unsharp_mask") return unsharp == other.unsharp;
    if (algorithm_version == 1 && type_id == "optical_diffusion") return diffusion == other.diffusion;
    if (algorithm_version == 1 && type_id == "imperfect_print") return imperfect_print == other.imperfect_print;
    if (algorithm_version == 1 && type_id == "film_grain") return grain == other.grain;
    if (algorithm_version == 1 && type_id == "light_leak") return leak == other.leak;
    if (algorithm_version == 1 && type_id == "split_tone") return split_tone == other.split_tone;
    if (algorithm_version == 1 && type_id == "vignette") return vignette == other.vignette;
    if (algorithm_version == 1 && type_id == "duotone") return duotone == other.duotone;
    if (algorithm_version == 1 && type_id == "soft_fade") return soft_fade == other.soft_fade;
    if (algorithm_version == 1 && type_id == "halation") return optical.halation == other.optical.halation;
    if (algorithm_version == 1 && type_id == "anamorphic_streaks") return optical.anamorphic_streaks == other.optical.anamorphic_streaks;
    if (algorithm_version == 1 && type_id == "starburst") return optical.starburst == other.optical.starburst;
    if (algorithm_version == 1 && type_id == "lens_bokeh") return optical.lens_bokeh == other.optical.lens_bokeh;
    if (algorithm_version == 1 && type_id == "tilt_shift") return optical.tilt_shift == other.optical.tilt_shift;
    if (algorithm_version == 1 && type_id == "zoom_blur") return optical.zoom_blur == other.optical.zoom_blur;
    if (algorithm_version == 1 && type_id == "spin_blur") return optical.spin_blur == other.optical.spin_blur;
    if (algorithm_version == 1 && type_id == "surface_blur") return optical.surface_blur == other.optical.surface_blur;
    if (algorithm_version == 1 && type_id == "high_pass") return optical.high_pass == other.optical.high_pass;
    if (algorithm_version == 1 && type_id == "gradient_map") return artistic.gradient_map == other.artistic.gradient_map;
    if (algorithm_version == 1 && type_id == "solarization") return artistic.solarization == other.artistic.solarization;
    if (algorithm_version == 1 && type_id == "color_halftone") return artistic.color_halftone == other.artistic.color_halftone;
    if (algorithm_version == 1 && type_id == "dither") return artistic.dither == other.artistic.dither;
    if (algorithm_version == 1 && type_id == "crosshatch") return artistic.crosshatch == other.artistic.crosshatch;
    if (algorithm_version == 1 && type_id == "ink_outline") return artistic.ink_outline == other.artistic.ink_outline;
    if (algorithm_version == 1 && type_id == "watercolor") return artistic.watercolor == other.artistic.watercolor;
    if (algorithm_version == 1 && type_id == "oil_paint") return artistic.oil_paint == other.artistic.oil_paint;
    if (algorithm_version == 1 && type_id == "pixel_mosaic") return artistic.pixel_mosaic == other.artistic.pixel_mosaic;
    if (algorithm_version == 1 && type_id == "rgb_glitch") return artistic.rgb_glitch == other.artistic.rgb_glitch;
    if (algorithm_version == 1 && type_id == "displacement_map") return artistic.displacement_map == other.artistic.displacement_map;
    return type_id != "gaussian_blur" || algorithm_version != 1 || gaussian == other.gaussian;
  }
};

}  // namespace photo_editor::pipeline
