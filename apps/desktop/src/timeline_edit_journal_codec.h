#pragma once

#include <QIODevice>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <atomic>

namespace photo_editor::desktop::timeline_edit_journal_codec {

inline constexpr qsizetype kMaximumDocumentBytes = 256 * 1024 * 1024;
inline constexpr qsizetype kMaximumCompressedBytes = 256 * 1024 * 1024;
inline constexpr qsizetype kMaximumEntryMetadataBytes = 1024 * 1024;

struct ReadResult final {
  QVector<QJsonObject> entries;
  int validEntryCount = 0;
  qint64 bytesRead = 0;
  qint64 lastEntryBytes = 0;
  qint64 lastDocumentBytes = 0;
  QString lastEntryHash;
  bool damagedTail = false;
  bool cancelled = false;
  QString error;
};

struct ListingTail final {
  QJsonObject entry;
  qint64 bytesRead = 0;
  bool damagedTail = false;
  // A valid final envelope does not establish the integrity of its prefix.
  bool requiresFullVerification = true;
  QString error;
};

// Presentation-only bounded tail read. Validates at most two final envelopes,
// never the full predecessor chain. Recovery/open/write must use readJsonLines.
[[nodiscard]] ListingTail readListingTail(
    QIODevice* input,
    const std::atomic_bool* cancellation = nullptr);

// Produces a storage envelope for a new compressed v2 record. The input is the
// existing v1-shaped logical entry before entryHash is assigned. Its document
// hash and previousEntryHash must already be present and correct.
[[nodiscard]] bool encodeCompressedV2(
    const QJsonObject& logical_v1_entry,
    QJsonObject* stored_entry,
    QString* error_message = nullptr);

// Decodes and validates one v1 or v2 storage record. For v2, the returned
// entry contains the decoded document and keeps its original v2 entryHash.
[[nodiscard]] bool decodeStorageEntry(
    const QJsonObject& stored_entry,
    QJsonObject* decoded_entry,
    QString* error_message = nullptr,
    qint64* decoded_document_bytes = nullptr);

// Reads a mixed-format JSONL stream, validates every hash link, and returns
// either the full decoded history or only its final record. A damaged suffix
// is reported without discarding already validated entries.
[[nodiscard]] ReadResult readJsonLines(
    QIODevice* input,
    bool retain_history = true,
    const std::atomic_bool* cancellation = nullptr);

[[nodiscard]] QString stableDocumentHash(QJsonObject document);
[[nodiscard]] QString entryHash(QJsonObject entry);
[[nodiscard]] bool compressedSizePairIsValid(
    qint64 compressed_bytes,
    qint64 uncompressed_bytes);
[[nodiscard]] qsizetype maximumEncodedLineBytes();

}  // namespace photo_editor::desktop::timeline_edit_journal_codec
