#pragma once

#include <string>

#include "photo_editor/catalog/asset_location.h"

namespace photo_editor::catalog {

struct DriveRecord final {
  std::string drive_id;
  std::string display_name;
  std::string mount_path;
  bool online = false;
};

struct CaptureMetadata final {
  int capture_year = 0;
  int capture_month = 0;
  int capture_day = 0;
  std::string camera_make;
  std::string camera_model;
  std::string lens_model;
  int iso = 0;
  std::string shutter_speed;
  double aperture = 0.0;
};

struct CatalogAssetRecord final {
  std::string asset_id;
  CaptureMetadata metadata;
  ImportMode import_mode = ImportMode::kReferenced;
  AssetLocation location;
};

}  // namespace photo_editor::catalog
