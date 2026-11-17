#pragma once

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/layer_style_types.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace photo_editor::pipeline {

[[nodiscard]] bool validate_layer_style_stack(const LayerStyleStack& stack,
    std::string* error = nullptr);
[[nodiscard]] std::optional<std::string> serialize_layer_style_stack(
    const LayerStyleStack& stack, std::string* error = nullptr);
[[nodiscard]] std::optional<LayerStyleStack> deserialize_layer_style_stack(
    std::string_view payload, std::string* error = nullptr);

struct LayerStyleMaskBinding final {
  std::string_view id;
  // Local layer raster coverage in [0, 1]. Masking affects generated style
  // pixels only; the original content and layer mask are handled separately.
  std::span<const float> coverage;
};

struct LayerStyleRenderOptions final {
  // Mapping from authored document pixels to the current content raster.
  double document_scale_x = 1.0;
  double document_scale_y = 1.0;
  double global_light_angle_degrees = -45.0;
  int maximum_padding = 1024;
  std::size_t maximum_output_pixels = 64U * 1024U * 1024U;
  std::size_t maximum_scratch_bytes = 1024U * 1024U * 1024U;
  CancellationToken cancellation;
};

enum class LayerStyleRenderStatus { Applied, Identity, Invalid, Unsupported, Cancelled, ResourceLimit };

struct LayerStylePreflightResult final {
  LayerStyleRenderStatus status = LayerStyleRenderStatus::Invalid;
  std::string error;
  int output_width = 0;
  int output_height = 0;
  int padding_left = 0;
  int padding_right = 0;
  int padding_top = 0;
  int padding_bottom = 0;
  [[nodiscard]] bool ok() const noexcept {
    return status == LayerStyleRenderStatus::Applied ||
        status == LayerStyleRenderStatus::Identity;
  }
};

// Validates an edit against the renderer's bounds and resource limits without
// allocating raster-sized buffers or reading pixel data. Controllers can call
// this before they commit an authored stack to document history.
[[nodiscard]] LayerStylePreflightResult preflight_layer_style_stack(
    int width, int height, const LayerStyleStack& stack,
    const LayerStyleRenderOptions& options = {});

struct LayerStyleRenderResult final {
  LayerStyleRenderStatus status = LayerStyleRenderStatus::Invalid;
  std::string error;
  std::vector<ColorPixel> pixels;
  int width = 0;
  int height = 0;
  // Canvas coordinate of result pixel (0, 0); negative values indicate padding.
  int canvas_offset_x = 0;
  int canvas_offset_y = 0;
  [[nodiscard]] bool ok() const noexcept {
    return status == LayerStyleRenderStatus::Applied ||
        status == LayerStyleRenderStatus::Identity;
  }
};

// Renders styles over content that has already passed through the layer's
// filter stack. Input and returned pixels use straight alpha and working-linear
// RGB. Layer masks, opacity, transforms, and final blend with the backdrop are
// intentionally left to the enclosing compositor.
[[nodiscard]] LayerStyleRenderResult render_layer_styles(
    std::span<const ColorPixel> source, int width, int height,
    const LayerStyleStack& stack, const LayerStyleRenderOptions& options = {},
    std::span<const LayerStyleMaskBinding> masks = {});

}  // namespace photo_editor::pipeline
