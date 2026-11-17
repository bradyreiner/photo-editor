#include "timeline_source_matte.h"
#include "qimage_color_conversion.h"

#include <QColorSpace>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace photo_editor::desktop {
namespace {

QString normalized_matte_mode(const QString& mode) {
  return mode.trimmed().toLower();
}

double linear_srgb_channel(int value) {
  const auto encoded = static_cast<double>(value) / 255.0;
  return encoded <= 0.04045
             ? encoded / 12.92
             : std::pow((encoded + 0.055) / 1.055, 2.4);
}

bool valid_filter_label(const QString& label) {
  if (label.isEmpty()) return false;
  for (const auto character : label) {
    const auto code = character.unicode();
    const auto alpha_numeric =
        (code >= u'a' && code <= u'z') || (code >= u'A' && code <= u'Z') ||
        (code >= u'0' && code <= u'9');
    if (!alpha_numeric && character != QLatin1Char('_') &&
        character != QLatin1Char('.') && character != QLatin1Char(':')) {
      return false;
    }
  }
  return true;
}

}  // namespace

QImage timeline_source_matte_alpha(
    const QImage& matte,
    const QString& mode,
    bool invert,
    const QSize& target_size,
    QString* error_message) {
  const auto fail = [error_message](const QString& message) {
    if (error_message != nullptr) *error_message = message;
    return QImage{};
  };
  if (error_message != nullptr) error_message->clear();
  const auto normalized_mode = normalized_matte_mode(mode);
  if (matte.isNull() ||
      (normalized_mode != QStringLiteral("alpha") &&
       normalized_mode != QStringLiteral("luminance"))) {
    return fail(QStringLiteral(
        "A readable matte image and the alpha or luminance mode are required."));
  }
  if (target_size.isValid() && target_size.isEmpty()) {
    return fail(QStringLiteral("The matte target size must be positive."));
  }

  auto color_managed = matte;
  const auto srgb = QColorSpace(QColorSpace::SRgb);
  if (!color_managed.colorSpace().isValid()) {
    color_managed.setColorSpace(srgb);
  } else if (color_managed.colorSpace() != srgb) {
    color_managed = convert_tagged_qimage_color_space(color_managed, srgb, QImageColorConversionPurpose::DecodeNormalization);
    if (color_managed.isNull()) {
      return fail(QStringLiteral("The matte color space could not be converted to sRGB."));
    }
  }
  auto straight_rgba = color_managed.convertToFormat(QImage::Format_ARGB32);
  if (straight_rgba.isNull()) {
    return fail(QStringLiteral("The matte could not be decoded as RGBA pixels."));
  }
  if (target_size.isValid() && target_size != straight_rgba.size()) {
    straight_rgba = straight_rgba.scaled(
        target_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (straight_rgba.isNull()) {
      return fail(QStringLiteral("The matte could not be smooth-scaled."));
    }
  }

  QImage alpha_image(straight_rgba.size(), QImage::Format_ARGB32);
  if (alpha_image.isNull()) {
    return fail(QStringLiteral("The matte alpha image could not be allocated."));
  }
  alpha_image.setColorSpace(srgb);
  for (int y = 0; y < straight_rgba.height(); ++y) {
    const auto* source_row =
        reinterpret_cast<const QRgb*>(straight_rgba.constScanLine(y));
    auto* output_row = reinterpret_cast<QRgb*>(alpha_image.scanLine(y));
    for (int x = 0; x < straight_rgba.width(); ++x) {
      const auto source_pixel = source_row[x];
      auto coverage = 0.0;
      if (normalized_mode == QStringLiteral("alpha")) {
        coverage = static_cast<double>(qAlpha(source_pixel)) / 255.0;
      } else {
        const auto luminance =
            0.2126 * linear_srgb_channel(qRed(source_pixel)) +
            0.7152 * linear_srgb_channel(qGreen(source_pixel)) +
            0.0722 * linear_srgb_channel(qBlue(source_pixel));
        coverage = luminance *
                   (static_cast<double>(qAlpha(source_pixel)) / 255.0);
      }
      if (invert) coverage = 1.0 - coverage;
      const auto alpha = static_cast<int>(std::lround(
          std::clamp(coverage, 0.0, 1.0) * 255.0));
      output_row[x] = qRgba(255, 255, 255, alpha);
    }
  }
  return alpha_image;
}

QString timeline_source_matte_ffmpeg_chain(
    const QString& source_label,
    const QString& matte_alpha_label,
    const QString& output_label,
    const QString& unique_prefix,
    const QSize& target_size,
    const QColor& fill_color,
    QString* error_message) {
  if (error_message != nullptr) error_message->clear();
  if (!valid_filter_label(source_label) ||
      !valid_filter_label(matte_alpha_label) ||
      !valid_filter_label(output_label) ||
      !valid_filter_label(unique_prefix) || !target_size.isValid() ||
      target_size.isEmpty()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral(
          "Source matte filter labels and target dimensions must be valid.");
    }
    return {};
  }

  const auto source_rgb_label = unique_prefix + QStringLiteral("_source_rgb");
  const auto source_alpha_input_label =
      unique_prefix + QStringLiteral("_source_alpha_input");
  const auto source_alpha_label = unique_prefix + QStringLiteral("_source_alpha");
  const auto matte_alpha_out_label = unique_prefix + QStringLiteral("_matte_alpha");
  const auto combined_alpha_label = unique_prefix + QStringLiteral("_combined_alpha");
  const auto source_input_label = fill_color.isValid()
      ? unique_prefix + QStringLiteral("_filled_source")
      : source_label;

  QStringList filters;
  if (fill_color.isValid()) {
    filters.push_back(
        QStringLiteral("[%1]format=rgba,geq=r='%2':g='%3':b='%4':a='alpha(X,Y)*%5'[%6]")
            .arg(source_label)
            .arg(fill_color.red())
            .arg(fill_color.green())
            .arg(fill_color.blue())
            .arg(QString::number(fill_color.alphaF(), 'g', 12), source_input_label));
  }
  filters.push_back(
      QStringLiteral("[%1]format=rgba,split=2[%2][%3]")
          .arg(source_input_label, source_rgb_label, source_alpha_input_label));
  filters.push_back(
      QStringLiteral("[%1]alphaextract,format=gray[%2]")
          .arg(source_alpha_input_label, source_alpha_label));
  filters.push_back(
      QStringLiteral("[%1]format=rgba,scale=%2:%3:flags=lanczos,"
                     "format=rgba,alphaextract,format=gray[%4]")
          .arg(matte_alpha_label)
          .arg(target_size.width())
          .arg(target_size.height())
          .arg(matte_alpha_out_label));
  filters.push_back(
      QStringLiteral("[%1][%2]blend=all_mode=multiply:shortest=1[%3]")
          .arg(source_alpha_label, matte_alpha_out_label, combined_alpha_label));
  filters.push_back(
      QStringLiteral("[%1][%2]alphamerge=shortest=1[%3]")
          .arg(source_rgb_label, combined_alpha_label, output_label));
  return filters.join(QLatin1Char(';'));
}

}  // namespace photo_editor::desktop
