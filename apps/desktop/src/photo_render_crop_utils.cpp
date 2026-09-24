#include "photo_render_utils.h"

#include <QtMath>

#include <algorithm>

namespace photo_editor::desktop {

namespace {

double clamp_unit(double value) {
  return qBound(0.0, value, 1.0);
}

}  // namespace

double crop_aspect_ratio(const QString& preset) {
  auto normalized = preset.trimmed().toLower();
  normalized.replace(QLatin1Char(':'), QLatin1Char('x'));
  normalized.remove(QLatin1Char(' '));
  if (normalized == QStringLiteral("square") || normalized == QStringLiteral("1x1")) {
    return 1.0;
  }
  if (normalized == QStringLiteral("a3") || normalized == QStringLiteral("a4") ||
      normalized == QStringLiteral("a5")) {
    return 210.0 / 297.0;
  }
  if (normalized == QStringLiteral("a3landscape") ||
      normalized == QStringLiteral("a4landscape") ||
      normalized == QStringLiteral("a5landscape")) {
    return 297.0 / 210.0;
  }
  const auto parts = normalized.split(QLatin1Char('x'), Qt::SkipEmptyParts);
  if (parts.size() == 2) {
    bool width_ok = false;
    bool height_ok = false;
    auto width_label = parts.at(0);
    auto height_label = parts.at(1);
    for (const auto& unit : {QStringLiteral("inches"), QStringLiteral("inch"), QStringLiteral("in"),
                             QStringLiteral("cm"), QStringLiteral("mm")}) {
      width_label.remove(unit);
      height_label.remove(unit);
    }
    const auto width = width_label.toDouble(&width_ok);
    const auto height = height_label.toDouble(&height_ok);
    if (width_ok && height_ok && width > 0.0 && height > 0.0) {
      return width / height;
    }
  }
  return 0.0;
}

bool has_explicit_crop_rect(const RenderEditState& state) {
  return state.crop_rect_width > 0.0 && state.crop_rect_height > 0.0;
}

QRectF normalized_crop_rect_for_state(const QSize& size, const RenderEditState& state) {
  if (!size.isValid() || size.width() <= 0 || size.height() <= 0) {
    return QRectF(0.0, 0.0, 1.0, 1.0);
  }

  if (has_explicit_crop_rect(state)) {
    const auto width = clamp_unit(state.crop_rect_width);
    const auto height = clamp_unit(state.crop_rect_height);
    const auto x = clamp_unit(state.crop_rect_x);
    const auto y = clamp_unit(state.crop_rect_y);
    return QRectF(
        std::min(x, std::max(0.0, 1.0 - width)),
        std::min(y, std::max(0.0, 1.0 - height)),
        width,
        height);
  }

  auto crop_width = size.width();
  auto crop_height = size.height();
  const auto aspect_ratio = crop_aspect_ratio(state.crop_preset);
  if (aspect_ratio > 0.0) {
    const auto current_ratio = static_cast<double>(size.width()) / static_cast<double>(size.height());
    if (current_ratio > aspect_ratio) {
      crop_width = static_cast<int>(qRound(static_cast<double>(size.height()) * aspect_ratio));
    } else {
      crop_height = static_cast<int>(qRound(static_cast<double>(size.width()) / aspect_ratio));
    }
  }

  crop_width = qMax(1, static_cast<int>(qRound(static_cast<double>(crop_width) / state.crop_zoom)));
  crop_height = qMax(1, static_cast<int>(qRound(static_cast<double>(crop_height) / state.crop_zoom)));
  const auto x = qMax(0, (size.width() - crop_width) / 2);
  const auto y = qMax(0, (size.height() - crop_height) / 2);
  return QRectF(
      static_cast<double>(x) / static_cast<double>(size.width()),
      static_cast<double>(y) / static_cast<double>(size.height()),
      static_cast<double>(crop_width) / static_cast<double>(size.width()),
      static_cast<double>(crop_height) / static_cast<double>(size.height()));
}

QRect crop_rect_for_image(const QSize& size, const RenderEditState& state) {
  if (!size.isValid()) {
    return {};
  }

  const auto normalized = normalized_crop_rect_for_state(size, state);
  const auto x = qBound(
      0, static_cast<int>(qRound(normalized.x() * static_cast<double>(size.width()))), size.width() - 1);
  const auto y = qBound(
      0, static_cast<int>(qRound(normalized.y() * static_cast<double>(size.height()))), size.height() - 1);
  const auto width = qBound(
      1, static_cast<int>(qRound(normalized.width() * static_cast<double>(size.width()))), size.width() - x);
  const auto height = qBound(
      1, static_cast<int>(qRound(normalized.height() * static_cast<double>(size.height()))), size.height() - y);
  return {x, y, width, height};
}

QImage apply_crop(const QImage& image, const RenderEditState& state) {
  return image.copy(crop_rect_for_image(image.size(), state));
}

}  // namespace photo_editor::desktop
