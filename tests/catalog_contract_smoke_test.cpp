#include <cstdlib>
#include <iostream>

#include "photo_editor/catalog/catalog_models.h"

int main() {
  photo_editor::catalog::CatalogAssetRecord asset{
      .asset_id = "asset-1",
      .metadata =
          {
              .capture_year = 2026,
              .capture_month = 3,
              .capture_day = 24,
              .camera_make = "Canon",
              .camera_model = "EOS R5",
              .lens_model = "RF 28-70mm",
              .iso = 400,
              .shutter_speed = "1/250",
              .aperture = 2.8,
          },
      .import_mode = photo_editor::catalog::ImportMode::kReferenced,
      .location =
          {
              .drive_id = "drive-a",
              .root_path = "/Volumes/Wedding",
              .relative_path = "2026/03/24/Canon/IMG_0001.CR3",
              .online = true,
          },
  };

  if (asset.metadata.capture_year != 2026) {
    std::cerr << "unexpected catalog metadata\n";
    return EXIT_FAILURE;
  }

  if (asset.location.drive_id != "drive-a") {
    std::cerr << "unexpected drive contract\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
