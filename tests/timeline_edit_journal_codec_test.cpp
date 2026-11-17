#include "../apps/desktop/src/timeline_edit_journal_codec.h"

#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QtEndian>

#include <iostream>

namespace codec = photo_editor::desktop::timeline_edit_journal_codec;

namespace {

bool require(bool condition, const char* message) {
  if (condition) return true;
  std::cerr << message << '\n';
  return false;
}

QJsonObject make_document(const QString& marker) {
  QJsonObject track;
  track.insert(QStringLiteral("id"), marker);
  track.insert(QStringLiteral("clips"), QJsonArray{});
  QJsonObject document;
  document.insert(QStringLiteral("timelineId"),
                  QStringLiteral("00000000-0000-4000-8000-000000000001"));
  document.insert(QStringLiteral("name"), QStringLiteral("Journal codec fixture"));
  document.insert(QStringLiteral("updatedAt"), QStringLiteral("2026-10-28T12:00:00Z"));
  document.insert(QStringLiteral("tracks"), QJsonArray{track});
  return document;
}

QJsonObject make_v1_entry(const QJsonObject& document,
                          const QString& previous_hash,
                          const QString& entry_id) {
  QJsonObject entry{
      {QStringLiteral("schema"), QStringLiteral("timeline-edit-journal:v1")},
      {QStringLiteral("entryId"), entry_id},
      {QStringLiteral("timelineId"), document.value(QStringLiteral("timelineId"))},
      {QStringLiteral("timestamp"), QStringLiteral("2026-10-28T12:00:00Z")},
      {QStringLiteral("reason"), QStringLiteral("codec-test")},
      {QStringLiteral("sequenceName"), QStringLiteral("Journal codec fixture")},
      {QStringLiteral("baseDocumentPath"), QStringLiteral("/managed/fixture.json")},
      {QStringLiteral("document"), document},
      {QStringLiteral("documentHash"), codec::stableDocumentHash(document)},
      {QStringLiteral("previousEntryHash"), previous_hash}};
  entry.insert(QStringLiteral("entryHash"), codec::entryHash(entry));
  return entry;
}

QByteArray line(const QJsonObject& entry) {
  auto result = QJsonDocument(entry).toJson(QJsonDocument::Compact);
  result.append('\n');
  return result;
}

bool append_to_buffer(QBuffer* buffer, const QJsonObject& entry) {
  const auto bytes = line(entry);
  return buffer->write(bytes) == bytes.size();
}

bool mixed_history_roundtrip() {
  const auto first_document = make_document(QStringLiteral("legacy-prefix"));
  const auto first_v1 = make_v1_entry(first_document, {}, QStringLiteral("entry-v1"));
  const auto second_document = make_document(QStringLiteral("compressed-middle"));
  const auto second_v1 = make_v1_entry(
      second_document, first_v1.value(QStringLiteral("entryHash")).toString(),
      QStringLiteral("entry-v2-a"));
  QJsonObject second_v2;
  QString error;
  if (!codec::encodeCompressedV2(second_v1, &second_v2, &error)) {
    std::cerr << "encode v2: " << error.toStdString() << '\n';
    return false;
  }

  const auto third_document = make_document(QStringLiteral("compressed-tail"));
  const auto third_v1 = make_v1_entry(
      third_document, second_v2.value(QStringLiteral("entryHash")).toString(),
      QStringLiteral("entry-v2-b"));
  QJsonObject third_v2;
  if (!codec::encodeCompressedV2(third_v1, &third_v2, &error)) {
    std::cerr << "encode second v2: " << error.toStdString() << '\n';
    return false;
  }

  QBuffer stream;
  if (!stream.open(QIODevice::ReadWrite) ||
      !append_to_buffer(&stream, first_v1) ||
      !append_to_buffer(&stream, second_v2) ||
      !append_to_buffer(&stream, third_v2)) {
    return require(false, "mixed journal fixture should serialize all three records");
  }
  stream.seek(0);
  const auto history = codec::readJsonLines(&stream, true);
  if (!require(!history.damagedTail && history.validEntryCount == 3 &&
                   history.entries.size() == 3 &&
                   history.entries.front() == first_v1 &&
                   second_v2.value(QStringLiteral("previousEntryHash")) ==
                       first_v1.value(QStringLiteral("entryHash")) &&
                   history.entries[1].value(QStringLiteral("entryHash")) ==
                       second_v2.value(QStringLiteral("entryHash")) &&
                   history.entries[1].value(QStringLiteral("document")) ==
                       QJsonValue(second_document) &&
                   !history.entries[1].contains(QStringLiteral("documentCompressedBase64")) &&
                   history.entries.back().value(QStringLiteral("entryHash")) ==
                       third_v2.value(QStringLiteral("entryHash")) &&
                   third_v2.value(QStringLiteral("previousEntryHash")) ==
                       second_v2.value(QStringLiteral("entryHash")) &&
                   history.lastEntryHash == third_v2.value(QStringLiteral("entryHash")).toString(),
               "mixed v1/v2 chain should return every logical snapshot and preserve existing hashes")) {
    return false;
  }

  stream.seek(0);
  const auto head_only = codec::readJsonLines(&stream, false);
  return require(!head_only.damagedTail && head_only.validEntryCount == 3 &&
                     head_only.entries.size() == 1 &&
                     head_only.entries.front().value(QStringLiteral("entryHash")) ==
                         third_v2.value(QStringLiteral("entryHash")),
                 "head-only validation should retain the final record while checking the full chain");
}

bool corruption_fails_closed() {
  const auto first = make_v1_entry(make_document(QStringLiteral("base")), {},
                                   QStringLiteral("entry-base"));
  auto next_logical = make_v1_entry(
      make_document(QStringLiteral("next")),
      first.value(QStringLiteral("entryHash")).toString(), QStringLiteral("entry-next"));
  QJsonObject valid_v2;
  QString error;
  if (!codec::encodeCompressedV2(next_logical, &valid_v2, &error)) {
    std::cerr << "encode corruption fixture: " << error.toStdString() << '\n';
    return false;
  }

  auto damaged_payload = valid_v2;
  auto encoded = damaged_payload.value(QStringLiteral("documentCompressedBase64")).toString();
  encoded[encoded.size() / 2] = encoded.at(encoded.size() / 2) == QLatin1Char('A')
      ? QLatin1Char('B') : QLatin1Char('A');
  damaged_payload.insert(QStringLiteral("documentCompressedBase64"), encoded);

  auto bad_declared_size = valid_v2;
  bad_declared_size.insert(QStringLiteral("documentBytes"), 257.0);
  bad_declared_size.remove(QStringLiteral("entryHash"));
  bad_declared_size.insert(QStringLiteral("entryHash"), codec::entryHash(bad_declared_size));

  auto bad_predecessor = valid_v2;
  bad_predecessor.insert(QStringLiteral("previousEntryHash"), QString(64, QLatin1Char('a')));
  bad_predecessor.remove(QStringLiteral("entryHash"));
  bad_predecessor.insert(QStringLiteral("entryHash"), codec::entryHash(bad_predecessor));

  auto bad_entry_hash = valid_v2;
  bad_entry_hash.insert(QStringLiteral("entryHash"), QString(64, QLatin1Char('b')));

  for (const auto& invalid : {damaged_payload, bad_declared_size, bad_entry_hash}) {
    QJsonObject decoded;
    if (codec::decodeStorageEntry(invalid, &decoded, &error)) {
      return require(false, "corrupt compressed payload, size, and entry hashes must be rejected");
    }
  }

  auto trailing_deflate_bytes = valid_v2;
  auto compressed = QByteArray::fromBase64(
      trailing_deflate_bytes.value(QStringLiteral("documentCompressedBase64")).toString().toLatin1());
  compressed.append('\0');
  trailing_deflate_bytes.insert(QStringLiteral("documentCompressedBase64"),
                                QString::fromLatin1(compressed.toBase64()));
  trailing_deflate_bytes.remove(QStringLiteral("entryHash"));
  trailing_deflate_bytes.insert(QStringLiteral("entryHash"), codec::entryHash(trailing_deflate_bytes));
  QJsonObject trailing_decoded;
  if (codec::decodeStorageEntry(trailing_deflate_bytes, &trailing_decoded, &error)) {
    return require(false, "trailing compressed bytes after the zlib stream must be rejected");
  }

  QBuffer chain;
  if (!chain.open(QIODevice::ReadWrite) || !append_to_buffer(&chain, first) ||
      !append_to_buffer(&chain, bad_predecessor)) {
    return require(false, "corrupt chain fixture should serialize");
  }
  chain.seek(0);
  const auto read = codec::readJsonLines(&chain, true);
  return require(read.damagedTail && read.validEntryCount == 1 &&
                     read.entries.size() == 1 &&
                     read.entries.front().value(QStringLiteral("entryHash")) ==
                         first.value(QStringLiteral("entryHash")),
                 "a broken predecessor must preserve the verified prefix and reject the tail");
}

bool bounded_payload_sizes() {
  const auto cap = static_cast<qint64>(codec::kMaximumDocumentBytes);
  if (!require(codec::compressedSizePairIsValid(4, cap) &&
                   !codec::compressedSizePairIsValid(4, cap + 1) &&
                   !codec::compressedSizePairIsValid(
                       static_cast<qint64>(codec::kMaximumCompressedBytes) + 1, cap) &&
                   codec::maximumEncodedLineBytes() >
                       codec::kMaximumCompressedBytes,
               "compressed and decompressed byte limits should be exact and bounded")) {
    return false;
  }

  const auto first = make_v1_entry(make_document(QStringLiteral("size-base")), {},
                                   QStringLiteral("entry-size-base"));
  auto logical = make_v1_entry(
      make_document(QStringLiteral("size-next")),
      first.value(QStringLiteral("entryHash")).toString(), QStringLiteral("entry-size-next"));
  QJsonObject stored;
  QString error;
  if (!codec::encodeCompressedV2(logical, &stored, &error)) return false;
  stored.insert(QStringLiteral("documentBytes"), static_cast<double>(cap + 1));
  stored.remove(QStringLiteral("entryHash"));
  stored.insert(QStringLiteral("entryHash"), codec::entryHash(stored));
  QJsonObject decoded;
  return require(!codec::decodeStorageEntry(stored, &decoded, &error),
                 "a correctly rehashed entry declaring an over-limit decoded size must be rejected before inflate");
}

bool forged_qcompress_length_is_bounded() {
  constexpr qsizetype actual_size = 64 * 1024 * 1024;
  auto document = make_document(QStringLiteral("forged-length"));
  document.insert(QStringLiteral("largeDeterministicPayload"),
                  QString(actual_size, QLatin1Char('z')));
  auto logical = make_v1_entry(document, {}, QStringLiteral("entry-forged-length"));
  QJsonObject stored;
  QString error;
  if (!codec::encodeCompressedV2(logical, &stored, &error)) {
    std::cerr << "encode forged-length fixture: " << error.toStdString() << '\n';
    return false;
  }

  auto compressed = QByteArray::fromBase64(
      stored.value(QStringLiteral("documentCompressedBase64")).toString().toLatin1());
  constexpr quint32 forged_size = 128;
  qToBigEndian<quint32>(forged_size, compressed.data());
  stored.insert(QStringLiteral("documentBytes"), static_cast<double>(forged_size));
  stored.insert(QStringLiteral("documentCompressedBase64"),
                QString::fromLatin1(compressed.toBase64()));
  stored.remove(QStringLiteral("entryHash"));
  stored.insert(QStringLiteral("entryHash"), codec::entryHash(stored));

  QJsonObject decoded;
  return require(!codec::decodeStorageEntry(stored, &decoded, &error) &&
                     error.contains(QStringLiteral("expands beyond its declared")),
                 "a rehashed 64 MiB zlib payload claiming 128 bytes must stop at the declared cap");
}

bool large_snapshot_roundtrip() {
  // Exercise a realistic large immutable snapshot without allocating close to
  // the 256 MiB hard cap. The repeated body compresses well but round-trips
  // byte-exact through the same JSON hashing path as a normal entry.
  const auto repeated_size = static_cast<qsizetype>(80) * 1024 * 1024;
  const QString repeated(repeated_size, QLatin1Char('x'));
  auto document = make_document(QStringLiteral("large"));
  document.insert(QStringLiteral("largeDeterministicPayload"), repeated);
  auto logical = make_v1_entry(document, {}, QStringLiteral("entry-large"));

  QJsonObject stored;
  QString error;
  if (!codec::encodeCompressedV2(logical, &stored, &error)) {
    std::cerr << "encode large entry: " << error.toStdString() << '\n';
    return false;
  }
  const auto compressed_chars =
      stored.value(QStringLiteral("documentCompressedBase64")).toString().size();
  QJsonObject decoded;
  if (!codec::decodeStorageEntry(stored, &decoded, &error)) {
    std::cerr << "decode large entry: " << error.toStdString() << '\n';
    return false;
  }
  return require(compressed_chars < repeated_size &&
                     decoded.value(QStringLiteral("document")) == QJsonValue(document) &&
                     decoded.value(QStringLiteral("documentHash")) ==
                         logical.value(QStringLiteral("documentHash")) &&
                     decoded.value(QStringLiteral("entryHash")) ==
                         stored.value(QStringLiteral("entryHash")),
                 "an 80 MiB snapshot should compress below its source size and round-trip exactly");
}

bool listing_tail_is_bounded_and_never_full_verification() {
  const auto first = make_v1_entry(make_document(QStringLiteral("first")), {},
                                   QStringLiteral("listing-first"));
  const auto last_logical = make_v1_entry(
      make_document(QStringLiteral("last")),
      first.value(QStringLiteral("entryHash")).toString(), QStringLiteral("listing-last"));
  QJsonObject last;
  if (!codec::encodeCompressedV2(last_logical, &last)) return false;
  // A damaged prefix must not become trusted just because its tail is valid.
  QByteArray bytes = line(first) + QByteArray(8 * 1024 * 1024, 'x') + '\n' + line(last);
  QBuffer stream(&bytes);
  if (!stream.open(QIODevice::ReadOnly)) return false;
  const auto listing = codec::readListingTail(&stream);
  if (!require(!listing.entry.isEmpty() && listing.requiresFullVerification &&
                   listing.entry.value(QStringLiteral("entryHash")) == last.value(QStringLiteral("entryHash")) &&
                   listing.bytesRead <= 1024 * 1024 && listing.bytesRead < bytes.size(),
               "Recents should read only a bounded final envelope and require full verification on open"))
    return false;
  stream.seek(0);
  const auto strict = codec::readJsonLines(&stream, false);
  if (!require(strict.damagedTail && strict.validEntryCount == 1 &&
                   strict.lastEntryHash == first.value(QStringLiteral("entryHash")).toString(),
               "Full recovery validation must still reject a broken prefix before the listed tail"))
    return false;
  stream.close();
  bytes.append("{truncated:");
  stream.open(QIODevice::ReadOnly);
  const auto interrupted = codec::readListingTail(&stream);
  if (!require(interrupted.damagedTail && interrupted.requiresFullVerification &&
                   interrupted.entry.value(QStringLiteral("entryHash")) == last.value(QStringLiteral("entryHash")),
               "Listing should find the preceding complete envelope after an interrupted final write"))
    return false;
  std::atomic_bool cancelled{true};
  const auto cancelled_listing = codec::readListingTail(&stream, &cancelled);
  return require(cancelled_listing.entry.isEmpty() && !cancelled_listing.error.isEmpty(),
                 "Listing must honor cancellation without returning a partial result");
}

}  // namespace

int main() {
  return mixed_history_roundtrip() && corruption_fails_closed() &&
          bounded_payload_sizes() && forged_qcompress_length_is_bounded() &&
          large_snapshot_roundtrip() && listing_tail_is_bounded_and_never_full_verification()
      ? 0
      : 1;
}
