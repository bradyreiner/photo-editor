#include "photo_editor/pipeline/layer_styles.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <iomanip>
#include <limits>
#include <locale>
#include <new>
#include <numbers>
#include <sstream>
#include <unordered_set>

namespace photo_editor::pipeline {
namespace {

constexpr std::string_view kMagic = "photo-editor-layer-styles:v1";
constexpr std::size_t kMaximumOperations = 512U;
constexpr std::size_t kMaximumPayloadBytes = 4U * 1024U * 1024U;
constexpr std::size_t kMaximumIdBytes = 256U;
constexpr std::size_t kMaximumTypeBytes = 128U;
constexpr std::size_t kMaximumMaskIdBytes = 256U;
constexpr std::size_t kMaximumParameterBytes = 256U * 1024U;
constexpr std::size_t kMaximumGradientStops = 32U;
constexpr double kMaximumStyleColor = 65536.0;
constexpr double kMaximumGeometry = 4096.0;

struct Padding final {
  int left = 0;
  int right = 0;
  int top = 0;
  int bottom = 0;
};

bool fail(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
  return false;
}

LayerStyleRenderResult render_failure(LayerStyleRenderStatus status, std::string message) {
  LayerStyleRenderResult result;
  result.status = status;
  result.error = std::move(message);
  return result;
}

LayerStylePreflightResult preflight_failure(LayerStyleRenderStatus status, std::string message) {
  LayerStylePreflightResult result;
  result.status = status;
  result.error = std::move(message);
  return result;
}

bool finite_range(double value, double minimum, double maximum) {
  return std::isfinite(value) && value >= minimum && value <= maximum;
}

bool valid_color(const LayerStyleColor& color) {
  return finite_range(color.red, -kMaximumStyleColor, kMaximumStyleColor) &&
      finite_range(color.green, -kMaximumStyleColor, kMaximumStyleColor) &&
      finite_range(color.blue, -kMaximumStyleColor, kMaximumStyleColor) &&
      finite_range(color.alpha, 0.0, 1.0);
}

bool valid_gradient(const LayerStyleGradient& gradient) {
  if (gradient.stops.size() < 2U || gradient.stops.size() > kMaximumGradientStops ||
      !finite_range(gradient.angle_degrees, -360.0, 360.0) ||
      !finite_range(gradient.scale, 0.01, 100.0)) return false;
  double previous = -1.0;
  for (const auto& stop : gradient.stops) {
    if (!finite_range(stop.position, 0.0, 1.0) || stop.position <= previous ||
        !valid_color(stop.color)) return false;
    previous = stop.position;
  }
  return std::abs(gradient.stops.front().position) <= 1e-12 &&
      std::abs(gradient.stops.back().position - 1.0) <= 1e-12;
}

bool valid_fill(const LayerStyleFill& fill) {
  if (const auto* color = std::get_if<LayerStyleColor>(&fill)) return valid_color(*color);
  if (const auto* gradient = std::get_if<LayerStyleGradient>(&fill)) return valid_gradient(*gradient);
  return false;
}

bool valid_plane(LayerStylePlane plane) {
  return plane == LayerStylePlane::BelowContent || plane == LayerStylePlane::OnContent ||
      plane == LayerStylePlane::AboveContent;
}

bool valid_blend_mode(LayerStyleBlendMode mode) {
  return mode == LayerStyleBlendMode::Normal || mode == LayerStyleBlendMode::Multiply ||
      mode == LayerStyleBlendMode::Screen || mode == LayerStyleBlendMode::Overlay ||
      mode == LayerStyleBlendMode::SoftLight;
}

bool valid_operation(const LayerStyleOperation& operation, std::string* error) {
  if (operation.id.empty() || operation.id.size() > kMaximumIdBytes ||
      operation.type_id.empty() || operation.type_id.size() > kMaximumTypeBytes ||
      operation.algorithm_version == 0U || !finite_range(operation.opacity, 0.0, 1.0) ||
      !valid_plane(operation.plane) || !valid_blend_mode(operation.blend_mode) ||
      operation.mask_ref.size() > kMaximumMaskIdBytes ||
      operation.opaque_parameters.size() > kMaximumParameterBytes) {
    return fail(error, "Invalid layer-style identity, opacity, plane, blend mode or payload size.");
  }
  if (is_known_layer_style(operation.type_id, operation.algorithm_version) &&
      (operation.type_id == "color_overlay" || operation.type_id == "gradient_overlay") &&
      operation.plane != LayerStylePlane::OnContent)
    return fail(error, "Color and gradient overlays must use the OnContent plane.");
  if (!is_known_layer_style(operation.type_id, operation.algorithm_version)) {
    if (!std::holds_alternative<std::monostate>(operation.parameters))
      return fail(error, "Unsupported layer styles must use the opaque parameter envelope.");
    return true;
  }
  if (!operation.opaque_parameters.empty())
    return fail(error, "Known layer styles may not carry opaque parameters.");

  if (operation.type_id == "stroke") {
    const auto* value = std::get_if<LayerStyleStroke>(&operation.parameters);
    if (value == nullptr || !finite_range(value->width, 0.0, kMaximumGeometry) ||
        (value->alignment != LayerStyleStrokeAlignment::Inside &&
         value->alignment != LayerStyleStrokeAlignment::Center &&
         value->alignment != LayerStyleStrokeAlignment::Outside) || !valid_fill(value->fill))
      return fail(error, "Invalid stroke parameters.");
  } else if (operation.type_id == "drop_shadow") {
    const auto* value = std::get_if<LayerStyleDropShadow>(&operation.parameters);
    if (value == nullptr || !valid_color(value->color) ||
        !finite_range(value->distance, 0.0, kMaximumGeometry) ||
        !finite_range(value->angle_degrees, -360.0, 360.0) ||
        !finite_range(value->blur, 0.0, kMaximumGeometry) ||
        !finite_range(value->spread, -kMaximumGeometry, kMaximumGeometry))
      return fail(error, "Invalid drop-shadow parameters.");
  } else if (operation.type_id == "inner_shadow") {
    const auto* value = std::get_if<LayerStyleInnerShadow>(&operation.parameters);
    if (value == nullptr || !valid_color(value->color) ||
        !finite_range(value->distance, 0.0, kMaximumGeometry) ||
        !finite_range(value->angle_degrees, -360.0, 360.0) ||
        !finite_range(value->blur, 0.0, kMaximumGeometry) ||
        !finite_range(value->spread, -kMaximumGeometry, kMaximumGeometry))
      return fail(error, "Invalid inner-shadow parameters.");
  } else if (operation.type_id == "inner_glow") {
    const auto* value = std::get_if<LayerStyleInnerGlow>(&operation.parameters);
    if (value == nullptr || !valid_color(value->color) ||
        !finite_range(value->blur, 0.0, kMaximumGeometry) ||
        !finite_range(value->spread, -kMaximumGeometry, kMaximumGeometry))
      return fail(error, "Invalid inner-glow parameters.");
  } else if (operation.type_id == "outer_glow") {
    const auto* value = std::get_if<LayerStyleOuterGlow>(&operation.parameters);
    if (value == nullptr || !valid_color(value->color) ||
        !finite_range(value->blur, 0.0, kMaximumGeometry) ||
        !finite_range(value->spread, -kMaximumGeometry, kMaximumGeometry))
      return fail(error, "Invalid outer-glow parameters.");
  } else if (operation.type_id == "color_overlay") {
    const auto* value = std::get_if<LayerStyleColorOverlay>(&operation.parameters);
    if (value == nullptr || !valid_color(value->color))
      return fail(error, "Invalid color-overlay parameters.");
  } else if (operation.type_id == "gradient_overlay") {
    const auto* value = std::get_if<LayerStyleGradientOverlay>(&operation.parameters);
    if (value == nullptr || !valid_gradient(value->gradient))
      return fail(error, "Invalid gradient-overlay parameters.");
  } else if (operation.type_id == "bevel_emboss") {
    const auto* value = std::get_if<LayerStyleBevelEmboss>(&operation.parameters);
    if (value == nullptr || !finite_range(value->size, 0.0, kMaximumGeometry) ||
        !finite_range(value->softness, 0.0, kMaximumGeometry) ||
        !finite_range(value->depth, 0.0, 16.0) ||
        !finite_range(value->angle_degrees, -360.0, 360.0) ||
        (value->mode != LayerStyleBevelMode::Inner &&
         value->mode != LayerStyleBevelMode::Outer &&
         value->mode != LayerStyleBevelMode::Emboss) ||
        !valid_color(value->highlight) || !valid_color(value->shadow))
      return fail(error, "Invalid bevel/emboss parameters.");
  }
  return true;
}

bool validate_ids(const LayerStyleStack& stack, std::string* error) {
  if (stack.version != kLayerStyleStackVersion)
    return fail(error, "Unsupported layer-style stack version.");
  if (stack.operations.size() > kMaximumOperations)
    return fail(error, "Too many layer-style operations.");
  std::unordered_set<std::string_view> ids;
  for (const auto& operation : stack.operations) {
    if (!valid_operation(operation, error)) return false;
    if (!ids.insert(operation.id).second)
      return fail(error, "Duplicate layer-style operation ID.");
  }
  return true;
}

void write_color(std::ostream& stream, const LayerStyleColor& color) {
  stream << color.red << ' ' << color.green << ' ' << color.blue << ' ' << color.alpha;
}

bool read_color(std::istream& stream, LayerStyleColor* color) {
  return color != nullptr && static_cast<bool>(stream >> color->red >> color->green >> color->blue >> color->alpha);
}

void write_gradient(std::ostream& stream, const LayerStyleGradient& gradient) {
  stream << gradient.angle_degrees << ' ' << gradient.scale << ' '
         << (gradient.reverse ? 1 : 0) << ' ' << gradient.stops.size();
  for (const auto& stop : gradient.stops) {
    stream << ' ' << stop.position << ' ';
    write_color(stream, stop.color);
  }
}

bool read_gradient(std::istream& stream, LayerStyleGradient* gradient) {
  unsigned reverse = 0;
  std::size_t count = 0;
  if (gradient == nullptr || !(stream >> gradient->angle_degrees >> gradient->scale >> reverse >> count) ||
      reverse > 1U || count < 2U || count > kMaximumGradientStops) return false;
  gradient->reverse = reverse != 0U;
  gradient->stops.clear();
  gradient->stops.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    LayerStyleGradientStop stop;
    if (!(stream >> stop.position) || !read_color(stream, &stop.color)) return false;
    gradient->stops.push_back(stop);
  }
  return true;
}

void write_fill(std::ostream& stream, const LayerStyleFill& fill) {
  if (const auto* color = std::get_if<LayerStyleColor>(&fill)) {
    stream << "solid ";
    write_color(stream, *color);
  } else {
    stream << "gradient ";
    write_gradient(stream, std::get<LayerStyleGradient>(fill));
  }
}

bool read_fill(std::istream& stream, LayerStyleFill* fill) {
  std::string kind;
  if (fill == nullptr || !(stream >> kind)) return false;
  if (kind == "solid") {
    LayerStyleColor color;
    if (!read_color(stream, &color)) return false;
    *fill = color;
    return true;
  }
  if (kind == "gradient") {
    LayerStyleGradient gradient;
    if (!read_gradient(stream, &gradient)) return false;
    *fill = std::move(gradient);
    return true;
  }
  return false;
}

bool exhausted(std::istream& stream) {
  stream >> std::ws;
  return stream.eof();
}

std::string encode_parameters(const LayerStyleOperation& operation) {
  if (!is_known_layer_style(operation.type_id, operation.algorithm_version))
    return operation.opaque_parameters;
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(std::numeric_limits<double>::max_digits10);
  if (operation.type_id == "stroke") {
    const auto& value = std::get<LayerStyleStroke>(operation.parameters);
    stream << value.width << ' ' << static_cast<unsigned>(value.alignment) << ' ';
    write_fill(stream, value.fill);
  } else if (operation.type_id == "drop_shadow") {
    const auto& value = std::get<LayerStyleDropShadow>(operation.parameters);
    write_color(stream, value.color);
    stream << ' ' << value.distance << ' ' << value.angle_degrees << ' ' << value.blur << ' '
           << value.spread << ' ' << (value.use_global_light ? 1 : 0);
  } else if (operation.type_id == "inner_shadow") {
    const auto& value = std::get<LayerStyleInnerShadow>(operation.parameters);
    write_color(stream, value.color);
    stream << ' ' << value.distance << ' ' << value.angle_degrees << ' ' << value.blur << ' '
           << value.spread << ' ' << (value.use_global_light ? 1 : 0);
  } else if (operation.type_id == "inner_glow") {
    const auto& value = std::get<LayerStyleInnerGlow>(operation.parameters);
    write_color(stream, value.color);
    stream << ' ' << value.blur << ' ' << value.spread;
  } else if (operation.type_id == "outer_glow") {
    const auto& value = std::get<LayerStyleOuterGlow>(operation.parameters);
    write_color(stream, value.color);
    stream << ' ' << value.blur << ' ' << value.spread;
  } else if (operation.type_id == "color_overlay") {
    write_color(stream, std::get<LayerStyleColorOverlay>(operation.parameters).color);
  } else if (operation.type_id == "gradient_overlay") {
    write_gradient(stream, std::get<LayerStyleGradientOverlay>(operation.parameters).gradient);
  } else if (operation.type_id == "bevel_emboss") {
    const auto& value = std::get<LayerStyleBevelEmboss>(operation.parameters);
    stream << value.size << ' ' << value.softness << ' ' << value.depth << ' '
           << value.angle_degrees << ' ' << (value.use_global_light ? 1 : 0) << ' '
           << static_cast<unsigned>(value.mode) << ' ';
    write_color(stream, value.highlight);
    stream << ' ';
    write_color(stream, value.shadow);
  }
  return stream.str();
}

bool decode_parameters(LayerStyleOperation* operation, const std::string& parameters,
    std::string* error) {
  if (operation == nullptr) return fail(error, "Missing layer-style output.");
  if (!is_known_layer_style(operation->type_id, operation->algorithm_version)) {
    operation->opaque_parameters = parameters;
    operation->parameters = std::monostate{};
    return true;
  }
  std::istringstream stream(parameters);
  stream.imbue(std::locale::classic());
  if (operation->type_id == "stroke") {
    LayerStyleStroke value;
    unsigned alignment = 0;
    if (!(stream >> value.width >> alignment) || alignment > 2U || !read_fill(stream, &value.fill))
      return fail(error, "Malformed stroke style parameters.");
    value.alignment = static_cast<LayerStyleStrokeAlignment>(alignment);
    operation->parameters = std::move(value);
  } else if (operation->type_id == "drop_shadow") {
    LayerStyleDropShadow value;
    unsigned global = 0;
    if (!read_color(stream, &value.color) || !(stream >> value.distance >> value.angle_degrees >> value.blur >>
            value.spread >> global) || global > 1U)
      return fail(error, "Malformed drop-shadow style parameters.");
    value.use_global_light = global != 0U;
    operation->parameters = value;
  } else if (operation->type_id == "inner_shadow") {
    LayerStyleInnerShadow value;
    unsigned global = 0;
    if (!read_color(stream, &value.color) || !(stream >> value.distance >> value.angle_degrees >> value.blur >>
            value.spread >> global) || global > 1U)
      return fail(error, "Malformed inner-shadow style parameters.");
    value.use_global_light = global != 0U;
    operation->parameters = value;
  } else if (operation->type_id == "inner_glow") {
    LayerStyleInnerGlow value;
    if (!read_color(stream, &value.color) || !(stream >> value.blur >> value.spread))
      return fail(error, "Malformed inner-glow style parameters.");
    operation->parameters = value;
  } else if (operation->type_id == "outer_glow") {
    LayerStyleOuterGlow value;
    if (!read_color(stream, &value.color) || !(stream >> value.blur >> value.spread))
      return fail(error, "Malformed outer-glow style parameters.");
    operation->parameters = value;
  } else if (operation->type_id == "color_overlay") {
    LayerStyleColorOverlay value;
    if (!read_color(stream, &value.color)) return fail(error, "Malformed color-overlay style parameters.");
    operation->parameters = value;
  } else if (operation->type_id == "gradient_overlay") {
    LayerStyleGradientOverlay value;
    if (!read_gradient(stream, &value.gradient)) return fail(error, "Malformed gradient-overlay style parameters.");
    operation->parameters = std::move(value);
  } else if (operation->type_id == "bevel_emboss") {
    LayerStyleBevelEmboss value;
    unsigned global = 0;
    unsigned mode = 0;
    if (!(stream >> value.size >> value.softness >> value.depth >> value.angle_degrees >> global >> mode) ||
        global > 1U || mode > 2U || !read_color(stream, &value.highlight) ||
        !read_color(stream, &value.shadow))
      return fail(error, "Malformed bevel/emboss style parameters.");
    value.use_global_light = global != 0U;
    value.mode = static_cast<LayerStyleBevelMode>(mode);
    operation->parameters = value;
  }
  if (!exhausted(stream)) return fail(error, "Trailing data in layer-style parameters.");
  return true;
}

bool mask_bindings_valid(std::span<const LayerStyleMaskBinding> masks, std::size_t input_size,
    CancellationToken cancellation, std::string* error) {
  std::unordered_set<std::string_view> ids;
  for (const auto& mask : masks) {
    if (mask.id.empty() || mask.coverage.size() != input_size || !ids.insert(mask.id).second)
      return fail(error, "Invalid or duplicate layer-style mask binding.");
    for (std::size_t index = 0; index < mask.coverage.size(); ++index) {
      if ((index & 4095U) == 0U && cancellation.cancelled()) return false;
      const auto value = mask.coverage[index];
      if (!std::isfinite(value) || value < 0.0F || value > 1.0F)
        return fail(error, "Layer-style mask coverage must be finite and between zero and one.");
    }
  }
  return true;
}

const LayerStyleMaskBinding* find_mask(std::span<const LayerStyleMaskBinding> masks,
    std::string_view id) {
  if (id.empty()) return nullptr;
  const auto found = std::find_if(masks.begin(), masks.end(),
      [&](const auto& mask) { return mask.id == id; });
  return found == masks.end() ? nullptr : &*found;
}

double mask_value_at(const LayerStyleMaskBinding* mask, int x, int y, int source_width,
    int source_height, int padding_left, int padding_top) {
  if (mask == nullptr) return 1.0;
  const int source_x = x - padding_left;
  const int source_y = y - padding_top;
  if (source_x < 0 || source_y < 0 || source_x >= source_width || source_y >= source_height) return 0.0;
  return mask->coverage[static_cast<std::size_t>(source_y) * static_cast<std::size_t>(source_width) +
      static_cast<std::size_t>(source_x)];
}

int bounded_ceil(double value, int maximum, bool* valid) {
  if (!std::isfinite(value) || value < 0.0 || value > static_cast<double>(maximum) + 1.0) {
    *valid = false;
    return 0;
  }
  return std::min(maximum + 1, static_cast<int>(std::ceil(value)));
}

double shadow_angle(double local_angle, bool use_global_light, const LayerStyleRenderOptions& options) {
  return use_global_light ? options.global_light_angle_degrees : local_angle;
}

bool add_padding(Padding* padding, double left, double right, double top, double bottom,
    const LayerStyleRenderOptions& options) {
  if (padding == nullptr) return false;
  bool valid = true;
  const auto add = [&](int* target, double amount) {
    const int addition = bounded_ceil(amount, options.maximum_padding, &valid);
    if (!valid || addition > options.maximum_padding) { valid = false; return; }
    *target = std::max(*target, addition);
  };
  add(&padding->left, left);
  add(&padding->right, right);
  add(&padding->top, top);
  add(&padding->bottom, bottom);
  return valid;
}

bool compute_padding(std::span<const LayerStyleOperation> operations,
    const LayerStyleRenderOptions& options, Padding* padding, std::string* error) {
  for (const auto& operation : operations) {
    if (!operation.enabled || operation.opacity <= 0.0) continue;
    if (!is_known_layer_style(operation.type_id, operation.algorithm_version)) continue;
    if (operation.type_id == "stroke") {
      const auto& value = std::get<LayerStyleStroke>(operation.parameters);
      const auto scaled_width_x = value.width * options.document_scale_x;
      const auto scaled_width_y = value.width * options.document_scale_y;
      if (scaled_width_x > options.maximum_padding || scaled_width_y > options.maximum_padding)
        return fail(error, "Layer-style stroke work radius exceeds the padding limit.");
      const auto fraction = value.alignment == LayerStyleStrokeAlignment::Outside ? 1.0 :
          value.alignment == LayerStyleStrokeAlignment::Center ? 0.5 : 0.0;
      if (!add_padding(padding, value.width * options.document_scale_x * fraction,
              value.width * options.document_scale_x * fraction,
              value.width * options.document_scale_y * fraction,
              value.width * options.document_scale_y * fraction, options))
        return fail(error, "Layer-style stroke exceeds the padding limit.");
    } else if (operation.type_id == "drop_shadow") {
      const auto& value = std::get<LayerStyleDropShadow>(operation.parameters);
      const auto angle = shadow_angle(value.angle_degrees, value.use_global_light, options) *
          std::numbers::pi / 180.0;
      const auto offset_x = std::cos(angle) * value.distance * options.document_scale_x;
      const auto offset_y = std::sin(angle) * value.distance * options.document_scale_y;
      const auto blur_x = 3.0 * value.blur * options.document_scale_x +
          std::max(0.0, value.spread * options.document_scale_x);
      const auto blur_y = 3.0 * value.blur * options.document_scale_y +
          std::max(0.0, value.spread * options.document_scale_y);
      if (3.0 * value.blur * std::max(options.document_scale_x, options.document_scale_y) >
              options.maximum_padding || std::abs(value.spread) *
              std::max(options.document_scale_x, options.document_scale_y) > options.maximum_padding)
        return fail(error, "Layer-style drop-shadow work radius exceeds the padding limit.");
      if (!add_padding(padding, blur_x + std::max(0.0, -offset_x), blur_x + std::max(0.0, offset_x),
              blur_y + std::max(0.0, -offset_y), blur_y + std::max(0.0, offset_y), options))
        return fail(error, "Layer-style drop shadow exceeds the padding limit.");
    } else if (operation.type_id == "inner_shadow") {
      const auto& value = std::get<LayerStyleInnerShadow>(operation.parameters);
      if (3.0 * value.blur * std::max(options.document_scale_x, options.document_scale_y) >
              options.maximum_padding || std::abs(value.spread) *
              std::max(options.document_scale_x, options.document_scale_y) > options.maximum_padding)
        return fail(error, "Layer-style inner-shadow work radius exceeds the padding limit.");
    } else if (operation.type_id == "inner_glow") {
      const auto& value = std::get<LayerStyleInnerGlow>(operation.parameters);
      if (3.0 * value.blur * std::max(options.document_scale_x, options.document_scale_y) >
              options.maximum_padding || std::abs(value.spread) *
              std::max(options.document_scale_x, options.document_scale_y) > options.maximum_padding)
        return fail(error, "Layer-style inner-glow work radius exceeds the padding limit.");
    } else if (operation.type_id == "outer_glow") {
      const auto& value = std::get<LayerStyleOuterGlow>(operation.parameters);
      if (3.0 * value.blur * std::max(options.document_scale_x, options.document_scale_y) >
              options.maximum_padding || std::abs(value.spread) *
              std::max(options.document_scale_x, options.document_scale_y) > options.maximum_padding)
        return fail(error, "Layer-style outer-glow work radius exceeds the padding limit.");
      const auto extent_x = 3.0 * value.blur * options.document_scale_x +
          std::max(0.0, value.spread * options.document_scale_x);
      const auto extent_y = 3.0 * value.blur * options.document_scale_y +
          std::max(0.0, value.spread * options.document_scale_y);
      if (!add_padding(padding, extent_x, extent_x, extent_y, extent_y, options))
        return fail(error, "Layer-style outer glow exceeds the padding limit.");
    } else if (operation.type_id == "bevel_emboss") {
      const auto& value = std::get<LayerStyleBevelEmboss>(operation.parameters);
      if (3.0 * std::max(value.softness, value.size * 0.15) *
              std::max(options.document_scale_x, options.document_scale_y) > options.maximum_padding)
        return fail(error, "Layer-style bevel work radius exceeds the padding limit.");
      if (value.mode == LayerStyleBevelMode::Outer || value.mode == LayerStyleBevelMode::Emboss) {
        const auto extent_x = value.size * options.document_scale_x;
        const auto extent_y = value.size * options.document_scale_y;
        if (!add_padding(padding, extent_x, extent_x, extent_y, extent_y, options))
          return fail(error, "Layer-style bevel exceeds the padding limit.");
      }
    }
  }
  return true;
}

std::vector<float> gaussian_kernel(double sigma, int* radius) {
  if (radius == nullptr || !std::isfinite(sigma) || sigma < 0.0) return {};
  *radius = static_cast<int>(std::ceil(3.0 * sigma));
  if (*radius <= 0) return {1.0F};
  const auto diameter = static_cast<std::size_t>(*radius * 2 + 1);
  std::vector<float> kernel(diameter);
  double sum = 0.0;
  for (int offset = -*radius; offset <= *radius; ++offset) {
    const auto x = static_cast<double>(offset);
    const auto weight = std::exp(-(x * x) / (2.0 * sigma * sigma));
    kernel[static_cast<std::size_t>(offset + *radius)] = static_cast<float>(weight);
    sum += weight;
  }
  for (auto& value : kernel) value = static_cast<float>(static_cast<double>(value) / sum);
  return kernel;
}

void gaussian_alpha(const std::vector<float>& input, std::vector<float>* temporary,
    std::vector<float>* output, int width, int height, double sigma_x, double sigma_y,
    CancellationToken cancellation) {
  if (temporary == nullptr || output == nullptr) return;
  int radius_x = 0;
  int radius_y = 0;
  const auto kernel_x = gaussian_kernel(sigma_x, &radius_x);
  const auto kernel_y = gaussian_kernel(sigma_y, &radius_y);
  if (kernel_x.empty() || kernel_y.empty()) return;
  for (int y = 0; y < height; ++y) {
    if (cancellation.cancelled()) return;
    for (int x = 0; x < width; ++x) {
      if ((x & 4095) == 0 && cancellation.cancelled()) return;
      double sum = 0.0;
      for (int k = -radius_x; k <= radius_x; ++k) {
        const int sample_x = x + k;
        if (sample_x < 0 || sample_x >= width) continue;
        const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
            static_cast<std::size_t>(sample_x);
        sum += static_cast<double>(input[index]) * kernel_x[static_cast<std::size_t>(k + radius_x)];
      }
      (*temporary)[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
          static_cast<std::size_t>(x)] = static_cast<float>(sum);
    }
  }
  for (int y = 0; y < height; ++y) {
    if (cancellation.cancelled()) return;
    for (int x = 0; x < width; ++x) {
      if ((x & 4095) == 0 && cancellation.cancelled()) return;
      double sum = 0.0;
      for (int k = -radius_y; k <= radius_y; ++k) {
        const int sample_y = y + k;
        if (sample_y < 0 || sample_y >= height) continue;
        const auto index = static_cast<std::size_t>(sample_y) * static_cast<std::size_t>(width) +
            static_cast<std::size_t>(x);
        sum += static_cast<double>((*temporary)[index]) * kernel_y[static_cast<std::size_t>(k + radius_y)];
      }
      (*output)[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
          static_cast<std::size_t>(x)] = static_cast<float>(sum);
    }
  }
}

void morph_line(const float* input, int length, int radius, bool dilate,
    float* output, int input_stride, int output_stride, CancellationToken cancellation) {
  if (input == nullptr || output == nullptr || length <= 0) return;
  if (radius <= 0) {
    for (int i = 0; i < length; ++i) {
      if ((i & 255) == 0 && cancellation.cancelled()) return;
      output[static_cast<std::ptrdiff_t>(i) * output_stride] =
          input[static_cast<std::ptrdiff_t>(i) * input_stride];
    }
    return;
  }
  std::deque<int> candidates;
  const int begin = -radius;
  const int end = length + radius - 1;
  for (int position = begin; position <= end; ++position) {
    if ((position & 255) == 0 && cancellation.cancelled()) return;
    const auto value = position < 0 || position >= length ? 0.0F :
        input[static_cast<std::ptrdiff_t>(position) * input_stride];
    while (!candidates.empty()) {
      const auto tail = candidates.back();
      const auto tail_value = tail < 0 || tail >= length ? 0.0F :
          input[static_cast<std::ptrdiff_t>(tail) * input_stride];
      if (dilate ? tail_value > value : tail_value < value) break;
      candidates.pop_back();
    }
    candidates.push_back(position);
    const auto first = position - radius * 2;
    while (!candidates.empty() && candidates.front() < first) candidates.pop_front();
    const auto output_position = position - radius;
    if (output_position >= 0 && output_position < length && !candidates.empty())
      output[static_cast<std::ptrdiff_t>(output_position) * output_stride] =
          candidates.front() < 0 || candidates.front() >= length
              ? 0.0F : input[static_cast<std::ptrdiff_t>(candidates.front()) * input_stride];
  }
}

void morph_line_fractional(const float* input, int length, double radius, bool dilate,
    float* output, int input_stride, int output_stride, CancellationToken cancellation) {
  if (input == nullptr || output == nullptr || length <= 0 || !std::isfinite(radius) || radius < 0.0)
    return;
  const auto lower_radius = static_cast<int>(std::floor(radius));
  const auto upper_radius = static_cast<int>(std::ceil(radius));
  if (lower_radius == upper_radius) {
    morph_line(input, length, lower_radius, dilate, output, input_stride, output_stride, cancellation);
    return;
  }

  const auto fraction = radius - static_cast<double>(lower_radius);
  const auto value_at = [&](int position) {
    return position < 0 || position >= length ? 0.0F :
        input[static_cast<std::ptrdiff_t>(position) * input_stride];
  };
  const auto push_candidate = [&](std::deque<int>* candidates, int position) {
    if (candidates == nullptr) return;
    const auto value = value_at(position);
    while (!candidates->empty()) {
      const auto tail_value = value_at(candidates->back());
      if (dilate ? tail_value > value : tail_value < value) break;
      candidates->pop_back();
    }
    candidates->push_back(position);
  };
  std::deque<int> lower_candidates;
  std::deque<int> upper_candidates;
  for (int position = -lower_radius; position <= lower_radius; ++position)
    push_candidate(&lower_candidates, position);
  for (int position = -upper_radius; position <= upper_radius; ++position)
    push_candidate(&upper_candidates, position);

  for (int position = 0; position < length; ++position) {
    if ((position & 255) == 0 && cancellation.cancelled()) return;
    if (position > 0) {
      const auto lower_first = position - lower_radius;
      while (!lower_candidates.empty() && lower_candidates.front() < lower_first)
        lower_candidates.pop_front();
      push_candidate(&lower_candidates, position + lower_radius);
      const auto upper_first = position - upper_radius;
      while (!upper_candidates.empty() && upper_candidates.front() < upper_first)
        upper_candidates.pop_front();
      push_candidate(&upper_candidates, position + upper_radius);
    }
    if (lower_candidates.empty() || upper_candidates.empty()) continue;
    const auto lower = value_at(lower_candidates.front());
    const auto upper = value_at(upper_candidates.front());
    const auto coverage = static_cast<double>(lower) +
        (static_cast<double>(upper) - static_cast<double>(lower)) * fraction;
    output[static_cast<std::ptrdiff_t>(position) * output_stride] =
        static_cast<float>(std::clamp(coverage, 0.0, 1.0));
  }
}

void morph_alpha_fractional(const std::vector<float>& input, std::vector<float>* temporary,
    std::vector<float>* output, int width, int height, double radius_x, double radius_y,
    bool dilate, CancellationToken cancellation) {
  if (temporary == nullptr || output == nullptr) return;
  for (int y = 0; y < height; ++y) {
    if (cancellation.cancelled()) return;
    const auto offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
    morph_line_fractional(input.data() + offset, width, radius_x, dilate,
        temporary->data() + offset, 1, 1, cancellation);
  }
  for (int x = 0; x < width; ++x) {
    if (cancellation.cancelled()) return;
    morph_line_fractional(temporary->data() + static_cast<std::size_t>(x), height,
        radius_y, dilate, output->data() + static_cast<std::size_t>(x), width, width, cancellation);
  }
}

void shift_alpha(const std::vector<float>& input, std::vector<float>* output,
    int width, int height, double offset_x, double offset_y, CancellationToken cancellation) {
  if (output == nullptr) return;
  for (int y = 0; y < height; ++y) {
    if (cancellation.cancelled()) return;
    for (int x = 0; x < width; ++x) {
      if ((x & 4095) == 0 && cancellation.cancelled()) return;
      const double source_x = static_cast<double>(x) - offset_x;
      const double source_y = static_cast<double>(y) - offset_y;
      const auto x0 = static_cast<int>(std::floor(source_x));
      const auto y0 = static_cast<int>(std::floor(source_y));
      const auto fx = source_x - x0;
      const auto fy = source_y - y0;
      const auto sample = [&](int sx, int sy) {
        if (sx < 0 || sy < 0 || sx >= width || sy >= height) return 0.0;
        return static_cast<double>(input[static_cast<std::size_t>(sy) * static_cast<std::size_t>(width) +
            static_cast<std::size_t>(sx)]);
      };
      const auto top = sample(x0, y0) * (1.0 - fx) + sample(x0 + 1, y0) * fx;
      const auto bottom = sample(x0, y0 + 1) * (1.0 - fx) + sample(x0 + 1, y0 + 1) * fx;
      (*output)[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
          static_cast<std::size_t>(x)] = static_cast<float>(std::clamp(top * (1.0 - fy) + bottom * fy, 0.0, 1.0));
    }
  }
}

double blend_channel(double base, double source, LayerStyleBlendMode mode) {
  switch (mode) {
    case LayerStyleBlendMode::Multiply: return base * source;
    case LayerStyleBlendMode::Screen: return base + source - base * source;
    case LayerStyleBlendMode::Overlay:
      return base <= 0.5 ? 2.0 * base * source : 1.0 - 2.0 * (1.0 - base) * (1.0 - source);
    case LayerStyleBlendMode::SoftLight:
      if (source <= 0.5) return base - (1.0 - 2.0 * source) * base * (1.0 - base);
      return base + (2.0 * source - 1.0) *
          ((base <= 0.25 ? ((16.0 * base - 12.0) * base + 4.0) * base : std::sqrt(std::max(0.0, base))) - base);
    case LayerStyleBlendMode::Normal: return source;
  }
  return source;
}

float bounded_channel(double value) {
  if (std::isnan(value)) return 0.0F;
  const auto limit = static_cast<double>(std::numeric_limits<float>::max());
  return static_cast<float>(std::clamp(value, -limit, limit));
}

void composite_over(ColorPixel* destination, const LayerStyleColor& color, double coverage,
    LayerStyleBlendMode mode) {
  if (destination == nullptr || coverage <= 0.0) return;
  const auto source_alpha = std::clamp(coverage * color.alpha, 0.0, 1.0);
  const auto dest_alpha = std::clamp(static_cast<double>(destination->alpha), 0.0, 1.0);
  if (source_alpha <= 0.0) return;
  const auto source_red = color.red;
  const auto source_green = color.green;
  const auto source_blue = color.blue;
  const auto base_red = static_cast<double>(destination->red);
  const auto base_green = static_cast<double>(destination->green);
  const auto base_blue = static_cast<double>(destination->blue);
  const auto output_alpha = source_alpha + dest_alpha * (1.0 - source_alpha);
  const auto composite_channel = [&](double base, double source) {
    const auto mixed = blend_channel(base, source, mode);
    const auto premultiplied = (1.0 - source_alpha) * dest_alpha * base +
        (1.0 - dest_alpha) * source_alpha * source + source_alpha * dest_alpha * mixed;
    return output_alpha > 1e-12 ? premultiplied / output_alpha : 0.0;
  };
  destination->red = bounded_channel(composite_channel(base_red, source_red));
  destination->green = bounded_channel(composite_channel(base_green, source_green));
  destination->blue = bounded_channel(composite_channel(base_blue, source_blue));
  destination->alpha = static_cast<float>(output_alpha);
}

void modify_content(ColorPixel* destination, const LayerStyleColor& color,
    double coverage, LayerStyleBlendMode mode) {
  if (destination == nullptr || destination->alpha <= 0.0F || coverage <= 0.0) return;
  const auto amount = std::clamp(coverage * color.alpha, 0.0, 1.0);
  const auto blend = [&](float base, double source) {
    const auto value = static_cast<double>(base);
    return bounded_channel(value + (blend_channel(value, source, mode) - value) * amount);
  };
  destination->red = blend(destination->red, color.red);
  destination->green = blend(destination->green, color.green);
  destination->blue = blend(destination->blue, color.blue);
}

LayerStyleColor gradient_sample(const LayerStyleGradient& gradient, int x, int y,
    int content_width, int content_height) {
  const auto angle = gradient.angle_degrees * std::numbers::pi / 180.0;
  const auto direction_x = std::cos(angle);
  const auto direction_y = std::sin(angle);
  const auto center_x = static_cast<double>(content_width - 1) * 0.5;
  const auto center_y = static_cast<double>(content_height - 1) * 0.5;
  const auto half_width = std::max(0.5, center_x);
  const auto half_height = std::max(0.5, center_y);
  const auto extent = std::abs(direction_x) * half_width + std::abs(direction_y) * half_height;
  auto position = 0.5 + ((static_cast<double>(x) - center_x) * direction_x +
      (static_cast<double>(y) - center_y) * direction_y) / (2.0 * extent * gradient.scale);
  if (gradient.reverse) position = 1.0 - position;
  position = std::clamp(position, 0.0, 1.0);
  const auto upper = std::lower_bound(gradient.stops.begin(), gradient.stops.end(), position,
      [](const LayerStyleGradientStop& stop, double value) { return stop.position < value; });
  if (upper == gradient.stops.begin()) return upper->color;
  if (upper == gradient.stops.end()) return gradient.stops.back().color;
  const auto& right = *upper;
  const auto& left = *(upper - 1);
  const auto amount = (position - left.position) / (right.position - left.position);
  const auto mix = [&](double a, double b) { return a + (b - a) * amount; };
  return {mix(left.color.red, right.color.red), mix(left.color.green, right.color.green),
      mix(left.color.blue, right.color.blue), mix(left.color.alpha, right.color.alpha)};
}

LayerStyleColor fill_sample(const LayerStyleFill& fill, int x, int y,
    int content_width, int content_height) {
  if (const auto* color = std::get_if<LayerStyleColor>(&fill)) return *color;
  return gradient_sample(std::get<LayerStyleGradient>(fill), x, y, content_width, content_height);
}

const LayerStyleMaskBinding* operation_mask(const LayerStyleOperation& operation,
    std::span<const LayerStyleMaskBinding> masks) {
  return find_mask(masks, operation.mask_ref);
}

void build_masked_alpha(const std::vector<float>& source_alpha, std::vector<float>* output,
    const LayerStyleMaskBinding* mask, int width, int height, int source_width,
    int source_height, int padding_left, int padding_top, CancellationToken cancellation) {
  if (output == nullptr) return;
  for (int y = 0; y < height; ++y) {
    if (cancellation.cancelled()) return;
    const auto row = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
    for (int x = 0; x < width; ++x) {
      if ((x & 4095) == 0 && cancellation.cancelled()) return;
      const auto index = row + static_cast<std::size_t>(x);
      (*output)[index] = static_cast<float>(source_alpha[index] *
          mask_value_at(mask, x, y, source_width, source_height, padding_left, padding_top));
    }
  }
}

void apply_spread(std::vector<float>* alpha, std::vector<float>* temporary,
    std::vector<float>* output, double spread_x, double spread_y, double scale_x,
    double scale_y, int width, int height, CancellationToken cancellation) {
  if (alpha == nullptr || temporary == nullptr || output == nullptr ||
      (std::abs(spread_x) < 1e-12 && std::abs(spread_y) < 1e-12)) return;
  const bool dilate = spread_x >= 0.0 && spread_y >= 0.0;
  const auto radius_x = std::abs(spread_x) * scale_x;
  const auto radius_y = std::abs(spread_y) * scale_y;
  morph_alpha_fractional(*alpha, temporary, output, width, height, radius_x, radius_y,
      dilate, cancellation);
  alpha->swap(*output);
}

void apply_blur(std::vector<float>* alpha, std::vector<float>* temporary,
    std::vector<float>* output, double blur, double scale_x, double scale_y,
    int width, int height, CancellationToken cancellation) {
  if (alpha == nullptr || temporary == nullptr || output == nullptr || blur <= 1e-9) return;
  gaussian_alpha(*alpha, temporary, output, width, height, blur * scale_x,
      blur * scale_y, cancellation);
  alpha->swap(*output);
}

void apply_shift(std::vector<float>* alpha, std::vector<float>* output,
    int width, int height, double x, double y, CancellationToken cancellation) {
  if (alpha == nullptr || output == nullptr || (std::abs(x) < 1e-12 && std::abs(y) < 1e-12)) return;
  shift_alpha(*alpha, output, width, height, x, y, cancellation);
  alpha->swap(*output);
}

void paint_solid_plane(LayerStyleRenderResult* result, const std::vector<float>& alpha,
    const LayerStyleColor& color, const LayerStyleOperation& operation,
    CancellationToken cancellation) {
  if (result == nullptr) return;
  for (int y = 0; y < result->height; ++y) {
    if (cancellation.cancelled()) return;
    const auto row = static_cast<std::size_t>(y) * static_cast<std::size_t>(result->width);
    for (int x = 0; x < result->width; ++x) {
      if ((x & 4095) == 0 && cancellation.cancelled()) return;
      const auto index = row + static_cast<std::size_t>(x);
      composite_over(&result->pixels[index], color,
          static_cast<double>(alpha[index]) * operation.opacity, operation.blend_mode);
    }
  }
}

void paint_fill_plane(LayerStyleRenderResult* result, const std::vector<float>& alpha,
    const LayerStyleFill& fill, const LayerStyleOperation& operation,
    int source_width, int source_height, int padding_left, int padding_top,
    CancellationToken cancellation) {
  if (result == nullptr) return;
  for (int y = 0; y < result->height; ++y) {
    if (cancellation.cancelled()) return;
    const auto row = static_cast<std::size_t>(y) * static_cast<std::size_t>(result->width);
    for (int x = 0; x < result->width; ++x) {
      if ((x & 4095) == 0 && cancellation.cancelled()) return;
      const auto index = row + static_cast<std::size_t>(x);
      const auto color = fill_sample(fill, x - padding_left, y - padding_top, source_width, source_height);
      composite_over(&result->pixels[index], color,
          static_cast<double>(alpha[index]) * operation.opacity, operation.blend_mode);
    }
  }
}

void blend_overlay_on_content(std::vector<ColorPixel>* content, std::span<const ColorPixel> source,
    int width, int height, int output_width, int padding_left, int padding_top,
    const std::vector<float>& alpha, const LayerStyleColor* color,
    const LayerStyleGradient* gradient, const LayerStyleOperation& operation,
    const LayerStyleMaskBinding* mask, int source_width, int source_height,
    CancellationToken cancellation) {
  if (content == nullptr) return;
  for (int y = 0; y < height; ++y) {
    if (cancellation.cancelled()) return;
    for (int x = 0; x < width; ++x) {
      if ((x & 4095) == 0 && cancellation.cancelled()) return;
      const auto source_index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
          static_cast<std::size_t>(x);
      const auto output_index = static_cast<std::size_t>(y + padding_top) *
          static_cast<std::size_t>(output_width) + static_cast<std::size_t>(x + padding_left);
      auto fill = color != nullptr ? *color : gradient_sample(*gradient, x, y, source_width, source_height);
      const auto source_alpha = static_cast<double>(source[source_index].alpha);
      const auto coverage = source_alpha > 1e-12
          ? std::clamp(static_cast<double>(alpha[output_index]) / source_alpha, 0.0, 1.0) * operation.opacity
          : 0.0;
      modify_content(&(*content)[source_index], fill, coverage, operation.blend_mode);
    }
  }
  (void)mask;
  (void)source_height;
}

void modify_content_with_mask(std::vector<ColorPixel>* content, int width, int height,
    int output_width, int padding_left, int padding_top, const std::vector<float>& alpha,
    const std::vector<float>& source_alpha,
    const LayerStyleColor& color, const LayerStyleOperation& operation,
    CancellationToken cancellation) {
  if (content == nullptr) return;
  for (int y = 0; y < height; ++y) {
    if (cancellation.cancelled()) return;
    for (int x = 0; x < width; ++x) {
      if ((x & 4095) == 0 && cancellation.cancelled()) return;
      const auto source_index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
          static_cast<std::size_t>(x);
      const auto output_index = static_cast<std::size_t>(y + padding_top) *
          static_cast<std::size_t>(output_width) + static_cast<std::size_t>(x + padding_left);
      const auto base_alpha = source_alpha[output_index];
      if (base_alpha <= 1e-8F) continue;
      const auto edge_ratio = std::clamp(static_cast<double>(alpha[output_index]) / base_alpha, 0.0, 1.0);
      modify_content(&(*content)[source_index], color, edge_ratio * operation.opacity,
          operation.blend_mode);
    }
  }
}

void modify_content_fill(std::vector<ColorPixel>* content, int width, int height,
    int output_width, int padding_left, int padding_top, const std::vector<float>& alpha,
    const std::vector<float>& source_alpha, const LayerStyleFill& fill,
    const LayerStyleOperation& operation, CancellationToken cancellation) {
  if (content == nullptr) return;
  for (int y = 0; y < height; ++y) {
    if (cancellation.cancelled()) return;
    for (int x = 0; x < width; ++x) {
      if ((x & 4095) == 0 && cancellation.cancelled()) return;
      const auto source_index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
          static_cast<std::size_t>(x);
      const auto output_index = static_cast<std::size_t>(y + padding_top) *
          static_cast<std::size_t>(output_width) + static_cast<std::size_t>(x + padding_left);
      const auto base_alpha = source_alpha[output_index];
      if (base_alpha <= 1e-8F) continue;
      const auto edge_ratio = std::clamp(static_cast<double>(alpha[output_index]) / base_alpha, 0.0, 1.0);
      const auto color = fill_sample(fill, x, y, width, height);
      modify_content(&(*content)[source_index], color, edge_ratio * operation.opacity,
          operation.blend_mode);
    }
  }
}

void render_bevel(LayerStyleRenderResult* result, std::vector<ColorPixel>* content,
    const std::vector<float>& source_alpha, std::vector<float>* working_alpha,
    std::vector<float>* temporary, std::vector<float>* output,
    int source_width, int source_height, int padding_left, int padding_top,
    const LayerStyleOperation& operation, const LayerStyleBevelEmboss& bevel,
    const LayerStyleMaskBinding* mask, const LayerStyleRenderOptions& options,
    bool on_content) {
  if (working_alpha == nullptr || temporary == nullptr || output == nullptr) return;
  *working_alpha = source_alpha;
  const auto smoothing = std::max(bevel.softness, bevel.size * 0.15);
  const auto smoothing_x = smoothing * options.document_scale_x;
  const auto smoothing_y = smoothing * options.document_scale_y;
  if (smoothing_x > 1e-9 || smoothing_y > 1e-9) {
    gaussian_alpha(*working_alpha, temporary, output, result->width, result->height,
        smoothing_x, smoothing_y, options.cancellation);
    working_alpha->swap(*output);
  }
  const auto light_angle = shadow_angle(bevel.angle_degrees, bevel.use_global_light, options) *
      std::numbers::pi / 180.0;
  const auto light_x = std::cos(light_angle);
  const auto light_y = std::sin(light_angle);
  for (int y = 0; y < result->height; ++y) {
    if (options.cancellation.cancelled()) return;
    for (int x = 0; x < result->width; ++x) {
      if ((x & 4095) == 0 && options.cancellation.cancelled()) return;
      const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(result->width) +
          static_cast<std::size_t>(x);
      const auto sample = [&](int sx, int sy) {
        if (sx < 0 || sy < 0 || sx >= result->width || sy >= result->height) return 0.0;
        return static_cast<double>((*working_alpha)[static_cast<std::size_t>(sy) *
            static_cast<std::size_t>(result->width) + static_cast<std::size_t>(sx)]);
      };
      const auto dx = (sample(x + 1, y) - sample(x - 1, y)) * 0.5 * options.document_scale_x;
      const auto dy = (sample(x, y + 1) - sample(x, y - 1)) * 0.5 * options.document_scale_y;
      const auto gradient_length = std::sqrt(dx * dx + dy * dy);
      const auto source_coverage = static_cast<double>(source_alpha[index]);
      const auto surface = bevel.mode == LayerStyleBevelMode::Inner ? source_coverage :
          bevel.mode == LayerStyleBevelMode::Outer ? 1.0 - source_coverage : 1.0;
      const auto normal_length = std::max(gradient_length, 1e-12);
      const auto facing = (-(dx / normal_length) * light_x - (dy / normal_length) * light_y);
      const auto edge = std::clamp(gradient_length * std::max(1.0,
          bevel.size * std::max(options.document_scale_x, options.document_scale_y)), 0.0, 1.0) *
          bevel.depth * surface;
      const auto mask_coverage = mask_value_at(mask, x, y, source_width, source_height,
          padding_left, padding_top);
      const auto highlight_coverage = edge * std::max(0.0, facing) * mask_coverage * operation.opacity;
      const auto shadow_coverage = edge * std::max(0.0, -facing) * mask_coverage * operation.opacity;
      if (on_content) {
        const auto sx = x - padding_left;
        const auto sy = y - padding_top;
        if (sx < 0 || sy < 0 || sx >= source_width || sy >= source_height) continue;
        const auto source_index = static_cast<std::size_t>(sy) * static_cast<std::size_t>(source_width) +
            static_cast<std::size_t>(sx);
        auto& pixel = (*content)[source_index];
        const auto alpha = std::max(1e-12, source_coverage);
        modify_content(&pixel, bevel.shadow, shadow_coverage / alpha, operation.blend_mode);
        modify_content(&pixel, bevel.highlight, highlight_coverage / alpha, operation.blend_mode);
      } else {
        composite_over(&result->pixels[index], bevel.shadow, shadow_coverage, operation.blend_mode);
        composite_over(&result->pixels[index], bevel.highlight, highlight_coverage, operation.blend_mode);
      }
    }
  }
  (void)source_height;
}

bool style_has_on_content_operation(const LayerStyleStack& stack) {
  return std::any_of(stack.operations.begin(), stack.operations.end(), [](const auto& operation) {
    return operation.enabled && operation.opacity > 0.0 &&
        operation.plane == LayerStylePlane::OnContent;
  });
}

}  // namespace

bool is_known_layer_style(std::string_view type_id, std::uint32_t algorithm_version) {
  return algorithm_version == kLayerStyleAlgorithmVersion &&
      (type_id == "stroke" || type_id == "drop_shadow" || type_id == "inner_shadow" ||
       type_id == "inner_glow" || type_id == "outer_glow" || type_id == "color_overlay" ||
       type_id == "gradient_overlay" || type_id == "bevel_emboss");
}

bool validate_layer_style_stack(const LayerStyleStack& stack, std::string* error) {
  if (error != nullptr) error->clear();
  return validate_ids(stack, error);
}

LayerStylePreflightResult preflight_layer_style_stack(int width, int height,
    const LayerStyleStack& stack, const LayerStyleRenderOptions& options) {
  if (options.cancellation.cancelled())
    return preflight_failure(LayerStyleRenderStatus::Cancelled, "Layer-style preflight was cancelled.");
  std::string validation_error;
  try {
    if (!validate_ids(stack, &validation_error))
      return preflight_failure(LayerStyleRenderStatus::Invalid, std::move(validation_error));
  } catch (const std::bad_alloc&) {
    return preflight_failure(LayerStyleRenderStatus::ResourceLimit,
        "Layer-style preflight could not validate the operation list.");
  }
  if (width <= 0 || height <= 0 || options.maximum_padding < 0 ||
      options.maximum_padding > 16384 || options.maximum_output_pixels == 0U ||
      options.maximum_scratch_bytes == 0U ||
      !finite_range(options.document_scale_x, 1e-6, 1e6) ||
      !finite_range(options.document_scale_y, 1e-6, 1e6) ||
      !finite_range(options.global_light_angle_degrees, -3600.0, 3600.0))
    return preflight_failure(LayerStyleRenderStatus::Invalid,
        "Invalid layer-style preflight dimensions or options.");

  const auto source_pixels = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
  if (source_pixels > options.maximum_output_pixels)
    return preflight_failure(LayerStyleRenderStatus::ResourceLimit,
        "Layer-style input exceeds the pixel budget.");

  const bool active = stack.enabled && std::any_of(stack.operations.begin(), stack.operations.end(),
      [](const auto& operation) { return operation.enabled && operation.opacity > 0.0; });
  for (const auto& operation : stack.operations) {
    if (options.cancellation.cancelled())
      return preflight_failure(LayerStyleRenderStatus::Cancelled, "Layer-style preflight was cancelled.");
    if (active && operation.enabled && operation.opacity > 0.0 &&
        !is_known_layer_style(operation.type_id, operation.algorithm_version))
      return preflight_failure(LayerStyleRenderStatus::Unsupported,
          "An enabled layer-style type or algorithm version is unsupported.");
  }

  LayerStylePreflightResult result;
  result.status = active ? LayerStyleRenderStatus::Applied : LayerStyleRenderStatus::Identity;
  result.output_width = width;
  result.output_height = height;
  if (!active) return result;

  Padding padding;
  if (!compute_padding(stack.operations, options, &padding, &validation_error))
    return preflight_failure(LayerStyleRenderStatus::ResourceLimit, std::move(validation_error));
  const auto output_width_64 = static_cast<std::int64_t>(width) + padding.left + padding.right;
  const auto output_height_64 = static_cast<std::int64_t>(height) + padding.top + padding.bottom;
  if (output_width_64 <= 0 || output_height_64 <= 0 ||
      output_width_64 > std::numeric_limits<int>::max() ||
      output_height_64 > std::numeric_limits<int>::max())
    return preflight_failure(LayerStyleRenderStatus::ResourceLimit,
        "Expanded layer-style dimensions overflow.");

  result.output_width = static_cast<int>(output_width_64);
  result.output_height = static_cast<int>(output_height_64);
  result.padding_left = padding.left;
  result.padding_right = padding.right;
  result.padding_top = padding.top;
  result.padding_bottom = padding.bottom;
  const auto output_pixels = static_cast<std::uint64_t>(result.output_width) *
      static_cast<std::uint64_t>(result.output_height);
  if (output_pixels > options.maximum_output_pixels) {
    result.status = LayerStyleRenderStatus::ResourceLimit;
    result.error = "Expanded layer-style output exceeds the pixel budget.";
    return result;
  }

  constexpr std::size_t kAlphaScratchBytesPerPixel = sizeof(float) * 4U;
  const auto maximum_scratch = static_cast<std::uint64_t>(options.maximum_scratch_bytes);
  if (output_pixels > maximum_scratch / kAlphaScratchBytesPerPixel) {
    result.status = LayerStyleRenderStatus::ResourceLimit;
    result.error = "Layer-style scratch storage exceeds the memory budget.";
    return result;
  }
  const auto alpha_scratch_bytes = output_pixels * kAlphaScratchBytesPerPixel;
  if (style_has_on_content_operation(stack)) {
    const auto remaining_scratch = maximum_scratch - alpha_scratch_bytes;
    if (source_pixels > remaining_scratch / sizeof(ColorPixel)) {
      result.status = LayerStyleRenderStatus::ResourceLimit;
      result.error = "Layer-style scratch storage exceeds the memory budget.";
      return result;
    }
  }
  if (options.cancellation.cancelled())
    return preflight_failure(LayerStyleRenderStatus::Cancelled, "Layer-style preflight was cancelled.");
  return result;
}

std::optional<std::string> serialize_layer_style_stack(const LayerStyleStack& stack,
    std::string* error) {
  if (error != nullptr) error->clear();
  if (!validate_ids(stack, error)) return std::nullopt;
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(std::numeric_limits<double>::max_digits10);
  stream << kMagic << ' ' << stack.version << ' ' << (stack.enabled ? 1 : 0) << ' '
         << stack.operations.size() << '\n';
  for (const auto& operation : stack.operations) {
    const auto parameters = encode_parameters(operation);
    if (parameters.size() > kMaximumParameterBytes) {
      fail(error, "Layer-style parameters exceed the size limit.");
      return std::nullopt;
    }
    stream << std::quoted(operation.id) << ' ' << std::quoted(operation.type_id) << ' '
           << operation.algorithm_version << ' ' << (operation.enabled ? 1 : 0) << ' '
           << operation.opacity << ' ' << static_cast<unsigned>(operation.plane) << ' '
           << static_cast<unsigned>(operation.blend_mode) << ' ' << std::quoted(operation.mask_ref) << ' '
           << std::quoted(parameters) << '\n';
    const auto written = static_cast<std::streamoff>(stream.tellp());
    if (written < 0 || static_cast<std::uint64_t>(written) > kMaximumPayloadBytes) {
      fail(error, "Layer-style stack exceeds the payload size limit.");
      return std::nullopt;
    }
  }
  auto payload = stream.str();
  if (payload.size() > kMaximumPayloadBytes) {
    fail(error, "Layer-style stack exceeds the payload size limit.");
    return std::nullopt;
  }
  return payload;
}

std::optional<LayerStyleStack> deserialize_layer_style_stack(std::string_view payload,
    std::string* error) {
  if (error != nullptr) error->clear();
  if (payload.size() > kMaximumPayloadBytes) {
    fail(error, "Layer-style stack exceeds the payload size limit.");
    return std::nullopt;
  }
  std::istringstream stream{std::string(payload)};
  stream.imbue(std::locale::classic());
  std::string magic;
  unsigned enabled = 0;
  std::size_t count = 0;
  LayerStyleStack stack;
  if (!(stream >> magic >> stack.version >> enabled >> count) || magic != kMagic ||
      stack.version != kLayerStyleStackVersion || enabled > 1U || count > kMaximumOperations) {
    fail(error, "Unsupported or invalid layer-style stack envelope.");
    return std::nullopt;
  }
  stack.enabled = enabled != 0U;
  stack.operations.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    LayerStyleOperation operation;
    std::int64_t algorithm = 0;
    unsigned operation_enabled = 0;
    unsigned plane = 0;
    unsigned blend = 0;
    std::string parameters;
    if (!(stream >> std::quoted(operation.id) >> std::quoted(operation.type_id) >> algorithm >>
          operation_enabled >> operation.opacity >> plane >> blend >> std::quoted(operation.mask_ref) >>
          std::quoted(parameters)) || algorithm <= 0 ||
        algorithm > std::numeric_limits<std::uint32_t>::max() || operation_enabled > 1U ||
        plane > static_cast<unsigned>(LayerStylePlane::AboveContent) ||
        blend > static_cast<unsigned>(LayerStyleBlendMode::SoftLight) ||
        parameters.size() > kMaximumParameterBytes) {
      fail(error, "Malformed layer-style operation.");
      return std::nullopt;
    }
    operation.algorithm_version = static_cast<std::uint32_t>(algorithm);
    operation.enabled = operation_enabled != 0U;
    operation.plane = static_cast<LayerStylePlane>(plane);
    operation.blend_mode = static_cast<LayerStyleBlendMode>(blend);
    if (!decode_parameters(&operation, parameters, error)) return std::nullopt;
    stack.operations.push_back(std::move(operation));
  }
  if (!exhausted(stream)) {
    fail(error, "Trailing data after layer-style stack.");
    return std::nullopt;
  }
  if (!validate_ids(stack, error)) return std::nullopt;
  return stack;
}

LayerStyleRenderResult render_layer_styles(std::span<const ColorPixel> source, int width,
    int height, const LayerStyleStack& stack, const LayerStyleRenderOptions& options,
    std::span<const LayerStyleMaskBinding> masks) {
  const auto preflight = preflight_layer_style_stack(width, height, stack, options);
  if (!preflight.ok()) return render_failure(preflight.status, preflight.error);
  std::string validation_error;
  const auto source_pixels = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
  if (source_pixels != source.size())
    return render_failure(LayerStyleRenderStatus::Invalid, "Layer-style source raster dimensions do not match its pixels.");
  if (!mask_bindings_valid(masks, source.size(), options.cancellation, &validation_error)) {
    if (options.cancellation.cancelled()) {
      return render_failure(LayerStyleRenderStatus::Cancelled, "Layer-style rendering was cancelled.");
    }
    return render_failure(LayerStyleRenderStatus::Invalid, std::move(validation_error));
  }
  for (const auto& operation : stack.operations) {
    if (options.cancellation.cancelled())
      return render_failure(LayerStyleRenderStatus::Cancelled, "Layer-style rendering was cancelled.");
    const bool active = stack.enabled && operation.enabled && operation.opacity > 0.0;
    if (active && !operation.mask_ref.empty() && find_mask(masks, operation.mask_ref) == nullptr)
      return render_failure(LayerStyleRenderStatus::Invalid, "A layer-style mask reference could not be resolved.");
  }
  for (std::size_t index = 0; index < source.size(); ++index) {
    if ((index & 4095U) == 0U && options.cancellation.cancelled())
      return render_failure(LayerStyleRenderStatus::Cancelled, "Layer-style rendering was cancelled.");
    const auto& pixel = source[index];
    if (!std::isfinite(pixel.red) || !std::isfinite(pixel.green) || !std::isfinite(pixel.blue) ||
        !finite_range(pixel.alpha, 0.0, 1.0))
      return render_failure(LayerStyleRenderStatus::Invalid,
          "Layer-style input must contain finite working-linear RGB and alpha in [0, 1].");
  }

  if (preflight.status == LayerStyleRenderStatus::Identity) {
    LayerStyleRenderResult result;
    result.status = LayerStyleRenderStatus::Identity;
    result.width = width;
    result.height = height;
    try {
      result.pixels.resize(source.size());
      constexpr std::size_t kCopyChunk = 1U << 20U;
      for (std::size_t offset = 0; offset < source.size(); offset += kCopyChunk) {
        if (options.cancellation.cancelled())
          return render_failure(LayerStyleRenderStatus::Cancelled, "Layer-style rendering was cancelled.");
        const auto count = std::min(kCopyChunk, source.size() - offset);
        std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(offset), count,
            result.pixels.begin() + static_cast<std::ptrdiff_t>(offset));
      }
    } catch (const std::bad_alloc&) {
      return render_failure(LayerStyleRenderStatus::ResourceLimit, "Layer-style identity copy allocation failed.");
    }
    if (options.cancellation.cancelled())
      return render_failure(LayerStyleRenderStatus::Cancelled, "Layer-style rendering was cancelled.");
    return result;
  }

  const auto output_width = preflight.output_width;
  const auto output_height = preflight.output_height;
  const auto output_pixels = static_cast<std::uint64_t>(output_width) *
      static_cast<std::uint64_t>(output_height);
  const Padding padding{preflight.padding_left, preflight.padding_right,
      preflight.padding_top, preflight.padding_bottom};
  const bool has_on_content = style_has_on_content_operation(stack);

  LayerStyleRenderResult result;
  result.status = LayerStyleRenderStatus::Applied;
  result.width = output_width;
  result.height = output_height;
  result.canvas_offset_x = -padding.left;
  result.canvas_offset_y = -padding.top;

  try {
    result.pixels.assign(static_cast<std::size_t>(output_pixels), ColorPixel{0, 0, 0, 0});
    std::vector<float> source_alpha(static_cast<std::size_t>(output_pixels), 0.0F);
    std::vector<float> working(static_cast<std::size_t>(output_pixels), 0.0F);
    std::vector<float> temporary(static_cast<std::size_t>(output_pixels), 0.0F);
    std::vector<float> output(static_cast<std::size_t>(output_pixels), 0.0F);
    std::vector<ColorPixel> styled_content;
    if (has_on_content) {
      styled_content.resize(source.size());
      constexpr std::size_t kCopyChunk = 1U << 20U;
      for (std::size_t offset = 0; offset < source.size(); offset += kCopyChunk) {
        if (options.cancellation.cancelled())
          return render_failure(LayerStyleRenderStatus::Cancelled, "Layer-style rendering was cancelled.");
        const auto count = std::min(kCopyChunk, source.size() - offset);
        std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(offset), count,
            styled_content.begin() + static_cast<std::ptrdiff_t>(offset));
      }
    }

    for (int y = 0; y < height; ++y) {
      if (options.cancellation.cancelled())
        return render_failure(LayerStyleRenderStatus::Cancelled, "Layer-style rendering was cancelled.");
      for (int x = 0; x < width; ++x) {
        const auto source_index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
            static_cast<std::size_t>(x);
        const auto output_index = static_cast<std::size_t>(y + padding.top) *
            static_cast<std::size_t>(output_width) + static_cast<std::size_t>(x + padding.left);
        source_alpha[output_index] = source[source_index].alpha;
      }
    }

    const auto apply_operation = [&](const LayerStyleOperation& operation, LayerStylePlane plane,
                                     bool content_plane) -> bool {
      if (!stack.enabled || !operation.enabled || operation.opacity <= 0.0 || operation.plane != plane) return true;
      const auto* mask = operation_mask(operation, masks);
      build_masked_alpha(source_alpha, &working, mask, output_width, output_height,
          width, height, padding.left, padding.top, options.cancellation);
      if (options.cancellation.cancelled()) return false;
      const auto finish_external = [&](const LayerStyleColor& color) {
        if (content_plane) {
          modify_content_with_mask(&styled_content, width, height, output_width, padding.left,
              padding.top, working, source_alpha, color, operation, options.cancellation);
        } else if (plane == LayerStylePlane::AboveContent || plane == LayerStylePlane::BelowContent) {
          paint_solid_plane(&result, working, color, operation, options.cancellation);
        }
      };

      if (operation.type_id == "stroke") {
        const auto& value = std::get<LayerStyleStroke>(operation.parameters);
        const auto width_x = value.width * options.document_scale_x;
        const auto width_y = value.width * options.document_scale_y;
        auto* stroke_alpha = &working;
        if (value.alignment == LayerStyleStrokeAlignment::Inside) {
          morph_alpha_fractional(working, &temporary, &output, output_width, output_height,
              width_x, width_y, false, options.cancellation);
          working.swap(output);
          for (int y = 0; y < output_height; ++y) {
            if (options.cancellation.cancelled()) return false;
            for (int x = 0; x < output_width; ++x) {
              if ((x & 4095) == 0 && options.cancellation.cancelled()) return false;
              const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(output_width) +
                  static_cast<std::size_t>(x);
              const auto original = static_cast<double>(source_alpha[index]) *
                  mask_value_at(mask, x, y, width, height, padding.left, padding.top);
              working[index] = static_cast<float>(std::max(0.0,
                  original - static_cast<double>(working[index])));
            }
          }
        } else if (value.alignment == LayerStyleStrokeAlignment::Outside) {
          morph_alpha_fractional(working, &temporary, &output, output_width, output_height,
              width_x, width_y, true, options.cancellation);
          working.swap(output);
          for (int y = 0; y < output_height; ++y) {
            if (options.cancellation.cancelled()) return false;
            for (int x = 0; x < output_width; ++x) {
              if ((x & 4095) == 0 && options.cancellation.cancelled()) return false;
              const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(output_width) +
                  static_cast<std::size_t>(x);
              const auto original = static_cast<double>(source_alpha[index]) *
                  mask_value_at(mask, x, y, width, height, padding.left, padding.top);
              working[index] = static_cast<float>(std::max(0.0, static_cast<double>(working[index]) - original));
            }
          }
        } else {
          const auto radius_x = width_x * 0.5;
          const auto radius_y = width_y * 0.5;
          morph_alpha_fractional(working, &temporary, &output, output_width, output_height,
              radius_x, radius_y, true, options.cancellation);
          // Keep the dilated coverage in output while eroding the original
          // masked alpha in working.
          morph_alpha_fractional(working, &temporary, &working, output_width, output_height,
              radius_x, radius_y, false, options.cancellation);
          for (int y = 0; y < output_height; ++y) {
            if (options.cancellation.cancelled()) return false;
            for (int x = 0; x < output_width; ++x) {
              if ((x & 4095) == 0 && options.cancellation.cancelled()) return false;
              const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(output_width) +
                  static_cast<std::size_t>(x);
              const auto original = static_cast<double>(source_alpha[index]) *
                  mask_value_at(mask, x, y, width, height, padding.left, padding.top);
              const auto outside = std::max(0.0, static_cast<double>(output[index]) - original);
              const auto inside = std::max(0.0, original - static_cast<double>(working[index]));
              working[index] = static_cast<float>(std::max(outside, inside));
            }
          }
        }
        if (options.cancellation.cancelled()) return false;
        if (const auto* solid = std::get_if<LayerStyleColor>(&value.fill)) {
          if (content_plane)
            modify_content_with_mask(&styled_content, width, height, output_width, padding.left,
                padding.top, *stroke_alpha, source_alpha, *solid, operation, options.cancellation);
          else paint_solid_plane(&result, *stroke_alpha, *solid, operation, options.cancellation);
        } else if (content_plane) {
          modify_content_fill(&styled_content, width, height, output_width, padding.left,
              padding.top, *stroke_alpha, source_alpha, value.fill, operation, options.cancellation);
        } else {
          paint_fill_plane(&result, *stroke_alpha, value.fill, operation, width, height,
              padding.left, padding.top, options.cancellation);
        }
        return !options.cancellation.cancelled();
      }

      if (operation.type_id == "drop_shadow" || operation.type_id == "inner_shadow") {
        const bool inner = operation.type_id == "inner_shadow";
        const auto& color = inner ? std::get<LayerStyleInnerShadow>(operation.parameters).color
                                  : std::get<LayerStyleDropShadow>(operation.parameters).color;
        const auto distance = inner ? std::get<LayerStyleInnerShadow>(operation.parameters).distance
                                    : std::get<LayerStyleDropShadow>(operation.parameters).distance;
        const auto local_angle = inner ? std::get<LayerStyleInnerShadow>(operation.parameters).angle_degrees
                                       : std::get<LayerStyleDropShadow>(operation.parameters).angle_degrees;
        const auto blur = inner ? std::get<LayerStyleInnerShadow>(operation.parameters).blur
                                : std::get<LayerStyleDropShadow>(operation.parameters).blur;
        const auto spread = inner ? std::get<LayerStyleInnerShadow>(operation.parameters).spread
                                  : std::get<LayerStyleDropShadow>(operation.parameters).spread;
        const auto use_global_light = inner ? std::get<LayerStyleInnerShadow>(operation.parameters).use_global_light
                                            : std::get<LayerStyleDropShadow>(operation.parameters).use_global_light;
        const auto angle = shadow_angle(local_angle, use_global_light, options) *
            std::numbers::pi / 180.0;
        const auto offset_x = std::cos(angle) * distance * options.document_scale_x;
        const auto offset_y = std::sin(angle) * distance * options.document_scale_y;
        if (std::abs(spread) > 1e-9)
          apply_spread(&working, &temporary, &output, spread, spread,
              options.document_scale_x, options.document_scale_y, output_width, output_height,
              options.cancellation);
        apply_shift(&working, &output, output_width, output_height, offset_x, offset_y,
            options.cancellation);
        apply_blur(&working, &temporary, &output, blur, options.document_scale_x,
            options.document_scale_y, output_width, output_height, options.cancellation);
        if (inner) {
          for (int y = 0; y < output_height; ++y) {
            if (options.cancellation.cancelled()) return false;
            for (int x = 0; x < output_width; ++x) {
              if ((x & 4095) == 0 && options.cancellation.cancelled()) return false;
              const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(output_width) +
                  static_cast<std::size_t>(x);
              const auto original = static_cast<double>(source_alpha[index]) *
                  mask_value_at(mask, x, y, width, height, padding.left, padding.top);
              working[index] = static_cast<float>(std::clamp(original *
                  (1.0 - static_cast<double>(working[index])), 0.0, 1.0));
            }
          }
        }
        finish_external(color);
        return !options.cancellation.cancelled();
      }

      if (operation.type_id == "inner_glow" || operation.type_id == "outer_glow") {
        const bool inner = operation.type_id == "inner_glow";
        const auto& color = inner ? std::get<LayerStyleInnerGlow>(operation.parameters).color
                                  : std::get<LayerStyleOuterGlow>(operation.parameters).color;
        const auto blur = inner ? std::get<LayerStyleInnerGlow>(operation.parameters).blur
                                : std::get<LayerStyleOuterGlow>(operation.parameters).blur;
        const auto spread = inner ? std::get<LayerStyleInnerGlow>(operation.parameters).spread
                                  : std::get<LayerStyleOuterGlow>(operation.parameters).spread;
        if (std::abs(spread) > 1e-9)
          apply_spread(&working, &temporary, &output, spread, spread,
              options.document_scale_x, options.document_scale_y, output_width, output_height,
              options.cancellation);
        apply_blur(&working, &temporary, &output, blur, options.document_scale_x,
            options.document_scale_y, output_width, output_height, options.cancellation);
        for (int y = 0; y < output_height; ++y) {
          if (options.cancellation.cancelled()) return false;
          const auto row = static_cast<std::size_t>(y) * static_cast<std::size_t>(output_width);
          for (int x = 0; x < output_width; ++x) {
            if ((x & 4095) == 0 && options.cancellation.cancelled()) return false;
            const auto index = row + static_cast<std::size_t>(x);
            const auto original = static_cast<double>(source_alpha[index]) *
                mask_value_at(mask, x, y, width, height, padding.left, padding.top);
            const auto blurred = static_cast<double>(working[index]);
            working[index] = static_cast<float>(inner
                ? std::clamp(original * (1.0 - blurred), 0.0, 1.0)
                : std::clamp(blurred - original, 0.0, 1.0));
          }
        }
        finish_external(color);
        return !options.cancellation.cancelled();
      }

      if (operation.type_id == "color_overlay" || operation.type_id == "gradient_overlay") {
        if (!content_plane) return fail(&result.error, "Overlays must use the OnContent plane.");
        if (operation.type_id == "color_overlay") {
          const auto& color = std::get<LayerStyleColorOverlay>(operation.parameters).color;
          blend_overlay_on_content(&styled_content, source, width, height, output_width,
              padding.left, padding.top, working, &color, nullptr, operation, mask, width,
              height, options.cancellation);
        } else {
          const auto& gradient = std::get<LayerStyleGradientOverlay>(operation.parameters).gradient;
          blend_overlay_on_content(&styled_content, source, width, height, output_width,
              padding.left, padding.top, working, nullptr, &gradient, operation, mask, width,
              height, options.cancellation);
        }
        return !options.cancellation.cancelled();
      }

      if (operation.type_id == "bevel_emboss") {
        render_bevel(&result, content_plane ? &styled_content : nullptr, source_alpha,
            &working, &temporary, &output, width, height, padding.left, padding.top,
            operation, std::get<LayerStyleBevelEmboss>(operation.parameters), mask,
            options, content_plane);
        return !options.cancellation.cancelled();
      }
      return fail(&result.error, "Unsupported layer-style operation.");
    };

    for (const auto& operation : stack.operations) {
      if (operation.plane == LayerStylePlane::BelowContent &&
          !apply_operation(operation, LayerStylePlane::BelowContent, false))
        return render_failure(options.cancellation.cancelled() ? LayerStyleRenderStatus::Cancelled :
            LayerStyleRenderStatus::Invalid, result.error.empty() ? "Layer-style rendering failed." : result.error);
    }

    for (const auto& operation : stack.operations) {
      if (operation.plane == LayerStylePlane::OnContent &&
          !apply_operation(operation, LayerStylePlane::OnContent, true))
        return render_failure(options.cancellation.cancelled() ? LayerStyleRenderStatus::Cancelled :
            LayerStyleRenderStatus::Invalid, result.error.empty() ? "Layer-style rendering failed." : result.error);
    }

    for (int y = 0; y < height; ++y) {
      if (options.cancellation.cancelled())
        return render_failure(LayerStyleRenderStatus::Cancelled, "Layer-style rendering was cancelled.");
      const auto source_row = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
      const auto output_row = static_cast<std::size_t>(y + padding.top) * static_cast<std::size_t>(output_width) +
          static_cast<std::size_t>(padding.left);
      for (int x = 0; x < width; ++x) {
        if ((x & 4095) == 0 && options.cancellation.cancelled())
          return render_failure(LayerStyleRenderStatus::Cancelled, "Layer-style rendering was cancelled.");
        const auto source_index = source_row + static_cast<std::size_t>(x);
        auto& destination = result.pixels[output_row + static_cast<std::size_t>(x)];
        const auto& pixel = has_on_content ? styled_content[source_index] : source[source_index];
        const auto color = LayerStyleColor{pixel.red, pixel.green, pixel.blue, pixel.alpha};
        composite_over(&destination, color, 1.0, LayerStyleBlendMode::Normal);
      }
    }

    for (const auto& operation : stack.operations) {
      if (operation.plane == LayerStylePlane::AboveContent &&
          !apply_operation(operation, LayerStylePlane::AboveContent, false))
        return render_failure(options.cancellation.cancelled() ? LayerStyleRenderStatus::Cancelled :
            LayerStyleRenderStatus::Invalid, result.error.empty() ? "Layer-style rendering failed." : result.error);
    }
  } catch (const std::bad_alloc&) {
    return render_failure(LayerStyleRenderStatus::ResourceLimit, "Layer-style storage allocation failed.");
  }
  if (options.cancellation.cancelled())
    return render_failure(LayerStyleRenderStatus::Cancelled, "Layer-style rendering was cancelled.");
  return result;
}

}  // namespace photo_editor::pipeline
