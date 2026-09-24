#pragma once
#include <QJsonObject>
#include <QVariantMap>
#include <cmath>
#include <optional>

namespace photo_editor::desktop {
// Creative display-referred effects, not a calibrated emulsion model.
struct GaussianFilmSettings {
  float grain_amount = 0;
  float grain_size = 1.5F; // Pixels at a 2048-pixel long edge.
  float halation_amount = 0;
  float halation_radius = 4; // Gaussian sigma at a 2048-pixel long edge.
  float halation_threshold = 0.8F; // Linearized luminance after exposure.
  quint32 seed = 1;
  bool operator==(const GaussianFilmSettings&) const = default;
  bool active() const { return grain_amount > 0 || halation_amount > 0; }
  bool valid() const {
    return std::isfinite(grain_amount) && grain_amount >= 0 && grain_amount <= 1 &&
        std::isfinite(grain_size) && grain_size >= 0.5F && grain_size <= 6 &&
        std::isfinite(halation_amount) && halation_amount >= 0 && halation_amount <= 1 &&
        std::isfinite(halation_radius) && halation_radius >= 0.5F && halation_radius <= 16 &&
        std::isfinite(halation_threshold) && halation_threshold >= 0.1F && halation_threshold <= 4;
  }
  QVariantMap description() const {
    return {{"schema", "framehouse-creative-film-v1"}, {"grainAmount", grain_amount},
        {"grainSize", grain_size}, {"halationAmount", halation_amount},
        {"halationRadius", halation_radius}, {"halationThreshold", halation_threshold}, {"seed", seed}};
  }
};
inline std::optional<GaussianFilmSettings> gaussian_film_settings(const QVariantMap& value) {
  const auto json = QJsonObject::fromVariantMap(value);
  if (json.size() != 7 || json.value("schema") != "framehouse-creative-film-v1") return {};
  const auto number = [&](const char* key, double low, double high) {
    const auto v = json.value(QLatin1String(key));
    return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() >= low && v.toDouble() <= high;
  };
  if (!number("grainAmount", 0, 1) || !number("grainSize", 0.5, 6) ||
      !number("halationAmount", 0, 1) || !number("halationRadius", 0.5, 16) ||
      !number("halationThreshold", 0.1, 4) || !number("seed", 0, 4294967295.0) ||
      json.value("seed").toDouble() != std::floor(json.value("seed").toDouble())) return {};
  return GaussianFilmSettings{static_cast<float>(json.value("grainAmount").toDouble()),
      static_cast<float>(json.value("grainSize").toDouble()),
      static_cast<float>(json.value("halationAmount").toDouble()),
      static_cast<float>(json.value("halationRadius").toDouble()),
      static_cast<float>(json.value("halationThreshold").toDouble()),
      static_cast<quint32>(json.value("seed").toDouble())};
}
}  // namespace photo_editor::desktop
