#pragma once

#include <QColor>
#include <QImage>
#include <QSize>
#include <QVector>

#include <memory>

namespace photo_editor::desktop {

struct BrushPreviewPoint final {
  double x = 0.5;
  double y = 0.5;
  double size = 0.05;
  double flow = 1.0;
  double opacity = 1.0;
  double auto_mask_anchor_x = 0.5;
  double auto_mask_anchor_y = 0.5;
  double pressure = 1.0;
  double tilt_x = 0.0;
  double tilt_y = 0.0;
  bool erase = false;
};

struct BrushPreviewStyle final {
  QColor color = QColor(94, 149, 211);
  double feather = 0.5;
  double hardness = 0.25;
  double opacity = 0.38;
  bool circular = false;
};

class BrushPreviewCompositor {
 public:
  virtual ~BrushPreviewCompositor() = default;

  [[nodiscard]] virtual bool available() const = 0;
  [[nodiscard]] virtual const char* backendName() const = 0;
  virtual bool ensureTarget(QImage* image, const QSize& size) const = 0;
  virtual void clear(QImage* image) const = 0;
  virtual void compositePoints(
      QImage* image,
      const QVector<BrushPreviewPoint>& points,
      const BrushPreviewStyle& style) const = 0;
  [[nodiscard]] virtual bool hasNativeTexture() const { return false; }
  [[nodiscard]] virtual quint64 nativeTextureHandle() const { return 0; }
  [[nodiscard]] virtual QSize nativeTextureSize() const { return {}; }

  [[nodiscard]] static std::unique_ptr<BrushPreviewCompositor> create();
};

}  // namespace photo_editor::desktop
