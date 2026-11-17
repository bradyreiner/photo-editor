#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantMap>
#include <array>
#include <algorithm>
#include <cmath>
#include <optional>

namespace photo_editor::desktop {
// Legacy creative effects or an explicitly dimensioned film-plane approximation.
// Neither is a measured commercial emulsion or recovered sensor radiometry.
struct GaussianFilmSettings {
  float grain_amount = 0;
  float grain_size = 1.5F; // Pixels at a 2048-pixel long edge.
  float halation_amount = 0;
  float halation_radius = 4; // Gaussian sigma at a 2048-pixel long edge.
  float halation_threshold = 0.8F; // Linearized luminance after exposure.
  quint32 seed = 1;
  bool film_plane = false;
  float grain_microns = 12; // Gaussian correlation length of density fluctuations.
  float halation_microns = 80; // Gaussian diffusion sigma in the recording plane.
  float format_width_mm = 36, format_height_mm = 24; // Used by uncalibrated cameras.
  std::array<float,3> return_rgb{1,0.15F,0.025F}; // Fraction of available returning RGB light.
  bool highlight_halation = false; // Opt-in highlight mask; v2 full-light transport stays unchanged.
  bool operator==(const GaussianFilmSettings&) const = default;
  bool active() const { return grain_amount > 0 || halation_amount > 0; }
  bool valid() const {
    return (!highlight_halation || film_plane) && std::isfinite(grain_amount) && grain_amount >= 0 && grain_amount <= 1 &&
        std::isfinite(grain_size) && grain_size >= 0.5F && grain_size <= 6 &&
        std::isfinite(halation_amount) && halation_amount >= 0 && halation_amount <= 1 &&
        std::isfinite(halation_radius) && halation_radius >= 0.5F && halation_radius <= 16 &&
        std::isfinite(halation_threshold) && halation_threshold >= 0.1F && halation_threshold <= 4 &&
        std::isfinite(grain_microns) && grain_microns >= 1 && grain_microns <= 100 &&
        std::isfinite(halation_microns) && halation_microns >= 1 && halation_microns <= 300 &&
        std::isfinite(format_width_mm) && format_width_mm >= 1 && format_width_mm <= 300 &&
        std::isfinite(format_height_mm) && format_height_mm >= 1 && format_height_mm <= 300 &&
        std::all_of(return_rgb.begin(),return_rgb.end(),[](float c) { return std::isfinite(c) && c>=0 && c<=1; });
  }
  QVariantMap description() const {
    QVariantMap result{{"schema", "framehouse-creative-film-v1"}, {"grainAmount", grain_amount},
        {"grainSize", grain_size}, {"halationAmount", halation_amount},
        {"halationRadius", halation_radius}, {"halationThreshold", halation_threshold}, {"seed", seed}};
    if (film_plane) {
      result["schema"]="framehouse-film-plane-v2";
      result["grainMicrons"]=grain_microns; result["halationMicrons"]=halation_microns;
      result["formatWidthMm"]=format_width_mm; result["formatHeightMm"]=format_height_mm;
      result["returnRgb"]=QVariantList{return_rgb[0],return_rgb[1],return_rgb[2]};
      if (highlight_halation) { result["schema"]="framehouse-film-plane-v3"; result["highlightHalation"]=true; }
    }
    return result;
  }
};
inline std::optional<GaussianFilmSettings> gaussian_film_settings(const QVariantMap& value) {
  const auto json = QJsonObject::fromVariantMap(value);
  const bool highlight=json.value("schema")=="framehouse-film-plane-v3";
  const bool plane=highlight || json.value("schema")=="framehouse-film-plane-v2";
  if (json.size() != (highlight ? 13 : plane ? 12 : 7) || (!plane && json.value("schema") != "framehouse-creative-film-v1")) return {};
  if (highlight && (!json.value("highlightHalation").isBool() || !json.value("highlightHalation").toBool())) return {};
  const auto number = [&](const char* key, double low, double high) {
    const auto v = json.value(QLatin1String(key));
    return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() >= low && v.toDouble() <= high;
  };
  if (!number("grainAmount", 0, 1) || !number("grainSize", 0.5, 6) ||
      !number("halationAmount", 0, 1) || !number("halationRadius", 0.5, 16) ||
      !number("halationThreshold", 0.1, 4) || !number("seed", 0, 4294967295.0) ||
      json.value("seed").toDouble() != std::floor(json.value("seed").toDouble())) return {};
  GaussianFilmSettings result{static_cast<float>(json.value("grainAmount").toDouble()),
      static_cast<float>(json.value("grainSize").toDouble()),
      static_cast<float>(json.value("halationAmount").toDouble()),
      static_cast<float>(json.value("halationRadius").toDouble()),
      static_cast<float>(json.value("halationThreshold").toDouble()),
      static_cast<quint32>(json.value("seed").toDouble())};
  if (plane) {
    if (!number("grainMicrons",1,100) || !number("halationMicrons",1,300) ||
        !number("formatWidthMm",1,300) || !number("formatHeightMm",1,300) ||
        !json.value("returnRgb").isArray() || json.value("returnRgb").toArray().size()!=3) return {};
    const auto colors=json.value("returnRgb").toArray();
    for (qsizetype i=0;i<3;++i) {
      if (!colors[i].isDouble() || !std::isfinite(colors[i].toDouble()) || colors[i].toDouble()<0 || colors[i].toDouble()>1) return {};
      result.return_rgb[static_cast<size_t>(i)]=static_cast<float>(colors[i].toDouble());
    }
    result.film_plane=true;
    result.highlight_halation=highlight;
    result.grain_microns=static_cast<float>(json.value("grainMicrons").toDouble());
    result.halation_microns=static_cast<float>(json.value("halationMicrons").toDouble());
    result.format_width_mm=static_cast<float>(json.value("formatWidthMm").toDouble());
    result.format_height_mm=static_cast<float>(json.value("formatHeightMm").toDouble());
  }
  return result;
}
}  // namespace photo_editor::desktop
