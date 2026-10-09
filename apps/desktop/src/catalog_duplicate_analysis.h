#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <memory>

namespace photo_editor::desktop {

struct CatalogDuplicateCandidate final {
  QString asset_id;
  QString source_path;
  // Optional metadata captured when the candidate list was prepared. A
  // negative value means the analyzer should use its own initial snapshot.
  qint64 expected_size_bytes = -1;
  qint64 expected_modified_ms = -1;
};

enum class CatalogDuplicateSkipReason {
  MissingAssetId,
  DuplicateAssetId,
  MissingSource,
  NotRegularFile,
  Unreadable,
  SourceChanged,
  Cancelled
};

struct CatalogDuplicateSkippedFile final {
  QString asset_id;
  QString source_path;
  CatalogDuplicateSkipReason reason = CatalogDuplicateSkipReason::MissingSource;
  QString detail;
};

struct CatalogDuplicateContentGroup final {
  // This key is a SHA-256 digest of file contents, computed only after the
  // source signature remained stable for the complete read.
  QString sha256;
  qint64 size_bytes = 0;
  QStringList asset_ids;
  QStringList source_paths;
};

struct CatalogDuplicateAnalysisResult final {
  QVector<CatalogDuplicateContentGroup> content_groups;
  // Maps each member asset ID to its confirmed SHA-256 group key.
  QHash<QString, QString> confirmed_group_by_asset_id;
  QVector<CatalogDuplicateSkippedFile> skipped_files;
  qsizetype input_count = 0;
  qsizetype size_unique_file_count = 0;
  qsizetype hash_attempt_count = 0;
  qsizetype hashed_file_count = 0;
  bool cancelled = false;

  [[nodiscard]] qsizetype skippedFileCount() const noexcept {
    return skipped_files.size();
  }
};

using CatalogDuplicateCancellationToken = std::shared_ptr<std::atomic_bool>;

// Groups only byte-identical files. File contents are read only when at least
// two candidates have equal size; each read checks cancellation and verifies
// the source path, canonical path, size, and modification time before and
// after hashing. The caller owns the cancellation token for the call's life.
[[nodiscard]] CatalogDuplicateAnalysisResult analyze_catalog_duplicates(
    const QVector<CatalogDuplicateCandidate>& candidates,
    const CatalogDuplicateCancellationToken& cancellation = {});

[[nodiscard]] QString catalog_duplicate_skip_reason_name(
    CatalogDuplicateSkipReason reason);

}  // namespace photo_editor::desktop
