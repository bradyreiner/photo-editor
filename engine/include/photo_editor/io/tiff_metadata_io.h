#pragma once

#include <string>
#include <string_view>

namespace photo_editor::io {

[[nodiscard]] bool tiff_metadata_backend_available();
[[nodiscard]] bool write_tiff_export_metadata(
    std::string_view file_path,
    std::string_view software,
    std::string_view description,
    std::string* failure_reason = nullptr);
[[nodiscard]] bool read_tiff_export_metadata(
    std::string_view file_path,
    std::string* software,
    std::string* description,
    std::string* failure_reason = nullptr);

}  // namespace photo_editor::io
