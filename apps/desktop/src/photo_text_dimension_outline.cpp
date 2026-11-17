#include "photo_text_dimension_outline.h"

#include "photo_point_text.h"
#include "photo_text_rasterizer.h"

#include <QPainterPathStroker>
#include <QTextLayout>

#include <algorithm>
#include <cmath>

namespace photo_editor::desktop {
namespace {
constexpr qsizetype kMaximumGlyphs = 4096;
constexpr int kMaximumPathElements = 250000;

bool append_outline(PhotoTextDimensionOutlines* result, const QPainterPath& path,
                    const PhotoTextStyle& style) {
  if (path.isEmpty()) return true;
  const auto append = [&](const QPainterPath& outline, QColor color) {
    if (outline.isEmpty() || color.alpha() == 0) return;
    auto found = std::find_if(result->faces.begin(), result->faces.end(),
        [&](const auto& face) { return face.color == color; });
    if (found == result->faces.end()) result->faces.push_back({outline, color});
    else found->path = found->path.united(outline);
  };
  if (style.stroke_width > 0) {
    QPainterPathStroker stroker;
    stroker.setWidth(style.stroke_width);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    append(stroker.createStroke(path).subtracted(path), style.stroke_color);
  }
  append(path, style.color);
  qsizetype elements = 0;
  for (const auto& face : result->faces) elements += face.path.elementCount();
  if (elements > kMaximumPathElements || result->faces.size() > 512) {
    result->error = QStringLiteral("This text has too many outlines for 3D. Shorten it or use fewer character colors.");
    return false;
  }
  return true;
}

bool collect_layout(PhotoTextDimensionOutlines* result, const QTextLayout& layout,
                    const PhotoTextStyle& style, int text_offset, int length,
                    const QString& text, const QImage& metrics_image,
                    qsizetype* glyph_count, const QPainterPath* baseline = nullptr) {
  double spacing = 0;
  const auto path_length = baseline ? baseline->length() : 0.0;
  for (const auto& segment : photo_text_format_segments(style, text_offset + length)) {
    const auto start = std::max(text_offset, segment.start);
    const auto end = std::min(text_offset + length, segment.start + segment.length);
    if (end <= start) continue;
    const auto effective = photo_text_style_with_attributes(style, segment.attributes);
    const QFontMetricsF metrics(photo_text_font_for_attributes(style, segment.attributes, metrics_image));
    const auto runs = layout.glyphRuns(start - text_offset, end - start,
        QTextLayout::RetrieveGlyphIndexes | QTextLayout::RetrieveGlyphPositions |
        QTextLayout::RetrieveStringIndexes);
    for (const auto& run : runs) {
      const auto indexes = run.glyphIndexes();
      const auto positions = run.positions();
      const auto string_indexes = run.stringIndexes();
      const auto advances = run.rawFont().advancesForGlyphIndexes(indexes);
      for (qsizetype i = 0; i < std::min({indexes.size(), positions.size(), advances.size()}); ++i) {
        if (++*glyph_count > kMaximumGlyphs) {
          result->error = QStringLiteral("3D text supports up to 4096 shaped glyphs per layer.");
          return false;
        }
        auto path = run.rawFont().pathForGlyph(indexes[i]);
        const auto string_index = i < string_indexes.size()
            ? static_cast<qsizetype>(string_indexes[i]) : qsizetype(-1);
        // Some shaping engines emit zero-advance, zero-ink control glyphs for
        // contextual scripts. They carry cluster mapping but no visible surface.
        if (path.isEmpty() && indexes[i] != 0 && advances[i].isNull() &&
            run.rawFont().boundingRect(indexes[i]).isEmpty()) continue;
        if (path.isEmpty() && string_index >= 0 && string_index < text.size() &&
            !text.at(string_index).isSpace() && text.at(string_index).category() != QChar::Other_Format) {
          result->error = QStringLiteral("A character in %1 has no vector outline (glyph %2, character %3). Choose an outline font for 3D text.").arg(run.rawFont().familyName()).arg(indexes[i]).arg(static_cast<unsigned int>(text.at(string_index).unicode()), 0, 16);
          return false;
        }
        QTransform transform;
        if (baseline) {
          const auto advance = advances[i].x();
          const auto distance = style.path_start * path_length + positions[i].x() + spacing + advance * 0.5;
          spacing += style.path_spacing;
          if (distance < 0 || distance > path_length || distance > style.path_end * path_length) continue;
          const auto anchor = baseline->pointAtPercent(baseline->percentAtLength(distance));
          const auto before = baseline->pointAtPercent(baseline->percentAtLength(std::max(0.0, distance - 0.5)));
          const auto after = baseline->pointAtPercent(baseline->percentAtLength(std::min(path_length, distance + 0.5)));
          const auto angle = std::atan2(after.y() - before.y(), after.x() - before.x());
          const auto side = style.path_side == QStringLiteral("left") ? 1.0 : -1.0;
          transform.translate(anchor.x() - std::sin(angle) * style.path_offset * side,
                              anchor.y() + std::cos(angle) * style.path_offset * side);
          transform.rotate(angle * 180.0 / std::acos(-1.0));
          transform.translate(-advance * 0.5, positions[i].y());
        } else {
          transform.translate(positions[i].x(), positions[i].y());
        }
        if (effective.underline) {
          QPainterPath underline;
          underline.addRect(0, metrics.underlinePos(), advances[i].x(), std::max(1.0, metrics.lineWidth()));
          path = path.united(underline);
        }
        if (!append_outline(result, transform.map(path), effective)) return false;
      }
    }
  }
  return true;
}
}  // namespace

PhotoTextDimensionOutlines photo_text_dimension_outlines(const QVariantMap& layer) {
  PhotoTextDimensionOutlines result;
  if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
    result.error = QStringLiteral("3D text requires an initialized font runtime.");
    return result;
  }
  const auto box = QSizeF(layer.value("pixelWidth", 512).toDouble(), layer.value("pixelHeight", 128).toDouble());
  if (!std::isfinite(box.width()) || !std::isfinite(box.height()) ||
      box.width() < 16 || box.height() < 16 || box.width() > 8192 || box.height() > 8192) {
    result.error = QStringLiteral("The text box exceeds supported 3D dimensions.");
    return result;
  }
  auto text = layer.value("text").toString();
  if (text.size() > 16384) {
    result.error = QStringLiteral("Shorten this text before enabling 3D.");
    return result;
  }
  PhotoTextStyle style;
  const auto legacy = !layer.contains("textTypography");
  if (!legacy) {
    const auto decoded = decode_photo_text_style(QJsonObject::fromVariantMap(layer.value("textTypography").toMap()), &result.error);
    if (!decoded) return result;
    style = *decoded;
  } else {
    style.family = layer.value("fontFamily", "Helvetica").toString();
    style.size = layer.value("fontSize", 48).toDouble();
    style.weight = layer.value("fontWeight").toString() == "bold" ? 700 : 400;
    style.color = QColor(layer.value("textColor", "#ffffff").toString());
  }
  if (style.path_exclusion) {
    result.error = QStringLiteral("3D paragraph exclusion is not supported yet. Use ordinary paragraph text or a text baseline.");
    return result;
  }
  style = photo_text_style_with_normalized_line_breaks(style, text);
  text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  text.replace(QChar('\r'), QChar('\n'));
  text.replace(QChar(0x2029), QChar('\n'));
  QImage metrics_image(1, 1, QImage::Format_RGBA8888);
  metrics_image.setDotsPerMeterX(3780);
  metrics_image.setDotsPerMeterY(3780);
  const auto font = photo_text_font_for_attributes(style, {}, metrics_image);
  QTextOption option;
  option.setWrapMode(style.point_text ? QTextOption::NoWrap : QTextOption::WrapAtWordBoundaryOrAnywhere);
  option.setAlignment(legacy_photo_text_alignment(layer));
  qsizetype glyph_count = 0;
  if (!style.path_points.isEmpty()) {
    if (text.contains(QChar('\n'))) {
      result.error = QStringLiteral("Text on a 3D baseline supports one line.");
      return result;
    }
    QTextLayout layout(text, font, &metrics_image);
    layout.setFormats(photo_text_format_ranges(style, 0, static_cast<int>(text.size()), metrics_image));
    layout.beginLayout();
    if (auto line = layout.createLine(); line.isValid()) line.setLineWidth(10000000);
    layout.endLayout();
    const auto baseline = build_photo_text_baseline(style, box);
    if (!collect_layout(&result, layout, style, 0, static_cast<int>(text.size()), text, metrics_image, &glyph_count, &baseline)) return result;
    return result;
  }
  const auto origin = style.point_text ? photo_text_origin(layer) : QPointF{};
  const auto max_size = photo_text_max_font_size(style, 0, static_cast<int>(text.size()));
  const auto advance = (style.runs.isEmpty() ? style.size : max_size) * style.line_height;
  const auto paragraphs = text.split(QChar('\n'), Qt::KeepEmptyParts);
  double y = origin.y();
  int offset = 0;
  for (const auto& paragraph : paragraphs) {
    QTextLayout layout(paragraph, font, &metrics_image);
    layout.setTextOption(option);
    layout.setFormats(photo_text_format_ranges(style, offset, static_cast<int>(paragraph.size()), metrics_image));
    layout.beginLayout();
    bool has_line = false;
    while (true) {
      auto line = layout.createLine();
      if (!line.isValid()) break;
      has_line = true;
      line.setLineWidth(std::max(1.0, box.width() - origin.x() - (legacy ? 16.0 : 0.0)));
      line.setPosition(QPointF(origin.x() + (legacy ? 8.0 : 0.0), y - style.baseline_offset));
      y += legacy ? line.height() : advance;
    }
    layout.endLayout();
    if (!has_line) y += advance;
    if (!collect_layout(&result, layout, style, offset, static_cast<int>(paragraph.size()), paragraph, metrics_image, &glyph_count)) return result;
    offset += static_cast<int>(paragraph.size()) + 1;
    y += style.paragraph_spacing;
  }
  if (legacy) {
    const auto shift = std::max(4.0, (box.height() - y) * 0.5);
    for (auto& face : result.faces) face.path.translate(0, shift);
  }
  return result;
}
}  // namespace photo_editor::desktop
