#include "editor_preview_controller.h"

#include "photo_layer_edit_access.h"
#include "photo_layer_style_light.h"
#include "photo_paint_surface.h"
#include "photo_render_utils.h"
#include "photo_text_dimension.h"
#include "photo_text_dimension_render.h"
#include "photo_vector_mask_coordinates.h"

#include <QCryptographicHash>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace photo_editor::desktop {
namespace {
QVariantMap default_dimension() { return encode_photo_text_dimension_variant_map(PhotoTextDimensionSettings{}); }

QString dimension_revision(const QVariantMap& layer) {
  return QString::fromLatin1(QCryptographicHash::hash(
      QJsonDocument(QJsonObject::fromVariantMap(photo_paint_target_identity(layer))).toJson(QJsonDocument::Compact),
      QCryptographicHash::Sha256).toHex());
}

QSize execution_frame(const QVariantMap& layer, QSize crop) {
  const auto cw = layer.value("canvasWidth", crop.width()).toDouble();
  const auto ch = layer.value("canvasHeight", crop.height()).toDouble();
  const auto width = layer.value("pixelWidth", 512).toDouble() * crop.width() / cw;
  const auto height = layer.value("pixelHeight", 128).toDouble() * crop.height() / ch;
  if (cw <= 0 || ch <= 0 || !std::isfinite(width) || !std::isfinite(height) ||
      width <= 0 || height <= 0 || width > 16384 || height > 16384) return {};
  return {std::max(1, qRound(width)), std::max(1, qRound(height))};
}

bool merged_dimension(const QVariantMap& original, const QVariantMap& patch,
                      QVariantMap* output, QString* error) {
  auto next = original;
  for (auto it = patch.cbegin(); it != patch.cend(); ++it) next.insert(it.key(), it.value());
  const auto decoded = decode_photo_text_dimension_variant_map(next, error);
  if (!decoded) return false;
  *output = encode_photo_text_dimension_variant_map(*decoded);
  return true;
}
}  // namespace

bool EditorPreviewController::validatePhotoTextDimensionLayerChange(const QVariantList& layers, QString* error) const {
  const bool has_text_dimensions = photo_text_dimensions_present_in_layers(layers);
  bool has_text_dimension_beyond_helper_depth = false;
  if (!has_text_dimensions) {
    std::vector<std::pair<QVariantList, int>> pending{{layers, 0}};
    qsizetype inspected_layers = 0;
    while (!pending.empty() && !has_text_dimension_beyond_helper_depth) {
      auto [scope_layers, depth] = std::move(pending.back());
      pending.pop_back();
      for (const auto& value : scope_layers) {
        if (++inspected_layers > 32768) {
          if (error != nullptr)
            *error = QStringLiteral("Text dimension layer inspection exceeds the supported limit.");
          return false;
        }
        if (value.metaType().id() != QMetaType::QVariantMap) continue;
        const auto layer = value.toMap();
        if (depth > 72 && layer.contains(QStringLiteral("textDimension"))) {
          has_text_dimension_beyond_helper_depth = true;
          break;
        }
        for (const auto& key : {QStringLiteral("children"), QStringLiteral("layers")}) {
          const auto children = layer.value(key);
          if (children.metaType().id() == QMetaType::QVariantList)
            pending.emplace_back(children.toList(), depth + 1);
        }
        const auto nested_document = layer.value(QStringLiteral("smartObjectDocument")).toMap();
        const auto nested_layers = nested_document.value(QStringLiteral("layers"));
        if (nested_layers.metaType().id() == QMetaType::QVariantList)
          pending.emplace_back(nested_layers.toList(), depth + 1);
      }
    }
  }
  if (!has_text_dimensions && !has_text_dimension_beyond_helper_depth) return true;
  if (!validate_photo_text_dimensions_in_layers(layers, error)) return false;
  const auto signature = [](const QVariantMap& layer) {
    QVariantMap result;
    for (const auto* key : {"text", "textTypography", "fontFamily", "fontSize", "fontWeight", "textColor",
        "textAlignment", "textDimension", "pixelWidth", "pixelHeight", "textOriginX", "textOriginY",
        "canvasWidth", "canvasHeight", "textPerspectiveHorizontal", "textPerspectiveVertical",
        "layerStyleStack", "layerStyleLightLinked", "layerStyleGlobalLightAngle", "effectStack"})
      if (layer.contains(QLatin1String(key))) result.insert(QLatin1String(key), layer.value(QLatin1String(key)));
    return result;
  };
  const auto state = currentState();
  const auto crop = crop_editing_ ? QRect(QPoint(), sourceImageSize()) : crop_rect_for_image(sourceImageSize(), state);
  const auto find_prior = [](const QVariantList& prior_layers,
                             const QVariantMap& layer) -> QVariantMap {
    const auto id = layer.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) return {};
    const auto prior = std::find_if(prior_layers.cbegin(), prior_layers.cend(),
        [&](const QVariant& candidate) {
          return candidate.metaType().id() == QMetaType::QVariantMap &&
              candidate.toMap().value(QStringLiteral("id")).toString() == id;
        });
    return prior == prior_layers.cend() ? QVariantMap{} : prior->toMap();
  };
  const auto fail = [&](const QString& fallback) {
    if (error != nullptr && error->isEmpty()) *error = fallback;
    return false;
  };
  const auto validate_scope = [&](auto&& self, const QVariantList& current_layers,
                                  const QVariantList& prior_layers,
                                  QSize canvas, QSize prior_canvas,
                                  double document_light_angle,
                                  int depth) -> bool {
    if (depth > 72) {
      if (error != nullptr)
        *error = QStringLiteral("Text dimension layer nesting exceeds the supported limit.");
      return false;
    }
    for (const auto& value : current_layers) {
      if (value.metaType().id() != QMetaType::QVariantMap) continue;
      auto layer = value.toMap();
      const auto prior_layer = find_prior(prior_layers, layer);
      const bool matched_prior = !prior_layer.isEmpty();
      if (layer.value(QStringLiteral("kind")).toString() == QStringLiteral("text") &&
          photo_text_dimension_is_active(layer)) {
        const bool unchanged = matched_prior && canvas == prior_canvas &&
            signature(prior_layer) == signature(layer);
        if (!unchanged) {
          if (layer.value(QStringLiteral("layerStyleLightLinked")).toBool())
            layer.insert(QStringLiteral("_documentLayerStyleLightAngle"),
                         document_light_angle);
          const auto frame = execution_frame(layer, canvas);
          if (!frame.isValid() ||
              !preflight_photo_text_dimension(layer, frame, error).isValid())
            return fail(QStringLiteral(
                "This text edit exceeds the supported dimensional text frame."));
        }
      }

      for (const auto& key : {QStringLiteral("children"), QStringLiteral("layers")}) {
        if (!layer.contains(key)) continue;
        const auto next_layers = layer.value(key).toList();
        const auto prior_children = matched_prior
            ? prior_layer.value(key).toList() : QVariantList{};
        if (!self(self, next_layers, prior_children, canvas, prior_canvas,
                  document_light_angle, depth + 1))
          return false;
      }

      const auto nested_document = layer.value(QStringLiteral("smartObjectDocument")).toMap();
      if (nested_document.isEmpty()) continue;
      const auto nested_layers = nested_document.value(QStringLiteral("layers")).toList();
      int nested_width = 0;
      int nested_height = 0;
      bool width_ok = false;
      bool height_ok = false;
      nested_width = nested_document.value(QStringLiteral("canvasWidth")).toInt(&width_ok);
      nested_height = nested_document.value(QStringLiteral("canvasHeight")).toInt(&height_ok);
      if (!width_ok || !height_ok || nested_width <= 0 || nested_height <= 0) {
        if (photo_text_dimensions_present_in_layers(nested_layers)) {
          if (error != nullptr)
            *error = QStringLiteral("Dimensional text requires a valid Smart Object canvas size.");
          return false;
        }
        continue;
      }
      const QSize nested_canvas(nested_width, nested_height);
      const auto prior_document = matched_prior
          ? prior_layer.value(QStringLiteral("smartObjectDocument")).toMap()
          : QVariantMap{};
      const bool matching_document = !prior_document.isEmpty() &&
          prior_document.value(QStringLiteral("contentId")).toString() ==
              nested_document.value(QStringLiteral("contentId")).toString();
      QSize prior_nested_canvas;
      if (matching_document) {
        bool prior_width_ok = false;
        bool prior_height_ok = false;
        const auto prior_width = prior_document.value(QStringLiteral("canvasWidth")).toInt(&prior_width_ok);
        const auto prior_height = prior_document.value(QStringLiteral("canvasHeight")).toInt(&prior_height_ok);
        if (prior_width_ok && prior_height_ok && prior_width > 0 && prior_height > 0)
          prior_nested_canvas = QSize(prior_width, prior_height);
      }
      double nested_light_angle = photo_layer_style_light::kDefaultAngleDegrees;
      if (nested_document.contains(QStringLiteral("documentLayerStyleLightAngle"))) {
        bool angle_ok = false;
        nested_light_angle = nested_document.value(
            QStringLiteral("documentLayerStyleLightAngle")).toDouble(&angle_ok);
        if (!angle_ok || !photo_layer_style_light::valid_angle(nested_light_angle)) {
          if (error != nullptr)
            *error = QStringLiteral("Smart Object document light angle is invalid.");
          return false;
        }
      } else if (photo_layer_style_light::has_linked_lighting_in_scope(nested_layers)) {
        if (error != nullptr)
          *error = QStringLiteral("Linked Smart Object lighting requires a document light angle.");
        return false;
      }
      if (!self(self, nested_layers,
                matching_document ? prior_document.value(QStringLiteral("layers")).toList()
                                  : QVariantList{},
                nested_canvas, prior_nested_canvas, nested_light_angle, depth + 1))
        return false;
    }
    return true;
  };
  return validate_scope(validate_scope, layers, state.layers, crop.size(), crop.size(),
                        state.document_layer_style_light_angle, 0);
}

QVariantMap EditorPreviewController::photoLayerTextDimension(int index) const {
  if (index < 0 || index >= layer_session_.layers().size()) return {};
  const auto layer = layer_session_.layers()[index].toMap();
  if (layer.value("kind").toString() != "text") return {};
  if (!layer.contains("textDimension")) return default_dimension();
  const auto decoded = decode_photo_text_dimension_variant_map(layer.value("textDimension").toMap());
  return decoded ? encode_photo_text_dimension_variant_map(*decoded) : QVariantMap{};
}

bool EditorPreviewController::setPhotoLayerTextDimension(int index, const QVariantMap& changes) {
  const auto access = customer_edit_access_.enter();
  const auto layers = layer_session_.layers();
  if (!access || changes.isEmpty() || index < 0 || index >= layers.size() ||
      layer_edit_access::is_effectively_locked(layers, index) ||
      layers[index].toMap().value("kind").toString() != "text" || photoPaintBusy() ||
      professional_content_work_running_ || history_preview_state_ || preset_preview_state_ ||
      !photo_text_editing_layer_id_.isEmpty()) return false;
  const auto current = photoLayerTextDimension(index);
  QVariantMap next;
  QString error;
  auto patch = changes;
  const auto current_settings = decode_photo_text_dimension_variant_map(current);
  if (current_settings && changes.value("cornerPinEnabled").toBool() &&
      !current_settings->corner_pin_enabled && !changes.contains("cornerPins") &&
      current_settings->corner_pins == PhotoTextDimensionSettings{}.corner_pins) {
    const auto original = layers[index].toMap();
    const QSizeF box(original.value("pixelWidth", 512).toDouble(), original.value("pixelHeight", 128).toDouble());
    const auto transform = photo_text_dimension_plane_transform(original, *current_settings, box);
    QVariantList pins;
    for (const auto& point : PhotoTextDimensionSettings{}.corner_pins) {
      const auto mapped = transform.map(QPointF(point.x() * box.width(), point.y() * box.height()));
      pins.push_back(QVariantMap{{"x", mapped.x() / box.width()}, {"y", mapped.y() / box.height()}});
    }
    patch.insert("cornerPins", pins);
  }
  if (!merged_dimension(current, patch, &next, &error)) { setStatus(error); return false; }
  if (next == current) return false;
  auto layer = layers[index].toMap(); layer.insert("textDimension", next);
  auto admission_layer = layer;
  admission_layer.insert("layerStyleGlobalLightAngle", photoLayerStyleLightAngle(index));
  const auto state = currentState();
  const auto crop = crop_editing_ ? QRect(QPoint(), sourceImageSize()) : crop_rect_for_image(sourceImageSize(), state);
  const auto frame = execution_frame(layer, crop.size());
  if (!frame.isValid() || !preflight_photo_text_dimension(admission_layer, frame, &error).isValid()) {
    setStatus(error.isEmpty() ? QStringLiteral("The text frame cannot be projected safely.") : error);
    return false;
  }
  auto updated = layers; updated[index] = layer;
  return applyPhotoLayerStackChange(updated, QStringLiteral("photo_text_dimension"), false,
      QStringLiteral("Change dimensional text"));
}

QString EditorPreviewController::photoTextDimensionTargetRevision(int index) const {
  const auto layers = layer_session_.layers();
  if (index < 0 || index >= layers.size() || layers[index].toMap().value("kind").toString() != "text") return {};
  return dimension_revision(layers[index].toMap());
}

QVariantMap EditorPreviewController::photoTextDimensionCanvasGeometry(int index) const {
  const auto state = currentState();
  if (index < 0 || index >= state.layers.size()) return {{"valid", false}};
  const auto original = state.layers[index].toMap();
  if (original.value("kind").toString() != "text") return {{"valid", false}};
  auto layer = original;
  if (photoTextDimensionGestureMatches(index, original.value("id").toString()))
    layer.insert("textDimension", photo_text_dimension_gesture_->draft_dimension);
  auto dimension = layer.value("textDimension", default_dimension()).toMap();
  QString error;
  const auto settings = decode_photo_text_dimension_variant_map(dimension, &error);
  if (!settings) return {{"valid", false}, {"reason", error}};
  const auto source = sourceImageSize();
  const auto crop = crop_editing_ ? QRect(QPoint(), source) : crop_rect_for_image(source, state);
  const DesktopSourceGeometry geometry{source, crop};
  const auto placement = vector_mask_placement(layer, state.layers, geometry);
  const auto frame = execution_frame(layer, crop.size());
  if (!placement.valid || !frame.isValid()) return {{"valid", false}};
  const QSizeF box(layer.value("pixelWidth", 512).toDouble(), layer.value("pixelHeight", 128).toDouble());
  const auto bounds = preflight_photo_text_dimension(layer, frame, &error);
  if (!bounds.isValid()) return {{"valid", false}, {"reason", error}};
  const auto map = [&](QPointF point) {
    const auto pixel = placement.local_to_source.map(QPointF(point.x() / box.width(), point.y() / box.height()));
    return mapCanvasCoordinate((pixel.x() - 0.5) / std::max(1, source.width() - 1),
        (pixel.y() - 0.5) / std::max(1, source.height() - 1), true, QStringLiteral("authoring-source"));
  };
  QVariantList outline;
  for (const auto& point : {bounds.topLeft(), bounds.topRight(), bounds.bottomRight(), bounds.bottomLeft()}) outline.push_back(map(point));
  QVariantMap corners;
  const std::array<QString, 4> names{"topLeft", "topRight", "bottomRight", "bottomLeft"};
  const auto plane = photo_text_dimension_plane_transform(layer, *settings, box);
  const std::array<QPointF, 4> box_corners{QPointF{}, QPointF(box.width(), 0), QPointF(box.width(), box.height()), QPointF(0, box.height())};
  QVariantList displayed_pins;
  for (std::size_t i = 0; i < names.size(); ++i) {
    const auto point = plane.map(box_corners[i]);
    corners.insert(names[i], map(point));
    displayed_pins.push_back(QVariantMap{{"x", point.x() / box.width()}, {"y", point.y() / box.height()}});
  }
  // A first direct pin drag starts from the visible plane, including existing
  // two-slider perspective. Other canvas modes never write these inactive pins.
  if (!settings->corner_pin_enabled) dimension.insert("cornerPins", displayed_pins);
  bool depth_valid = false;
  auto depth = project_photo_text_dimension_layer_point(layer, *settings,
      QVector3D(static_cast<float>(box.width()), static_cast<float>(box.height()), static_cast<float>(-settings->depth)), &depth_valid);
  bool rotate_valid = false;
  auto rotate = project_photo_text_dimension_layer_point(layer, *settings,
      QVector3D(static_cast<float>(box.width() * settings->pivot_x), static_cast<float>(box.height() * settings->pivot_y), 0), &rotate_valid);
  if (!depth_valid) depth = bounds.bottomRight();
  if (!rotate_valid) rotate = bounds.center();
  const auto locked = layer_edit_access::is_effectively_locked(state.layers, index);
  const auto shown = original.value("enabled", true).toBool();
  return {{"valid", true}, {"editable", !locked && shown}, {"layerId", original.value("id")},
      {"documentToken", asset_id_}, {"revision", dimension_revision(original)},
      {"dimension", dimension}, {"outline", outline}, {"logicalWidth", box.width()}, {"logicalHeight", box.height()},
      {"handles", QVariantMap{{"rotate", map(rotate)}, {"depth", map(depth)}, {"corners", corners}}},
      {"reason", locked ? QStringLiteral("Unlock the text layer and its groups to edit on canvas.") :
          !shown ? QStringLiteral("Show the text layer before editing its geometry.") : QString()}};
}

QVariantMap EditorPreviewController::textDimensionPatchAt(int index, const QString& mode,
    double view_x, double view_y, const QVariantMap& start_dimension,
    const QVariantMap& context, int modifiers) const {
  if (!std::isfinite(view_x) || !std::isfinite(view_y)) return {};
  const auto state = currentState();
  if (index < 0 || index >= state.layers.size()) return {};
  const auto layer = state.layers[index].toMap();
  const auto source = sourceImageSize();
  const auto crop = crop_editing_ ? QRect(QPoint(), source) : crop_rect_for_image(source, state);
  const auto placement = vector_mask_placement(layer, state.layers, DesktopSourceGeometry{source, crop});
  if (!placement.valid) return {};
  const auto local = [&](double x, double y) {
    const auto iw = context.value("overlayImageWidth", 1).toDouble();
    const auto ih = context.value("overlayImageHeight", 1).toDouble();
    if (iw <= 0 || ih <= 0) return QPointF(qQNaN(), qQNaN());
    const auto mapped = mapCanvasCoordinate((x - context.value("overlayImageX").toDouble()) / iw,
        (y - context.value("overlayImageY").toDouble()) / ih, false, QStringLiteral("authoring-source"));
    if (!mapped.value("valid").toBool()) return QPointF(qQNaN(), qQNaN());
    const auto pixel = QPointF(mapped.value("x").toDouble() * std::max(0, source.width() - 1) + 0.5,
        mapped.value("y").toDouble() * std::max(0, source.height() - 1) + 0.5);
    return placement.local_to_source.inverted().map(pixel);
  };
  const auto current = local(view_x, view_y);
  const auto initial = local(context.value("pressViewX").toDouble(), context.value("pressViewY").toDouble());
  if (!std::isfinite(current.x()) || !std::isfinite(current.y()) || !std::isfinite(initial.x()) || !std::isfinite(initial.y())) return {};
  const auto delta = current - initial;
  const auto shift = (modifiers & static_cast<int>(Qt::ShiftModifier)) != 0;
  if (mode == "rotate") {
    auto yaw = start_dimension.value("rotationY").toDouble() + delta.x() * 180.0;
    const auto width = layer.value("pixelWidth", 512).toDouble();
    const auto height = layer.value("pixelHeight", 128).toDouble();
    auto pitch = start_dimension.value("rotationX").toDouble() - delta.y() * height / std::max(width, height) * 180.0;
    if (shift) { yaw = std::round(yaw / 15.0) * 15.0; pitch = std::round(pitch / 15.0) * 15.0; }
    return {{"rotationX", std::clamp(pitch, -3600.0, 3600.0)}, {"rotationY", std::clamp(yaw, -3600.0, 3600.0)}};
  }
  if (mode == "depth") {
    const auto start = decode_photo_text_dimension_variant_map(start_dimension);
    if (!start) return {};
    const auto width = layer.value("pixelWidth", 512).toDouble();
    const auto height = layer.value("pixelHeight", 128).toDouble();
    bool base_valid = false, next_valid = false;
    const auto base = project_photo_text_dimension_layer_point(layer, *start,
        QVector3D(static_cast<float>(width), static_cast<float>(height), static_cast<float>(-start->depth)), &base_valid);
    const auto next = project_photo_text_dimension_layer_point(layer, *start,
        QVector3D(static_cast<float>(width), static_cast<float>(height), static_cast<float>(-start->depth - 1)), &next_valid);
    const auto axis = next - base;
    const auto length_squared = QPointF::dotProduct(axis, axis);
    const QPointF movement(delta.x() * width, delta.y() * height);
    const auto change = base_valid && next_valid && length_squared > 0.0001
        ? QPointF::dotProduct(movement, axis) / length_squared : movement.x() - movement.y();
    auto depth = start->depth + change;
    if (shift) depth = std::round(depth);
    return {{"depth", std::clamp(depth, 0.0, 8192.0)}};
  }
  const QStringList corners{"corner-topLeft", "corner-topRight", "corner-bottomRight", "corner-bottomLeft"};
  const auto corner = corners.indexOf(mode);
  if (corner < 0) return {};
  auto pins = start_dimension.value("cornerPins").toList();
  if (pins.size() != 4) return {};
  auto point = current;
  if (shift) point = {std::round(point.x() * 100) / 100, std::round(point.y() * 100) / 100};
  pins[corner] = QVariantMap{{"x", point.x()}, {"y", point.y()}};
  return {{"cornerPinEnabled", true}, {"cornerPins", pins}};
}

bool EditorPreviewController::photoTextDimensionGestureMatches(int index, const QString& id) const {
  if (!photo_text_dimension_gesture_) return false;
  const auto& gesture = *photo_text_dimension_gesture_;
  const auto layers = layer_session_.layers();
  return gesture.asset_id == asset_id_ && gesture.layer_id == id && index >= 0 && index < layers.size() &&
      layers[index].toMap().value("id").toString() == id &&
      photo_paint_target_identity(layers[index].toMap()) == gesture.original_layer &&
      !layer_edit_access::is_effectively_locked(layers, index) &&
      !history_preview_state_ && !preset_preview_state_ && !history_service_.transactionActive() &&
      !professional_content_work_running_ && !photoPaintBusy() && photo_text_editing_layer_id_.isEmpty() &&
      editHistoryCurrentIndex() == gesture.history_index;
}

bool EditorPreviewController::beginTextDimensionGesture(int index, const QString& layer_id,
    const QString& document_token, const QString& revision) {
  const auto access = customer_edit_access_.enter();
  if (!access || photo_text_dimension_gesture_ || photo_layer_style_gesture_ ||
      history_preview_state_ || preset_preview_state_ || history_service_.transactionActive() ||
      professional_content_work_running_ || photoPaintBusy() || !photo_text_editing_layer_id_.isEmpty()) return false;
  const auto geometry = photoTextDimensionCanvasGeometry(index);
  if (!geometry.value("editable").toBool() || geometry.value("layerId").toString() != layer_id ||
      document_token != asset_id_ || geometry.value("revision").toString() != revision) return false;
  photo_text_dimension_gesture_ = PhotoTextDimensionGesture{asset_id_, layer_id, revision,
      photo_paint_target_identity(layer_session_.layers()[index].toMap()), photoLayerTextDimension(index), editHistoryCurrentIndex(), false};
  return true;
}

bool EditorPreviewController::previewTextDimensionGesture(int index, const QString& layer_id,
    const QVariantMap& changes, const QString& document_token, const QString& revision) {
  const auto access = customer_edit_access_.enter();
  if (!access || document_token != asset_id_ || !photoTextDimensionGestureMatches(index, layer_id) ||
      photo_text_dimension_gesture_->revision != revision || changes.isEmpty()) return false;
  const QStringList allowed{"rotationX", "rotationY", "rotationZ", "depth", "cornerPinEnabled", "cornerPins"};
  for (auto it = changes.cbegin(); it != changes.cend(); ++it) if (!allowed.contains(it.key())) return false;
  auto& gesture = *photo_text_dimension_gesture_;
  QVariantMap next;
  QString error;
  if (!merged_dimension(gesture.draft_dimension, changes, &next, &error)) { setStatus(error); return false; }
  auto layer = layer_session_.layers()[index].toMap(); layer.insert("textDimension", next);
  layer.insert("layerStyleGlobalLightAngle", photoLayerStyleLightAngle(index));
  const auto crop = crop_editing_ ? QRect(QPoint(), sourceImageSize()) : crop_rect_for_image(sourceImageSize(), currentState());
  if (!preflight_photo_text_dimension(layer, execution_frame(layer, crop.size()), &error).isValid()) { setStatus(error); return false; }
  gesture.changed = next != photoLayerTextDimension(index);
  gesture.draft_dimension = std::move(next);
  submitLayerCompositeEditIntent(QStringLiteral("text_dimension_canvas_preview"), false);
  return true;
}

EditState EditorPreviewController::applyPhotoTextDimensionGesturePreview(EditState state) const {
  if (!photo_text_dimension_gesture_) return state;
  const auto& gesture = *photo_text_dimension_gesture_;
  for (int i = 0; i < state.layers.size(); ++i) {
    if (state.layers[i].toMap().value("id").toString() != gesture.layer_id) continue;
    if (!photoTextDimensionGestureMatches(i, gesture.layer_id)) return state;
    auto layer = state.layers[i].toMap(); layer.insert("textDimension", gesture.draft_dimension); state.layers[i] = layer; break;
  }
  return state;
}

bool EditorPreviewController::finishTextDimensionGesture(int index, const QString& layer_id,
    bool commit, const QString& document_token, const QString& revision) {
  const auto access = [&]() -> CustomerEditAccess::Scope {
    if (commit) return customer_edit_access_.enter();
    return customer_edit_access_.restore();
  }();
  if (!photo_text_dimension_gesture_ || photo_text_dimension_gesture_->layer_id != layer_id ||
      photo_text_dimension_gesture_->asset_id != document_token || photo_text_dimension_gesture_->revision != revision) return false;
  const auto gesture = *photo_text_dimension_gesture_;
  const auto valid = photoTextDimensionGestureMatches(index, layer_id);
  photo_text_dimension_gesture_.reset();
  if (!commit || !access || !valid || !gesture.changed) {
    if (gesture.asset_id == asset_id_) submitLayerCompositeEditIntent(QStringLiteral("text_dimension_canvas_cancel"), false);
    return !commit || (valid && !gesture.changed);
  }
  return setPhotoLayerTextDimension(index, gesture.draft_dimension);
}
}  // namespace photo_editor::desktop
