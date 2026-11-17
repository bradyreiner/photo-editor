#pragma once

#include <QQuickImageProvider>

namespace photo_editor::desktop {

// Bounded, display-referred source thumbnails for the pre-import browser.
// This provider never owns Edit's linear RAW decoding or color pipeline.
class ImportPreviewImageProvider : public QQuickImageProvider {
 public:
  ImportPreviewImageProvider();
  QImage requestImage(const QString& id, QSize* size, const QSize& requested_size) override;
};

QImage read_import_preview_image(const QString& path, int maximum_edge);

}  // namespace photo_editor::desktop
