#pragma once

#include <QQuickImageProvider>
#include <QString>

class QQmlEngine;

namespace photo_editor::desktop {

class CreativeExampleThumbnailImageProvider final : public QQuickAsyncImageProvider {
 public:
  QQuickImageResponse* requestImageResponse(
      const QString& id,
      const QSize& requested_size) override;
};

void register_creative_example_thumbnail_provider(QQmlEngine* engine);

}  // namespace photo_editor::desktop
