#include "timeline_frame_shadow.h"

#include <algorithm>
#include <cmath>

namespace photo_editor::desktop::timeline_frame_shadow {
namespace {

constexpr double kMaximumOffset = 512.0;
constexpr double kMaximumBlurRadius = 256.0;
constexpr double kMaximumSpread = 64.0;

QString number(double value) {
  return QString::number(value, 'f', 4);
}

QString label_token(QString value) {
  for (auto& character : value) {
    if (!character.isLetterOrNumber() && character != QLatin1Char('_')) {
      character = QLatin1Char('_');
    }
  }
  return value.isEmpty() ? QStringLiteral("shadow") : value;
}

void set_error(QString* error_message, const QString& message) {
  if (error_message != nullptr) *error_message = message;
}

}  // namespace

QVariantMap default_parameters() {
  return {{QStringLiteral("enabled"), false},
          {QStringLiteral("color"), QStringLiteral("#00000000")},
          {QStringLiteral("offsetX"), 0.0},
          {QStringLiteral("offsetY"), 0.0},
          {QStringLiteral("blurRadius"), 0.0},
          {QStringLiteral("spread"), 0.0}};
}

bool parse_parameters(const QVariantMap& map,
                      Parameters* parameters,
                      QString* error_message) {
  if (parameters == nullptr) {
    set_error(error_message, QStringLiteral("A frame-shadow output is required."));
    return false;
  }
  const auto color = QColor(map.value(QStringLiteral("color"),
                                      QStringLiteral("#00000000")).toString());
  const auto offset_x = map.value(QStringLiteral("offsetX"), 0.0).toDouble();
  const auto offset_y = map.value(QStringLiteral("offsetY"), 0.0).toDouble();
  const auto blur_radius = map.value(QStringLiteral("blurRadius"), 0.0).toDouble();
  const auto spread = map.value(QStringLiteral("spread"), 0.0).toDouble();
  if (!map.value(QStringLiteral("enabled")).isValid() ||
      !color.isValid() || !std::isfinite(offset_x) || !std::isfinite(offset_y) ||
      !std::isfinite(blur_radius) || !std::isfinite(spread) ||
      std::abs(offset_x) > kMaximumOffset || std::abs(offset_y) > kMaximumOffset ||
      blur_radius < 0.0 || blur_radius > kMaximumBlurRadius ||
      std::abs(spread) > kMaximumSpread) {
    set_error(error_message,
              QStringLiteral("Frame shadow needs an enabled flag, valid color, offsets within ±512 px, blur radius 0–256 px, and spread within ±64 px."));
    return false;
  }
  parameters->enabled = map.value(QStringLiteral("enabled")).toBool();
  parameters->color = color;
  parameters->offset_x = offset_x;
  parameters->offset_y = offset_y;
  parameters->blur_radius = blur_radius;
  parameters->spread = spread;
  return true;
}

bool is_default_parameters(const Parameters& parameters) {
  return !parameters.enabled && parameters.color == QColor(QStringLiteral("#00000000")) &&
         std::abs(parameters.offset_x) <= 1.0e-9 &&
         std::abs(parameters.offset_y) <= 1.0e-9 &&
         std::abs(parameters.blur_radius) <= 1.0e-9 &&
         std::abs(parameters.spread) <= 1.0e-9;
}

QVariantMap to_variant_map(const Parameters& parameters) {
  return {{QStringLiteral("enabled"), parameters.enabled},
          {QStringLiteral("color"), parameters.color.name(QColor::HexArgb)},
          {QStringLiteral("offsetX"), parameters.offset_x},
          {QStringLiteral("offsetY"), parameters.offset_y},
          {QStringLiteral("blurRadius"), parameters.blur_radius},
          {QStringLiteral("spread"), parameters.spread}};
}

QString ffmpeg_filter(const Parameters& parameters,
                      const QSize& frame_size,
                      const QSize& sequence_size,
                      const QSize& target_size,
                      const QString& unique_prefix,
                      const QString& output_label,
                      QSize* padded_frame_size,
                      QString* error_message,
                      bool high_precision_compositing) {
  if (!parameters.enabled || parameters.color.alpha() == 0) {
    if (padded_frame_size != nullptr) *padded_frame_size = frame_size;
    return QString{};
  }
  if (frame_size.width() < 1 || frame_size.height() < 1 ||
      sequence_size.width() < 1 || sequence_size.height() < 1 ||
      target_size.width() < 1 || target_size.height() < 1 ||
      output_label.isEmpty()) {
    set_error(error_message, QStringLiteral("Frame-shadow render dimensions and output label must be valid."));
    return QString{};
  }

  const auto scale_x = static_cast<double>(target_size.width()) /
                       static_cast<double>(sequence_size.width());
  const auto scale_y = static_cast<double>(target_size.height()) /
                       static_cast<double>(sequence_size.height());
  const auto offset_x = qRound(parameters.offset_x * scale_x);
  const auto offset_y = qRound(parameters.offset_y * scale_y);
  const auto spread = qRound(parameters.spread * std::min(scale_x, scale_y));
  // CSS box-shadow blur-radius is approximately twice the Gaussian sigma.
  const auto sigma = parameters.blur_radius * 0.5 * std::min(scale_x, scale_y);
  const auto padding = qMax(1, static_cast<int>(std::ceil(
      std::max(std::abs(offset_x), std::abs(offset_y)) +
      std::abs(spread) + sigma * 3.0 + 2.0)));
  const auto padded_width = frame_size.width() + padding * 2;
  const auto padded_height = frame_size.height() + padding * 2;
  if (padded_frame_size != nullptr) {
    *padded_frame_size = QSize(padded_width, padded_height);
  }

  const auto prefix = label_token(unique_prefix);
  const auto foreground = prefix + QStringLiteral("_fg");
  const auto alpha_source = prefix + QStringLiteral("_alpha_source");
  const auto shadow = prefix + QStringLiteral("_shadow");
  const auto foreground_padded = prefix + QStringLiteral("_fg_padded");

  QString morphology;
  for (int index = 0; index < std::abs(spread); ++index) {
    morphology += spread > 0 ? QStringLiteral("dilation=threshold0=65535,")
                             : QStringLiteral("erosion=threshold0=65535,");
  }
  const auto alpha_x = padding + offset_x;
  const auto alpha_y = padding + offset_y;
  const auto shadow_alpha_fraction =
      QString::number(static_cast<double>(parameters.color.alpha()) / 255.0, 'f', 8);
  const auto channel_scale = high_precision_compositing ? 257 : 1;
  const auto r = parameters.color.red() * channel_scale;
  const auto g = parameters.color.green() * channel_scale;
  const auto b = parameters.color.blue() * channel_scale;
  const auto blur_filter = sigma > 0.001
      ? QStringLiteral("gblur=sigma=%1:steps=3,").arg(number(sigma))
      : QString{};
  QString graph = QStringLiteral(
      "format=rgba,split=2[%1][%2];"
      "[%2]alphaextract,format=gray,pad=%3:%4:%5:%6:color=black,%7"
      "%8format=rgba,geq=r=%9:g=%10:b=%11:a='r(X,Y)*%12'[%13];"
      "[%1]format=rgba,pad=%3:%4:%14:%15:color=black@0[%16];"
      "[%13][%16]overlay=x=0:y=0:shortest=1:format=auto:alpha=straight,"
      "format=rgba[%17];")
      .arg(foreground, alpha_source)
      .arg(padded_width)
      .arg(padded_height)
      .arg(alpha_x)
      .arg(alpha_y)
      .arg(morphology)
      .arg(blur_filter)
      .arg(r)
      .arg(g)
      .arg(b)
      .arg(shadow_alpha_fraction)
      .arg(shadow)
      .arg(padding)
      .arg(padding)
      .arg(foreground_padded)
      .arg(output_label);
  // The first input to split is the unlabelled end of the incoming frame-mode
  // chain. Both branches are padded symmetrically so later frame transforms
  // retain the original frame center.
  return graph;
}

}  // namespace photo_editor::desktop::timeline_frame_shadow
