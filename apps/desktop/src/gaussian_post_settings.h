#pragma once
#include <QJsonObject>
#include <QVariantMap>
#include <cmath>
#include <optional>

namespace photo_editor::desktop {
// Creative image-space camera effects. Sizes are pixels at a 2048 px long edge;
// spectral glare is an RGB approximation, not wavelength-traced diffraction.
struct GaussianPostSettings {
  float bloom_amount = 0, bloom_radius = 8;
  float glare_amount = 0, glare_radius = 32;
  int glare_rays = 4; // Opposed pairs of streaks.
  float glare_rotation = 0, spectral_intensity = 0, threshold = 0.8F;
  float chromatic_aberration = 0, saturation = 1;
  bool operator==(const GaussianPostSettings&) const = default;
  bool spatial() const { return bloom_amount > 0 || glare_amount > 0 || chromatic_aberration > 0; }
  bool active() const { return spatial() || saturation != 1; }
  bool valid() const {
    const auto range = [](float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; };
    return range(bloom_amount,0,4) && range(bloom_radius,0.5F,32) &&
        range(glare_amount,0,4) && range(glare_radius,1,64) && glare_rays >= 1 && glare_rays <= 8 &&
        range(glare_rotation,0,180) && range(spectral_intensity,0,1) && range(threshold,0,16) &&
        range(chromatic_aberration,0,8) && range(saturation,0,2);
  }
  QVariantMap description() const {
    return {{"schema","framehouse-camera-post-v1"},{"bloomAmount",bloom_amount},{"bloomRadius",bloom_radius},
        {"glareAmount",glare_amount},{"glareRadius",glare_radius},{"glareRays",glare_rays},
        {"glareRotation",glare_rotation},{"spectralIntensity",spectral_intensity},{"threshold",threshold},
        {"chromaticAberration",chromatic_aberration},{"saturation",saturation}};
  }
};
inline std::optional<GaussianPostSettings> gaussian_post_settings(const QVariantMap& value) {
  const auto json = QJsonObject::fromVariantMap(value);
  if (json.size() != 11 || json.value("schema") != "framehouse-camera-post-v1") return {};
  const auto number = [&](const char* key, double low, double high) {
    const auto v = json.value(QLatin1String(key));
    return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() >= low && v.toDouble() <= high;
  };
  if (!number("bloomAmount",0,4) || !number("bloomRadius",0.5,32) ||
      !number("glareAmount",0,4) || !number("glareRadius",1,64) || !number("glareRays",1,8) ||
      json.value("glareRays").toDouble() != std::floor(json.value("glareRays").toDouble()) ||
      !number("glareRotation",0,180) || !number("spectralIntensity",0,1) || !number("threshold",0,16) ||
      !number("chromaticAberration",0,8) || !number("saturation",0,2)) return {};
  return GaussianPostSettings{static_cast<float>(json.value("bloomAmount").toDouble()),
      static_cast<float>(json.value("bloomRadius").toDouble()), static_cast<float>(json.value("glareAmount").toDouble()),
      static_cast<float>(json.value("glareRadius").toDouble()), json.value("glareRays").toInt(),
      static_cast<float>(json.value("glareRotation").toDouble()), static_cast<float>(json.value("spectralIntensity").toDouble()),
      static_cast<float>(json.value("threshold").toDouble()), static_cast<float>(json.value("chromaticAberration").toDouble()),
      static_cast<float>(json.value("saturation").toDouble())};
}
}  // namespace photo_editor::desktop
