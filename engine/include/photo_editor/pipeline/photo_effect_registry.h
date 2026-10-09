#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace photo_editor::pipeline {

struct PhotoEffectDescriptor final {
  std::string_view type_id;
  std::uint32_t algorithm_version;
  std::string_view display_name;
  std::string_view category;
  std::string_view description;
  bool spatial;
};

// Entries describe shipped CPU operators. Backend admission remains explicit;
// membership here does not promise GPU execution or interactive performance.
inline constexpr std::array photo_effect_registry{
    PhotoEffectDescriptor{"motion_blur", 1, "Motion blur", "Blur",
        "Blur along a direction with adjustable distance and angle.", true},
    PhotoEffectDescriptor{"dragged_shutter", 1, "Dragged shutter", "Optical",
        "Create a directional exposure trail with center falloff, highlight weighting, and subject protection.", true},
    PhotoEffectDescriptor{"unsharp_mask", 1, "Unsharp mask", "Sharpen",
        "Sharpen detail with adjustable radius, strength and noise threshold.", true},
    PhotoEffectDescriptor{"posterize_linear_rgb", 1, "Posterize (linear RGB)", "Color",
        "Reduce each linear RGB channel to a stepped grid, preserving HDR and alpha.", false},
    PhotoEffectDescriptor{"box_blur", 1, "Box blur", "Blur",
        "Average a square neighborhood with an integer pixel radius.", true},
    PhotoEffectDescriptor{"gaussian_blur", 1, "Gaussian blur", "Blur",
        "Soften detail with a Gaussian kernel in linear RGB.", true},
    PhotoEffectDescriptor{"optical_diffusion", 1, "Optical diffusion", "Optical",
        "Scatter highlights with a soft threshold, warm tint, and photographic falloff.", true},
    PhotoEffectDescriptor{"imperfect_print", 1, "Imperfect print", "Print",
        "Simulate paper variation, ink spread, density, and independently registered color plates.", true},
    PhotoEffectDescriptor{"film_grain", 1, "Film grain", "Film",
        "Add seeded, fine-grained photographic texture that stays fixed in document space.", false},
    PhotoEffectDescriptor{"light_leak", 1, "Light leak", "Film",
        "Add a soft, positioned wash of warm or cool light across the frame.", false},
    PhotoEffectDescriptor{"split_tone", 1, "Split tone", "Color",
        "Tint shadows and highlights independently while keeping their luminance structure.", false},
    PhotoEffectDescriptor{"vignette", 1, "Vignette", "Lens",
        "Gently darken the frame edges with adjustable shape and feathering.", false},
    PhotoEffectDescriptor{"duotone", 1, "Duotone", "Color",
        "Map image tones between two hues while preserving HDR luminance.", false},
    PhotoEffectDescriptor{"soft_fade", 1, "Soft fade", "Film",
        "Lift deep shadows and soften contrast and saturation for a gentle print finish.", false},
    PhotoEffectDescriptor{"invert_linear_rgb", 1, "Invert (linear RGB)", "Color",
        "Invert each linear working RGB channel around 0.5, preserving alpha.", false},
    PhotoEffectDescriptor{"halation", 1, "Film halation", "Optical", "Warm highlight fringes with controlled spread and threshold.", true},
    PhotoEffectDescriptor{"anamorphic_streaks", 1, "Anamorphic streaks", "Optical", "Directional streaks from bright lights with controlled width and tint.", true},
    PhotoEffectDescriptor{"starburst", 1, "Starburst", "Optical", "A radial highlight kernel with adjustable rays and rotation.", true},
    PhotoEffectDescriptor{"lens_bokeh", 1, "Lens bokeh", "Blur", "Integrate a polygonal aperture to defocus the image.", true},
    PhotoEffectDescriptor{"tilt_shift", 1, "Tilt-shift", "Blur", "Position a sharp focus band with gradual defocus outside it.", true},
    PhotoEffectDescriptor{"zoom_blur", 1, "Zoom blur", "Blur", "Integrate radial exposure samples around an adjustable center.", true},
    PhotoEffectDescriptor{"spin_blur", 1, "Spin blur", "Blur", "Integrate rotational exposure samples around an adjustable center.", true},
    PhotoEffectDescriptor{"surface_blur", 1, "Surface blur", "Blur", "Smooth texture using a bilateral filter that respects tonal edges.", true},
    PhotoEffectDescriptor{"high_pass", 1, "High-pass detail", "Sharpen", "Extract fine detail around neutral gray from a normalized low-pass image.", true},
    PhotoEffectDescriptor{"gradient_map", 1, "Gradient map", "Color", "Map luminance through an editable gradient with multiple color stops.", false},
    PhotoEffectDescriptor{"solarization", 1, "Solarization", "Color", "Reverse tones above an adjustable threshold with controlled softness.", false},
    PhotoEffectDescriptor{"color_halftone", 1, "Color halftone", "Print", "Create screened dot patterns with independent channel angles.", true},
    PhotoEffectDescriptor{"dither", 1, "Dither", "Print", "Quantize tones using ordered Bayer patterns or Floyd–Steinberg error diffusion.", true},
    PhotoEffectDescriptor{"crosshatch", 1, "Crosshatch", "Artistic", "Build shading from layered ink strokes at adjustable angles.", false},
    PhotoEffectDescriptor{"ink_outline", 1, "Ink outline", "Artistic", "Extract contours with adjustable stroke width and contrast.", true},
    PhotoEffectDescriptor{"watercolor", 1, "Watercolor", "Artistic", "Combine softened pigment regions, edge darkening, and granulation.", true},
    PhotoEffectDescriptor{"oil_paint", 1, "Oil paint", "Artistic", "Create painterly color regions using a local intensity-mode filter.", true},
    PhotoEffectDescriptor{"pixel_mosaic", 1, "Pixel mosaic", "Artistic", "Sample color into square, hexagonal, or irregular cells.", true},
    PhotoEffectDescriptor{"rgb_glitch", 1, "RGB glitch", "Distort", "Shift color channels and seeded horizontal bands independently.", true},
    PhotoEffectDescriptor{"displacement_map", 1, "Displacement map", "Distort", "Warp pixels using the red and green channels of an imported map.", true}};

inline bool is_optical_photo_effect(std::string_view type_id) {
  return type_id == "halation" || type_id == "anamorphic_streaks" || type_id == "starburst" ||
      type_id == "lens_bokeh" || type_id == "tilt_shift" || type_id == "zoom_blur" ||
      type_id == "spin_blur" || type_id == "surface_blur" || type_id == "high_pass";
}

inline bool is_artistic_photo_effect(std::string_view type_id) {
  return type_id == "gradient_map" || type_id == "solarization" || type_id == "color_halftone" ||
      type_id == "dither" || type_id == "crosshatch" || type_id == "ink_outline" ||
      type_id == "watercolor" || type_id == "oil_paint" || type_id == "pixel_mosaic" ||
      type_id == "rgb_glitch" || type_id == "displacement_map";
}

inline const PhotoEffectDescriptor* find_photo_effect(std::string_view type_id, std::uint32_t version) {
  for (const auto& descriptor : photo_effect_registry)
    if (descriptor.type_id == type_id && descriptor.algorithm_version == version) return &descriptor;
  return nullptr;
}

}  // namespace photo_editor::pipeline
