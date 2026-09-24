#pragma once

#include <QString>

namespace photo_editor::desktop {

// Derived, query-independent descriptions only: no pixels, paths, or catalog IDs.
// Each operation owns its SQLite connection and runs on a background worker.
struct VisualSearchIndex final {
  static QString lookup(const QString& path, const QString& key);
  static bool save(const QString& path, const QString& key, const QString& description);
  static bool clear(const QString& path);
};

}  // namespace photo_editor::desktop
