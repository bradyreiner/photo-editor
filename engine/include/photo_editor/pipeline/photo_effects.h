#pragma once

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/photo_effect_types.h"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace photo_editor::pipeline {

struct PhotoEffectRenderOptions final {
  double scale_x = 1.0;
  double scale_y = 1.0;
  std::size_t max_scratch_bytes = 512U * 1024U * 1024U;
  CancellationToken cancellation;
};

struct PhotoEffectSupport final {
  int x = 0;
  int y = 0;
};

struct PhotoEffectMaskBinding final {
  std::string_view id;
  std::span<const float> coverage;
};

enum class PhotoEffectStatus { Applied, Identity, Invalid, Unsupported, Cancelled, ResourceLimit };

struct PhotoEffectResult final {
  PhotoEffectStatus status = PhotoEffectStatus::Invalid;
  std::string error;
  [[nodiscard]] bool ok() const {
    return status == PhotoEffectStatus::Applied || status == PhotoEffectStatus::Identity;
  }
};

// Includes parameter/version validation; returns no guessed support for unknown effects.
[[nodiscard]] PhotoEffectResult photo_effect_support(
    const PhotoEffectInstance& effect, const PhotoEffectRenderOptions& options,
    PhotoEffectSupport* support);

// Linear working RGB, straight alpha on entry/exit; convolution/mixing uses premultiplied
// alpha. Mask is effect coverage, not a restriction on which source pixels are sampled.
// Caller supplies a whole image or a padded tile and crops off the declared support.
// Errors/cancellation never publish a partially processed image.
[[nodiscard]] PhotoEffectResult apply_photo_effect(
    std::vector<ColorPixel>* pixels, int width, int height,
    const PhotoEffectInstance& effect, const PhotoEffectRenderOptions& options = {},
    std::span<const float> mask = {});

// Array order is execution order. Each effect has a unique stable ID. The whole
// stack is transactional, including failures in later operators or mask resolution.
[[nodiscard]] PhotoEffectResult photo_effect_stack_support(
    std::span<const PhotoEffectInstance> effects, const PhotoEffectRenderOptions& options,
    PhotoEffectSupport* support);
[[nodiscard]] PhotoEffectResult apply_photo_effect_stack(
    std::vector<ColorPixel>* pixels, int width, int height,
    std::span<const PhotoEffectInstance> effects,
    const PhotoEffectRenderOptions& options = {},
    std::span<const PhotoEffectMaskBinding> masks = {});

// Bounded tile scratch plus one transactional output image. Input stays unchanged
// on any failure. Mask bindings cover the complete input image.
[[nodiscard]] PhotoEffectResult apply_photo_effect_stack_tiled(
    std::vector<ColorPixel>* pixels, int width, int height,
    std::span<const PhotoEffectInstance> effects,
    const PhotoEffectRenderOptions& options = {},
    std::span<const PhotoEffectMaskBinding> masks = {}, int tile_size = 256);

}  // namespace photo_editor::pipeline
