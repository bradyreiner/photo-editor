#include "editor_preview_controller.h"
#include "photo_vector_path_io.h"
#include <QFile>
#include <QFutureWatcher>
#include <QSaveFile>
#include <QUrl>
#include <QUuid>
#include <QtConcurrent>

namespace photo_editor::desktop {
bool EditorPreviewController::importPhotoVectorSvg(const QString& asset_id, const QString& path) {
  const auto access = customer_edit_access_.enter();
  if (!access || asset_id != asset_id_ || !hasAsset() || professional_content_work_running_ || path.isEmpty()) return false;
  const auto layers = layer_session_.layers(); const auto size = sourceImageSize(); const QUrl url(path);
  if (size.isEmpty()) return false;
  const auto file = url.isLocalFile() ? url.toLocalFile() : path;
  professional_content_work_running_ = true; emit controlsChanged(); setStatus("Importing editable SVG paths…");
  auto* watcher = new QFutureWatcher<QVariantMap>(this);
  connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher, asset_id, layers] {
    const auto result = watcher->result(); watcher->deleteLater(); professional_content_work_running_ = false; emit controlsChanged();
    if (asset_id != asset_id_ || layers != layer_session_.layers()) { setStatus("The document changed. Current layers were preserved."); return; }
    const auto error = result.value("error").toString(); if (!error.isEmpty()) { setStatus("SVG import failed: " + error); return; }
    applyPhotoLayerStackChange(result.value("layers").toList(), "import_vector_svg", false, "Import editable SVG paths");
  });
  watcher->setFuture(QtConcurrent::run([file, layers, size] {
    QFile input(file); if (!input.open(QIODevice::ReadOnly) || input.size() > 8LL * 1024 * 1024)
      return QVariantMap{{"error", "SVG input is unavailable or exceeds 8 MiB."}};
    QVector<vector_path_io::StyledPath> paths; QString error;
    if (!vector_path_io::import_svg(input.readAll(), &paths, &error)) return QVariantMap{{"error", error}};
    auto next = layers; QVariantList imported;
    for (const auto& styled : paths) {
      const auto bounds = styled.path.document_size;
      if (!bounds.isValid() || bounds.isEmpty()) return QVariantMap{{"error", "SVG viewBox is invalid."}};
      const auto scale = std::min({1.0, size.width() / bounds.width(), size.height() / bounds.height()});
      const auto width = std::max(1, qRound(bounds.width() * scale)), height = std::max(1, qRound(bounds.height() * scale));
      const auto stroke_width = styled.stroke_color.isEmpty() ? 0.0 : styled.stroke_width * scale;
      if (!std::isfinite(stroke_width) || stroke_width < 0 || stroke_width > 4096.0)
        return QVariantMap{{"error", "SVG stroke width exceeds this editor's supported raster range."}};
      QVariantMap layer{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"name", "SVG Path"}, {"kind", "pixel"},
          {"vectorPath", vector_paths::to_variant(styled.path)}, {"vectorFillColor", styled.fill_color.isEmpty() ? "transparent" : styled.fill_color},
          {"vectorStrokeColor", styled.stroke_color.isEmpty() ? "transparent" : styled.stroke_color}, {"vectorStrokeWidth", stroke_width},
          {"vectorFillEnabled", !styled.fill_color.isEmpty()}, {"vectorStrokeEnabled", !styled.stroke_color.isEmpty()},
          {"pixelWidth", width}, {"pixelHeight", height}, {"canvasWidth", size.width()}, {"canvasHeight", size.height()},
          {"coordinateSpace", "source-area"}, {"coordinateArea", QVariantMap{{"x", 0.0}, {"y", 0.0}, {"width", 1.0}, {"height", 1.0}}},
          {"enabled", true}, {"opacity", 1.0}, {"scale", 1.0}};
      imported.append(layer);
    }
    // SVG paints elements in document order; Looksy's first row is topmost.
    for (const auto& item : imported) next.prepend(item);
    return QVariantMap{{"layers", next}};
  }));
  return true;
}
bool EditorPreviewController::exportPhotoVectorSvg(const QString& asset_id, const QString& layer_id, const QString& path) {
  if (asset_id != asset_id_ || professional_content_work_running_ || path.isEmpty()) return false;
  QVariantMap layer; for (const auto& item : layer_session_.layers()) if (item.toMap().value("id").toString() == layer_id) { layer = item.toMap(); break; }
  if (layer.value("vectorPath").toMap().isEmpty()) return false;
  const QUrl url(path); const auto file = url.isLocalFile() ? url.toLocalFile() : path;
  professional_content_work_running_ = true; emit controlsChanged(); setStatus("Exporting SVG path…");
  auto* watcher = new QFutureWatcher<QString>(this);
  connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, asset_id] {
    const auto error = watcher->result(); watcher->deleteLater(); professional_content_work_running_ = false; emit controlsChanged();
    if (asset_id == asset_id_) setStatus(error.isEmpty() ? "SVG path exported." : "SVG export failed: " + error);
  });
  watcher->setFuture(QtConcurrent::run([layer, file] {
    vector_path_io::StyledPath styled; QString error;
    if (!vector_paths::to_document(layer.value("vectorPath").toMap(), &styled.path, &error)) return error;
    styled.fill_color = layer.value("vectorFillEnabled", true).toBool() ? layer.value("vectorFillColor", "#4a78ff").toString() : QString{};
    styled.stroke_color = layer.value("vectorStrokeEnabled", true).toBool() ? layer.value("vectorStrokeColor", "#ffffff").toString() : QString{};
    styled.stroke_width = layer.value("vectorStrokeWidth", 0.0).toDouble();
    const auto bytes = vector_path_io::export_svg({styled}, QSizeF(layer.value("pixelWidth").toDouble(), layer.value("pixelHeight").toDouble()), &error);
    if (!error.isEmpty() || bytes.isEmpty()) return error.isEmpty() ? QString("SVG export produced no data.") : error;
    QSaveFile output(file); if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) return QString("Could not save the SVG path.");
    return QString{};
  }));
  return true;
}
}
