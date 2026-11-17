#include "editor_preview_controller.h"

#include "photo_layer_edit_access.h"
#include "photo_layer_style_codec.h"
#include "photo_layer_style_light.h"
#include "photo_paint_surface.h"
#include "photo_render_utils.h"
#include "photo_layer_effect_codec.h"
#include "photo_text_dimension_render.h"
#include "photo_vector_mask_coordinates.h"

#include "photo_editor/pipeline/photo_effects.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <limits>

namespace photo_editor::desktop {
namespace {
namespace codec = layer_style_codec;
using photo_editor::pipeline::LayerStyleStack;

struct CanvasFrame final {
  QSize raster;
  double scale_x = 1.0;
  double scale_y = 1.0;
  VectorMaskPlacement placement;
};

CanvasFrame canvas_frame(const QVariantMap& layer, const QVariantList& layers,
                        const QSize& source, const QRect& crop) {
  CanvasFrame frame;
  const DesktopSourceGeometry geometry{source, crop};
  if (!geometry.valid()) return frame;
  auto placement_layer = layer;
  if (layer.contains("smartObjectDocument")) placement_layer.insert("kind", QStringLiteral("group"));
  const auto kind = placement_layer.value("kind").toString();
  if (kind != "pixel" && kind != "source" && kind != "text" &&
      kind != "fill" && kind != "group") return frame;
  const auto cw = layer.value("canvasWidth", crop.width()).toInt();
  const auto ch = layer.value("canvasHeight", crop.height()).toInt();
  if (cw <= 0 || ch <= 0) return frame;
  frame.scale_x = static_cast<double>(crop.width()) / cw;
  frame.scale_y = static_cast<double>(crop.height()) / ch;
  if (kind == "fill" || kind == "group" || layer.value("copyBaseImage").toBool()) {
    frame.raster = crop.size();
  } else {
    auto sx = frame.scale_x;
    auto sy = frame.scale_y;
    if (kind == "pixel" && layer.value("coordinateSpace").toString() == "source-area") {
      const auto area = layer.value("coordinateArea").toMap();
      const auto current = geometry.edge_area();
      const auto w = area.value("width").toDouble();
      const auto h = area.value("height").toDouble();
      if (!std::isfinite(w) || !std::isfinite(h) || w <= 0 || h <= 0) return {};
      sx *= w / current.width();
      sy *= h / current.height();
    }
    const auto width = kind == "text" ? std::clamp(layer.value("pixelWidth", 512).toInt(), 16, 8192)
                                       : layer.value("pixelWidth").toInt();
    const auto height = kind == "text" ? std::clamp(layer.value("pixelHeight", 128).toInt(), 16, 8192)
                                        : layer.value("pixelHeight").toInt();
    const auto rw = width * sx;
    const auto rh = height * sy;
    if (width <= 0 || height <= 0 || !std::isfinite(rw) || !std::isfinite(rh) ||
        rw > 1000000 || rh > 1000000 || rw <= 0 || rh <= 0) return {};
    frame.raster = QSize(std::max(1, qRound(rw)), std::max(1, qRound(rh)));
  }
  frame.placement = vector_mask_placement(placement_layer, layers, geometry);
  if (!frame.placement.valid) return {};
  if (kind == "text" && photo_text_dimension_is_active(layer)) {
    auto bounds = preflight_photo_text_dimension(layer, frame.raster);
    if (!bounds.isValid()) return {};
    const auto effects = decode_photo_layer_effects(layer);
    if (!effects) return {};
    photo_editor::pipeline::PhotoEffectSupport effect_support;
    const auto effect_admission = photo_editor::pipeline::photo_effect_stack_support(
        *effects, {.scale_x = frame.scale_x, .scale_y = frame.scale_y},
        &effect_support);
    if (!effect_admission.ok() || effect_support.x < 0 || effect_support.y < 0)
      return {};
    const bool expands_transparent_bounds = std::any_of(
        effects->cbegin(), effects->cend(), [](const auto& effect) {
          const auto edge = effect.type_id == "box_blur"
              ? effect.box.edge_mode : effect.gaussian.edge_mode;
          return effect.enabled && effect.amount > 0.0 &&
              edge == photo_editor::pipeline::PhotoEffectEdgeMode::Transparent;
        });
    const auto box_width = layer.value("pixelWidth", 512).toDouble();
    const auto box_height = layer.value("pixelHeight", 128).toDouble();
    const auto raster_scale_x = frame.raster.width() / box_width;
    const auto raster_scale_y = frame.raster.height() / box_height;
    if (!std::isfinite(raster_scale_x) || !std::isfinite(raster_scale_y) ||
        raster_scale_x <= 0.0 || raster_scale_y <= 0.0) return {};
    if (expands_transparent_bounds &&
        (effect_support.x > 0 || effect_support.y > 0)) {
      const auto support_x = effect_support.x / raster_scale_x;
      const auto support_y = effect_support.y / raster_scale_y;
      if (!std::isfinite(support_x) || !std::isfinite(support_y)) return {};
      bounds = bounds.adjusted(-support_x, -support_y, support_x, support_y);
    }
    // Convert the renderer's complete padded texture to the authored layer
    // frame exactly once. Rounding the original raster can differ from the
    // document scale, so support uses that raster's actual logical-pixel ratio.
    const QTransform expanded_to_authored(bounds.width() / box_width, 0, 0,
        bounds.height() / box_height, bounds.x() / box_width, bounds.y() / box_height);
    frame.placement.local_to_source = expanded_to_authored * frame.placement.local_to_source;
    const auto width = bounds.width() * raster_scale_x;
    const auto height = bounds.height() * raster_scale_y;
    if (!std::isfinite(width) || !std::isfinite(height) || width < 1 || height < 1 ||
        width > 16384 || height > 16384) return {};
    frame.raster = QSize(qRound(width), qRound(height));
  }
  return frame;
}

int style_position(const LayerStyleStack& stack, const QString& id) {
  for (std::size_t i = 0; i < stack.operations.size(); ++i)
    if (QString::fromStdString(stack.operations[i].id) == id) return static_cast<int>(i);
  return -1;
}

bool canvas_style(const QVariantMap& operation) {
  const auto type = operation.value("typeId").toString();
  const auto parameters = operation.value("parameters").toMap();
  return type == "drop_shadow" || type == "inner_shadow" || type == "gradient_overlay" ||
      (type == "stroke" && parameters.value("fill").toMap().value("kind").toString() == "gradient");
}

bool preflight(const QVariantMap& layer, const QVariantList& layers,
               const QSize& source, const QRect& crop, double light, QString* error) {
  const auto frame = canvas_frame(layer, layers, source, crop);
  if (frame.raster.isEmpty()) {
    if (error) *error = QStringLiteral("This layer's style frame cannot be mapped safely.");
    return false;
  }
  LayerStyleStack stack;
  if (!codec::decode_stack(layer.value("layerStyleStack").toString(), &stack, error)) return false;
  photo_editor::pipeline::LayerStyleRenderOptions options;
  options.document_scale_x = frame.scale_x;
  options.document_scale_y = frame.scale_y;
  options.global_light_angle_degrees = light;
  const auto result = photo_editor::pipeline::preflight_layer_style_stack(
      frame.raster.width(), frame.raster.height(), stack, options);
  if (!result.ok() && error) *error = QString::fromStdString(result.error);
  return result.ok();
}

// A canvas gesture may only change its named geometry controls. Full parameter
// maps from QML retain all colors, masks and other authored settings exactly.
bool geometry_parameters(const QVariantMap& original, const QVariantMap& supplied,
                         const QString& type, QVariantMap* updated,
                         double* light_angle, bool* shared, QString* error) {
  auto parameters = original;
  for (auto it = supplied.cbegin(); it != supplied.cend(); ++it) parameters.insert(it.key(), it.value());
  auto unchanged_original = original;
  auto unchanged_candidate = parameters;
  *shared = false;
  if (type == "drop_shadow" || type == "inner_shadow") {
    *shared = original.value("useGlobalLight").toBool();
    *light_angle = parameters.value("angleDegrees").toDouble();
    for (const auto* key : {"distance", "angleDegrees"}) {
      unchanged_original.remove(QLatin1String(key));
      unchanged_candidate.remove(QLatin1String(key));
    }
    if (*shared) parameters.insert("angleDegrees", original.value("angleDegrees"));
  } else {
    const auto gradient_path = type == "stroke" ? QStringLiteral("fill") : QStringLiteral("gradient");
    auto before = original.value(gradient_path).toMap();
    auto after = parameters.value(gradient_path).toMap();
    if (type == "stroke") {
      if (before.value("kind") != after.value("kind")) return false;
      before = before.value("gradient").toMap();
      after = after.value("gradient").toMap();
    }
    for (const auto* key : {"angleDegrees", "scale"}) {
      before.remove(QLatin1String(key));
      after.remove(QLatin1String(key));
    }
    if (before != after) {
      if (error) *error = QStringLiteral("Canvas gradient edits cannot change the gradient stops.");
      return false;
    }
    unchanged_original.remove(gradient_path);
    unchanged_candidate.remove(gradient_path);
  }
  if (unchanged_original != unchanged_candidate) {
    if (error) *error = QStringLiteral("Canvas edits cannot change unrelated style settings.");
    return false;
  }
  *updated = parameters;
  return true;
}
}  // namespace

QVariantMap EditorPreviewController::photoLayerStyleCanvasGeometry(int index, const QString& style_id) const {
  const auto state = currentState();
  if (index < 0 || index >= state.layers.size()) return {{"valid", false}};
  const auto layer = state.layers[index].toMap();
  const auto source = sourceImageSize();
  const auto crop = crop_editing_ ? QRect(QPoint(), source) : crop_rect_for_image(source, state);
  const auto frame = canvas_frame(layer, state.layers, source, crop);
  LayerStyleStack stack;
  QString error;
  if (frame.raster.isEmpty() || !codec::decode_stack(layer.value("layerStyleStack").toString(), &stack, &error))
    return {{"valid", false}, {"reason", error}};
  const auto position = style_position(stack, style_id);
  if (position < 0) return {{"valid", false}};
  const auto operation = codec::operation_to_variant(stack.operations[static_cast<std::size_t>(position)]);
  if (!operation.value("supported").toBool() || !canvas_style(operation)) return {{"valid", false}};
  const auto light = photoLayerStyleLightAngle(index);
  const auto locked = layer_edit_access::is_effectively_locked(state.layers, index);
  const auto shown = layer.value("enabled", true).toBool() && operation.value("enabled").toBool();
  const auto fingerprint = QVariantMap{{"layer", layer.value("id")}, {"stack", layer.value("layerStyleStack")},
      {"history", editHistoryCurrentIndex()}, {"light", light}, {"asset", asset_id_},
      {"width", frame.raster.width()}, {"height", frame.raster.height()},
      {"cropX", crop.x()}, {"cropY", crop.y()}, {"cropW", crop.width()}, {"cropH", crop.height()},
      {"xx", frame.placement.local_to_source.m11()}, {"xy", frame.placement.local_to_source.m12()},
      {"yx", frame.placement.local_to_source.m21()}, {"yy", frame.placement.local_to_source.m22()},
      {"dx", frame.placement.local_to_source.dx()}, {"dy", frame.placement.local_to_source.dy()}};
  const auto revision = QString::fromLatin1(QCryptographicHash::hash(
      QJsonDocument(QJsonObject::fromVariantMap(fingerprint)).toJson(QJsonDocument::Compact),
      QCryptographicHash::Sha256).toHex());
  return {{"valid", true}, {"editable", !locked && shown}, {"layerId", layer.value("id")},
      {"styleId", style_id}, {"styleType", operation.value("typeId")},
      {"styleParameters", operation.value("parameters")}, {"styleWidth", frame.raster.width()},
      {"styleHeight", frame.raster.height()}, {"styleScaleX", frame.scale_x}, {"styleScaleY", frame.scale_y},
      {"globalLightAngleDegrees", light}, {"revision", revision},
      {"reason", locked ? QStringLiteral("Unlock the layer and its groups to edit on canvas.") :
          !shown ? QStringLiteral("Show the layer and enable this style to edit on canvas.") : QString()}};
}

QVariantMap EditorPreviewController::mapPhotoLayerStyleCoordinate(int index, double x, double y, bool to_view) const {
  if (!std::isfinite(x) || !std::isfinite(y)) return {{"valid", false}};
  const auto state = currentState();
  if (index < 0 || index >= state.layers.size()) return {{"valid", false}};
  const auto source = sourceImageSize();
  const auto crop = crop_editing_ ? QRect(QPoint(), source) : crop_rect_for_image(source, state);
  const auto layer = state.layers[index].toMap();
  const auto frame = canvas_frame(layer, state.layers, source, crop);
  if (frame.raster.isEmpty()) return {{"valid", false}};
  const auto space = photo_text_dimension_is_active(layer)
      ? QStringLiteral("authoring-source") : QStringLiteral("source");
  if (to_view) {
    const auto pixel = frame.placement.local_to_source.map(QPointF(x / frame.raster.width(), y / frame.raster.height()));
    return mapCanvasCoordinate((pixel.x() - 0.5) / std::max(1, source.width() - 1),
        (pixel.y() - 0.5) / std::max(1, source.height() - 1), true, space);
  }
  const auto mapped = mapCanvasCoordinate(x, y, false, space);
  if (!mapped.value("valid").toBool()) return {{"valid", false}};
  const auto pixel = QPointF(mapped.value("x").toDouble() * std::max(0, source.width() - 1) + 0.5,
      mapped.value("y").toDouble() * std::max(0, source.height() - 1) + 0.5);
  const auto local = frame.placement.local_to_source.inverted().map(pixel);
  return {{"valid", true}, {"x", local.x() * frame.raster.width()}, {"y", local.y() * frame.raster.height()}};
}

bool EditorPreviewController::photoLayerStyleGestureMatches(int index, const QString& layer_id, const QString& style_id) const {
  if (!photo_layer_style_gesture_) return false;
  const auto& gesture = *photo_layer_style_gesture_;
  const auto layers = layer_session_.layers();
  return gesture.asset_id == asset_id_ && gesture.layer_id == layer_id && gesture.style_id == style_id &&
      index >= 0 && index < layers.size() &&
      photo_paint_target_identity(layers[index].toMap()) == gesture.original_layer &&
      layers[index].toMap().value("id").toString() == layer_id &&
      !layer_edit_access::is_effectively_locked(layers, index) &&
      !history_preview_state_ && !preset_preview_state_ &&
      photo_text_editing_layer_id_.isEmpty() &&
      !history_service_.transactionActive() && !professional_content_work_running_ && !photoPaintBusy() &&
      editHistoryCurrentIndex() == gesture.history_index &&
      photoLayerStyleLightAngle(index) == gesture.original_light_angle;
}

bool EditorPreviewController::beginPhotoLayerStyleGesture(int index, const QString& layer_id, const QString& style_id) {
  const auto access = customer_edit_access_.enter();
  if (!access || photo_layer_style_gesture_ || photo_text_dimension_gesture_ || professional_content_work_running_ || photoPaintBusy() ||
      !photo_text_editing_layer_id_.isEmpty() || history_service_.transactionActive() ||
      history_preview_state_ || preset_preview_state_) return false;
  const auto context = photoLayerStyleCanvasGeometry(index, style_id);
  if (!context.value("editable").toBool() || context.value("layerId").toString() != layer_id) return false;
  PhotoLayerStyleGesture gesture;
  gesture.asset_id = asset_id_; gesture.layer_id = layer_id; gesture.style_id = style_id;
  gesture.original_layer = photo_paint_target_identity(layer_session_.layers()[index].toMap());
  gesture.draft_stack = gesture.original_layer.value("layerStyleStack").toString();
  gesture.original_light_angle = gesture.draft_light_angle = photoLayerStyleLightAngle(index);
  gesture.history_index = editHistoryCurrentIndex();
  photo_layer_style_gesture_ = std::move(gesture);
  return true;
}

bool EditorPreviewController::previewPhotoLayerStyleGesture(int index, const QString& layer_id,
    const QString& style_id, const QVariantMap& patch) {
  const auto access = customer_edit_access_.enter();
  if (!access || !photoLayerStyleGestureMatches(index, layer_id, style_id)) {
    if (photo_layer_style_gesture_ && photo_layer_style_gesture_->layer_id == layer_id &&
        photo_layer_style_gesture_->style_id == style_id)
      static_cast<void>(finishPhotoLayerStyleGesture(index, layer_id, style_id, false));
    return false;
  }
  auto& gesture = *photo_layer_style_gesture_;
  LayerStyleStack stack;
  QString error;
  if (!codec::decode_stack(gesture.original_layer.value("layerStyleStack").toString(), &stack, &error)) return false;
  const auto position = style_position(stack, style_id);
  if (position < 0) return false;
  auto map = codec::operation_to_variant(stack.operations[static_cast<std::size_t>(position)]);
  QVariantMap parameters;
  double proposed_angle = gesture.original_light_angle;
  bool shared = false;
  if (patch.size() != 1 || !patch.contains("parameters") ||
      patch.value("parameters").metaType().id() != QMetaType::QVariantMap ||
      !geometry_parameters(map.value("parameters").toMap(), patch.value("parameters").toMap(),
          map.value("typeId").toString(), &parameters, &proposed_angle, &shared, &error)) {
    setStatus(error.isEmpty() ? QStringLiteral("Invalid canvas style edit.") : error); return false;
  }
  map.insert("parameters", parameters);
  if (!codec::operation_from_variant(map, &stack.operations[static_cast<std::size_t>(position)], &error) ||
      !codec::validate_stack(stack, &error)) { setStatus(error); return false; }
  QString encoded;
  if (!codec::encode_stack(stack, &encoded, &error)) { setStatus(error); return false; }
  if (shared && std::abs(proposed_angle - gesture.original_light_angle) < 1e-9)
    proposed_angle = gesture.original_light_angle;
  auto state = currentState();
  if (shared && photo_layer_style_light::is_linked(state.layers[index].toMap())) {
    const auto light = photo_layer_style_light::with_document_angle(state, proposed_angle, sourceImageSize());
    if (!light.error.isEmpty()) { setStatus(light.error); return false; }
  }
  auto layer = state.layers[index].toMap(); layer.insert("layerStyleStack", encoded);
  state.layers[index] = layer;
  const auto source = sourceImageSize();
  const auto crop = crop_editing_ ? QRect(QPoint(), source) : crop_rect_for_image(source, state);
  if (!preflight(layer, state.layers, source, crop, shared ? proposed_angle : gesture.original_light_angle, &error)) {
    setStatus(error); return false;
  }
  gesture.patch = {{"parameters", parameters}};
  gesture.draft_stack = encoded;
  gesture.uses_global_light = shared;
  gesture.draft_light_angle = proposed_angle;
  submitLayerCompositeEditIntent(QStringLiteral("layer_style_canvas_preview"), false);
  return true;
}

EditState EditorPreviewController::applyPhotoLayerStyleGesturePreview(EditState state) const {
  if (!photo_layer_style_gesture_) return state;
  const auto& gesture = *photo_layer_style_gesture_;
  int index = -1;
  for (int i = 0; i < state.layers.size(); ++i)
    if (state.layers[i].toMap().value("id").toString() == gesture.layer_id) { index = i; break; }
  if (index < 0 || !photoLayerStyleGestureMatches(index, gesture.layer_id, gesture.style_id)) return state;
  if (gesture.uses_global_light && photo_layer_style_light::is_linked(state.layers[index].toMap())) {
    auto light = photo_layer_style_light::with_document_angle(state, gesture.draft_light_angle, sourceImageSize());
    if (!light.error.isEmpty()) return state;
    state = std::move(light.state);
  }
  auto layer = state.layers[index].toMap();
  layer.insert("layerStyleStack", gesture.draft_stack);
  if (gesture.uses_global_light && !photo_layer_style_light::is_linked(layer))
    layer.insert("layerStyleGlobalLightAngle", gesture.draft_light_angle);
  state.layers[index] = layer;
  return state;
}

bool EditorPreviewController::finishPhotoLayerStyleGesture(int index, const QString& layer_id,
    const QString& style_id, bool commit) {
  const auto access = commit ? customer_edit_access_.enter() : customer_edit_access_.restore();
  if (!photo_layer_style_gesture_ || photo_layer_style_gesture_->layer_id != layer_id ||
      photo_layer_style_gesture_->style_id != style_id) return false;
  const auto gesture = *photo_layer_style_gesture_;
  const auto valid = photoLayerStyleGestureMatches(index, layer_id, style_id);
  photo_layer_style_gesture_.reset();
  if (!commit || !access || !valid || gesture.patch.isEmpty()) {
    if (gesture.asset_id == asset_id_) submitLayerCompositeEditIntent(QStringLiteral("layer_style_canvas_cancel"), false);
    return !commit;
  }
  const auto parameters_changed = gesture.draft_stack != gesture.original_layer.value("layerStyleStack").toString();
  const auto lighting_changed = gesture.uses_global_light &&
      std::abs(gesture.draft_light_angle - gesture.original_light_angle) >= 1e-9;
  if (!parameters_changed && !lighting_changed) return true;
  beginCoalescedEdit();
  if ((parameters_changed && !setPhotoLayerStyleParameters(index, style_id, gesture.patch)) ||
      (lighting_changed && !setPhotoLayerStyleLightAngle(index, gesture.draft_light_angle))) {
    static_cast<void>(cancelCoalescedEdit());
    submitLayerCompositeEditIntent(QStringLiteral("layer_style_canvas_rejected"), false);
    return false;
  }
  endCoalescedEdit();
  return true;
}
}  // namespace photo_editor::desktop
