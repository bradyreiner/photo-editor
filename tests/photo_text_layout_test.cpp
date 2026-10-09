#include "photo_text_layout.h"
#include "photo_point_text.h"
#include <iostream>
#include <QLineF>

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  using namespace photo_editor::desktop;
  PhotoTextStyle style;
  style.size = 23.375;
  const auto plain = rasterize_photo_text("Caption", style, {300, 100}, {300, 100});
  if (!plain.error.isEmpty() || plain.image.isNull() || plain.line_count != 1 || plain.overflow) { std::cerr << "Typography check failed at line " << __LINE__ << "\n"; return 1; }
  style.italic = true;
  style.underline = true;
  const auto decorated = rasterize_photo_text("Caption", style, {300, 100}, {300, 100});
  if (decorated.image == plain.image) { std::cerr << "Typography check failed at line " << __LINE__ << "\n"; return 1; }
  style.tracking = 7;
  const QString sentence = "One two three four five six seven eight";
  const auto wrapped = rasterize_photo_text(sentence, style, {150, 400}, {150, 400});
  const auto enlarged = rasterize_photo_text(sentence, style, {150, 400}, {300, 800});
  const auto justified = rasterize_photo_text(sentence, style, {150, 400}, {150, 400}, Qt::AlignJustify);
  if (justified.image == wrapped.image || justified.line_count != wrapped.line_count) return 1;
  if (wrapped.line_count < 3 || wrapped.line_count != enlarged.line_count ||
      wrapped.content_height != enlarged.content_height || enlarged.image.size() != QSize(300, 800)) { std::cerr << "Typography check failed at line " << __LINE__ << "\n"; return 1; }
  const auto metrics = rasterize_photo_text(sentence, style, {150, 400}, {1, 1});
  if (metrics.line_count != wrapped.line_count || metrics.content_height != wrapped.content_height ||
      metrics.overflow != wrapped.overflow) {
    std::cerr << "Inspector metrics must match full-resolution text layout\n";
    return 1;
  }
  const auto layout_only = rasterize_photo_text(sentence, style, {150, 400}, {1, 1},
      Qt::AlignLeft, 0, {}, true);
  if (layout_only.error != metrics.error || layout_only.line_count != metrics.line_count ||
      layout_only.content_width != metrics.content_width ||
      layout_only.content_height != metrics.content_height || layout_only.overflow != metrics.overflow ||
      !layout_only.ink_bounds.isEmpty()) {
    std::cerr << "Layout-only inspector metrics must match the raster route without requesting glyph bounds\n";
    return 1;
  }
  auto variable_style = style;
  variable_style.axes.insert(QStringLiteral("wdth"), 100.0);
  const auto variable_full = rasterize_photo_text(sentence, variable_style, {150, 400}, {1, 1});
  const auto variable_metrics = rasterize_photo_text(sentence, variable_style, {150, 400}, {1, 1},
      Qt::AlignLeft, 0, {}, true);
  if (!decode_photo_text_style(encode_photo_text_style(variable_style)) ||
      variable_metrics.line_count != variable_full.line_count ||
      variable_metrics.content_width != variable_full.content_width ||
      variable_metrics.content_height != variable_full.content_height ||
      variable_metrics.overflow != variable_full.overflow || !variable_metrics.ink_bounds.isEmpty()) {
    std::cerr << "Version-4 variable-axis layout metrics must match raster layout\n";
    return 1;
  }
  auto glass_style = variable_style;
  glass_style.material = QStringLiteral("frosted_glass");
  const auto glass_full = rasterize_photo_text(sentence, glass_style, {150, 400}, {1, 1});
  const auto glass_metrics = rasterize_photo_text(sentence, glass_style, {150, 400}, {1, 1},
      Qt::AlignLeft, 0, {}, true);
  if (encode_photo_text_style(glass_style).value(QStringLiteral("version")).toInt() != 8 ||
      glass_metrics.line_count != glass_full.line_count || glass_metrics.content_width != glass_full.content_width ||
      glass_metrics.content_height != glass_full.content_height || glass_metrics.overflow != glass_full.overflow ||
      !glass_metrics.ink_bounds.isEmpty()) {
    std::cerr << "Version-8 material layout metrics must match raster layout\n";
    return 1;
  }
  const auto clipped = rasterize_photo_text(sentence, style, {150, 30}, {150, 30});
  if (!clipped.overflow || clipped.image.isNull()) { std::cerr << "Typography check failed at line " << __LINE__ << "\n"; return 1; }
  style.line_height = 2;
  const auto spaced = rasterize_photo_text(sentence, style, {150, 400}, {150, 400});
  if (spaced.content_height <= wrapped.content_height) { std::cerr << "Typography check failed at line " << __LINE__ << "\n"; return 1; }
  style.color = QColor("#40ffffff");
  const auto translucent = rasterize_photo_text("Caption", style, {300, 100}, {300, 100});
  if (translucent.image.format() != QImage::Format_RGBA64 ||
      translucent.image.colorSpace() != QColorSpace(QColorSpace::SRgb)) return 1;
  bool sub_eight_bit_coverage = false;
  for (int y = 0; y < translucent.image.height(); ++y) {
    const auto* pixels = reinterpret_cast<const QRgba64*>(translucent.image.constScanLine(y));
    for (int x = 0; x < translucent.image.width(); ++x)
      sub_eight_bit_coverage = sub_eight_bit_coverage || pixels[x].alpha() % 257 != 0;
  }
  if (!sub_eight_bit_coverage) {
    std::cerr << "Translucent text edges must retain coverage beyond eight bits\n";
    return 1;
  }
  int maximum_alpha = 0;
  for (int y = 0; y < translucent.image.height(); ++y)
    for (int x = 0; x < translucent.image.width(); ++x)
      maximum_alpha = std::max(maximum_alpha, translucent.image.pixelColor(x, y).alpha());
  if (maximum_alpha == 0 || maximum_alpha > 64) { std::cerr << "Typography check failed at line " << __LINE__ << "\n"; return 1; }
  if (rasterize_photo_text("Caption", style, {300, 100}, {20000, 100}).error.isEmpty()) { std::cerr << "Typography check failed at line " << __LINE__ << "\n"; return 1; }
  PhotoTextStyle paragraph_style;
  paragraph_style.size = 24;
  const auto lf = rasterize_photo_text("First\n\nLast\n", paragraph_style, {400, 200}, {400, 200});
  const auto crlf = rasterize_photo_text("First\r\n\r\nLast\r\n", paragraph_style, {400, 200}, {400, 200});
  const auto cr = rasterize_photo_text("First\r\rLast\r", paragraph_style, {400, 200}, {400, 200});
  const auto separators = rasterize_photo_text(QString::fromUtf8("First\u2029\u2029Last\u2029"),
      paragraph_style, {400, 200}, {400, 200});
  if (lf.line_count != 4 || crlf.line_count != 4 || cr.line_count != 4 || separators.line_count != 4 ||
      lf.image != crlf.image || lf.image != cr.image || lf.image != separators.image) {
    std::cerr << "Paragraph separators must preserve blank/trailing paragraphs and equivalent pixels\n";
    return 1;
  }
  paragraph_style.paragraph_spacing = 17.25;
  const auto paragraph_spaced = rasterize_photo_text("First\n\nLast\n", paragraph_style, {400, 400}, {400, 400});
  if (paragraph_spaced.line_count != 4 || std::abs(paragraph_spaced.content_height - lf.content_height - 3 * 17.25) > 1e-6)
    return 1;
  paragraph_style.paragraph_spacing = 0;
  PhotoTextStyle point_style;
  point_style.point_text = true;
  point_style.size = 35.5;
  point_style.italic = true;
  point_style.baseline_offset = 12.5;
  if (measure_photo_point_text("X   ", point_style).size.width() <= measure_photo_point_text("X", point_style).size.width()) {
    std::cerr << "Point bounds must include trailing whitespace for the caret\n";return 1;
  }
  const QString point_text = "A long point caption that must never wrap automatically";
  const auto point = measure_photo_point_text(point_text, point_style);
  if (!point.error.isEmpty() || point.size.width() <= 400 || point.size.height() <= 16) return 1;
  const auto point_raster = rasterize_photo_text(point_text, point_style, point.size, point.size, Qt::AlignLeft, 0, point.origin);
  if (!point_raster.error.isEmpty() || point_raster.line_count != 1) return 1;
  // Glyph overhangs must fit inside the measured raster, even with italic type and
  // positive baseline shift. Compare with a generously padded reference raster.
  const auto reference = rasterize_photo_text(point_text, point_style, point.size + QSize(80,80),
      point.size + QSize(80,80), Qt::AlignLeft, 0, point.origin + QPointF(40,40));
  if (point_raster.image != reference.image.copy(QRect(QPoint(40,40),point.size))) return 1;
  for (int y=0;y<reference.image.height();++y) for(int x=0;x<reference.image.width();++x)
    if (!QRect(QPoint(40,40),point.size).contains(x,y) && reference.image.pixelColor(x,y).alpha()!=0) {
      std::cerr << "Point text measurement clipped glyph ink\n";return 1;
    }
  QVariantMap layer{{"text", "Short"}, {"pixelWidth", 200}, {"pixelHeight", 80},
      {"x", 30}, {"y", 50}, {"scale", 1.7}, {"rotation", 35},
      {"textTypography", encode_photo_text_style(point_style).toVariantMap()}};
  const auto anchor = [](const QVariantMap& value) {
    const QPointF half(value.value("pixelWidth").toDouble()/2, value.value("pixelHeight").toDouble()/2);
    const auto local = photo_text_origin(value)-half;
    const auto angle = value.value("rotation").toDouble()*std::numbers::pi/180;
    return QPointF(value.value("x").toDouble(), value.value("y").toDouble()) + half*1.7 +
        QPointF(local.x()*std::cos(angle)-local.y()*std::sin(angle), local.x()*std::sin(angle)+local.y()*std::cos(angle))*1.7;
  };
  const auto before_anchor = anchor(layer);
  if (!fit_photo_point_text(&layer)) return 1;
  layer.insert("text", point_text);
  if (!fit_photo_point_text(&layer) || QLineF(before_anchor,anchor(layer)).length()>1.5) return 1;
  if (measure_photo_point_text(QString(2000, QChar('W')), point_style).error.isEmpty()) return 1;
  const QString mixed = QString::fromUtf8("مرحبا 123 world\n日本語 Cafe\u0301");
  const auto multilingual = rasterize_photo_text(mixed, paragraph_style, {400, 200}, {400, 200});
  const auto multilingual_large = rasterize_photo_text(mixed, paragraph_style, {400, 200}, {800, 400});
  if (!multilingual.error.isEmpty() || multilingual.line_count != 2 ||
      multilingual_large.line_count != multilingual.line_count ||
      multilingual_large.content_height != multilingual.content_height) return 1;
  bool has_ink = false;
  for (int y = 0; y < multilingual.image.height(); ++y)
    for (int x = 0; x < multilingual.image.width(); ++x)
      has_ink = has_ink || multilingual.image.pixelColor(x, y).alpha() > 0;
  if (!has_ink) return 1;
  std::cout << "Typography rendering, stable reflow, overflow and alpha passed\n";
}
