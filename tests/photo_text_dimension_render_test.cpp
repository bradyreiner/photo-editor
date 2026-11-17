#include "photo_text_dimension_render.h"
#include "photo_text_rasterizer.h"
#include "photo_text_style.h"

#include <QColorSpace>
#include <QDir>
#include <QFontDatabase>
#include <QBrush>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QRawFont>
#include <QRadialGradient>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace photo_editor::desktop;

int fail(const QString& message) {
  std::cerr << "Dimensional text render check failed: "
            << message.toStdString() << '\n';
  return 1;
}

bool font_has_outline(const QString& family) {
  QFont font(family);
  font.setPixelSize(64);
  const auto raw = QRawFont::fromFont(font);
  if (!raw.isValid()) return false;
  const auto glyphs = raw.glyphIndexesForString(QStringLiteral("Flatkeepsitsglyphs3D"));
  if (glyphs.isEmpty()) return false;
  for (const auto glyph : glyphs)
    if (glyph == 0 || raw.pathForGlyph(glyph).isEmpty()) return false;
  return true;
}

QString installed_outline_family() {
  const auto preferred = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
  if (font_has_outline(preferred)) return preferred;
  for (const auto& family : QFontDatabase::families())
    if (font_has_outline(family)) return family;
  return {};
}

PhotoTextStyle text_style(const QString& family, double size = 60.0) {
  PhotoTextStyle style;
  style.family = family;
  style.size = size;
  style.color = QColor(QStringLiteral("#fff8ed"));
  return style;
}

QVariantMap text_layer(const QString& text, const PhotoTextStyle& style,
                       const PhotoTextDimensionSettings* dimension,
                       QSize box = QSize(520, 180)) {
  QVariantMap layer{{QStringLiteral("kind"), QStringLiteral("text")},
      {QStringLiteral("id"), QStringLiteral("dimension-render-fixture")},
      {QStringLiteral("text"), text},
      {QStringLiteral("pixelWidth"), box.width()},
      {QStringLiteral("pixelHeight"), box.height()},
      {QStringLiteral("textTypography"), encode_photo_text_style(style).toVariantMap()}};
  if (dimension != nullptr)
    layer.insert(QStringLiteral("textDimension"), encode_photo_text_dimension_variant_map(*dimension));
  return layer;
}

QImage to_linear_float(QImage image) {
  if (image.isNull()) return {};
  if (!image.colorSpace().isValid()) image.setColorSpace(QColorSpace(QColorSpace::SRgb));
  return image.convertedToColorSpace(QColorSpace(QColorSpace::SRgbLinear),
                                     QImage::Format_RGBA32FPx4);
}

double channel(const QImage& image, int x, int y, int index) {
  const auto* pixel = reinterpret_cast<const float*>(image.constScanLine(y)) + x * 4;
  return pixel[index];
}

struct PixelStats final {
  QRect alpha_bounds;
  qint64 covered = 0;
  qint64 fractional_alpha = 0;
  double maximum_channel = 0.0;
  double maximum_alpha = 0.0;
  double maximum_border_alpha = 0.0;
};

PixelStats pixel_stats(const QImage& image) {
  PixelStats result;
  int left = image.width(), top = image.height(), right = -1, bottom = -1;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const auto alpha = channel(image, x, y, 3);
      result.maximum_alpha = std::max(result.maximum_alpha, alpha);
      for (int c = 0; c < 3; ++c)
        result.maximum_channel = std::max(result.maximum_channel, channel(image, x, y, c));
      if (alpha > 1.0e-6) {
        ++result.covered;
        left = std::min(left, x); top = std::min(top, y);
        right = std::max(right, x); bottom = std::max(bottom, y);
        if (alpha < 0.999) ++result.fractional_alpha;
      }
      if (x == 0 || y == 0 || x == image.width() - 1 || y == image.height() - 1)
        result.maximum_border_alpha = std::max(result.maximum_border_alpha, alpha);
    }
  }
  if (right >= left && bottom >= top)
    result.alpha_bounds = QRect(QPoint(left, top), QPoint(right, bottom));
  return result;
}

PhotoTextDimensionFrame render_frame(const QVariantMap& layer, QSize raster_size,
                                    const QString& context) {
  auto frame = render_photo_text_dimension(layer, raster_size);
  if (!frame.error.isEmpty() || frame.image.isNull())
    std::cerr << context.toStdString() << ": " << frame.error.toStdString() << '\n';
  return frame;
}

bool close_rect(const QRectF& a, const QRectF& b, double tolerance = 1.0e-6) {
  return std::abs(a.x() - b.x()) <= tolerance &&
      std::abs(a.y() - b.y()) <= tolerance &&
      std::abs(a.width() - b.width()) <= tolerance &&
      std::abs(a.height() - b.height()) <= tolerance;
}

qint64 changed_float_pixels(const QImage& a, const QImage& b,
                            double threshold = 0.025) {
  if (a.isNull() || b.isNull() || a.size() != b.size() ||
      a.format() != QImage::Format_RGBA32FPx4 || b.format() != QImage::Format_RGBA32FPx4)
    return -1;
  qint64 changed = 0;
  for (int y = 0; y < a.height(); ++y) {
    for (int x = 0; x < a.width(); ++x) {
      bool differs = false;
      for (int c = 0; c < 4; ++c)
        differs = differs || std::abs(channel(a, x, y, c) - channel(b, x, y, c)) > threshold;
      if (differs) ++changed;
    }
  }
  return changed;
}

qint64 changed_frame_pixels(const PhotoTextDimensionFrame& a,
                            const PhotoTextDimensionFrame& b,
                            double threshold = 0.025) {
  if (a.image.isNull() || b.image.isNull() ||
      a.image.format() != QImage::Format_RGBA32FPx4 ||
      b.image.format() != QImage::Format_RGBA32FPx4)
    return -1;
  const int left = std::min(-a.padding.x(), -b.padding.x());
  const int top = std::min(-a.padding.y(), -b.padding.y());
  const int right = std::max(a.image.width() - a.padding.x(),
                             b.image.width() - b.padding.x());
  const int bottom = std::max(a.image.height() - a.padding.y(),
                              b.image.height() - b.padding.y());
  qint64 changed = 0;
  for (int y = top; y < bottom; ++y) {
    for (int x = left; x < right; ++x) {
      const int ax = x + a.padding.x(), ay = y + a.padding.y();
      const int bx = x + b.padding.x(), by = y + b.padding.y();
      const bool in_a = ax >= 0 && ay >= 0 && ax < a.image.width() && ay < a.image.height();
      const bool in_b = bx >= 0 && by >= 0 && bx < b.image.width() && by < b.image.height();
      bool differs = in_a != in_b;
      for (int c = 0; c < 4 && !differs; ++c) {
        const auto av = in_a ? channel(a.image, ax, ay, c) : 0.0;
        const auto bv = in_b ? channel(b.image, bx, by, c) : 0.0;
        differs = std::abs(av - bv) > threshold;
      }
      if (differs) ++changed;
    }
  }
  return changed;
}

bool write_gallery(const QString& directory, const QString& family) {
  if (!QDir().mkpath(directory)) {
    std::cerr << "Could not create gallery directory: " << directory.toStdString() << '\n';
    return false;
  }
  struct Sample final {
    QString name;
    QString text;
    QString title;
    QString descriptor;
    QColor background;
    QColor glow;
    PhotoTextDimensionSettings settings;
    double font_size = 164.0;
  };

  PhotoTextDimensionSettings studio;
  studio.enabled = true; studio.depth = 42; studio.bevel_width = 4;
  studio.bevel_segments = 5; studio.rotation_x = -12; studio.rotation_y = -18;
  studio.front_color = QColor(QStringLiteral("#ffffd891"));
  studio.side_color = QColor(QStringLiteral("#ff713d2a"));
  studio.bevel_color = QColor(QStringLiteral("#ffffefbd"));
  studio.use_text_color = false; studio.roughness = 0.32; studio.metallic = 0.28;
  studio.light_azimuth = -38; studio.light_elevation = 48;
  studio.light_intensity = 1.2; studio.ambient = 0.25;

  auto metal = studio;
  metal.depth = 28; metal.bevel_width = 3.5; metal.bevel_segments = 6;
  metal.bevel_profile = PhotoTextDimensionSettings::BevelProfile::Chamfer;
  metal.front_color = QColor(QStringLiteral("#ffe3eaf2"));
  metal.side_color = QColor(QStringLiteral("#ff647287"));
  metal.bevel_color = QColor(QStringLiteral("#ffffffff"));
  metal.roughness = 0.22; metal.metallic = 0.82;
  metal.light_azimuth = -48; metal.light_elevation = 42;

  auto retro = studio;
  retro.depth = 58; retro.bevel_width = 2.4; retro.bevel_segments = 4;
  retro.front_color = QColor(QStringLiteral("#ffffbc54"));
  retro.side_color = QColor(QStringLiteral("#ffb52e63"));
  retro.bevel_color = QColor(QStringLiteral("#ffffe69a"));
  retro.roughness = 0.46; retro.metallic = 0.18;
  retro.rotation_x = -8; retro.rotation_y = -15; retro.rotation_z = -2;
  retro.light_azimuth = -52; retro.light_elevation = 34;

  auto four_point = studio;
  four_point.depth = 32; four_point.bevel_width = 3;
  four_point.rotation_x = -8; four_point.rotation_y = -10; four_point.rotation_z = 3;
  four_point.corner_pin_enabled = true;
  four_point.corner_pins = {QPointF(0.08, 0.12), QPointF(0.94, 0.02),
      QPointF(0.99, 0.90), QPointF(0.02, 0.98)};
  four_point.front_color = QColor(QStringLiteral("#ffffdf9d"));

  const std::array<Sample, 4> samples{{
      {QStringLiteral("studio"), QStringLiteral("STUDIO"), QStringLiteral("STUDIO / GILT"),
          QStringLiteral("Warm brass  /  rounded bevel"), QColor("#102440"), QColor("#315b88"), studio, 164},
      {QStringLiteral("beveled-metal"), QStringLiteral("ALLOY"), QStringLiteral("ALLOY / CHROME"),
          QStringLiteral("Cool steel  /  chamfered edge"), QColor("#251b37"), QColor("#513a71"), metal, 170},
      {QStringLiteral("retro-deep"), QStringLiteral("RETRO"), QStringLiteral("RETRO / DEEP CUT"),
          QStringLiteral("Candy enamel  /  long cast depth"), QColor("#073937"), QColor("#136a62"), retro, 180},
      {QStringLiteral("four-point"), QStringLiteral("PERSPECTIVE"), QStringLiteral("PERSPECTIVE / FOUR POINT"),
          QStringLiteral("Pinned plane  /  compound tilt"), QColor("#efe7d9"), QColor("#d7c7ab"), four_point, 115}}};

  for (qsizetype sample_index = 0; sample_index < static_cast<qsizetype>(samples.size()); ++sample_index) {
    const auto& sample = samples[static_cast<std::size_t>(sample_index)];
    auto style = text_style(family, sample.font_size);
    style.weight = 800;
    style.point_text = true;
    style.color = sample.settings.front_color;
    const auto layer = text_layer(sample.text, style, &sample.settings, QSize(900, 220));
    const auto frame = render_photo_text_dimension(layer, QSize(900, 220));
    if (!frame.error.isEmpty() || frame.image.isNull()) {
      std::cerr << "Gallery render failed for " << sample.name.toStdString()
                << ": " << frame.error.toStdString() << '\n';
      return false;
    }
    const auto ink_stats = pixel_stats(frame.image);
    if (ink_stats.covered == 0 || ink_stats.alpha_bounds.isEmpty()) {
      std::cerr << "Gallery render has no visible ink for " << sample.name.toStdString() << '\n';
      return false;
    }
    auto ink = frame.image.copy(ink_stats.alpha_bounds);
    const auto fit = std::min({1.0, 900.0 / ink.width(), 380.0 / ink.height()});
    if (fit < 1.0) {
      ink = ink.scaled(QSize(std::max(1, qRound(ink.width() * fit)),
                             std::max(1, qRound(ink.height() * fit))),
                       Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    QImage background(1200, 720, QImage::Format_ARGB32_Premultiplied);
    background.setColorSpace(QColorSpace(QColorSpace::SRgb));
    QLinearGradient base(0, 0, 0, background.height());
    base.setColorAt(0.0, sample.background.lighter(118));
    base.setColorAt(1.0, sample.background.darker(132));
    background.fill(sample.background);
    QPainter painter(&background);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(background.rect(), QBrush(base));
    QRadialGradient glow(QPointF(600, 335), 510);
    auto glow_center = sample.glow;
    glow_center.setAlpha(90);
    auto glow_edge = sample.glow;
    glow_edge.setAlpha(0);
    glow.setColorAt(0.0, glow_center);
    glow.setColorAt(1.0, glow_edge);
    painter.fillRect(background.rect(), QBrush(glow));
    QFont label_font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    label_font.setPixelSize(15);
    label_font.setWeight(QFont::DemiBold);
    label_font.setLetterSpacing(QFont::AbsoluteSpacing, 2.2);
    painter.setFont(label_font);
    const auto label_color = sample.name == QStringLiteral("four-point")
        ? QColor(QStringLiteral("#514637")) : QColor(QStringLiteral("#bdcfe0"));
    painter.setPen(label_color);
    painter.drawText(QPointF(72, 78), QStringLiteral("LOOKSY   /   TYPE OBJECTS"));
    painter.drawText(QRectF(850, 54, 278, 32), Qt::AlignRight | Qt::AlignVCenter,
                     QStringLiteral("%1  /  04").arg(sample_index + 1, 2, 10, QLatin1Char('0')));
    const QPointF placement((background.width() - ink.width()) * 0.5,
                            365.0 - ink.height() * 0.5);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(placement, ink);
    painter.setPen(QPen(label_color, 1.0));
    painter.drawLine(QPointF(72, 586), QPointF(1128, 586));
    QFont title_font = label_font;
    title_font.setPixelSize(22);
    title_font.setWeight(QFont::Bold);
    title_font.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    painter.setFont(title_font);
    painter.drawText(QPointF(72, 632), sample.title);
    QFont descriptor_font = label_font;
    descriptor_font.setPixelSize(15);
    descriptor_font.setWeight(QFont::Normal);
    descriptor_font.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
    painter.setFont(descriptor_font);
    painter.drawText(QPointF(72, 660), sample.descriptor);
    painter.end();
    const auto path = QDir(directory).filePath(sample.name + QStringLiteral(".png"));
    if (!background.save(path)) {
      std::cerr << "Could not write gallery image: " << path.toStdString() << '\n';
      return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication application(argc, argv);
  using Settings = PhotoTextDimensionSettings;

  QString gallery_directory;
  if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--write-gallery"))
    gallery_directory = QString::fromLocal8Bit(argv[2]);
  else if (argc != 1)
    return fail(QStringLiteral("Usage: photo_text_dimension_render_test [--write-gallery directory]"));

  const auto family = installed_outline_family();
  if (family.isEmpty()) return fail(QStringLiteral(
      "The installed font database has no outline-capable font for renderer fixtures."));

  auto flat_style = text_style(family, 54.0);
  flat_style.tracking = 0.6;
  flat_style.color = QColor(QStringLiteral("#fff7eb"));
  const QSize box(520, 180);
  const auto plain_layer = text_layer(QStringLiteral("Flat keeps its glyphs"), flat_style, nullptr, box);
  const auto plain = render_frame(plain_layer, box, QStringLiteral("dimension absent"));
  QString raster_error;
  const auto expected_plain = to_linear_float(rasterize_legacy_photo_text(plain_layer, box, &raster_error));
  if (!raster_error.isEmpty() || plain.image.isNull() || expected_plain.isNull() ||
      plain.image.format() != QImage::Format_RGBA32FPx4 ||
      plain.image.colorSpace() != QColorSpace(QColorSpace::SRgbLinear) ||
      plain.image.size() != box || plain.padding != QPoint() ||
      !close_rect(plain.logical_bounds, QRectF(0.0, 0.0, box.width(), box.height())) ||
      changed_float_pixels(plain.image, expected_plain, 0.001) != 0)
    return fail(QStringLiteral("Absent dimensional settings must preserve the production flat raster exactly."));

  Settings disabled;
  const auto disabled_layer = text_layer(QStringLiteral("Flat keeps its glyphs"), flat_style, &disabled, box);
  const auto disabled_frame = render_frame(disabled_layer, box, QStringLiteral("dimension disabled"));
  if (photo_text_dimension_is_active(disabled_layer) || disabled_frame.image.isNull() ||
      disabled_frame.image.size() != box || disabled_frame.padding != QPoint() ||
      !close_rect(disabled_frame.logical_bounds, plain.logical_bounds) ||
      changed_float_pixels(disabled_frame.image, plain.image, 0.001) != 0)
    return fail(QStringLiteral("A disabled envelope must render exactly like legacy flat text."));

  auto identity_pins = disabled;
  identity_pins.corner_pin_enabled = true;
  const auto identity_layer = text_layer(QStringLiteral("Flat keeps its glyphs"), flat_style,
                                         &identity_pins, box);
  QString identity_error;
  const auto identity_preflight = preflight_photo_text_dimension(identity_layer, box, &identity_error);
  const auto identity_frame = render_frame(identity_layer, box, QStringLiteral("identity corner pins"));
  if (!identity_error.isEmpty() || identity_frame.image.isNull() ||
      !close_rect(identity_preflight, identity_frame.logical_bounds) ||
      identity_preflight != QRectF(0.0, 0.0, box.width(), box.height()) ||
      identity_frame.image.size() != box || identity_frame.padding != QPoint() ||
      changed_float_pixels(identity_frame.image, plain.image, 0.001) != 0)
    return fail(QStringLiteral("Identity pin-only preflight and rendering must agree and preserve flat pixels."));

  auto pinned = identity_pins;
  pinned.corner_pins = {QPointF(0.08, 0.10), QPointF(0.92, 0.02),
      QPointF(0.99, 0.89), QPointF(0.01, 0.98)};
  const auto pinned_layer = text_layer(QStringLiteral("Four point plane"), flat_style, &pinned, box);
  QString pinned_error;
  const auto pinned_preflight = preflight_photo_text_dimension(pinned_layer, box, &pinned_error);
  const auto pinned_frame = render_frame(pinned_layer, box, QStringLiteral("four point projection"));
  if (!pinned_error.isEmpty() || pinned_frame.image.isNull() ||
      !close_rect(pinned_preflight, pinned_frame.logical_bounds) ||
      pinned_frame.padding == QPoint() || pinned_frame.image.size() == box ||
      changed_frame_pixels(pinned_frame, identity_frame) < 100)
    return fail(QStringLiteral("Non-identity four-corner projection must render within its admitted bounds."));

  Settings dimensional;
  dimensional.enabled = true;
  dimensional.depth = 38.0;
  dimensional.bevel_width = 3.5;
  dimensional.bevel_segments = 5;
  dimensional.rotation_x = -13.0;
  dimensional.rotation_y = 19.0;
  dimensional.rotation_z = 7.0;
  dimensional.pivot_x = 0.46;
  dimensional.pivot_y = 0.53;
  dimensional.focal_length = 920.0;
  dimensional.use_text_color = false;
  dimensional.front_color = QColor(QStringLiteral("#fff6e8d0"));
  dimensional.side_color = QColor(QStringLiteral("#ff1764d0"));
  dimensional.bevel_color = QColor(QStringLiteral("#ffffe7ad"));
  dimensional.roughness = 0.22;
  dimensional.metallic = 0.26;
  dimensional.light_azimuth = -38.0;
  dimensional.light_elevation = 47.0;
  dimensional.light_intensity = 10.0;
  dimensional.ambient = 0.55;
  const auto dimension_layer = text_layer(QStringLiteral("3D depth"), flat_style, &dimensional, box);
  QString dimension_error;
  const auto dimension_preflight = preflight_photo_text_dimension(dimension_layer, box, &dimension_error);
  const auto dimension_frame = render_frame(dimension_layer, box, QStringLiteral("rotated extruded text"));
  const auto dimension_stats = pixel_stats(dimension_frame.image);
  if (!dimension_error.isEmpty() || dimension_frame.image.isNull() ||
      !close_rect(dimension_preflight, dimension_frame.logical_bounds) ||
      dimension_frame.image.format() != QImage::Format_RGBA32FPx4 ||
      dimension_frame.image.colorSpace() != QColorSpace(QColorSpace::SRgbLinear) ||
      dimension_frame.image.size() == box || dimension_stats.covered < 500 ||
      dimension_stats.fractional_alpha < 20 || dimension_stats.maximum_channel <= 1.0)
    return fail(QStringLiteral("Extruded text must retain padded antialiased linear-float HDR output."));

  auto alternate_side = dimensional;
  alternate_side.side_color = QColor(QStringLiteral("#ff16bf63"));
  const auto alternate_side_layer = text_layer(QStringLiteral("3D depth"), flat_style, &alternate_side, box);
  const auto alternate_side_frame = render_frame(alternate_side_layer, box, QStringLiteral("alternate side material"));
  if (alternate_side_frame.image.isNull() ||
      !close_rect(dimension_frame.logical_bounds, alternate_side_frame.logical_bounds) ||
      changed_float_pixels(dimension_frame.image, alternate_side_frame.image) < 40)
    return fail(QStringLiteral("Changing the side material must change rendered extrusion pixels."));
  auto flat_geometry = dimensional;
  flat_geometry.depth = 0.0;
  flat_geometry.bevel_width = 0.0;
  const auto flat_side_a = render_frame(text_layer(QStringLiteral("3D depth"), flat_style,
      &flat_geometry, box), box, QStringLiteral("zero-depth material A"));
  auto flat_geometry_b = flat_geometry;
  flat_geometry_b.side_color = alternate_side.side_color;
  const auto flat_side_b = render_frame(text_layer(QStringLiteral("3D depth"), flat_style,
      &flat_geometry_b, box), box, QStringLiteral("zero-depth material B"));
  if (flat_side_a.image.isNull() || flat_side_b.image.isNull() ||
      changed_float_pixels(flat_side_a.image, flat_side_b.image) > 2)
    return fail(QStringLiteral("A zero-depth surface must not gain side-color pixels."));

  auto shadow_style = flat_style;
  shadow_style.shadow_x = 34.0;
  shadow_style.shadow_y = 21.0;
  shadow_style.shadow_color = QColor(QStringLiteral("#d9000000"));
  const auto shadow_layer = text_layer(QStringLiteral("Shadow finish"), shadow_style, &dimensional, box);
  const auto no_shadow_layer = text_layer(QStringLiteral("Shadow finish"), flat_style, &dimensional, box);
  QString no_shadow_error, shadow_error;
  const auto no_shadow_bounds = preflight_photo_text_dimension(no_shadow_layer, box, &no_shadow_error);
  const auto shadow_bounds = preflight_photo_text_dimension(shadow_layer, box, &shadow_error);
  const auto no_shadow_frame = render_frame(no_shadow_layer, box, QStringLiteral("unshadowed reference"));
  const auto shadow_frame = render_frame(shadow_layer, box, QStringLiteral("shadow finishing"));
  const auto no_shadow_stats = pixel_stats(no_shadow_frame.image);
  const auto shadow_stats = pixel_stats(shadow_frame.image);
  const QRectF unshadowed_ink_bounds(
      no_shadow_bounds.x() + no_shadow_stats.alpha_bounds.x(),
      no_shadow_bounds.y() + no_shadow_stats.alpha_bounds.y(),
      no_shadow_stats.alpha_bounds.width(), no_shadow_stats.alpha_bounds.height());
  qint64 outside_shadow_ink = 0;
  for (int y = 0; y < shadow_frame.image.height(); ++y) {
    for (int x = 0; x < shadow_frame.image.width(); ++x) {
      if (channel(shadow_frame.image, x, y, 3) <= 0.01) continue;
      const QPointF logical(shadow_frame.logical_bounds.x() + x + 0.5,
                            shadow_frame.logical_bounds.y() + y + 0.5);
      if (!unshadowed_ink_bounds.contains(logical))
        ++outside_shadow_ink;
    }
  }
  const bool shadow_bounds_grew_x = shadow_bounds.right() > no_shadow_bounds.right() + 20.0;
  const bool shadow_bounds_grew_y = shadow_bounds.bottom() > no_shadow_bounds.bottom() + 10.0;
  const bool shadow_ink_escapes_unshadowed_bounds = outside_shadow_ink >= 10;
  const bool shadow_is_unclipped = shadow_stats.maximum_border_alpha <= 1.0e-5;
  if (!no_shadow_error.isEmpty() || !shadow_error.isEmpty() || no_shadow_frame.image.isNull() ||
      no_shadow_stats.covered == 0 || shadow_frame.image.isNull() ||
      !shadow_bounds_grew_x || !shadow_bounds_grew_y ||
      !shadow_ink_escapes_unshadowed_bounds || !shadow_is_unclipped) {
    std::cerr << "Shadow diagnostics: preflight errors=('" << no_shadow_error.toStdString()
              << "','" << shadow_error.toStdString() << "'), frame="
              << shadow_frame.image.width() << 'x' << shadow_frame.image.height()
              << ", no-shadow bounds=(" << no_shadow_bounds.x() << ',' << no_shadow_bounds.y()
              << ',' << no_shadow_bounds.width() << ',' << no_shadow_bounds.height()
              << "), shadow bounds=(" << shadow_bounds.x() << ',' << shadow_bounds.y()
              << ',' << shadow_bounds.width() << ',' << shadow_bounds.height()
              << "), shadow growth=(" << shadow_bounds.right() - no_shadow_bounds.right()
              << ',' << shadow_bounds.bottom() - no_shadow_bounds.bottom()
              << "), unshadowed ink bounds=(" << unshadowed_ink_bounds.x() << ','
              << unshadowed_ink_bounds.y() << ',' << unshadowed_ink_bounds.width()
              << ',' << unshadowed_ink_bounds.height() << "), outside ink=" << outside_shadow_ink
              << ", border alpha=" << shadow_stats.maximum_border_alpha << '\n';
    return fail(QStringLiteral("Typography shadow must be visible and admitted without clipping at the output bounds."));
  }

  auto near_plane = dimensional;
  near_plane.depth = 1500.0;
  near_plane.bevel_width = 0.0;
  near_plane.rotation_x = 180.0;
  near_plane.focal_length = 1000.0;
  const auto near_layer = text_layer(QStringLiteral("Near plane"), flat_style, &near_plane, box);
  QString near_error;
  const auto near_bounds = preflight_photo_text_dimension(near_layer, box, &near_error);
  const auto near_frame = render_photo_text_dimension(near_layer, box);
  if (near_error.isEmpty() || !near_error.contains(QStringLiteral("near plane"), Qt::CaseInsensitive) ||
      near_bounds.isValid() || near_frame.error.isEmpty() || !near_frame.image.isNull())
    return fail(QStringLiteral("Geometry crossing the camera near plane must be rejected before rendering."));

  auto out_of_range_layer = text_layer(QStringLiteral("Invalid pin"), flat_style,
      &pinned, box);
  auto invalid_pin_envelope = out_of_range_layer.value(QStringLiteral("textDimension")).toMap();
  auto invalid_pin_points = invalid_pin_envelope.value(QStringLiteral("cornerPins")).toList();
  auto invalid_pin_point = invalid_pin_points.at(1).toMap();
  invalid_pin_point.insert(QStringLiteral("x"), 4.5);
  invalid_pin_points[1] = invalid_pin_point;
  invalid_pin_envelope.insert(QStringLiteral("cornerPins"), invalid_pin_points);
  out_of_range_layer.insert(QStringLiteral("textDimension"), invalid_pin_envelope);
  QString pin_error;
  const auto invalid_pin_bounds = preflight_photo_text_dimension(out_of_range_layer, box, &pin_error);
  const auto invalid_pin_frame = render_photo_text_dimension(out_of_range_layer, box);
  if (pin_error.isEmpty() || invalid_pin_bounds.isValid() ||
      invalid_pin_frame.error.isEmpty() || !invalid_pin_frame.image.isNull())
    return fail(QStringLiteral("Corner pins outside the normalized coordinate limit must fail closed."));

  auto crossing_layer = text_layer(QStringLiteral("Crossing pins"), flat_style,
      &pinned, box);
  auto crossing_envelope = crossing_layer.value(QStringLiteral("textDimension")).toMap();
  crossing_envelope.insert(QStringLiteral("cornerPins"), QVariantList{
      QVariantMap{{QStringLiteral("x"), 0.1}, {QStringLiteral("y"), 0.1}},
      QVariantMap{{QStringLiteral("x"), 0.9}, {QStringLiteral("y"), 0.9}},
      QVariantMap{{QStringLiteral("x"), 0.9}, {QStringLiteral("y"), 0.1}},
      QVariantMap{{QStringLiteral("x"), 0.1}, {QStringLiteral("y"), 0.9}}});
  crossing_layer.insert(QStringLiteral("textDimension"), crossing_envelope);
  QString crossing_error;
  if (preflight_photo_text_dimension(crossing_layer, box, &crossing_error).isValid() ||
      crossing_error.isEmpty())
    return fail(QStringLiteral("Self-intersecting four-point projection must be rejected."));

  if (!gallery_directory.isEmpty() && !write_gallery(gallery_directory, family)) return 1;
  std::cout << "Dimensional text adapter preserves flat text, admits bounded projections, and renders linear 3D frames\n";
  return 0;
}
