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
    PhotoEffectDescriptor{"unsharp_mask", 1, "Unsharp mask", "Sharpen",
        "Sharpen detail with adjustable radius, strength and noise threshold.", true},
    PhotoEffectDescriptor{"posterize_linear_rgb", 1, "Posterize (linear RGB)", "Color",
        "Reduce each linear RGB channel to a stepped grid, preserving HDR and alpha.", false},
    PhotoEffectDescriptor{"box_blur", 1, "Box blur", "Blur",
        "Average a square neighborhood with an integer pixel radius.", true},
    PhotoEffectDescriptor{"gaussian_blur", 1, "Gaussian blur", "Blur",
        "Soften detail with a Gaussian kernel in linear RGB.", true},
    PhotoEffectDescriptor{"invert_linear_rgb", 1, "Invert (linear RGB)", "Color",
        "Invert each linear working RGB channel around 0.5, preserving alpha.", false}};

inline const PhotoEffectDescriptor* find_photo_effect(std::string_view type_id, std::uint32_t version) {
  for (const auto& descriptor : photo_effect_registry)
    if (descriptor.type_id == type_id && descriptor.algorithm_version == version) return &descriptor;
  return nullptr;
}

}  // namespace photo_editor::pipeline
