#pragma once
#include <QJsonObject>
#include <QVariantMap>
#include <cmath>
#include <optional>

namespace photo_editor::desktop {
// Experimental ideal pupils down to f/0.1 can approach five times the
// object distance when focused close to the focal length. This is a bounded
// geometric model, not a claim of a realizable compound lens at that f-number.
inline constexpr float gaussian_max_aperture_focus_ratio = 5;
struct GaussianLensSettings {
  float focus_distance = 1;  // Scene units, not invented metres.
  float aperture_radius = 0; // Zero is exact pinhole mode.
  int blades = 0;            // Zero is circular; otherwise regular polygon.
  float rotation_degrees = 0;
  float roundness = 0;
  int samples = 32;
  float aperture_edge = 0;       // Radial pupil response proportional to r^edge.
  float vignetting_distance = 0; // Scene units from aperture to barrel opening.
  float vignetting_scale = 1;    // Barrel opening radius / aperture radius.
  float spherical_distortion = 0;
  float astigmatism = 0;
  float field_curvature = 0;
  bool auto_focus = false;
  float bokeh_aspect = 1; // Vertical / horizontal pupil aspect, equal-area ellipse.
  bool ray_traced = false; // Per-pixel primary rays through the pupil; opt-in due to cost.
  float pupil_softness = 0; // Apodized pupil transmission (1-r^2)^softness.
  bool ray_mapping() const { return spherical_distortion != 0 || astigmatism != 0 || field_curvature != 0; }
  bool operator==(const GaussianLensSettings&) const = default;
  bool valid() const {
    return std::isfinite(focus_distance) && focus_distance >= 0.01F && focus_distance <= 1e8F &&
        std::isfinite(aperture_radius) && aperture_radius >= 0 && aperture_radius <= focus_distance * gaussian_max_aperture_focus_ratio &&
        (blades == 0 || (blades >= 3 && blades <= 16)) &&
        std::isfinite(rotation_degrees) && rotation_degrees >= 0 && rotation_degrees <= 360 &&
        std::isfinite(roundness) && roundness >= 0 && roundness <= 1 &&
        std::isfinite(aperture_edge) && aperture_edge >= 0 && aperture_edge <= 8 &&
        std::isfinite(vignetting_distance) && vignetting_distance >= 0 && vignetting_distance <= focus_distance * 0.25F &&
        std::isfinite(vignetting_scale) && vignetting_scale >= 0.5F && vignetting_scale <= 4 &&
        std::isfinite(spherical_distortion) && spherical_distortion >= -0.5F && spherical_distortion <= 0.5F &&
        std::isfinite(astigmatism) && astigmatism >= -2 && astigmatism <= 2 &&
        std::isfinite(field_curvature) && field_curvature >= -2 && field_curvature <= 2 &&
        std::isfinite(bokeh_aspect) && bokeh_aspect >= 0.5F && bokeh_aspect <= 3 &&
        std::isfinite(pupil_softness) && pupil_softness >= 0 && pupil_softness <= 4 &&
        (samples == 16 || samples == 32 || samples == 64 || samples == 128 ||
         samples == 256 || samples == 512 || samples == 1024 || samples == 2048 || samples == 4096);
  }
  QVariantMap description() const {
    QVariantMap result{{"schema", samples > 128 ? "framehouse-thin-lens-v2" : "framehouse-thin-lens-v1"}, {"focusDistance", focus_distance},
        {"apertureRadius", aperture_radius}, {"blades", blades}, {"rotationDegrees", rotation_degrees},
        {"roundness", roundness}, {"samples", samples}};
    if (bokeh_aspect != 1 || auto_focus || ray_mapping() || aperture_edge != 0 || vignetting_distance != 0 || vignetting_scale != 1) {
      result.insert("schema", "framehouse-thin-lens-v3");
      result.insert("apertureEdge", aperture_edge);
      result.insert("vignettingDistance", vignetting_distance);
      result.insert("vignettingScale", vignetting_scale);
    }
    if (bokeh_aspect != 1 || auto_focus || ray_mapping()) {
      result.insert("schema","framehouse-thin-lens-v4");
      result.insert("sphericalDistortion",spherical_distortion);
      result.insert("astigmatism",astigmatism);
      result.insert("fieldCurvature",field_curvature);
    }
    if (auto_focus) { result.insert("schema","framehouse-thin-lens-v5"); result.insert("focusMode","auto-centre"); }
    if (bokeh_aspect != 1) {
      result.insert("schema","framehouse-thin-lens-v6"); result.insert("bokehAspect",bokeh_aspect);
      result.insert("focusMode",auto_focus ? "auto-centre" : "manual");
    }
    if (ray_traced || pupil_softness != 0) {
      result.insert("schema", "framehouse-thin-lens-v7");
      result.insert("apertureEdge", aperture_edge);
      result.insert("vignettingDistance", vignetting_distance);
      result.insert("vignettingScale", vignetting_scale);
      result.insert("sphericalDistortion", spherical_distortion);
      result.insert("astigmatism", astigmatism);
      result.insert("fieldCurvature", field_curvature);
      result.insert("focusMode", auto_focus ? "auto-centre" : "manual");
      result.insert("bokehAspect", bokeh_aspect);
      result.insert("rayTraced", ray_traced);
      result.insert("pupilSoftness", pupil_softness);
    }
    return result;
  }
};
inline std::optional<GaussianLensSettings> gaussian_lens_settings(const QVariantMap& value) {
  const auto json = QJsonObject::fromVariantMap(value);
  const bool ray_traced = json.value("schema") == "framehouse-thin-lens-v7";
  const bool shaped = ray_traced || json.value("schema") == "framehouse-thin-lens-v6";
  const bool automatic = shaped || json.value("schema") == "framehouse-thin-lens-v5";
  const bool mapped = automatic || json.value("schema") == "framehouse-thin-lens-v4";
  const bool pupil = mapped || json.value("schema") == "framehouse-thin-lens-v3";
  const bool extended = json.value("schema") == "framehouse-thin-lens-v2";
  if (json.size() != (ray_traced ? 17 : shaped ? 15 : automatic ? 14 : mapped ? 13 : pupil ? 10 : 7) ||
      (!ray_traced && !pupil && !extended && json.value("schema") != "framehouse-thin-lens-v1")) return {};
  for (const auto* key : {"focusDistance", "apertureRadius", "blades", "rotationDegrees", "roundness", "samples"})
    if (!json.value(QLatin1String(key)).isDouble() || !std::isfinite(json.value(QLatin1String(key)).toDouble())) return {};
  const auto focus = json.value("focusDistance").toDouble(), radius = json.value("apertureRadius").toDouble();
  const auto blades = json.value("blades").toDouble(), samples = json.value("samples").toDouble();
  const auto rotation = json.value("rotationDegrees").toDouble(), roundness = json.value("roundness").toDouble();
  if (focus < 0.01 || focus > 1e8 || radius < 0 || radius > focus * gaussian_max_aperture_focus_ratio ||
      blades < 0 || blades > 16 || blades != std::floor(blades) ||
      samples < (extended ? 256 : 16) || samples > (pupil || extended ? 4096 : 128) || samples != std::floor(samples) ||
      rotation < 0 || rotation > 360 || roundness < 0 || roundness > 1) return {};
  GaussianLensSettings result{static_cast<float>(focus), static_cast<float>(radius), static_cast<int>(blades),
      static_cast<float>(rotation), static_cast<float>(roundness), static_cast<int>(samples)};
  if (pupil) {
    for (const auto* key : {"apertureEdge", "vignettingDistance", "vignettingScale"})
      if (!json.value(QLatin1String(key)).isDouble() || !std::isfinite(json.value(QLatin1String(key)).toDouble())) return {};
    const auto edge = json.value("apertureEdge").toDouble(), distance = json.value("vignettingDistance").toDouble(), scale = json.value("vignettingScale").toDouble();
    if (edge < 0 || edge > 8 || distance < 0 || distance > focus*0.25 || scale < 0.5 || scale > 4) return {};
    result.aperture_edge = static_cast<float>(edge);
    result.vignetting_distance = static_cast<float>(distance);
    result.vignetting_scale = static_cast<float>(scale);
  }
  if (mapped) {
    for (const auto* key : {"sphericalDistortion","astigmatism","fieldCurvature"})
      if (!json.value(QLatin1String(key)).isDouble() || !std::isfinite(json.value(QLatin1String(key)).toDouble())) return {};
    const auto distortion = json.value("sphericalDistortion").toDouble(), astigmatism = json.value("astigmatism").toDouble(), curvature = json.value("fieldCurvature").toDouble();
    if (distortion < -0.5 || distortion > 0.5 || astigmatism < -2 || astigmatism > 2 || curvature < -2 || curvature > 2) return {};
    result.spherical_distortion = static_cast<float>(distortion);
    result.astigmatism = static_cast<float>(astigmatism);
    result.field_curvature = static_cast<float>(curvature);
  }
  if (automatic) {
    const auto mode = json.value("focusMode").toString();
    if (mode != "manual" && mode != "auto-centre") return {};
    result.auto_focus = mode == "auto-centre";
  }
  if (shaped) {
    if (!json.value("bokehAspect").isDouble()) return {};
    result.bokeh_aspect = static_cast<float>(json.value("bokehAspect").toDouble());
  }
  if (ray_traced) {
    if (!json.value("rayTraced").isBool() || !json.value("pupilSoftness").isDouble() ||
        !std::isfinite(json.value("pupilSoftness").toDouble())) return {};
    const double softness = json.value("pupilSoftness").toDouble();
    if (softness < 0 || softness > 4) return {};
    result.ray_traced = json.value("rayTraced").toBool();
    result.pupil_softness = static_cast<float>(softness);
  }
  return result.valid() ? std::optional<GaussianLensSettings>(result) : std::nullopt;
}
}  // namespace photo_editor::desktop
