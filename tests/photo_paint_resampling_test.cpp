#include "photo_paint_surface.h"

#include <QCoreApplication>
#include <QBuffer>
#include <QJsonDocument>
#include <QtGui/qrgbafloat.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

namespace {
using namespace photo_editor::desktop;

double error(const QRgbaFloat32& a, const QRgbaFloat32& b) {
  return std::max({std::abs(static_cast<double>(a.red() - b.red())),
      std::abs(static_cast<double>(a.green() - b.green())),
      std::abs(static_cast<double>(a.blue() - b.blue())),
      std::abs(static_cast<double>(a.alpha() - b.alpha()))});
}

QImage area_reference(const QImage& source, QSize output) {
  // Independent summed-area oracle over the assembled image. Fractional
  // rectangle integrals are bilinear samples of this prefix table, rather
  // than the production rasterizer's per-tile separable kernels.
  using Sum = std::array<double, 4>;
  const auto stride = static_cast<std::size_t>(source.width() + 1);
  std::vector<Sum> prefix(stride * static_cast<std::size_t>(source.height() + 1));
  for (int y = 0; y < source.height(); ++y) {
    const auto* row = reinterpret_cast<const QRgbaFloat32*>(source.constScanLine(y));
    for (int x = 0; x < source.width(); ++x) {
      const Sum pixel{row[x].red(), row[x].green(), row[x].blue(), row[x].alpha()};
      const auto index = static_cast<std::size_t>(y + 1) * stride + static_cast<std::size_t>(x + 1);
      for (std::size_t c = 0; c < 4; ++c)
        prefix[index][c] = pixel[c] + prefix[index - 1][c] + prefix[index - stride][c] - prefix[index - stride - 1][c];
    }
  }
  const auto integral = [&](double x, double y, std::size_t c) {
    const auto ix = std::clamp(static_cast<int>(std::floor(x)), 0, source.width());
    const auto iy = std::clamp(static_cast<int>(std::floor(y)), 0, source.height());
    const auto x1 = std::min(ix + 1, source.width()), y1 = std::min(iy + 1, source.height());
    const auto sample = [&](int sx, int sy) { return prefix[static_cast<std::size_t>(sy) * stride + static_cast<std::size_t>(sx)][c]; };
    return std::lerp(std::lerp(sample(ix, iy), sample(x1, iy), x - ix),
        std::lerp(sample(ix, y1), sample(x1, y1), x - ix), y - iy);
  };
  const QSize reduced(std::min(output.width(), source.width()), std::min(output.height(), source.height()));
  QImage reference(reduced, QImage::Format_RGBA32FPx4_Premultiplied);
  reference.setColorSpace(source.colorSpace());
  const auto sx = static_cast<double>(source.width()) / reduced.width();
  const auto sy = static_cast<double>(source.height()) / reduced.height();
  for (int y = 0; y < reduced.height(); ++y) {
    auto* row = reinterpret_cast<QRgbaFloat32*>(reference.scanLine(y));
    for (int x = 0; x < reduced.width(); ++x) {
      Sum sum{};
      for (std::size_t c = 0; c < 4; ++c)
        sum[c] = (integral((x + 1) * sx, (y + 1) * sy, c) - integral(x * sx, (y + 1) * sy, c) -
            integral((x + 1) * sx, y * sy, c) + integral(x * sx, y * sy, c)) / (sx * sy);
      row[x] = QRgbaFloat32{static_cast<float>(sum[0]), static_cast<float>(sum[1]),
          static_cast<float>(sum[2]), static_cast<float>(sum[3])};
    }
  }
  if (reduced == output) return reference;
  QImage expanded(output, reference.format());
  expanded.setColorSpace(reference.colorSpace());
  for (int y = 0; y < output.height(); ++y) {
    const auto py = std::clamp((y + 0.5) * reduced.height() / output.height() - 0.5,
        0.0, static_cast<double>(reduced.height() - 1));
    const auto y0 = static_cast<int>(std::floor(py)), y1 = std::min(y0 + 1, reduced.height() - 1);
    auto* row = reinterpret_cast<QRgbaFloat32*>(expanded.scanLine(y));
    for (int x = 0; x < output.width(); ++x) {
      const auto px = std::clamp((x + 0.5) * reduced.width() / output.width() - 0.5,
          0.0, static_cast<double>(reduced.width() - 1));
      const auto x0 = static_cast<int>(std::floor(px)), x1 = std::min(x0 + 1, reduced.width() - 1);
      Sum sum{};
      for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
        const auto syi = dy ? y1 : y0, sxi = dx ? x1 : x0;
        const auto& pixel = reinterpret_cast<const QRgbaFloat32*>(reference.constScanLine(syi))[sxi];
        const auto weight = (dx ? px - x0 : 1.0 - (px - x0)) *
            (dy ? py - y0 : 1.0 - (py - y0));
        sum[0] += weight * pixel.red(); sum[1] += weight * pixel.green();
        sum[2] += weight * pixel.blue(); sum[3] += weight * pixel.alpha();
      }
      row[x] = QRgbaFloat32{static_cast<float>(sum[0]), static_cast<float>(sum[1]),
          static_cast<float>(sum[2]), static_cast<float>(sum[3])};
    }
  }
  return expanded;
}

QVariantMap layer_fixture(bool legacy) {
  QVariantMap layer{{"kind", "pixel"}, {"pixelWidth", 515}, {"pixelHeight", 259},
      {"paintTiles", empty_photo_paint_tiles(QColorSpace(QColorSpace::SRgbLinear))}};
  if (legacy) {
    QImage image(515, 259, QImage::Format_ARGB32);
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    image.fill(QColor(40, 90, 170, 230));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    layer.insert("pixelData", bytes);
  }
  return layer;
}

bool verify(bool legacy, bool draft) {
  auto layer = layer_fixture(legacy);
  PhotoPaintSurface surface;
  if (!surface.load(layer)) return false;
  QVariantMap draft_tiles;
  for (int ty = 0; ty < 3; ++ty) {
    for (int tx = 0; tx < 5; ++tx) {
      // Missing tiles must retain legacy pixels or transparent padding.
      if (tx == 0 && ty == 0) continue;
      auto tile = surface.tile(tx, ty);
      for (int y = 0; y < tile.height(); ++y) {
        auto* row = reinterpret_cast<QRgbaFloat32*>(tile.scanLine(y));
        for (int x = 0; x < tile.width(); ++x) {
          const auto sx = tx * kPhotoPaintTileEdge + x;
          const auto sy = ty * kPhotoPaintTileEdge + y;
          // Fine cutout holes, partial erasing, and signed/HDR color across
          // exact and partial tile boundaries. Transparent RGB stays zero.
          const auto alpha = ((sx + sy) % 7 < 4 &&
              std::hypot(sx - 255.5, sy - 127.5) < 110.0) ? 0.8F : 0.0F;
          row[x] = QRgbaFloat32{-0.2F * alpha, 1.8F * alpha, 3.0F * alpha, alpha};
        }
      }
      if (draft) draft_tiles.insert(PhotoPaintSurface::key(tx, ty), tile);
      else surface.store(tx, ty, tile);
    }
  }
  const auto full = surface.raster({}, nullptr, draft_tiles);
  if (full.isNull()) return false;
  for (const auto size : {QSize(256, 128), QSize(128, 64), QSize(97, 49),
           QSize(9, 5), QSize(64, 388), QSize(773, 48), QSize(1030, 518), QSize(773, 388)}) {
    const auto actual = surface.raster(size, nullptr, draft_tiles);
    const auto reference = area_reference(full, size);
    if (actual.isNull() || actual.colorSpace() != surface.color_space) return false;
    double maximum = 0;
    for (int y = 0; y < size.height(); ++y) {
      const auto* a = reinterpret_cast<const QRgbaFloat32*>(actual.constScanLine(y));
      const auto* b = reinterpret_cast<const QRgbaFloat32*>(reference.constScanLine(y));
      for (int x = 0; x < size.width(); ++x) maximum = std::max(maximum, error(a[x], b[x]));
    }
    if (maximum > 2.0e-6) {
      std::cerr << "Paint area reduction differs from assembled source: "
                << size.width() << 'x' << size.height() << " error=" << maximum << '\n';
      return false;
    }
    for (const auto region : {QRect(3, 2, std::max(1, size.width() - 6), std::max(1, size.height() - 4)),
             QRect(-2, -1, size.width() / 2 + 3, size.height() / 2 + 2)}) {
      const auto partial = surface.raster_region(size, region, nullptr, draft_tiles);
      if (partial.isNull()) return false;
      for (int y = 0; y < partial.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgbaFloat32*>(partial.constScanLine(y));
        for (int x = 0; x < partial.width(); ++x) {
          const auto global = region.topLeft() + QPoint(x, y);
          const auto expected = actual.rect().contains(global)
              ? reinterpret_cast<const QRgbaFloat32*>(actual.constScanLine(global.y()))[global.x()]
              : QRgbaFloat32{0, 0, 0, 0};
          if (error(row[x], expected) > 1.0e-7) {
            std::cerr << "Regional paint sampling changed its global source footprint\n";
            return false;
          }
        }
      }
    }
  }
  if (!draft && !legacy) {
    layer.insert("paintTiles", surface.snapshot());
    const auto reopened_layer = QJsonDocument::fromJson(
        QJsonDocument::fromVariant(layer).toJson()).toVariant().toMap();
    PhotoPaintSurface reopened;
    if (!reopened.load(reopened_layer) || reopened.raster({97, 49}) != surface.raster({97, 49}))
      return false;
  }
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  for (bool legacy : {false, true}) for (bool draft : {false, true})
    if (!verify(legacy, draft)) return 1;
  std::cout << "Paint/erase area sampling preserves alpha, HDR, tile boundaries, draft/commit, "
               "legacy replacement, regional coordinates and reopen across eight resolutions.\n";
  return 0;
}
