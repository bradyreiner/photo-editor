#pragma once

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace photo_editor::desktop {
// Native launcher installs and direct app launches must find the same model.
// Explicit catalog isolation and model overrides remain authoritative. Selection
// of an existing folder never substitutes for setup's full SHA-256 verification.
inline QString search_model_location(const QString& app_data, const QString& shared_data,
    const QString& explicit_model, bool isolated_data) {
  if (!explicit_model.trimmed().isEmpty()) return explicit_model;
  const auto local = QDir(app_data).filePath("search-models/siglip2-base-patch16-224");
  const auto shared = QDir(shared_data).filePath("PhotoEditor/search-models/siglip2-base-patch16-224");
  return !isolated_data && !QFileInfo::exists(local) && QFileInfo(shared).isDir() ? shared : local;
}
}  // namespace photo_editor::desktop
