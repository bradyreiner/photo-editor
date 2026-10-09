#include "catalog_duplicate_analysis.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>

#include <utility>

namespace photo_editor::desktop {
namespace {

constexpr qint64 kHashChunkBytes = 1024 * 1024;

struct SourceSignature final {
  QString source_path;
  QString resolved_path;
  qint64 size_bytes = -1;
  qint64 modified_ms = -1;
};

struct PreparedCandidate final {
  CatalogDuplicateCandidate candidate;
  SourceSignature signature;
  qsizetype input_index = -1;
};

bool is_cancelled(const CatalogDuplicateCancellationToken& cancellation) {
  return cancellation && cancellation->load(std::memory_order_relaxed);
}

bool capture_signature(const QString& path, SourceSignature* signature, QString* detail) {
  QFileInfo info(path);
  info.refresh();
  if (!info.exists()) {
    if (detail) *detail = QStringLiteral("The source file is missing.");
    return false;
  }
  if (!info.isFile()) {
    if (detail) *detail = QStringLiteral("The source is not a regular file.");
    return false;
  }
  if (!info.isReadable()) {
    if (detail) *detail = QStringLiteral("The source file is not readable.");
    return false;
  }

  const auto canonical_path = info.canonicalFilePath();
  signature->source_path = path;
  signature->resolved_path = canonical_path.isEmpty()
      ? QDir::cleanPath(info.absoluteFilePath())
      : QDir::cleanPath(canonical_path);
  signature->size_bytes = info.size();
  signature->modified_ms = info.lastModified().toMSecsSinceEpoch();
  if (signature->size_bytes < 0 || signature->resolved_path.isEmpty()) {
    if (detail) *detail = QStringLiteral("The source file signature is unavailable.");
    return false;
  }
  return true;
}

bool same_signature(const SourceSignature& left, const SourceSignature& right) {
  return left.source_path == right.source_path &&
         left.resolved_path == right.resolved_path &&
         left.size_bytes == right.size_bytes &&
         left.modified_ms == right.modified_ms;
}

void append_skipped(
    CatalogDuplicateAnalysisResult* result,
    const CatalogDuplicateCandidate& candidate,
    CatalogDuplicateSkipReason reason,
    const QString& detail) {
  result->skipped_files.push_back({candidate.asset_id, candidate.source_path, reason, detail});
}

void mark_unaccounted_cancelled(
    CatalogDuplicateAnalysisResult* result,
    const QVector<CatalogDuplicateCandidate>& candidates,
    const QSet<qsizetype>& accounted) {
  for (qsizetype index = 0; index < candidates.size(); ++index) {
    if (!accounted.contains(index)) {
      append_skipped(
          result,
          candidates.at(index),
          CatalogDuplicateSkipReason::Cancelled,
          QStringLiteral("Duplicate analysis was canceled before this file was read."));
    }
  }
}

struct HashedCandidate final {
  PreparedCandidate prepared;
  QByteArray digest;
};

enum class HashStatus { Complete, Skipped, Cancelled };

HashStatus hash_candidate(
    const PreparedCandidate& prepared,
    const CatalogDuplicateCancellationToken& cancellation,
    QByteArray* digest,
    CatalogDuplicateSkipReason* skip_reason,
    QString* skip_detail) {
  if (is_cancelled(cancellation)) {
    *skip_reason = CatalogDuplicateSkipReason::Cancelled;
    *skip_detail = QStringLiteral("Duplicate analysis was canceled before this file was read.");
    return HashStatus::Cancelled;
  }

  SourceSignature before;
  QString signature_detail;
  if (!capture_signature(prepared.candidate.source_path, &before, &signature_detail)) {
    *skip_reason = signature_detail.contains(QStringLiteral("not a regular file"))
        ? CatalogDuplicateSkipReason::NotRegularFile
        : (signature_detail.contains(QStringLiteral("not readable"))
               ? CatalogDuplicateSkipReason::Unreadable
               : CatalogDuplicateSkipReason::MissingSource);
    *skip_detail = signature_detail;
    return HashStatus::Skipped;
  }
  if (!same_signature(prepared.signature, before) ||
      (prepared.candidate.expected_size_bytes >= 0 &&
       before.size_bytes != prepared.candidate.expected_size_bytes) ||
      (prepared.candidate.expected_modified_ms >= 0 &&
       before.modified_ms != prepared.candidate.expected_modified_ms)) {
    *skip_reason = CatalogDuplicateSkipReason::SourceChanged;
    *skip_detail = QStringLiteral("The source path, size, or modification time changed before hashing.");
    return HashStatus::Skipped;
  }

  QFile file(prepared.candidate.source_path);
  if (!file.open(QIODevice::ReadOnly)) {
    const auto open_error = file.errorString();
    SourceSignature after_open_failure;
    if (!capture_signature(prepared.candidate.source_path, &after_open_failure, nullptr) ||
        !same_signature(before, after_open_failure)) {
      *skip_reason = CatalogDuplicateSkipReason::SourceChanged;
      *skip_detail = QStringLiteral("The source path, size, or modification time changed while opening the file.");
    } else {
      *skip_reason = CatalogDuplicateSkipReason::Unreadable;
      *skip_detail = QStringLiteral("The source file could not be opened for reading: %1").arg(open_error);
    }
    return HashStatus::Skipped;
  }

  QCryptographicHash hash(QCryptographicHash::Sha256);
  qint64 bytes_read = 0;
  QString read_error;
  for (;;) {
    if (is_cancelled(cancellation)) {
      *skip_reason = CatalogDuplicateSkipReason::Cancelled;
      *skip_detail = QStringLiteral("Duplicate analysis was canceled while this file was being read.");
      return HashStatus::Cancelled;
    }
    const auto chunk = file.read(kHashChunkBytes);
    if (chunk.isEmpty()) {
      if (file.atEnd())
        break;
      read_error = file.error() != QFileDevice::NoError
          ? QStringLiteral("The source file could not be read: %1").arg(file.errorString())
          : QStringLiteral("The source file returned no data before reaching its end.");
      break;
    }
    bytes_read += chunk.size();
    hash.addData(chunk);
  }
  if (read_error.isEmpty() && file.error() != QFileDevice::NoError)
    read_error = QStringLiteral("The source file could not be read: %1").arg(file.errorString());
  file.close();

  SourceSignature after;
  if (!capture_signature(prepared.candidate.source_path, &after, &signature_detail)) {
    *skip_reason = CatalogDuplicateSkipReason::SourceChanged;
    *skip_detail = QStringLiteral("The source disappeared or became unreadable during hashing.");
    return HashStatus::Skipped;
  }
  if (!same_signature(before, after) || !same_signature(prepared.signature, after) ||
      bytes_read != before.size_bytes ||
      (prepared.candidate.expected_size_bytes >= 0 &&
       after.size_bytes != prepared.candidate.expected_size_bytes) ||
      (prepared.candidate.expected_modified_ms >= 0 &&
       after.modified_ms != prepared.candidate.expected_modified_ms)) {
    *skip_reason = CatalogDuplicateSkipReason::SourceChanged;
    *skip_detail = QStringLiteral("The source path, size, or modification time changed during hashing.");
    return HashStatus::Skipped;
  }
  if (!read_error.isEmpty()) {
    *skip_reason = CatalogDuplicateSkipReason::Unreadable;
    *skip_detail = read_error;
    return HashStatus::Skipped;
  }

  *digest = hash.result().toHex();
  return HashStatus::Complete;
}

}  // namespace

CatalogDuplicateAnalysisResult analyze_catalog_duplicates(
    const QVector<CatalogDuplicateCandidate>& candidates,
    const CatalogDuplicateCancellationToken& cancellation) {
  CatalogDuplicateAnalysisResult result;
  result.input_count = candidates.size();

  QMap<qint64, QVector<PreparedCandidate>> candidates_by_size;
  QSet<QString> asset_ids;
  QSet<qsizetype> accounted;
  accounted.reserve(candidates.size());

  for (qsizetype index = 0; index < candidates.size(); ++index) {
    if (is_cancelled(cancellation)) {
      result.cancelled = true;
      mark_unaccounted_cancelled(&result, candidates, accounted);
      return result;
    }

    const auto& candidate = candidates.at(index);
    if (candidate.asset_id.trimmed().isEmpty()) {
      append_skipped(
          &result,
          candidate,
          CatalogDuplicateSkipReason::MissingAssetId,
          QStringLiteral("A catalog asset ID is required for a content duplicate group."));
      accounted.insert(index);
      continue;
    }
    if (asset_ids.contains(candidate.asset_id)) {
      append_skipped(
          &result,
          candidate,
          CatalogDuplicateSkipReason::DuplicateAssetId,
          QStringLiteral("The candidate list contains this asset ID more than once."));
      accounted.insert(index);
      continue;
    }
    asset_ids.insert(candidate.asset_id);

    if (candidate.source_path.trimmed().isEmpty()) {
      append_skipped(
          &result,
          candidate,
          CatalogDuplicateSkipReason::MissingSource,
          QStringLiteral("The catalog asset has no source path."));
      accounted.insert(index);
      continue;
    }

    SourceSignature signature;
    QString detail;
    if (!capture_signature(candidate.source_path, &signature, &detail)) {
      const auto reason = detail.contains(QStringLiteral("not a regular file"))
          ? CatalogDuplicateSkipReason::NotRegularFile
          : (detail.contains(QStringLiteral("not readable"))
                 ? CatalogDuplicateSkipReason::Unreadable
                 : CatalogDuplicateSkipReason::MissingSource);
      append_skipped(&result, candidate, reason, detail);
      accounted.insert(index);
      continue;
    }
    if ((candidate.expected_size_bytes >= 0 &&
         signature.size_bytes != candidate.expected_size_bytes) ||
        (candidate.expected_modified_ms >= 0 &&
         signature.modified_ms != candidate.expected_modified_ms)) {
      append_skipped(
          &result,
          candidate,
          CatalogDuplicateSkipReason::SourceChanged,
          QStringLiteral("The source path, size, or modification time no longer matches the catalog snapshot."));
      accounted.insert(index);
      continue;
    }

    PreparedCandidate prepared{candidate, signature, index};
    candidates_by_size[signature.size_bytes].push_back(prepared);
  }

  QVector<PreparedCandidate> hash_work;
  for (auto iterator = candidates_by_size.cbegin(); iterator != candidates_by_size.cend(); ++iterator) {
    if (is_cancelled(cancellation)) {
      result.cancelled = true;
      mark_unaccounted_cancelled(&result, candidates, accounted);
      return result;
    }
    if (iterator.value().size() == 1) {
      ++result.size_unique_file_count;
      accounted.insert(iterator.value().constFirst().input_index);
      continue;
    }
    hash_work += iterator.value();
  }

  QMap<QByteArray, QVector<HashedCandidate>> candidates_by_digest;
  for (qsizetype index = 0; index < hash_work.size(); ++index) {
    if (is_cancelled(cancellation)) {
      result.cancelled = true;
      for (qsizetype pending = index; pending < hash_work.size(); ++pending) {
        const auto& item = hash_work.at(pending);
        if (!accounted.contains(item.input_index)) {
          append_skipped(
              &result,
              item.candidate,
              CatalogDuplicateSkipReason::Cancelled,
              QStringLiteral("Duplicate analysis was canceled before this file was read."));
          accounted.insert(item.input_index);
        }
      }
      return result;
    }

    const auto& item = hash_work.at(index);
    ++result.hash_attempt_count;
    QByteArray digest;
    CatalogDuplicateSkipReason skip_reason = CatalogDuplicateSkipReason::Unreadable;
    QString skip_detail;
    const auto status = hash_candidate(
        item,
        cancellation,
        &digest,
        &skip_reason,
        &skip_detail);
    if (status == HashStatus::Cancelled) {
      result.cancelled = true;
      for (qsizetype pending = index; pending < hash_work.size(); ++pending) {
        const auto& pending_item = hash_work.at(pending);
        if (!accounted.contains(pending_item.input_index)) {
          append_skipped(
              &result,
              pending_item.candidate,
              CatalogDuplicateSkipReason::Cancelled,
              pending == index
                  ? skip_detail
                  : QStringLiteral("Duplicate analysis was canceled before this file was read."));
          accounted.insert(pending_item.input_index);
        }
      }
      return result;
    }
    if (status == HashStatus::Skipped) {
      append_skipped(&result, item.candidate, skip_reason, skip_detail);
      accounted.insert(item.input_index);
      continue;
    }

    ++result.hashed_file_count;
    accounted.insert(item.input_index);
    candidates_by_digest[digest].push_back({item, digest});
  }

  for (auto iterator = candidates_by_digest.cbegin(); iterator != candidates_by_digest.cend(); ++iterator) {
    if (iterator.value().size() < 2)
      continue;

    CatalogDuplicateContentGroup group;
    group.sha256 = QString::fromLatin1(iterator.key());
    group.size_bytes = iterator.value().constFirst().prepared.signature.size_bytes;
    for (const auto& member : iterator.value()) {
      group.asset_ids.push_back(member.prepared.candidate.asset_id);
      group.source_paths.push_back(member.prepared.candidate.source_path);
      result.confirmed_group_by_asset_id.insert(member.prepared.candidate.asset_id, group.sha256);
    }
    result.content_groups.push_back(std::move(group));
  }

  return result;
}

QString catalog_duplicate_skip_reason_name(CatalogDuplicateSkipReason reason) {
  switch (reason) {
    case CatalogDuplicateSkipReason::MissingAssetId:
      return QStringLiteral("missing-asset-id");
    case CatalogDuplicateSkipReason::DuplicateAssetId:
      return QStringLiteral("duplicate-asset-id");
    case CatalogDuplicateSkipReason::MissingSource:
      return QStringLiteral("missing-source");
    case CatalogDuplicateSkipReason::NotRegularFile:
      return QStringLiteral("not-regular-file");
    case CatalogDuplicateSkipReason::Unreadable:
      return QStringLiteral("unreadable");
    case CatalogDuplicateSkipReason::SourceChanged:
      return QStringLiteral("source-changed");
    case CatalogDuplicateSkipReason::Cancelled:
      return QStringLiteral("cancelled");
  }
  return QStringLiteral("unknown");
}

}  // namespace photo_editor::desktop
