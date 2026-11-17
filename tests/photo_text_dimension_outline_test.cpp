#include "photo_point_text.h"
#include "photo_text_dimension_outline.h"
#include "photo_text_layout.h"
#include "photo_text_rasterizer.h"
#include "photo_text_style.h"

#include <QFontDatabase>
#include <QGlyphRun>
#include <QColorSpace>
#include <QImage>
#include <QPainter>
#include <QRawFont>
#include <QTextLayout>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace photo_editor::desktop;

int fail(const QString& message) {
  std::cerr << "Dimensional text outline check failed: "
            << message.toStdString() << '\n';
  return 1;
}

QFont font_for_family(const QString& family, int pixel_size = 64) {
  QFont font(family);
  font.setPixelSize(pixel_size);
  font.setHintingPreference(QFont::PreferNoHinting);
  return font;
}

bool family_has_outlines(const QString& family, const QString& sample) {
  const auto raw = QRawFont::fromFont(font_for_family(family));
  if (!raw.isValid()) return false;
  const auto glyphs = raw.glyphIndexesForString(sample);
  if (glyphs.isEmpty()) return false;
  for (const auto glyph : glyphs) {
    if (glyph == 0 || raw.pathForGlyph(glyph).isEmpty()) return false;
  }
  return true;
}

bool family_has_latin_counter(const QString& family) {
  const auto raw = QRawFont::fromFont(font_for_family(family));
  if (!raw.isValid()) return false;
  const auto glyphs = raw.glyphIndexesForString(QStringLiteral("O"));
  if (glyphs.size() != 1 || glyphs.first() == 0) return false;
  const auto path = raw.pathForGlyph(glyphs.first());
  return !path.isEmpty() && !path.contains(path.boundingRect().center());
}

QString latin_outline_family() {
  QStringList candidates{QFontDatabase::systemFont(QFontDatabase::GeneralFont).family()};
  for (const auto& family : QFontDatabase::families())
    if (!candidates.contains(family, Qt::CaseInsensitive)) candidates.push_back(family);
  for (const auto& family : candidates)
    if (family_has_outlines(family, QStringLiteral("AO")) && family_has_latin_counter(family))
      return family;
  return {};
}

QString arabic_outline_family() {
  const auto sample = QString::fromUtf8("سلام");
  for (const auto& family : QFontDatabase::families())
    if (family_has_outlines(family, sample)) return family;
  return {};
}

PhotoTextStyle base_style(const QString& family) {
  PhotoTextStyle style;
  style.family = family;
  style.size = 52.0;
  style.color = QColor(QStringLiteral("#fff5e8"));
  return style;
}

QVariantMap text_layer(const QString& text, const PhotoTextStyle& style,
                       QSize size, QVariantMap extra = {}) {
  QVariantMap layer{{QStringLiteral("kind"), QStringLiteral("text")},
      {QStringLiteral("text"), text},
      {QStringLiteral("pixelWidth"), size.width()},
      {QStringLiteral("pixelHeight"), size.height()},
      {QStringLiteral("textTypography"), encode_photo_text_style(style).toVariantMap()}};
  for (auto it = extra.cbegin(); it != extra.cend(); ++it) layer.insert(it.key(), it.value());
  return layer;
}

QImage rasterize_outlines(const PhotoTextDimensionOutlines& outlines, QSize size) {
  QImage image(size, QImage::Format_RGBA64);
  if (image.isNull()) return {};
  image.setColorSpace(QColorSpace(QColorSpace::SRgb));
  image.setDotsPerMeterX(3780);
  image.setDotsPerMeterY(3780);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  for (const auto& face : outlines.faces) painter.fillPath(face.path, face.color);
  return image;
}

struct Coverage final {
  QRect bounds;
  qint64 reference_ink = 0;
  qint64 outline_ink = 0;
  qint64 union_ink = 0;
  qint64 disagreements = 0;
  qint64 distant_disagreements = 0;
};

Coverage compare_coverage(const QImage& reference, const QImage& outlined,
                         int alpha_threshold = 24) {
  Coverage result;
  if (reference.isNull() || outlined.isNull() || reference.size() != outlined.size()) return result;
  int left = reference.width();
  int top = reference.height();
  int right = -1;
  int bottom = -1;
  for (int y = 0; y < reference.height(); ++y) {
    for (int x = 0; x < reference.width(); ++x) {
      const bool a = reference.pixelColor(x, y).alpha() >= alpha_threshold;
      const bool b = outlined.pixelColor(x, y).alpha() >= alpha_threshold;
      if (a) ++result.reference_ink;
      if (b) ++result.outline_ink;
      if (a || b) {
        ++result.union_ink;
        left = std::min(left, x);
        top = std::min(top, y);
        right = std::max(right, x);
        bottom = std::max(bottom, y);
      }
      if (a != b) {
        ++result.disagreements;
        // Qt's native glyph raster and vector path raster use different edge
        // sampling, especially at half-pixel point-text origins. Only a one-pixel
        // antialias fringe is tolerated; misplaced or missing strokes still fail.
        const auto& other = a ? outlined : reference;
        bool nearby = false;
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx)
            if (other.rect().contains(x + dx, y + dy) &&
                other.pixelColor(x + dx, y + dy).alpha() >= alpha_threshold) nearby = true;
        if (!nearby) ++result.distant_disagreements;
      }
    }
  }
  if (right >= left && bottom >= top)
    result.bounds = QRect(QPoint(left, top), QPoint(right, bottom));
  return result;
}

bool coverage_matches(const QVariantMap& layer, const QString& name,
                      double maximum_disagreement = 0.14) {
  const auto outlines = photo_text_dimension_outlines(layer);
  if (!outlines.error.isEmpty() || outlines.faces.isEmpty()) {
    std::cerr << name.toStdString() << ": outline extraction failed or returned no faces: "
              << outlines.error.toStdString() << '\n';
    return false;
  }
  QString raster_error;
  const QSize size(layer.value(QStringLiteral("pixelWidth")).toInt(),
                   layer.value(QStringLiteral("pixelHeight")).toInt());
  const auto reference = rasterize_legacy_photo_text(layer, size, &raster_error);
  const auto vector_image = rasterize_outlines(outlines, size);
  if (!raster_error.isEmpty() || reference.isNull() || vector_image.isNull()) {
    std::cerr << name.toStdString() << ": production rasterization failed: "
              << raster_error.toStdString() << '\n';
    return false;
  }
  const auto coverage = compare_coverage(reference, vector_image);
  const auto disagreement = coverage.union_ink == 0 ? 1.0
      : static_cast<double>(coverage.disagreements) / static_cast<double>(coverage.union_ink);
  const auto reference_bounds = [&] {
    int left = size.width(), top = size.height(), right = -1, bottom = -1;
    for (int y = 0; y < reference.height(); ++y)
      for (int x = 0; x < reference.width(); ++x)
        if (reference.pixelColor(x, y).alpha() >= 24) {
          left = std::min(left, x); top = std::min(top, y);
          right = std::max(right, x); bottom = std::max(bottom, y);
        }
    return right >= left && bottom >= top
        ? QRect(QPoint(left, top), QPoint(right, bottom)) : QRect{};
  }();
  const auto vector_bounds = [&] {
    int left = size.width(), top = size.height(), right = -1, bottom = -1;
    for (int y = 0; y < vector_image.height(); ++y)
      for (int x = 0; x < vector_image.width(); ++x)
        if (vector_image.pixelColor(x, y).alpha() >= 24) {
          left = std::min(left, x); top = std::min(top, y);
          right = std::max(right, x); bottom = std::max(bottom, y);
        }
    return right >= left && bottom >= top
        ? QRect(QPoint(left, top), QPoint(right, bottom)) : QRect{};
  }();
  const auto edge_delta = [](const QRect& a, const QRect& b) {
    return std::max({std::abs(a.left() - b.left()), std::abs(a.top() - b.top()),
        std::abs(a.right() - b.right()), std::abs(a.bottom() - b.bottom())});
  };
  if (coverage.reference_ink < 100 || coverage.outline_ink < 100 ||
      (disagreement > maximum_disagreement &&
          (disagreement > 0.30 || coverage.distant_disagreements > coverage.union_ink / 100)) || reference_bounds.isEmpty() ||
      vector_bounds.isEmpty() || edge_delta(reference_bounds, vector_bounds) > 3) {
    reference.save("/tmp/looksy-outline-reference.png");
    vector_image.save("/tmp/looksy-outline-vector.png");
    std::cerr << "font=" << layer.value("textTypography").toMap().value("family").toString().toStdString() << '\n';
    std::cerr << name.toStdString() << ": weak/mismatched coverage (reference="
              << coverage.reference_ink << ", outline=" << coverage.outline_ink
              << ", disagreement=" << disagreement << ", bounds delta="
              << edge_delta(reference_bounds, vector_bounds) << ")\n";
    return false;
  }
  return true;
}

bool contains_color_ink(const QImage& image, QColor color) {
  int matching = 0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const auto pixel = image.pixelColor(x, y);
      if (pixel.alpha() < 160) continue;
      if (std::abs(pixel.red() - color.red()) < 35 &&
          std::abs(pixel.green() - color.green()) < 35 &&
          std::abs(pixel.blue() - color.blue()) < 35)
        ++matching;
    }
  }
  return matching >= 20;
}

int shaped_glyph_count(const QString& family, const QString& text) {
  QImage metrics(1, 1, QImage::Format_RGBA8888);
  metrics.setDotsPerMeterX(3780);
  metrics.setDotsPerMeterY(3780);
  auto style = base_style(family);
  const auto font = photo_text_font_for_attributes(style, {}, metrics);
  QTextLayout layout(text, font, &metrics);
  QTextOption option;
  option.setWrapMode(QTextOption::NoWrap);
  layout.setTextOption(option);
  layout.beginLayout();
  auto line = layout.createLine();
  if (line.isValid()) line.setLineWidth(100000.0);
  layout.endLayout();
  int count = 0;
  for (const auto& run : layout.glyphRuns())
    count += static_cast<int>(run.glyphIndexes().size());
  return count;
}

QString family_with_ligature() {
  for (const auto& family : QFontDatabase::families())
    if (family_has_outlines(family, QStringLiteral("office")) &&
        shaped_glyph_count(family, QStringLiteral("office")) < QStringLiteral("office").size())
      return family;
  return {};
}

QString color_font_without_vector_glyph() {
  const auto emoji = QString::fromUcs4(U"\U0001F600");
  for (const auto& family : QFontDatabase::families()) {
    const auto raw = QRawFont::fromFont(font_for_family(family));
    if (!raw.isValid() || !raw.supportsCharacter(0x1F600U)) continue;
    const auto glyphs = raw.glyphIndexesForString(emoji);
    if (!glyphs.isEmpty() && glyphs.first() != 0 && raw.pathForGlyph(glyphs.first()).isEmpty())
      return family;
  }
  return {};
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  using namespace photo_editor::desktop;

  const auto family = latin_outline_family();
  if (family.isEmpty()) return fail(QStringLiteral(
      "The installed font database has no outline-capable Latin font for coverage comparison."));

  auto paragraph = base_style(family);
  paragraph.size = 46.0;
  paragraph.tracking = 2.1;
  paragraph.line_height = 1.16;
  paragraph.baseline_offset = 4.5;
  paragraph.paragraph_spacing = 11.0;
  paragraph.underline = true;
  paragraph.stroke_width = 1.8;
  paragraph.stroke_color = QColor(QStringLiteral("#164db7"));
  const auto paragraph_text = QStringLiteral("Glyph shapes\nFill, stroke and underline");
  const auto paragraph_layer = text_layer(paragraph_text, paragraph, QSize(600, 190));
  if (!coverage_matches(paragraph_layer, QStringLiteral("multiline styled paragraph"), 0.20)) return 1;

  auto run_style = base_style(family);
  run_style.size = 58.0;
  run_style.runs = {
      QVariantMap{{QStringLiteral("start"), 0}, {QStringLiteral("length"), 1},
          {QStringLiteral("color"), QStringLiteral("#ffc81e1e")},
          {QStringLiteral("underline"), true}},
      QVariantMap{{QStringLiteral("start"), 1}, {QStringLiteral("length"), 1},
          {QStringLiteral("color"), QStringLiteral("#ff2458d8")},
          {QStringLiteral("strokeWidth"), 2.0},
          {QStringLiteral("strokeColor"), QStringLiteral("#ff1e8b42")}}};
  const auto run_layer = text_layer(QStringLiteral("AB"), run_style, QSize(260, 120));
  const auto run_outlines = photo_text_dimension_outlines(run_layer);
  const auto run_reference = rasterize_legacy_photo_text(run_layer, QSize(260, 120));
  const auto run_vector = rasterize_outlines(run_outlines, QSize(260, 120));
  if (!coverage_matches(run_layer, QStringLiteral("colored character runs"), 0.22) ||
      run_outlines.faces.size() < 2 || !contains_color_ink(run_reference, QColor("#ffc81e1e")) ||
      !contains_color_ink(run_reference, QColor("#ff2458d8")) ||
      !contains_color_ink(run_vector, QColor("#ffc81e1e")) ||
      !contains_color_ink(run_vector, QColor("#ff2458d8")))
    return fail(QStringLiteral("Character runs must preserve distinct visible glyph colors."));

  auto point_style = base_style(family);
  point_style.point_text = true;
  point_style.size = 48.0;
  point_style.tracking = 0.75;
  point_style.baseline_offset = 8.0;
  const auto point_layer = text_layer(QStringLiteral("Point origin"), point_style,
      QSize(420, 140), {{QStringLiteral("textOriginX"), 31.5},
                        {QStringLiteral("textOriginY"), 55.0}});
  if (!coverage_matches(point_layer, QStringLiteral("point text origin and baseline offset"), 0.16)) return 1;
  auto originless = point_layer;
  originless.insert(QStringLiteral("textOriginX"), 0.0);
  originless.insert(QStringLiteral("textOriginY"), 0.0);
  const auto point_geometry = photo_text_dimension_outlines(point_layer);
  const auto originless_geometry = photo_text_dimension_outlines(originless);
  if (point_geometry.faces.isEmpty() || originless_geometry.faces.isEmpty() ||
      point_geometry.faces.first().path.boundingRect().left() -
          originless_geometry.faces.first().path.boundingRect().left() < 20.0 ||
      point_geometry.faces.first().path.boundingRect().top() -
          originless_geometry.faces.first().path.boundingRect().top() < 35.0)
    return fail(QStringLiteral("Point text outlines must include the authored text origin."));

  auto curve_style = base_style(family);
  curve_style.size = 43.0;
  curve_style.tracking = 1.1;
  curve_style.path_points = {QPointF(0.05, 0.72), QPointF(0.24, 0.30),
      QPointF(0.51, 0.65), QPointF(0.75, 0.29), QPointF(0.95, 0.55)};
  curve_style.path_start = 0.04;
  curve_style.path_end = 0.98;
  curve_style.path_offset = 3.0;
  curve_style.path_spacing = 1.4;
  curve_style.path_side = QStringLiteral("left");
  const auto curve_layer = text_layer(QStringLiteral("Curved baseline"), curve_style, QSize(640, 220));
  if (!coverage_matches(curve_layer, QStringLiteral("curved baseline placement"), 0.19)) return 1;

  auto counter_style = base_style(family);
  counter_style.size = 92.0;
  const auto counter_layer = text_layer(QStringLiteral("O"), counter_style, QSize(180, 135));
  const auto counter = photo_text_dimension_outlines(counter_layer);
  if (!counter.error.isEmpty() || counter.faces.isEmpty())
    return fail(QStringLiteral("The installed outline font must produce a vector counter glyph."));
  const auto counter_center = counter.faces.first().path.boundingRect().center();
  if (counter.faces.first().path.contains(counter_center))
    return fail(QStringLiteral("The O glyph counter must remain a hole in its vector fill."));
  const auto counter_image = rasterize_outlines(counter, QSize(180, 135));
  const auto production_counter = rasterize_legacy_photo_text(counter_layer, QSize(180, 135));
  const auto center_x = qBound(0, qRound(counter_center.x()), counter_image.width() - 1);
  const auto center_y = qBound(0, qRound(counter_center.y()), counter_image.height() - 1);
  if (counter_image.pixelColor(center_x, center_y).alpha() > 8 ||
      production_counter.pixelColor(center_x, center_y).alpha() > 8 ||
      !coverage_matches(counter_layer, QStringLiteral("glyph counter preservation"), 0.14))
    return fail(QStringLiteral("Glyph counters must remain transparent in outline and raster output."));

  const auto arabic_family = arabic_outline_family();
  if (!arabic_family.isEmpty()) {
    auto arabic_style = base_style(arabic_family);
    arabic_style.size = 56.0;
    const auto arabic_layer = text_layer(QString::fromUtf8("سلام"), arabic_style, QSize(330, 130));
    if (!coverage_matches(arabic_layer, QStringLiteral("installed RTL font shaping"), 0.20)) return 1;
  } else std::cout << "RTL outline comparison skipped: no installed font exposes Arabic vector glyphs\n";

  const auto ligature_family = family_with_ligature();
  if (!ligature_family.isEmpty()) {
    auto ligature_style = base_style(ligature_family);
    ligature_style.size = 54.0;
    const auto ligature_layer = text_layer(QStringLiteral("office"), ligature_style, QSize(300, 130));
    if (!coverage_matches(ligature_layer, QStringLiteral("installed ligature shaping"), 0.18)) return 1;
  } else std::cout << "Ligature outline comparison skipped: no installed font shapes office with a ligature\n";

  const auto color_family = color_font_without_vector_glyph();
  if (!color_family.isEmpty()) {
    auto color_style = base_style(color_family);
    color_style.size = 64.0;
    const auto color_layer = text_layer(QString::fromUcs4(U"\U0001F600"), color_style, QSize(150, 120));
    const auto color_outlines = photo_text_dimension_outlines(color_layer);
    if (!color_outlines.faces.isEmpty() ||
        !color_outlines.error.contains(QStringLiteral("no vector outline"), Qt::CaseInsensitive))
      return fail(QStringLiteral("Installed color glyphs must fail with the vector-outline explanation."));
  }

  auto bounded_style = base_style(family);
  bounded_style.point_text = true;
  const auto too_many_glyphs = text_layer(QString(4097, QChar(' ')), bounded_style, QSize(8192, 160));
  const auto bounded = photo_text_dimension_outlines(too_many_glyphs);
  if (!bounded.faces.isEmpty() || !bounded.error.contains(QStringLiteral("4096")))
    return fail(QStringLiteral("Outline extraction must stop at the 4096 shaped-glyph budget."));

  auto invalid_box = text_layer(QStringLiteral("Finite check"), base_style(family), QSize(180, 100));
  invalid_box.insert(QStringLiteral("pixelWidth"), std::numeric_limits<double>::quiet_NaN());
  const auto nonfinite = photo_text_dimension_outlines(invalid_box);
  if (!nonfinite.faces.isEmpty() || nonfinite.error.isEmpty())
    return fail(QStringLiteral("Non-finite text box dimensions must fail closed."));

  std::cout << "Dimensional text outlines match production glyph coverage and retain bounded vector geometry\n";
  return 0;
}
