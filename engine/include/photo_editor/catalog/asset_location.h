#pragma once

#include <string>

namespace photo_editor::catalog {

enum class ImportMode {
  kReferenced,
  kManaged,
};

struct AssetLocation final {
  std::string drive_id;
  std::string root_path;
  std::string relative_path;
  bool online = false;
};

struct ManagedPathTemplate final {
  std::string template_pattern = "YYYY/MM/DD/Camera Model";
};

}  // namespace photo_editor::catalog
