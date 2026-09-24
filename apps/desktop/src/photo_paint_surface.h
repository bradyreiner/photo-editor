#pragma once

#include "photo_paint_format.h"
#include <QColorSpace>
#include <QImage>
#include <QPainter>
#include <QVariantMap>
#include <QtEndian>
#include <cstring>
#include <limits>

namespace photo_editor::desktop {

inline QVariantMap empty_photo_paint_tiles(const QColorSpace& space) {
  return {{"version", 1}, {"format", QLatin1String(kPhotoPaintFormat)},
      {"tileSize", kPhotoPaintTileEdge}, {"icc", QString::fromLatin1(space.iccProfile().toBase64())},
      {"tiles", QVariantMap{}}};
}

inline QVariantMap photo_paint_target_identity(QVariantMap layer, bool include_content = true) {
  for (auto it=layer.begin();it!=layer.end();) {
    if (it.key().startsWith(QStringLiteral("thumbnail"),Qt::CaseInsensitive)) it=layer.erase(it);
    else ++it;
  }
  if (!include_content) { layer.remove("paintTiles"); layer.remove("pixelData"); }
  return layer;
}

// Immutable, compressed tiles are JSON-safe strings. QVariant's implicit sharing
// gives each history state its own tile map while sharing every unchanged tile.
class PhotoPaintSurface final {
 public:
  QSize size;
  QColorSpace color_space;
  QVariantMap payload;
  QVariantMap tiles;
  QImage legacy;

  bool load(const QVariantMap& layer, QString* error = nullptr) {
    const auto fail = [&](const QString& text) { if (error) *error = text; return false; };
    size = QSize(layer.value("pixelWidth").toInt(), layer.value("pixelHeight").toInt());
    if (size.isEmpty() || size.width() > 1000000 || size.height() > 1000000 ||
        static_cast<qint64>(size.width()) * size.height() > 1000000000)
      return fail(QStringLiteral("Invalid paint canvas dimensions."));
    payload = layer.value("paintTiles").toMap();
    if (payload.isEmpty()) payload = empty_photo_paint_tiles(QColorSpace(QColorSpace::SRgbLinear));
    if (payload.value("version").toInt() != 1 || payload.value("format").toString() != QLatin1String(kPhotoPaintFormat) ||
        payload.value("tileSize").toInt() != kPhotoPaintTileEdge)
      return fail(QStringLiteral("Unsupported paint tile format."));
    color_space = QColorSpace::fromIccProfile(QByteArray::fromBase64(payload.value("icc").toString().toLatin1()));
    if (!color_space.isValid()) return fail(QStringLiteral("Paint layer color profile is invalid."));
    tiles = payload.value("tiles").toMap();
    for (auto it = tiles.cbegin(); it != tiles.cend(); ++it) {
      const auto parts = it.key().split(',');
      bool x_ok = false, y_ok = false;
      const int x = parts.value(0).toInt(&x_ok), y = parts.value(1).toInt(&y_ok);
      if (parts.size() != 2 || !x_ok || !y_ok || x < 0 || y < 0 ||
          x >= (size.width() + kPhotoPaintTileEdge - 1) / kPhotoPaintTileEdge ||
          y >= (size.height() + kPhotoPaintTileEdge - 1) / kPhotoPaintTileEdge || key(x,y) != it.key())
        return fail(QStringLiteral("Invalid paint tile coordinates."));
    }
    const auto png = layer.value("pixelData").toByteArray();
    if (!png.isEmpty()) {
      legacy = QImage::fromData(png, "PNG");
      if (legacy.isNull() || legacy.size() != size) return fail(QStringLiteral("Could not decode the original pixel layer."));
      if (!legacy.colorSpace().isValid()) legacy.setColorSpace(QColorSpace(QColorSpace::SRgb));
    }
    return true;
  }

  static QString key(int x, int y) { return QString::number(x) + ',' + QString::number(y); }
  QRect tile_rect(int x, int y) const {
    return QRect(x * kPhotoPaintTileEdge, y * kPhotoPaintTileEdge, kPhotoPaintTileEdge, kPhotoPaintTileEdge)
        .intersected(QRect(QPoint(), size));
  }
  QImage tile(int x, int y, QString* error = nullptr) const {
    const auto rect = tile_rect(x,y);
    QImage image;
    const auto value = tiles.value(key(x,y));
    if (value.isValid()) {
      const auto packed = QByteArray::fromBase64(value.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
      const auto expected = static_cast<qsizetype>(rect.width()) * rect.height() * 16;
      // Validate qCompress's declared length before decompression/allocation.
      if (packed.size() < 4 || qFromBigEndian<quint32>(packed.constData()) != expected ||
          packed.size() > expected * 2 + 1024) {
        if (error) *error = QStringLiteral("Invalid paint tile payload.");
        return {};
      }
      auto raw = qUncompress(packed);
      if (raw.size() != expected) {
        if (error) *error = QStringLiteral("Paint tile is truncated or corrupt.");
        return {};
      }
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
      for (qsizetype i = 0; i < raw.size(); i += 4) {
        const auto value32 = qFromLittleEndian<quint32>(raw.constData() + i);
        std::memcpy(raw.data() + i, &value32, 4);
      }
#endif
      image = QImage(reinterpret_cast<const uchar*>(raw.constData()), rect.width(), rect.height(),
          rect.width() * 16, QImage::Format_RGBA32FPx4_Premultiplied).copy();
    } else if (!legacy.isNull()) {
      image = legacy.copy(rect).convertedToColorSpace(color_space, QImage::Format_RGBA32FPx4_Premultiplied);
    } else {
      image = QImage(rect.size(), QImage::Format_RGBA32FPx4_Premultiplied);
      image.fill(Qt::transparent);
    }
    image.setColorSpace(color_space);
    return image;
  }

  void store(int x, int y, const QImage& image) {
    QByteArray raw(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes());
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    for (qsizetype i = 0; i < raw.size(); i += 4) {
      quint32 value; std::memcpy(&value, raw.constData() + i, 4);
      qToLittleEndian(value, raw.data() + i);
    }
#endif
    tiles.insert(key(x,y), QString::fromLatin1(qCompress(raw, 1).toBase64()));
  }
  QVariantMap snapshot() const {
    auto result = payload;
    result.insert("tiles", tiles);
    return result;
  }
  QImage raster(QSize output = {}, QString* error = nullptr, const QVariantMap& draft = {}) const {
    if (!output.isValid()) output = size;
    return raster_region(output,QRect(QPoint(),output),error,draft);
  }
  QImage raster_region(QSize output, QRect region, QString* error = nullptr,
      const QVariantMap& draft = {}) const {
    if (!output.isValid()) output = size;
    QImage image(region.size(), QImage::Format_RGBA32FPx4_Premultiplied);
    if (image.isNull()) { if (error) *error = QStringLiteral("Paint raster allocation failed."); return {}; }
    image.setColorSpace(color_space);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.translate(-region.topLeft());
    painter.scale(static_cast<double>(output.width()) / size.width(), static_cast<double>(output.height()) / size.height());
    if (!legacy.isNull()) {
      const auto base = legacy.convertedToColorSpace(color_space, QImage::Format_RGBA32FPx4_Premultiplied);
      painter.drawImage(QRect(QPoint(),size), base);
    }
    auto keys = tiles.keys();
    for (auto it = draft.cbegin(); it != draft.cend(); ++it) if (!tiles.contains(it.key())) keys.append(it.key());
    for (const auto& name : keys) {
      const auto parts = name.split(',');
      const int x = parts.value(0).toInt(), y = parts.value(1).toInt();
      const QRectF footprint(x*kPhotoPaintTileEdge*double(output.width())/size.width(),
          y*kPhotoPaintTileEdge*double(output.height())/size.height(),
          kPhotoPaintTileEdge*double(output.width())/size.width(),
          kPhotoPaintTileEdge*double(output.height())/size.height());
      if (!footprint.adjusted(-2,-2,2,2).intersects(region)) continue;
      const auto patch = draft.contains(name) ? draft.value(name).value<QImage>() : tile(x,y,error);
      if (patch.isNull()) return {};
      painter.drawImage(tile_rect(x,y).topLeft(), patch);
    }
    painter.end();
    return image;
  }
};

inline QImage rasterize_photo_paint_layer(const QVariantMap& layer, QSize output = {}, QString* error = nullptr) {
  PhotoPaintSurface surface;
  if (!surface.load(layer,error)) return {};
  return surface.raster(output,error,layer.value("paintDraftTiles").toMap());
}

}  // namespace photo_editor::desktop
