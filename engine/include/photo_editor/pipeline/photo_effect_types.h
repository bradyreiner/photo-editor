#pragma once

#include <cstdint>
#include <string>
#include <vector>

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
  // Alpha-normalized Gaussian detail, with threshold in linear working RGB units.
  struct Unsharp final {
    double sigma = 2.0;
    double strength = 1.0;
    double threshold = 0.0;
    PhotoEffectEdgeMode edge_mode = PhotoEffectEdgeMode::Reflect;
    bool operator==(const Unsharp&) const = default;
  } unsharp;
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
    if (algorithm_version == 1 && type_id == "unsharp_mask") return unsharp == other.unsharp;
    return type_id != "gaussian_blur" || algorithm_version != 1 || gaussian == other.gaussian;
  }
};

}  // namespace photo_editor::pipeline
