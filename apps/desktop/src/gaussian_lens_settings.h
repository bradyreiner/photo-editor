#pragma once
#include <QJsonObject>
#include <QVariantMap>
#include <cmath>
#include <optional>

namespace photo_editor::desktop {
struct GaussianLensSettings {
  float focus_distance = 1;  // Scene units, not invented metres.
  float aperture_radius = 0; // Zero is exact pinhole mode.
  int blades = 0;            // Zero is circular; otherwise regular polygon.
  float rotation_degrees = 0;
  float roundness = 0;
  int samples = 32;
  bool operator==(const GaussianLensSettings&) const = default;
  bool valid() const {
    return std::isfinite(focus_distance) && focus_distance >= 0.01F && focus_distance <= 1e8F &&
        std::isfinite(aperture_radius) && aperture_radius >= 0 && aperture_radius <= focus_distance * 0.25F &&
        (blades == 0 || (blades >= 3 && blades <= 16)) &&
        std::isfinite(rotation_degrees) && rotation_degrees >= 0 && rotation_degrees <= 360 &&
        std::isfinite(roundness) && roundness >= 0 && roundness <= 1 &&
        (samples == 16 || samples == 32 || samples == 64 || samples == 128 ||
         samples == 256 || samples == 512 || samples == 1024 || samples == 2048 || samples == 4096);
  }
  QVariantMap description() const {
    return {{"schema", samples > 128 ? "framehouse-thin-lens-v2" : "framehouse-thin-lens-v1"}, {"focusDistance", focus_distance},
        {"apertureRadius", aperture_radius}, {"blades", blades}, {"rotationDegrees", rotation_degrees},
        {"roundness", roundness}, {"samples", samples}};
  }
};
inline std::optional<GaussianLensSettings> gaussian_lens_settings(const QVariantMap& value) {
  const auto json = QJsonObject::fromVariantMap(value);
  const bool extended = json.value("schema") == "framehouse-thin-lens-v2";
  if (json.size() != 7 || (!extended && json.value("schema") != "framehouse-thin-lens-v1")) return {};
  for (const auto* key : {"focusDistance", "apertureRadius", "blades", "rotationDegrees", "roundness", "samples"})
    if (!json.value(QLatin1String(key)).isDouble() || !std::isfinite(json.value(QLatin1String(key)).toDouble())) return {};
  const auto focus = json.value("focusDistance").toDouble(), radius = json.value("apertureRadius").toDouble();
  const auto blades = json.value("blades").toDouble(), samples = json.value("samples").toDouble();
  const auto rotation = json.value("rotationDegrees").toDouble(), roundness = json.value("roundness").toDouble();
  if (focus < 0.01 || focus > 1e8 || radius < 0 || radius > focus * 0.25 ||
      blades < 0 || blades > 16 || blades != std::floor(blades) ||
      samples < (extended ? 256 : 16) || samples > (extended ? 4096 : 128) || samples != std::floor(samples) ||
      rotation < 0 || rotation > 360 || roundness < 0 || roundness > 1) return {};
  const GaussianLensSettings result{static_cast<float>(focus), static_cast<float>(radius), static_cast<int>(blades),
      static_cast<float>(rotation), static_cast<float>(roundness), static_cast<int>(samples)};
  return result.valid() ? std::optional<GaussianLensSettings>(result) : std::nullopt;
}
}  // namespace photo_editor::desktop
