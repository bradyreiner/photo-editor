#pragma once

#include <QColorSpace>
#include <QDir>
#include <QImage>
#include <QImageWriter>
#include <QSaveFile>
#include <QScopeGuard>
#include <QUuid>
#include <QVariantMap>
#include <cmath>
#include <functional>

namespace photo_editor::desktop {

// A canvas has an owned, durable source just like an imported photograph. The
// normal catalog and recipe paths remain responsible for its editable layers.
inline QString blank_canvas_file_title(const QVariantMap& settings) {
  auto title = settings.value(QStringLiteral("name"), QStringLiteral("Untitled Canvas")).toString().trimmed().left(120);
  if (title.isEmpty()) title = QStringLiteral("Untitled Canvas");
  for (auto& ch : title) {
    if (!ch.isLetterOrNumber() && ch != QChar(' ') && ch != QChar('-') && ch != QChar('_')) ch = QChar('_');
  }
  return title;
}

// An executor can capture file absence before starting a worker, using the same
// identity/path that worker will create. Only UUID document folders are admitted.
inline QString planned_blank_canvas_source_path(const QString& document_root,
                                                const QVariantMap& settings,
                                                const QString& document_id) {
  const QUuid id(document_id);
  if (document_root.isEmpty() || !QDir(document_root).isAbsolute() || id.isNull() ||
      id.toString(QUuid::WithoutBraces) != document_id.toLower()) return {};
  return QDir(QDir(document_root).filePath(document_id)).filePath(blank_canvas_file_title(settings) + QStringLiteral(".png"));
}

inline QVariantMap create_blank_canvas_source(const QString& document_root,
                                              const QVariantMap& settings,
                                              const QString& planned_document_id = {},
                                              const std::function<bool()>& cancelled = {}) {
  const auto failure = [](const QString& error) {
    return QVariantMap{{QStringLiteral("error"), error}};
  };
  const auto stopped = [&cancelled]() { return cancelled && cancelled(); };
  const auto cancelled_result = [] {
    return QVariantMap{{QStringLiteral("error"), QStringLiteral("Canvas creation stopped.")},
        {QStringLiteral("cancelled"), true}, {QStringLiteral("mutationNotApplied"), true}};
  };
  if (stopped()) return cancelled_result();
  const auto width = settings.value(QStringLiteral("width"), 1080).toDouble();
  const auto height = settings.value(QStringLiteral("height"), 1350).toDouble();
  const auto dpi = settings.value(QStringLiteral("resolution"), 144).toDouble();
  if (!std::isfinite(width) || !std::isfinite(height) || width < 1 || height < 1 ||
      width > 8192 || height > 8192 || width != std::floor(width) || height != std::floor(height) ||
      width * height > 32.0 * 1024.0 * 1024.0) {
    return failure(QStringLiteral("Use whole-pixel dimensions from 1 to 8192, up to 32 megapixels."));
  }
  if (!std::isfinite(dpi) || dpi < 72 || dpi > 600) {
    return failure(QStringLiteral("Resolution must be between 72 and 600 pixels per inch."));
  }
  const auto background = settings.value(QStringLiteral("background"), QStringLiteral("transparent")).toString();
  const auto color = background == QStringLiteral("transparent") ? QColor(Qt::transparent)
      : background == QStringLiteral("white") ? QColor(Qt::white)
      : background == QStringLiteral("color") ? QColor(settings.value(QStringLiteral("color")).toString())
      : QColor{};
  if (!color.isValid()) return failure(QStringLiteral("Choose a valid canvas background color."));
  auto title = settings.value(QStringLiteral("name"), QStringLiteral("Untitled Canvas")).toString().trimmed().left(120);
  if (title.isEmpty()) title = QStringLiteral("Untitled Canvas");
  const auto id = planned_document_id.isEmpty()
      ? QUuid::createUuid().toString(QUuid::WithoutBraces) : planned_document_id;
  const auto path = planned_blank_canvas_source_path(document_root, settings, id);
  if (path.isEmpty()) return failure(QStringLiteral("A valid absolute document root and canvas identity are required."));
  QImage source(static_cast<int>(width), static_cast<int>(height), QImage::Format_RGBA8888);
  if (source.isNull()) return failure(QStringLiteral("Not enough memory for this canvas."));
  source.fill(color);
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  source.setDotsPerMeterX(static_cast<int>(std::lround(dpi / 0.0254)));
  source.setDotsPerMeterY(static_cast<int>(std::lround(dpi / 0.0254)));
  // A unique embedded identity also keeps identical blank documents distinct
  // when the importer checks file-content duplicates.
  source.setText(QStringLiteral("LooksyCanvasId"), id);
  source.setText(QStringLiteral("Title"), title);
  if (stopped()) return cancelled_result();
  if (!QDir().mkpath(document_root) || !QDir(document_root).mkdir(id))
    return failure(QStringLiteral("Could not create a new canvas document folder; an existing document was preserved."));
  bool committed = false;
  const auto cleanup = qScopeGuard([&] { if (!committed) static_cast<void>(QDir(document_root).rmdir(id)); });
  if (stopped()) return cancelled_result();
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) return failure(file.errorString());
  QImageWriter writer(&file, "png");
  if (!writer.write(source)) return failure(writer.errorString());
  if (stopped()) { file.cancelWriting(); return cancelled_result(); }
  if (!file.commit()) return failure(file.errorString());
  committed = true;
  return {{QStringLiteral("path"), path}, {QStringLiteral("name"), title}, {QStringLiteral("documentId"), id}};
}

}  // namespace photo_editor::desktop
