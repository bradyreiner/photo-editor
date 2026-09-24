#pragma once
#include "gaussian_lens_settings.h"
#include "gaussian_film_settings.h"
#include <QJsonObject>
#include <QVariantMap>
#include <cmath>
#include <optional>

namespace photo_editor::desktop {
// Creative processing of display-referred reconstructed photographs, not
// recovered sensor exposure or a measured film-stock response.
struct GaussianPhotoSettings {
  float focal_scale = 1;
  float exposure_ev = 0;
  int look = 0;  // Neutral, soft highlights, monochrome.
  GaussianLensSettings lens{};
  GaussianFilmSettings film{};
  bool operator==(const GaussianPhotoSettings&) const = default;
  bool valid() const {
    return std::isfinite(focal_scale) && focal_scale >= 0.25F && focal_scale <= 4 &&
        std::isfinite(exposure_ev) && exposure_ev >= -6 && exposure_ev <= 6 && look >= 0 && look <= 2 && lens.valid() && film.valid();
  }
  QVariantMap description() const {
    QVariantMap result{{"schema", "framehouse-gaussian-photo-settings-v1"}, {"focalScale", focal_scale},
        {"exposureEv", exposure_ev}, {"look", look == 1 ? "soft-highlight" : look == 2 ? "monochrome" : "neutral"}};
    if (lens != GaussianLensSettings{}) {
      result.insert("schema", "framehouse-gaussian-photo-settings-v2");
      result.insert("lens", lens.description());
    }
    if (film != GaussianFilmSettings{}) {
      result.insert("schema", "framehouse-gaussian-photo-settings-v3");
      result.insert("lens", lens.description());
      result.insert("film", film.description());
    }
    return result;
  }
};
inline std::optional<GaussianPhotoSettings> gaussian_photo_settings(const QVariantMap& value) {
  const auto json = QJsonObject::fromVariantMap(value);
  const bool film = json.value("schema") == "framehouse-gaussian-photo-settings-v3";
  const bool optical = film || json.value("schema") == "framehouse-gaussian-photo-settings-v2";
  if ((!optical && json.value("schema") != "framehouse-gaussian-photo-settings-v1") ||
      json.size() != (film ? 6 : optical ? 5 : 4) ||
      !json.value("focalScale").isDouble() || !json.value("exposureEv").isDouble()) return {};
  const auto focal = json.value("focalScale").toDouble();
  const auto exposure = json.value("exposureEv").toDouble();
  const auto look = json.value("look").toString();
  if (!std::isfinite(focal) || focal < 0.25 || focal > 4 ||
      !std::isfinite(exposure) || exposure < -6 || exposure > 6 ||
      (look != "neutral" && look != "soft-highlight" && look != "monochrome")) return {};
  GaussianPhotoSettings result{static_cast<float>(focal), static_cast<float>(exposure),
      look == "soft-highlight" ? 1 : look == "monochrome" ? 2 : 0};
  if (optical) {
    if (!json.value("lens").isObject()) return {};
    const auto lens = gaussian_lens_settings(json.value("lens").toObject().toVariantMap());
    if (!lens) return {};
    result.lens = *lens;
  }
  if (film) {
    if (!json.value("film").isObject()) return {};
    const auto parsed = gaussian_film_settings(json.value("film").toObject().toVariantMap());
    if (!parsed) return {};
    result.film = *parsed;
  }
  return result;
}
}  // namespace photo_editor::desktop
