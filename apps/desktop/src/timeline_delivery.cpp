#include "timeline_delivery.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUrl>
#include <QVector>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <limits>
#include <utility>

#ifdef Q_OS_MACOS
#include <sys/stdio.h>
#elif defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace photo_editor::desktop::timeline_delivery {
namespace {

constexpr qint64 kCopyBufferBytes = 256 * 1024;
constexpr quint64 kMaximumCompoundDocumentBytes = 64ULL * 1024ULL * 1024ULL;
constexpr quint64 kZip32MaxValue = 0xfffffffeULL;
constexpr quint64 kZip32Sentinel = 0xffffffffULL;
constexpr quint16 kZip64ExtraFieldId = 0x0001U;
constexpr quint64 kZip16MaxValue = 0xfffeULL;
constexpr quint64 kZip16Sentinel = 0xffffULL;
constexpr qint64 kMaximumZipEntries = std::numeric_limits<int>::max() - 3LL;
constexpr int kMaximumDocumentDepth = 64;
constexpr qsizetype kMaximumProcessOutputBytes = 1024 * 1024;
constexpr int kMaximumSignalStreamsPerType = 8;
constexpr int kMaximumSignalScanMilliseconds = 60 * 60 * 1000;

struct PackageEntry final {
  QString archive_path;
  QString source_path;
  QString canonical_source_path;
  QByteArray data;
  QByteArray sha256;
  quint32 crc32 = 0;
  quint64 size = 0;
  QString source_label;
  QString source_path_hash;
  QStringList roles;
  bool from_source = false;
};

struct WrittenEntry final {
  QByteArray name;
  quint32 crc32 = 0;
  quint64 size = 0;
  quint64 local_header_offset = 0;
};

struct ProcessResult final {
  bool started = false;
  bool canceled = false;
  bool timed_out = false;
  bool output_truncated = false;
  int exit_code = -1;
  QByteArray standard_output;
  QByteArray standard_error;
  QString error;
};

QVariantMap failure(const QString& message, const QString& status = QStringLiteral("failed")) {
  return {{QStringLiteral("success"), false},
          {QStringLiteral("status"), status},
          {QStringLiteral("message"), message}};
}

bool is_canceled(const std::shared_ptr<std::atomic_bool>& cancellation) {
  return cancellation && cancellation->load(std::memory_order_relaxed);
}

QString canonical_source_identity(const QString& path) {
  QFileInfo info(path);
  auto canonical = info.canonicalFilePath();
  if (canonical.isEmpty()) {
    canonical = info.absoluteFilePath();
  }
  return QDir::cleanPath(canonical);
}

QString local_path_from_value(const QString& value, const QString& base_directory) {
  const auto trimmed = value.trimmed();
  if (trimmed.isEmpty()) {
    return {};
  }
  const QUrl url(trimmed);
  QString path = url.isLocalFile() ? url.toLocalFile() : trimmed;
  const QFileInfo info(path);
  if (info.isRelative()) {
    path = QDir(base_directory).absoluteFilePath(path);
  }
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString safe_file_name(QString value) {
  value = value.normalized(QString::NormalizationForm_C);
  value.replace(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f/\\\\:]")),
                QStringLiteral("_"));
  value = value.trimmed();
  while (value.startsWith(QLatin1Char('.'))) {
    value.remove(0, 1);
  }
  value = value.left(120).trimmed();
  return value.isEmpty() ? QStringLiteral("asset") : value;
}

bool safe_zip_path(const QString& value) {
  if (value.isEmpty() || value.startsWith(QLatin1Char('/')) || value.contains(QLatin1Char('\\')) ||
      value.contains(QChar::Null)) {
    return false;
  }
  const auto parts = value.split(QLatin1Char('/'), Qt::KeepEmptyParts);
  return std::all_of(parts.cbegin(), parts.cend(), [](const QString& part) {
    return !part.isEmpty() && part != QStringLiteral(".") && part != QStringLiteral("..");
  });
}

quint32 crc32_update(quint32 crc, const char* data, qsizetype size) {
  static const auto table = [] {
    std::array<quint32, 256> values{};
    for (quint32 index = 0; index < values.size(); ++index) {
      auto value = index;
      for (int bit = 0; bit < 8; ++bit) {
        value = (value & 1U) != 0U ? (value >> 1U) ^ 0xedb88320U : value >> 1U;
      }
      values[index] = value;
    }
    return values;
  }();
  for (qsizetype index = 0; index < size; ++index) {
    const auto byte = static_cast<unsigned char>(data[index]);
    const auto table_index = static_cast<quint8>((crc ^ byte) & 0xffU);
    crc = table[table_index] ^ (crc >> 8U);
  }
  return crc;
}

void append_u16(QByteArray* bytes, quint16 value) {
  bytes->append(static_cast<char>(value & 0xffU));
  bytes->append(static_cast<char>((value >> 8U) & 0xffU));
}

void append_u32(QByteArray* bytes, quint32 value) {
  append_u16(bytes, static_cast<quint16>(value & 0xffffU));
  append_u16(bytes, static_cast<quint16>((value >> 16U) & 0xffffU));
}

void append_u64(QByteArray* bytes, quint64 value) {
  append_u32(bytes, static_cast<quint32>(value & 0xffffffffULL));
  append_u32(bytes, static_cast<quint32>((value >> 32U) & 0xffffffffULL));
}

bool write_bytes(QIODevice* output, const QByteArray& bytes, quint64* written) {
  if (output == nullptr || written == nullptr) {
    return false;
  }
  const auto count = output->write(bytes);
  if (count != bytes.size()) {
    return false;
  }
  const auto byte_count = static_cast<quint64>(count);
  if (byte_count > std::numeric_limits<quint64>::max() - *written) {
    return false;
  }
  *written += byte_count;
  return true;
}

bool write_local_header(QIODevice* output, const PackageEntry& entry, quint64* written,
                        WrittenEntry* record, quint64 zip32_max_value) {
  const auto size_uses_zip64 = entry.size > zip32_max_value;
  if (output == nullptr || record == nullptr || !safe_zip_path(entry.archive_path) ||
      entry.archive_path.toUtf8().size() > std::numeric_limits<quint16>::max()) {
    return false;
  }
  const auto name = entry.archive_path.toUtf8();
  QByteArray extra;
  if (size_uses_zip64) {
    append_u16(&extra, kZip64ExtraFieldId);
    append_u16(&extra, 16U);
    append_u64(&extra, entry.size);
    append_u64(&extra, entry.size);
  }
  QByteArray header;
  header.reserve(30 + name.size() + extra.size());
  append_u32(&header, 0x04034b50U);
  append_u16(&header, size_uses_zip64 ? 45U : 20U);
  append_u16(&header, 0x0800U);  // UTF-8 names
  append_u16(&header, 0U);       // Stored without compression.
  append_u16(&header, 0U);       // DOS time
  append_u16(&header, 0x0021U);  // 1980-01-01
  append_u32(&header, entry.crc32);
  const auto size_field = size_uses_zip64 ? static_cast<quint32>(kZip32Sentinel)
                                         : static_cast<quint32>(entry.size);
  append_u32(&header, size_field);
  append_u32(&header, size_field);
  append_u16(&header, static_cast<quint16>(name.size()));
  append_u16(&header, static_cast<quint16>(extra.size()));
  header.append(name);
  header.append(extra);
  record->name = name;
  record->crc32 = entry.crc32;
  record->size = entry.size;
  record->local_header_offset = *written;
  return write_bytes(output, header, written);
}

bool write_source_entry(QIODevice* output, const PackageEntry& entry, quint64* written,
                        const std::shared_ptr<std::atomic_bool>& cancellation,
                        QString* error) {
  if (QFileInfo(entry.source_path).isSymLink() ||
      canonical_source_identity(entry.source_path) != entry.canonical_source_path) {
    if (error != nullptr) {
      *error = QStringLiteral("A collected source changed identity before it could be copied: %1")
                   .arg(entry.source_label);
    }
    return false;
  }
  QFile source(entry.source_path);
  if (!source.open(QIODevice::ReadOnly)) {
    if (error != nullptr) {
      *error = QStringLiteral("A collected file became unreadable: %1").arg(entry.source_label);
    }
    return false;
  }
  if (QFileInfo(entry.source_path).isSymLink() ||
      canonical_source_identity(entry.source_path) != entry.canonical_source_path) {
    if (error != nullptr) {
      *error = QStringLiteral("A collected source changed identity while it was being opened: %1")
                   .arg(entry.source_label);
    }
    return false;
  }
  QCryptographicHash digest(QCryptographicHash::Sha256);
  quint32 crc = 0xffffffffU;
  quint64 copied = 0;
  while (!source.atEnd()) {
    if (is_canceled(cancellation)) {
      if (error != nullptr) {
        *error = QStringLiteral("Project collection was canceled.");
      }
      return false;
    }
    const auto chunk = source.read(kCopyBufferBytes);
    if (chunk.isEmpty() && source.error() != QFileDevice::NoError) {
      if (error != nullptr) {
        *error = QStringLiteral("A collected file could not be read: %1").arg(entry.source_label);
      }
      return false;
    }
    digest.addData(chunk);
    crc = crc32_update(crc, chunk.constData(), chunk.size());
    const auto count = output->write(chunk);
    if (count != chunk.size()) {
      if (error != nullptr) {
        *error = QStringLiteral("The project archive could not be written.");
      }
      return false;
    }
    copied += static_cast<quint64>(count);
    const auto byte_count = static_cast<quint64>(count);
    if (byte_count > std::numeric_limits<quint64>::max() - *written) {
      if (error != nullptr) {
        *error = QStringLiteral("The project archive exceeds the ZIP64 size limit.");
      }
      return false;
    }
    *written += byte_count;
  }
  if (QFileInfo(entry.source_path).isSymLink() ||
      canonical_source_identity(entry.source_path) != entry.canonical_source_path ||
      copied != entry.size || digest.result() != entry.sha256 ||
      (crc ^ 0xffffffffU) != entry.crc32) {
    if (error != nullptr) {
      *error = QStringLiteral("A source file changed while it was being collected: %1")
                   .arg(entry.source_label);
    }
    return false;
  }
  return true;
}

bool write_data_entry(QIODevice* output, const PackageEntry& entry, quint64* written,
                      QString* error) {
  if (static_cast<quint64>(entry.data.size()) != entry.size) {
    if (error != nullptr) {
      *error = QStringLiteral("An internal package file has inconsistent size metadata.");
    }
    return false;
  }
  const auto count = output->write(entry.data);
  if (count != entry.data.size()) {
    if (error != nullptr) {
      *error = QStringLiteral("The project archive could not be written.");
    }
    return false;
  }
  const auto byte_count = static_cast<quint64>(count);
  if (byte_count > std::numeric_limits<quint64>::max() - *written) {
    if (error != nullptr) {
      *error = QStringLiteral("The project archive exceeds the ZIP64 size limit.");
    }
    return false;
  }
  *written += byte_count;
  return true;
}

bool write_central_directory(QIODevice* output, const QVector<WrittenEntry>& records,
                             quint64* written, QString* error,
                             quint64 zip32_max_value_for_testing) {
  if (output == nullptr || written == nullptr || records.size() > kMaximumZipEntries) {
    if (error != nullptr) {
      *error = QStringLiteral("The package contains too many files for this platform.");
    }
    return false;
  }
  const auto directory_offset = *written;
  bool has_zip64_entry = false;
  for (const auto& record : records) {
    const auto size_uses_zip64 = record.size > zip32_max_value_for_testing;
    const auto offset_uses_zip64 = record.local_header_offset > zip32_max_value_for_testing;
    const auto extra_data_size = (size_uses_zip64 ? 16 : 0) + (offset_uses_zip64 ? 8 : 0);
    const auto extra_size = extra_data_size > 0 ? extra_data_size + 4 : 0;
    has_zip64_entry = has_zip64_entry || size_uses_zip64 || offset_uses_zip64;
    QByteArray extra;
    if (extra_data_size > 0) {
      append_u16(&extra, kZip64ExtraFieldId);
      append_u16(&extra, static_cast<quint16>(extra_data_size));
      if (size_uses_zip64) {
        append_u64(&extra, record.size);
        append_u64(&extra, record.size);
      }
      if (offset_uses_zip64) {
        append_u64(&extra, record.local_header_offset);
      }
    }
    QByteArray header;
    header.reserve(46 + record.name.size() + extra.size());
    append_u32(&header, 0x02014b50U);
    append_u16(&header, 20U);
    append_u16(&header, extra_data_size > 0 ? 45U : 20U);
    append_u16(&header, 0x0800U);
    append_u16(&header, 0U);
    append_u16(&header, 0U);
    append_u16(&header, 0x0021U);
    append_u32(&header, record.crc32);
    const auto size_field = size_uses_zip64 ? static_cast<quint32>(kZip32Sentinel)
                                           : static_cast<quint32>(record.size);
    append_u32(&header, size_field);
    append_u32(&header, size_field);
    append_u16(&header, static_cast<quint16>(record.name.size()));
    append_u16(&header, static_cast<quint16>(extra_size));
    append_u16(&header, 0U);
    append_u16(&header, 0U);
    append_u16(&header, 0U);
    append_u32(&header, 0U);
    append_u32(&header, offset_uses_zip64 ? static_cast<quint32>(kZip32Sentinel)
                                         : static_cast<quint32>(record.local_header_offset));
    header.append(record.name);
    header.append(extra);
    if (!write_bytes(output, header, written)) {
      if (error != nullptr) {
        *error = QStringLiteral("The ZIP directory could not be written.");
      }
      return false;
    }
  }
  const auto directory_size = *written - directory_offset;
  const auto count_uses_zip64 = static_cast<quint64>(records.size()) > kZip16MaxValue;
  const auto directory_size_uses_zip64 = directory_size > zip32_max_value_for_testing;
  const auto directory_offset_uses_zip64 = directory_offset > zip32_max_value_for_testing;
  const auto zip64_required = has_zip64_entry || count_uses_zip64 ||
                              directory_size_uses_zip64 || directory_offset_uses_zip64;
  if (zip64_required) {
    const auto zip64_record_offset = *written;
    QByteArray zip64_footer;
    append_u32(&zip64_footer, 0x06064b50U);
    append_u64(&zip64_footer, 44U);
    append_u16(&zip64_footer, 45U);
    append_u16(&zip64_footer, 45U);
    append_u32(&zip64_footer, 0U);
    append_u32(&zip64_footer, 0U);
    append_u64(&zip64_footer, static_cast<quint64>(records.size()));
    append_u64(&zip64_footer, static_cast<quint64>(records.size()));
    append_u64(&zip64_footer, directory_size);
    append_u64(&zip64_footer, directory_offset);
    append_u32(&zip64_footer, 0x07064b50U);
    append_u32(&zip64_footer, 0U);
    append_u64(&zip64_footer, zip64_record_offset);
    append_u32(&zip64_footer, 1U);
    if (!write_bytes(output, zip64_footer, written)) {
      if (error != nullptr) {
        *error = QStringLiteral("The ZIP64 end records could not be written.");
      }
      return false;
    }
  }
  QByteArray footer;
  append_u32(&footer, 0x06054b50U);
  append_u16(&footer, 0U);
  append_u16(&footer, 0U);
  append_u16(&footer, count_uses_zip64 ? static_cast<quint16>(kZip16Sentinel)
                                      : static_cast<quint16>(records.size()));
  append_u16(&footer, count_uses_zip64 ? static_cast<quint16>(kZip16Sentinel)
                                      : static_cast<quint16>(records.size()));
  append_u32(&footer, directory_size_uses_zip64 ? static_cast<quint32>(kZip32Sentinel)
                                                 : static_cast<quint32>(directory_size));
  append_u32(&footer, directory_offset_uses_zip64 ? static_cast<quint32>(kZip32Sentinel)
                                                   : static_cast<quint32>(directory_offset));
  append_u16(&footer, 0U);
  if (!write_bytes(output, footer, written)) {
    if (error != nullptr) {
      *error = QStringLiteral("The ZIP directory footer could not be written.");
    }
    return false;
  }
  return true;
}

bool publish_file_no_replace(
    const QString& staged_path,
    const QString& destination_path,
    QString* error) {
#ifdef Q_OS_MACOS
  const auto staged_bytes = QFile::encodeName(staged_path);
  const auto destination_bytes = QFile::encodeName(destination_path);
  if (::renamex_np(staged_bytes.constData(), destination_bytes.constData(), RENAME_EXCL) == 0) {
    return true;
  }
  const auto exclusive_rename_error = errno;
  if (exclusive_rename_error == EEXIST) {
    if (error != nullptr) {
      *error = QStringLiteral("The destination appeared while the archive was being written; it was left untouched.");
    }
    return false;
  }
#elif defined(Q_OS_WIN)
  if (::MoveFileW(reinterpret_cast<LPCWSTR>(staged_path.utf16()),
                  reinterpret_cast<LPCWSTR>(destination_path.utf16()))) {
    return true;
  }
  const auto move_error = ::GetLastError();
  if (move_error == ERROR_FILE_EXISTS || move_error == ERROR_ALREADY_EXISTS) {
    if (error != nullptr) {
      *error = QStringLiteral("The destination appeared while the archive was being written; it was left untouched.");
    }
    return false;
  }
#endif

  // Hard-link publication is also an atomic create-if-absent operation. It
  // is the portable fallback for filesystems where exclusive rename is not
  // implemented; unsupported media still fails closed without replacing.
  std::error_code hardlink_error;
  std::filesystem::create_hard_link(
      std::filesystem::path(QFile::encodeName(staged_path).toStdString()),
      std::filesystem::path(QFile::encodeName(destination_path).toStdString()),
      hardlink_error);
  if (!hardlink_error) {
    QFile::remove(staged_path);
    return true;
  }
  if (QFileInfo(destination_path).exists() || QFileInfo(destination_path).isSymLink()) {
    if (error != nullptr) {
      *error = QStringLiteral("The destination appeared while the archive was being written; it was left untouched.");
    }
  } else if (error != nullptr) {
    auto detail = QString::fromLocal8Bit(hardlink_error.message().c_str());
#ifdef Q_OS_MACOS
    detail.prepend(QStringLiteral("Exclusive rename failed (%1); hard-link fallback failed: ")
                       .arg(QString::fromLocal8Bit(std::strerror(exclusive_rename_error))));
#elif defined(Q_OS_WIN)
    detail.prepend(QStringLiteral("No-replace move failed with Windows error %1; hard-link fallback failed: ")
                       .arg(move_error));
#endif
    *error = QStringLiteral("The complete archive could not be published without replacing another file: %1")
                 .arg(detail);
  }
  return false;
}

bool add_data_entry(QVector<PackageEntry>* entries, const QString& archive_path,
                    const QByteArray& data, const QString& source_label = QString()) {
  if (entries == nullptr || !safe_zip_path(archive_path)) {
    return false;
  }
  const auto found = std::find_if(entries->cbegin(), entries->cend(),
                                  [&archive_path](const PackageEntry& entry) {
                                    return entry.archive_path == archive_path;
                                  });
  if (found != entries->cend()) {
    return false;
  }
  const auto digest = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
  const auto crc = crc32_update(0xffffffffU, data.constData(), data.size()) ^ 0xffffffffU;
  entries->push_back(PackageEntry{archive_path, {}, {}, data, digest, crc,
                                  static_cast<quint64>(data.size()), source_label, {}, {}, false});
  return true;
}

QString resource_archive_path(const QByteArray& sha256, const QString& source_path) {
  auto extension = QFileInfo(source_path).suffix().toLower();
  if (!QRegularExpression(QStringLiteral("^[a-z0-9]{1,16}$"))
           .match(extension).hasMatch()) {
    extension = QStringLiteral("bin");
  }
  return QStringLiteral("Media/%1.%2")
      .arg(QString::fromLatin1(sha256.toHex()), extension);
}

QString document_archive_path(const QString& source_path) {
  const auto canonical = QFileInfo(source_path).canonicalFilePath();
  const auto identity = canonical.isEmpty() ? QFileInfo(source_path).absoluteFilePath() : canonical;
  const auto digest = QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex();
  return QStringLiteral("Compound-%1.json").arg(QString::fromLatin1(digest));
}

class ProjectCollector final {
 public:
  ProjectCollector(QString root_source_path,
                   std::shared_ptr<std::atomic_bool> cancellation)
      : root_source_path_(std::move(root_source_path)),
        cancellation_(std::move(cancellation)) {}

  bool collect(QJsonObject* root, QString* error) {
    if (root == nullptr) {
      if (error != nullptr) {
        *error = QStringLiteral("The timeline document is unavailable.");
      }
      return false;
    }
    const auto root_path = local_path_from_value(root_source_path_, QDir::currentPath());
    root_directory_ = root_path.isEmpty() ? QDir::currentPath() : QFileInfo(root_path).absolutePath();
    if (!root_path.isEmpty() && QFileInfo::exists(root_path)) {
      auto root_identity = QFileInfo(root_path).canonicalFilePath();
      if (root_identity.isEmpty()) {
        root_identity = QFileInfo(root_path).absoluteFilePath();
      }
      document_destinations_.insert(root_identity, QStringLiteral("Project.json"));
    }
    QJsonValue rewritten(*root);
    if (!rewriteValue(&rewritten, root_directory_, QStringLiteral("Project.json"), 0, error) ||
        !rewritten.isObject()) {
      return false;
    }
    *root = rewritten.toObject();
    return !is_canceled(cancellation_);
  }

  [[nodiscard]] const QVector<PackageEntry>& entries() const { return entries_; }
  [[nodiscard]] const QStringList& warnings() const { return warnings_; }
  [[nodiscard]] const QStringList& font_families() const { return font_families_; }

 private:
  bool rewriteValue(QJsonValue* value, const QString& source_directory,
                    const QString& logical_document_path, int depth, QString* error) {
    if (value == nullptr || depth > kMaximumDocumentDepth) {
      if (error != nullptr) {
        *error = QStringLiteral("The timeline contains nested documents beyond the supported depth.");
      }
      return false;
    }
    if (is_canceled(cancellation_)) {
      if (error != nullptr) {
        *error = QStringLiteral("Project collection was canceled.");
      }
      return false;
    }
    if (value->isArray()) {
      auto array = value->toArray();
      for (qsizetype index = 0; index < array.size(); ++index) {
        auto child = array.at(index);
        if (!rewriteValue(&child, source_directory, logical_document_path, depth + 1, error)) {
          return false;
        }
        array.replace(index, child);
      }
      *value = array;
      return true;
    }
    if (!value->isObject()) {
      return true;
    }

    auto object = value->toObject();
    const auto keys = object.keys();
    for (const auto& key : keys) {
      const auto raw = object.value(key);
      if ((key == QStringLiteral("titleFontFamily") || key == QStringLiteral("fontFamily")) &&
          raw.isString()) {
        const auto family = raw.toString().trimmed();
        if (!family.isEmpty() && !font_families_.contains(family, Qt::CaseInsensitive)) {
          font_families_.push_back(family);
        }
      }
      if (key == QStringLiteral("compoundDocumentPath") && raw.isString()) {
        const auto child_source_path = local_path_from_value(raw.toString(), source_directory);
        if (child_source_path.isEmpty()) {
          object.insert(key, QString{});
          warnings_.push_back(QStringLiteral("An empty compound sequence reference was omitted."));
          continue;
        }
        auto canonical = QFileInfo(child_source_path).canonicalFilePath();
        if (canonical.isEmpty()) {
          canonical = QFileInfo(child_source_path).absoluteFilePath();
        }
        auto archive_path = document_destinations_.value(canonical);
        if (archive_path.isEmpty()) {
          QFileInfo source_info(child_source_path);
          if (source_info.isSymLink() || !source_info.isFile() || !source_info.isReadable()) {
            if (error != nullptr) {
              *error = QStringLiteral("A referenced compound sequence is missing, unreadable, or a symbolic link: %1")
                           .arg(source_info.fileName());
            }
            return false;
          }
          archive_path = document_archive_path(child_source_path);
          document_destinations_.insert(canonical, archive_path);
          QFile input(child_source_path);
          if (!input.open(QIODevice::ReadOnly)) {
            if (error != nullptr) {
              *error = QStringLiteral("A referenced compound sequence could not be read: %1")
                           .arg(source_info.fileName());
            }
            return false;
          }
          const QFileInfo opened_source_info(child_source_path);
          if (opened_source_info.isSymLink() ||
              canonical_source_identity(child_source_path) != canonical) {
            if (error != nullptr) {
              *error = QStringLiteral(
                           "A referenced compound sequence changed identity while it was being opened: %1")
                           .arg(source_info.fileName());
            }
            return false;
          }
          const auto remaining_bytes = kMaximumCompoundDocumentBytes - compound_document_bytes_;
          const auto reported_size = input.size();
          if (reported_size < 0 || static_cast<quint64>(reported_size) > remaining_bytes) {
            if (error != nullptr) {
              *error = QStringLiteral(
                           "Referenced compound sequences exceed the 64 MiB package limit: %1")
                           .arg(source_info.fileName());
            }
            return false;
          }
          QByteArray bytes;
          bytes.reserve(static_cast<qsizetype>(reported_size));
          while (!input.atEnd()) {
            if (is_canceled(cancellation_)) {
              if (error != nullptr) {
                *error = QStringLiteral("Project collection was canceled.");
              }
              return false;
            }
            const auto remaining_with_probe =
                remaining_bytes - static_cast<quint64>(bytes.size()) + 1ULL;
            const auto request_size = static_cast<qint64>(qMin(
                static_cast<quint64>(kCopyBufferBytes), remaining_with_probe));
            const auto chunk = input.read(request_size);
            if (chunk.isEmpty()) {
              if (input.error() != QFileDevice::NoError) {
                if (error != nullptr) {
                  *error = QStringLiteral("A referenced compound sequence could not be read: %1")
                               .arg(source_info.fileName());
                }
                return false;
              }
              break;
            }
            bytes.append(chunk);
            if (static_cast<quint64>(bytes.size()) > remaining_bytes) {
              if (error != nullptr) {
                *error = QStringLiteral(
                             "Referenced compound sequences exceed the 64 MiB package limit: %1")
                             .arg(source_info.fileName());
              }
              return false;
            }
          }
          const QFileInfo after_read(child_source_path);
          if (after_read.isSymLink() || bytes.size() != reported_size ||
              after_read.size() != reported_size ||
              canonical_source_identity(child_source_path) != canonical) {
            if (error != nullptr) {
              *error = QStringLiteral(
                           "A referenced compound sequence changed while it was being read: %1")
                           .arg(source_info.fileName());
            }
            return false;
          }
          compound_document_bytes_ += static_cast<quint64>(bytes.size());
          QJsonParseError parse_error;
          auto child_document = QJsonDocument::fromJson(bytes, &parse_error);
          if (!child_document.isObject()) {
            if (error != nullptr) {
              *error = QStringLiteral("A referenced compound sequence is not a valid project document: %1")
                           .arg(source_info.fileName());
            }
            return false;
          }
          QJsonValue child_value(child_document.object());
          const auto child_directory = source_info.absolutePath();
          if (!rewriteValue(&child_value, child_directory, archive_path, depth + 1, error) ||
              !child_value.isObject()) {
            return false;
          }
          const auto child_object = child_value.toObject();
          const auto child_bytes = QJsonDocument(child_object).toJson(QJsonDocument::Indented);
          if (!add_data_entry(&entries_, archive_path, child_bytes, source_info.fileName())) {
            if (error != nullptr) {
              *error = QStringLiteral("Two compound documents produced the same package name.");
            }
            return false;
          }
        }
        object.insert(key, QFileInfo(archive_path).fileName());
        continue;
      }

      if (key == QStringLiteral("proxyPath") || key == QStringLiteral("posterSource") ||
          key == QStringLiteral("stabilizationTransformPath") ||
          key == QStringLiteral("stabilizationRenderPath") ||
          key == QStringLiteral("stabilizationPreviewPath")) {
        if (raw.isString() && !raw.toString().trimmed().isEmpty()) {
          warnings_.push_back(QStringLiteral("Generated preview or analysis files are regenerated after opening."));
        }
        object.insert(key, QString{});
        if (key == QStringLiteral("proxyPath")) {
          object.insert(QStringLiteral("proxyEnabled"), false);
        }
        continue;
      }

      const auto path_like = key == QStringLiteral("filePath") ||
                             key == QStringLiteral("colorLutPath") ||
                             key == QStringLiteral("compoundRenderPath") ||
                             key.endsWith(QStringLiteral("Path"), Qt::CaseInsensitive) ||
                             key.compare(QStringLiteral("path"), Qt::CaseInsensitive) == 0;
      if (path_like && raw.isString()) {
        const auto raw_path = raw.toString().trimmed();
        if (raw_path.isEmpty()) {
          continue;
        }
        const auto source_path = local_path_from_value(raw_path, source_directory);
        if (source_path.isEmpty()) {
          if (error != nullptr) {
            *error = QStringLiteral("A referenced project asset uses an unsupported path: %1").arg(key);
          }
          return false;
        }
        const auto required = key == QStringLiteral("filePath") ||
                              (key == QStringLiteral("colorLutPath") &&
                               object.value(QStringLiteral("colorLutEnabled")).toBool(false));
        const QFileInfo source_info(source_path);
        if (!source_info.isFile() || source_info.isSymLink() || !source_info.isReadable()) {
          if (required) {
            if (error != nullptr) {
              *error = QStringLiteral("A referenced media file or LUT is missing, unreadable, or a symbolic link: %1")
                           .arg(source_info.fileName().isEmpty() ? key : source_info.fileName());
            }
            return false;
          }
          object.insert(key, QString{});
          warnings_.push_back(QStringLiteral("An optional generated or linked asset was unavailable: %1")
                                  .arg(source_info.fileName().isEmpty() ? key : source_info.fileName()));
          continue;
        }
        QString archive_path;
        if (!add_source_file(source_path, key, &archive_path, error)) {
          return false;
        }
        object.insert(key, archive_path);
        continue;
      }

      auto child = raw;
      if (!rewriteValue(&child, source_directory, logical_document_path, depth + 1, error)) {
        return false;
      }
      object.insert(key, child);
    }
    *value = object;
    return true;
  }

  bool add_source_file(const QString& source_path, const QString& role,
                       QString* archive_path, QString* error) {
    QFileInfo info(source_path);
    if (info.isSymLink() || !info.isFile() || !info.isReadable()) {
      if (error != nullptr) {
        *error = QStringLiteral("A referenced file is not a readable regular file: %1")
                     .arg(info.fileName());
      }
      return false;
    }
    const auto canonical = canonical_source_identity(source_path);
    const QFileInfo source_info_after_identity(source_path);
    if (source_info_after_identity.isSymLink() ||
        canonical_source_identity(source_path) != canonical) {
      if (error != nullptr) {
        *error = QStringLiteral("A referenced file changed identity while it was being collected: %1")
                     .arg(info.fileName());
      }
      return false;
    }
    const auto existing = source_destinations_.constFind(canonical);
    if (existing != source_destinations_.cend()) {
      *archive_path = existing.value();
      const auto index = source_entry_indices_.value(canonical, -1);
      if (index >= 0 && !entries_[index].roles.contains(role)) {
        entries_[index].roles.push_back(role);
      }
      return true;
    }

    QFile file(source_path);
    if (!file.open(QIODevice::ReadOnly)) {
      if (error != nullptr) {
        *error = QStringLiteral("A referenced file could not be read: %1").arg(info.fileName());
      }
      return false;
    }
    const QFileInfo opened_source_info(source_path);
    if (opened_source_info.isSymLink() ||
        canonical_source_identity(source_path) != canonical) {
      if (error != nullptr) {
        *error = QStringLiteral("A referenced file changed identity while it was being opened: %1")
                     .arg(info.fileName());
      }
      return false;
    }
    QCryptographicHash digest(QCryptographicHash::Sha256);
    quint32 crc = 0xffffffffU;
    quint64 size = 0;
    while (!file.atEnd()) {
      if (is_canceled(cancellation_)) {
        if (error != nullptr) {
          *error = QStringLiteral("Project collection was canceled.");
        }
        return false;
      }
      const auto chunk = file.read(kCopyBufferBytes);
      if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
        if (error != nullptr) {
          *error = QStringLiteral("A referenced file could not be read: %1").arg(info.fileName());
        }
        return false;
      }
      const auto chunk_size = static_cast<quint64>(chunk.size());
      if (chunk_size > std::numeric_limits<quint64>::max() - size) {
        if (error != nullptr) {
          *error = QStringLiteral("A source file exceeds the ZIP64 size limit: %1")
                       .arg(info.fileName());
        }
        return false;
      }
      size += chunk_size;
      digest.addData(chunk);
      crc = crc32_update(crc, chunk.constData(), chunk.size());
    }
    const QFileInfo source_info_after_read(source_path);
    if (source_info_after_read.isSymLink() ||
        canonical_source_identity(source_path) != canonical) {
      if (error != nullptr) {
        *error = QStringLiteral("A referenced file changed identity while it was being read: %1")
                     .arg(info.fileName());
      }
      return false;
    }
    const auto hash = digest.result();
    const auto package_path = resource_archive_path(hash, source_path);
    if (!safe_zip_path(package_path)) {
      if (error != nullptr) {
        *error = QStringLiteral("A referenced file could not be assigned a safe archive name.");
      }
      return false;
    }
    const auto same_package_path = std::find_if(
        entries_.cbegin(), entries_.cend(), [&package_path](const PackageEntry& entry) {
          return entry.archive_path == package_path;
        });
    if (same_package_path != entries_.cend()) {
      if (same_package_path->sha256 != hash) {
        if (error != nullptr) {
          *error = QStringLiteral("Two referenced files collided on the generated archive path.");
        }
        return false;
      }
      const auto index = static_cast<int>(std::distance(entries_.cbegin(), same_package_path));
      if (!entries_[index].roles.contains(role)) {
        entries_[index].roles.push_back(role);
      }
      source_destinations_.insert(canonical, package_path);
      source_entry_indices_.insert(canonical, index);
      *archive_path = package_path;
      return true;
    }
    const auto source_label = safe_file_name(info.fileName());
    const auto path_hash = QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha256);
    entries_.push_back(PackageEntry{package_path, source_path, canonical, {}, hash,
                                    crc ^ 0xffffffffU, size, source_label,
                                    QString::fromLatin1(path_hash.toHex()), {role}, true});
    const auto index = static_cast<int>(entries_.size() - 1);
    source_destinations_.insert(canonical, package_path);
    source_entry_indices_.insert(canonical, index);
    *archive_path = package_path;
    return true;
  }

  QString root_source_path_;
  QString root_directory_;
  std::shared_ptr<std::atomic_bool> cancellation_;
  QVector<PackageEntry> entries_;
  QHash<QString, QString> source_destinations_;
  QHash<QString, int> source_entry_indices_;
  QHash<QString, QString> document_destinations_;
  quint64 compound_document_bytes_ = 0;
  QStringList warnings_;
  QStringList font_families_;
};

QVariantMap make_manifest(const QString& project_name, const QVector<PackageEntry>& entries,
                          const QStringList& fonts, const QStringList& warnings,
                          const QByteArray& document_bytes) {
  QJsonArray files;
  files.push_back(QJsonObject{{QStringLiteral("path"), QStringLiteral("Project.json")},
                              {QStringLiteral("role"), QStringLiteral("timeline-document")},
                              {QStringLiteral("size"), document_bytes.size()},
                              {QStringLiteral("sha256"), QString::fromLatin1(
                                   QCryptographicHash::hash(document_bytes, QCryptographicHash::Sha256).toHex())}});
  for (const auto& entry : entries) {
    QJsonObject record{{QStringLiteral("path"), entry.archive_path},
                       {QStringLiteral("size"), static_cast<double>(entry.size)},
                       {QStringLiteral("sha256"), QString::fromLatin1(entry.sha256.toHex())}};
    if (!entry.source_label.isEmpty()) {
      record.insert(QStringLiteral("sourceLabel"), entry.source_label);
      record.insert(QStringLiteral("sourcePathIdentitySha256"), entry.source_path_hash);
    }
    if (entry.roles.size() == 1) {
      record.insert(QStringLiteral("role"), entry.roles.first());
    } else if (!entry.roles.isEmpty()) {
      QJsonArray roles;
      for (const auto& role : entry.roles) {
        roles.push_back(role);
      }
      record.insert(QStringLiteral("roles"), roles);
    } else {
      record.insert(QStringLiteral("role"), QStringLiteral("compound-document"));
    }
    files.push_back(record);
  }

  QJsonArray font_items;
  for (const auto& family : fonts) {
    font_items.push_back(QJsonObject{
        {QStringLiteral("family"), family},
        {QStringLiteral("status"), QStringLiteral("not-embedded")},
        {QStringLiteral("reason"), QStringLiteral("Font files are not copied because font licenses and installation requirements vary.")}});
  }
  QJsonArray warning_items;
  QSet<QString> unique_warnings;
  for (const auto& warning : warnings) {
    if (!unique_warnings.contains(warning)) {
      unique_warnings.insert(warning);
      warning_items.push_back(warning);
    }
  }
  const QJsonObject manifest{
      {QStringLiteral("format"), QStringLiteral("looksy-portable-timeline")},
      {QStringLiteral("schemaVersion"), 1},
      {QStringLiteral("projectName"), project_name},
      {QStringLiteral("archivePathPolicy"), QStringLiteral("All ZIP entry paths are generated relative ASCII names with no parent traversal; original Unicode source labels remain in the manifest.")},
      {QStringLiteral("files"), files},
      {QStringLiteral("fontDependencies"), font_items},
      {QStringLiteral("limitations"), QJsonArray{
           QStringLiteral("System fonts are listed but not embedded."),
           QStringLiteral("Playback proxies and generated preview, waveform, and analysis caches are not included; they are regenerated on demand."),
           QStringLiteral("Timeline documents and referenced media/LUT files are included when readable.")}},
      {QStringLiteral("warnings"), warning_items}};
  return {{QStringLiteral("json"), QJsonDocument(manifest).toJson(QJsonDocument::Indented)},
          {QStringLiteral("fileCount"), files.size()},
          {QStringLiteral("warningCount"), warning_items.size()},
          {QStringLiteral("fontDependencyCount"), font_items.size()}};
}

QVariantMap write_zip(const QString& destination_path, const QVector<PackageEntry>& entries,
                      const QByteArray& project_bytes, const QByteArray& manifest_bytes,
                      const std::shared_ptr<std::atomic_bool>& cancellation,
                      quint64 zip32_max_value_for_testing,
                      const std::function<void()>& before_publish_for_testing) {
  QFileInfo destination_info(destination_path);
  if (destination_path.trimmed().isEmpty() || destination_info.isSymLink() || destination_info.exists()) {
    return failure(QStringLiteral("Choose a new ZIP path. Existing files and symbolic links are left untouched."));
  }
  if (static_cast<qint64>(entries.size()) + 3 > kMaximumZipEntries) {
    return failure(QStringLiteral("The project has too many files for this platform."));
  }
  QVector<PackageEntry> all_entries = entries;
  if (!add_data_entry(&all_entries, QStringLiteral("Project.json"), project_bytes,
                      QStringLiteral("Timeline project")) ||
      !add_data_entry(&all_entries, QStringLiteral("manifest.json"), manifest_bytes,
                      QStringLiteral("Checksum manifest")) ||
      !add_data_entry(&all_entries, QStringLiteral("README.txt"),
                      QByteArray("Looksy portable timeline project\n\nExtract this ZIP, then open Project.json in Looksy.\nReview manifest.json for SHA-256 checksums and dependencies that still require local installation.\n"),
                      QStringLiteral("Package instructions"))) {
    return failure(QStringLiteral("The package contains duplicate or unsafe archive paths."));
  }

  const auto absolute_destination = destination_info.absoluteFilePath();
  const auto staging_template = QDir(destination_info.absolutePath()).filePath(
      destination_info.fileName() + QStringLiteral(".XXXXXX"));
  QTemporaryFile output(staging_template);
  output.setAutoRemove(true);
  if (!output.open()) {
    return failure(QStringLiteral("A temporary archive could not be opened beside the destination: %1")
                       .arg(output.errorString()));
  }
  quint64 written = 0;
  QVector<WrittenEntry> records;
  records.reserve(all_entries.size());
  for (const auto& entry : all_entries) {
    if (is_canceled(cancellation)) {
      output.close();
      return failure(QStringLiteral("Project collection was canceled."), QStringLiteral("canceled"));
    }
    WrittenEntry record;
    if (!write_local_header(&output, entry, &written, &record, zip32_max_value_for_testing)) {
      output.close();
      return failure(QStringLiteral("A safe ZIP file header could not be written."));
    }
    QString error;
    const auto wrote = entry.from_source
                           ? write_source_entry(&output, entry, &written, cancellation, &error)
                           : write_data_entry(&output, entry, &written, &error);
    if (!wrote) {
      output.close();
      return failure(error.isEmpty() ? QStringLiteral("The archive file could not be written.") : error,
                     is_canceled(cancellation) ? QStringLiteral("canceled") : QStringLiteral("failed"));
    }
    records.push_back(record);
  }
  QString directory_error;
  if (!write_central_directory(&output, records, &written, &directory_error,
                               zip32_max_value_for_testing)) {
    output.close();
    return failure(directory_error);
  }
  if (is_canceled(cancellation)) {
    output.close();
    return failure(QStringLiteral("Project collection was canceled."), QStringLiteral("canceled"));
  }
  if (!output.flush()) {
    const auto error = output.errorString();
    output.close();
    return failure(QStringLiteral("The project archive could not be flushed: %1").arg(error));
  }
  output.close();
  if (is_canceled(cancellation)) {
    return failure(QStringLiteral("Project collection was canceled."), QStringLiteral("canceled"));
  }
  if (before_publish_for_testing) {
    before_publish_for_testing();
  }
  if (is_canceled(cancellation)) {
    return failure(QStringLiteral("Project collection was canceled."), QStringLiteral("canceled"));
  }

  QString publish_error;
  if (!publish_file_no_replace(output.fileName(), absolute_destination, &publish_error)) {
    return failure(publish_error);
  }
  return {{QStringLiteral("success"), true},
          {QStringLiteral("status"), QStringLiteral("complete")},
          {QStringLiteral("path"), absolute_destination},
          {QStringLiteral("size"), static_cast<double>(QFileInfo(absolute_destination).size())},
          {QStringLiteral("fileCount"), records.size()},
          {QStringLiteral("message"), QStringLiteral("Portable project package created. Extract it, then open Project.json in Looksy.")}};
}

ProcessResult run_process(const QString& executable, const QStringList& arguments,
                          const std::shared_ptr<std::atomic_bool>& cancellation,
                          int timeout_msecs) {
  ProcessResult result;
  QProcess process;
  process.setProgram(executable);
  process.setArguments(arguments);
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start();
  if (!process.waitForStarted(3000)) {
    result.error = QStringLiteral("Could not start %1.").arg(QFileInfo(executable).fileName());
    return result;
  }
  result.started = true;
  QElapsedTimer elapsed;
  elapsed.start();
  const auto append_bounded = [&result](QByteArray* destination, const QByteArray& chunk) {
    const auto remaining = qMax<qsizetype>(0, kMaximumProcessOutputBytes - destination->size());
    if (chunk.size() > remaining) {
      result.output_truncated = true;
    }
    destination->append(chunk.left(remaining));
  };
  while (process.state() != QProcess::NotRunning) {
    if (is_canceled(cancellation)) {
      result.canceled = true;
      process.kill();
      process.waitForFinished(1000);
      break;
    }
    if (timeout_msecs > 0 && elapsed.elapsed() > timeout_msecs) {
      result.timed_out = true;
      process.kill();
      process.waitForFinished(1000);
      break;
    }
    if (process.waitForFinished(100)) {
      break;
    }
    const auto out_chunk = process.readAllStandardOutput();
    const auto err_chunk = process.readAllStandardError();
    append_bounded(&result.standard_output, out_chunk);
    append_bounded(&result.standard_error, err_chunk);
  }
  append_bounded(&result.standard_output, process.readAllStandardOutput());
  append_bounded(&result.standard_error, process.readAllStandardError());
  result.exit_code = process.exitCode();
  return result;
}

QVariantMap probe_file(const QString& output_path,
                       const std::shared_ptr<std::atomic_bool>& cancellation,
                       bool* has_video, bool* has_audio) {
  const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  if (ffprobe.isEmpty()) {
    return failure(QStringLiteral("ffprobe is not installed or is not available on PATH."));
  }
  const auto process = run_process(
      ffprobe,
      {QStringLiteral("-v"), QStringLiteral("error"),
       QStringLiteral("-show_entries"),
       QStringLiteral("format=format_name,duration,size:stream=index,codec_type,codec_name,width,height,pix_fmt,sample_rate,channels,channel_layout,duration"),
       QStringLiteral("-show_format"), QStringLiteral("-show_streams"),
       QStringLiteral("-of"), QStringLiteral("json"), output_path},
      cancellation, 30000);
  if (process.canceled) {
    return failure(QStringLiteral("Output inspection was canceled."), QStringLiteral("canceled"));
  }
  if (process.timed_out) {
    return failure(QStringLiteral("ffprobe did not finish within 30 seconds."));
  }
  if (!process.started || process.exit_code != 0) {
    const auto details = QString::fromLocal8Bit(process.standard_error).trimmed();
    return failure(details.isEmpty() ? QStringLiteral("ffprobe could not read the selected file.") : details);
  }
  if (process.output_truncated) {
    return failure(QStringLiteral("ffprobe stream information exceeded the 1 MiB inspection limit."));
  }
  QJsonParseError parse_error;
  const auto probe = QJsonDocument::fromJson(process.standard_output, &parse_error);
  if (!probe.isObject()) {
    return failure(QStringLiteral("ffprobe returned malformed stream information."));
  }
  const auto stream_values = probe.object().value(QStringLiteral("streams")).toArray();
  if (stream_values.isEmpty()) {
    return failure(QStringLiteral("No media streams were found in the selected file."));
  }
  QVariantList streams;
  double duration_seconds = probe.object().value(QStringLiteral("format")).toObject()
                                .value(QStringLiteral("duration")).toString().toDouble();
  *has_video = false;
  *has_audio = false;
  for (const auto& stream_value : stream_values) {
    const auto stream = stream_value.toObject();
    const auto type = stream.value(QStringLiteral("codec_type")).toString();
    if (type == QStringLiteral("video")) {
      *has_video = true;
    } else if (type == QStringLiteral("audio")) {
      *has_audio = true;
    }
    const auto stream_duration = stream.value(QStringLiteral("duration")).toString().toDouble();
    duration_seconds = qMax(duration_seconds, stream_duration);
    QVariantMap record{{QStringLiteral("index"), stream.value(QStringLiteral("index")).toInt()},
                       {QStringLiteral("type"), type},
                       {QStringLiteral("codec"), stream.value(QStringLiteral("codec_name")).toString()}};
    if (type == QStringLiteral("video")) {
      record.insert(QStringLiteral("width"), stream.value(QStringLiteral("width")).toInt());
      record.insert(QStringLiteral("height"), stream.value(QStringLiteral("height")).toInt());
      record.insert(QStringLiteral("pixelFormat"), stream.value(QStringLiteral("pix_fmt")).toString());
    } else if (type == QStringLiteral("audio")) {
      record.insert(QStringLiteral("sampleRate"), stream.value(QStringLiteral("sample_rate")).toString().toInt());
      record.insert(QStringLiteral("channels"), stream.value(QStringLiteral("channels")).toInt());
      record.insert(QStringLiteral("channelLayout"), stream.value(QStringLiteral("channel_layout")).toString());
    }
    streams.push_back(record);
  }
  return {{QStringLiteral("success"), true},
          {QStringLiteral("durationSeconds"), duration_seconds},
          {QStringLiteral("format"), probe.object().value(QStringLiteral("format")).toObject()
                                          .value(QStringLiteral("format_name")).toString()},
          {QStringLiteral("streams"), streams},
          {QStringLiteral("hasVideo"), *has_video},
          {QStringLiteral("hasAudio"), *has_audio}};
}

QVariantList black_spans(const QByteArray& log, double total_duration) {
  static const QRegularExpression expression(
      QStringLiteral("black_start:([0-9]+(?:\\.[0-9]+)?)\\s+black_end:([0-9]+(?:\\.[0-9]+)?)\\s+black_duration:([0-9]+(?:\\.[0-9]+)?)"));
  QVariantList spans;
  const auto text = QString::fromLocal8Bit(log);
  auto iterator = expression.globalMatch(text);
  while (iterator.hasNext()) {
    const auto match = iterator.next();
    spans.push_back(QVariantMap{{QStringLiteral("startSeconds"), match.captured(1).toDouble()},
                                {QStringLiteral("endSeconds"), match.captured(2).toDouble()},
                                {QStringLiteral("durationSeconds"), match.captured(3).toDouble()}});
  }
  if (!spans.isEmpty() && total_duration > 0.0 &&
      text.contains(QStringLiteral("black_start:")) &&
      !text.contains(QStringLiteral("black_end:"))) {
    spans.push_back(QVariantMap{{QStringLiteral("startSeconds"), total_duration},
                                {QStringLiteral("endSeconds"), total_duration},
                                {QStringLiteral("durationSeconds"), 0.0}});
  }
  return spans;
}

QVariantList silence_spans(const QByteArray& log, double total_duration) {
  static const QRegularExpression start_expression(
      QStringLiteral("silence_start:\\s*([0-9]+(?:\\.[0-9]+)?)"));
  static const QRegularExpression end_expression(
      QStringLiteral("silence_end:\\s*([0-9]+(?:\\.[0-9]+)?)\\s*\\|\\s*silence_duration:\\s*([0-9]+(?:\\.[0-9]+)?)"));
  const auto text = QString::fromLocal8Bit(log);
  QVariantList spans;
  double pending_start = -1.0;
  auto starts = start_expression.globalMatch(text);
  auto ends = end_expression.globalMatch(text);
  QVector<double> start_values;
  QVector<QPair<double, double>> end_values;
  while (starts.hasNext()) {
    start_values.push_back(starts.next().captured(1).toDouble());
  }
  while (ends.hasNext()) {
    const auto match = ends.next();
    end_values.push_back({match.captured(1).toDouble(), match.captured(2).toDouble()});
  }
  for (const auto start : start_values) {
    pending_start = start;
    const auto found = std::find_if(end_values.cbegin(), end_values.cend(),
                                    [start](const QPair<double, double>& end) {
                                      return end.first >= start;
                                    });
    if (found != end_values.cend()) {
      spans.push_back(QVariantMap{{QStringLiteral("startSeconds"), start},
                                  {QStringLiteral("endSeconds"), found->first},
                                  {QStringLiteral("durationSeconds"), found->second}});
      pending_start = -1.0;
    }
  }
  if (pending_start >= 0.0 && total_duration > pending_start) {
    spans.push_back(QVariantMap{{QStringLiteral("startSeconds"), pending_start},
                                {QStringLiteral("endSeconds"), total_duration},
                                {QStringLiteral("durationSeconds"), total_duration - pending_start}});
  }
  return spans;
}

QString concise_decode_message(const ProcessResult& process) {
  const auto detail = QString::fromLocal8Bit(process.standard_error).trimmed();
  if (process.canceled) {
    return QStringLiteral("Output inspection was canceled.");
  }
  if (process.timed_out) {
    return QStringLiteral("Media decoding exceeded the one hour safety limit.");
  }
  if (process.exit_code == 0) {
    return QStringLiteral("All selected audio and video streams decoded without a reported error.");
  }
  if (detail.isEmpty()) {
    return QStringLiteral("FFmpeg could not fully decode the selected output.");
  }
  return detail.left(500);
}

}  // namespace

QString resolveDocumentResourcePath(const QString& path, const QString& document_path) {
  const auto trimmed = path.trimmed();
  if (trimmed.isEmpty()) {
    return {};
  }
  const QUrl url(trimmed);
  if (url.isLocalFile()) {
    return QDir::cleanPath(QFileInfo(url.toLocalFile()).absoluteFilePath());
  }
  const QFileInfo info(trimmed);
  if (!info.isRelative()) {
    return QDir::cleanPath(info.absoluteFilePath());
  }
  return QDir::cleanPath(
      QDir(QFileInfo(document_path).absolutePath()).absoluteFilePath(trimmed));
}

QVariantMap createPortableProjectArchive(
    const QString& destination_path,
    const QByteArray& document_json,
    const QString& project_name,
    const QString& source_document_path,
    const std::shared_ptr<std::atomic_bool>& cancellation,
    quint64 zip32_max_value_for_testing,
    const std::function<void()>& before_publish_for_testing) {
  zip32_max_value_for_testing = qMin(zip32_max_value_for_testing, kZip32MaxValue);
  if (is_canceled(cancellation)) {
    return failure(QStringLiteral("Project collection was canceled."), QStringLiteral("canceled"));
  }
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(document_json, &parse_error);
  if (!document.isObject()) {
    return failure(QStringLiteral("The current timeline could not be serialized as a project document."));
  }
  auto project_object = document.object();
  ProjectCollector collector(source_document_path, cancellation);
  QString collection_error;
  if (!collector.collect(&project_object, &collection_error)) {
    return failure(collection_error.isEmpty() ? QStringLiteral("Project collection was canceled.") : collection_error,
                   is_canceled(cancellation) ? QStringLiteral("canceled") : QStringLiteral("failed"));
  }
  const auto project_bytes = QJsonDocument(project_object).toJson(QJsonDocument::Indented);
  auto manifest_result = make_manifest(project_name, collector.entries(), collector.font_families(),
                                       collector.warnings(), project_bytes);
  const auto manifest_bytes = manifest_result.value(QStringLiteral("json")).toByteArray();
  auto result = write_zip(destination_path, collector.entries(), project_bytes, manifest_bytes,
                          cancellation, zip32_max_value_for_testing,
                          before_publish_for_testing);
  if (!result.value(QStringLiteral("success")).toBool()) {
    return result;
  }
  result.insert(QStringLiteral("warningCount"), manifest_result.value(QStringLiteral("warningCount")));
  result.insert(QStringLiteral("fontDependencyCount"),
                manifest_result.value(QStringLiteral("fontDependencyCount")));
  result.insert(QStringLiteral("manifestPath"), QStringLiteral("manifest.json"));
  return result;
}

QVariantMap inspectRenderedOutput(
    const QString& output_path,
    const std::shared_ptr<std::atomic_bool>& cancellation) {
  if (is_canceled(cancellation)) {
    return failure(QStringLiteral("Output inspection was canceled."), QStringLiteral("canceled"));
  }
  const QFileInfo info(output_path);
  if (!info.isFile() || info.isSymLink() || !info.isReadable()) {
    return failure(QStringLiteral("Choose a readable rendered media file."));
  }
  bool has_video = false;
  bool has_audio = false;
  const auto probe = probe_file(info.absoluteFilePath(), cancellation, &has_video, &has_audio);
  if (!probe.value(QStringLiteral("success")).toBool()) {
    return probe;
  }
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty()) {
    return failure(QStringLiteral("ffmpeg is not installed or is not available on PATH."));
  }
  if (!has_video && !has_audio) {
    return failure(QStringLiteral("The file has no audio or video stream to decode."));
  }
  QStringList decode_arguments{QStringLiteral("-hide_banner"), QStringLiteral("-nostats"),
                               QStringLiteral("-v"), QStringLiteral("error"),
                               QStringLiteral("-xerror"), QStringLiteral("-i"), info.absoluteFilePath()};
  if (has_video) {
    decode_arguments << QStringLiteral("-map") << QStringLiteral("0:v?");
  }
  if (has_audio) {
    decode_arguments << QStringLiteral("-map") << QStringLiteral("0:a?");
  }
  decode_arguments << QStringLiteral("-f") << QStringLiteral("null") << QStringLiteral("-");
  const auto decoded = run_process(ffmpeg, decode_arguments, cancellation, 60 * 60 * 1000);
  if (decoded.canceled) {
    return failure(QStringLiteral("Output inspection was canceled."), QStringLiteral("canceled"));
  }
  if (!decoded.started) {
    return failure(decoded.error);
  }

  QVariantList black_segments;
  QVariantList silence_segments;
  bool signal_scan_complete = true;
  QString signal_scan_warning;
  const auto append_warning = [&signal_scan_warning](const QString& warning) {
    if (warning.isEmpty()) {
      return;
    }
    if (!signal_scan_warning.isEmpty()) {
      signal_scan_warning += QLatin1Char(' ');
    }
    signal_scan_warning += warning;
  };
  const auto annotate_spans = [](QVariantList* spans, int stream_index,
                                 const QString& stream_label) {
    if (spans == nullptr) {
      return;
    }
    for (auto& value : *spans) {
      auto span = value.toMap();
      span.insert(QStringLiteral("streamIndex"), stream_index);
      span.insert(QStringLiteral("streamLabel"), stream_label);
      value = span;
    }
  };
  QVector<int> video_stream_indices;
  QVector<int> audio_stream_indices;
  const auto stream_values = probe.value(QStringLiteral("streams")).toList();
  for (const auto& stream_value : stream_values) {
    const auto stream = stream_value.toMap();
    bool stream_index_ok = false;
    const auto stream_index = stream.value(QStringLiteral("index")).toInt(&stream_index_ok);
    if (!stream_index_ok || stream_index < 0) {
      continue;
    }
    const auto type = stream.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("video")) {
      video_stream_indices.push_back(stream_index);
    } else if (type == QStringLiteral("audio")) {
      audio_stream_indices.push_back(stream_index);
    }
  }
  if (video_stream_indices.size() > kMaximumSignalStreamsPerType) {
    signal_scan_complete = false;
    append_warning(QStringLiteral("Only the first %1 of %2 video streams were scanned.")
                       .arg(kMaximumSignalStreamsPerType)
                       .arg(video_stream_indices.size()));
  }
  if (audio_stream_indices.size() > kMaximumSignalStreamsPerType) {
    signal_scan_complete = false;
    append_warning(QStringLiteral("Only the first %1 of %2 audio streams were scanned.")
                       .arg(kMaximumSignalStreamsPerType)
                       .arg(audio_stream_indices.size()));
  }

  QElapsedTimer signal_scan_timer;
  signal_scan_timer.start();
  auto video_streams_scanned = 0;
  auto audio_streams_scanned = 0;
  const auto probed_duration = probe.value(QStringLiteral("durationSeconds")).toDouble();
  const auto scan_streams = [&](const QVector<int>& stream_indices, bool video) -> bool {
    const auto scan_count = qMin<qsizetype>(
        stream_indices.size(), static_cast<qsizetype>(kMaximumSignalStreamsPerType));
    for (qsizetype index = 0; index < scan_count; ++index) {
      if (is_canceled(cancellation)) {
        return false;
      }
      const auto remaining_ms = kMaximumSignalScanMilliseconds - signal_scan_timer.elapsed();
      if (remaining_ms <= 0) {
        signal_scan_complete = false;
        append_warning(QStringLiteral("The one-hour total signal-scan limit was reached; remaining streams were not scanned."));
        return true;
      }
      const auto stream_index = stream_indices.at(index);
      const auto stream_label = QStringLiteral("%1 stream %2")
                                    .arg(video ? QStringLiteral("Video") : QStringLiteral("Audio"))
                                    .arg(stream_index);
      QStringList arguments{QStringLiteral("-hide_banner"), QStringLiteral("-nostats"),
                            QStringLiteral("-v"), QStringLiteral("info"),
                            QStringLiteral("-i"), info.absoluteFilePath(),
                            QStringLiteral("-map"), QStringLiteral("0:%1").arg(stream_index)};
      if (video) {
        arguments << QStringLiteral("-vf") << QStringLiteral("blackdetect=d=0.10:pix_th=0.10")
                  << QStringLiteral("-an");
      } else {
        arguments << QStringLiteral("-af") << QStringLiteral("silencedetect=n=-60dB:d=0.50")
                  << QStringLiteral("-vn");
      }
      arguments << QStringLiteral("-f") << QStringLiteral("null") << QStringLiteral("-");
      const auto scan = run_process(ffmpeg, arguments, cancellation,
                                    static_cast<int>(remaining_ms));
      if (scan.canceled) {
        return false;
      }
      if (!scan.started || scan.timed_out || scan.exit_code != 0 || scan.output_truncated) {
        signal_scan_complete = false;
        const auto reason = scan.timed_out
                                ? QStringLiteral("timed out")
                                : scan.output_truncated
                                      ? QStringLiteral("exceeded the 1 MiB diagnostic limit")
                                      : QStringLiteral("did not complete");
        append_warning(QStringLiteral("%1 %2; no complete signal conclusion is available for that stream.")
                           .arg(stream_label, reason));
        if (scan.timed_out || signal_scan_timer.elapsed() >= kMaximumSignalScanMilliseconds) {
          append_warning(QStringLiteral("The one-hour total signal-scan limit was reached; remaining streams were not scanned."));
          return true;
        }
        continue;
      }

      auto spans = video ? black_spans(scan.standard_error, probed_duration)
                         : silence_spans(scan.standard_error, probed_duration);
      annotate_spans(&spans, stream_index, stream_label);
      if (video) {
        black_segments.append(spans);
        ++video_streams_scanned;
      } else {
        silence_segments.append(spans);
        ++audio_streams_scanned;
      }
    }
    return true;
  };
  if (!scan_streams(video_stream_indices, true) ||
      !scan_streams(audio_stream_indices, false)) {
    return failure(QStringLiteral("Output inspection was canceled."), QStringLiteral("canceled"));
  }

  const auto decode_ok = decoded.exit_code == 0 && !decoded.timed_out;
  const auto duration = probed_duration;
  const auto duration_text = duration > 0.0
                                 ? QStringLiteral("%1 s").arg(duration, 0, 'f', 2)
                                 : QStringLiteral("unknown duration");
  QString message = QStringLiteral("%1; %2.")
                        .arg(decode_ok ? QStringLiteral("Decode completed")
                                       : QStringLiteral("Decode reported errors"),
                             duration_text);
  if (signal_scan_complete) {
    message += QStringLiteral(" %1 video-black span%2 and %3 audio-silence span%4 flagged for review.")
                   .arg(black_segments.size())
                   .arg(black_segments.size() == 1 ? QString{} : QStringLiteral("s"))
                   .arg(silence_segments.size())
                   .arg(silence_segments.size() == 1 ? QString{} : QStringLiteral("s"));
  } else {
    message += QStringLiteral(" Signal scan incomplete; listed spans cover only successfully scanned streams.");
  }
  // Black frames and quiet intervals are observations for editorial review, not automatic failures.
  QVariantList findings;
  if (!decode_ok) {
    findings.push_back(QVariantMap{
        {QStringLiteral("severity"), QStringLiteral("error")},
        {QStringLiteral("label"), QStringLiteral("Decode error")},
        {QStringLiteral("message"), concise_decode_message(decoded)}});
  }
  if (!black_segments.isEmpty()) {
    findings.push_back(QVariantMap{
        {QStringLiteral("severity"), QStringLiteral("review")},
        {QStringLiteral("label"), QStringLiteral("Black video")},
        {QStringLiteral("message"), QStringLiteral("%1 black span%2 detected. Review each range; intentional fades and black cards are valid.")
                                         .arg(black_segments.size())
                                         .arg(black_segments.size() == 1 ? QString{} : QStringLiteral("s"))}});
  }
  if (!silence_segments.isEmpty()) {
    findings.push_back(QVariantMap{
        {QStringLiteral("severity"), QStringLiteral("review")},
        {QStringLiteral("label"), QStringLiteral("Audio silence")},
        {QStringLiteral("message"), QStringLiteral("%1 quiet span%2 detected below -60 dBFS for at least 0.5 seconds. Review each range against the program intent.")
                                         .arg(silence_segments.size())
                                         .arg(silence_segments.size() == 1 ? QString{} : QStringLiteral("s"))}});
  }
  if (!signal_scan_complete) {
    findings.push_back(QVariantMap{
        {QStringLiteral("severity"), QStringLiteral("warning")},
        {QStringLiteral("label"), QStringLiteral("Signal scan incomplete")},
        {QStringLiteral("message"), signal_scan_warning.isEmpty()
                                         ? QStringLiteral("Black and silence scan did not complete; no span conclusion is available.")
                                         : signal_scan_warning}});
  }
  if (findings.isEmpty()) {
    findings.push_back(QVariantMap{
        {QStringLiteral("severity"), QStringLiteral("success")},
        {QStringLiteral("label"), QStringLiteral("Decode complete")},
        {QStringLiteral("message"), QStringLiteral("All selected audio and video streams decoded without a reported error. No black or quiet spans met the review thresholds.")}});
  }
  auto result = probe;
  result.insert(QStringLiteral("status"), decode_ok ? QStringLiteral("complete") : QStringLiteral("review"));
  result.insert(QStringLiteral("path"), info.absoluteFilePath());
  result.insert(QStringLiteral("fileSize"), info.size());
  result.insert(QStringLiteral("decodeOk"), decode_ok);
  result.insert(QStringLiteral("decodeMessage"), concise_decode_message(decoded));
  result.insert(QStringLiteral("decodeDiagnostics"), QString::fromLocal8Bit(decoded.standard_error));
  result.insert(QStringLiteral("blackSegments"), black_segments);
  result.insert(QStringLiteral("silenceSegments"), silence_segments);
  result.insert(QStringLiteral("signalScanComplete"), signal_scan_complete);
  result.insert(QStringLiteral("signalScanWarning"), signal_scan_warning);
  result.insert(QStringLiteral("videoStreamsScanned"), video_streams_scanned);
  result.insert(QStringLiteral("audioStreamsScanned"), audio_streams_scanned);
  result.insert(QStringLiteral("findings"), findings);
  result.insert(QStringLiteral("message"), message);
  result.insert(QStringLiteral("success"), decode_ok);
  result.insert(QStringLiteral("signalsAreAdvisory"), true);
  result.insert(QStringLiteral("blackThreshold"), QStringLiteral("blackdetect d=0.10s, pix_th=0.10"));
  result.insert(QStringLiteral("silenceThreshold"), QStringLiteral("silencedetect below -60 dBFS for 0.50s"));
  return result;
}

}  // namespace photo_editor::desktop::timeline_delivery
