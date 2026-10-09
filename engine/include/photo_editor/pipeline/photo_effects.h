#pragma once

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/photo_effect_types.h"

#include <cstddef>
#include <array>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace photo_editor::pipeline {

struct PhotoEffectRenderOptions final {
  double scale_x = 1.0;
  double scale_y = 1.0;
  // Absolute raster origin for deterministic document-space procedural effects.
  int origin_x = 0;
  int origin_y = 0;
  // Full raster dimensions for tile-stable spatial falloff. Zero selects the
  // dimensions passed to the current whole-frame operation.
  int frame_width = 0;
  int frame_height = 0;
  // XYZ Y coefficients of the actual linear working gamut, not display RGB.
  std::array<double, 3> luminance_weights{0.2126, 0.7152, 0.0722};
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

// Causal/global operators must run once over the complete raster to preserve
// their exact result. Tile entry points transparently use this admitted path.
[[nodiscard]] bool photo_effect_stack_requires_full_frame(
    std::span<const PhotoEffectInstance> effects);

// A borrowed cropped tile. Rows begin at pixels.data() + row * stride.
struct PhotoEffectTileView final {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  int stride = 0;
  std::span<const ColorPixel> pixels;
};
using PhotoEffectTileSink = std::function<bool(const PhotoEffectTileView&)>;

// Bounded tile scratch; input and caller-owned destination are excluded from
// max_scratch_bytes. The sink must not modify input or retain the borrowed view.
// A sink may receive tiles before a later failure. The caller must discard its
// destination on failure/cancellation; this API does not publish a transaction.
// Identity emits no tiles. A false sink result aborts further delivery.
[[nodiscard]] PhotoEffectResult render_photo_effect_tiles(
    std::span<const ColorPixel> pixels, int width, int height,
    std::span<const PhotoEffectInstance> effects,
    const PhotoEffectTileSink& sink, const PhotoEffectRenderOptions& options = {},
    std::span<const PhotoEffectMaskBinding> masks = {}, int tile_size = 256);

// Bounded tile scratch plus one transactional output image. Input stays unchanged
// on any failure. Mask bindings cover the complete input image.
[[nodiscard]] PhotoEffectResult apply_photo_effect_stack_tiled(
    std::vector<ColorPixel>* pixels, int width, int height,
    std::span<const PhotoEffectInstance> effects,
    const PhotoEffectRenderOptions& options = {},
    std::span<const PhotoEffectMaskBinding> masks = {}, int tile_size = 256);

// For a raster already owned by an enclosing transaction. Reuses its storage
// with bounded delayed row writes instead of allocating a full output image.
// Unlike apply_photo_effect_stack_tiled, failure/cancellation may leave partial
// output: the caller MUST discard this private raster on failure. Pending rows
// and tile scratch together stay within max_scratch_bytes.
[[nodiscard]] PhotoEffectResult apply_photo_effect_stack_to_private_raster(
    std::vector<ColorPixel>* pixels, int width, int height,
    std::span<const PhotoEffectInstance> effects,
    const PhotoEffectRenderOptions& options = {},
    std::span<const PhotoEffectMaskBinding> masks = {}, int tile_size = 256);

}  // namespace photo_editor::pipeline
