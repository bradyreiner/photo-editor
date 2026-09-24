#include "sensor_dust_map_store.h"

#include <cstdlib>
#include <iostream>

#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QTemporaryDir>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main() {
  using namespace photo_editor::desktop;

  const auto normalized_key =
      sensor_dust_map_key(QStringLiteral("  Camera   X "), QStringLiteral(" Lens Y "));
  if (normalized_key.size() != 64 ||
      normalized_key != sensor_dust_map_key(
                            QStringLiteral("Camera X"),
                            QStringLiteral("Lens Y")) ||
      normalized_key == sensor_dust_map_key(
                            QStringLiteral("Camera X"),
                            QStringLiteral("Lens Z")) ||
      sensor_dust_map_key({}, {}) != sensor_dust_map_key(
                                           QStringLiteral("unknown-camera"),
                                           QStringLiteral("unknown-lens"))) {
    return fail("dust-map identity must be canonical, stable, and camera/lens specific");
  }

  QTemporaryDir storage;
  if (!storage.isValid()) {
    return fail("temporary dust-map storage must be available");
  }
  const QByteArray alpha("\x00\x40\x80\xff", 4);
  if (write_sensor_dust_map(
          {}, QStringLiteral("Camera X"), QStringLiteral("Lens Y"),
          alpha, 2, 2, 81.5, 0.64) ||
      write_sensor_dust_map(
          storage.path(), QStringLiteral("Camera X"), QStringLiteral("Lens Y"),
          {}, 2, 2, 81.5, 0.64)) {
    return fail("dust-map persistence must fail closed on invalid roots and mattes");
  }
  if (!write_sensor_dust_map(
          storage.path(),
          QStringLiteral("Camera X"),
          QStringLiteral("Lens Y"),
          alpha,
          2,
          2,
          81.5,
          0.64)) {
    return fail("a valid dust map must be written atomically");
  }

  const auto path = sensor_dust_map_path(
      storage.path(), QStringLiteral("Camera X"), QStringLiteral("Lens Y"));
  const auto payload = read_sensor_dust_map(
      storage.path(), QStringLiteral("Camera X"), QStringLiteral("Lens Y"));
  if (!QFileInfo::exists(path) || !payload.valid || payload.width != 2 ||
      payload.height != 2 || payload.segmentation_data != alpha ||
      payload.remove_strength != 81.5 || payload.feather != 0.64) {
    return fail("dust-map round trip must preserve exact raster and edit parameters");
  }
  if (read_sensor_dust_map(
          storage.path(), QStringLiteral("Other Camera"), QStringLiteral("Lens Y"))
          .valid) {
    return fail("camera/lens identities must isolate stored dust maps");
  }

  QFile corrupt(path);
  if (!corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      corrupt.write("not-json") != 8) {
    return fail("the corruption fixture must replace the stored payload");
  }
  corrupt.close();
  if (read_sensor_dust_map(
          storage.path(), QStringLiteral("Camera X"), QStringLiteral("Lens Y"))
          .valid) {
    return fail("corrupt dust-map storage must fail closed");
  }
  return EXIT_SUCCESS;
}
