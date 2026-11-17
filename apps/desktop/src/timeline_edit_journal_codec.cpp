#include "timeline_edit_journal_codec.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <zlib.h>

namespace photo_editor::desktop::timeline_edit_journal_codec {
namespace {

constexpr auto kV1Schema = "timeline-edit-journal:v1";
constexpr auto kV2Schema = "timeline-edit-journal:v2";
constexpr auto kV2Encoding = "qcompress-json-base64-v1";
constexpr qsizetype kInflateChunkBytes = 64 * 1024;

void fail(QString* error_message, const QString& message) {
  if (error_message != nullptr) *error_message = message;
}

QByteArray compactJson(const QJsonObject& object) {
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QString sha256Hex(const QByteArray& bytes) {
  return QString::fromLatin1(
      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool validSha256(const QString& value) {
  if (value.size() != 64) return false;
  for (const auto character : value) {
    const bool digit = character >= QLatin1Char('0') && character <= QLatin1Char('9');
    const bool lower_hex = character >= QLatin1Char('a') && character <= QLatin1Char('f');
    if (!digit && !lower_hex) return false;
  }
  return true;
}

bool boundedQCompressInflate(const QByteArray& compressed,
                             const qint64 declared_bytes,
                             QByteArray* raw,
                             QString* error_message) {
  if (raw == nullptr || compressed.size() < 6 || declared_bytes <= 0 ||
      declared_bytes > kMaximumDocumentBytes ||
      compressed.size() - 4 > static_cast<qsizetype>(std::numeric_limits<uInt>::max())) {
    fail(error_message, QStringLiteral("Compressed Timeline journal inflate bounds are invalid."));
    return false;
  }

  z_stream stream{};
  stream.next_in = reinterpret_cast<Bytef*>(
      const_cast<char*>(compressed.constData() + 4));
  stream.avail_in = static_cast<uInt>(compressed.size() - 4);
  if (inflateInit(&stream) != Z_OK) {
    fail(error_message, QStringLiteral("Compressed Timeline journal inflater could not initialize."));
    return false;
  }

  QByteArray decoded;
  decoded.resize(static_cast<qsizetype>(declared_bytes));
  qsizetype decoded_bytes = 0;
  std::array<Bytef, static_cast<std::size_t>(kInflateChunkBytes)> chunk{};
  auto stream_ended = false;
  auto valid = true;
  QString failure_message;
  while (!stream_ended) {
    stream.next_out = chunk.data();
    stream.avail_out = static_cast<uInt>(chunk.size());
    const auto input_before = stream.avail_in;
    const auto result = inflate(&stream, Z_NO_FLUSH);
    const auto produced = static_cast<qsizetype>(chunk.size() - stream.avail_out);
    if (produced > 0) {
      if (static_cast<qint64>(decoded_bytes) + produced > declared_bytes) {
        valid = false;
        failure_message = QStringLiteral("Compressed Timeline journal expands beyond its declared document size.");
        break;
      }
      std::memcpy(decoded.data() + decoded_bytes, chunk.data(),
                  static_cast<std::size_t>(produced));
      decoded_bytes += produced;
    }
    if (result == Z_STREAM_END) {
      stream_ended = true;
      break;
    }
    if (result != Z_OK ||
        (produced == 0 && stream.avail_in == input_before)) {
      valid = false;
      failure_message = QStringLiteral("Compressed Timeline journal contains a truncated or invalid deflate stream.");
      break;
    }
  }
  const auto exact = valid && stream_ended && decoded_bytes == declared_bytes &&
                     stream.avail_in == 0 &&
                     stream.total_in == static_cast<uLong>(compressed.size() - 4) &&
                     stream.total_out == static_cast<uLong>(declared_bytes);
  inflateEnd(&stream);
  if (!exact) {
    fail(error_message, failure_message.isEmpty()
        ? QStringLiteral("Compressed Timeline journal did not consume exactly its declared byte stream.")
        : failure_message);
    return false;
  }
  *raw = std::move(decoded);
  if (error_message != nullptr) error_message->clear();
  return true;
}

bool decodeV2Document(const QJsonObject& entry,
                      QJsonObject* document,
                      QString* error_message) {
  if (entry.contains(QStringLiteral("document")) ||
      entry.value(QStringLiteral("documentEncoding")).toString() !=
          QString::fromLatin1(kV2Encoding)) {
    fail(error_message, QStringLiteral("Compressed Timeline journal entry has an invalid encoding envelope."));
    return false;
  }

  const auto declared_value = entry.value(QStringLiteral("documentBytes"));
  if (!declared_value.isDouble()) {
    fail(error_message, QStringLiteral("Compressed Timeline journal entry has no exact document byte count."));
    return false;
  }
  const auto declared_number = declared_value.toDouble(-1.0);
  if (!std::isfinite(declared_number) || std::floor(declared_number) != declared_number ||
      declared_number < 0.0 || declared_number > static_cast<double>(kMaximumDocumentBytes)) {
    fail(error_message, QStringLiteral("Compressed Timeline journal document exceeds the decompressed size limit."));
    return false;
  }

  const auto encoded_value = entry.value(QStringLiteral("documentCompressedBase64"));
  if (!encoded_value.isString()) {
    fail(error_message, QStringLiteral("Compressed Timeline journal entry is missing its encoded document."));
    return false;
  }
  const auto encoded = encoded_value.toString().toLatin1();
  const auto maximum_base64_size =
      4 * ((kMaximumCompressedBytes + 2) / 3);
  if (encoded.isEmpty() || encoded.size() > maximum_base64_size) {
    fail(error_message, QStringLiteral("Compressed Timeline journal payload exceeds the compressed size limit."));
    return false;
  }
  const auto base64 = QByteArray::fromBase64Encoding(
      encoded, QByteArray::AbortOnBase64DecodingErrors);
  if (base64.decodingStatus != QByteArray::Base64DecodingStatus::Ok ||
      base64.decoded.toBase64() != encoded) {
    fail(error_message, QStringLiteral("Compressed Timeline journal payload is not canonical base64."));
    return false;
  }
  const auto compressed = base64.decoded;
  const auto declared_bytes = static_cast<qint64>(declared_number);
  if (!compressedSizePairIsValid(compressed.size(), declared_bytes) ||
      compressed.size() < 4 ||
      qFromBigEndian<quint32>(compressed.constData()) != static_cast<quint32>(declared_bytes)) {
    fail(error_message, QStringLiteral("Compressed Timeline journal size metadata is invalid."));
    return false;
  }

  QByteArray raw;
  if (!boundedQCompressInflate(compressed, declared_bytes, &raw, error_message)) {
    return false;
  }
  QJsonParseError parse_error;
  const auto parsed = QJsonDocument::fromJson(raw, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !parsed.isObject() || parsed.object().isEmpty()) {
    fail(error_message, QStringLiteral("Compressed Timeline journal payload is not a nonempty JSON object."));
    return false;
  }
  *document = parsed.object();
  return true;
}

}  // namespace

QString stableDocumentHash(QJsonObject document) {
  document.remove(QStringLiteral("updatedAt"));
  return sha256Hex(compactJson(document));
}

QString entryHash(QJsonObject entry) {
  entry.remove(QStringLiteral("entryHash"));
  return sha256Hex(compactJson(entry));
}

bool compressedSizePairIsValid(const qint64 compressed_bytes,
                               const qint64 uncompressed_bytes) {
  return compressed_bytes >= 4 && compressed_bytes <= kMaximumCompressedBytes &&
      uncompressed_bytes >= 0 && uncompressed_bytes <= kMaximumDocumentBytes;
}

qsizetype maximumEncodedLineBytes() {
  const auto max_base64_bytes =
      4LL * ((static_cast<qint64>(kMaximumCompressedBytes) + 2LL) / 3LL);
  return static_cast<qsizetype>(max_base64_bytes + kMaximumEntryMetadataBytes + 2LL);
}

bool encodeCompressedV2(const QJsonObject& logical_v1_entry,
                        QJsonObject* stored_entry,
                        QString* error_message) {
  if (stored_entry == nullptr ||
      logical_v1_entry.value(QStringLiteral("schema")).toString() !=
          QString::fromLatin1(kV1Schema) ||
      !logical_v1_entry.value(QStringLiteral("document")).isObject() ||
      logical_v1_entry.value(QStringLiteral("entryId")).toString().isEmpty() ||
      logical_v1_entry.value(QStringLiteral("timelineId")).toString().isEmpty() ||
      logical_v1_entry.contains(QStringLiteral("documentEncoding")) ||
      logical_v1_entry.contains(QStringLiteral("documentBytes")) ||
      logical_v1_entry.contains(QStringLiteral("documentCompressedBase64"))) {
    fail(error_message, QStringLiteral("A compressed journal record requires a valid v1-shaped entry."));
    return false;
  }

  const auto document = logical_v1_entry.value(QStringLiteral("document")).toObject();
  const auto document_bytes = compactJson(document);
  if (document.isEmpty() || document_bytes.isEmpty() ||
      document_bytes.size() > kMaximumDocumentBytes) {
    fail(error_message, QStringLiteral("Timeline journal document exceeds the compressed codec limit."));
    return false;
  }
  if (logical_v1_entry.value(QStringLiteral("documentHash")).toString() !=
      stableDocumentHash(document)) {
    fail(error_message, QStringLiteral("Timeline journal document hash does not match its snapshot."));
    return false;
  }
  const auto previous_hash = logical_v1_entry.value(QStringLiteral("previousEntryHash")).toString();
  if (!previous_hash.isEmpty() && !validSha256(previous_hash)) {
    fail(error_message, QStringLiteral("Timeline journal predecessor hash is malformed."));
    return false;
  }

  const auto compressed = qCompress(document_bytes, 6);
  if (!compressedSizePairIsValid(compressed.size(), document_bytes.size())) {
    fail(error_message, QStringLiteral("Compressed Timeline journal record exceeds its storage limit."));
    return false;
  }

  auto stored = logical_v1_entry;
  stored.remove(QStringLiteral("entryHash"));
  stored.remove(QStringLiteral("document"));
  stored.insert(QStringLiteral("schema"), QString::fromLatin1(kV2Schema));
  stored.insert(QStringLiteral("documentEncoding"), QString::fromLatin1(kV2Encoding));
  stored.insert(QStringLiteral("documentBytes"), static_cast<double>(document_bytes.size()));
  stored.insert(QStringLiteral("documentCompressedBase64"),
                QString::fromLatin1(compressed.toBase64()));
  stored.insert(QStringLiteral("entryHash"), entryHash(stored));
  *stored_entry = std::move(stored);
  if (error_message != nullptr) error_message->clear();
  return true;
}

bool decodeStorageEntry(const QJsonObject& stored_entry,
                        QJsonObject* decoded_entry,
                        QString* error_message,
                        qint64* decoded_document_bytes) {
  if (decoded_entry == nullptr) {
    fail(error_message, QStringLiteral("A Timeline journal decoded-entry destination is required."));
    return false;
  }
  const auto schema = stored_entry.value(QStringLiteral("schema")).toString();
  if (stored_entry.value(QStringLiteral("entryId")).toString().isEmpty() ||
      stored_entry.value(QStringLiteral("timelineId")).toString().isEmpty()) {
    fail(error_message, QStringLiteral("Timeline journal entry is missing its stable identity."));
    return false;
  }
  QJsonObject document;
  qint64 document_bytes = 0;
  if (schema == QString::fromLatin1(kV1Schema)) {
    if (stored_entry.contains(QStringLiteral("documentEncoding")) ||
        stored_entry.contains(QStringLiteral("documentCompressedBase64")) ||
        !stored_entry.value(QStringLiteral("document")).isObject()) {
      fail(error_message, QStringLiteral("Legacy Timeline journal entry has an invalid document envelope."));
      return false;
    }
    document = stored_entry.value(QStringLiteral("document")).toObject();
    const auto raw_size = compactJson(document).size();
    if (document.isEmpty() || raw_size <= 0 || raw_size > kMaximumDocumentBytes) {
      fail(error_message, QStringLiteral("Legacy Timeline journal document exceeds the bounded reader limit."));
      return false;
    }
    document_bytes = static_cast<qint64>(raw_size);
  } else if (schema == QString::fromLatin1(kV2Schema)) {
    if (!decodeV2Document(stored_entry, &document, error_message)) return false;
    document_bytes = stored_entry.value(QStringLiteral("documentBytes")).toInteger(-1);
  } else {
    fail(error_message, QStringLiteral("Timeline journal entry uses an unsupported schema."));
    return false;
  }

  const auto document_hash = stored_entry.value(QStringLiteral("documentHash")).toString();
  const auto previous_hash = stored_entry.value(QStringLiteral("previousEntryHash")).toString();
  const auto stored_hash = stored_entry.value(QStringLiteral("entryHash")).toString();
  if (!validSha256(document_hash) || document_hash != stableDocumentHash(document) ||
      (!previous_hash.isEmpty() && !validSha256(previous_hash)) ||
      !validSha256(stored_hash) || stored_hash != entryHash(stored_entry)) {
    fail(error_message, QStringLiteral("Timeline journal entry or document hash validation failed."));
    return false;
  }

  auto decoded = stored_entry;
  if (schema == QString::fromLatin1(kV2Schema)) {
    decoded.remove(QStringLiteral("documentEncoding"));
    decoded.remove(QStringLiteral("documentBytes"));
    decoded.remove(QStringLiteral("documentCompressedBase64"));
    decoded.insert(QStringLiteral("document"), document);
  }
  *decoded_entry = std::move(decoded);
  if (decoded_document_bytes != nullptr) *decoded_document_bytes = document_bytes;
  if (error_message != nullptr) error_message->clear();
  return true;
}

ListingTail readListingTail(
    QIODevice* input,
    const std::atomic_bool* cancellation) {
  ListingTail result;
  if (input == nullptr || !input->isOpen() || !input->isReadable() || input->isSequential()) {
    result.error = QStringLiteral("Timeline listing requires a seekable journal.");
    return result;
  }
  constexpr qint64 block_bytes = 1024 * 1024;
  const auto maximum_line = maximumEncodedLineBytes();
  const auto maximum_read = 2 * static_cast<qint64>(maximum_line) + 2;
  auto cursor = input->size();
  QByteArray pending;
  auto candidates = 0;
  while (cursor > 0 || !pending.isEmpty()) {
    if (cancellation != nullptr && cancellation->load(std::memory_order_relaxed)) {
      result.error = QStringLiteral("Timeline listing was cancelled.");
      return result;
    }
    while (!pending.isEmpty() && (pending.back() == '\n' || pending.back() == '\r'))
      pending.chop(1);
    const auto separator = pending.lastIndexOf('\n');
    if (separator < 0 && cursor > 0) {
      const auto count = std::min({block_bytes, cursor, maximum_read - result.bytesRead});
      if (count <= 0 || pending.size() > maximum_line || !input->seek(cursor - count)) {
        result.error = QStringLiteral("Timeline listing tail exceeds its bounded read limit.");
        return result;
      }
      const auto block = input->read(count);
      if (block.size() != count) {
        result.error = QStringLiteral("Timeline listing tail could not be read completely.");
        return result;
      }
      pending.prepend(block);
      cursor -= count;
      result.bytesRead += count;
      continue;
    }
    const auto line = pending.mid(separator + 1).trimmed();
    pending.truncate(separator < 0 ? 0 : separator);
    if (line.isEmpty()) continue;
    QJsonParseError parse_error;
    const auto parsed = QJsonDocument::fromJson(line, &parse_error);
    if (line.size() <= maximum_line && parse_error.error == QJsonParseError::NoError &&
        parsed.isObject() && decodeStorageEntry(parsed.object(), &result.entry, &result.error))
      return result;
    result.entry = {};
    result.damagedTail = true;
    if (++candidates >= 2) {
      result.error = QStringLiteral("No valid recovery envelope in the bounded listing tail.");
      return result;
    }
  }
  return result;
}

ReadResult readJsonLines(
    QIODevice* input,
    const bool retain_history,
    const std::atomic_bool* cancellation) {
  ReadResult result;
  if (input == nullptr || !input->isOpen() || !input->isReadable()) {
    result.error = QStringLiteral("Timeline journal input is not open for reading.");
    result.damagedTail = true;
    return result;
  }

  const auto max_line_bytes = maximumEncodedLineBytes();
  QString previous_hash;
  while (!input->atEnd()) {
    if (cancellation != nullptr && cancellation->load(std::memory_order_relaxed)) {
      result.entries.clear();
      result.cancelled = true;
      result.error = QStringLiteral("Timeline journal validation was cancelled during shutdown.");
      return result;
    }
    const auto line = input->readLine(max_line_bytes + 1);
    if (line.isEmpty()) {
      if (input->atEnd()) break;
      result.error = QStringLiteral("Timeline journal read stopped before end of file.");
      result.damagedTail = true;
      break;
    }
    result.bytesRead += static_cast<qint64>(line.size());
    if (line.size() > max_line_bytes) {
      result.error = QStringLiteral("Timeline journal record exceeds the bounded line size.");
      result.damagedTail = true;
      break;
    }
    const auto trimmed = line.trimmed();
    if (trimmed.isEmpty()) continue;

    QJsonParseError parse_error;
    const auto parsed = QJsonDocument::fromJson(trimmed, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !parsed.isObject()) {
      result.error = QStringLiteral("Timeline journal contains an invalid JSON record.");
      result.damagedTail = true;
      break;
    }
    const auto stored = parsed.object();
    QJsonObject decoded;
    auto decoded_document_bytes = qint64{0};
    if (!decodeStorageEntry(
            stored, &decoded, &result.error, &decoded_document_bytes)) {
      result.damagedTail = true;
      break;
    }
    if (stored.value(QStringLiteral("previousEntryHash")).toString() != previous_hash) {
      result.error = QStringLiteral("Timeline journal predecessor chain is discontinuous.");
      result.damagedTail = true;
      break;
    }

    previous_hash = stored.value(QStringLiteral("entryHash")).toString();
    ++result.validEntryCount;
    result.lastEntryHash = previous_hash;
    result.lastEntryBytes = static_cast<qint64>(line.size());
    result.lastDocumentBytes = decoded_document_bytes;
    if (!retain_history) result.entries.clear();
    result.entries.push_back(std::move(decoded));
  }
  return result;
}

}  // namespace photo_editor::desktop::timeline_edit_journal_codec
