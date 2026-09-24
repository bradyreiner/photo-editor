#pragma once

#include "photo_text_style.h"

#include <QGuiApplication>
#include <QColorSpace>
#include <QImage>
#include <QPainter>
#include <QTextLayout>
#include <QGlyphRun>
#include <algorithm>

namespace photo_editor::desktop {

struct PhotoTextLayoutResult final {
  QImage image;
  int line_count = 0;
  double content_height = 0;
  double content_width = 0;
  QRectF ink_bounds;
  bool overflow = false;
  QString error;
};

// Version-1 paragraph layout: top aligned, explicit padding, document-pixel metrics.
// Raster resolution changes sampling only; wrapping uses logical box dimensions.
inline PhotoTextLayoutResult rasterize_photo_text(
    QString text, const PhotoTextStyle& style, QSizeF box, QSize raster_size,
    Qt::Alignment alignment = Qt::AlignLeft, double padding = 0, QPointF origin = {}) {
  PhotoTextLayoutResult result;
  if (!decode_photo_text_style(encode_photo_text_style(style), &result.error)) return result;
  if (!std::isfinite(box.width()) || !std::isfinite(box.height()) ||
      !std::isfinite(origin.x()) || !std::isfinite(origin.y()) ||
      std::abs(origin.x()) > 1048576 || std::abs(origin.y()) > 1048576 ||
      !std::isfinite(padding) || padding < 0 || box.width() <= 2 * padding ||
      box.height() <= 2 * padding || box.width() > 1048576 || box.height() > 1048576 ||
      raster_size.width() <= 0 || raster_size.height() <= 0 ||
      raster_size.width() > 16384 || raster_size.height() > 16384 ||
      static_cast<qint64>(raster_size.width()) * raster_size.height() > 32LL * 1024 * 1024 ||
      text.size() > 1048576) {
    result.error = QStringLiteral("Text layout exceeds the supported dimensions or memory budget.");
    return result;
  }
  if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
    result.error = QStringLiteral("Text rendering requires an initialized font runtime.");
    return result;
  }
  result.image = QImage(raster_size, QImage::Format_RGBA64);
  if (result.image.isNull()) {
    result.error = QStringLiteral("Could not allocate the text raster.");
    return result;
  }
  // Fix logical DPI so point sizes remain document pixels on all host displays.
  result.image.setColorSpace(QColorSpace(QColorSpace::SRgb));
  result.image.setDotsPerMeterX(3780);
  result.image.setDotsPerMeterY(3780);
  result.image.fill(Qt::transparent);
  QFont font(style.family);
  if (!style.face.isEmpty()) font.setStyleName(style.face);
  font.setPointSizeF(style.size * 72.0 / result.image.logicalDpiY());
  font.setWeight(static_cast<QFont::Weight>(style.weight));
  font.setItalic(style.italic);
  font.setUnderline(style.underline);
  font.setLetterSpacing(QFont::AbsoluteSpacing, style.tracking);
  font.setHintingPreference(QFont::PreferNoHinting);
  QPainter painter(&result.image);
  painter.setRenderHint(QPainter::TextAntialiasing);
  painter.scale(raster_size.width() / box.width(), raster_size.height() / box.height());
  auto ink_color = style.color;
  ink_color.setAlpha(255);
  painter.setPen(ink_color);
  painter.setClipRect(QRectF(0, 0, box.width(), box.height()));
  QTextOption option;
  option.setWrapMode(style.point_text ? QTextOption::NoWrap : QTextOption::WrapAtWordBoundaryOrAnywhere);
  option.setAlignment(alignment);
  text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  text.replace(QChar('\r'), QChar('\n'));
  text.replace(QChar(0x2029), QChar('\n'));
  const auto paragraphs = text.split(QChar('\n'), Qt::KeepEmptyParts);
  double y = padding + origin.y();
  bool first_paragraph = true;
  for (const auto& paragraph : paragraphs) {
    if (!first_paragraph) y += style.paragraph_spacing;
    first_paragraph = false;
    QTextLayout layout(paragraph, font, &result.image);
    layout.setTextOption(option);
    layout.beginLayout();
    bool has_line = false;
    while (true) {
      auto line = layout.createLine();
      if (!line.isValid()) break;
      has_line = true;
      line.setLineWidth(box.width() - 2 * padding - origin.x());
      line.setPosition(QPointF(padding + origin.x(), y - style.baseline_offset));
      result.content_width = std::max(result.content_width, line.naturalTextWidth());
      result.overflow = result.overflow || y - style.baseline_offset < 0 ||
          y - style.baseline_offset + line.height() > box.height() - padding ||
          line.naturalTextWidth() > box.width() - 2 * padding;
      y += style.size * style.line_height;
      ++result.line_count;
    }
    layout.endLayout();
    for (const auto& run : layout.glyphRuns()) result.ink_bounds = result.ink_bounds.united(run.boundingRect());
    layout.draw(&painter, QPointF(0, 0));
    if (!has_line) {
      y += style.size * style.line_height;
      ++result.line_count;
    }
  }
  if (style.color.alpha() < 255) {
    painter.resetTransform();
    painter.setClipping(false);
    painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    painter.fillRect(result.image.rect(), QColor(0, 0, 0, style.color.alpha()));
  }
  result.content_height = y + padding;
  result.overflow = result.overflow || result.content_height > box.height();
  return result;
}

struct PhotoPointTextGeometry final {
  QSize size;
  QPointF origin;
  QString error;
};

// Measure in document pixels, including actual glyph overhangs. The origin keeps
// the text layout anchor separate from its fitted raster's upper-left corner.
inline PhotoPointTextGeometry measure_photo_point_text(const QString& text, const PhotoTextStyle& style) {
  auto point_style = style;
  point_style.point_text = true;
  const auto measured = rasterize_photo_text(text, point_style, {8192, 8192}, {1, 1});
  if (!measured.error.isEmpty()) return {{}, {}, measured.error};
  const auto left = std::floor(std::min(0.0, measured.ink_bounds.left())) - 1;
  const auto top = std::floor(std::min(0.0, measured.ink_bounds.top())) - 1;
  const auto right = std::ceil(std::max(measured.content_width, measured.ink_bounds.right())) + 1;
  const auto bottom = std::ceil(std::max(measured.content_height, measured.ink_bounds.bottom())) + 1;
  if (right - left > 8192 || bottom - top > 8192 || (right - left) * (bottom - top) > 32LL * 1024 * 1024)
    return {{}, {}, QStringLiteral("Point text exceeds the supported dimensions. Use a paragraph box or smaller type.")};
  return {QSize(std::max(16, static_cast<int>(right - left)), std::max(16, static_cast<int>(bottom - top))), {-left, -top}, {}};
}

}  // namespace photo_editor::desktop
