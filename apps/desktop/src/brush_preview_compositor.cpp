#include "brush_preview_compositor.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace photo_editor::desktop {

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
std::unique_ptr<BrushPreviewCompositor> create_metal_brush_preview_compositor();
#endif

namespace {

double clamp_unit(double value) {
  return std::clamp(value, 0.0, 1.0);
}

double smoothstep(double edge0, double edge1, double x) {
  if (edge0 >= edge1) {
    return x < edge0 ? 0.0 : 1.0;
  }
  const auto t = clamp_unit((x - edge0) / (edge1 - edge0));
  return t * t * (3.0 - 2.0 * t);
}

class CpuBrushPreviewCompositor final : public BrushPreviewCompositor {
 public:
  [[nodiscard]] bool available() const override { return true; }
  [[nodiscard]] const char* backendName() const override { return "cpu"; }

  bool ensureTarget(QImage* image, const QSize& size) const override {
    if (image == nullptr || !size.isValid() || size.width() <= 1 || size.height() <= 1) {
      return false;
    }
    if (image->size() == size && image->format() == QImage::Format_ARGB32_Premultiplied) {
      return true;
    }
    *image = QImage(size, QImage::Format_ARGB32_Premultiplied);
    image->fill(Qt::transparent);
    return !image->isNull();
  }

  void clear(QImage* image) const override {
    if (image == nullptr || image->isNull()) {
      return;
    }
    image->fill(Qt::transparent);
  }

  void compositePoints(
      QImage* image,
      const QVector<BrushPreviewPoint>& points,
      const BrushPreviewStyle& style) const override {
    if (image == nullptr || image->isNull() || points.isEmpty()) {
      return;
    }

    const auto base_red = style.color.redF();
    const auto base_green = style.color.greenF();
    const auto base_blue = style.color.blueF();
    const auto style_opacity = clamp_unit(style.opacity);
    const auto softness = std::clamp(style.feather, 0.05, 1.0);
    const auto inner_scale = std::max(0.0, 1.0 - softness);

    for (const auto& point : points) {
      const auto clamped_size = std::max(0.01, point.size);
      const auto clamped_flow = clamp_unit(point.flow);
      const auto center_x =
          clamp_unit(point.x) * static_cast<double>(std::max(image->width() - 1, 1));
      const auto center_y =
          clamp_unit(point.y) * static_cast<double>(std::max(image->height() - 1, 1));
      const auto radius_x = std::max(
          2,
          static_cast<int>(std::ceil(clamped_size * static_cast<double>(std::max(image->width() - 1, 1)))));
      const auto radius_y = std::max(
          2,
          static_cast<int>(std::ceil(clamped_size * static_cast<double>(std::max(image->height() - 1, 1)))));
      const auto min_x = std::max(0, static_cast<int>(std::floor(center_x)) - radius_x);
      const auto max_x = std::min(image->width() - 1, static_cast<int>(std::ceil(center_x)) + radius_x);
      const auto min_y = std::max(0, static_cast<int>(std::floor(center_y)) - radius_y);
      const auto max_y = std::min(image->height() - 1, static_cast<int>(std::ceil(center_y)) + radius_y);

      for (int y = min_y; y <= max_y; ++y) {
        auto* scanline = reinterpret_cast<QRgb*>(image->scanLine(y));
        const auto pixel_y =
            image->height() > 1 ? static_cast<double>(y) / static_cast<double>(image->height() - 1) : 0.5;
        for (int x = min_x; x <= max_x; ++x) {
          const auto pixel_x =
              image->width() > 1 ? static_cast<double>(x) / static_cast<double>(image->width() - 1) : 0.5;
          const auto delta_x = pixel_x - point.x;
          const auto delta_y = pixel_y - point.y;
          const auto distance = std::sqrt(delta_x * delta_x + delta_y * delta_y) / clamped_size;
          const auto weight = clamped_flow * (1.0 - smoothstep(inner_scale, 1.0, distance));
          const auto src_alpha = clamp_unit(weight * style_opacity);
          if (src_alpha <= 1e-6) {
            continue;
          }

          const auto destination = QColor::fromRgba(scanline[x]);
          const auto dst_alpha = destination.alphaF();
          const auto out_alpha = src_alpha + dst_alpha * (1.0 - src_alpha);
          if (out_alpha <= 1e-6) {
            continue;
          }

          const auto out_red =
              (base_red * src_alpha + destination.redF() * dst_alpha * (1.0 - src_alpha)) / out_alpha;
          const auto out_green =
              (base_green * src_alpha + destination.greenF() * dst_alpha * (1.0 - src_alpha)) / out_alpha;
          const auto out_blue =
              (base_blue * src_alpha + destination.blueF() * dst_alpha * (1.0 - src_alpha)) / out_alpha;
          scanline[x] = qRgba(
              qBound(0, qRound(out_red * 255.0), 255),
              qBound(0, qRound(out_green * 255.0), 255),
              qBound(0, qRound(out_blue * 255.0), 255),
              qBound(0, qRound(out_alpha * 255.0), 255));
        }
      }
    }
  }
};

}  // namespace

std::unique_ptr<BrushPreviewCompositor> BrushPreviewCompositor::create() {
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  if (auto metal = create_metal_brush_preview_compositor();
      metal != nullptr && metal->available()) {
    return metal;
  }
#endif
  return std::make_unique<CpuBrushPreviewCompositor>();
}

}  // namespace photo_editor::desktop
