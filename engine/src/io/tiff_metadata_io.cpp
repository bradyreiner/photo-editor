#include "photo_editor/io/tiff_metadata_io.h"

#if defined(PHOTO_EDITOR_HAS_LIBTIFF) && PHOTO_EDITOR_HAS_LIBTIFF
#include <tiffio.h>
#endif

namespace photo_editor::io {

bool tiff_metadata_backend_available() {
#if defined(PHOTO_EDITOR_HAS_LIBTIFF) && PHOTO_EDITOR_HAS_LIBTIFF
  return true;
#else
  return false;
#endif
}

bool write_tiff_export_metadata(
    std::string_view file_path,
    std::string_view software,
    std::string_view description,
    std::string* failure_reason) {
#if defined(PHOTO_EDITOR_HAS_LIBTIFF) && PHOTO_EDITOR_HAS_LIBTIFF
  auto* file = TIFFOpen(
      std::string(file_path).c_str(),
      "r+");
  if (file == nullptr) {
    if (failure_reason != nullptr) {
      *failure_reason =
          "tiff_metadata_open_failed";
    }
    return false;
  }
  const auto software_value =
      std::string(software);
  const auto description_value =
      std::string(description);
  const auto fields_set =
      TIFFSetField(
          file,
          TIFFTAG_SOFTWARE,
          software_value.c_str()) == 1 &&
      TIFFSetField(
          file,
          TIFFTAG_IMAGEDESCRIPTION,
          description_value.c_str()) == 1;
  const auto directory_written =
      fields_set &&
      TIFFRewriteDirectory(file) == 1;
  TIFFClose(file);
  if (!directory_written &&
      failure_reason != nullptr) {
    *failure_reason =
        "tiff_metadata_write_failed";
  }
  return directory_written;
#else
  static_cast<void>(file_path);
  static_cast<void>(software);
  static_cast<void>(description);
  if (failure_reason != nullptr) {
    *failure_reason =
        "tiff_metadata_backend_unavailable";
  }
  return false;
#endif
}

bool read_tiff_export_metadata(
    std::string_view file_path,
    std::string* software,
    std::string* description,
    std::string* failure_reason) {
#if defined(PHOTO_EDITOR_HAS_LIBTIFF) && PHOTO_EDITOR_HAS_LIBTIFF
  auto* file = TIFFOpen(
      std::string(file_path).c_str(),
      "r");
  if (file == nullptr) {
    if (failure_reason != nullptr) {
      *failure_reason =
          "tiff_metadata_open_failed";
    }
    return false;
  }
  char* software_value = nullptr;
  char* description_value = nullptr;
  const auto has_software =
      TIFFGetField(
          file,
          TIFFTAG_SOFTWARE,
          &software_value) == 1 &&
      software_value != nullptr;
  const auto has_description =
      TIFFGetField(
          file,
          TIFFTAG_IMAGEDESCRIPTION,
          &description_value) == 1 &&
      description_value != nullptr;
  if (software != nullptr) {
    *software =
        has_software
            ? software_value
            : std::string{};
  }
  if (description != nullptr) {
    *description =
        has_description
            ? description_value
            : std::string{};
  }
  TIFFClose(file);
  if ((!has_software ||
       !has_description) &&
      failure_reason != nullptr) {
    *failure_reason =
        "tiff_metadata_missing";
  }
  return has_software &&
         has_description;
#else
  static_cast<void>(file_path);
  if (software != nullptr) {
    software->clear();
  }
  if (description != nullptr) {
    description->clear();
  }
  if (failure_reason != nullptr) {
    *failure_reason =
        "tiff_metadata_backend_unavailable";
  }
  return false;
#endif
}

}  // namespace photo_editor::io
