#include "catalog_duplicate_analysis.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>

namespace {

using namespace photo_editor::desktop;

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

bool write_file(const QString& path, const QByteArray& contents) {
  if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    return false;
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size() && file.flush();
}

CatalogDuplicateCandidate candidate(const QString& asset_id, const QString& path) {
  const QFileInfo info(path);
  return {
      asset_id,
      path,
      info.size(),
      info.lastModified().toMSecsSinceEpoch()};
}

const CatalogDuplicateSkippedFile* find_skip(
    const CatalogDuplicateAnalysisResult& result,
    const QString& asset_id) {
  for (const auto& skipped : result.skipped_files) {
    if (skipped.asset_id == asset_id)
      return &skipped;
  }
  return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir temporary;
  if (!temporary.isValid())
    return fail("Could not create duplicate analysis fixture");

  const auto root = temporary.path();
  const auto identical_contents = QByteArrayLiteral("same image bytes");
  const auto first_path = QDir(root).filePath(QStringLiteral("camera-a/IMG_1001.jpg"));
  const auto second_path = QDir(root).filePath(QStringLiteral("camera-b/renamed-copy.raw"));
  const auto different_same_size_path = QDir(root).filePath(QStringLiteral("camera-c/IMG_1001.jpg"));
  const auto unique_size_path = QDir(root).filePath(QStringLiteral("camera-d/IMG_1001.jpg"));
  if (!write_file(first_path, identical_contents) ||
      !write_file(second_path, identical_contents) ||
      !write_file(different_same_size_path, QByteArrayLiteral("different image!")) ||
      !write_file(unique_size_path, QByteArrayLiteral("a different length")))
    return fail("Could not write duplicate analysis fixtures");

  const auto same_size = QFileInfo(first_path).size();
  if (same_size != QFileInfo(second_path).size() ||
      same_size != QFileInfo(different_same_size_path).size() ||
      same_size == QFileInfo(unique_size_path).size())
    return fail("Duplicate analysis fixture sizes are not set up correctly");

  const QVector<CatalogDuplicateCandidate> candidates{
      candidate(QStringLiteral("asset-a"), first_path),
      candidate(QStringLiteral("asset-b"), second_path),
      candidate(QStringLiteral("asset-c"), different_same_size_path),
      candidate(QStringLiteral("asset-unique-size"), unique_size_path)};
  const auto result = analyze_catalog_duplicates(candidates);
  const auto expected_hash = QString::fromLatin1(
      QCryptographicHash::hash(identical_contents, QCryptographicHash::Sha256).toHex());
  if (result.cancelled || result.input_count != 4 || result.content_groups.size() != 1 ||
      result.content_groups.constFirst().sha256 != expected_hash ||
      result.content_groups.constFirst().size_bytes != same_size ||
      result.content_groups.constFirst().asset_ids != QStringList{QStringLiteral("asset-a"), QStringLiteral("asset-b")} ||
      result.confirmed_group_by_asset_id.value(QStringLiteral("asset-a")) != expected_hash ||
      result.confirmed_group_by_asset_id.value(QStringLiteral("asset-b")) != expected_hash ||
      result.confirmed_group_by_asset_id.contains(QStringLiteral("asset-c")) ||
      result.confirmed_group_by_asset_id.contains(QStringLiteral("asset-unique-size")))
    return fail("Only byte-identical, equal-size assets should receive confirmed SHA-256 groups");
  if (result.size_unique_file_count != 1 || result.hash_attempt_count != 3 ||
      result.hashed_file_count != 3 || !result.skipped_files.isEmpty())
    return fail("Unique-size files should avoid content reads while matching-size candidates are hashed");

  auto stale_candidate = candidate(QStringLiteral("stale-size"), first_path);
  stale_candidate.expected_size_bytes += 1;
  auto stale_mtime_candidate = candidate(QStringLiteral("stale-mtime"), second_path);
  stale_mtime_candidate.expected_modified_ms += 60'000;
  const QVector<CatalogDuplicateCandidate> stale_inputs{
      stale_candidate,
      stale_mtime_candidate,
      candidate(QStringLiteral("missing-source"), QDir(root).filePath(QStringLiteral("gone.jpg"))),
      {QStringLiteral("directory-source"), root, -1, -1}};
  const auto stale_result = analyze_catalog_duplicates(stale_inputs);
  if (stale_result.content_groups.size() != 0 || stale_result.skippedFileCount() != 4 ||
      !find_skip(stale_result, QStringLiteral("stale-size")) ||
      find_skip(stale_result, QStringLiteral("stale-size"))->reason != CatalogDuplicateSkipReason::SourceChanged ||
      !find_skip(stale_result, QStringLiteral("stale-mtime")) ||
      find_skip(stale_result, QStringLiteral("stale-mtime"))->reason != CatalogDuplicateSkipReason::SourceChanged ||
      !find_skip(stale_result, QStringLiteral("missing-source")) ||
      find_skip(stale_result, QStringLiteral("missing-source"))->reason != CatalogDuplicateSkipReason::MissingSource ||
      !find_skip(stale_result, QStringLiteral("directory-source")) ||
      find_skip(stale_result, QStringLiteral("directory-source"))->reason != CatalogDuplicateSkipReason::NotRegularFile)
    return fail("Changed, missing, and non-file sources must be skipped with explicit reasons");

  auto cancellation = std::make_shared<std::atomic_bool>(true);
  const auto canceled_result = analyze_catalog_duplicates(
      {candidate(QStringLiteral("cancel-a"), first_path),
       candidate(QStringLiteral("cancel-b"), second_path)},
      cancellation);
  if (!canceled_result.cancelled || canceled_result.hash_attempt_count != 0 ||
      canceled_result.hashed_file_count != 0 || canceled_result.skippedFileCount() != 2 ||
      !find_skip(canceled_result, QStringLiteral("cancel-a")) ||
      find_skip(canceled_result, QStringLiteral("cancel-a"))->reason != CatalogDuplicateSkipReason::Cancelled)
    return fail("Atomic cancellation must stop analysis and account for unprocessed files");

  std::cout << "Content-only duplicate grouping, size bucketing, source signatures, skips, and cancellation passed\n";
  return 0;
}
