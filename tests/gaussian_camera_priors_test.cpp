#include "gaussian_camera_priors.h"
#include <limits>
#include <iostream>

using photo_editor::desktop::gaussian_camera_prior;
int main() {
  QVariantMap metadata{{"make", "Camera"}, {"model", "Body"}, {"lens", "Prime"},
      {"bodySerial", "body-one"}, {"focalMm", 35.0}, {"focal35Mm", 50.0}};
  const auto prior = gaussian_camera_prior(metadata, QSize(800, 600));
  if (!prior.value("available").toBool() || prior.value("calibrated").toBool() ||
      prior.value("cameraModel") != "SIMPLE_RADIAL" ||
      std::abs(prior.value("focalPixels").toDouble() - 50.0 / 43.27 * 1000.0) > 1e-10 ||
      prior.value("groupKey").toString().isEmpty()) return EXIT_FAILURE;
  const auto portrait = gaussian_camera_prior(metadata, QSize(600, 800));
  const auto scaled = gaussian_camera_prior(metadata, QSize(400, 300));
  if (portrait.value("focalPixels") != prior.value("focalPixels") || portrait.value("groupKey") == prior.value("groupKey") ||
      std::abs(scaled.value("focalPixels").toDouble() * 2 - prior.value("focalPixels").toDouble()) > 1e-10) return EXIT_FAILURE;
  const auto parameters = prior.value("parameters").toString().split(',');
  if (parameters.size() != 4 || parameters[1].toDouble() != 400 || parameters[2].toDouble() != 300 || parameters[3] != "0") return EXIT_FAILURE;
  for (const auto& key : {"make", "model", "lens", "bodySerial", "focalMm"}) {
    auto changed = metadata;
    changed.insert(QString::fromLatin1(key), key == QStringLiteral("focalMm") ? QVariant(36.0) : QVariant(QStringLiteral("other")));
    if (gaussian_camera_prior(changed, QSize(800, 600)).value("groupKey") == prior.value("groupKey")) return EXIT_FAILURE;
  }
  for (const auto focal : {0.0, -10.0, 10001.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
    auto invalid = metadata; invalid.insert("focal35Mm", focal);
    if (gaussian_camera_prior(invalid, QSize(800, 600)).value("available").toBool()) return EXIT_FAILURE;
  }
  for (const auto size : {QSize(), QSize(0, 600), QSize(800, 0)}) {
    if (gaussian_camera_prior(metadata, size).value("available").toBool()) return EXIT_FAILURE;
  }
  auto raw_metadata = metadata;
  raw_metadata.insert("sourceFormat", "dng");
  auto jpeg_metadata = metadata;
  jpeg_metadata.insert("sourceFormat", "jpeg");
  if (gaussian_camera_prior(raw_metadata, QSize(800, 600)).value("groupKey") ==
      gaussian_camera_prior(jpeg_metadata, QSize(800, 600)).value("groupKey")) return EXIT_FAILURE;
  for (const auto& key : {"bodySerial", "lens", "focalMm"}) {
    auto unidentified = metadata; unidentified.remove(QString::fromLatin1(key));
    const auto result = gaussian_camera_prior(unidentified, QSize(800, 600));
    if (!result.value("available").toBool() || !result.value("groupKey").toString().isEmpty()) return EXIT_FAILURE;
  }
  metadata.remove("make");
  if (!gaussian_camera_prior(metadata, QSize(800, 600)).value("groupKey").toString().isEmpty()) return EXIT_FAILURE;
  std::cout << "EXIF focal priors: diagonal scaling, portrait, centered intrinsics, identity separation and unknown/invalid fallback pass.\n";
  return EXIT_SUCCESS;
}
