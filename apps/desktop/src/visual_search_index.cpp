#include "visual_search_index.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <sqlite3.h>

#include <memory>

namespace photo_editor::desktop {
namespace {
using Database = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

bool valid_key(const QString& key) {
  static const QRegularExpression pattern(QStringLiteral("^[a-f0-9]{64}$"));
  return pattern.match(key).hasMatch();
}

Database open(const QString& path, bool write) {
  if (path.isEmpty() || !QFileInfo(path).isAbsolute() || QFileInfo(path).isSymLink()) return {nullptr, sqlite3_close};
  if (write) {
    const auto directory = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(directory)) return {nullptr, sqlite3_close};
  }
  sqlite3* raw = nullptr;
  const auto flags = write ? SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE : SQLITE_OPEN_READONLY;
  if (sqlite3_open_v2(QFile::encodeName(path).constData(), &raw, flags, nullptr) != SQLITE_OK) {
    if (raw) sqlite3_close(raw);
    return {nullptr, sqlite3_close};
  }
  Database database(raw, sqlite3_close);
  sqlite3_busy_timeout(raw, 1000);
  if (write) {
    if (!QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner) ||
        sqlite3_exec(raw, "CREATE TABLE IF NOT EXISTS descriptions ("
                         "key TEXT PRIMARY KEY, description TEXT NOT NULL, "
                         "created_at INTEGER NOT NULL DEFAULT (unixepoch()))", nullptr, nullptr, nullptr) != SQLITE_OK) {
      return {nullptr, sqlite3_close};
    }
  }
  return database;
}
}  // namespace

QString VisualSearchIndex::lookup(const QString& path, const QString& key) {
  if (!valid_key(key)) return {};
  auto database = open(path, false);
  if (!database) return {};
  sqlite3_stmt* raw = nullptr;
  if (sqlite3_prepare_v2(database.get(), "SELECT description FROM descriptions WHERE key=?1", -1, &raw, nullptr) != SQLITE_OK) return {};
  Statement statement(raw, sqlite3_finalize);
  const auto encoded_key = key.toUtf8();
  sqlite3_bind_text(raw, 1, encoded_key.constData(), static_cast<int>(encoded_key.size()), SQLITE_TRANSIENT);
  if (sqlite3_step(raw) != SQLITE_ROW || sqlite3_column_bytes(raw, 0) > 4800) return {};
  const auto value = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(raw, 0)), sqlite3_column_bytes(raw, 0));
  return value.trimmed().isEmpty() || value.size() > 1200 ? QString() : value;
}

bool VisualSearchIndex::save(const QString& path, const QString& key, const QString& description) {
  if (!valid_key(key) || description.trimmed().isEmpty() || description.size() > 1200) return false;
  auto database = open(path, true);
  if (!database) return false;
  sqlite3_stmt* raw = nullptr;
  // A bounded derived index. Eviction only removes old analysis, never originals.
  if (sqlite3_exec(database.get(), "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) return false;
  if (sqlite3_prepare_v2(database.get(), "INSERT OR REPLACE INTO descriptions(key, description) VALUES(?1, ?2)", -1, &raw, nullptr) != SQLITE_OK) return false;
  Statement statement(raw, sqlite3_finalize);
  const auto encoded_key = key.toUtf8();
  const auto encoded_description = description.toUtf8();
  sqlite3_bind_text(raw, 1, encoded_key.constData(), static_cast<int>(encoded_key.size()), SQLITE_TRANSIENT);
  sqlite3_bind_text(raw, 2, encoded_description.constData(), static_cast<int>(encoded_description.size()), SQLITE_TRANSIENT);
  if (sqlite3_step(raw) != SQLITE_DONE) return false;
  return sqlite3_exec(database.get(), "DELETE FROM descriptions WHERE key IN ("
                       "SELECT key FROM descriptions ORDER BY created_at DESC, rowid DESC LIMIT -1 OFFSET 10000); COMMIT",
                       nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool VisualSearchIndex::clear(const QString& path) {
  if (path.isEmpty() || !QFileInfo(path).isAbsolute()) return false;
  if (!QFileInfo::exists(path)) return true;
  auto database = open(path, true);
  if (!database) return false;
  return sqlite3_exec(database.get(), "PRAGMA secure_delete=ON; DELETE FROM descriptions; VACUUM",
                      nullptr, nullptr, nullptr) == SQLITE_OK;
}
}  // namespace photo_editor::desktop
