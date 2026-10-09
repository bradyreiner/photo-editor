#pragma once

#include <QString>
#include <QVariantMap>

namespace photo_editor::desktop {

// Derived descriptions and hashed preparation identities: no pixels, paths, or
// unhashed catalog IDs. Preparation aliases share the description retention cap.
// Each operation owns its SQLite connection and runs on a background worker.
struct VisualSearchIndex final {
  static QString lookup(const QString& path, const QString& key);
  static bool save(const QString& path, const QString& key, const QString& description,
                   const QString& preparation_key = {});
  static QVariantMap lookupPrepared(const QString& path, const QString& preparation_key);
  static bool savePreparation(const QString& path, const QString& preparation_key, const QString& key);
  static bool clear(const QString& path);
};

}  // namespace photo_editor::desktop
