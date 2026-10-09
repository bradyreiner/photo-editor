#pragma once

#include "export_image_encoder.h"
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <atomic>
#include <functional>
#include <memory>
#include <exception>

namespace photo_editor::desktop {

struct PhotoExportControl final {
  enum class Phase { Running, Cancelled, Publishing, Finished };
  std::atomic<Phase> phase{Phase::Running};
  [[nodiscard]] bool cancel() {
    auto expected = Phase::Running;
    return phase.compare_exchange_strong(expected, Phase::Cancelled);
  }
  [[nodiscard]] bool cancelled() const { return phase.load() == Phase::Cancelled; }
};

struct PhotoExportResult final {
  bool success = false;
  bool cancelled = false;
  QString path;
  QString error;
};

// The producer owns a recipe/layer snapshot, never a controller or UI object.
// Cancellation is accepted until the single atomic transition to publication.
[[nodiscard]] inline PhotoExportResult run_photo_export(
    const std::function<QImage()>& produce,
    const QString& path,
    const QByteArray& format,
    const std::shared_ptr<PhotoExportControl>& control,
    const std::function<void()>& before_commit = {}) {
  PhotoExportResult result;
  result.path = path;
  const auto fail = [&](const QString& error) {
    auto expected = PhotoExportControl::Phase::Running;
    if (!control->phase.compare_exchange_strong(expected, PhotoExportControl::Phase::Finished)) {
      result.cancelled = expected == PhotoExportControl::Phase::Cancelled;
      if (expected == PhotoExportControl::Phase::Publishing)
        control->phase.store(PhotoExportControl::Phase::Finished);
    }
    result.error = result.cancelled ? QStringLiteral("Export cancelled.") : error;
    return result;
  };
  try {
    if (control->cancelled()) return fail({});
    if (path.isEmpty() || (format != "png" && format != "jpg" && format != "tif"))
      return fail(QStringLiteral("Choose a PNG, JPEG or TIFF export destination."));
    QImage image = produce();
    if (control->cancelled()) return fail({});
    if (image.isNull()) return fail(QStringLiteral("Could not render the export image."));
    if (format == "png" && (image.format() == QImage::Format_RGBA32FPx4 ||
                            image.format() == QImage::Format_RGBA32FPx4_Premultiplied))
      image = image.convertToFormat(QImage::Format_RGBA64);
    if (format == "jpg") {
      auto settings = export_image_settings_for_preset(QStringLiteral("JPEG"));
      image = prepare_image_for_export_encoding(image, settings);
      if (image.isNull()) return fail(QStringLiteral("Could not prepare the JPEG image."));
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
      return fail(QStringLiteral("Could not create the export directory."));
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) return fail(output.errorString());
    QImageWriter writer(&output, format);
    if (format == "jpg") writer.setQuality(95);
    else if (format == "png") writer.setCompression(kPngExportCompression);
    if (!writer.write(image)) {
      output.cancelWriting();
      return fail(writer.errorString());
    }
    if (before_commit) before_commit();
    auto expected = PhotoExportControl::Phase::Running;
    if (!control->phase.compare_exchange_strong(expected, PhotoExportControl::Phase::Publishing)) {
      output.cancelWriting();
      return fail(QStringLiteral("Export publication was cancelled."));
    }
    result.success = output.commit();
    if (!result.success) result.error = output.errorString();
    control->phase.store(PhotoExportControl::Phase::Finished);
    return result;
  } catch (const std::exception& error) {
    return fail(QString::fromUtf8(error.what()));
  } catch (...) {
    return fail(QStringLiteral("Unexpected export failure."));
  }
}

}  // namespace photo_editor::desktop
