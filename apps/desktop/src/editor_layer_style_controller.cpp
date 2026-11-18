#include "editor_preview_controller.h"

#include "photo_editor/pipeline/layer_styles.h"
#include "mask_presentation_schema.h"
#include "mask_workflow_ownership.h"
#include "photo_layer_style_codec.h"
#include "photo_layer_style_light.h"
#include "photo_text_dimension_render.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>
#include <QSet>
#include <QSize>
#include <QUrl>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace photo_editor::desktop {
namespace {
namespace codec = layer_style_codec;
using photo_editor::pipeline::LayerStyleOperation;
using photo_editor::pipeline::LayerStyleStack;

bool layer_or_ancestor_locked(const QVariantList& layers, int index) {
  QSet<QString> visited;
  for (int depth = 0; depth < 64 && index >= 0 && index < layers.size(); ++depth) {
    const auto layer = layers.at(index).toMap();
    const auto id = layer.value(QStringLiteral("id")).toString();
    if (layer.value(QStringLiteral("locked")).toBool() || visited.contains(id)) return true;
    visited.insert(id);
    const auto parent_id = layer.value(QStringLiteral("parentId")).toString();
    if (parent_id.isEmpty()) return false;
    index = -1;
    for (int candidate = 0; candidate < layers.size(); ++candidate) {
      if (layers.at(candidate).toMap().value(QStringLiteral("id")).toString() == parent_id) {
        index = candidate;
        break;
      }
    }
  }
  return true;
}

bool style_target_supported(const QVariantMap& layer) {
  const auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
  return kind == QStringLiteral("pixel") || kind == QStringLiteral("source") ||
      kind == QStringLiteral("text") || kind == QStringLiteral("fill") ||
      kind == QStringLiteral("group");
}

bool validate_mask_references(const LayerStyleStack& stack,
                              const QVariantList& mask_stack,
                              QString* error) {
  QSet<QString> available_ids;
  for (const auto& value : mask_stack) {
    const auto mask = value.toMap();
    const auto set_id = mask.value(QStringLiteral("maskSetId")).toString().trimmed();
    if (!set_id.isEmpty()) available_ids.insert(set_id);
  }
  for (const auto& operation : stack.operations) {
    if (operation.mask_ref.empty()) continue;
    const auto mask_id = QString::fromUtf8(operation.mask_ref.data(),
        static_cast<qsizetype>(operation.mask_ref.size())).trimmed();
    if (!available_ids.contains(mask_id)) {
      if (error != nullptr) {
        *error = QStringLiteral(
            "This style references a missing layer mask. Restore that mask or clear the style mask reference before applying.");
      }
      return false;
    }
  }
  return true;
}

bool stack_has_mask_references(const LayerStyleStack& stack) {
  return std::any_of(stack.operations.cbegin(), stack.operations.cend(),
      [](const LayerStyleOperation& operation) { return !operation.mask_ref.empty(); });
}

QString fresh_mask_id() {
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QVariantList clone_selection_set_for_style(const QVariantList& sources,
                                           const QString& source_set_id,
                                           const QString& destination_set_id) {
  QVariantList result;
  QHash<QString, QString> copied_ids;
  for (const auto& value : sources) {
    const auto mask = value.toMap();
    if (mask.value(QStringLiteral("maskSetId")).toString() != source_set_id ||
        !is_selection_mask(mask)) continue;
    copied_ids.insert(mask.value(QStringLiteral("id")).toString(), fresh_mask_id());
  }
  if (copied_ids.isEmpty()) return result;
  for (const auto& value : sources) {
    auto mask = value.toMap();
    if (mask.value(QStringLiteral("maskSetId")).toString() != source_set_id ||
        !is_selection_mask(mask)) continue;
    mask.insert(QStringLiteral("id"), copied_ids.value(mask.value(QStringLiteral("id")).toString()));
    mask.insert(QStringLiteral("maskSetId"), destination_set_id);
    mask.insert(QStringLiteral("maskSetName"), QStringLiteral("Style mask"));
    for (const auto* key : {"parentId", "outputId"}) {
      const auto original = mask.value(QLatin1String(key)).toString();
      if (copied_ids.contains(original)) mask.insert(QLatin1String(key), copied_ids.value(original));
    }
    result.append(mask);
  }
  return result;
}

void remove_exclusive_style_mask_sources(EditState* state,
                                         const QVariantMap& previous_layer,
                                         const QVariantList& surviving_layers) {
  if (state == nullptr) return;
  const auto exclusive = exclusive_mask_sets_for_deleted_layer(
      previous_layer, surviving_layers, state->mask_stack);
  if (exclusive.isEmpty()) return;
  const auto selection_ids = state->selection_state.value(QStringLiteral("savedSetIds"))
                                 .toStringList();
  const auto current_id = state->selection_state.value(QStringLiteral("currentSetId")).toString();
  const auto previous_id = state->selection_state.value(QStringLiteral("previousSetId")).toString();
  QSet<QString> locked_set_ids;
  for (const auto& value : state->mask_stack) {
    const auto mask = value.toMap();
    if (mask.value(QStringLiteral("locked")).toBool()) {
      const auto id = mask.value(QStringLiteral("maskSetId")).toString().trimmed();
      if (!id.isEmpty()) locked_set_ids.insert(id);
    }
  }
  QVariantList retained;
  retained.reserve(state->mask_stack.size());
  for (const auto& value : state->mask_stack) {
    const auto id = value.toMap().value(QStringLiteral("maskSetId")).toString();
    if (!exclusive.contains(id) || locked_set_ids.contains(id) || selection_ids.contains(id) ||
        current_id == id || previous_id == id) retained.append(value);
  }
  state->mask_stack = retained;
}

bool preflight_stack_for_layer(const QVariantMap& layer, const LayerStyleStack& stack,
                               double global_light_angle, const QSize& document_size,
                               QString* error) {
  const auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
  QSize raster_size = document_size;
  if (kind != QStringLiteral("fill") && kind != QStringLiteral("group")) {
    bool width_ok = false;
    bool height_ok = false;
    const auto width = layer.value(QStringLiteral("pixelWidth")).toInt(&width_ok);
    const auto height = layer.value(QStringLiteral("pixelHeight")).toInt(&height_ok);
    if (width_ok && height_ok && width > 0 && height > 0) raster_size = QSize(width, height);
  }
  if (!raster_size.isValid() || raster_size.width() <= 0 || raster_size.height() <= 0) {
    if (error != nullptr)
      *error = QStringLiteral("The layer source dimensions are unavailable for safe style rendering.");
    return false;
  }
  if (kind == QStringLiteral("text") && photo_text_dimension_is_active(layer)) {
    auto candidate = layer;
    const auto encoded = photo_editor::pipeline::serialize_layer_style_stack(stack);
    if (!encoded) { if (error) *error = QStringLiteral("Layer styles could not be encoded."); return false; }
    candidate.insert("layerStyleStack", QString::fromStdString(*encoded));
    candidate.insert("_documentLayerStyleLightAngle", global_light_angle);
    return preflight_photo_text_dimension(candidate, raster_size, error).isValid();
  }
  photo_editor::pipeline::LayerStyleRenderOptions options;
  options.document_scale_x = 1.0;
  options.document_scale_y = 1.0;
  options.global_light_angle_degrees = global_light_angle;
  const auto preflight = photo_editor::pipeline::preflight_layer_style_stack(
      raster_size.width(), raster_size.height(), stack, options);
  if (!preflight.ok()) {
    if (error != nullptr) *error = QString::fromStdString(preflight.error);
    return false;
  }
  return true;
}

QString preflight_rejection_message(const QString& action, const QString& detail) {
  return QStringLiteral(
      "%1 was rejected before history because it cannot be rendered safely: %2. Check the source dimensions and reduce blur, spread, size, or shadow distance, then try again.")
      .arg(action, detail);
}

bool load_stack(const QVariantMap& layer, LayerStyleStack* stack, QString* error) {
  return codec::decode_stack(layer.value(QStringLiteral("layerStyleStack")).toString(),
                             stack, error);
}

bool store_stack(QVariantMap* layer, const LayerStyleStack& stack, QString* error) {
  if (layer == nullptr) {
    if (error != nullptr) *error = QStringLiteral("Missing layer output.");
    return false;
  }
  QString serialized;
  if (!codec::encode_stack(stack, &serialized, error)) return false;
  layer->insert(QStringLiteral("layerStyleStack"), serialized);
  if (!layer->contains(QStringLiteral("layerStyleGlobalLightAngle")))
    layer->insert(QStringLiteral("layerStyleGlobalLightAngle"), -45.0);
  return true;
}

double layer_light_angle(const QVariantMap& layer) {
  bool ok = false;
  const auto angle = layer.value(QStringLiteral("layerStyleGlobalLightAngle"), -45.0)
                         .toDouble(&ok);
  return ok && std::isfinite(angle) && angle >= -3600.0 && angle <= 3600.0
      ? angle : -45.0;
}

int operation_index(const LayerStyleStack& stack, const QString& id) {
  const auto found = std::find_if(stack.operations.begin(), stack.operations.end(),
      [&id](const LayerStyleOperation& operation) {
        return QString::fromStdString(operation.id) == id;
      });
  return found == stack.operations.end()
      ? -1 : static_cast<int>(found - stack.operations.begin());
}

photo_editor::pipeline::LayerStylePlane inferred_stroke_plane(
    photo_editor::pipeline::LayerStyleStrokeAlignment alignment) {
  using photo_editor::pipeline::LayerStylePlane;
  using photo_editor::pipeline::LayerStyleStrokeAlignment;
  if (alignment == LayerStyleStrokeAlignment::Inside) return LayerStylePlane::OnContent;
  if (alignment == LayerStyleStrokeAlignment::Center) return LayerStylePlane::AboveContent;
  return LayerStylePlane::BelowContent;
}

bool parse_update(const LayerStyleOperation& current, const QVariantMap& updates,
                  LayerStyleOperation* result, QString* error) {
  auto map = codec::operation_to_variant(current);
  auto parameters = map.value(QStringLiteral("parameters")).toMap();
  const auto provided_parameters = updates.value(QStringLiteral("parameters")).toMap();
  for (auto it = provided_parameters.cbegin(); it != provided_parameters.cend(); ++it)
    parameters.insert(it.key(), it.value());

  static const QSet<QString> base_fields{
      QStringLiteral("enabled"), QStringLiteral("opacity"), QStringLiteral("plane"),
      QStringLiteral("blendMode"), QStringLiteral("maskRef")};
  for (auto it = updates.cbegin(); it != updates.cend(); ++it) {
    if (it.key() == QStringLiteral("parameters")) continue;
    if (base_fields.contains(it.key())) map.insert(it.key(), it.value());
    else if (it.key() != QStringLiteral("id") && it.key() != QStringLiteral("type") &&
             it.key() != QStringLiteral("typeId") && it.key() != QStringLiteral("name") &&
             it.key() != QStringLiteral("supported") &&
             it.key() != QStringLiteral("algorithmVersion")) {
      parameters.insert(it.key(), it.value());
    }
  }
  map.insert(QStringLiteral("parameters"), parameters);
  map.insert(QStringLiteral("id"), QString::fromStdString(current.id));
  map.insert(QStringLiteral("typeId"), QString::fromStdString(current.type_id));
  map.insert(QStringLiteral("algorithmVersion"), current.algorithm_version);
  if (!codec::operation_from_variant(map, result, error)) return false;
  const auto explicitly_set_plane = updates.contains(QStringLiteral("plane"));
  const auto alignment_updated = provided_parameters.contains(QStringLiteral("alignment")) ||
      updates.contains(QStringLiteral("alignment"));
  if (current.type_id == "stroke" && alignment_updated && !explicitly_set_plane) {
    const auto* previous_stroke = std::get_if<photo_editor::pipeline::LayerStyleStroke>(
        &current.parameters);
    auto* updated_stroke = std::get_if<photo_editor::pipeline::LayerStyleStroke>(
        &result->parameters);
    if (previous_stroke != nullptr && updated_stroke != nullptr &&
        previous_stroke->alignment != updated_stroke->alignment &&
        current.plane == inferred_stroke_plane(previous_stroke->alignment)) {
      result->plane = inferred_stroke_plane(updated_stroke->alignment);
    }
  }
  return true;
}

QString local_path(const QString& path) {
  const QUrl url(path);
  return url.isLocalFile() ? url.toLocalFile() : path;
}

}  // namespace

QVariantList EditorPreviewController::photoLayerStyles(int index) const {
  const auto layers = layer_session_.layers();
  if (index < 0 || index >= layers.size()) return {};
  LayerStyleStack stack;
  if (!load_stack(layers.at(index).toMap(), &stack, nullptr)) return {};
  return codec::stack_to_variant(stack);
}

QVariantList EditorPreviewController::availablePhotoLayerStyles() const {
  return codec::available_types();
}

double EditorPreviewController::photoLayerStyleLightAngle(int index) const {
  const auto layers = layer_session_.layers();
  if (index < 0 || index >= layers.size()) return -45.0;
  const auto layer = layers.at(index).toMap();
  return photo_layer_style_light::is_linked(layer)
      ? document_layer_style_light_angle_ : layer_light_angle(layer);
}

bool EditorPreviewController::photoLayerStyleLightLinked(int index) const {
  const auto layers = layer_session_.layers();
  return index >= 0 && index < layers.size() &&
      photo_layer_style_light::is_linked(layers.at(index).toMap());
}

double EditorPreviewController::documentPhotoLayerStyleLightAngle() const {
  return document_layer_style_light_angle_;
}

bool EditorPreviewController::addPhotoLayerStyle(int index, const QString& type) {
  const auto access = customer_edit_access_.enter();
  auto layers = layer_session_.layers();
  if (!access || professional_content_work_running_ || index < 0 ||
      index >= layers.size() || layer_or_ancestor_locked(layers, index)) return false;
  auto layer = layers.at(index).toMap();
  if (!style_target_supported(layer)) {
    setStatus(QStringLiteral("Layer styles are not supported for this layer type."));
    return false;
  }
  LayerStyleStack stack;
  QString error;
  if (!load_stack(layer, &stack, &error)) {
    setStatus(QStringLiteral("Layer styles could not be read: %1").arg(error));
    return false;
  }
  LayerStyleOperation operation;
  if (!codec::create_default_operation(type,
          QUuid::createUuid().toString(QUuid::WithoutBraces), &operation, &error)) {
    setStatus(error);
    return false;
  }
  stack.operations.push_back(std::move(operation));
  if (!codec::validate_stack(stack, &error) ||
      !preflight_stack_for_layer(layer, stack, photoLayerStyleLightAngle(index),
          QSize(sourceImageWidth(), sourceImageHeight()), &error) ||
      !store_stack(&layer, stack, &error)) {
    setStatus(preflight_rejection_message(QStringLiteral("Adding this layer style"), error));
    return false;
  }
  layers[index] = layer;
  return applyPhotoLayerStackChange(layers, QStringLiteral("layer_style_add"), false,
      QStringLiteral("Add layer style"));
}

bool EditorPreviewController::setPhotoLayerStyleParameters(
    int index, const QString& style_id, const QVariantMap& parameters) {
  const auto access = customer_edit_access_.enter();
  auto layers = layer_session_.layers();
  if (!access || professional_content_work_running_ || index < 0 ||
      index >= layers.size() || layer_or_ancestor_locked(layers, index)) return false;
  auto layer = layers.at(index).toMap();
  if (!style_target_supported(layer)) {
    setStatus(QStringLiteral("Layer styles are not supported for this layer type."));
    return false;
  }
  LayerStyleStack stack;
  QString error;
  if (!load_stack(layer, &stack, &error)) {
    setStatus(QStringLiteral("Layer styles could not be read: %1").arg(error));
    return false;
  }
  const auto position = operation_index(stack, style_id);
  if (position < 0) return false;
  auto& current = stack.operations[static_cast<std::size_t>(position)];
  if (!photo_editor::pipeline::is_known_layer_style(current.type_id,
          current.algorithm_version)) {
    setStatus(QStringLiteral("This style comes from an unsupported version and is read-only."));
    return false;
  }
  LayerStyleOperation updated;
  if (!parse_update(current, parameters, &updated, &error)) {
    setStatus(QStringLiteral("Layer style parameters are invalid: %1").arg(error));
    return false;
  }
  if (updated == current) return false;
  current = std::move(updated);
  if (!codec::validate_stack(stack, &error) ||
      !validate_mask_references(stack, currentState().mask_stack, &error) ||
      !preflight_stack_for_layer(layer, stack, photoLayerStyleLightAngle(index),
          QSize(sourceImageWidth(), sourceImageHeight()), &error) ||
      !store_stack(&layer, stack, &error)) {
    setStatus(preflight_rejection_message(QStringLiteral("This layer style edit"), error));
    return false;
  }
  layers[index] = layer;
  return applyPhotoLayerStackChange(layers, QStringLiteral("layer_style_parameters"), false,
      QStringLiteral("Edit layer style"));
}

bool EditorPreviewController::setPhotoLayerStyleEnabled(
    int index, const QString& style_id, bool enabled) {
  const auto access = customer_edit_access_.enter();
  auto layers = layer_session_.layers();
  if (!access || professional_content_work_running_ || index < 0 ||
      index >= layers.size() || layer_or_ancestor_locked(layers, index)) return false;
  auto layer = layers.at(index).toMap();
  if (!style_target_supported(layer)) {
    setStatus(QStringLiteral("Layer styles are not supported for this layer type."));
    return false;
  }
  LayerStyleStack stack;
  QString error;
  if (!load_stack(layer, &stack, &error)) {
    setStatus(QStringLiteral("Layer styles could not be read: %1").arg(error));
    return false;
  }
  const auto position = operation_index(stack, style_id);
  if (position < 0) return false;
  auto& operation = stack.operations[static_cast<std::size_t>(position)];
  const auto supported = photo_editor::pipeline::is_known_layer_style(
      operation.type_id, operation.algorithm_version);
  if (!supported && enabled) {
    setStatus(QStringLiteral("An unsupported style can be disabled or removed, but not enabled."));
    return false;
  }
  if (operation.enabled == enabled) return false;
  operation.enabled = enabled;
  if (!codec::validate_stack(stack, &error) ||
      (enabled && !preflight_stack_for_layer(layer, stack, photoLayerStyleLightAngle(index),
          QSize(sourceImageWidth(), sourceImageHeight()), &error)) ||
      !store_stack(&layer, stack, &error)) {
    setStatus(preflight_rejection_message(QStringLiteral("Enabling this layer style"), error));
    return false;
  }
  layers[index] = layer;
  return applyPhotoLayerStackChange(layers, QStringLiteral("layer_style_enabled"), false,
      enabled ? QStringLiteral("Enable layer style") : QStringLiteral("Disable layer style"));
}

bool EditorPreviewController::setPhotoLayerStyleLightAngle(int index,
                                                           double angle_degrees) {
  const auto access = customer_edit_access_.enter();
  if (!access) return false;
  const auto current_layers = layer_session_.layers();
  if (index < 0 || index >= current_layers.size()) return false;
  if (photo_layer_style_light::is_linked(current_layers.at(index).toMap()))
    return setDocumentPhotoLayerStyleLightAngle(angle_degrees);
  auto layers = layer_session_.layers();
  if (!access || professional_content_work_running_ || index < 0 ||
      index >= layers.size() || layer_or_ancestor_locked(layers, index) ||
      !std::isfinite(angle_degrees) || angle_degrees < -3600.0 || angle_degrees > 3600.0)
    return false;
  auto layer = layers.at(index).toMap();
  if (!style_target_supported(layer)) {
    setStatus(QStringLiteral("Layer styles are not supported for this layer type."));
    return false;
  }
  LayerStyleStack stack;
  QString error;
  if (!load_stack(layer, &stack, &error)) {
    setStatus(QStringLiteral("Layer styles could not be read: %1").arg(error));
    return false;
  }
  if (layer_light_angle(layer) == angle_degrees) return false;
  if (!preflight_stack_for_layer(layer, stack, angle_degrees,
          QSize(sourceImageWidth(), sourceImageHeight()), &error)) {
    setStatus(preflight_rejection_message(QStringLiteral("Changing shared layer lighting"), error));
    return false;
  }
  layer.insert(QStringLiteral("layerStyleGlobalLightAngle"), angle_degrees);
  layers[index] = layer;
  return applyPhotoLayerStackChange(layers, QStringLiteral("layer_style_light"), false,
      QStringLiteral("Change layer style lighting"));
}

bool EditorPreviewController::setDocumentPhotoLayerStyleLightAngle(
    double angle_degrees) {
  const auto access = customer_edit_access_.enter();
  if (!access || professional_content_work_running_) return false;
  const auto change = photo_layer_style_light::with_document_angle(
      currentState(), angle_degrees, sourceImageSize());
  if (!change.error.isEmpty()) {
    setStatus(change.error);
    return false;
  }
  if (!change.applied) return false;
  for (const auto& value : change.state.layers) {
    const auto layer = value.toMap();
    if (!photo_layer_style_light::is_linked(layer) || !photo_text_dimension_is_active(layer)) continue;
    LayerStyleStack stack;
    QString error;
    if (!load_stack(layer, &stack, &error) || !preflight_stack_for_layer(layer, stack,
        angle_degrees, sourceImageSize(), &error)) {
      setStatus(preflight_rejection_message(QStringLiteral("Changing document lighting"), error));
      return false;
    }
  }
  rememberStateForUndo(QStringLiteral("Change document layer style lighting"));
  document_layer_style_light_angle_ = change.state.document_layer_style_light_angle;
  invalidatePhotoLayerThumbnailCache();
  persistCurrentState();
  scheduleControlsChanged();
  photo_layer_adjustment_render_timer_.stop();
  photo_layer_adjustment_render_burst_started_msecs_ = 0;
  photo_layer_adjustment_render_pending_write_count_ = 0;
  submitLayerCompositeEditIntent(QStringLiteral("layer_style_light"), false,
      QStringLiteral("photo_layer_change"));
  return true;
}

bool EditorPreviewController::setPhotoLayerStyleLightLinked(int index,
                                                            bool linked) {
  const auto access = customer_edit_access_.enter();
  if (!access || professional_content_work_running_) return false;
  const auto change = photo_layer_style_light::with_layer_linked(
      currentState(), index, linked, sourceImageSize());
  if (!change.error.isEmpty()) {
    setStatus(change.error);
    return false;
  }
  if (!change.applied) return false;
  return applyPhotoLayerStackChange(
      change.state.layers, QStringLiteral("layer_style_light_link"), false,
      linked ? QStringLiteral("Link layer style lighting")
             : QStringLiteral("Unlink layer style lighting"));
}

bool EditorPreviewController::movePhotoLayerStyle(
    int index, const QString& style_id, int destination) {
  const auto access = customer_edit_access_.enter();
  auto layers = layer_session_.layers();
  if (!access || professional_content_work_running_ || index < 0 ||
      index >= layers.size() || layer_or_ancestor_locked(layers, index)) return false;
  auto layer = layers.at(index).toMap();
  if (!style_target_supported(layer)) {
    setStatus(QStringLiteral("Layer styles are not supported for this layer type."));
    return false;
  }
  LayerStyleStack stack;
  QString error;
  if (!load_stack(layer, &stack, &error)) {
    setStatus(QStringLiteral("Layer styles could not be read: %1").arg(error));
    return false;
  }
  const auto source = operation_index(stack, style_id);
  if (source < 0 || destination < 0 || destination >= static_cast<int>(stack.operations.size()) ||
      source == destination) return false;
  auto operation = std::move(stack.operations[static_cast<std::size_t>(source)]);
  stack.operations.erase(stack.operations.begin() + source);
  stack.operations.insert(stack.operations.begin() + destination, std::move(operation));
  if (!store_stack(&layer, stack, &error)) {
    setStatus(error);
    return false;
  }
  layers[index] = layer;
  return applyPhotoLayerStackChange(layers, QStringLiteral("layer_style_reorder"), false,
      QStringLiteral("Reorder layer styles"));
}

bool EditorPreviewController::removePhotoLayerStyle(int index, const QString& style_id) {
  const auto access = customer_edit_access_.enter();
  auto layers = layer_session_.layers();
  if (!access || professional_content_work_running_ || index < 0 ||
      index >= layers.size() || layer_or_ancestor_locked(layers, index)) return false;
  auto layer = layers.at(index).toMap();
  if (!style_target_supported(layer)) {
    setStatus(QStringLiteral("Layer styles are not supported for this layer type."));
    return false;
  }
  LayerStyleStack stack;
  QString error;
  if (!load_stack(layer, &stack, &error)) {
    setStatus(QStringLiteral("Layer styles could not be read: %1").arg(error));
    return false;
  }
  const auto position = operation_index(stack, style_id);
  if (position < 0) return false;
  stack.operations.erase(stack.operations.begin() + position);
  if (!store_stack(&layer, stack, &error)) {
    setStatus(error);
    return false;
  }
  layers[index] = layer;
  return applyPhotoLayerStackChange(layers, QStringLiteral("layer_style_remove"), false,
      QStringLiteral("Remove layer style"));
}

bool EditorPreviewController::maskPhotoLayerStyleFromSelection(
    int index, const QString& style_id) {
  const auto access = customer_edit_access_.enter();
  if (!access || professional_content_work_running_) return false;
  auto next = currentState();
  if (index < 0 || index >= next.layers.size()) {
    setStatus(QStringLiteral("Choose a layer before creating a style mask."));
    return false;
  }
  if (layer_or_ancestor_locked(next.layers, index)) {
    setStatus(QStringLiteral("Unlock this layer and its parent groups before changing a style mask."));
    return false;
  }

  const auto source_set_id = workingSelectionSetId();
  if (source_set_id.isEmpty()) {
    setStatus(QStringLiteral(
        "Create or activate a selection first, then mask this style from that selection."));
    return false;
  }
  QVariantList source_masks;
  for (const auto& value : next.mask_stack) {
    const auto mask = value.toMap();
    if (mask.value(QStringLiteral("maskSetId")).toString() == source_set_id &&
        is_selection_mask(mask)) source_masks.append(value);
  }
  if (source_masks.isEmpty()) {
    setStatus(QStringLiteral(
        "The current selection is unavailable. Reselect it or create a new selection before masking this style."));
    return false;
  }
  for (const auto& value : source_masks) {
    const auto mask = value.toMap();
    if (mask.value(QStringLiteral("locked")).toBool() ||
        selection_has_locked_consumer(mask, next.layers)) {
      setStatus(QStringLiteral(
          "Unlock the selection and any layer using it before copying it to a style mask."));
      return false;
    }
  }

  auto layer = next.layers.at(index).toMap();
  if (!style_target_supported(layer)) {
    setStatus(QStringLiteral("Layer styles are not supported for this layer type."));
    return false;
  }
  LayerStyleStack stack;
  QString error;
  if (!load_stack(layer, &stack, &error)) {
    setStatus(QStringLiteral("Layer styles could not be read: %1").arg(error));
    return false;
  }
  const auto position = operation_index(stack, style_id);
  if (position < 0) {
    setStatus(QStringLiteral("This style no longer exists. Refresh the inspector and try again."));
    return false;
  }
  auto& operation = stack.operations[static_cast<std::size_t>(position)];
  if (!photo_editor::pipeline::is_known_layer_style(operation.type_id,
          operation.algorithm_version)) {
    setStatus(QStringLiteral("This style comes from an unsupported version and cannot be edited."));
    return false;
  }

  const auto previous_layer = layer;
  const auto destination_set_id = fresh_mask_id();
  const auto copied_masks = clone_selection_set_for_style(
      next.mask_stack, source_set_id, destination_set_id);
  if (copied_masks.isEmpty()) {
    setStatus(QStringLiteral("The current selection could not be copied into a style mask."));
    return false;
  }
  const auto previous_mask_ref = QString::fromStdString(operation.mask_ref);
  operation.mask_ref = destination_set_id.toStdString();
  if (!codec::validate_stack(stack, &error) ||
      !store_stack(&layer, stack, &error)) {
    setStatus(QStringLiteral("Style mask could not be created: %1").arg(error));
    return false;
  }
  next.layers[index] = layer;
  next.mask_stack.append(copied_masks);
  if (!preflight_stack_for_layer(layer, stack, photoLayerStyleLightAngle(index),
          QSize(sourceImageWidth(), sourceImageHeight()), &error)) {
    setStatus(preflight_rejection_message(QStringLiteral("Creating this style mask"), error));
    return false;
  }
  if (!validate_mask_references(stack, next.mask_stack, &error)) {
    setStatus(QStringLiteral("Style mask could not be created: %1").arg(error));
    return false;
  }
  if (!previous_mask_ref.isEmpty()) {
    const QVariantList survivors = next.layers;
    remove_exclusive_style_mask_sources(&next, previous_layer, survivors);
  }
  rememberStateForUndo(QStringLiteral("Mask layer style from selection"));
  applyState(next);
  persistCurrentState();
  submitFullPipelineEditIntent(QStringLiteral("layer_style_selection_mask"));
  return true;
}

bool EditorPreviewController::selectPhotoLayerStyleMask(
    int index, const QString& style_id) {
  const auto layers = layer_session_.layers();
  if (index < 0 || index >= layers.size()) {
    setStatus(QStringLiteral("Choose a layer before editing its style mask."));
    return false;
  }
  if (layer_or_ancestor_locked(layers, index)) {
    setStatus(QStringLiteral("Unlock this layer and its parent groups before editing its style mask."));
    return false;
  }
  LayerStyleStack stack;
  QString error;
  if (!load_stack(layers.at(index).toMap(), &stack, &error)) {
    setStatus(QStringLiteral("Layer styles could not be read: %1").arg(error));
    return false;
  }
  const auto position = operation_index(stack, style_id);
  if (position < 0) {
    setStatus(QStringLiteral("This style no longer exists. Refresh the inspector and try again."));
    return false;
  }
  const auto& operation = stack.operations[static_cast<std::size_t>(position)];
  if (operation.mask_ref.empty()) {
    setStatus(QStringLiteral("This style has no mask yet. Create one from the current selection."));
    return false;
  }
  const auto set_id = QString::fromStdString(operation.mask_ref);
  const auto mask_index = mask_session_.indexForMaskSet(set_id);
  if (mask_index < 0) {
    setStatus(QStringLiteral("This style mask is missing. Clear it or restore the mask source."));
    return false;
  }
  const auto mask = mask_session_.stack().at(mask_index).toMap();
  if (!is_selection_mask(mask)) {
    setStatus(QStringLiteral("This style mask is not an editable selection mask."));
    return false;
  }
  if (!selectLayerMask(index, set_id)) {
    setStatus(QStringLiteral("This style mask could not be selected for editing."));
    return false;
  }
  return true;
}

bool EditorPreviewController::clearPhotoLayerStyleMask(
    int index, const QString& style_id) {
  const auto access = customer_edit_access_.enter();
  if (!access || professional_content_work_running_) return false;
  auto next = currentState();
  if (index < 0 || index >= next.layers.size()) {
    setStatus(QStringLiteral("Choose a layer before clearing a style mask."));
    return false;
  }
  if (layer_or_ancestor_locked(next.layers, index)) {
    setStatus(QStringLiteral("Unlock this layer and its parent groups before clearing a style mask."));
    return false;
  }
  auto layer = next.layers.at(index).toMap();
  if (!style_target_supported(layer)) {
    setStatus(QStringLiteral("Layer styles are not supported for this layer type."));
    return false;
  }
  LayerStyleStack stack;
  QString error;
  if (!load_stack(layer, &stack, &error)) {
    setStatus(QStringLiteral("Layer styles could not be read: %1").arg(error));
    return false;
  }
  const auto position = operation_index(stack, style_id);
  if (position < 0) {
    setStatus(QStringLiteral("This style no longer exists. Refresh the inspector and try again."));
    return false;
  }
  auto& operation = stack.operations[static_cast<std::size_t>(position)];
  if (operation.mask_ref.empty()) {
    setStatus(QStringLiteral("This style does not have a mask to clear."));
    return false;
  }
  const auto previous_layer = layer;
  operation.mask_ref.clear();
  if (!codec::validate_stack(stack, &error) ||
      !store_stack(&layer, stack, &error)) {
    setStatus(QStringLiteral("Style mask could not be cleared: %1").arg(error));
    return false;
  }
  next.layers[index] = layer;
  if (!validate_mask_references(stack, next.mask_stack, &error)) {
    setStatus(QStringLiteral("Style mask could not be cleared: %1").arg(error));
    return false;
  }
  const QVariantList survivors = next.layers;
  remove_exclusive_style_mask_sources(&next, previous_layer, survivors);
  rememberStateForUndo(QStringLiteral("Clear layer style mask"));
  applyState(next);
  persistCurrentState();
  submitFullPipelineEditIntent(QStringLiteral("clear_layer_style_mask"));
  return true;
}

QString EditorPreviewController::photoLayerStyleEnvelope(int index) const {
  const auto layers = layer_session_.layers();
  if (index < 0 || index >= layers.size()) return {};
  const auto layer = layers.at(index).toMap();
  LayerStyleStack stack;
  QString error;
  if (!load_stack(layer, &stack, &error)) return {};
  QString serialized;
  if (!codec::encode_stack(stack, &serialized, &error)) return {};
  const auto bytes = codec::encode_envelope(
      serialized, photoLayerStyleLightAngle(index), &error);
  if (bytes.isEmpty()) return {};
  return QString::fromUtf8(bytes);
}

bool EditorPreviewController::applyPhotoLayerStyleEnvelope(int index,
                                                           const QString& envelope) {
  const auto access = customer_edit_access_.enter();
  auto layers = layer_session_.layers();
  if (!access || professional_content_work_running_ || index < 0 ||
      index >= layers.size() || layer_or_ancestor_locked(layers, index)) return false;
  const auto bytes = envelope.toUtf8();
  QString serialized;
  double light_angle = -45.0;
  QString error;
  if (!codec::decode_envelope(bytes, &serialized, &light_angle, &error)) {
    setStatus(QStringLiteral("Layer style envelope could not be applied: %1").arg(error));
    return false;
  }
  LayerStyleStack stack;
  if (!codec::decode_stack(serialized, &stack, &error) ||
      !codec::validate_stack(stack, &error)) {
    setStatus(QStringLiteral("Layer style envelope could not be applied: %1").arg(error));
    return false;
  }
  if (!validate_mask_references(stack, currentState().mask_stack, &error)) {
    setStatus(QStringLiteral("Layer style envelope could not be applied: %1").arg(error));
    return false;
  }
  auto layer = layers.at(index).toMap();
  if (!style_target_supported(layer)) {
    setStatus(QStringLiteral("Layer styles are not supported for this layer type."));
    return false;
  }
  const bool linked = photo_layer_style_light::is_linked(layer);
  const auto effective_light_angle = linked
      ? document_layer_style_light_angle_ : light_angle;
  if (layer.value(QStringLiteral("layerStyleStack")).toString() == serialized &&
      (linked || layer_light_angle(layer) == light_angle)) return false;
  if (!preflight_stack_for_layer(layer, stack, effective_light_angle,
          QSize(sourceImageWidth(), sourceImageHeight()), &error)) {
    setStatus(preflight_rejection_message(QStringLiteral("Applying this style preset"), error));
    return false;
  }
  if (!store_stack(&layer, stack, &error)) {
    setStatus(QStringLiteral("Layer style envelope could not be applied: %1").arg(error));
    return false;
  }
  if (!linked)
    layer.insert(QStringLiteral("layerStyleGlobalLightAngle"), light_angle);
  layers[index] = layer;
  return applyPhotoLayerStackChange(layers, QStringLiteral("layer_style_paste"), false,
      QStringLiteral("Paste layer styles"));
}

bool EditorPreviewController::importPhotoLayerStyleFile(int index, const QString& path) {
  const auto file_path = local_path(path);
  QFile input(file_path);
  if (!input.open(QIODevice::ReadOnly) || input.size() <= 0 ||
      input.size() > codec::kMaximumEnvelopeBytes) {
    setStatus(QStringLiteral("Layer style file is unavailable or exceeds 4 MiB."));
    return false;
  }
  const auto bytes = input.readAll();
  if (bytes.size() != input.size()) {
    setStatus(QStringLiteral("The complete layer style file could not be read."));
    return false;
  }
  return applyPhotoLayerStyleEnvelope(index, QString::fromUtf8(bytes));
}

bool EditorPreviewController::exportPhotoLayerStyleFile(int index,
                                                        const QString& path) {
  const auto layers = layer_session_.layers();
  if (index < 0 || index >= layers.size()) return false;
  LayerStyleStack stack;
  QString error;
  if (!load_stack(layers.at(index).toMap(), &stack, &error)) {
    setStatus(QStringLiteral("Layer style preset could not be saved: %1").arg(error));
    return false;
  }
  if (stack_has_mask_references(stack)) {
    setStatus(QStringLiteral(
        "Reusable presets cannot include document-local style masks. Clear each style mask reference before saving the preset."));
    return false;
  }
  const auto envelope = photoLayerStyleEnvelope(index);
  if (envelope.isEmpty()) return false;
  const auto file_path = local_path(path);
  QSaveFile output(file_path);
  const auto bytes = envelope.toUtf8();
  if (!output.open(QIODevice::WriteOnly) || bytes.isEmpty() ||
      bytes.size() > codec::kMaximumEnvelopeBytes || output.write(bytes) != bytes.size() ||
      !output.commit()) return false;
  return true;
}

}  // namespace photo_editor::desktop
