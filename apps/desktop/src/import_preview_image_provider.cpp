#include "import_preview_image_provider.h"

#include <QFileInfo>
#include <QUrl>
#include <algorithm>

namespace photo_editor::desktop {

ImportPreviewImageProvider::ImportPreviewImageProvider()
    : QQuickImageProvider(Image, QQmlImageProviderBase::ForceAsynchronousImageLoading) {}

QImage ImportPreviewImageProvider::requestImage(const QString& id, QSize* size, const QSize& requested_size) {
  const auto path = QUrl::fromPercentEncoding(id.section(QLatin1Char('?'), 0, 0).toUtf8());
  const QFileInfo source(path);
  if (!source.isAbsolute() || !source.isFile() || !source.isReadable()) {
    if (size) *size = {};
    return {};
  }
  const auto requested_edge = std::max(requested_size.width(), requested_size.height());
  const auto edge = std::clamp(requested_edge > 0 ? requested_edge : 384, 32, 1024);
  auto result = read_import_preview_image(path, edge);
  if (size) *size = result.size();
  return result;
}

}  // namespace photo_editor::desktop
