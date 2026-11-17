#pragma once

#include <QBuffer>
#include <QImage>
#include <QRectF>
#include <QSet>
#include <QVariantList>
#include <QtMath>

namespace photo_editor::desktop {

inline QRectF board_photo_crop_bounds(const QVariantMap& layer, const QSizeF& size) {
  const auto crop = layer.value("boardPhotoCrop").toMap();
  if (crop.isEmpty()) return QRectF(QPointF(), size);
  return QRectF(crop.value("x").toDouble() * size.width(), crop.value("y").toDouble() * size.height(),
      crop.value("width").toDouble() * size.width(), crop.value("height").toDouble() * size.height());
}

inline QRect photo_board_alpha_bounds(const QImage& source) {
  const auto image = source.convertToFormat(QImage::Format_ARGB32);
  int left = image.width(), top = image.height(), right = -1, bottom = -1;
  for (int y = 0; y < image.height(); ++y) {
    const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
    for (int x = 0; x < image.width(); ++x) {
      if (qAlpha(row[x]) == 0) continue;
      left = qMin(left, x); right = qMax(right, x);
      top = qMin(top, y); bottom = qMax(bottom, y);
    }
  }
  return right < left ? QRect{} : QRect(QPoint(left, top), QPoint(right, bottom));
}

// Layout is atomic: validate every target before handing one new layer stack to
// the ordinary editor history/render/persistence path. Never resize source pixels.
inline bool layout_board_photos(QVariantList* layers, const QVariantList& selected,
                                const QString& preset, const QSize& canvas,
                                const QVariantMap& options = {}) {
  if (!layers || !canvas.isValid()) return false;
  const bool multiple = preset == "stack" || preset == "columns" || preset == "grid";
  if ((!multiple && preset != "fit" && preset != "fill") ||
      (multiple ? selected.size() < 2 : selected.size() != 1)) return false;
  const double margin = options.value("margin", 0).toDouble();
  const double spacing = options.value("spacing", 0).toDouble();
  const double border = options.value("border", 0).toDouble();
  if (!qIsFinite(margin) || !qIsFinite(spacing) || !qIsFinite(border) ||
      margin < 0 || spacing < 0 || border < 0) return false;
  const int count = static_cast<int>(selected.size());
  const int columns = preset == "columns" ? count : preset == "grid" ? qCeil(qSqrt(count)) : 1;
  const int rows = preset == "stack" ? count : preset == "grid" ? (count + columns - 1) / columns : 1;
  const QRectF canvas_frame(options.value("canvasX", 0).toDouble(), options.value("canvasY", 0).toDouble(),
      options.value("canvasWidth", canvas.width()).toDouble(), options.value("canvasHeight", canvas.height()).toDouble());
  if (!canvas_frame.isValid() || !QRectF(QPointF(), canvas).contains(canvas_frame)) return false;
  const double outer = margin + border;
  const double gutter = spacing + border;
  const double cell_width = (canvas_frame.width() - 2 * outer - (columns - 1) * gutter) / columns;
  const double cell_height = (canvas_frame.height() - 2 * outer - (rows - 1) * gutter) / rows;
  if (cell_width <= 0 || cell_height <= 0) return false;
  auto result = *layers;
  QSet<int> seen;
  for (qsizetype slot = 0; slot < selected.size(); ++slot) {
    bool valid = false;
    const int index = selected.at(slot).toInt(&valid);
    if (!valid || index < 0 || index >= layers->size() || seen.contains(index)) return false;
    seen.insert(index);
    auto layer = layers->at(index).toMap();
    const auto kind = layer.value("kind").toString();
    if ((kind != "pixel" && kind != "source") || layer.value("locked").toBool() ||
        !layer.value("enabled", true).toBool() || !layer.value("parentId").toString().isEmpty()) return false;
    const auto has_content = [&layer](const char* key) {
      const auto value = layer.value(QLatin1String(key));
      return !value.toString().isEmpty() || !value.toList().isEmpty() || !value.toMap().isEmpty();
    };

    // Legacy Board graphics were full-canvas PNGs. Remove transparent padding
    // only for plain PNG layers; masks, HDR paint tiles and effects retain their
    // original coordinate basis and data.
    if (kind == "pixel" && !layer.contains("paintTiles") &&
        !layer.value("hasLayerMask").toBool() && !layer.contains("layerMask") &&
        !has_content("maskStack") && !has_content("maskSets") &&
        !has_content("effects") && !has_content("effectStack") &&
        !layer.contains("vectorMask") && !layer.contains("maskData") && !layer.contains("layerStyleStack")) {
      const auto image = QImage::fromData(layer.value("pixelData").toByteArray(), "PNG");
      const auto bounds = photo_board_alpha_bounds(image);
      if (bounds.isEmpty()) return false;
      if (bounds != image.rect()) {
        QByteArray pixels;
        QBuffer output(&pixels);
        if (!output.open(QIODevice::WriteOnly) || !image.copy(bounds).save(&output, "PNG")) return false;
        layer.insert("pixelData", pixels);
        layer.insert("pixelWidth", bounds.width());
        layer.insert("pixelHeight", bounds.height());
      }
    }
    const auto width = layer.value("pixelWidth").toDouble();
    const auto height = layer.value("pixelHeight").toDouble();
    if (!(width > 0 && height > 0)) return false;
    const int column = static_cast<int>(slot) % columns;
    const int row = static_cast<int>(slot) / columns;
    QRectF frame(canvas_frame.x() + outer + column * (cell_width + gutter),
                 canvas_frame.y() + outer + row * (cell_height + gutter), cell_width, cell_height);
    const auto scale = preset != "fit" ? qMax(frame.width() / width, frame.height() / height)
                                         : qMin(frame.width() / width, frame.height() / height);
    // Match the transform model's supported scale range without silently clipping.
    if (!qIsFinite(scale) || scale < 0.05 || scale > 32.0) return false;
    layer.insert("scale", scale);
    layer.insert("rotation", 0.0);
    layer.remove("boardPhotoFraming");
    layer.insert("x", qRound(frame.center().x() - width * scale / 2));
    layer.insert("y", qRound(frame.center().y() - height * scale / 2));
    if (preset == "fit") layer.remove("boardPhotoCrop");
    else {
      const auto crop_width = qMin(1.0, frame.width() / (width * scale));
      const auto crop_height = qMin(1.0, frame.height() / (height * scale));
      layer.insert("boardPhotoCrop", QVariantMap{{"x", (1 - crop_width) / 2}, {"y", (1 - crop_height) / 2},
          {"width", crop_width}, {"height", crop_height}});
    }
    auto settings = options;
    settings.insert("preset", preset);
    settings.insert("layoutVersion", 2);
    layer.insert("boardPhotoLayoutSettings", settings);
    result[index] = layer;
  }
  *layers = result;
  return true;
}

}  // namespace photo_editor::desktop
