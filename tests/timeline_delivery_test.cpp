#include "timeline_delivery.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <thread>

using photo_editor::desktop::timeline_delivery::createPortableProjectArchive;
using photo_editor::desktop::timeline_delivery::inspectRenderedOutput;
using photo_editor::desktop::timeline_delivery::resolveDocumentResourcePath;

namespace {

bool check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    return false;
  }
  return true;
}

quint16 read_u16(const QByteArray& bytes, qsizetype offset) {
  const auto first = static_cast<quint8>(bytes.at(offset));
  const auto second = static_cast<quint8>(bytes.at(offset + 1));
  return static_cast<quint16>(first | (static_cast<quint16>(second) << 8U));
}

quint32 read_u32(const QByteArray& bytes, qsizetype offset) {
  const auto low = read_u16(bytes, offset);
  const auto high = read_u16(bytes, offset + 2);
  return static_cast<quint32>(low) | (static_cast<quint32>(high) << 16U);
}

quint64 read_u64(const QByteArray& bytes, qsizetype offset) {
  return static_cast<quint64>(read_u32(bytes, offset)) |
         (static_cast<quint64>(read_u32(bytes, offset + 4)) << 32U);
}

quint32 crc32_value(const QByteArray& bytes) {
  quint32 crc = 0xffffffffU;
  for (const auto byte : bytes) {
    crc ^= static_cast<unsigned char>(byte);
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 1U) != 0U ? (crc >> 1U) ^ 0xedb88320U : crc >> 1U;
    }
  }
  return crc ^ 0xffffffffU;
}

bool parse_zip64_extra(const QByteArray& bytes, qsizetype extra_offset, quint16 extra_length,
                       bool need_uncompressed_size, bool need_compressed_size,
                       bool need_local_offset, quint64* uncompressed_size,
                       quint64* compressed_size, quint64* local_offset,
                       bool* saw_zip64_extra) {
  const auto end_offset = extra_offset + static_cast<qsizetype>(extra_length);
  if (extra_offset < 0 || end_offset > bytes.size()) {
    return false;
  }
  for (auto cursor = extra_offset; cursor + 4 <= end_offset;) {
    const auto field_id = read_u16(bytes, cursor);
    const auto field_length = read_u16(bytes, cursor + 2);
    cursor += 4;
    const auto field_size = static_cast<qsizetype>(field_length);
    if (cursor + field_size > end_offset) {
      return false;
    }
    if (field_id == 0x0001U) {
      if (saw_zip64_extra != nullptr) {
        *saw_zip64_extra = true;
      }
      const auto field_end = cursor + field_length;
      if (need_uncompressed_size) {
        if (cursor + 8 > field_end || uncompressed_size == nullptr) return false;
        *uncompressed_size = read_u64(bytes, cursor);
        cursor += 8;
      }
      if (need_compressed_size) {
        if (cursor + 8 > field_end || compressed_size == nullptr) return false;
        *compressed_size = read_u64(bytes, cursor);
        cursor += 8;
      }
      if (need_local_offset) {
        if (cursor + 8 > field_end || local_offset == nullptr) return false;
        *local_offset = read_u64(bytes, cursor);
      }
      return true;
    }
    cursor += field_size;
  }
  return !need_uncompressed_size && !need_compressed_size && !need_local_offset;
}

bool unzip_stored_entries(const QString& path, QHash<QString, QByteArray>* files,
                          bool* saw_zip64_entry_extra = nullptr,
                          bool* saw_zip64_end_records = nullptr) {
  if (files == nullptr) return false;
  QFile archive(path);
  if (!archive.open(QIODevice::ReadOnly)) return false;
  const auto bytes = archive.readAll();
  if (bytes.size() < 22) return false;
  const auto minimum_offset = qMax<qsizetype>(0, bytes.size() - 65557);
  qsizetype footer_offset = -1;
  for (qsizetype offset = bytes.size() - 22; offset >= minimum_offset; --offset) {
    if (read_u32(bytes, offset) == 0x06054b50U) {
      footer_offset = offset;
      break;
    }
  }
  if (footer_offset < 0) return false;

  quint64 entry_count = read_u16(bytes, footer_offset + 10);
  quint64 directory_offset = read_u32(bytes, footer_offset + 16);
  quint64 directory_size = read_u32(bytes, footer_offset + 12);
  if (footer_offset >= 20 && read_u32(bytes, footer_offset - 20) == 0x07064b50U) {
    const auto zip64_offset = read_u64(bytes, footer_offset - 12);
    if (zip64_offset > static_cast<quint64>(bytes.size()) ||
        read_u32(bytes, static_cast<qsizetype>(zip64_offset)) != 0x06064b50U) {
      return false;
    }
    const auto record_offset = static_cast<qsizetype>(zip64_offset);
    if (read_u64(bytes, record_offset + 4) < 44U) return false;
    entry_count = read_u64(bytes, record_offset + 32);
    directory_size = read_u64(bytes, record_offset + 40);
    directory_offset = read_u64(bytes, record_offset + 48);
    if (saw_zip64_end_records != nullptr) *saw_zip64_end_records = true;
  }
  if (entry_count > static_cast<quint64>(std::numeric_limits<int>::max()) ||
      directory_offset > static_cast<quint64>(bytes.size()) ||
      directory_size > static_cast<quint64>(bytes.size()) - directory_offset) {
    return false;
  }
  auto cursor = static_cast<qsizetype>(directory_offset);
  for (quint64 index = 0; index < entry_count; ++index) {
    if (cursor < 0 || cursor + 46 > bytes.size() ||
        read_u32(bytes, cursor) != 0x02014b50U) return false;
    const auto method = read_u16(bytes, cursor + 10);
    const auto flags = read_u16(bytes, cursor + 8);
    const auto expected_crc = read_u32(bytes, cursor + 16);
    const auto compressed_size_field = read_u32(bytes, cursor + 20);
    const auto uncompressed_size_field = read_u32(bytes, cursor + 24);
    const auto name_length = read_u16(bytes, cursor + 28);
    const auto extra_length = read_u16(bytes, cursor + 30);
    const auto comment_length = read_u16(bytes, cursor + 32);
    const auto local_offset_field = read_u32(bytes, cursor + 42);
    const auto extra_offset = cursor + 46 + static_cast<qsizetype>(name_length);
    if (extra_offset + static_cast<qsizetype>(extra_length) +
            static_cast<qsizetype>(comment_length) > bytes.size()) return false;
    quint64 compressed_size = compressed_size_field;
    quint64 uncompressed_size = uncompressed_size_field;
    quint64 local_offset = local_offset_field;
    const auto need_uncompressed = uncompressed_size_field == 0xffffffffU;
    const auto need_compressed = compressed_size_field == 0xffffffffU;
    const auto need_offset = local_offset_field == 0xffffffffU;
    if ((need_uncompressed || need_compressed || need_offset) &&
        !parse_zip64_extra(bytes, extra_offset, extra_length, need_uncompressed,
                           need_compressed, need_offset, &uncompressed_size,
                           &compressed_size, &local_offset, saw_zip64_entry_extra)) {
      return false;
    }
    if (local_offset > static_cast<quint64>(bytes.size())) return false;
    const auto local_offset_index = static_cast<qsizetype>(local_offset);
    const auto name = QString::fromUtf8(bytes.constData() + cursor + 46, name_length);
    if (method != 0 || flags != 0x0800U || compressed_size != uncompressed_size ||
        read_u32(bytes, local_offset_index) != 0x04034b50U) {
      return false;
    }
    const auto local_name_length = read_u16(bytes, local_offset_index + 26);
    const auto local_extra_length = read_u16(bytes, local_offset_index + 28);
    const auto local_flags = read_u16(bytes, local_offset_index + 6);
    const auto local_compressed_field = read_u32(bytes, local_offset_index + 18);
    const auto local_uncompressed_field = read_u32(bytes, local_offset_index + 22);
    const auto local_name = QString::fromUtf8(bytes.constData() + local_offset_index + 30,
                                              local_name_length);
    if (local_flags != flags || local_name != name) {
      return false;
    }
    const auto local_extra_offset = local_offset_index + 30 +
                                    static_cast<qsizetype>(local_name_length);
    quint64 local_compressed_size = local_compressed_field;
    quint64 local_uncompressed_size = local_uncompressed_field;
    const auto local_needs_size = local_uncompressed_field == 0xffffffffU ||
                                  local_compressed_field == 0xffffffffU;
    if (local_needs_size &&
        !parse_zip64_extra(bytes, local_extra_offset, local_extra_length,
                           local_uncompressed_field == 0xffffffffU,
                           local_compressed_field == 0xffffffffU, false,
                           &local_uncompressed_size, &local_compressed_size, nullptr,
                           saw_zip64_entry_extra)) {
      return false;
    }
    if (local_uncompressed_size != uncompressed_size ||
        local_compressed_size != compressed_size) return false;
    const auto data_offset = local_offset_index + 30 +
                             static_cast<qsizetype>(local_name_length) +
                             static_cast<qsizetype>(local_extra_length);
    if (data_offset < 0 || uncompressed_size > static_cast<quint64>(bytes.size()) ||
        static_cast<quint64>(data_offset) + uncompressed_size > static_cast<quint64>(bytes.size())) {
      return false;
    }
    const auto data = bytes.mid(data_offset, static_cast<qsizetype>(uncompressed_size));
    if (expected_crc != crc32_value(data) ||
        read_u32(bytes, local_offset_index + 14) != expected_crc) return false;
    files->insert(name, data);
    cursor += 46 + static_cast<qsizetype>(name_length) +
              static_cast<qsizetype>(extra_length) +
              static_cast<qsizetype>(comment_length);
  }
  return static_cast<quint64>(cursor) == directory_offset + directory_size;
}

QByteArray read_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

bool write_file(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

bool test_portable_package() {
  QTemporaryDir temporary;
  if (!check(temporary.isValid(), "temporary workspace is available")) {
    return false;
  }
  const auto root = temporary.path();
  const auto source_dir_a = QDir(root).filePath(QStringLiteral("a café"));
  const auto source_dir_b = QDir(root).filePath(QStringLiteral("b café"));
  const auto media_a = QDir(source_dir_a).filePath(QStringLiteral("shot numéro.mov"));
  const auto media_b = QDir(source_dir_b).filePath(QStringLiteral("shot numéro.mov"));
  const auto lut = QDir(root).filePath(QStringLiteral("camera.cube"));
  const auto child_path = QDir(root).filePath(QStringLiteral("child.json"));
  const auto source_document = QDir(root).filePath(QStringLiteral("author.json"));
  const auto output_path = QDir(root).filePath(QStringLiteral("Portable project café.zip"));
  QDir().mkpath(source_dir_a);
  QDir().mkpath(source_dir_b);
  const QByteArray media_a_bytes("camera-a-original\0bytes", 23);
  const QByteArray media_b_bytes("camera-b-original\0bytes", 23);
  const QByteArray lut_bytes("LUT_1D_SIZE 2\n0 0 0\n1 1 1\n");
  if (!check(write_file(media_a, media_a_bytes) && write_file(media_b, media_b_bytes) &&
                 write_file(lut, lut_bytes),
             "fixture media and LUT files are written")) {
    return false;
  }

  const QJsonObject child_document{
      {QStringLiteral("timelineId"), QStringLiteral("child-id")},
      {QStringLiteral("name"), QStringLiteral("Child")},
      {QStringLiteral("tracks"), QJsonArray{QJsonObject{
           {QStringLiteral("clips"), QJsonArray{QJsonObject{
                {QStringLiteral("filePath"), media_a},
                {QStringLiteral("mediaType"), QStringLiteral("video")}}}}}}}};
  if (!check(write_file(child_path, QJsonDocument(child_document).toJson()),
             "compound timeline fixture is written")) {
    return false;
  }
  const QJsonObject root_document{
      {QStringLiteral("timelineId"), QStringLiteral("root-id")},
      {QStringLiteral("name"), QStringLiteral("Original Name")},
      {QStringLiteral("tracks"), QJsonArray{
           QJsonObject{{QStringLiteral("clips"), QJsonArray{
               QJsonObject{{QStringLiteral("filePath"), media_a},
                           {QStringLiteral("colorLutPath"), lut},
                           {QStringLiteral("colorLutEnabled"), true},
                           {QStringLiteral("titleFontFamily"), QStringLiteral("Example Licensed Font")}},
               QJsonObject{{QStringLiteral("filePath"), media_b},
                           {QStringLiteral("compoundDocumentPath"), child_path}}}}}}},
      {QStringLiteral("clips"), QJsonArray{QJsonObject{{QStringLiteral("filePath"), media_a}}}}};
  if (!check(write_file(source_document, QJsonDocument(root_document).toJson()),
             "root document fixture is written")) {
    return false;
  }

  auto cancellation = std::make_shared<std::atomic_bool>(false);
  const auto original_a = read_file(media_a);
  const auto original_b = read_file(media_b);
  const auto original_lut = read_file(lut);
  const auto packaged = createPortableProjectArchive(
      output_path, QJsonDocument(root_document).toJson(), QStringLiteral("Original Name"),
      source_document, cancellation);
  if (!check(packaged.value(QStringLiteral("success")).toBool(),
             "portable package is created") ||
      !check(QFileInfo::exists(output_path), "ZIP output exists after atomic commit")) {
    return false;
  }
  QHash<QString, QByteArray> archived_files;
  bool zip32_has_zip64_extra = false;
  bool zip32_has_zip64_footer = false;
  if (!check(unzip_stored_entries(output_path, &archived_files,
                                  &zip32_has_zip64_extra, &zip32_has_zip64_footer),
             "ZIP central directory and stored entries are valid") ||
      !check(!zip32_has_zip64_extra && !zip32_has_zip64_footer,
             "small packages keep the standard ZIP32 layout")) {
    return false;
  }
  if (!check(archived_files.contains(QStringLiteral("Project.json")) &&
                 archived_files.contains(QStringLiteral("manifest.json")) &&
                 archived_files.contains(QStringLiteral("README.txt")),
             "package has project, checksum manifest, and extraction instructions")) {
    return false;
  }

  QJsonParseError parse_error;
  const auto packaged_document = QJsonDocument::fromJson(
      archived_files.value(QStringLiteral("Project.json")), &parse_error);
  if (!check(packaged_document.isObject(), "packaged project document parses") ||
      !check(parse_error.error == QJsonParseError::NoError, "packaged document JSON is valid")) {
    return false;
  }
  const auto clips = packaged_document.object().value(QStringLiteral("tracks")).toArray()
                         .first().toObject().value(QStringLiteral("clips")).toArray();
  const auto first_clip = clips.at(0).toObject();
  const auto second_clip = clips.at(1).toObject();
  const auto path_a = first_clip.value(QStringLiteral("filePath")).toString();
  const auto path_b = second_clip.value(QStringLiteral("filePath")).toString();
  const auto lut_path = first_clip.value(QStringLiteral("colorLutPath")).toString();
  if (!check(path_a.startsWith(QStringLiteral("Media/")) &&
                 path_b.startsWith(QStringLiteral("Media/")) && path_a != path_b,
             "same-named sources receive collision-resistant relative package paths") ||
      !check(lut_path.startsWith(QStringLiteral("Media/")), "LUT reference is portable and relative")) {
    return false;
  }
  const auto compound_path = second_clip.value(QStringLiteral("compoundDocumentPath")).toString();
  if (!check(archived_files.contains(compound_path), "compound timeline is collected into the package") ||
      !check(!compound_path.contains(QStringLiteral("..")), "compound package path has no parent traversal") ||
      !check(archived_files.value(path_a) == media_a_bytes && archived_files.value(path_b) == media_b_bytes &&
                 archived_files.value(lut_path) == lut_bytes,
             "packaged media and LUT bytes match the original referenced files")) {
    return false;
  }
  const auto extracted_root = QDir(root).filePath(QStringLiteral("extracted"));
  for (auto iterator = archived_files.cbegin(); iterator != archived_files.cend(); ++iterator) {
    const auto target_path = QDir(extracted_root).absoluteFilePath(iterator.key());
    const auto clean_root = QDir::cleanPath(QFileInfo(extracted_root).absoluteFilePath());
    const auto clean_target = QDir::cleanPath(target_path);
    if (!check(clean_target.startsWith(clean_root + QDir::separator()),
               "validated ZIP entry stays inside the extraction root")) {
      return false;
    }
    QDir().mkpath(QFileInfo(target_path).absolutePath());
    if (!check(write_file(target_path, iterator.value()), "valid package entry extracts")) {
      return false;
    }
  }
  const auto extracted_document = QDir(extracted_root).filePath(QStringLiteral("Project.json"));
  const auto resolved_media = resolveDocumentResourcePath(path_a, extracted_document);
  if (!check(QFileInfo(resolved_media).absoluteFilePath() == QFileInfo(
                 QDir(extracted_root).filePath(path_a)).absoluteFilePath() &&
                 read_file(resolved_media) == media_a_bytes,
             "document-relative media resolves and reopens from an extracted package")) {
    return false;
  }
  for (auto iterator = archived_files.cbegin(); iterator != archived_files.cend(); ++iterator) {
    if (!check(!iterator.key().startsWith(QLatin1Char('/')) &&
                   !iterator.key().contains(QStringLiteral("../")) &&
                   !iterator.key().contains(QLatin1Char('\\')),
               "ZIP entries never contain absolute paths or traversal")) {
      return false;
    }
  }
  const auto manifest = QJsonDocument::fromJson(archived_files.value(QStringLiteral("manifest.json")));
  const auto manifest_files = manifest.object().value(QStringLiteral("files")).toArray();
  QSet<QString> checksum_paths;
  auto all_archive_paths_are_ascii = true;
  for (auto iterator = archived_files.cbegin(); iterator != archived_files.cend(); ++iterator) {
    for (const auto character : iterator.key()) {
      all_archive_paths_are_ascii = all_archive_paths_are_ascii && character.unicode() < 0x80U;
    }
  }
  auto original_unicode_label_is_disclosed = false;
  for (const auto& item : manifest_files) {
    const auto record = item.toObject();
    const auto path = record.value(QStringLiteral("path")).toString();
    original_unicode_label_is_disclosed = original_unicode_label_is_disclosed ||
        record.value(QStringLiteral("sourceLabel")).toString().contains(QStringLiteral("numéro"));
    const auto bytes = archived_files.value(path);
    const auto checksum = QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    if (!check(archived_files.contains(path) && record.value(QStringLiteral("sha256")).toString() == checksum,
               "manifest checksum matches archived bytes")) {
      return false;
    }
    checksum_paths.insert(path);
  }
  if (!check(checksum_paths.contains(QStringLiteral("Project.json")) &&
                 checksum_paths.contains(path_a) && checksum_paths.contains(path_b) &&
                 checksum_paths.contains(lut_path),
             "manifest covers project, all referenced media, and LUT bytes") ||
      !check(all_archive_paths_are_ascii && original_unicode_label_is_disclosed,
             "ZIP entry names are portable ASCII while the manifest retains original Unicode source labels") ||
      !check(manifest.object().value(QStringLiteral("fontDependencies")).toArray().size() == 1,
             "font dependency is disclosed without embedding a font file")) {
    return false;
  }
  if (!check(read_file(media_a) == original_a && read_file(media_b) == original_b &&
                 read_file(lut) == original_lut,
             "source media and LUT files remain byte-identical")) {
    return false;
  }

  const auto zip64_path = QDir(root).filePath(QStringLiteral("portable-zip64-test.zip"));
  const auto zip64_result = createPortableProjectArchive(
      zip64_path, QJsonDocument(root_document).toJson(), QStringLiteral("Original Name"),
      source_document, cancellation, 16U);
  QHash<QString, QByteArray> zip64_files;
  bool zip64_has_entry_extra = false;
  bool zip64_has_end_records = false;
  if (!check(zip64_result.value(QStringLiteral("success")).toBool(),
             "ZIP64 test seam creates a small archive with ZIP64 headers") ||
      !check(unzip_stored_entries(zip64_path, &zip64_files, &zip64_has_entry_extra,
                                  &zip64_has_end_records),
             "ZIP64 local/central entries and end records are readable") ||
      !check(zip64_has_entry_extra && zip64_has_end_records,
             "ZIP64 branch writes per-entry extra fields and ZIP64 end records") ||
      !check(zip64_files.value(QStringLiteral("Project.json")).size() > 16,
             "ZIP64 test exercises large-entry sentinels without multi-gigabyte fixtures")) {
    return false;
  }
  const auto unzip = QStandardPaths::findExecutable(QStringLiteral("unzip"));
  if (!check(!unzip.isEmpty() &&
                 QProcess::execute(unzip, {QStringLiteral("-tq"), zip64_path}) == 0,
             "a standard ZIP reader accepts the ZIP64 package")) {
    return false;
  }

  const auto racing_destination = QDir(root).filePath(QStringLiteral("appeared-during-write.zip"));
  const QByteArray racing_destination_bytes("created by another writer");
  bool destination_created_before_publish = false;
  const auto racing_result = createPortableProjectArchive(
      racing_destination, QJsonDocument(root_document).toJson(), QStringLiteral("Original Name"),
      source_document, cancellation, 0xfffffffeULL, [&]() {
        destination_created_before_publish = write_file(racing_destination, racing_destination_bytes);
      });
  if (!check(destination_created_before_publish &&
                 !racing_result.value(QStringLiteral("success")).toBool() &&
                 read_file(racing_destination) == racing_destination_bytes,
             "a destination created immediately before publish is never overwritten") ||
      !check(QDir(root).entryList({QStringLiteral("appeared-during-write.zip.*")},
                                  QDir::Files | QDir::Hidden).isEmpty(),
             "failed no-replace publish cleans up its staged archive")) {
    return false;
  }

  const auto existing_before = read_file(output_path);
  const auto overwrite_result = createPortableProjectArchive(
      output_path, QJsonDocument(root_document).toJson(), QStringLiteral("Original Name"),
      source_document, cancellation);
  if (!check(!overwrite_result.value(QStringLiteral("success")).toBool() &&
                 read_file(output_path) == existing_before,
             "existing destination is left untouched")) {
    return false;
  }

  const auto missing_path = QDir(root).filePath(QStringLiteral("missing.zip"));
  auto missing_document = root_document;
  auto missing_tracks = missing_document.value(QStringLiteral("tracks")).toArray();
  auto first_track = missing_tracks.first().toObject();
  auto missing_clips = first_track.value(QStringLiteral("clips")).toArray();
  auto broken_clip = missing_clips.first().toObject();
  broken_clip.insert(QStringLiteral("filePath"), QDir(root).filePath(QStringLiteral("absent.mov")));
  missing_clips.replace(0, broken_clip);
  first_track.insert(QStringLiteral("clips"), missing_clips);
  missing_tracks.replace(0, first_track);
  missing_document.insert(QStringLiteral("tracks"), missing_tracks);
  const auto missing_result = createPortableProjectArchive(
      missing_path, QJsonDocument(missing_document).toJson(), QStringLiteral("Missing"),
      source_document, cancellation);
  if (!check(!missing_result.value(QStringLiteral("success")).toBool() &&
                 !QFileInfo::exists(missing_path),
             "missing required media fails without leaving a partial archive")) {
    return false;
  }

  const auto oversized_compound_path = QDir(root).filePath(QStringLiteral("oversized-compound.json"));
  QFile oversized_compound(oversized_compound_path);
  if (!check(oversized_compound.open(QIODevice::WriteOnly) &&
                 oversized_compound.resize(64LL * 1024LL * 1024LL + 1LL),
             "sparse oversized compound document fixture is created")) {
    return false;
  }
  oversized_compound.close();
  const auto oversized_output = QDir(root).filePath(QStringLiteral("oversized-compound.zip"));
  const QJsonObject oversized_document{
      {QStringLiteral("timelineId"), QStringLiteral("oversized-compound-id")},
      {QStringLiteral("tracks"), QJsonArray{QJsonObject{
           {QStringLiteral("clips"), QJsonArray{QJsonObject{
                {QStringLiteral("compoundDocumentPath"), oversized_compound_path}}}}}}}};
  const auto oversized_result = createPortableProjectArchive(
      oversized_output, QJsonDocument(oversized_document).toJson(),
      QStringLiteral("Oversized compound"), source_document, cancellation);
  if (!check(!oversized_result.value(QStringLiteral("success")).toBool() &&
                 oversized_result.value(QStringLiteral("message")).toString().contains(
                     QStringLiteral("64 MiB")) &&
                 !QFileInfo::exists(oversized_output),
             "compound-document size is bounded before parsing and leaves no partial archive")) {
    return false;
  }

  const auto symlink_path = QDir(root).filePath(QStringLiteral("linked-shot.mov"));
  if (!check(QFile::link(media_a, symlink_path), "symbolic-link fixture is created")) {
    return false;
  }
  const auto symlink_output = QDir(root).filePath(QStringLiteral("symlink.zip"));
  const auto symlink_document = QJsonObject{
      {QStringLiteral("timelineId"), QStringLiteral("symlink-id")},
      {QStringLiteral("tracks"), QJsonArray{QJsonObject{
           {QStringLiteral("clips"), QJsonArray{QJsonObject{
                {QStringLiteral("filePath"), symlink_path}}}}}}}};
  const auto symlink_result = createPortableProjectArchive(
      symlink_output, QJsonDocument(symlink_document).toJson(), QStringLiteral("Symlink"),
      source_document, cancellation);
  if (!check(!symlink_result.value(QStringLiteral("success")).toBool() &&
                 !QFileInfo::exists(symlink_output),
             "symbolic-link media is rejected without creating an archive")) {
    return false;
  }

  const auto large_media = QDir(root).filePath(QStringLiteral("large-source.mov"));
  const auto large_output = QDir(root).filePath(QStringLiteral("mid-cancel.zip"));
  if (!check(write_file(large_media, QByteArray(32 * 1024 * 1024, 'x')),
             "large cancellation fixture is written")) {
    return false;
  }
  const QJsonObject large_document{
      {QStringLiteral("timelineId"), QStringLiteral("large-id")},
      {QStringLiteral("tracks"), QJsonArray{QJsonObject{
           {QStringLiteral("clips"), QJsonArray{QJsonObject{
                {QStringLiteral("filePath"), large_media}}}}}}}};
  auto mid_cancellation = std::make_shared<std::atomic_bool>(false);
  std::atomic_bool package_finished{false};
  std::thread cancel_thread([mid_cancellation, &package_finished, root]() {
    while (!package_finished.load(std::memory_order_relaxed)) {
      const auto files = QDir(root).entryList(QDir::Files | QDir::Hidden);
      const auto temporary_archive = std::find_if(
          files.cbegin(), files.cend(), [](const QString& file_name) {
            return file_name.contains(QStringLiteral("mid-cancel.zip."));
          });
      if (temporary_archive != files.cend()) {
        mid_cancellation->store(true, std::memory_order_relaxed);
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  const auto mid_cancel_result = createPortableProjectArchive(
      large_output, QJsonDocument(large_document).toJson(), QStringLiteral("Large"),
      source_document, mid_cancellation);
  package_finished.store(true, std::memory_order_relaxed);
  cancel_thread.join();
  if (!check(mid_cancel_result.value(QStringLiteral("status")).toString() == QStringLiteral("canceled") &&
                 !QFileInfo::exists(large_output),
             "mid-copy cancellation atomically removes incomplete output")) {
    return false;
  }

  const auto canceled_path = QDir(root).filePath(QStringLiteral("canceled.zip"));
  auto canceled = std::make_shared<std::atomic_bool>(true);
  const auto canceled_result = createPortableProjectArchive(
      canceled_path, QJsonDocument(root_document).toJson(), QStringLiteral("Canceled"),
      source_document, canceled);
  return check(canceled_result.value(QStringLiteral("status")).toString() == QStringLiteral("canceled") &&
                   !QFileInfo::exists(canceled_path),
               "pre-canceled collection leaves no partial output");
}

bool test_output_qc() {
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  if (!check(!ffmpeg.isEmpty() && !ffprobe.isEmpty(),
             "encoded output QC proof requires ffmpeg and ffprobe on PATH")) {
    return false;
  }
  QTemporaryDir temporary;
  if (!check(temporary.isValid(), "temporary QC workspace is available")) {
    return false;
  }
  const auto fixture_path = QDir(temporary.path()).filePath(QStringLiteral("black silence café.mkv"));
  QProcess encoder;
  encoder.setProgram(ffmpeg);
  encoder.setArguments({QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                        QStringLiteral("error"), QStringLiteral("-y"),
                        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                        QStringLiteral("color=c=black:s=64x48:r=24:d=1"),
                        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                        QStringLiteral("sine=frequency=1000:sample_rate=48000:duration=1"),
                        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                        QStringLiteral("anullsrc=channel_layout=stereo:sample_rate=48000"),
                        QStringLiteral("-map"), QStringLiteral("0:v:0"),
                        QStringLiteral("-map"), QStringLiteral("1:a:0"),
                        QStringLiteral("-map"), QStringLiteral("2:a:0"),
                        QStringLiteral("-t"), QStringLiteral("1"), QStringLiteral("-c:v"),
                        QStringLiteral("ffv1"), QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"),
                        fixture_path});
  encoder.start();
  if (!encoder.waitForStarted(3000) || !encoder.waitForFinished(30000) ||
      encoder.exitCode() != 0) {
    return check(false, "ffmpeg can encode deterministic black and silence QC fixture");
  }
  auto cancellation = std::make_shared<std::atomic_bool>(false);
  const auto result = inspectRenderedOutput(fixture_path, cancellation);
  if (!check(result.value(QStringLiteral("success")).toBool(),
             "valid encoded audio/video output passes full decode") ||
      !check(result.value(QStringLiteral("hasVideo")).toBool() &&
                 result.value(QStringLiteral("hasAudio")).toBool(),
             "ffprobe identifies the actual video and audio streams") ||
      !check(result.value(QStringLiteral("audioStreamsScanned")).toInt() == 2 &&
                 result.value(QStringLiteral("videoStreamsScanned")).toInt() == 1,
             "signal QC scans each audio and video stream in the file") ||
      !check(result.value(QStringLiteral("durationSeconds")).toDouble() > 0.9,
             "ffprobe reports encoded output duration") ||
      !check(result.value(QStringLiteral("decodeOk")).toBool(),
             "full ffmpeg decode completes without reported corruption") ||
      !check(result.value(QStringLiteral("signalsAreAdvisory")).toBool(),
             "black and silence findings are advisory, never arbitrary delivery blockers") ||
      !check(!result.value(QStringLiteral("blackSegments")).toList().isEmpty() &&
                 !result.value(QStringLiteral("silenceSegments")).toList().isEmpty(),
             "known black video and silent audio are surfaced as review spans")) {
    return false;
  }
  const auto black_span = result.value(QStringLiteral("blackSegments")).toList().first().toMap();
  const auto silence_span = result.value(QStringLiteral("silenceSegments")).toList().first().toMap();
  if (!check(black_span.value(QStringLiteral("endSeconds")).toDouble() >
                 black_span.value(QStringLiteral("startSeconds")).toDouble() &&
                 silence_span.value(QStringLiteral("endSeconds")).toDouble() >
                 silence_span.value(QStringLiteral("startSeconds")).toDouble() &&
                 black_span.value(QStringLiteral("streamIndex")).toInt() == 0 &&
                 black_span.value(QStringLiteral("streamLabel")).toString() == QStringLiteral("Video stream 0"),
             "black and silence QC spans include actionable times and the source stream identity")) {
    return false;
  }
  auto second_audio_stream_flagged = false;
  for (const auto& span_value : result.value(QStringLiteral("silenceSegments")).toList()) {
    const auto span = span_value.toMap();
    if (span.value(QStringLiteral("streamIndex")).toInt() == 2 &&
        span.value(QStringLiteral("streamLabel")).toString() == QStringLiteral("Audio stream 2")) {
      second_audio_stream_flagged = true;
      break;
    }
  }
  if (!check(second_audio_stream_flagged,
             "the silent second audio stream is identified even when the first stream contains a tone")) {
    return false;
  }
  const auto findings = result.value(QStringLiteral("findings")).toList();
  bool has_review = false;
  for (const auto& finding : findings) {
    has_review = has_review || finding.toMap().value(QStringLiteral("severity")).toString() ==
                                   QStringLiteral("review");
  }
  if (!check(has_review, "black/silence observations appear as review findings")) {
    return false;
  }
  const auto invalid_path = QDir(temporary.path()).filePath(QStringLiteral("not media café.bin"));
  if (!check(write_file(invalid_path, QByteArray("not a media stream")),
             "invalid QC fixture is written")) {
    return false;
  }
  const auto invalid = inspectRenderedOutput(invalid_path, cancellation);
  return check(!invalid.value(QStringLiteral("success")).toBool(),
               "invalid media is reported as an inspection failure");
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  return test_portable_package() && test_output_qc() ? 0 : 1;
}
