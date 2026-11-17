#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace photo_editor::pipeline {

inline constexpr std::uint32_t kLayerStyleStackVersion = 1;
inline constexpr std::uint32_t kLayerStyleAlgorithmVersion = 1;

enum class LayerStylePlane : std::uint8_t { BelowContent, OnContent, AboveContent };
enum class LayerStyleBlendMode : std::uint8_t { Normal, Multiply, Screen, Overlay, SoftLight };
enum class LayerStyleStrokeAlignment : std::uint8_t { Inside, Center, Outside };
enum class LayerStyleBevelMode : std::uint8_t { Inner, Outer, Emboss };

// RGB values are straight-alpha, scene-linear values in the pipeline working
// space. The desktop layer-style adapter owns color-space conversion.
struct LayerStyleColor final {
  double red = 1.0;
  double green = 1.0;
  double blue = 1.0;
  double alpha = 1.0;
  bool operator==(const LayerStyleColor&) const = default;
};

struct LayerStyleGradientStop final {
  double position = 0.0;
  LayerStyleColor color;
  bool operator==(const LayerStyleGradientStop&) const = default;
};

struct LayerStyleGradient final {
  std::vector<LayerStyleGradientStop> stops;
  // Clockwise in image coordinates: zero points right; positive Y points down.
  double angle_degrees = 0.0;
  double scale = 1.0;
  bool reverse = false;
  bool operator==(const LayerStyleGradient&) const = default;
};

using LayerStyleFill = std::variant<LayerStyleColor, LayerStyleGradient>;

struct LayerStyleStroke final {
  double width = 4.0;
  LayerStyleStrokeAlignment alignment = LayerStyleStrokeAlignment::Outside;
  LayerStyleFill fill = LayerStyleColor{.red = 1.0, .green = 1.0, .blue = 1.0, .alpha = 1.0};
  bool operator==(const LayerStyleStroke&) const = default;
};

struct LayerStyleDropShadow final {
  LayerStyleColor color{.red = 0.0, .green = 0.0, .blue = 0.0, .alpha = 0.65};
  double distance = 12.0;
  double angle_degrees = 45.0;
  double blur = 8.0;
  double spread = 0.0;
  bool use_global_light = true;
  bool operator==(const LayerStyleDropShadow&) const = default;
};

struct LayerStyleInnerShadow final {
  LayerStyleColor color{.red = 0.0, .green = 0.0, .blue = 0.0, .alpha = 0.65};
  double distance = 6.0;
  double angle_degrees = 45.0;
  double blur = 6.0;
  double spread = 0.0;
  bool use_global_light = true;
  bool operator==(const LayerStyleInnerShadow&) const = default;
};

struct LayerStyleInnerGlow final {
  LayerStyleColor color{.red = 1.0, .green = 0.75, .blue = 0.25, .alpha = 0.8};
  double blur = 12.0;
  double spread = 0.0;
  bool operator==(const LayerStyleInnerGlow&) const = default;
};

struct LayerStyleOuterGlow final {
  LayerStyleColor color{.red = 1.0, .green = 0.75, .blue = 0.25, .alpha = 0.8};
  double blur = 12.0;
  double spread = 0.0;
  bool operator==(const LayerStyleOuterGlow&) const = default;
};

struct LayerStyleColorOverlay final {
  LayerStyleColor color{.red = 1.0, .green = 1.0, .blue = 1.0, .alpha = 1.0};
  bool operator==(const LayerStyleColorOverlay&) const = default;
};

struct LayerStyleGradientOverlay final {
  LayerStyleGradient gradient;
  bool operator==(const LayerStyleGradientOverlay&) const = default;
};

struct LayerStyleBevelEmboss final {
  double size = 4.0;
  double softness = 0.5;
  double depth = 1.0;
  double angle_degrees = -45.0;
  bool use_global_light = true;
  LayerStyleBevelMode mode = LayerStyleBevelMode::Inner;
  LayerStyleColor highlight{.red = 1.0, .green = 1.0, .blue = 1.0, .alpha = 0.8};
  LayerStyleColor shadow{.red = 0.0, .green = 0.0, .blue = 0.0, .alpha = 0.8};
  bool operator==(const LayerStyleBevelEmboss&) const = default;
};

using LayerStyleParameters = std::variant<std::monostate, LayerStyleStroke,
    LayerStyleDropShadow, LayerStyleInnerShadow, LayerStyleInnerGlow,
    LayerStyleOuterGlow, LayerStyleColorOverlay, LayerStyleGradientOverlay,
    LayerStyleBevelEmboss>;

struct LayerStyleOperation final {
  std::string id;
  std::string type_id;
  std::uint32_t algorithm_version = kLayerStyleAlgorithmVersion;
  bool enabled = true;
  double opacity = 1.0;
  LayerStylePlane plane = LayerStylePlane::BelowContent;
  LayerStyleBlendMode blend_mode = LayerStyleBlendMode::Normal;
  std::string mask_ref;
  LayerStyleParameters parameters;
  // Unknown types and algorithm versions preserve their opaque parameter text.
  // They are inert when disabled; enabled unsupported operations fail closed.
  std::string opaque_parameters;
  bool operator==(const LayerStyleOperation&) const = default;
};

struct LayerStyleStack final {
  std::uint32_t version = kLayerStyleStackVersion;
  bool enabled = true;
  // Array order is render order within each operation's declared plane.
  std::vector<LayerStyleOperation> operations;
  bool operator==(const LayerStyleStack&) const = default;
};

[[nodiscard]] bool is_known_layer_style(std::string_view type_id,
    std::uint32_t algorithm_version = kLayerStyleAlgorithmVersion);

}  // namespace photo_editor::pipeline
