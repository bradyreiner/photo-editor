#include "editor_preview_controller.h"
#include "photo_layer_blend_if.h"
#include "photo_smart_object.h"
#include "photo_vector_path.h"
#include "photo_vector_mask.h"
#include "photo_vector_mask_rebase.h"
#include "photo_vector_mask_coordinates.h"
#include "photo_paint_coordinates.h"
#include "photo_render_utils.h"
#include "literal_selection_mask.h"

#include <QUuid>
#include <QBuffer>
#include <QFutureWatcher>
#include <QImageReader>
#include <QPainter>
#include <QSet>
#include <QUrl>
#include <QtConcurrent>

namespace photo_editor::desktop {
namespace {
bool professional_layer_locked(const QVariantList& layers, int index) {
  QSet<QString> visited;
  for (int depth = 0; depth < 64 && index >= 0 && index < layers.size(); ++depth) {
    const auto layer = layers[index].toMap();
    const auto id = layer.value(QStringLiteral("id")).toString();
    if (layer.value(QStringLiteral("locked")).toBool() || visited.contains(id)) return true;
    visited.insert(id);
    const auto parent = layer.value(QStringLiteral("parentId")).toString();
    if (parent.isEmpty()) return false;
    index = -1;
    for (int candidate = 0; candidate < layers.size(); ++candidate)
      if (layers[candidate].toMap().value(QStringLiteral("id")).toString() == parent) { index = candidate; break; }
  }
  return true;
}
}

QVariantMap EditorPreviewController::photoLayerBlendIf(int index) const {
  if (index < 0 || index >= layer_session_.layers().size()) return {};
  const auto settings = blend_if_from_variant(layer_session_.layers()[index].toMap().value(QStringLiteral("blendIf")).toMap());
  return settings ? blend_if_to_variant(*settings) : QVariantMap{};
}

bool EditorPreviewController::setPhotoLayerBlendIf(int index, const QVariantMap& settings) {
  const auto access = customer_edit_access_.enter();
  if (!access || index < 0 || index >= layer_session_.layers().size()) return false;
  QString error;
  const auto parsed = blend_if_from_variant(settings, &error);
  if (!parsed) { setStatus(error); return false; }
  auto layers = layer_session_.layers(); auto layer = layers[index].toMap();
  if (professional_layer_locked(layers, index)) { setStatus(QStringLiteral("Unlock the layer and its parent groups before editing.")); return false; }
  const auto normalized = blend_if_to_variant(*parsed);
  if (normalized == photoLayerBlendIf(index)) return false;
  layer.insert(QStringLiteral("blendIf"), normalized); layers[index] = layer;
  return applyPhotoLayerStackChange(layers, QStringLiteral("layer_blend_if"), false, QStringLiteral("Change tonal blending"));
}

bool EditorPreviewController::convertPhotoLayerToSmartObject(int index, bool linked) {
  return preparePhotoSmartObjectContents(index, {}, linked, false);
}

bool EditorPreviewController::replacePhotoSmartObjectContents(int index, const QString& path) {
  if (path.trimmed().isEmpty() || index < 0 || index >= layer_session_.layers().size()) return false;
  const auto content = layer_session_.layers()[index].toMap().value(QStringLiteral("smartObject")).toMap();
  if (content.isEmpty()) return false;
  const QUrl url(path);
  return preparePhotoSmartObjectContents(index, url.isLocalFile() ? url.toLocalFile() : path,
      content.value(QStringLiteral("policy")).toString() == QStringLiteral("linked"), true);
}

bool EditorPreviewController::preparePhotoSmartObjectContents(int index, const QString& path, bool linked, bool replace) {
  const auto access = customer_edit_access_.enter();
  if (!access || professional_content_work_running_ || index < 0 || index >= layer_session_.layers().size()) return false;
  if (transient_document_session_ && linked) {
    setStatus(tr("Component images are embedded. Keep linked sources in the parent document, or use Embed source here."));
    return false;
  }
  const auto captured_layers = layer_session_.layers(); const auto layer = captured_layers[index].toMap();
  if (professional_layer_locked(captured_layers, index) || layer.value(QStringLiteral("baseImageEdit")).toBool()) return false;
  const auto asset = asset_id_; const auto id = layer.value(QStringLiteral("id")).toString();
  professional_content_work_running_ = true; emit controlsChanged(); setStatus(QStringLiteral("Preparing Smart Object content…"));
  auto* watcher = new QFutureWatcher<QVariantMap>(this);
  connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher, asset, captured_layers, replace] {
    const auto result = watcher->result(); watcher->deleteLater(); professional_content_work_running_ = false; emit controlsChanged();
    if (asset != asset_id_ || captured_layers != layer_session_.layers()) {
      setStatus(QStringLiteral("The document changed while preparing content. Current edits were preserved.")); return;
    }
    QString error = result.value(QStringLiteral("error")).toString();
    if (!error.isEmpty()) { setStatus(error); return; }
    const auto layers = result.value(QStringLiteral("layers")).toList();
    applyPhotoLayerStackChange(layers, QStringLiteral("smart_object_content"), false,
        replace ? QStringLiteral("Replace shared contents") : QStringLiteral("Create Smart Object"));
  });
  watcher->setFuture(QtConcurrent::run([layer, captured_layers, id, path, linked, replace] {
    auto source = layer; QString error;
    if (replace) {
      source = {{QStringLiteral("kind"), QStringLiteral("source")}, {QStringLiteral("sourcePath"), path}};
      const auto png_payload = layer.value(QStringLiteral("smartObject")).toMap().value(QStringLiteral("payloadKind")).toString() == QStringLiteral("layer-png");
      if (png_payload) {
        QImageReader reader(path); const auto size = reader.size();
        if (!size.isValid() || static_cast<qint64>(size.width()) * size.height() > 128LL * 1024 * 1024)
          return QVariantMap{{QStringLiteral("error"), QStringLiteral("Replacement image dimensions exceed the supported budget.")}};
        const auto image = reader.read(); QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
        if (image.isNull() || !image.save(&buffer, "PNG"))
          return QVariantMap{{QStringLiteral("error"), QStringLiteral("Could not decode the replacement image.")}};
        source = {{QStringLiteral("kind"), QStringLiteral("pixel")}, {QStringLiteral("pixelData"), png},
            {QStringLiteral("pixelWidth"), image.width()}, {QStringLiteral("pixelHeight"), image.height()}};
      }
    }
    if (source.value(QStringLiteral("kind")).toString() == QStringLiteral("source")) {
      const auto source_path = source.value(QStringLiteral("sourcePath")).toString();
      auto dimensions = QImageReader(source_path).size();
      if (!dimensions.isValid()) dimensions = source_image_size(source_path);
      if (dimensions.isValid()) {
        source.insert(QStringLiteral("pixelWidth"), dimensions.width());
        source.insert(QStringLiteral("pixelHeight"), dimensions.height());
      }
    }
    const auto descriptor = smart_object::create_content_descriptor(source,
        linked ? smart_object::ContentPolicy::Linked : smart_object::ContentPolicy::Embedded, &error);
    if (!error.isEmpty()) return QVariantMap{{"error", error}};
    const auto next = replace ? smart_object::replace_contents(captured_layers, {id}, descriptor, &error)
        : smart_object::create_instances(captured_layers, {id}, descriptor, &error);
    return QVariantMap{{"layers", next}, {"error", error}};
  }));
  return true;
}

bool EditorPreviewController::duplicatePhotoSmartObject(int index) {
  return preparePhotoSmartObjectLinkOperation(index, "duplicate");
}
bool EditorPreviewController::makePhotoSmartObjectIndependent(int index) {
  return preparePhotoSmartObjectLinkOperation(index, "independent");
}

bool EditorPreviewController::addPhotoVectorPath(const QVariantMap& path, const QString& fill, const QString& stroke, double stroke_width) {
  const auto access = customer_edit_access_.enter();
  if (!access || !hasAsset() || !QColor(fill).isValid() || !QColor(stroke).isValid() ||
      !std::isfinite(stroke_width) || stroke_width < 0 || stroke_width > 4096) return false;
  vector_paths::VectorPathDocument document; QString error;
  if (!vector_paths::to_document(path, &document, &error)) { setStatus(error); return false; }
  const QSize canvas(sourceImageWidth(), sourceImageHeight());
  if (canvas.isEmpty() || vector_paths::to_painter_path(document, canvas, &error).elementCount() < 2) {
    setStatus(QStringLiteral("Add at least two path anchors before finishing.")); return false;
  }
  auto layers = layer_session_.layers();
  layers.prepend(QVariantMap{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
      {"kind", "pixel"}, {"name", QStringLiteral("Vector Path")},
      {"vectorPath", vector_paths::to_variant(document)}, {"vectorFillColor", fill},
      {"vectorStrokeColor", stroke}, {"vectorStrokeWidth", stroke_width},
      {"pixelWidth", canvas.width()}, {"pixelHeight", canvas.height()},
      {"canvasWidth", canvas.width()}, {"canvasHeight", canvas.height()},
      {"coordinateSpace", "source-area"},
      {"coordinateArea", QVariantMap{{"x", 0.0}, {"y", 0.0}, {"width", 1.0}, {"height", 1.0}}},
      {"enabled", true}, {"opacity", 1.0}, {"scale", 1.0}});
  return applyPhotoLayerStackChange(layers, QStringLiteral("vector_path_create"), false, QStringLiteral("Create vector path"));
}

bool EditorPreviewController::setPhotoLayerVectorPath(int index, const QVariantMap& path, const QVariantMap& appearance) {
  const auto access = customer_edit_access_.enter();
  if (!access || index < 0 || index >= layer_session_.layers().size()) return false;
  auto layers = layer_session_.layers(); auto layer = layers[index].toMap();
  if (professional_layer_locked(layers, index) || !layer.contains(QStringLiteral("vectorPath"))) return false;
  vector_paths::VectorPathDocument document; QString error;
  if (!vector_paths::to_document(path, &document, &error)) { setStatus(error); return false; }
  if (vector_paths::to_painter_path(document, QSize(sourceImageWidth(), sourceImageHeight()), &error).elementCount() < 2) return false;
  const auto fill = appearance.value(QStringLiteral("fill"), layer.value(QStringLiteral("vectorFillColor"))).toString();
  const auto stroke = appearance.value(QStringLiteral("stroke"), layer.value(QStringLiteral("vectorStrokeColor"))).toString();
  const auto width = appearance.value(QStringLiteral("strokeWidth"), layer.value(QStringLiteral("vectorStrokeWidth"))).toDouble();
  if (!QColor(fill).isValid() || !QColor(stroke).isValid() || !std::isfinite(width) || width < 0 || width > 4096) return false;
  layer.insert(QStringLiteral("vectorPath"), vector_paths::to_variant(document));
  if (fill != layer.value("vectorFillColor").toString()) layer.insert("vectorFillEnabled", QColor(fill).alpha() > 0);
  if (stroke != layer.value("vectorStrokeColor").toString()) layer.insert("vectorStrokeEnabled", QColor(stroke).alpha() > 0);
  layer.insert(QStringLiteral("vectorFillColor"), fill); layer.insert(QStringLiteral("vectorStrokeColor"), stroke);
  layer.insert(QStringLiteral("vectorStrokeWidth"), width); layers[index] = layer;
  return applyPhotoLayerStackChange(layers, QStringLiteral("vector_path_edit"), false, QStringLiteral("Edit vector path"));
}

QVariantMap EditorPreviewController::mapPhotoVectorPathCoordinate(int index, double x, double y, bool path_to_view) const {
  if (!std::isfinite(x) || !std::isfinite(y)) return {{"valid", false}};
  const auto size = sourceImageSize();
  if (size.isEmpty()) return {{"valid", false}};
  if (index < 0) {
    if (path_to_view)
      return mapCanvasCoordinate((x * size.width() - 0.5) / std::max(1, size.width() - 1),
          (y * size.height() - 0.5) / std::max(1, size.height() - 1), true, QStringLiteral("source"));
    const auto point = mapCanvasCoordinate(x, y, false, QStringLiteral("source"));
    return {{"valid", point.value("valid")},
        {"x", (point.value("x").toDouble() * std::max(0, size.width() - 1) + 0.5) / size.width()},
        {"y", (point.value("y").toDouble() * std::max(0, size.height() - 1) + 0.5) / size.height()}};
  }
  if (index >= layer_session_.layers().size()) return {{"valid", false}};
  const auto layer = layer_session_.layers()[index].toMap();
  const auto placement = photo_paint_layer_to_document(layer, layer_session_.layers(),
      {size, crop_rect_for_image(size, currentState())});
  if (!placement.valid) return {{"valid", false}};
  const auto width = layer.value("pixelWidth").toDouble(), height = layer.value("pixelHeight").toDouble();
  const auto canvas_width = layer.value("canvasWidth").toDouble(), canvas_height = layer.value("canvasHeight").toDouble();
  if (width <= 0 || height <= 0 || canvas_width <= 0 || canvas_height <= 0) return {{"valid", false}};
  const auto space = QStringLiteral("paint:") + layer.value("id").toString();
  if (path_to_view) {
    const auto point = placement.local_to_document.map(QPointF(x * width, y * height));
    return mapCanvasCoordinate(point.x() / canvas_width, point.y() / canvas_height, true, space);
  }
  const auto mapped = mapCanvasCoordinate(x, y, false, space);
  if (!mapped.value("valid").toBool()) return {{"valid", false}};
  const auto local = placement.local_to_document.inverted().map(QPointF(mapped.value("x").toDouble() * canvas_width,
      mapped.value("y").toDouble() * canvas_height));
  return {{"valid", true}, {"x", local.x() / width}, {"y", local.y() / height}};
}

void EditorPreviewController::cancelPhotoVectorPathSelection() {
  if (professional_path_selection_pending_) {
    (void)mask_session_.advanceWorkRevision();
    professional_path_selection_pending_ = false;
  }
}

bool EditorPreviewController::selectPhotoVectorPath(const QVariantMap& path, const QString& operation, int layer_index) {
  const auto access = customer_edit_access_.enter();
  // "New" is a legacy tool-mode spelling. The working selection is retained
  // until explicitly cleared, so a subsequent path adds to it.
  const auto compose_operation = operation == QStringLiteral("replace") ? QStringLiteral("add") : operation;
  if (!access || selection_work_running_ || (compose_operation != "add" && compose_operation != "subtract" && compose_operation != "intersect")) return false;
  vector_paths::VectorPathDocument document; QString error;
  if (!vector_paths::to_document(path, &document, &error)) { setStatus(error); return false; }
  document.subpaths.removeIf([](const auto& subpath) { return !subpath.closed; });
  if (document.subpaths.isEmpty()) { setStatus(QStringLiteral("Close a path before using it as a selection.")); return false; }
  const auto size = sourceImageSize(); if (size.isEmpty()) return false;
  const auto crop = crop_rect_for_image(size, currentState());
  auto local_size = size; QTransform transform;
  const auto layers = layer_session_.layers();
  if (layer_index >= 0) {
    if (layer_index >= layers.size()) return false;
    const auto layer = layers[layer_index].toMap();
    const auto placement = photo_paint_layer_to_document(layer, layers, {size, crop});
    if (!placement.valid) return false;
    transform = placement.local_to_document;
    local_size = QSize(layer.value("pixelWidth").toInt(), layer.value("pixelHeight").toInt());
  }
  const auto asset = asset_id_; const auto revision = mask_session_.workRevision(); const auto working = workingSelectionSetId();
  professional_path_selection_pending_ = true; selection_work_running_ = true; emit maskStateChanged(); setStatus(QStringLiteral("Preparing path selection…"));
  auto* watcher = new QFutureWatcher<QImage>(this);
  connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, asset, revision, working, operation = compose_operation, layers, crop, size] {
    const auto image = watcher->result(); watcher->deleteLater(); selection_work_running_ = false; emit maskStateChanged();
    if (asset != asset_id_) { professional_path_selection_pending_ = false; emit professionalPathSelectionFinished(false); return; }
    if (revision != mask_session_.workRevision() || working != workingSelectionSetId() || layers != layer_session_.layers() || crop != crop_rect_for_image(size, currentState())) {
      setStatus(QStringLiteral("The selection or layers changed while preparing the path. Current edits were preserved.")); professional_path_selection_pending_ = false; emit professionalPathSelectionFinished(false); return;
    }
    if (image.isNull()) { setStatus(QStringLiteral("Could not rasterize the path selection.")); professional_path_selection_pending_ = false; emit professionalPathSelectionFinished(false); return; }
    const auto raster = image.convertToFormat(QImage::Format_ARGB32_Premultiplied); QByteArray alpha(raster.width() * raster.height(), '\0');
    for (int y = 0; y < raster.height(); ++y) {
      const auto* pixels = reinterpret_cast<const QRgb*>(raster.constScanLine(y));
      for (int x = 0; x < raster.width(); ++x) alpha[y * raster.width() + x] = static_cast<char>(qAlpha(pixels[x]));
    }
    auto mask = literal_selection_mask(alpha, raster.size());
    mask.insert(QStringLiteral("maskSetName"), QStringLiteral("Path Selection"));
    if (hasWorkingSelection()) {
      if (!processWorkingSelection({mask}, {}, operation, true)) { professional_path_selection_pending_ = false; emit professionalPathSelectionFinished(false); }
      return;
    }
    finishLayerMaskEditing(); auto stack = mask_session_.stack(); stack.append(mask);
    const bool accepted = applyMaskStackChange(stack, static_cast<int>(stack.size()) - 1, true, QStringLiteral("path_selection"), false);
    professional_path_selection_pending_ = false; emit professionalPathSelectionFinished(accepted);
  });
  watcher->setFuture(QtConcurrent::run([document, size, local_size, transform] {
    QString error; const auto path = vector_paths::to_painter_path(document, local_size, &error);
    if (!error.isEmpty() || static_cast<qint64>(size.width()) * size.height() > 128LL * 1024 * 1024) return QImage{};
    QImage raster(size, QImage::Format_ARGB32_Premultiplied); raster.fill(Qt::transparent);
    QPainter painter(&raster); painter.setRenderHint(QPainter::Antialiasing); painter.setTransform(transform);
    painter.fillPath(path, Qt::white); painter.end(); return raster;
  }));
  return true;
}

QVariantMap EditorPreviewController::smartObjectLinkStatuses() const {
  QVariantMap statuses;
  for (const auto& item : layer_session_.layers()) {
    const auto layer = item.toMap(); const auto id = layer.value("id").toString(); const auto content = layer.value("smartObject").toMap();
    const auto cached = professional_link_status_cache_.value(id).toMap();
    if (!content.isEmpty() && cached.value("asset").toString() == asset_id_ && cached.value("descriptor").toMap() == content) statuses.insert(id, cached.value("result"));
  }
  return statuses;
}
bool EditorPreviewController::checkPhotoSmartObjectLink(int index) {
  if (professional_content_work_running_ || index < 0 || index >= layer_session_.layers().size()) return false;
  const auto layer = layer_session_.layers()[index].toMap(); const auto descriptor = layer.value("smartObject").toMap();
  if (descriptor.isEmpty()) return false;
  const auto id = layer.value("id").toString(); const auto asset = asset_id_;
  professional_content_work_running_ = true; emit controlsChanged();
  auto* watcher = new QFutureWatcher<QVariantMap>(this);
  connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher, descriptor, id, asset] {
    const auto result = watcher->result(); watcher->deleteLater(); professional_content_work_running_ = false;
    if (asset == asset_id_) for (const auto& item : layer_session_.layers()) {
      const auto layer = item.toMap();
      if (layer.value("id").toString() == id && layer.value("smartObject").toMap() == descriptor)
        professional_link_status_cache_.insert(id, QVariantMap{{"asset", asset}, {"descriptor", descriptor}, {"result", result}});
    }
    emit controlsChanged();
  });
  watcher->setFuture(QtConcurrent::run([descriptor] { QString error; auto result = smart_object::inspect_link_status(descriptor, &error);
    if (result.isEmpty()) result = {{"status", "unreadable"}, {"message", error}};
    return result; }));
  return true;
}
bool EditorPreviewController::relinkPhotoSmartObjectContents(int index, const QString& path) {
  if (path.trimmed().isEmpty()) return false;
  const QUrl url(path); return preparePhotoSmartObjectLinkOperation(index, "relink", url.isLocalFile() ? url.toLocalFile() : path);
}
bool EditorPreviewController::refreshPhotoSmartObjectContents(int index) { return preparePhotoSmartObjectLinkOperation(index, "refresh"); }
bool EditorPreviewController::embedPhotoSmartObjectContents(int index) { return preparePhotoSmartObjectLinkOperation(index, "embed"); }
bool EditorPreviewController::preparePhotoSmartObjectLinkOperation(int index, const QString& operation, const QString& path) {
  if (transient_document_session_ && (operation == QStringLiteral("relink") || operation == QStringLiteral("refresh"))) {
    setStatus(tr("Component images are embedded. Keep linked sources in the parent document."));
    return false;
  }
  const auto access = customer_edit_access_.enter(); const auto layers = layer_session_.layers();
  if (!access || professional_content_work_running_ || index < 0 || index >= layers.size() || professional_layer_locked(layers, index)) return false;
  const auto layer = layers[index].toMap(); const auto content = layer.value("smartObject").toMap();
  if (content.isEmpty() || ((operation != "duplicate" && operation != "independent") && content.value("policy").toString() != "linked")) return false;
  const auto id = layer.value("id").toString(), asset = asset_id_;
  professional_content_work_running_ = true; emit controlsChanged(); setStatus("Preparing linked content…");
  auto* watcher = new QFutureWatcher<QVariantMap>(this);
  connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher, asset, layers, operation] {
    const auto result = watcher->result(); watcher->deleteLater(); professional_content_work_running_ = false; emit controlsChanged();
    if (asset != asset_id_ || layers != layer_session_.layers()) { setStatus("The document changed while preparing content. Current edits were preserved."); return; }
    const auto error = result.value("error").toString(); if (!error.isEmpty()) { setStatus(error); return; }
    const auto next = result.value("layers").toList();
    professional_link_status_cache_.clear();
    applyPhotoLayerStackChange(next, "smart_object_link", false,
        operation == "duplicate" ? "Duplicate shared instance" : operation == "independent" ? "Make Smart Object independent" :
        operation == "embed" ? "Embed linked source" : operation == "refresh" ? "Update linked source" : "Relink shared source");
  });
  watcher->setFuture(QtConcurrent::run([layers, layer, index, id, content, operation, path] {
    QString error; QVariantList next;
    if (operation == "duplicate") {
      auto copy = smart_object::create_instance(layer, content, QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
      next = layers;
      if (error.isEmpty() && !copy.isEmpty()) {
        copy.insert("name", layer.value("name").toString() + " Copy"); next.insert(index, copy);
      }
      return QVariantMap{{"layers", next}, {"error", error}};
    }
    if (operation == "independent") {
      next = smart_object::make_independent(layers, {id}, &error);
      return QVariantMap{{"layers", next}, {"error", error}};
    }
    auto dimensions = QImageReader(operation == "relink" ? path : content.value("sourcePath").toString()).size();
    if (!dimensions.isValid() && operation != "embed") dimensions = source_image_size(operation == "relink" ? path : content.value("sourcePath").toString());
    const auto width = dimensions.isValid() ? dimensions.width() : 0, height = dimensions.isValid() ? dimensions.height() : 0;
    if (operation == "embed") next = smart_object::embed_linked_contents(layers, {id}, &error);
    else if (operation == "refresh") next = smart_object::refresh_linked_contents(layers, {id}, width, height, &error);
    else next = smart_object::relink_contents(layers, {id}, path, width, height, &error);
    return QVariantMap{{"layers", next}, {"error", error}};
  }));
  return true;
}

bool EditorPreviewController::setPhotoLayerVectorMask(int index, const QVariantMap& path) {
  const auto access = customer_edit_access_.enter();
  auto layers = layer_session_.layers();
  if (!access || index < 0 || index >= layers.size() || professional_layer_locked(layers, index)) return false;
  auto layer = layers[index].toMap();
  if (layer.value("baseImageEdit").toBool()) return false;
  vector_masks::PhotoVectorMask mask; QString error;
  if (layer.contains("vectorMask") && !vector_masks::from_variant(layer.value("vectorMask").toMap(), &mask, &error)) { setStatus(error); return false; }
  if (!vector_paths::to_document(path, &mask.path, &error)) { setStatus(error); return false; }
  if (mask.id.isEmpty()) mask.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const auto value = vector_masks::to_variant(mask, &error);
  if (!error.isEmpty()) { setStatus(error); return false; }
  if (value == layer.value("vectorMask").toMap()) return false;
  layer.insert("vectorMask", value); layers[index] = layer;
  return applyPhotoLayerStackChange(layers, "vector_mask_path", false, "Edit vector mask");
}

bool EditorPreviewController::changePhotoLayerVectorMaskSetting(int index, const QString& key, const QVariant& value) {
  const auto access = customer_edit_access_.enter(); auto layers = layer_session_.layers();
  if (!access || index < 0 || index >= layers.size() || professional_layer_locked(layers, index)) return false;
  auto layer = layers[index].toMap(); auto definition = layer.value("vectorMask").toMap();
  if (definition.isEmpty() || definition.value(key) == value) return false;
  definition.insert(key, value); vector_masks::PhotoVectorMask mask; QString error;
  if (!vector_masks::from_variant(definition, &mask, &error)) { setStatus(error); return false; }
  layer.insert("vectorMask", vector_masks::to_variant(mask)); layers[index] = layer;
  return applyPhotoLayerStackChange(layers, "vector_mask_setting", false, "Change vector mask");
}
bool EditorPreviewController::setPhotoLayerVectorMaskBasis(int index, const QString& basis) {
  const auto access = customer_edit_access_.enter(); auto layers = layer_session_.layers();
  if (!access || index < 0 || index >= layers.size() || professional_layer_locked(layers, index)) return false;
  if (basis != "local-layer" && basis != "source-document") return false;
  auto layer = layers[index].toMap(); vector_masks::PhotoVectorMask source, converted; QString error;
  if (!vector_masks::from_variant(layer.value("vectorMask").toMap(), &source, &error)) { setStatus(error); return false; }
  const auto target = basis == "local-layer" ? vector_masks::CoordinateBasis::LocalLayer : vector_masks::CoordinateBasis::SourceDocument;
  if (source.basis == target) return false;
  const auto size = sourceImageSize();
  const auto placement = vector_mask_placement(layer, layers, {size, crop_rect_for_image(size, currentState())});
  if (!vector_masks::rebase_coordinates(source, target, placement, size, &converted, &error)) { setStatus(error); return false; }
  layer.insert("vectorMask", vector_masks::to_variant(converted)); layers[index] = layer;
  return applyPhotoLayerStackChange(layers, "vector_mask_basis", false, "Change mask coordinate basis");
}
bool EditorPreviewController::setPhotoLayerVectorMaskEnabled(int index, bool enabled) { return changePhotoLayerVectorMaskSetting(index, "enabled", enabled); }
bool EditorPreviewController::setPhotoLayerVectorMaskInverted(int index, bool inverted) { return changePhotoLayerVectorMaskSetting(index, "invert", inverted); }
bool EditorPreviewController::setPhotoLayerVectorMaskDensity(int index, double density) {
  if (!std::isfinite(density) || density < 0 || density > 1) return false;
  return changePhotoLayerVectorMaskSetting(index, "density", density);
}
bool EditorPreviewController::removePhotoLayerVectorMask(int index) {
  const auto access = customer_edit_access_.enter(); auto layers = layer_session_.layers();
  if (!access || index < 0 || index >= layers.size() || professional_layer_locked(layers, index)) return false;
  auto layer = layers[index].toMap(); if (!layer.contains("vectorMask")) return false;
  layer.remove("vectorMask"); layers[index] = layer;
  return applyPhotoLayerStackChange(layers, "vector_mask_remove", false, "Remove vector mask");
}
QVariantMap EditorPreviewController::mapPhotoVectorMaskCoordinate(int index, double x, double y, bool path_to_view) const {
  if (!std::isfinite(x) || !std::isfinite(y) || index < 0 || index >= layer_session_.layers().size()) return {{"valid", false}};
  const auto size = sourceImageSize(); const auto layer = layer_session_.layers()[index].toMap();
  const auto placement = vector_mask_placement(layer, layer_session_.layers(), {size, crop_rect_for_image(size, currentState())});
  if (!placement.valid) return {{"valid", false}};
  const bool source_basis = layer.value("vectorMask").toMap().value("basis").toString() == "source-document";
  if (path_to_view) {
    const auto source = source_basis ? QPointF(x * size.width(), y * size.height()) : placement.local_to_source.map(QPointF(x, y));
    return mapCanvasCoordinate((source.x() - 0.5) / std::max(1, size.width() - 1),
        (source.y() - 0.5) / std::max(1, size.height() - 1), true, "source");
  }
  const auto source = mapCanvasCoordinate(x, y, false, "source");
  if (!source.value("valid").toBool()) return {{"valid", false}};
  const QPointF source_point(source.value("x").toDouble() * std::max(0, size.width() - 1) + 0.5,
      source.value("y").toDouble() * std::max(0, size.height() - 1) + 0.5);
  const auto point = source_basis ? QPointF(source_point.x() / size.width(), source_point.y() / size.height())
      : placement.local_to_source.inverted().map(source_point);
  return {{"valid", true}, {"x", point.x()}, {"y", point.y()}};
}
bool EditorPreviewController::transferPhotoLayerVectorMask(const QString& source_id, const QString& target_id, bool copy) {
  const auto access = customer_edit_access_.enter(); if (!access || source_id == target_id) return false;
  auto layers = layer_session_.layers(); int source = -1, target = -1;
  for (int i = 0; i < layers.size(); ++i) { const auto id = layers[i].toMap().value("id").toString(); if (id == source_id) source = i; if (id == target_id) target = i; }
  if (source < 0 || target < 0 || professional_layer_locked(layers, source) || professional_layer_locked(layers, target)) return false;
  auto source_layer = layers[source].toMap(), target_layer = layers[target].toMap();
  if (target_layer.contains("vectorMask")) { setStatus("Remove the destination vector mask before moving another onto it."); return false; }
  if (target_layer.value("baseImageEdit").toBool()) return false;
  vector_masks::PhotoVectorMask mask; QString error;
  if (!vector_masks::from_variant(source_layer.value("vectorMask").toMap(), &mask, &error)) { setStatus(error); return false; }
  const auto size = sourceImageSize();
  if (mask.basis == vector_masks::CoordinateBasis::LocalLayer) {
    const auto placement = vector_mask_placement(source_layer, layers, {size, crop_rect_for_image(size, currentState())});
    if (!placement.valid) return false;
    const auto bounds = mask.path.coordinate_space == vector_paths::CoordinateSpace::Normalized ? QSizeF(1, 1) : mask.path.document_size;
    const auto map = [&](QPointF point) { const auto p = placement.local_to_source.map(QPointF(point.x() / bounds.width(), point.y() / bounds.height())); return QPointF(p.x() / size.width(), p.y() / size.height()); };
    for (auto& subpath : mask.path.subpaths) for (auto& anchor : subpath.anchors) {
      const auto p = map(anchor.position), in = map(anchor.position + anchor.incoming), out = map(anchor.position + anchor.outgoing);
      anchor.position = p; anchor.incoming = in - p; anchor.outgoing = out - p;
    }
    mask.path.coordinate_space = vector_paths::CoordinateSpace::Normalized; mask.path.document_size = QSizeF(0, 0);
    mask.basis = vector_masks::CoordinateBasis::SourceDocument;
  }
  if (copy) mask.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const auto definition = vector_masks::to_variant(mask, &error);
  if (!error.isEmpty()) { setStatus("The transformed mask extends beyond the supported document bounds. Current masks were preserved."); return false; }
  target_layer.insert("vectorMask", definition); layers[target] = target_layer;
  if (!copy) { source_layer.remove("vectorMask"); layers[source] = source_layer; }
  return applyPhotoLayerStackChange(layers, "vector_mask_transfer", false, copy ? "Copy vector mask" : "Move vector mask");
}

}  // namespace photo_editor::desktop
