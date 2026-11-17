#pragma once

#include "photo_paint_format.h"
#include "qimage_color_conversion.h"
#include <QColorSpace>
#include <QImage>
#include <QPainter>
#include <QVariantMap>
#include <QtEndian>
#include <QtGui/qrgbafloat.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace photo_editor::desktop {

inline QVariantMap empty_photo_paint_tiles(const QColorSpace& space) {
  return {{"version", 1}, {"format", QLatin1String(kPhotoPaintFormat)},
      {"tileSize", kPhotoPaintTileEdge}, {"icc", QString::fromLatin1(space.iccProfile().toBase64())},
      {"tiles", QVariantMap{}}};
}

inline QVariantMap photo_paint_target_identity(QVariantMap layer, bool include_content = true) {
  for (auto it=layer.begin();it!=layer.end();) {
    if (it.key().startsWith(QStringLiteral("thumbnail"),Qt::CaseInsensitive) ||
        it.key() == QStringLiteral("displaySubtitle") || it.key() == QStringLiteral("kindLabel") ||
        it.key() == QStringLiteral("layerAuditLabel")) it=layer.erase(it);
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
      image = convert_tagged_qimage_color_space(legacy.copy(rect), color_space,
          QImage::Format_RGBA32FPx4_Premultiplied,
          QImageColorConversionPurpose::DecodeNormalization);
    } else {
      image = QImage(rect.size(), QImage::Format_RGBA32FPx4_Premultiplied);
      image.fill(Qt::transparent);
    }
    image.setColorSpace(color_space);
    return image;
  }

  static QString encode_tile(const QImage& image) {
    QByteArray raw(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes());
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    for (qsizetype i = 0; i < raw.size(); i += 4) {
      quint32 value; std::memcpy(&value, raw.constData() + i, 4);
      qToLittleEndian(value, raw.data() + i);
    }
#endif
    return QString::fromLatin1(qCompress(raw, 1).toBase64());
  }
  void store(int x, int y, const QImage& image) {
    tiles.insert(key(x,y), encode_tile(image));
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
    if (output != size)
      return filtered_raster_region(output, region, error, draft);
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
      const auto base = convert_tagged_qimage_color_space(legacy, color_space,
          QImage::Format_RGBA32FPx4_Premultiplied,
          QImageColorConversionPurpose::DecodeNormalization);
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

 private:
  struct SampleKernel final {
    int first = 0;
    std::vector<double> weights;
  };

  static SampleKernel sample_kernel(int destination, int source_extent, int output_extent) {
    SampleKernel kernel;
    if (destination < 0 || destination >= output_extent) return kernel;
    const auto scale = static_cast<double>(source_extent) / output_extent;
    if (scale > 1.0) {
      const auto begin = static_cast<double>(destination) * scale;
      const auto end = static_cast<double>(destination + 1) * scale;
      kernel.first = std::max(0, static_cast<int>(std::floor(begin)));
      const auto last = std::min(source_extent, static_cast<int>(std::ceil(end)));
      kernel.weights.reserve(static_cast<std::size_t>(last - kernel.first));
      for (auto source = kernel.first; source < last; ++source)
        kernel.weights.push_back(std::max(0.0,
            std::min(end, static_cast<double>(source + 1)) -
            std::max(begin, static_cast<double>(source))) / scale);
    } else {
      const auto center = (static_cast<double>(destination) + 0.5) * scale - 0.5;
      const auto first = static_cast<int>(std::floor(center));
      kernel.first = std::clamp(first, 0, source_extent - 1);
      const auto last = std::clamp(first + 1, 0, source_extent - 1);
      if (last == kernel.first) kernel.weights = {1.0};
      else kernel.weights = {1.0 - (center - first), center - first};
    }
    return kernel;
  }

  QImage filtered_raster_region(QSize output, QRect region, QString* error,
      const QVariantMap& draft) const {
    QImage image(region.size(), QImage::Format_RGBA32FPx4_Premultiplied);
    if (image.isNull()) {
      if (error) *error = QStringLiteral("Paint raster allocation failed.");
      return {};
    }
    image.setColorSpace(color_space);
    image.fill(Qt::transparent);
    // One source-pixel footprint for every output pixel, independent of tile
    // boundaries and regional rendering. QPainter's smooth image transform is
    // bilinear when minifying; it can miss painted/erased pixels entirely.
    // Integrate premultiplied linear RGBA instead, so transparent holes, signed
    // colors and HDR energy survive thumbnail and preview reductions alike.
    std::vector<SampleKernel> horizontal, vertical;
    horizontal.reserve(static_cast<std::size_t>(region.width()));
    vertical.reserve(static_cast<std::size_t>(region.height()));
    for (int x = region.left(); x <= region.right(); ++x)
      horizontal.push_back(sample_kernel(x, size.width(), output.width()));
    for (int y = region.top(); y <= region.bottom(); ++y)
      vertical.push_back(sample_kernel(y, size.height(), output.height()));

    auto names = tiles.keys();
    for (auto it = draft.cbegin(); it != draft.cend(); ++it)
      if (!tiles.contains(it.key())) names.append(it.key());
    if (!legacy.isNull()) {
      // Legacy pixels fill only tiles which have no authoritative replacement.
      const auto source_left = std::max(0, static_cast<int>(std::floor(
          static_cast<double>(region.left()) * size.width() / output.width())) - 1);
      const auto source_top = std::max(0, static_cast<int>(std::floor(
          static_cast<double>(region.top()) * size.height() / output.height())) - 1);
      const auto source_right = std::min(size.width(), static_cast<int>(std::ceil(
          static_cast<double>(region.right() + 1) * size.width() / output.width())) + 1);
      const auto source_bottom = std::min(size.height(), static_cast<int>(std::ceil(
          static_cast<double>(region.bottom() + 1) * size.height() / output.height())) + 1);
      for (int y = source_top / kPhotoPaintTileEdge; y * kPhotoPaintTileEdge < source_bottom; ++y)
        for (int x = source_left / kPhotoPaintTileEdge; x * kPhotoPaintTileEdge < source_right; ++x) {
          const auto name = key(x, y);
          if (!tiles.contains(name) && !draft.contains(name)) names.append(name);
        }
    }
    const auto margin_x = static_cast<int>(std::ceil(std::max(
        1.0, 0.5 * output.width() / size.width()))) + 1;
    const auto margin_y = static_cast<int>(std::ceil(std::max(
        1.0, 0.5 * output.height() / size.height()))) + 1;
    for (const auto& name : names) {
      const auto parts = name.split(',');
      const auto tx = parts.value(0).toInt(), ty = parts.value(1).toInt();
      const auto source_rect = tile_rect(tx, ty);
      const auto left = static_cast<int>(std::floor(
          source_rect.left() * static_cast<double>(output.width()) / size.width())) - margin_x;
      const auto top = static_cast<int>(std::floor(
          source_rect.top() * static_cast<double>(output.height()) / size.height())) - margin_y;
      const auto right = static_cast<int>(std::ceil(
          (source_rect.right() + 1) * static_cast<double>(output.width()) / size.width())) + margin_x;
      const auto bottom = static_cast<int>(std::ceil(
          (source_rect.bottom() + 1) * static_cast<double>(output.height()) / size.height())) + margin_y;
      const auto destination = QRect(QPoint(left, top), QPoint(right, bottom))
          .intersected(region).intersected(QRect(QPoint(), output));
      if (destination.isEmpty()) continue;
      auto patch = draft.contains(name) ? draft.value(name).value<QImage>() : tile(tx, ty, error);
      if (patch.isNull() || patch.size() != source_rect.size()) {
        if (error && error->isEmpty()) *error = QStringLiteral("Invalid paint draft tile.");
        return {};
      }
      if (patch.format() != QImage::Format_RGBA32FPx4_Premultiplied)
        patch = patch.convertToFormat(QImage::Format_RGBA32FPx4_Premultiplied);
      for (int y = destination.top(); y <= destination.bottom(); ++y) {
        const auto& ky = vertical[static_cast<std::size_t>(y - region.top())];
        auto* output_row = reinterpret_cast<QRgbaFloat32*>(image.scanLine(y - region.top()));
        for (int x = destination.left(); x <= destination.right(); ++x) {
          const auto& kx = horizontal[static_cast<std::size_t>(x - region.left())];
          double red = 0, green = 0, blue = 0, alpha = 0;
          const auto y_begin = std::max(ky.first, source_rect.top());
          const auto y_end = std::min(ky.first + static_cast<int>(ky.weights.size()), source_rect.bottom() + 1);
          const auto x_begin = std::max(kx.first, source_rect.left());
          const auto x_end = std::min(kx.first + static_cast<int>(kx.weights.size()), source_rect.right() + 1);
          for (int sy = y_begin; sy < y_end; ++sy) {
            const auto* row = reinterpret_cast<const QRgbaFloat32*>(patch.constScanLine(sy - source_rect.top()));
            const auto wy = ky.weights[static_cast<std::size_t>(sy - ky.first)];
            for (int sx = x_begin; sx < x_end; ++sx) {
              const auto weight = wy * kx.weights[static_cast<std::size_t>(sx - kx.first)];
              const auto& pixel = row[sx - source_rect.left()];
              red += weight * pixel.red(); green += weight * pixel.green();
              blue += weight * pixel.blue(); alpha += weight * pixel.alpha();
            }
          }
          auto& pixel = output_row[x - region.left()];
          pixel = QRgbaFloat32{pixel.red() + static_cast<float>(red),
              pixel.green() + static_cast<float>(green), pixel.blue() + static_cast<float>(blue),
              pixel.alpha() + static_cast<float>(alpha)};
        }
      }
    }
    return image;
  }
};

inline QImage rasterize_photo_paint_layer(const QVariantMap& layer, QSize output = {}, QString* error = nullptr) {
  PhotoPaintSurface surface;
  if (!surface.load(layer,error)) return {};
  return surface.raster(output,error,layer.value("paintDraftTiles").toMap());
}

}  // namespace photo_editor::desktop
