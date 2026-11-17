#include "import_preview_image_provider.h"
#include "raw_image_decoder.h"
#include <QImageReader>

namespace photo_editor::desktop {
QImage read_import_preview_image(const QString& path, int maximum_edge) {
  if (raw_image_supported(path)) {
    QImage result;
    RawDecodeSettings settings;
    settings.demosaic_quality = QStringLiteral("Preview");
    return decode_raw_image(path, &result, maximum_edge, settings) ? result : QImage{};
  }
  QImageReader reader(path);
  reader.setAutoTransform(true);
  const auto size = reader.size();
  if (size.isValid()) reader.setScaledSize(size.scaled(maximum_edge, maximum_edge, Qt::KeepAspectRatio));
  return reader.read();
}
}  // namespace photo_editor::desktop
