#pragma once
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QSize>
#include <QVariantMap>
#include <cmath>

namespace photo_editor::desktop {
// EXIF 35mm equivalence supplies an initial focal prior, not a calibration.
// Match COLMAP 3.13's diagonal convention for already oriented/resized pixels.
inline QVariantMap gaussian_camera_prior(const QVariantMap& metadata, QSize prepared_size) {
  const auto focal35 = metadata.value("focal35Mm").toDouble();
  if (prepared_size.isEmpty() || !std::isfinite(focal35) || focal35 <= 0 || focal35 > 10000)
    return {{"available", false}, {"contract", "exif-focal-prior-v1"},
        {"reason", "No valid 35mm-equivalent focal length; reconstruction will estimate intrinsics."}};
  const auto focal = focal35 / 43.27 * std::hypot(prepared_size.width(), prepared_size.height());
  QVariantMap identity = metadata;
  identity.remove("available"); identity.remove("backend");
  identity.insert("width", prepared_size.width()); identity.insert("height", prepared_size.height());
  const auto group = QString::fromLatin1(QCryptographicHash::hash(
      QJsonDocument::fromVariant(identity).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
  // A model name alone cannot identify a physical camera/lens. Keep per-frame
  // intrinsics unless the source identifies both body and lens, and supplies
  // the physical focal length (35mm-equivalent values are often rounded).
  const auto focal_mm = metadata.value("focalMm").toDouble();
  const bool share = !metadata.value("make").toString().isEmpty() && !metadata.value("model").toString().isEmpty() &&
      !metadata.value("bodySerial").toString().isEmpty() && !metadata.value("lens").toString().isEmpty() &&
      std::isfinite(focal_mm) && focal_mm > 0 && focal_mm <= 10000;
  return {{"available", true}, {"contract", "exif-focal-prior-v1"},
      {"cameraModel", "SIMPLE_RADIAL"}, {"focalPixels", focal}, {"groupKey", share ? group : QString()},
      {"parameters", QStringLiteral("%1,%2,%3,0").arg(focal, 0, 'g', 17)
          .arg(prepared_size.width() / 2.0, 0, 'g', 17).arg(prepared_size.height() / 2.0, 0, 'g', 17)},
      {"source", "EXIF-FocalLengthIn35mmFilm"}, {"calibrated", false}};
}
}
