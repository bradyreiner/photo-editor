#include "brush_rasterizer.h"
#include "brush_live_preview_raster.h"
#include <QElapsedTimer>

#include <atomic>
#include <cstdlib>
#include <iostream>

#include <QColor>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QVariantMap zero_mask(int width, int height) {
  QVariantMap mask;
  mask.insert(QStringLiteral("segmentationData"), QByteArray(width * height, char(0)));
  mask.insert(QStringLiteral("segmentationWidth"), width);
  mask.insert(QStringLiteral("segmentationHeight"), height);
  return mask;
}

unsigned char alpha_at(const QVariantMap& mask, int x, int y) {
  const auto width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto data = mask.value(QStringLiteral("segmentationData")).toByteArray();
  return static_cast<unsigned char>(data.at(y * width + x));
}

}  // namespace

int main() {
  using namespace photo_editor::desktop;

  bool cancelled = true;
  QVariantMap invalid;
  if (rasterize_brush_point_into_mask(
          nullptr, nullptr, 0.5, 0.5, 0.25, 1.0, 1.0, 0.05)
          .isValid() ||
      rasterize_brush_point_into_mask(
          &invalid, nullptr, 0.5, 0.5, 0.25, 1.0, 1.0, 0.05, false,
          false, 0.18, -1.0, -1.0, false, {}, &cancelled)
          .isValid() ||
      cancelled) {
    return fail("brush rasterization must reject absent payloads and normalize cancellation output");
  }

  auto mask = zero_mask(9, 9);
  const auto painted = rasterize_brush_point_into_mask(
      &mask, nullptr, 0.5, 0.5, 0.25, 1.0, 1.0, 0.05);
  if (painted != QRect(3, 3, 3, 3) || alpha_at(mask, 4, 4) != 255U ||
      alpha_at(mask, 3, 3) != 255U || alpha_at(mask, 2, 4) != 0U ||
      alpha_at(mask, 0, 0) != 0U) {
    return fail("ordinary brush rasterization must publish the exact bounded opaque dab");
  }
  const auto erased = rasterize_brush_point_into_mask(
      &mask, nullptr, 0.5, 0.5, 0.25, 0.5, 1.0, 0.05, true);
  if (erased != QRect(3, 3, 3, 3) || alpha_at(mask, 4, 4) != 128U) {
    return fail("erase rasterization must attenuate existing alpha with the exact flow weight");
  }

  QImage split(9, 9, QImage::Format_ARGB32);
  for (int y = 0; y < split.height(); ++y) {
    for (int x = 0; x < split.width(); ++x) {
      split.setPixelColor(x, y, x < 5 ? QColor(220, 35, 25) : QColor(25, 45, 220));
    }
  }
  auto auto_mask = zero_mask(9, 9);
  qint64 candidates = 0;
  qint64 edge_evaluations = 0;
  const auto auto_changed = rasterize_brush_point_into_mask(
      &auto_mask, &split, 0.5, 0.5, 0.5, 1.0, 1.0, 0.05, false, true,
      0.05, 0.25, 0.5, true, {}, nullptr, &candidates, &edge_evaluations);
  if (!auto_changed.isValid() || alpha_at(auto_mask, 3, 4) == 0U ||
      alpha_at(auto_mask, 6, 4) != 0U || candidates <= 0 ||
      edge_evaluations <= 0 || candidates <= edge_evaluations) {
    return fail("auto-mask rasterization must admit matching color and reject the split region");
  }

  std::atomic<int> latest_generation{2};
  auto cancelled_mask = zero_mask(9, 9);
  cancelled = false;
  const auto cancelled_rect = rasterize_brush_point_into_mask(
      &cancelled_mask, nullptr, 0.5, 0.5, 0.25, 1.0, 1.0, 0.05,
      false, false, 0.18, -1.0, -1.0, false,
      photo_editor::pipeline::CancellationToken{&latest_generation, 1},
      &cancelled);
  if (cancelled_rect.isValid() || !cancelled || alpha_at(cancelled_mask, 4, 4) != 0U) {
    return fail("cancelled brush rasterization must publish no partial mask mutation");
  }


  // Consecutive pointer batches must preserve earlier segments and their exact
  // alpha. The old preview recreated an empty mask for every batch.
  BrushLivePreviewRaster preview;
  const QSize preview_size(2560, 1280);
  const BrushPreviewStyle preview_style{.color = QColor(210, 85, 30), .feather = 0.5,
      .hardness = 0.25, .opacity = 0.4};
  const BrushPreviewPoint first{.x = 0.2, .y = 0.3, .size = 0.012, .flow = 0.5};
  const BrushPreviewPoint second{.x = 0.7, .y = 0.6, .size = 0.012, .flow = 0.8};
  preview.append(preview_size, {first}, preview_style);
  const auto first_pixel = preview.image.pixel(512, 384);
  const auto* allocation = preview.alpha.constData();
  const auto dirty = preview.append(preview_size, {second}, preview_style);
  if (preview.image.pixel(512, 384) != first_pixel || qAlpha(first_pixel) == 0 ||
      preview.alpha.constData() != allocation || dirty.width() >= 100 || dirty.height() >= 100)
    return fail("Live brush batches must retain earlier coverage and update only the new footprint");
  const auto pixel = preview.image.pixel(1791, 767);
  if (qRed(pixel) > qAlpha(pixel) || qGreen(pixel) > qAlpha(pixel) || qBlue(pixel) > qAlpha(pixel))
    return fail("Live brush output must contain premultiplied RGB");
  auto reference = zero_mask(preview_size.width(), preview_size.height());
  for (const auto& point : {first, second})
    (void)rasterize_brush_point_into_mask(&reference, nullptr, point.x, point.y,
        point.size, point.flow, point.opacity, preview_style.feather);
  if (preview.alpha != reference.value("segmentationData").toByteArray())
    return fail("Incremental live coverage must match committed brush rasterization");
  auto hidden_style = preview_style;
  hidden_style.opacity = 0;
  preview.append(preview_size, {}, hidden_style);
  if (qAlpha(preview.image.pixel(512, 384)) != 0 || qAlpha(preview.image.pixel(1791, 767)) != 0)
    return fail("Changing overlay mode must also restyle earlier segments");
  preview.append(preview_size, {}, preview_style);
  if (preview.image.pixel(512, 384) != first_pixel)
    return fail("Restoring overlay visibility must retain the complete stroke");

  // Compare warm incremental shading with the previous full-frame scan. Include
  // an image snapshot as the UI provider would retain, so COW costs are included.
  QElapsedTimer clock;
  clock.start();
  for (int i = 0; i < 80; ++i) {
    const auto snapshot = preview.image;
    auto point = first;
    point.x = 0.1 + static_cast<double>(i) * 0.009;
    point.y = 0.8;
    preview.append(preview_size, {point}, preview_style);
    if (snapshot.isNull()) return fail("Preview snapshot should remain valid");
  }
  const auto incremental_ns = clock.nsecsElapsed();
  clock.restart();
  QImage baseline(preview_size, QImage::Format_ARGB32_Premultiplied);
  for (int i = 0; i < 80; ++i) {
    auto point = first;
    point.x = 0.1 + static_cast<double>(i) * 0.009;
    point.y = 0.8;
    auto batch = zero_mask(preview_size.width(), preview_size.height());
    (void)rasterize_brush_point_into_mask(&batch, nullptr, point.x, point.y,
        point.size, point.flow, point.opacity, preview_style.feather);
    const auto alpha = batch.value("segmentationData").toByteArray();
    baseline.fill(Qt::transparent);
    for (int y = 0; y < baseline.height(); ++y) {
      auto* row = reinterpret_cast<QRgb*>(baseline.scanLine(y));
      for (int x = 0; x < baseline.width(); ++x) {
        const auto a = qRound(static_cast<unsigned char>(alpha.at(y * baseline.width() + x)) * preview_style.opacity);
        if (a) row[x] = qPremultiply(qRgba(210, 85, 30, a));
      }
    }
  }
  std::cout << "live_preview_80_batches_incremental_ms=" << incremental_ns / 1000000.0
            << " baseline_ms=" << clock.nsecsElapsed() / 1000000.0 << '\n';
  return EXIT_SUCCESS;
}
