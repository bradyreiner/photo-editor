#pragma once

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QRegularExpression>

namespace photo_editor::desktop::timeline_visible_proof {

inline constexpr auto kCacheDirectoryName = "timeline-visible-proofs";
inline constexpr int kMaximumCaptures = 16;
inline constexpr qint64 kMaximumCacheBytes = 512LL * 1024LL * 1024LL;

struct Cache final {
  bool ok = false;
  QString directory;
  QString error;
  int capture_count = 0;
  qint64 byte_count = 0;
};

// Inspect only the application-selected cache root. The caller supplies
// AppBootstrap::cacheLocation() so isolated workers stay inside their private
// runtime instead of sharing the user's global QStandardPaths cache.
inline Cache inspect_cache(const QString& cache_root) {
  Cache result;
  if (cache_root.trimmed().isEmpty()) {
    result.error = QStringLiteral("The app cache directory is unavailable.");
    return result;
  }
  if (!QDir().mkpath(cache_root)) {
    result.error = QStringLiteral("The app cache directory could not be prepared.");
    return result;
  }
  const QFileInfo cache_root_info(cache_root);
  const auto canonical_cache_root = cache_root_info.canonicalFilePath();
  if (!cache_root_info.isDir() || canonical_cache_root.isEmpty()) {
    result.error = QStringLiteral("The app cache directory is not a usable directory.");
    return result;
  }

  const auto proof_directory = QDir(canonical_cache_root).filePath(
      QString::fromLatin1(kCacheDirectoryName));
  QFileInfo proof_directory_info(proof_directory);
  if (proof_directory_info.exists() && proof_directory_info.isSymLink()) {
    result.error = QStringLiteral("The app proof cache path is unsafe.");
    return result;
  }
  if (!proof_directory_info.exists() && !QDir(canonical_cache_root).mkdir(
          QString::fromLatin1(kCacheDirectoryName))) {
    result.error = QStringLiteral("The app proof cache directory could not be created.");
    return result;
  }
  proof_directory_info.setFile(proof_directory);
  const auto canonical_proof_directory = proof_directory_info.canonicalFilePath();
  if (!proof_directory_info.isDir() || canonical_proof_directory.isEmpty() ||
      QDir::cleanPath(canonical_proof_directory) != QDir::cleanPath(proof_directory)) {
    result.error = QStringLiteral("The app proof cache directory is not safely contained.");
    return result;
  }
  const auto owner_permissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                 QFileDevice::ExeOwner;
  if (!QFile::setPermissions(canonical_proof_directory, owner_permissions)) {
    result.error = QStringLiteral("The app proof cache permissions could not be restricted.");
    return result;
  }

  const QRegularExpression capture_name_pattern(
      QStringLiteral("^timeline-visible-[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\.png$"));
  const QDir proof_dir(canonical_proof_directory);
  const auto entries = proof_dir.entryInfoList(
      QDir::NoDotAndDotDot | QDir::AllEntries, QDir::Name);
  for (const auto& entry : entries) {
    if (!capture_name_pattern.match(entry.fileName()).hasMatch()) continue;
    if (entry.isSymLink() || !entry.isFile() || entry.size() < 0) {
      result.error = QStringLiteral("The app proof cache contains an unsafe capture entry.");
      return result;
    }
    ++result.capture_count;
    result.byte_count += entry.size();
  }
  if (result.capture_count > kMaximumCaptures ||
      result.byte_count > kMaximumCacheBytes) {
    result.error = QStringLiteral("The app proof cache has reached its bounded storage limit.");
    return result;
  }
  result.directory = canonical_proof_directory;
  result.ok = true;
  return result;
}

}  // namespace photo_editor::desktop::timeline_visible_proof
