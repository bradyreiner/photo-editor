#pragma once

#include "edit_recipe_state.h"
#include "photo_layer_style_codec.h"

#include "photo_editor/pipeline/layer_styles.h"

#include <QSet>
#include <QSize>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

namespace photo_editor::desktop::photo_layer_style_light {

inline constexpr double kDefaultAngleDegrees = -45.0;
inline constexpr auto kExpandedDocumentAngleField =
    "_documentLayerStyleLightAngle";

struct Change final {
  bool applied = false;
  EditRecipeState state;
  QString error;
  int affected_layer_count = 0;
};

inline bool valid_angle(double angle_degrees) {
  return std::isfinite(angle_degrees) && angle_degrees >= -3600.0 &&
      angle_degrees <= 3600.0;
}

inline bool is_linked(const QVariantMap& layer) {
  const auto value = layer.value(QStringLiteral("layerStyleLightLinked"));
  return value.metaType().id() == QMetaType::Bool && value.toBool();
}

inline bool has_valid_link_field(const QVariantMap& layer) {
  const auto value = layer.value(QStringLiteral("layerStyleLightLinked"));
  return !value.isValid() || value.metaType().id() == QMetaType::Bool;
}

inline bool has_linked_lighting_in_scope(const QVariantList& layers) {
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    if (is_linked(layer)) return true;
    for (const auto* key : {"children", "layers"}) {
      if (has_linked_lighting_in_scope(
              layer.value(QString::fromLatin1(key)).toList())) return true;
    }
  }
  return false;
}

inline bool has_linked_lighting(const QVariantList& layers) {
  if (has_linked_lighting_in_scope(layers)) return true;
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    for (const auto* key : {"children", "layers"}) {
      if (has_linked_lighting(
              layer.value(QString::fromLatin1(key)).toList())) return true;
    }
    const auto nested = layer.value(QStringLiteral("smartObjectDocument")).toMap();
    if (has_linked_lighting(nested.value(QStringLiteral("layers")).toList()))
      return true;
  }
  return false;
}

inline bool has_nested_document_angle(const QVariantList& layers) {
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    for (const auto* key : {"children", "layers"}) {
      if (has_nested_document_angle(
              layer.value(QString::fromLatin1(key)).toList())) return true;
    }
    const auto nested = layer.value(QStringLiteral("smartObjectDocument")).toMap();
    if (!nested.isEmpty()) {
      bool angle_ok = false;
      const auto angle = nested.value(QStringLiteral("documentLayerStyleLightAngle"),
                                      kDefaultAngleDegrees).toDouble(&angle_ok);
      if (!angle_ok || std::abs(angle - kDefaultAngleDegrees) >= 1e-9)
        return true;
      if (has_linked_lighting(nested.value(QStringLiteral("layers")).toList()) ||
          has_nested_document_angle(nested.value(QStringLiteral("layers")).toList()))
        return true;
    }
  }
  return false;
}

inline bool requires_document_light_capability(const EditRecipeState& state) {
  return !valid_angle(state.document_layer_style_light_angle) ||
      std::abs(state.document_layer_style_light_angle -
                  kDefaultAngleDegrees) >= 1e-9 ||
      has_linked_lighting(state.layers) || has_nested_document_angle(state.layers);
}

inline bool layer_or_ancestor_locked(const QVariantList& layers, int index) {
  QSet<QString> visited;
  for (int depth = 0; depth < 64 && index >= 0 && index < layers.size(); ++depth) {
    const auto layer = layers.at(index).toMap();
    const auto id = layer.value(QStringLiteral("id")).toString();
    if (layer.value(QStringLiteral("locked")).toBool() || visited.contains(id))
      return true;
    visited.insert(id);
    const auto parent_id = layer.value(QStringLiteral("parentId")).toString();
    if (parent_id.isEmpty()) return false;
    index = -1;
    for (int candidate = 0; candidate < layers.size(); ++candidate) {
      if (layers.at(candidate).toMap().value(QStringLiteral("id")).toString() ==
          parent_id) {
        index = candidate;
        break;
      }
    }
  }
  return true;
}

inline double local_angle(const QVariantMap& layer) {
  bool angle_ok = false;
  const auto angle = layer.value(QStringLiteral("layerStyleGlobalLightAngle"),
                                 kDefaultAngleDegrees).toDouble(&angle_ok);
  return angle_ok && valid_angle(angle) ? angle : kDefaultAngleDegrees;
}

inline double effective_angle(const EditRecipeState& state, int layer_index) {
  if (layer_index < 0 || layer_index >= state.layers.size())
    return kDefaultAngleDegrees;
  const auto layer = state.layers.at(layer_index).toMap();
  if (is_linked(layer) && valid_angle(state.document_layer_style_light_angle))
    return state.document_layer_style_light_angle;
  return local_angle(layer);
}

inline bool decode_stack(const QVariantMap& layer,
                         photo_editor::pipeline::LayerStyleStack* stack,
                         QString* error) {
  if (stack == nullptr ||
      layer.value(QStringLiteral("layerStyleStack")).metaType().id() !=
          QMetaType::QString) {
    if (error != nullptr)
      *error = QStringLiteral("Linked lighting requires a valid layer style stack.");
    return false;
  }
  QString detail;
  if (!layer_style_codec::decode_stack(
          layer.value(QStringLiteral("layerStyleStack")).toString(), stack,
          &detail)) {
    if (error != nullptr)
      *error = QStringLiteral("Layer styles could not be read: %1").arg(detail);
    return false;
  }
  return true;
}

inline bool preflight(const QVariantMap& layer,
                      const photo_editor::pipeline::LayerStyleStack& stack,
                      double angle_degrees, const QSize& document_size,
                      QString* error) {
  if (!valid_angle(angle_degrees)) {
    if (error != nullptr)
      *error = QStringLiteral("Document layer style light angle is outside the supported range.");
    return false;
  }
  const auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
  QSize raster_size = document_size;
  if (kind != QStringLiteral("fill") && kind != QStringLiteral("group")) {
    bool width_ok = false;
    bool height_ok = false;
    const auto width = layer.value(QStringLiteral("pixelWidth")).toInt(&width_ok);
    const auto height = layer.value(QStringLiteral("pixelHeight")).toInt(&height_ok);
    if (width_ok && height_ok && width > 0 && height > 0)
      raster_size = QSize(width, height);
  }
  if (!raster_size.isValid() || raster_size.width() <= 0 ||
      raster_size.height() <= 0) {
    if (error != nullptr)
      *error = QStringLiteral("Layer source dimensions are unavailable for safe style rendering.");
    return false;
  }
  photo_editor::pipeline::LayerStyleRenderOptions options;
  options.global_light_angle_degrees = angle_degrees;
  const auto result = photo_editor::pipeline::preflight_layer_style_stack(
      raster_size.width(), raster_size.height(), stack, options);
  if (!result.ok()) {
    if (error != nullptr) *error = QString::fromStdString(result.error);
    return false;
  }
  return true;
}

inline Change with_document_angle(const EditRecipeState& state,
                                 double angle_degrees,
                                 const QSize& document_size) {
  Change result;
  result.state = state;
  if (!valid_angle(angle_degrees)) {
    result.error = QStringLiteral("Document layer style light angle must be finite and within 3600 degrees.");
    return result;
  }
  if (!valid_angle(state.document_layer_style_light_angle)) {
    result.error = QStringLiteral("Stored document layer style light angle is invalid.");
    return result;
  }
  if (std::abs(state.document_layer_style_light_angle - angle_degrees) < 1e-9)
    return result;
  for (int index = 0; index < state.layers.size(); ++index) {
    const auto layer = state.layers.at(index).toMap();
    if (!has_valid_link_field(layer)) {
      result.error = QStringLiteral("Layer style link state must be a boolean.");
      return result;
    }
    if (!is_linked(layer)) continue;
    if (layer_or_ancestor_locked(state.layers, index)) {
      result.error = QStringLiteral("Unlock each linked layer and its parent groups before changing document lighting.");
      return result;
    }
    photo_editor::pipeline::LayerStyleStack stack;
    if (!decode_stack(layer, &stack, &result.error) ||
        !preflight(layer, stack, angle_degrees, document_size, &result.error))
      return result;
    ++result.affected_layer_count;
  }
  result.state.document_layer_style_light_angle = angle_degrees;
  result.applied = true;
  return result;
}

inline Change with_layer_linked(const EditRecipeState& state, int layer_index,
                                bool linked, const QSize& document_size) {
  Change result;
  result.state = state;
  if (layer_index < 0 || layer_index >= state.layers.size()) {
    result.error = QStringLiteral("Layer style target is unavailable.");
    return result;
  }
  auto layer = state.layers.at(layer_index).toMap();
  if (!has_valid_link_field(layer)) {
    result.error = QStringLiteral("Layer style link state must be a boolean.");
    return result;
  }
  const auto was_linked = is_linked(layer);
  if (was_linked == linked) return result;
  if (layer_or_ancestor_locked(state.layers, layer_index)) {
    result.error = QStringLiteral("Unlock the target layer and its parent groups before changing its lighting link.");
    return result;
  }
  photo_editor::pipeline::LayerStyleStack stack;
  if (!decode_stack(layer, &stack, &result.error)) return result;
  const auto appearance_angle = was_linked
      ? state.document_layer_style_light_angle : local_angle(layer);
  if (!valid_angle(appearance_angle) ||
      !preflight(layer, stack, linked ? state.document_layer_style_light_angle
                                     : appearance_angle,
                 document_size, &result.error))
    return result;
  if (linked) {
    layer.insert(QStringLiteral("layerStyleLightLinked"), true);
  } else {
    layer.insert(QStringLiteral("layerStyleGlobalLightAngle"), appearance_angle);
    layer.remove(QStringLiteral("layerStyleLightLinked"));
  }
  result.state.layers[layer_index] = layer;
  result.applied = true;
  result.affected_layer_count = 1;
  return result;
}

}  // namespace photo_editor::desktop::photo_layer_style_light
