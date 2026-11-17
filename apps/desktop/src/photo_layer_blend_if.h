#pragma once

#include "photo_editor/pipeline/blend_if.h"

#include <QVariantMap>
#include <QString>
#include <array>
#include <cmath>
#include <optional>

namespace photo_editor::desktop {

inline constexpr std::array<std::pair<const char*, double photo_editor::pipeline::LayerBlendIf::*>, 8>
    blend_if_fields{{
        {"sourceBlackLow", &photo_editor::pipeline::LayerBlendIf::source_black_low},
        {"sourceBlackHigh", &photo_editor::pipeline::LayerBlendIf::source_black_high},
        {"sourceWhiteLow", &photo_editor::pipeline::LayerBlendIf::source_white_low},
        {"sourceWhiteHigh", &photo_editor::pipeline::LayerBlendIf::source_white_high},
        {"underlyingBlackLow", &photo_editor::pipeline::LayerBlendIf::underlying_black_low},
        {"underlyingBlackHigh", &photo_editor::pipeline::LayerBlendIf::underlying_black_high},
        {"underlyingWhiteLow", &photo_editor::pipeline::LayerBlendIf::underlying_white_low},
        {"underlyingWhiteHigh", &photo_editor::pipeline::LayerBlendIf::underlying_white_high},
    }};

inline std::optional<photo_editor::pipeline::LayerBlendIf> blend_if_from_variant(
    const QVariantMap& map, QString* error = nullptr) {
  using namespace photo_editor::pipeline;
  if (error) error->clear();
  LayerBlendIf result;
  result.enabled = map.value(QStringLiteral("enabled"), false).toBool();
  const auto channel = map.value(QStringLiteral("channel"), QStringLiteral("gray")).toString();
  if (channel == QStringLiteral("gray")) result.channel = BlendIfChannel::Gray;
  else if (channel == QStringLiteral("red")) result.channel = BlendIfChannel::Red;
  else if (channel == QStringLiteral("green")) result.channel = BlendIfChannel::Green;
  else if (channel == QStringLiteral("blue")) result.channel = BlendIfChannel::Blue;
  else { if (error) *error = QStringLiteral("Choose Gray, Red, Green or Blue for Blend If."); return std::nullopt; }
  for (const auto& [key, field] : blend_if_fields) {
    bool valid = false;
    const auto value = map.value(QLatin1String(key), result.*field).toDouble(&valid);
    if (!valid || !std::isfinite(value) || value < 0 || value > 1) {
      if (error) *error = QStringLiteral("Blend If ranges must be finite values between 0 and 1.");
      return std::nullopt;
    }
    result.*field = value;
  }
  if (result.source_black_low > result.source_black_high || result.source_white_low > result.source_white_high ||
      result.underlying_black_low > result.underlying_black_high || result.underlying_white_low > result.underlying_white_high) {
    if (error) *error = QStringLiteral("Each Blend If transition must run from its lower value to its higher value.");
    return std::nullopt;
  }
  return result;
}

inline QVariantMap blend_if_to_variant(const photo_editor::pipeline::LayerBlendIf& settings) {
  using photo_editor::pipeline::BlendIfChannel;
  const auto channel = settings.channel == BlendIfChannel::Red ? QStringLiteral("red")
      : settings.channel == BlendIfChannel::Green ? QStringLiteral("green")
      : settings.channel == BlendIfChannel::Blue ? QStringLiteral("blue") : QStringLiteral("gray");
  QVariantMap result{{QStringLiteral("enabled"), settings.enabled}, {QStringLiteral("channel"), channel}};
  for (const auto& [key, field] : blend_if_fields) result.insert(QLatin1String(key), settings.*field);
  return result;
}

}  // namespace photo_editor::desktop
