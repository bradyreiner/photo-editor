#include "editor_preview_controller.h"
#include "edit_recipe_mask_schema.h"
#include "photo_render_utils.h"
#include "recipe_mask_sampling.h"
#include "mask_outline_tracing.h"
#include "literal_selection_mask.h"
#include "photo_layer_edit_access.h"
#include "mask_workflow_ownership.h"

#include <QFutureWatcher>
#include <QPainter>
#include <QScopedValueRollback>
#include <QUuid>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>

namespace photo_editor::desktop {
namespace {
int selection_edge_target(const QVariantList& masks, int active) {
  if (active < 0 || active >= masks.size()) return -1;
  const auto mask = masks[active].toMap();
  if (mask.value("kind").toString() != QStringLiteral("brush")) return active;
  const auto set = mask.value("maskSetId").toString();
  for (int i = 0; i < masks.size(); ++i) {
    const auto source = masks[i].toMap();
    if (source.value("maskSetId").toString() == set &&
        source.value("kind").toString() != QStringLiteral("brush")) return i;
  }
  for (int i = 0; i < masks.size(); ++i) {
    const auto source = masks[i].toMap();
    if (source.value("maskSetId").toString() == set &&
        (!source.value("brushStrokes").toList().isEmpty() ||
         !source.value("segmentationData").toByteArray().isEmpty())) return i;
  }
  return active;
}
double edge_feather(const QVariantMap& mask) {
  const auto kind = mask.value("kind").toString();
  if (kind == QStringLiteral("brush") || is_raster_mask_kind(kind))
    return mask.value("selectionFeather").toDouble() / 200.0;
  if (kind == "color" || kind == "luminance" || kind == "depth")
    return mask.value("rangeFeather", 0.1).toDouble();
  return mask.value("feather", 0.08).toDouble();
}
QString fresh_id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QVariantList masks_for_set(const QVariantList& stack, const QString& id) {
  QVariantList result;
  if (id.isEmpty()) return result;
  for (const auto& value : stack)
    if (value.toMap().value("maskSetId").toString() == id) result.append(value);
  return result;
}
QStringList layer_mask_values(const QVariantMap& layer, const char* list_key, const char* single_key = nullptr) {
  QStringList result;
  for (const auto& id : layer.value(QLatin1String(list_key)).toList())
    if (!id.toString().isEmpty() && !result.contains(id.toString())) result.append(id.toString());
  if (single_key) {
    const auto first = layer.value(QLatin1String(single_key)).toString();
    if (!first.isEmpty() && !result.contains(first)) result.prepend(first);
  }
  return result;
}
QStringList layer_mask_primary_ids(const QVariantMap& layer) {
  return layer_mask_values(layer, "selectionMaskSetIds", "selectionMaskSetId");
}
QStringList layer_mask_intersection_ids(const QVariantMap& layer) {
  return layer_mask_values(layer, "selectionIntersectionMaskSetIds");
}
QStringList layer_mask_ids(const QVariantMap& layer) {
  auto result = layer_mask_primary_ids(layer);
  for (const auto& id : layer_mask_intersection_ids(layer))
    if (!result.contains(id)) result.append(id);
  return result;
}
QStringList layer_mask_names(const QVariantMap& layer, const char* list_key, const char* single_key) {
  auto result = layer_mask_values(layer, list_key, single_key);
  return result;
}
QStringList layer_mask_names_for_ids(
    const QStringList& ids,
    const QStringList& old_ids,
    const QStringList& old_names,
    const QVariantList& stack) {
  QStringList names;
  names.reserve(ids.size());
  for (const auto& id : ids) {
    const auto masks = masks_for_set(stack, id);
    auto name = masks.isEmpty()
        ? QString()
        : masks.first().toMap().value(QStringLiteral("maskSetName")).toString();
    if (name.isEmpty()) {
      const auto old_index = old_ids.indexOf(id);
      if (old_index >= 0 && old_index < old_names.size()) name = old_names.at(old_index);
    }
    names.append(name.isEmpty() ? QStringLiteral("Mask") : name);
  }
  // Keep legacy name-only references in their original domain when another
  // explicit mask in that domain is added, removed, or transferred.
  for (auto index = 0; index < old_names.size(); ++index) {
    if ((index >= old_ids.size() || old_ids.at(index).isEmpty()) &&
        !old_names.at(index).isEmpty()) {
      names.append(old_names.at(index));
    }
  }
  return names;
}
void write_layer_masks(
    QVariantMap* layer,
    const QStringList& primary_ids,
    const QStringList& intersection_ids,
    const QVariantList& stack,
    bool clear_legacy_names = false) {
  const auto old_primary_ids = layer_mask_primary_ids(*layer);
  const auto old_primary_names = layer_mask_names(
      *layer, "selectionMaskSetNames", "selectionMaskSetName");
  const auto old_intersection_ids = layer_mask_intersection_ids(*layer);
  const auto old_intersection_names = layer_mask_names(
      *layer, "selectionIntersectionMaskSetNames", nullptr);
  const bool has_existing_mask = !old_primary_ids.isEmpty() || !old_primary_names.isEmpty() ||
      !old_intersection_ids.isEmpty() || !old_intersection_names.isEmpty() ||
      layer->contains(QStringLiteral("maskData")) ||
      layer->value(QStringLiteral("maskWidth")).toInt() > 0 ||
      layer->value(QStringLiteral("maskHeight")).toInt() > 0;
  const auto primary_names = clear_legacy_names
      ? layer_mask_names_for_ids(primary_ids, old_primary_ids, {}, stack)
      : layer_mask_names_for_ids(primary_ids, old_primary_ids, old_primary_names, stack);
  const auto intersection_names = clear_legacy_names
      ? layer_mask_names_for_ids(intersection_ids, old_intersection_ids, {}, stack)
      : layer_mask_names_for_ids(intersection_ids, old_intersection_ids, old_intersection_names, stack);
  const bool has_mask_references = !primary_ids.isEmpty() || !intersection_ids.isEmpty() ||
      !primary_names.isEmpty() || !intersection_names.isEmpty();
  const bool mask_enabled = !has_existing_mask ||
      layer->value(QStringLiteral("maskEnabled"), true).toBool();
  layer->insert("selectionScoped", has_mask_references);
  if (primary_ids.isEmpty() && primary_names.isEmpty()) {
    for (const auto* key : {"selectionMaskSetId", "selectionMaskSetIds", "selectionMaskSetName",
                            "selectionMaskSetNames"}) layer->remove(key);
  } else {
    QVariantList values;
    for (const auto& id : primary_ids) values.append(id);
    if (primary_ids.isEmpty()) layer->remove("selectionMaskSetIds");
    else layer->insert("selectionMaskSetIds", values);
    if (primary_ids.isEmpty()) layer->remove("selectionMaskSetId");
    else layer->insert("selectionMaskSetId", primary_ids.first());
    QVariantList name_values;
    for (const auto& name : primary_names) name_values.append(name);
    if (primary_names.isEmpty()) layer->remove("selectionMaskSetNames");
    else layer->insert("selectionMaskSetNames", name_values);
    if (primary_names.isEmpty()) layer->remove("selectionMaskSetName");
    else layer->insert("selectionMaskSetName", primary_names.first());
  }
  if (intersection_ids.isEmpty() && intersection_names.isEmpty()) {
    layer->remove("selectionIntersectionMaskSetIds");
    layer->remove("selectionIntersectionMaskSetNames");
  } else {
    QVariantList values;
    for (const auto& id : intersection_ids) values.append(id);
    if (intersection_ids.isEmpty()) layer->remove("selectionIntersectionMaskSetIds");
    else layer->insert("selectionIntersectionMaskSetIds", values);
    QVariantList name_values;
    for (const auto& name : intersection_names) name_values.append(name);
    if (intersection_names.isEmpty()) layer->remove("selectionIntersectionMaskSetNames");
    else layer->insert("selectionIntersectionMaskSetNames", name_values);
  }
  if (has_mask_references) {
    auto unique_ids = primary_ids;
    for (const auto& id : intersection_ids)
      if (!unique_ids.contains(id)) unique_ids.append(id);
    const auto primary_name_only_count = std::max(
        0, static_cast<int>(primary_names.size()) - static_cast<int>(primary_ids.size()));
    const auto intersection_name_only_count = std::max(
        0, static_cast<int>(intersection_names.size()) - static_cast<int>(intersection_ids.size()));
    layer->insert("selectionMaskSetCount", static_cast<int>(unique_ids.size()) +
        primary_name_only_count + intersection_name_only_count);
    layer->insert("maskEnabled", mask_enabled);
  } else {
    layer->remove("selectionMaskSetCount");
  }
}
QVariantList clone_set(const QVariantList& sources, const QString& id, const QString& name) {
  QVariantList result;
  QHash<QString, QString> copied_ids;
  for (const auto& value : sources)
    copied_ids.insert(value.toMap().value("id").toString(), fresh_id());
  for (const auto& value : sources) {
    auto mask = value.toMap();
    mask.insert("id", copied_ids.value(mask.value("id").toString()));
    mask.insert("maskSetId", id); mask.insert("maskSetName", name);
    for (const auto* key : {"parentId", "outputId"}) {
      const auto original = mask.value(QLatin1String(key)).toString();
      if (copied_ids.contains(original)) mask.insert(QLatin1String(key), copied_ids.value(original));
    }
    mask.insert("locked", false); mask.insert("solo", false);
    for (const auto* key : {"exposure", "contrast", "highlights", "shadows", "whites", "blacks", "temperature", "tint", "vibrance", "saturation", "hueShift", "saturationShift", "colorizeHue", "colorizeStrength"})
      mask.insert(QLatin1String(key), 0.0);
    result.append(mask);
  }
  return result;
}
QVariantMap baked_selection(const QString& path, EditState state, const QVariantList& masks,
                            const QVariantMap& transform = {}, int max_edge = 0) {
  if (masks.isEmpty()) return {};
  // Selection authoring uses the original source grid, independent of the current crop.
  state.crop_rect_x = 0; state.crop_rect_y = 0; state.crop_rect_width = 0; state.crop_rect_height = 0; state.crop_zoom = 1;
  QImage source;
  photo_editor::pipeline::DecodedImageMetadata metadata;
  if (!decode_source_image(path, &source, max_edge, state, &metadata)) return {};
  const auto sampling = prepare_recipe_mask_sampling(source, state, metadata, masks.first().toMap(), QRect(QPoint(), source.size()));
  std::vector<photo_editor::pipeline::LocalMask> compiled;
  for (const auto& value : masks) compiled.push_back(sampling.compile_mask(value.toMap()));
  const auto alpha = sampling.evaluate(compiled);
  if (!alpha.valid) return {};
  QImage image(reinterpret_cast<const uchar*>(alpha.alpha_mask.constData()), alpha.size.width(), alpha.size.height(), alpha.size.width(), QImage::Format_Grayscale8);
  image = image.copy();
  if (!transform.isEmpty()) {
    QImage target(image.size(), QImage::Format_ARGB32_Premultiplied); target.fill(Qt::black);
    QPainter painter(&target); painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QPointF center(image.width() * 0.5, image.height() * 0.5);
    painter.translate(center + QPointF(transform.value("x", 0.0).toDouble() * image.width(), transform.value("y", 0.0).toDouble() * image.height()));
    painter.rotate(transform.value("rotation", 0.0).toDouble());
    painter.scale(transform.value("scaleX", 1.0).toDouble(), transform.value("scaleY", 1.0).toDouble());
    painter.translate(-center); painter.drawImage(QPoint(), image); painter.end();
    image = target.convertToFormat(QImage::Format_Grayscale8);
  }
  QByteArray bytes(image.width() * image.height(), '\0');
  for (int y = 0; y < image.height(); ++y)
    std::copy_n(image.constScanLine(y), image.width(), reinterpret_cast<uchar*>(bytes.data()) + y * image.width());
  return literal_selection_mask(bytes, image.size());
}
}

QVariantMap EditorPreviewController::selectionEdgeControls() const {
  const auto index = selection_edge_target(mask_session_.stack(), activeMaskIndex());
  if (index < 0) return {};
  const auto mask = mask_session_.stack()[index].toMap();
  const auto kind = mask.value("kind").toString();
  const auto short_side = static_cast<double>(std::min(sourceImageWidth(), sourceImageHeight()));
  double pixel_scale = 0.0;
  QString pixel_description;
  if (kind == "brush" || is_raster_mask_kind(kind)) {
    pixel_scale = short_side * 0.05;
    pixel_description = QStringLiteral("Spatial feather in source pixels (Gaussian sigma).");
  } else if (kind == "rectangle" || kind == "ellipse" || kind == "radial") {
    pixel_scale = std::min(mask.value("radiusX", 0.25).toDouble() * sourceImageWidth(),
        mask.value("radiusY", 0.25).toDouble() * sourceImageHeight());
    pixel_description = QStringLiteral("Inward feather width along the selection's shorter axis, in source pixels.");
  } else if (kind == "polygon" || kind == "lasso") {
    pixel_scale = short_side;
    pixel_description = QStringLiteral("Approximate edge width in source pixels, measured against the shorter image axis.");
  }
  const auto feather_max = kind == "polygon" ? 0.2 : kind == "lasso" ? 0.25 : 0.5;
  return {{"name", mask.value("name")}, {"id", mask.value("id")}, {"locked", mask.value("locked")}, {"feather", edge_feather(mask)},
      {"featherPixelScale", pixel_scale}, {"featherPixels", edge_feather(mask) * pixel_scale},
      {"featherPixelsMax", feather_max * pixel_scale}, {"featherPixelDescription", pixel_description},
      {"density", mask.value("maskSetDensity", 1.0)}, {"opacity", mask.value("maskSetOpacity", 1.0)},
      {"inverted", mask.value("maskSetSelectionInvert", false)},
      {"shift", mask.value("selectionExpand", 0.0)}, {"smooth", mask.value("selectionSmooth", 0.0)},
      {"fineEdge", mask.value("selectionFineEdge", 0.0)}, {"contrast", mask.value("selectionEdgeContrast", 0.0)}};
}

bool EditorPreviewController::setSelectionEdgeAdjustment(const QString& key, double value) {
  if (key == QStringLiteral("featherPixels")) {
    const auto scale = selectionEdgeControls().value("featherPixelScale").toDouble();
    return scale > 0.0 && setSelectionEdgeAdjustment(QStringLiteral("feather"), value / scale);
  }
  const auto customer_edit_scope = customer_edit_access_.enter();
  if (!customer_edit_scope || !std::isfinite(value)) return false;
  auto masks = mask_session_.stack();
  const auto index = selection_edge_target(masks, activeMaskIndex());
  if (index < 0) return false;
  auto mask = masks[index].toMap();
  if (mask.value("toolRole").toString() != "selection") return false;
  const auto kind = mask.value("kind").toString();
  if (key == "density" || key == "opacity" || key == "invert") {
    const auto parameter = key == "density" ? QStringLiteral("maskSetDensity")
        : key == "opacity" ? QStringLiteral("maskSetOpacity") : QStringLiteral("maskSetSelectionInvert");
    const QVariant strength = key == "invert" ? QVariant(value > 0.5) : QVariant(std::clamp(value, 0.0, 1.0));
    const auto set_id = mask.value("maskSetId").toString();
    for (auto& item : masks) {
      auto source = item.toMap();
      if (source.value("maskSetId").toString() != set_id) continue;
      source.insert(parameter, strength);
      item = source;
    }
    return applyMaskStackChange(masks, activeMaskIndex(), true, QStringLiteral("selection_mask_strength"), false);
  }
  if (key == "feather") {
    if (kind == "brush" || is_raster_mask_kind(kind))
      mask.insert("selectionFeather", std::clamp(value * 200.0, 0.0, 100.0));
    else if (kind == "color" || kind == "luminance" || kind == "depth")
      mask.insert("rangeFeather", std::clamp(value, 0.0, 0.5));
    else mask.insert("feather", std::clamp(value, 0.0, kind == "polygon" ? 0.2 : kind == "lasso" ? 0.25 : 0.5));
  } else if (key == "reset") {
    for (const auto* parameter : {"selectionExpand", "selectionSmooth", "selectionFineEdge",
             "selectionEdgeContrast", "selectionFeather", "selectionBlur", "selectionGrow", "selectionShrink"})
      mask.insert(QLatin1String(parameter), 0.0);
    if (kind != "brush" && !is_raster_mask_kind(kind))
      mask.insert(kind == "color" || kind == "luminance" || kind == "depth" ? "rangeFeather" : "feather", 0.0);
  } else if (key == "shift") mask.insert("selectionExpand", std::clamp(value, -100.0, 100.0));
  else if (key == "smooth") mask.insert("selectionSmooth", std::clamp(value, 0.0, 100.0));
  else if (key == "fineEdge") mask.insert("selectionFineEdge", std::clamp(value, 0.0, 100.0));
  else if (key == "contrast") mask.insert("selectionEdgeContrast", std::clamp(value, 0.0, 100.0));
  else return false;
  masks[index] = mask;
  return applyMaskStackChange(masks, activeMaskIndex(), true, QStringLiteral("selection_edge_refine"), false);
}

QVariantList EditorPreviewController::workingSelectionOutlinePaths() const {
  if (!hasWorkingSelection()) return {};
  const_cast<EditorPreviewController*>(this)->requestWorkingSelectionOutline();
  return working_outline_key_.startsWith(QStringLiteral("%1|%2|").arg(asset_id_, workingSelectionSetId()))
      ? working_outline_paths_ : QVariantList{};
}
void EditorPreviewController::requestWorkingSelectionOutline() {
  const auto id = workingSelectionSetId();
  if (id.isEmpty()) return;
  const auto key = workingSelectionOutlineCacheKey();
  if (key == working_outline_key_ || working_outline_running_) return;
  working_outline_running_ = true;
  const auto path = file_path_; const auto state = currentState();
  const auto sources = workingSelectionMasks();
  auto* watcher = new QFutureWatcher<QVariantList>(this);
  connect(watcher, &QFutureWatcher<QVariantList>::finished, this, [this, watcher, key] {
    const auto paths = watcher->result(); watcher->deleteLater(); working_outline_running_ = false;
    if (key == workingSelectionOutlineCacheKey()) {
      working_outline_paths_ = paths; working_outline_key_ = key;
    } else if (hasWorkingSelection()) {
      // Finish only the newest request when coverage or source sampling changed.
      requestWorkingSelectionOutline();
    }
    emit maskOverlayChanged();
  });
  watcher->setFuture(QtConcurrent::run([path, state, sources] {
    const auto mask = baked_selection(path, state, sources, {}, 768);
    return mask.isEmpty() ? QVariantList{} : traced_outline_paths_for_alpha_mask_with_edge_padding(
        mask.value("segmentationData").toByteArray(), mask.value("segmentationWidth").toInt(), mask.value("segmentationHeight").toInt());
  }));
}

QString EditorPreviewController::workingSelectionSetId() const {
  const auto id = selection_state_.value("currentSetId").toString();
  if (!id.isEmpty() && mask_session_.indexForMaskSet(id) >= 0) return id;
  if (selection_state_.value("initialized").toBool()) return {};
  const auto active = activeMaskDefinition();
  return active.value("toolRole").toString() == QStringLiteral("selection") ? active.value("maskSetId").toString() : QString();
}
QVariantList EditorPreviewController::savedSelections() const {
  QVariantList result;
  for (const auto& id : selection_state_.value("savedSetIds").toStringList()) {
    const auto masks = masks_for_set(mask_session_.stack(), id);
    if (!masks.isEmpty()) result.append(QVariantMap{{"id", id}, {"name", masks.first().toMap().value("maskSetName")}});
  }
  return result;
}
bool EditorPreviewController::hasWorkingSelection() const { return !workingSelectionSetId().isEmpty(); }
QVariantList EditorPreviewController::workingSelectionMasks() const { return masks_for_set(mask_session_.stack(), workingSelectionSetId()); }
bool EditorPreviewController::workingSelectionInverted() const {
  const auto masks = workingSelectionMasks();
  return !masks.isEmpty() && masks.first().toMap().value("maskSetSelectionInvert").toBool();
}
bool EditorPreviewController::canReselect() const {
  return mask_session_.indexForMaskSet(selection_state_.value("previousSetId").toString()) >= 0;
}
bool EditorPreviewController::editingLayerMask() const {
  if (mask_edit_layer_id_.isEmpty()) return false;
  const auto set = activeMaskDefinition().value("maskSetId").toString();
  if (set != mask_edit_set_id_) return false;
  for (const auto& value : layer_session_.layers()) {
    const auto layer = value.toMap();
    if (layer.value("id").toString() == mask_edit_layer_id_)
      return layer_references_editable_mask_set(layer, set);
  }
  return false;
}
QString EditorPreviewController::maskEditLayerId() const { return editingLayerMask() ? mask_edit_layer_id_ : QString(); }

bool EditorPreviewController::activateWorkingSelection() {
  const auto id = workingSelectionSetId();
  const bool was_mask = !mask_edit_layer_id_.isEmpty();
  if (was_mask && brush_stroke_active_) endActiveBrushStroke();
  mask_edit_layer_id_.clear();
  mask_edit_set_id_.clear();
  setActiveMaskIndex(mask_session_.indexForMaskSet(id));
  if (was_mask) emit maskStateChanged();
  return !id.isEmpty();
}
void EditorPreviewController::finishLayerMaskEditing() { static_cast<void>(activateWorkingSelection()); }
bool EditorPreviewController::deselectWorkingSelection() {
  const auto access = customer_edit_access_.enter(); if (!access || !hasWorkingSelection()) return false;
  rememberStateForUndo(QStringLiteral("Deselect"));
  selection_state_.insert("previousSetId", workingSelectionSetId());
  selection_state_.insert("currentSetId", QString()); selection_state_.insert("initialized", true);
  if (!editingLayerMask()) setActiveMaskIndex(-1);
  emit maskStateChanged(); persistCurrentState(); return true;
}
bool EditorPreviewController::reselectWorkingSelection() {
  const auto access = customer_edit_access_.enter(); if (!access || !canReselect()) return false;
  rememberStateForUndo(QStringLiteral("Reselect"));
  selection_state_.insert("currentSetId", selection_state_.value("previousSetId"));
  selection_state_.insert("initialized", true);
  if (!editingLayerMask()) activateWorkingSelection();
  emit maskStateChanged(); persistCurrentState(); return true;
}
bool EditorPreviewController::saveWorkingSelection(const QString& name) {
  const auto access = customer_edit_access_.enter();
  if (!access || !hasWorkingSelection() || name.simplified().isEmpty()) return false;
  const auto id = fresh_id();
  auto stack = mask_session_.stack();
  stack.append(clone_set(workingSelectionMasks(), id, name.simplified()));
  beginCoalescedEdit();
  rememberStateForUndo(QStringLiteral("Save selection"));
  auto saved = selection_state_.value("savedSetIds").toStringList(); saved.append(id);
  selection_state_.insert("savedSetIds", saved);
  const QScopedValueRollback<bool> suspend(selection_identity_suspended_, true);
  const bool changed = applyMaskStackChange(stack, activeMaskIndex(), false, QStringLiteral("save_selection"), false);
  endCoalescedEdit(); emit maskStateChanged(); persistCurrentState(); return changed;
}

bool EditorPreviewController::selectLayerMask(int index, const QString& requested_id) {
  if (index < 0 || index >= layer_session_.layers().size()) return false;
  const auto layer = layer_session_.layers()[index].toMap();
  const auto ids = layer_mask_ids(layer);
  const auto id = requested_id.isEmpty() ? ids.value(0) : requested_id;
  if (!layer_references_editable_mask_set(layer, id) || mask_session_.indexForMaskSet(id) < 0)
    return false;
  // Freeze legacy selection identity before changing the inspected mask.
  const auto working = workingSelectionSetId();
  selection_state_.insert("currentSetId", working); selection_state_.insert("initialized", true);
  if (brush_stroke_active_) endActiveBrushStroke();
  mask_edit_layer_id_ = layer.value("id").toString();
  mask_edit_set_id_ = id;
  mask_edit_follows_layer_transfer_ = ids.contains(id);
  mask_edit_asset_id_ = asset_id_;
  setActiveMaskIndex(mask_session_.indexForMaskSet(id)); emit maskStateChanged(); return true;
}
int EditorPreviewController::createAdjustmentLayerFromWorkingSelection(const QString& name) {
  const auto access = customer_edit_access_.enter();
  if (!access || !hasWorkingSelection() || selectionWorkBusy() || selection_edit_session_active_ ||
      brush_stroke_active_) return -1;
  // Snapshot the working selection into a single layer. Subsequent selection
  // gestures must not change this layer's coverage, and Undo removes both pieces.
  beginCoalescedEdit();
  const bool changed = addAdjustmentLayer(name.simplified().isEmpty()
      ? QStringLiteral("Selected area") : name.simplified()) && applyWorkingSelectionToLayer(0, false);
  if (!changed) static_cast<void>(cancelCoalescedEdit());
  endCoalescedEdit();
  if (!changed) return -1;
  setStatus(QStringLiteral("Created an adjustment layer for the selected area."));
  return 0;
}

bool EditorPreviewController::applyWorkingSelectionToLayer(int index, bool hide) {
  const auto access = customer_edit_access_.enter();
  if (!access || !hasWorkingSelection() || index < 0 || index >= layer_session_.layers().size()) return false;
  auto layers = layer_session_.layers(); auto layer = layers[index].toMap();
  if (layer_edit_access::is_effectively_locked(layers, index)) {
    setStatus(QStringLiteral("Unlock the layer and its groups before adding a mask."));
    return false;
  }
  const auto id = fresh_id();
  const auto name = workingSelectionMasks().first().toMap().value("maskSetName").toString();
  auto additions = clone_set(workingSelectionMasks(), id, name);
  if (hide) for (auto& value : additions) { auto mask = value.toMap(); mask.insert("maskSetSelectionInvert", !mask.value("maskSetSelectionInvert").toBool()); value = mask; }
  auto stack = mask_session_.stack(); stack.append(additions);
  auto primary_ids = layer_mask_primary_ids(layer);
  primary_ids.append(id);
  write_layer_masks(&layer, primary_ids, layer_mask_intersection_ids(layer), stack);
  layers[index] = layer;
  beginCoalescedEdit();
  const QScopedValueRollback<bool> suspend(selection_identity_suspended_, true);
  const bool masks_applied = applyMaskStackChange(stack, activeMaskIndex(), false, QStringLiteral("attach_layer_mask"), false);
  const bool changed = masks_applied && applyPhotoLayerStackChange(layers, QStringLiteral("attach_layer_mask"), false, QStringLiteral("Add layer mask"));
  endCoalescedEdit(); return changed;
}
bool EditorPreviewController::invertLayerMask(int index, const QString& requested_id) {
  const auto access = customer_edit_access_.enter();
  if (!access || index < 0 || index >= layer_session_.layers().size()) return false;
  const auto layers = layer_session_.layers();
  const auto layer = layers[index].toMap();
  const auto ids = layer_mask_ids(layer);
  const auto id = requested_id.isEmpty() ? ids.value(0) : requested_id;
  if (layer_edit_access::is_effectively_locked(layers, index) || !ids.contains(id)) return false;
  auto stack = mask_session_.stack(); const auto sources = masks_for_set(stack, id);
  if (sources.isEmpty()) return false;
  const bool inverted = !sources.first().toMap().value("maskSetSelectionInvert").toBool();
  for (auto& value : stack) { auto mask = value.toMap(); if (mask.value("maskSetId").toString() != id) continue;
    if (mask.value("locked").toBool()) return false;
    mask.insert("maskSetSelectionInvert", inverted); value = mask;
  }
  return applyMaskStackChange(stack, activeMaskIndex(), true, QStringLiteral("invert_layer_mask"), false);
}

bool EditorPreviewController::removeLayerMask(int index, const QString& id) {
  const auto access = customer_edit_access_.enter();
  if (!access || index < 0 || index >= layer_session_.layers().size()) return false;
  auto layers = layer_session_.layers(); auto layer = layers[index].toMap();
  if (layer_edit_access::is_effectively_locked(layers, index)) return false;
  auto primary_ids = layer_mask_primary_ids(layer);
  auto intersection_ids = layer_mask_intersection_ids(layer);
  auto ids = layer_mask_ids(layer);
  const auto masks = mask_session_.stack();
  for (const auto& attached_id : ids) {
    if (!id.isEmpty() && attached_id != id) continue;
    for (const auto& source : masks_for_set(masks, attached_id))
      if (source.toMap().value("locked").toBool()) return false;
  }
  if (id.isEmpty()) {
    primary_ids.clear();
    intersection_ids.clear();
  } else {
    if (!ids.removeOne(id)) return false;
    primary_ids.removeAll(id);
    intersection_ids.removeAll(id);
  }
  write_layer_masks(&layer, primary_ids, intersection_ids, masks, id.isEmpty());
  layers[index] = layer;
  return applyPhotoLayerStackChange(layers, QStringLiteral("remove_layer_mask"), false, QStringLiteral("Remove layer mask"));
}
bool EditorPreviewController::transferLayerMask(const QString& source_id, const QString& set_id, const QString& target_id, bool copy) {
  const auto access = customer_edit_access_.enter();
  if (!access || source_id == target_id) return false;
  const auto sources = masks_for_set(mask_session_.stack(), set_id);
  if (sources.isEmpty() || std::any_of(sources.cbegin(), sources.cend(), [](const QVariant& value) {
        return value.toMap().value("locked").toBool();
      })) return false;
  auto layers = layer_session_.layers(); int source = -1, target = -1;
  for (int i = 0; i < layers.size(); ++i) {
    const auto layer = layers[i].toMap();
    if (layer.value("id").toString() == source_id) source = i;
    if (layer.value("id").toString() == target_id) target = i;
  }
  if (target < 0 || (!source_id.isEmpty() && source < 0)) return false;
  if (layer_edit_access::is_effectively_locked(layers, target)) return false;
  auto destination = layers[target].toMap();
  auto origin = source >= 0 ? layers[source].toMap() : QVariantMap{};
  if (source >= 0 && (!layer_mask_ids(origin).contains(set_id) ||
      layer_edit_access::is_effectively_locked(layers, source))) return false;
  auto stack = mask_session_.stack(); auto destination_set = set_id;
  // A free selection is copied into a layer mask; it remains an independent selection.
  if (copy || source < 0) {
    destination_set = fresh_id(); const auto masks = masks_for_set(stack, set_id);
    stack.append(clone_set(masks, destination_set, masks.first().toMap().value("maskSetName").toString()));
  }
  auto target_primary_ids = layer_mask_primary_ids(destination);
  auto target_intersection_ids = layer_mask_intersection_ids(destination);
  if (layer_mask_ids(destination).contains(destination_set)) return false;
  const bool transfer_intersection = source >= 0 &&
      !layer_mask_primary_ids(origin).contains(set_id) &&
      layer_mask_intersection_ids(origin).contains(set_id);
  if (transfer_intersection) target_intersection_ids.append(destination_set);
  else target_primary_ids.append(destination_set);
  write_layer_masks(&destination, target_primary_ids, target_intersection_ids, stack);
  layers[target] = destination;
  if (source >= 0 && !copy) {
    auto origin_primary_ids = layer_mask_primary_ids(origin);
    auto origin_intersection_ids = layer_mask_intersection_ids(origin);
    origin_primary_ids.removeAll(set_id);
    origin_intersection_ids.removeAll(set_id);
    write_layer_masks(&origin, origin_primary_ids, origin_intersection_ids, stack);
    layers[source] = origin;
  }
  beginCoalescedEdit();
  const QScopedValueRollback<bool> suspend(selection_identity_suspended_, true);
  if (stack != mask_session_.stack()) applyMaskStackChange(stack, activeMaskIndex(), false, QStringLiteral("copy_layer_mask"), false);
  const bool changed = applyPhotoLayerStackChange(layers, QStringLiteral("transfer_layer_mask"), false, copy ? QStringLiteral("Copy layer mask") : QStringLiteral("Move layer mask"));
  endCoalescedEdit();
  if (changed && mask_edit_layer_id_ == source_id && !copy) mask_edit_layer_id_ = target_id;
  emit maskStateChanged(); return changed;
}
bool EditorPreviewController::setPhotoLayerAlphaLocked(int index, bool locked) {
  const auto access = customer_edit_access_.enter(); if (!access || index < 0 || index >= layer_session_.layers().size()) return false;
  auto layers = layer_session_.layers(); auto layer = layers[index].toMap();
  if (layer.value("locked").toBool()) return false;
  layer.insert("alphaLocked", locked); layers[index] = layer;
  return applyPhotoLayerStackChange(layers, QStringLiteral("alpha_lock"), false, QStringLiteral("Lock transparency"));
}

bool EditorPreviewController::loadMaskAsSelection(const QString& id, const QString& operation) {
  const auto access = customer_edit_access_.enter();
  const auto incoming = masks_for_set(mask_session_.stack(), id);
  if (!access || incoming.isEmpty() || selection_work_running_) return false;
  if (id == workingSelectionSetId()) return activateWorkingSelection();
  if (!hasWorkingSelection()) {
    beginCoalescedEdit(); rememberStateForUndo(QStringLiteral("Load selection"));
    const auto new_id = fresh_id(); auto stack = mask_session_.stack();
    stack.append(clone_set(incoming, new_id, incoming.first().toMap().value("maskSetName").toString()));
    selection_state_.insert("currentSetId", new_id); selection_state_.insert("initialized", true);
    const QScopedValueRollback<bool> suspend(selection_identity_suspended_, true);
    const bool changed = applyMaskStackChange(stack, static_cast<int>(stack.size()) - 1, false, QStringLiteral("load_selection"), false);
    endCoalescedEdit(); mask_edit_layer_id_.clear(); mask_edit_set_id_.clear(); emit maskStateChanged(); return changed;
  }
  if (operation != "add" && operation != "subtract" && operation != "intersect") return false;
  return processWorkingSelection(incoming, {}, operation);
}

bool EditorPreviewController::transformWorkingSelection(const QVariantMap& transform) {
  const auto access = customer_edit_access_.enter();
  if (!access || !hasWorkingSelection() || selection_work_running_) return false;
  for (const auto* key : {"x", "y", "rotation", "scaleX", "scaleY"}) {
    const double value = transform.value(key, key[0] == 's' ? 1.0 : 0.0).toDouble();
    if (!std::isfinite(value) || ((key[0] == 's') && (value < 0.01 || value > 100.0))) return false;
  }
  return processWorkingSelection(workingSelectionMasks(), transform, QStringLiteral("transform"));
}
bool EditorPreviewController::processWorkingSelection(const QVariantList& masks, const QVariantMap& transform, const QString& operation, bool notify_path) {
  const auto asset = asset_id_; const auto revision = mask_session_.workRevision(); const auto working = workingSelectionSetId();
  if (working.isEmpty()) return false;
  for (const auto& value : workingSelectionMasks()) {
    const auto source = value.toMap();
    if (source.value(QStringLiteral("locked")).toBool() ||
        selection_has_locked_consumer(source, layer_session_.layers())) {
      setStatus(QStringLiteral("Unlock the selection and its owning layers before changing coverage."));
      return false;
    }
  }
  const auto state = currentState(); const auto path = file_path_;
  selection_work_running_ = true; emit maskStateChanged(); setStatus(QStringLiteral("Preparing selection…"));
  auto* watcher = new QFutureWatcher<QVariantMap>(this);
  connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher, asset, revision, working, operation, notify_path] {
    auto mask = watcher->result(); watcher->deleteLater(); selection_work_running_ = false;
    emit maskStateChanged();
    if (asset != asset_id_ || revision != mask_session_.workRevision() || working != workingSelectionSetId()) {
      setStatus(QStringLiteral("Selection changed while processing. Current edits were preserved."));
      if (notify_path) { professional_path_selection_pending_ = false; emit professionalPathSelectionFinished(false); }
      return;
    }
    if (mask.isEmpty()) { setStatus(QStringLiteral("Could not prepare this selection."));
      if (notify_path) { professional_path_selection_pending_ = false; emit professionalPathSelectionFinished(false); }
      return; }
    auto stack = mask_session_.stack();
    const auto reference = workingSelectionMasks().first().toMap();
    // The worker returns the complete combined coverage, including the old selection.
    // Keeping its old sources would union them back in and undo subtraction.
    stack.removeIf([&working](const QVariant& value) { return value.toMap().value("maskSetId").toString() == working; });
    mask.insert("id", fresh_id()); mask.insert("maskSetId", working); mask.insert("maskSetName", reference.value("maskSetName"));
    mask.insert("operation", QStringLiteral("add"));
    stack.append(mask);
    mask_edit_layer_id_.clear();
    mask_edit_set_id_.clear();
    const bool accepted = applyMaskStackChange(stack, static_cast<int>(stack.size()) - 1, true, operation == QStringLiteral("transform") ? QStringLiteral("transform_selection") : QStringLiteral("load_selection"), false);
    if (notify_path) { professional_path_selection_pending_ = false; emit professionalPathSelectionFinished(accepted); }
  });
  watcher->setFuture(QtConcurrent::run([path, state, masks, transform, working, operation] {
    auto incoming = baked_selection(path, state, masks, transform);
    if (operation == QStringLiteral("transform") || incoming.isEmpty()) return incoming;
    const auto existing = baked_selection(path, state, masks_for_set(state.mask_stack, working));
    auto a = existing.value("segmentationData").toByteArray();
    const auto b = incoming.value("segmentationData").toByteArray();
    if (a.size() != b.size() || a.isEmpty()) return QVariantMap{};
    for (qsizetype i = 0; i < a.size(); ++i) {
      const int left = static_cast<unsigned char>(a[i]), right = static_cast<unsigned char>(b[i]);
      const int alpha = operation == QStringLiteral("subtract") ? std::max(0, left - right)
          : operation == QStringLiteral("intersect") ? qRound(left * right / 255.0)
          : qRound((left / 255.0 + right / 255.0 - left * right / 65025.0) * 255.0);
      a[i] = static_cast<char>(std::clamp(alpha, 0, 255));
    }
    incoming.insert("segmentationData", a);
    return incoming;
  }));
  return true;
}
}  // namespace photo_editor::desktop
