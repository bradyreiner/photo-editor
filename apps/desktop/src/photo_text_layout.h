#pragma once

#include "photo_text_style.h"

#include <QGuiApplication>
#include <QColorSpace>
#include <QImage>
#include <QPainter>
#include <QTextLayout>
#include <QTextCharFormat>
#include <QGlyphRun>
#include <QPainterPath>
#include <QFontMetricsF>
#include <QRawFont>
#include <QTransform>
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

// Keep the path used for glyph placement available to analytic geometry tests
// and other canvas consumers, so the measured curve is the rendered curve.
inline QPainterPath build_photo_text_baseline(const PhotoTextStyle& style, QSizeF box) {
  QVector<QPointF> points;
  points.reserve(style.path_points.size());
  for (const auto& point : style.path_points)
    points.push_back(QPointF(point.x() * box.width(), point.y() * box.height()));
  if (style.path_closed && points.size() > 2 && QLineF(points.first(), points.last()).length() < 0.01)
    points.removeLast();
  if (points.size() < 2) return {};

  QPainterPath baseline;
  baseline.moveTo(points.first());
  const auto count = static_cast<int>(points.size());
  const auto segments = style.path_closed ? count : count - 1;
  for (int i = 0; i < segments; ++i) {
    const auto p1 = points.at(i % count);
    const auto p2 = points.at((i + 1) % count);
    const auto p0 = style.path_closed ? points.at((i - 1 + count) % count) : points.at(std::max(0, i - 1));
    const auto p3 = style.path_closed ? points.at((i + 2) % count) : points.at(std::min(count - 1, i + 2));
    baseline.cubicTo(p1 + (p2 - p0) / 6.0, p2 - (p3 - p1) / 6.0, p2);
  }
  if (style.path_closed) baseline.closeSubpath();
  return baseline;
}

inline PhotoTextLayoutResult rasterize_photo_text_on_path(
    const QString& text, const PhotoTextStyle& style, QSizeF box,
    QSize raster_size, Qt::Alignment alignment = Qt::AlignLeft, bool layout_only = false);

// Paragraphs retain normal shaping and wrapping, while line bands that cross
// the closed subject contour are placed in the widest free interval beside it.
inline PhotoTextLayoutResult rasterize_photo_text_around_contour(
    QString text, const PhotoTextStyle& style, QSizeF box, QSize raster_size, double padding,
    bool layout_only = false) {
  PhotoTextLayoutResult result;
  if (!std::isfinite(box.width()) || !std::isfinite(box.height()) || box.width() <= 2 * padding ||
      box.height() <= 2 * padding || raster_size.width() <= 0 || raster_size.height() <= 0 ||
      static_cast<qint64>(raster_size.width()) * raster_size.height() > 32LL * 1024 * 1024) {
    result.error = QStringLiteral("Paragraph contour layout exceeds the supported dimensions.");
    return result;
  }
  if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
    result.error = QStringLiteral("Text rendering requires an initialized font runtime.");
    return result;
  }
  QVector<QVector<QPointF>> source_contours = style.path_contours;
  if (source_contours.isEmpty()) source_contours.push_back(style.path_points);
  else if (!style.path_points.isEmpty()) source_contours[0] = style.path_points;
  QVector<QVector<QPointF>> sampled_contours;
  double total_contour_length = 0;
  for (const auto& source_points : source_contours) {
    QVector<QPointF> points;
    points.reserve(source_points.size());
    for (const auto& point : source_points) points.push_back({point.x() * box.width(), point.y() * box.height()});
    if (points.size() > 2 && QLineF(points.first(), points.last()).length() < 0.01) points.removeLast();
    if (points.size() < 3) continue;
    QPainterPath contour;
    contour.moveTo(points.first());
    const auto count = static_cast<int>(points.size());
    for (int i = 0; i < count; ++i) {
      const auto p1 = points.at(i), p2 = points.at((i + 1) % count);
      const auto p0 = points.at((i - 1 + count) % count), p3 = points.at((i + 2) % count);
      contour.cubicTo(p1 + (p2 - p0) / 6.0, p2 - (p3 - p1) / 6.0, p2);
    }
    contour.closeSubpath();
    const auto contour_length = contour.length();
    if (!std::isfinite(contour_length) || contour_length < 1) continue;
    total_contour_length += contour_length;
    sampled_contours.push_back({});
    const auto samples_per_contour = 100000.0 /
        static_cast<double>(std::max<qsizetype>(1, source_contours.size()));
    const auto step = std::max(0.5, contour_length / samples_per_contour);
    for (double distance = 0; distance < contour_length; distance += step)
      sampled_contours.last().push_back(contour.pointAtPercent(contour.percentAtLength(distance)));
    sampled_contours.last().push_back(sampled_contours.last().first());
  }
  if (sampled_contours.isEmpty() || !std::isfinite(total_contour_length)) {
    result.error = QStringLiteral("Paragraph exclusion requires at least one valid closed contour.");
    return result;
  }

  result.image = QImage(raster_size, QImage::Format_RGBA64);
  if (result.image.isNull()) {
    result.error = QStringLiteral("Could not allocate the paragraph text raster.");
    return result;
  }
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
  apply_photo_text_variable_axes(font, style);
  font.setLetterSpacing(QFont::AbsoluteSpacing, style.tracking);
  font.setHintingPreference(QFont::PreferNoHinting);
  const auto metrics = QFontMetricsF(font);
  const auto line_height = std::max(style.size * style.line_height, metrics.height());
  QPainter painter(&result.image);
  painter.setRenderHint(QPainter::TextAntialiasing);
  auto paragraph_ink = style.color;
  paragraph_ink.setAlpha(255);
  painter.setPen(paragraph_ink);
  painter.scale(raster_size.width() / box.width(), raster_size.height() / box.height());
  painter.setClipRect(QRectF(0, 0, box.width(), box.height()));
  QTextOption option;
  option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  text.replace(QChar('\r'), QChar('\n'));
  text.replace(QChar(0x2029), QChar('\n'));
  const auto paragraphs = text.split(QChar('\n'), Qt::KeepEmptyParts);
  const auto free_intervals = [&](double top, double bottom) {
    QVector<QPair<double, double>> blocked;
    const auto scan_step_y = std::max(1.0, std::min(line_height / 3.0, 8.0));
    for (double y = top + 0.5; y < bottom; y += scan_step_y) {
      QVector<double> crossings;
      for (const auto& sampled : sampled_contours) {
        for (qsizetype i = 1; i < sampled.size(); ++i) {
          const auto a = sampled.at(i - 1), b = sampled.at(i);
          if ((a.y() <= y && b.y() > y) || (b.y() <= y && a.y() > y))
            crossings.push_back(a.x() + (y - a.y()) * (b.x() - a.x()) / (b.y() - a.y()));
        }
      }
      std::sort(crossings.begin(), crossings.end());
      for (qsizetype i = 0; i + 1 < crossings.size(); i += 2)
        blocked.push_back({crossings.at(i), crossings.at(i + 1)});
    }
    const auto left = padding, right = box.width() - padding;
    std::sort(blocked.begin(), blocked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    QVector<QPair<double, double>> intervals;
    auto cursor = left;
    for (const auto& range : blocked) {
      const auto block_left = qBound(left, range.first, right);
      const auto block_right = qBound(left, range.second, right);
      if (block_right <= cursor) continue;
      if (block_left > cursor) intervals.push_back({cursor, block_left});
      cursor = std::max(cursor, block_right);
      if (cursor >= right) break;
    }
    if (cursor < right) intervals.push_back({cursor, right});
    std::sort(intervals.begin(), intervals.end(), [](const auto& a, const auto& b) {
      return (a.second - a.first) > (b.second - b.first);
    });
    return intervals;
  };

  double y = padding;
  for (qsizetype paragraph_index = 0; paragraph_index < paragraphs.size(); ++paragraph_index) {
    const auto& paragraph = paragraphs.at(paragraph_index);
    int cursor = 0;
    if (paragraph.isEmpty()) {
      y += line_height;
      ++result.line_count;
    }
    while (cursor < paragraph.size() && y + line_height <= box.height() - padding) {
      auto intervals = free_intervals(y, y + line_height);
      if (intervals.isEmpty() || intervals.first().second - intervals.first().first < metrics.averageCharWidth()) {
        y += line_height;
        ++result.line_count;
        continue;
      }
      const auto interval = intervals.first();
      QTextLayout layout(paragraph.mid(cursor), font, &result.image);
      layout.setTextOption(option);
      layout.beginLayout();
      auto line = layout.createLine();
      if (line.isValid()) {
        line.setLineWidth(interval.second - interval.first);
        line.setPosition(QPointF(interval.first, y - style.baseline_offset));
      }
      layout.endLayout();
      if (!line.isValid() || line.textLength() <= 0) {
        result.error = QStringLiteral("Could not advance paragraph text around the contour.");
        return result;
      }
      const auto consumed = line.textLength();
      result.content_width = std::max(result.content_width, line.naturalTextWidth());
      result.overflow = result.overflow || line.naturalTextWidth() > interval.second - interval.first;
      if (!layout_only)
        for (const auto& run : layout.glyphRuns()) result.ink_bounds = result.ink_bounds.united(run.boundingRect());
      QTextLayout::FormatRange ink;
      ink.start = 0;
      ink.length = static_cast<int>(paragraph.size() - cursor);
      if (style.stroke_width > 0)
        ink.format.setTextOutline(QPen(style.stroke_color, style.stroke_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      if (!layout_only && (style.shadow_x != 0 || style.shadow_y != 0)) {
        auto shadow = ink;
        shadow.format.setForeground(style.shadow_color);
        if (style.stroke_width > 0)
          shadow.format.setTextOutline(QPen(style.shadow_color, style.stroke_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        layout.draw(&painter, QPointF(style.shadow_x, style.shadow_y), {shadow});
      }
      if (!layout_only) layout.draw(&painter, QPointF(0, 0), {ink});
      cursor += consumed;
      y += line_height;
      ++result.line_count;
    }
    if (cursor < paragraph.size()) result.overflow = true;
    if (paragraph_index + 1 < paragraphs.size()) y += style.paragraph_spacing;
  }
  result.content_height = y + padding;
  result.overflow = result.overflow || result.content_height > box.height();
  if (style.color.alpha() < 255) {
    painter.resetTransform();
    painter.setClipping(false);
    painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    painter.fillRect(result.image.rect(), QColor(0, 0, 0, style.color.alpha()));
  }
  return result;
}

// Version-1 paragraph layout: top aligned, explicit padding, document-pixel metrics.
// Raster resolution changes sampling only; wrapping uses logical box dimensions.
// layout_only is for inspector queries that consume metrics but never need text pixels or ink bounds.
inline PhotoTextLayoutResult rasterize_photo_text(
    QString text, const PhotoTextStyle& style, QSizeF box, QSize raster_size,
    Qt::Alignment alignment = Qt::AlignLeft, double padding = 0, QPointF origin = {},
    bool layout_only = false) {
  PhotoTextLayoutResult result;
  if (!decode_photo_text_style(encode_photo_text_style(style), &result.error)) return result;
  if (style.path_exclusion)
    return rasterize_photo_text_around_contour(text, style, box, raster_size, padding, layout_only);
  if (!style.path_points.isEmpty())
    return rasterize_photo_text_on_path(text, style, box, raster_size, alignment, layout_only);
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
  apply_photo_text_variable_axes(font, style);
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
    if (!layout_only)
      for (const auto& run : layout.glyphRuns()) result.ink_bounds = result.ink_bounds.united(run.boundingRect());
    QTextLayout::FormatRange ink;
    ink.start = 0;
    ink.length = static_cast<int>(paragraph.size());
    if (style.stroke_width > 0)
      ink.format.setTextOutline(QPen(style.stroke_color, style.stroke_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (!layout_only && (style.shadow_x != 0 || style.shadow_y != 0)) {
      auto shadow = ink;
      shadow.format.setForeground(style.shadow_color);
      if (style.stroke_width > 0)
        shadow.format.setTextOutline(QPen(style.shadow_color, style.stroke_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      layout.draw(&painter, QPointF(style.shadow_x, style.shadow_y), {shadow});
    }
    if (!layout_only) layout.draw(&painter, QPointF(0, 0), {ink});
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

inline PhotoTextLayoutResult rasterize_photo_text_on_path(
    const QString& text, const PhotoTextStyle& style, QSizeF box,
    QSize raster_size, Qt::Alignment alignment, bool layout_only) {
  (void)alignment;
  PhotoTextLayoutResult result;
  if (!decode_photo_text_style(encode_photo_text_style(style), &result.error)) return result;
  if (!std::isfinite(box.width()) || !std::isfinite(box.height()) || box.width() <= 0 || box.height() <= 0 ||
      raster_size.width() <= 0 || raster_size.height() <= 0 || raster_size.width() > 16384 || raster_size.height() > 16384 ||
      static_cast<qint64>(raster_size.width()) * raster_size.height() > 32LL * 1024 * 1024 || text.size() > 1048576) {
    result.error = QStringLiteral("Text contour raster exceeds the supported dimensions or memory budget.");
    return result;
  }
  if (text.contains(QChar::LineFeed) || text.contains(QChar::CarriageReturn)) {
    result.error = QStringLiteral("Text on a contour currently supports one line. Split paragraph text into editable text layers.");
    return result;
  }
  if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
    result.error = QStringLiteral("Text rendering requires an initialized font runtime.");
    return result;
  }
  const auto baseline = build_photo_text_baseline(style, box);
  if (baseline.isEmpty()) {
    result.error = QStringLiteral("Text contour needs at least two distinct points.");
    return result;
  }
  const auto path_length = baseline.length();
  if (!std::isfinite(path_length) || path_length < 1.0) {
    result.error = QStringLiteral("Text contour is too short or degenerate.");
    return result;
  }

  result.image = QImage(raster_size, QImage::Format_RGBA64);
  if (result.image.isNull()) {
    result.error = QStringLiteral("Could not allocate the contour text raster.");
    return result;
  }
  result.image.setColorSpace(QColorSpace(QColorSpace::SRgb));
  result.image.setDotsPerMeterX(3780);
  result.image.setDotsPerMeterY(3780);
  result.image.fill(Qt::transparent);
  QFont font(style.family);
  if (!style.face.isEmpty()) font.setStyleName(style.face);
  font.setPointSizeF(style.size * 72.0 / result.image.logicalDpiY());
  font.setWeight(static_cast<QFont::Weight>(style.weight));
  font.setItalic(style.italic);
  // Underlines are drawn along each glyph's local tangent below, so QTextLayout
  // does not add a flat horizontal underline that would ignore the baseline.
  font.setUnderline(false);
  apply_photo_text_variable_axes(font, style);
  font.setLetterSpacing(QFont::AbsoluteSpacing, style.tracking);
  font.setHintingPreference(QFont::PreferNoHinting);
  QTextLayout layout(text, font, &result.image);
  QTextOption option;
  option.setWrapMode(QTextOption::NoWrap);
  layout.setTextOption(option);
  layout.beginLayout();
  auto line = layout.createLine();
  if (line.isValid()) {
    line.setLineWidth(1048576.0);
    line.setPosition(QPointF(0, 0));
  }
  layout.endLayout();
  if (!line.isValid()) {
    result.error = QStringLiteral("Could not shape text for the contour.");
    return result;
  }
  const auto runs = layout.glyphRuns();
  double max_advance = 0;
  int glyph_count = 0;
  for (const auto& run : runs) {
    const auto glyphs = run.glyphIndexes();
    const auto positions = run.positions();
    const auto advances = run.rawFont().advancesForGlyphIndexes(glyphs);
    const auto size = std::min({glyphs.size(), positions.size(), advances.size()});
    for (qsizetype i = 0; i < size; ++i) {
      max_advance = std::max(max_advance, positions.at(i).x() + advances.at(i).x());
      ++glyph_count;
    }
  }
  result.content_width = max_advance + std::max(0, glyph_count - 1) * style.path_spacing;
  result.line_count = 1;
  result.content_height = style.size;
  const auto start_distance = style.path_start * path_length;
  const auto end_distance = style.path_end * path_length;
  result.overflow = start_distance + result.content_width > end_distance;
  if (layout_only) return result;
  QPainter painter(&result.image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.scale(raster_size.width() / box.width(), raster_size.height() / box.height());
  const auto draw_color = [&](const QPainterPath& glyph_path, const QColor& color, double offset_x, double offset_y) {
    if (glyph_path.isEmpty()) return;
    const auto shifted = glyph_path.translated(offset_x, offset_y);
    painter.setBrush(color);
    painter.setPen(style.stroke_width > 0
        ? QPen(style.stroke_color, style.stroke_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)
        : Qt::NoPen);
    if (style.stroke_width > 0) painter.drawPath(shifted);
    painter.fillPath(shifted, color);
  };
  double spacing_offset = 0;
  int global_glyph = 0;
  for (const auto& run : runs) {
    const auto glyphs = run.glyphIndexes();
    const auto positions = run.positions();
    const auto raw_font = run.rawFont();
    const auto advances = raw_font.advancesForGlyphIndexes(glyphs);
    const auto size = std::min({glyphs.size(), positions.size(), advances.size()});
    for (qsizetype i = 0; i < size; ++i, ++global_glyph) {
      const auto advance = advances.at(i).x();
      const auto center_distance = start_distance + positions.at(i).x() + spacing_offset + advance * 0.5;
      spacing_offset += style.path_spacing;
      if (center_distance < 0 || center_distance > path_length || center_distance > end_distance) continue;
      const auto percent = baseline.percentAtLength(center_distance);
      const auto anchor = baseline.pointAtPercent(percent);
      const auto before = baseline.pointAtPercent(baseline.percentAtLength(std::max(0.0, center_distance - 0.5)));
      const auto after = baseline.pointAtPercent(baseline.percentAtLength(std::min(path_length, center_distance + 0.5)));
      const auto angle = std::atan2(after.y() - before.y(), after.x() - before.x()) * 180.0 / std::acos(-1.0);
      const auto radians = angle * std::acos(-1.0) / 180.0;
      const auto side_sign = style.path_side == QStringLiteral("left") ? 1.0 : -1.0;
      const QPointF normal(-std::sin(radians) * style.path_offset * side_sign,
          std::cos(radians) * style.path_offset * side_sign);
      QTransform transform;
      transform.translate(anchor.x() + normal.x(), anchor.y() + normal.y());
      transform.rotate(angle);
      transform.translate(-advance * 0.5, 0);
      const auto glyph_path = transform.map(
          raw_font.pathForGlyph(glyphs.at(i)).translated(QPointF(0, positions.at(i).y())));
      if (style.shadow_x != 0 || style.shadow_y != 0) draw_color(glyph_path, style.shadow_color, style.shadow_x, style.shadow_y);
      draw_color(glyph_path, style.color, 0, 0);
      if (style.underline) {
        QPainterPath underline;
        const auto underline_y = positions.at(i).y() + QFontMetricsF(font).underlinePos();
        underline.moveTo(0, underline_y);
        underline.lineTo(advance, underline_y);
        const auto placed_underline = transform.map(underline);
        const auto underline_width = std::max(1.0, QFontMetricsF(font).lineWidth());
        if (style.shadow_x != 0 || style.shadow_y != 0) {
          painter.setPen(QPen(style.shadow_color, underline_width, Qt::SolidLine, Qt::RoundCap));
          painter.drawPath(placed_underline.translated(style.shadow_x, style.shadow_y));
        }
        painter.setPen(QPen(style.color, underline_width, Qt::SolidLine, Qt::RoundCap));
        painter.drawPath(placed_underline);
      }
    }
  }
  for (const auto& run : runs) result.ink_bounds = result.ink_bounds.united(run.boundingRect());
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
  auto bounds = measured.ink_bounds.adjusted(-style.stroke_width / 2, -style.stroke_width / 2,
      style.stroke_width / 2, style.stroke_width / 2);
  bounds = bounds.united(bounds.translated(style.shadow_x, style.shadow_y));
  const auto left = std::floor(std::min(0.0, bounds.left())) - 1;
  const auto top = std::floor(std::min(0.0, bounds.top())) - 1;
  const auto right = std::ceil(std::max(measured.content_width, bounds.right())) + 1;
  const auto bottom = std::ceil(std::max(measured.content_height, bounds.bottom())) + 1;
  if (right - left > 8192 || bottom - top > 8192 || (right - left) * (bottom - top) > 32LL * 1024 * 1024)
    return {{}, {}, QStringLiteral("Point text exceeds the supported dimensions. Use a paragraph box or smaller type.")};
  return {QSize(std::max(16, static_cast<int>(right - left)), std::max(16, static_cast<int>(bottom - top))), {-left, -top}, {}};
}

}  // namespace photo_editor::desktop
