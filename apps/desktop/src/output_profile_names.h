#pragma once

#include "photo_editor/pipeline/color_space.h"

#include <QString>

namespace photo_editor::desktop {

[[nodiscard]] inline QString linear_output_profile_name(
    photo_editor::pipeline::ColorPrimaries primaries,
    const QString& fallback) {
  using photo_editor::pipeline::ColorPrimaries;
  switch (primaries) {
    case ColorPrimaries::Srgb:
    case ColorPrimaries::Rec709:
      return QStringLiteral("Linear sRGB");
    case ColorPrimaries::DisplayP3:
      return QStringLiteral("Linear Display P3");
    case ColorPrimaries::Rec2020:
      return QStringLiteral("Linear Rec.2020");
    case ColorPrimaries::ProPhotoRgb:
      return QStringLiteral("Linear ProPhoto RGB");
    case ColorPrimaries::Unknown:
    case ColorPrimaries::AdobeRgb:
    case ColorPrimaries::AcesAp0:
    case ColorPrimaries::AcesAp1:
      return fallback;
  }
  return fallback;
}

[[nodiscard]] inline QString encoded_output_profile_name(
    photo_editor::pipeline::ColorPrimaries primaries,
    const QString& fallback) {
  using photo_editor::pipeline::ColorPrimaries;
  switch (primaries) {
    case ColorPrimaries::Srgb:
      return QStringLiteral("sRGB");
    case ColorPrimaries::Rec709:
      return QStringLiteral("Rec.709");
    case ColorPrimaries::DisplayP3:
      return QStringLiteral("Display P3");
    case ColorPrimaries::Rec2020:
      return QStringLiteral("Rec.2020");
    case ColorPrimaries::ProPhotoRgb:
      return QStringLiteral("ProPhoto RGB");
    case ColorPrimaries::Unknown:
    case ColorPrimaries::AdobeRgb:
    case ColorPrimaries::AcesAp0:
    case ColorPrimaries::AcesAp1:
      return fallback;
  }
  return fallback;
}

}  // namespace photo_editor::desktop
