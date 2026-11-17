#pragma once
#include <QJsonObject>
#include <QVariantMap>
#include <cmath>
#include <optional>

namespace photo_editor::desktop {
struct GaussianToneSettings {
  float contrast = 1;
  float highlight_compression = 0;
  float warmth = 0, tint = 0, vibrance = 0, fade = 0;
  bool operator==(const GaussianToneSettings&) const = default;
  bool valid() const {
    return std::isfinite(contrast) && contrast >= 0.25F && contrast <= 2 &&
        std::isfinite(highlight_compression) && highlight_compression >= 0 && highlight_compression <= 1 &&
        std::isfinite(warmth) && warmth >= -1 && warmth <= 1 &&
        std::isfinite(tint) && tint >= -1 && tint <= 1 &&
        std::isfinite(vibrance) && vibrance >= -1 && vibrance <= 1 &&
        std::isfinite(fade) && fade >= 0 && fade <= 0.25F;
  }
  QVariantMap description() const {
    QVariantMap result{{"schema","framehouse-camera-tone-v1"},{"contrast",contrast},{"highlightCompression",highlight_compression}};
    if (warmth != 0 || tint != 0 || vibrance != 0 || fade != 0) {
      result.insert("schema","framehouse-camera-tone-v2");
      result.insert("warmth",warmth); result.insert("tint",tint);
      result.insert("vibrance",vibrance); result.insert("fade",fade);
    }
    return result;
  }
};
inline std::optional<GaussianToneSettings> gaussian_tone_settings(const QVariantMap& value) {
  const auto json = QJsonObject::fromVariantMap(value);
  const bool color = json.value("schema") == "framehouse-camera-tone-v2";
  if (json.size() != (color ? 7 : 3) || (!color && json.value("schema") != "framehouse-camera-tone-v1") ||
      !json.value("contrast").isDouble() || !json.value("highlightCompression").isDouble()) return {};
  const auto contrast = json.value("contrast").toDouble(), compression = json.value("highlightCompression").toDouble();
  if (!std::isfinite(contrast) || contrast < 0.25 || contrast > 2 ||
      !std::isfinite(compression) || compression < 0 || compression > 1) return {};
  GaussianToneSettings result{static_cast<float>(contrast),static_cast<float>(compression)};
  if (color) {
    for (const auto* key : {"warmth","tint","vibrance","fade"})
      if (!json.value(QLatin1String(key)).isDouble()) return {};
    result.warmth = static_cast<float>(json.value("warmth").toDouble());
    result.tint = static_cast<float>(json.value("tint").toDouble());
    result.vibrance = static_cast<float>(json.value("vibrance").toDouble());
    result.fade = static_cast<float>(json.value("fade").toDouble());
  }
  return result.valid() ? std::optional<GaussianToneSettings>(result) : std::nullopt;
}
}  // namespace photo_editor::desktop
