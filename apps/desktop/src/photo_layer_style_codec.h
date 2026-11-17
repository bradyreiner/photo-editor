#pragma once

#include "photo_editor/pipeline/layer_styles.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace photo_editor::desktop::layer_style_codec {

using photo_editor::pipeline::LayerStyleBevelEmboss;
using photo_editor::pipeline::LayerStyleBevelMode;
using photo_editor::pipeline::LayerStyleBlendMode;
using photo_editor::pipeline::LayerStyleColor;
using photo_editor::pipeline::LayerStyleColorOverlay;
using photo_editor::pipeline::LayerStyleDropShadow;
using photo_editor::pipeline::LayerStyleFill;
using photo_editor::pipeline::LayerStyleGradient;
using photo_editor::pipeline::LayerStyleGradientOverlay;
using photo_editor::pipeline::LayerStyleGradientStop;
using photo_editor::pipeline::LayerStyleInnerGlow;
using photo_editor::pipeline::LayerStyleInnerShadow;
using photo_editor::pipeline::LayerStyleOperation;
using photo_editor::pipeline::LayerStyleOuterGlow;
using photo_editor::pipeline::LayerStylePlane;
using photo_editor::pipeline::LayerStyleStack;
using photo_editor::pipeline::LayerStyleStroke;
using photo_editor::pipeline::LayerStyleStrokeAlignment;

inline constexpr qsizetype kMaximumEnvelopeBytes = 4 * 1024 * 1024;
inline constexpr auto kEnvelopeSchema = "looksy-layer-style-preset";

inline bool fail(QString* error, const QString& message) {
  if (error != nullptr) *error = message;
  return false;
}

inline QString type_name(std::string_view type_id) {
  if (type_id == "stroke") return QStringLiteral("Stroke");
  if (type_id == "drop_shadow") return QStringLiteral("Drop Shadow");
  if (type_id == "inner_shadow") return QStringLiteral("Inner Shadow");
  if (type_id == "inner_glow") return QStringLiteral("Inner Glow");
  if (type_id == "outer_glow") return QStringLiteral("Outer Glow");
  if (type_id == "color_overlay") return QStringLiteral("Color Overlay");
  if (type_id == "gradient_overlay") return QStringLiteral("Gradient Overlay");
  if (type_id == "bevel_emboss") return QStringLiteral("Bevel and Emboss");
  return QString::fromUtf8(type_id.data(), static_cast<qsizetype>(type_id.size()));
}

inline QString plane_name(LayerStylePlane plane) {
  switch (plane) {
    case LayerStylePlane::BelowContent: return QStringLiteral("belowContent");
    case LayerStylePlane::OnContent: return QStringLiteral("onContent");
    case LayerStylePlane::AboveContent: return QStringLiteral("aboveContent");
  }
  return {};
}

inline bool parse_plane(const QString& value, LayerStylePlane* output) {
  if (output == nullptr) return false;
  if (value == QStringLiteral("belowContent")) *output = LayerStylePlane::BelowContent;
  else if (value == QStringLiteral("onContent")) *output = LayerStylePlane::OnContent;
  else if (value == QStringLiteral("aboveContent")) *output = LayerStylePlane::AboveContent;
  else return false;
  return true;
}

inline QString blend_name(LayerStyleBlendMode mode) {
  switch (mode) {
    case LayerStyleBlendMode::Normal: return QStringLiteral("normal");
    case LayerStyleBlendMode::Multiply: return QStringLiteral("multiply");
    case LayerStyleBlendMode::Screen: return QStringLiteral("screen");
    case LayerStyleBlendMode::Overlay: return QStringLiteral("overlay");
    case LayerStyleBlendMode::SoftLight: return QStringLiteral("softLight");
  }
  return {};
}

inline bool parse_blend(const QString& value, LayerStyleBlendMode* output) {
  if (output == nullptr) return false;
  if (value == QStringLiteral("normal")) *output = LayerStyleBlendMode::Normal;
  else if (value == QStringLiteral("multiply")) *output = LayerStyleBlendMode::Multiply;
  else if (value == QStringLiteral("screen")) *output = LayerStyleBlendMode::Screen;
  else if (value == QStringLiteral("overlay")) *output = LayerStyleBlendMode::Overlay;
  else if (value == QStringLiteral("softLight")) *output = LayerStyleBlendMode::SoftLight;
  else return false;
  return true;
}

inline QString alignment_name(LayerStyleStrokeAlignment value) {
  switch (value) {
    case LayerStyleStrokeAlignment::Inside: return QStringLiteral("inside");
    case LayerStyleStrokeAlignment::Center: return QStringLiteral("center");
    case LayerStyleStrokeAlignment::Outside: return QStringLiteral("outside");
  }
  return {};
}

inline bool parse_alignment(const QString& value, LayerStyleStrokeAlignment* output) {
  if (output == nullptr) return false;
  if (value == QStringLiteral("inside")) *output = LayerStyleStrokeAlignment::Inside;
  else if (value == QStringLiteral("center")) *output = LayerStyleStrokeAlignment::Center;
  else if (value == QStringLiteral("outside")) *output = LayerStyleStrokeAlignment::Outside;
  else return false;
  return true;
}

inline QString bevel_mode_name(LayerStyleBevelMode mode) {
  switch (mode) {
    case LayerStyleBevelMode::Inner: return QStringLiteral("inner");
    case LayerStyleBevelMode::Outer: return QStringLiteral("outer");
    case LayerStyleBevelMode::Emboss: return QStringLiteral("emboss");
  }
  return {};
}

inline bool parse_bevel_mode(const QString& value, LayerStyleBevelMode* output) {
  if (output == nullptr) return false;
  if (value == QStringLiteral("inner")) *output = LayerStyleBevelMode::Inner;
  else if (value == QStringLiteral("outer")) *output = LayerStyleBevelMode::Outer;
  else if (value == QStringLiteral("emboss")) *output = LayerStyleBevelMode::Emboss;
  else return false;
  return true;
}

inline QVariantMap color_to_variant(const LayerStyleColor& color) {
  return {{QStringLiteral("r"), color.red}, {QStringLiteral("g"), color.green},
      {QStringLiteral("b"), color.blue}, {QStringLiteral("a"), color.alpha}};
}

inline bool color_from_variant(const QVariant& value, LayerStyleColor* color,
                               QString* error) {
  const auto map = value.toMap();
  if (color == nullptr || map.isEmpty())
    return fail(error, QStringLiteral("A style color must be an {r,g,b,a} linear-sRGB map."));
  double channels[4]{};
  const char* keys[] = {"r", "g", "b", "a"};
  for (int index = 0; index < 4; ++index) {
    bool ok = false;
    channels[index] = map.value(QLatin1String(keys[index])).toDouble(&ok);
    if (!ok || !std::isfinite(channels[index]))
      return fail(error, QStringLiteral("Style color channels must be finite numbers."));
  }
  *color = LayerStyleColor{channels[0], channels[1], channels[2], channels[3]};
  return true;
}

inline QVariantMap gradient_to_variant(const LayerStyleGradient& gradient) {
  QVariantList stops;
  for (const auto& stop : gradient.stops)
    stops.append(QVariantMap{{QStringLiteral("position"), stop.position},
        {QStringLiteral("color"), color_to_variant(stop.color)}});
  return {{QStringLiteral("stops"), stops},
      {QStringLiteral("angleDegrees"), gradient.angle_degrees},
      {QStringLiteral("scale"), gradient.scale},
      {QStringLiteral("reverse"), gradient.reverse}};
}

inline bool read_number(const QVariantMap& map, const QString& key, double* output,
                        QString* error) {
  bool ok = false;
  const auto number = map.value(key).toDouble(&ok);
  if (!ok || !std::isfinite(number))
    return fail(error, QStringLiteral("Style parameter '%1' must be a finite number.").arg(key));
  *output = number;
  return true;
}

inline bool read_bool(const QVariantMap& map, const QString& key, bool* output,
                      QString* error) {
  const auto value = map.value(key);
  if (!value.isValid() || value.metaType().id() != QMetaType::Bool)
    return fail(error, QStringLiteral("Style parameter '%1' must be true or false.").arg(key));
  *output = value.toBool();
  return true;
}

inline bool gradient_from_variant(const QVariant& value, LayerStyleGradient* gradient,
                                  QString* error) {
  const auto map = value.toMap();
  if (gradient == nullptr || map.isEmpty())
    return fail(error, QStringLiteral("A gradient must contain angleDegrees, scale, reverse and stops."));
  if (!read_number(map, QStringLiteral("angleDegrees"), &gradient->angle_degrees, error) ||
      !read_number(map, QStringLiteral("scale"), &gradient->scale, error) ||
      !read_bool(map, QStringLiteral("reverse"), &gradient->reverse, error)) return false;
  const auto stops = map.value(QStringLiteral("stops")).toList();
  gradient->stops.clear();
  gradient->stops.reserve(static_cast<std::size_t>(stops.size()));
  for (const auto& value_stop : stops) {
    const auto stop_map = value_stop.toMap();
    LayerStyleGradientStop stop;
    if (!read_number(stop_map, QStringLiteral("position"), &stop.position, error) ||
        !color_from_variant(stop_map.value(QStringLiteral("color")), &stop.color, error)) return false;
    gradient->stops.push_back(std::move(stop));
  }
  return true;
}

inline QVariantMap fill_to_variant(const LayerStyleFill& fill) {
  if (const auto* color = std::get_if<LayerStyleColor>(&fill))
    return {{QStringLiteral("kind"), QStringLiteral("solid")},
        {QStringLiteral("color"), color_to_variant(*color)}};
  return {{QStringLiteral("kind"), QStringLiteral("gradient")},
      {QStringLiteral("gradient"), gradient_to_variant(std::get<LayerStyleGradient>(fill))}};
}

inline bool fill_from_variant(const QVariant& value, LayerStyleFill* fill,
                              QString* error) {
  const auto map = value.toMap();
  const auto kind = map.value(QStringLiteral("kind")).toString();
  if (kind == QStringLiteral("solid")) {
    LayerStyleColor color;
    if (!color_from_variant(map.value(QStringLiteral("color")), &color, error)) return false;
    *fill = color;
    return true;
  }
  if (kind == QStringLiteral("gradient")) {
    LayerStyleGradient gradient;
    if (!gradient_from_variant(map.value(QStringLiteral("gradient")), &gradient, error)) return false;
    *fill = std::move(gradient);
    return true;
  }
  return fail(error, QStringLiteral("A stroke fill kind must be 'solid' or 'gradient'."));
}

inline QVariantMap operation_to_variant(const LayerStyleOperation& operation) {
  const bool supported = photo_editor::pipeline::is_known_layer_style(
      operation.type_id, operation.algorithm_version);
  QVariantMap parameters;
  if (const auto* value = std::get_if<LayerStyleStroke>(&operation.parameters)) {
    parameters = {{QStringLiteral("width"), value->width},
        {QStringLiteral("alignment"), alignment_name(value->alignment)},
        {QStringLiteral("fill"), fill_to_variant(value->fill)}};
  } else if (const auto* value = std::get_if<LayerStyleDropShadow>(&operation.parameters)) {
    parameters = {{QStringLiteral("color"), color_to_variant(value->color)},
        {QStringLiteral("distance"), value->distance},
        {QStringLiteral("angleDegrees"), value->angle_degrees},
        {QStringLiteral("blur"), value->blur}, {QStringLiteral("spread"), value->spread},
        {QStringLiteral("useGlobalLight"), value->use_global_light}};
  } else if (const auto* value = std::get_if<LayerStyleInnerShadow>(&operation.parameters)) {
    parameters = {{QStringLiteral("color"), color_to_variant(value->color)},
        {QStringLiteral("distance"), value->distance},
        {QStringLiteral("angleDegrees"), value->angle_degrees},
        {QStringLiteral("blur"), value->blur}, {QStringLiteral("spread"), value->spread},
        {QStringLiteral("useGlobalLight"), value->use_global_light}};
  } else if (const auto* value = std::get_if<LayerStyleInnerGlow>(&operation.parameters)) {
    parameters = {{QStringLiteral("color"), color_to_variant(value->color)},
        {QStringLiteral("blur"), value->blur}, {QStringLiteral("spread"), value->spread}};
  } else if (const auto* value = std::get_if<LayerStyleOuterGlow>(&operation.parameters)) {
    parameters = {{QStringLiteral("color"), color_to_variant(value->color)},
        {QStringLiteral("blur"), value->blur}, {QStringLiteral("spread"), value->spread}};
  } else if (const auto* value = std::get_if<LayerStyleColorOverlay>(&operation.parameters)) {
    parameters = {{QStringLiteral("color"), color_to_variant(value->color)}};
  } else if (const auto* value = std::get_if<LayerStyleGradientOverlay>(&operation.parameters)) {
    parameters = {{QStringLiteral("gradient"), gradient_to_variant(value->gradient)}};
  } else if (const auto* value = std::get_if<LayerStyleBevelEmboss>(&operation.parameters)) {
    parameters = {{QStringLiteral("size"), value->size},
        {QStringLiteral("softness"), value->softness}, {QStringLiteral("depth"), value->depth},
        {QStringLiteral("angleDegrees"), value->angle_degrees},
        {QStringLiteral("useGlobalLight"), value->use_global_light},
        {QStringLiteral("mode"), bevel_mode_name(value->mode)},
        {QStringLiteral("highlight"), color_to_variant(value->highlight)},
        {QStringLiteral("shadow"), color_to_variant(value->shadow)}};
  } else if (!operation.opaque_parameters.empty()) {
    parameters.insert(QStringLiteral("opaque"),
        QString::fromUtf8(operation.opaque_parameters.data(),
            static_cast<qsizetype>(operation.opaque_parameters.size())));
  }
  return {{QStringLiteral("id"), QString::fromUtf8(operation.id.data(),
              static_cast<qsizetype>(operation.id.size()))},
      {QStringLiteral("type"), QString::fromUtf8(operation.type_id.data(),
              static_cast<qsizetype>(operation.type_id.size()))},
      {QStringLiteral("typeId"), QString::fromUtf8(operation.type_id.data(),
              static_cast<qsizetype>(operation.type_id.size()))},
      {QStringLiteral("name"), type_name(operation.type_id)},
      {QStringLiteral("supported"), supported},
      {QStringLiteral("algorithmVersion"), operation.algorithm_version},
      {QStringLiteral("enabled"), operation.enabled},
      {QStringLiteral("opacity"), operation.opacity},
      {QStringLiteral("plane"), plane_name(operation.plane)},
      {QStringLiteral("blendMode"), blend_name(operation.blend_mode)},
      {QStringLiteral("maskRef"), QString::fromUtf8(operation.mask_ref.data(),
              static_cast<qsizetype>(operation.mask_ref.size()))},
      {QStringLiteral("parameters"), parameters}};
}

inline bool operation_from_variant(const QVariantMap& map, LayerStyleOperation* operation,
                                   QString* error) {
  if (operation == nullptr) return fail(error, QStringLiteral("Missing style operation output."));
  const auto type = map.value(QStringLiteral("typeId"), map.value(QStringLiteral("type"))).toString();
  const auto parameters = map.value(QStringLiteral("parameters")).toMap();
  operation->id = map.value(QStringLiteral("id")).toString().toStdString();
  operation->type_id = type.toStdString();
  const auto version = map.value(QStringLiteral("algorithmVersion"), 1).toUInt();
  operation->algorithm_version = version;
  operation->enabled = map.value(QStringLiteral("enabled"), true).toBool();
  if (!read_bool(map, QStringLiteral("enabled"), &operation->enabled, error)) return false;
  if (!read_number(map, QStringLiteral("opacity"), &operation->opacity, error)) return false;
  if (!parse_plane(map.value(QStringLiteral("plane")).toString(), &operation->plane))
    return fail(error, QStringLiteral("Unknown style plane."));
  if (!parse_blend(map.value(QStringLiteral("blendMode")).toString(), &operation->blend_mode))
    return fail(error, QStringLiteral("Unknown style blend mode."));
  operation->mask_ref = map.value(QStringLiteral("maskRef")).toString().toStdString();
  if (!photo_editor::pipeline::is_known_layer_style(operation->type_id,
          operation->algorithm_version)) {
    operation->parameters = std::monostate{};
    operation->opaque_parameters = parameters.value(QStringLiteral("opaque")).toString().toStdString();
    return true;
  }
  operation->opaque_parameters.clear();
  if (operation->type_id == "stroke") {
    LayerStyleStroke value;
    if (!read_number(parameters, QStringLiteral("width"), &value.width, error) ||
        !parse_alignment(parameters.value(QStringLiteral("alignment")).toString(), &value.alignment) ||
        !fill_from_variant(parameters.value(QStringLiteral("fill")), &value.fill, error))
      return error != nullptr && !error->isEmpty() ? false :
          fail(error, QStringLiteral("Unknown stroke alignment."));
    operation->parameters = std::move(value);
  } else if (operation->type_id == "drop_shadow") {
    LayerStyleDropShadow value;
    if (!color_from_variant(parameters.value(QStringLiteral("color")), &value.color, error) ||
        !read_number(parameters, QStringLiteral("distance"), &value.distance, error) ||
        !read_number(parameters, QStringLiteral("angleDegrees"), &value.angle_degrees, error) ||
        !read_number(parameters, QStringLiteral("blur"), &value.blur, error) ||
        !read_number(parameters, QStringLiteral("spread"), &value.spread, error) ||
        !read_bool(parameters, QStringLiteral("useGlobalLight"), &value.use_global_light, error)) return false;
    operation->parameters = value;
  } else if (operation->type_id == "inner_shadow") {
    LayerStyleInnerShadow value;
    if (!color_from_variant(parameters.value(QStringLiteral("color")), &value.color, error) ||
        !read_number(parameters, QStringLiteral("distance"), &value.distance, error) ||
        !read_number(parameters, QStringLiteral("angleDegrees"), &value.angle_degrees, error) ||
        !read_number(parameters, QStringLiteral("blur"), &value.blur, error) ||
        !read_number(parameters, QStringLiteral("spread"), &value.spread, error) ||
        !read_bool(parameters, QStringLiteral("useGlobalLight"), &value.use_global_light, error)) return false;
    operation->parameters = value;
  } else if (operation->type_id == "inner_glow") {
    LayerStyleInnerGlow value;
    if (!color_from_variant(parameters.value(QStringLiteral("color")), &value.color, error) ||
        !read_number(parameters, QStringLiteral("blur"), &value.blur, error) ||
        !read_number(parameters, QStringLiteral("spread"), &value.spread, error)) return false;
    operation->parameters = value;
  } else if (operation->type_id == "outer_glow") {
    LayerStyleOuterGlow value;
    if (!color_from_variant(parameters.value(QStringLiteral("color")), &value.color, error) ||
        !read_number(parameters, QStringLiteral("blur"), &value.blur, error) ||
        !read_number(parameters, QStringLiteral("spread"), &value.spread, error)) return false;
    operation->parameters = value;
  } else if (operation->type_id == "color_overlay") {
    LayerStyleColorOverlay value;
    if (!color_from_variant(parameters.value(QStringLiteral("color")), &value.color, error)) return false;
    operation->parameters = value;
  } else if (operation->type_id == "gradient_overlay") {
    LayerStyleGradientOverlay value;
    if (!gradient_from_variant(parameters.value(QStringLiteral("gradient")), &value.gradient, error)) return false;
    operation->parameters = std::move(value);
  } else if (operation->type_id == "bevel_emboss") {
    LayerStyleBevelEmboss value;
    if (!read_number(parameters, QStringLiteral("size"), &value.size, error) ||
        !read_number(parameters, QStringLiteral("softness"), &value.softness, error) ||
        !read_number(parameters, QStringLiteral("depth"), &value.depth, error) ||
        !read_number(parameters, QStringLiteral("angleDegrees"), &value.angle_degrees, error) ||
        !read_bool(parameters, QStringLiteral("useGlobalLight"), &value.use_global_light, error) ||
        !parse_bevel_mode(parameters.value(QStringLiteral("mode")).toString(), &value.mode) ||
        !color_from_variant(parameters.value(QStringLiteral("highlight")), &value.highlight, error) ||
        !color_from_variant(parameters.value(QStringLiteral("shadow")), &value.shadow, error))
      return error != nullptr && !error->isEmpty() ? false :
          fail(error, QStringLiteral("Unknown bevel mode."));
    operation->parameters = value;
  }
  return true;
}

inline bool validate_stack(const LayerStyleStack& stack, QString* error) {
  std::string detail;
  if (!photo_editor::pipeline::validate_layer_style_stack(stack, &detail))
    return fail(error, QString::fromStdString(detail));
  return true;
}

inline bool decode_stack(const QString& serialized, LayerStyleStack* stack,
                         QString* error) {
  if (stack == nullptr) return fail(error, QStringLiteral("Missing style stack output."));
  if (serialized.isEmpty()) {
    *stack = LayerStyleStack{};
    return true;
  }
  std::string detail;
  const auto decoded = photo_editor::pipeline::deserialize_layer_style_stack(
      serialized.toStdString(), &detail);
  if (!decoded) return fail(error, QString::fromStdString(detail));
  *stack = *decoded;
  return true;
}

inline bool encode_stack(const LayerStyleStack& stack, QString* serialized,
                         QString* error) {
  if (serialized == nullptr) return fail(error, QStringLiteral("Missing serialized stack output."));
  std::string detail;
  const auto encoded = photo_editor::pipeline::serialize_layer_style_stack(stack, &detail);
  if (!encoded) return fail(error, QString::fromStdString(detail));
  *serialized = QString::fromStdString(*encoded);
  return true;
}

inline QVariantList stack_to_variant(const LayerStyleStack& stack) {
  QVariantList result;
  for (const auto& operation : stack.operations) result.append(operation_to_variant(operation));
  return result;
}

inline QVariantList available_types() {
  QVariantList result;
  for (const auto& [id, name] : {
           std::pair{"stroke", "Stroke"}, {"drop_shadow", "Drop Shadow"},
           {"inner_shadow", "Inner Shadow"}, {"inner_glow", "Inner Glow"},
           {"outer_glow", "Outer Glow"}, {"color_overlay", "Color Overlay"},
           {"gradient_overlay", "Gradient Overlay"}, {"bevel_emboss", "Bevel and Emboss"}}) {
    result.append(QVariantMap{{QStringLiteral("type"), QString::fromLatin1(id)},
        {QStringLiteral("typeId"), QString::fromLatin1(id)},
        {QStringLiteral("name"), QString::fromLatin1(name)}});
  }
  return result;
}

inline bool create_default_operation(const QString& type, const QString& id,
                                     LayerStyleOperation* operation, QString* error) {
  if (operation == nullptr || id.isEmpty())
    return fail(error, QStringLiteral("A style operation requires an ID."));
  operation->id = id.toStdString();
  operation->type_id = type.toStdString();
  operation->algorithm_version = photo_editor::pipeline::kLayerStyleAlgorithmVersion;
  operation->enabled = true;
  operation->opacity = 1.0;
  operation->blend_mode = LayerStyleBlendMode::Normal;
  if (type == QStringLiteral("stroke")) {
    operation->plane = LayerStylePlane::BelowContent;
    operation->parameters = LayerStyleStroke{};
  } else if (type == QStringLiteral("drop_shadow")) {
    operation->plane = LayerStylePlane::BelowContent;
    operation->parameters = LayerStyleDropShadow{};
  } else if (type == QStringLiteral("inner_shadow")) {
    operation->plane = LayerStylePlane::OnContent;
    operation->parameters = LayerStyleInnerShadow{};
  } else if (type == QStringLiteral("inner_glow")) {
    operation->plane = LayerStylePlane::OnContent;
    operation->parameters = LayerStyleInnerGlow{};
  } else if (type == QStringLiteral("outer_glow")) {
    operation->plane = LayerStylePlane::BelowContent;
    operation->parameters = LayerStyleOuterGlow{};
  } else if (type == QStringLiteral("color_overlay")) {
    operation->plane = LayerStylePlane::OnContent;
    operation->parameters = LayerStyleColorOverlay{};
  } else if (type == QStringLiteral("gradient_overlay")) {
    operation->plane = LayerStylePlane::OnContent;
    auto gradient = LayerStyleGradient{};
    gradient.stops = {{0.0, LayerStyleColor{.red = 0.0, .green = 0.0, .blue = 0.0, .alpha = 1.0}},
        {1.0, LayerStyleColor{.red = 1.0, .green = 1.0, .blue = 1.0, .alpha = 1.0}}};
    operation->parameters = LayerStyleGradientOverlay{std::move(gradient)};
  } else if (type == QStringLiteral("bevel_emboss")) {
    operation->plane = LayerStylePlane::OnContent;
    operation->parameters = LayerStyleBevelEmboss{};
  } else {
    return fail(error, QStringLiteral("Unsupported layer style type."));
  }
  LayerStyleStack candidate;
  candidate.operations.push_back(*operation);
  return validate_stack(candidate, error);
}

inline QByteArray encode_envelope(const QString& stack_string, double global_light_angle,
                                  QString* error) {
  if (!std::isfinite(global_light_angle) || global_light_angle < -3600.0 ||
      global_light_angle > 3600.0) {
    fail(error, QStringLiteral("The shared light angle must be between -3600 and 3600 degrees."));
    return {};
  }
  LayerStyleStack stack;
  if (!decode_stack(stack_string, &stack, error)) return {};
  QJsonObject object{{QStringLiteral("schema"), QString::fromLatin1(kEnvelopeSchema)},
      {QStringLiteral("version"), 1},
      {QStringLiteral("colorSpace"), QStringLiteral("linear-srgb")},
      {QStringLiteral("layerStyleStack"), stack_string},
      {QStringLiteral("globalLightAngle"), global_light_angle}};
  const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
  if (bytes.isEmpty() || bytes.size() > kMaximumEnvelopeBytes) {
    fail(error, QStringLiteral("The layer style envelope is empty or exceeds 4 MiB."));
    return {};
  }
  return bytes;
}

inline bool decode_envelope(const QByteArray& bytes, QString* stack_string,
                            double* global_light_angle, QString* error) {
  if (bytes.isEmpty() || bytes.size() > kMaximumEnvelopeBytes)
    return fail(error, QStringLiteral("The layer style envelope is empty or exceeds 4 MiB."));
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(bytes, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    return fail(error, QStringLiteral("The layer style envelope is not valid JSON."));
  const auto object = document.object();
  if (object.value(QStringLiteral("schema")).toString() != QString::fromLatin1(kEnvelopeSchema) ||
      object.value(QStringLiteral("version")).toInt() != 1 ||
      object.value(QStringLiteral("colorSpace")).toString() != QStringLiteral("linear-srgb") ||
      !object.value(QStringLiteral("layerStyleStack")).isString() ||
      !object.value(QStringLiteral("globalLightAngle")).isDouble())
    return fail(error, QStringLiteral("The layer style envelope has an unsupported schema or color space."));
  const auto serialized = object.value(QStringLiteral("layerStyleStack")).toString();
  LayerStyleStack stack;
  if (!decode_stack(serialized, &stack, error)) return false;
  const auto angle = object.value(QStringLiteral("globalLightAngle")).toDouble();
  if (!std::isfinite(angle) || angle < -3600.0 || angle > 3600.0)
    return fail(error, QStringLiteral("The shared light angle must be between -3600 and 3600 degrees."));
  if (stack_string != nullptr) *stack_string = serialized;
  if (global_light_angle != nullptr) *global_light_angle = angle;
  return true;
}

}  // namespace photo_editor::desktop::layer_style_codec
