#include "timeline_mcp_export.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>

using photo_editor::desktop::kMaximumTimelineMcpReviewExcerptBytes;
using photo_editor::desktop::kMaximumTimelineMcpExportBytes;
using photo_editor::desktop::timeline_mcp_export_fingerprint;
using photo_editor::desktop::timeline_mcp_export_maximum_output_bytes;
using photo_editor::desktop::timeline_mcp_export_review_range;
using photo_editor::desktop::timeline_mcp_export_source_manifest;
using photo_editor::desktop::timeline_mcp_export_verify_source_manifest;

namespace {

bool write_file(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
         file.write(bytes) == bytes.size() && file.flush();
}

QJsonObject source_clip(const QString& path, double start, double trim_end) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  const auto bytes = file.readAll();
  const auto hash = QString::fromLatin1(
      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
  return {{QStringLiteral("mediaType"), QStringLiteral("video")},
          {QStringLiteral("filePath"), QFileInfo(path).canonicalFilePath()},
          {QStringLiteral("sourceFileSize"), bytes.size()},
          {QStringLiteral("sourceContentSha256"), hash},
          {QStringLiteral("startSeconds"), start},
          {QStringLiteral("durationSeconds"), trim_end},
          {QStringLiteral("trimStartSeconds"), 0.0},
          {QStringLiteral("trimEndSeconds"), trim_end},
          {QStringLiteral("playbackRate"), 1.0}};
}

bool run() {
  QTemporaryDir directory;
  if (!directory.isValid()) return false;
  const auto media_a = directory.filePath(QStringLiteral("range-a.bin"));
  const auto media_b = directory.filePath(QStringLiteral("range-b.bin"));
  const auto audio = directory.filePath(QStringLiteral("range-audio.bin"));
  const auto photo = directory.filePath(QStringLiteral("range-default-photo.png"));
  const auto matte = directory.filePath(QStringLiteral("range-matte.bin"));
  const auto lut = directory.filePath(QStringLiteral("range-look.cube"));
  const auto compound = directory.filePath(QStringLiteral("range-compound.json"));
  if (!write_file(media_a, QByteArrayLiteral("primary-a")) ||
      !write_file(media_b, QByteArrayLiteral("primary-b")) ||
      !write_file(audio, QByteArrayLiteral("primary-audio")) ||
      !write_file(photo, QByteArrayLiteral("default-photo-pixels")) ||
      !write_file(matte, QByteArrayLiteral("matte-pixels")) ||
      !write_file(lut, QByteArrayLiteral("lut-points")) ||
      !write_file(compound, QByteArrayLiteral("compound-snapshot"))) {
    std::cerr << "fixture files could not be written\n";
    return false;
  }

  auto selected_clip = source_clip(media_a, 0.75, 2.0);
  selected_clip.insert(QStringLiteral("colorLutEnabled"), true);
  selected_clip.insert(QStringLiteral("colorLutPath"), QFileInfo(lut).canonicalFilePath());
  selected_clip.insert(QStringLiteral("sourceMatte"), QJsonObject{
      {QStringLiteral("enabled"), true},
      {QStringLiteral("filePath"), QFileInfo(matte).canonicalFilePath()},
  });
  selected_clip.insert(QStringLiteral("compoundDocumentPath"),
                       QFileInfo(compound).canonicalFilePath());
  auto default_photo_clip = source_clip(photo, 0.5, 2.0);
  default_photo_clip.insert(QStringLiteral("mediaType"), QStringLiteral("photo"));
  default_photo_clip.insert(QStringLiteral("colorLutEnabled"), true);
  default_photo_clip.insert(QStringLiteral("colorLutPath"), QString{});
  auto dissolve_clip = source_clip(media_b, 2.05, 1.0);
  dissolve_clip.insert(QStringLiteral("transitionInName"), QStringLiteral("Cross Dissolve"));
  dissolve_clip.insert(QStringLiteral("transitionInSeconds"), 0.4);
  auto disabled_missing_lut_clip = source_clip(media_b, 0.5, 2.0);
  disabled_missing_lut_clip.insert(QStringLiteral("colorLutEnabled"), false);
  disabled_missing_lut_clip.insert(
      QStringLiteral("colorLutPath"), directory.filePath(QStringLiteral("missing-disabled.cube")));
  const QJsonObject off_range_clip{
      {QStringLiteral("mediaType"), QStringLiteral("video")},
      {QStringLiteral("filePath"), directory.filePath(QStringLiteral("missing-off-range.bin"))},
      {QStringLiteral("sourceFileSize"), 1000000},
      {QStringLiteral("sourceContentSha256"), QString(64, QLatin1Char('f'))},
      {QStringLiteral("startSeconds"), 0.0},
      {QStringLiteral("durationSeconds"), 0.5},
      {QStringLiteral("trimStartSeconds"), 0.0},
      {QStringLiteral("trimEndSeconds"), 0.5},
      {QStringLiteral("playbackRate"), 1.0},
  };
  auto audio_clip = source_clip(audio, 0.5, 2.0);
  audio_clip.insert(QStringLiteral("mediaType"), QStringLiteral("audio"));
  audio_clip.insert(QStringLiteral("colorLutEnabled"), true);
  audio_clip.insert(QStringLiteral("colorLutPath"), QString{});
  const QJsonObject snapshot{
      {QStringLiteral("tracks"), QJsonArray{
          QJsonObject{{QStringLiteral("kind"), QStringLiteral("video")},
                      {QStringLiteral("clips"), QJsonArray{
                          off_range_clip, selected_clip, default_photo_clip, dissolve_clip,
                          disabled_missing_lut_clip}}},
          QJsonObject{{QStringLiteral("kind"), QStringLiteral("audio")},
                      {QStringLiteral("clips"), QJsonArray{audio_clip}}},
      }},
  };
  const QJsonObject range{
      {QStringLiteral("startFrame"), 30},
      {QStringLiteral("endFrameExclusive"), 60},
      {QStringLiteral("frameRate"), 30.0},
  };
  qint64 start_frame = 0;
  qint64 end_frame = 0;
  auto frame_rate = 0.0;
  if (!timeline_mcp_export_review_range(range, &start_frame, &end_frame, &frame_rate) ||
      start_frame != 30 || end_frame != 60 || frame_rate != 30.0 ||
      timeline_mcp_export_review_range(
          QJsonObject{{QStringLiteral("startFrame"), 30},
                      {QStringLiteral("endFrameExclusive"), 30},
                      {QStringLiteral("frameRate"), 30.0}},
          &start_frame, &end_frame, &frame_rate) ||
      timeline_mcp_export_review_range(
          QJsonObject{{QStringLiteral("startFrame"), 0},
                      {QStringLiteral("endFrameExclusive"), 481},
                      {QStringLiteral("frameRate"), 30.0}},
          &start_frame, &end_frame, &frame_rate)) {
    std::cerr << "global half-open excerpt validation did not enforce a nonempty 15-second limit\n";
    return false;
  }
  for (const auto fps : {24.0, 30.0, 60.0, 120.0, 240.0, 30000.0 / 1001.0}) {
    const auto minimum_frames = static_cast<qint64>(std::ceil(fps));
    const auto maximum_frames = static_cast<qint64>(std::floor(15.0 * fps));
    for (const auto frames : {qint64{1}, minimum_frames - 1,
                              minimum_frames, maximum_frames, maximum_frames + 1}) {
      const auto candidate = QJsonObject{
          {QStringLiteral("startFrame"), 6540},
          {QStringLiteral("endFrameExclusive"), 6540 + frames},
          {QStringLiteral("frameRate"), fps}};
      const auto expected = frames >= minimum_frames && frames <= maximum_frames;
      if (timeline_mcp_export_review_range(
              candidate, &start_frame, &end_frame, &frame_rate) != expected) {
        std::cerr << "review export preflight disagrees with the 1-to-15-second frame range at "
                  << fps << " fps, " << frames << " frames\n";
        return false;
      }
    }
  }
  if (timeline_mcp_export_maximum_output_bytes({}) != kMaximumTimelineMcpExportBytes ||
      timeline_mcp_export_maximum_output_bytes(range) != kMaximumTimelineMcpReviewExcerptBytes ||
      timeline_mcp_export_fingerprint(QStringLiteral("request"), QStringLiteral("timeline"),
                                      QStringLiteral("hash"), QStringLiteral("root"), range) ==
          timeline_mcp_export_fingerprint(QStringLiteral("request"), QStringLiteral("timeline"),
                                         QStringLiteral("hash"), QStringLiteral("root"))) {
    std::cerr << "review excerpt caps and idempotency fingerprints should bind the selected range\n";
    return false;
  }

  const auto manifest = timeline_mcp_export_source_manifest(
      QJsonDocument(snapshot).toJson(QJsonDocument::Compact), range);
  const auto records = manifest.value(QStringLiteral("sources")).toList();
  if (!manifest.value(QStringLiteral("ok")).toBool() ||
      manifest.value(QStringLiteral("primarySourceCount")).toInt() != 4 ||
      manifest.value(QStringLiteral("sourceCount")).toInt() != 7 || records.size() != 7) {
    std::cerr << "the range manifest should include intersecting media and active dependencies while ignoring an enabled empty default LUT\n";
    return false;
  }
  auto enabled_missing_lut_clip = selected_clip;
  enabled_missing_lut_clip.insert(
      QStringLiteral("colorLutPath"), directory.filePath(QStringLiteral("missing-enabled.cube")));
  const QJsonObject missing_lut_snapshot{
      {QStringLiteral("tracks"), QJsonArray{QJsonObject{
          {QStringLiteral("kind"), QStringLiteral("video")},
          {QStringLiteral("clips"), QJsonArray{enabled_missing_lut_clip}},
      }}}};
  if (timeline_mcp_export_source_manifest(
          QJsonDocument(missing_lut_snapshot).toJson(QJsonDocument::Compact), range)
          .value(QStringLiteral("ok")).toBool()) {
    std::cerr << "an enabled nonempty missing LUT dependency must fail closed\n";
    return false;
  }
  auto stat_dependency_count = 0;
  for (const auto& value : records) {
    const auto record = value.toMap();
    if (record.value(QStringLiteral("identityKind")).toString() == QStringLiteral("stat-v1")) {
      ++stat_dependency_count;
      if (record.value(QStringLiteral("modifiedMs")).toLongLong() <= 0 ||
          record.contains(QStringLiteral("sha256"))) {
        std::cerr << "untracked dependencies should be pinned by cheap stat identity before worker hashing\n";
        return false;
      }
    }
  }
  if (stat_dependency_count != 3) {
    std::cerr << "all active side dependencies should use asynchronous stat-pinned hashing\n";
    return false;
  }
  const auto verified = timeline_mcp_export_verify_source_manifest(records);
  if (!verified.value(QStringLiteral("ok")).toBool() ||
      verified.value(QStringLiteral("verifiedSourceCount")).toInt() != 7 ||
      verified.value(QStringLiteral("verifiedSources")).toList().size() != 7) {
    std::cerr << "worker verification should hash every pinned source and dependency\n";
    return false;
  }
  QFile changed_lut(lut);
  if (!changed_lut.open(QIODevice::Append) || changed_lut.write("changed") != 7) {
    std::cerr << "dependency mutation fixture could not be changed after enqueue\n";
    return false;
  }
  if (!changed_lut.flush()) {
    std::cerr << "dependency mutation fixture could not flush its queued change\n";
    return false;
  }
  changed_lut.close();
  const auto changed = timeline_mcp_export_verify_source_manifest(records);
  if (changed.value(QStringLiteral("ok")).toBool() ||
      changed.value(QStringLiteral("errorCode")).toString() !=
          QStringLiteral("source_identity_changed") ||
      changed.value(QStringLiteral("verifiedSourceCount")).toInt() < 1) {
    std::cerr << "a file changed after range queueing must fail closed with typed counts: code="
              << changed.value(QStringLiteral("errorCode")).toString().toStdString()
              << " verified=" << changed.value(QStringLiteral("verifiedSourceCount")).toInt()
              << " total=" << changed.value(QStringLiteral("sourceCount")).toInt()
              << " message=" << changed.value(QStringLiteral("error")).toString().toStdString()
              << '\n';
    return false;
  }

  auto missing_source_hash_clip = source_clip(media_a, 0.0, 2.0);
  missing_source_hash_clip.remove(QStringLiteral("sourceContentSha256"));
  const QJsonObject missing_source_hash_snapshot{
      {QStringLiteral("tracks"), QJsonArray{QJsonObject{
          {QStringLiteral("kind"), QStringLiteral("video")},
          {QStringLiteral("clips"), QJsonArray{missing_source_hash_clip}},
      }}}};
  const auto missing_source_hash_manifest = timeline_mcp_export_source_manifest(
      QJsonDocument(missing_source_hash_snapshot).toJson(QJsonDocument::Compact), range);
  if (missing_source_hash_manifest.value(QStringLiteral("ok")).toBool()) {
    std::cerr << "a selected primary source without a recorded full SHA-256 must fail closed\n";
    return false;
  }

  const QJsonObject primary_source_snapshot{
      {QStringLiteral("tracks"), QJsonArray{QJsonObject{
          {QStringLiteral("kind"), QStringLiteral("video")},
          {QStringLiteral("clips"), QJsonArray{source_clip(media_a, 0.0, 2.0)}},
      }}}};
  const auto primary_source_manifest = timeline_mcp_export_source_manifest(
      QJsonDocument(primary_source_snapshot).toJson(QJsonDocument::Compact), range);
  const auto primary_source_records =
      primary_source_manifest.value(QStringLiteral("sources")).toList();
  QFile changed_primary_source(media_a);
  if (!primary_source_manifest.value(QStringLiteral("ok")).toBool() ||
      primary_source_records.size() != 1 ||
      !changed_primary_source.open(QIODevice::ReadWrite) ||
      !changed_primary_source.seek(0) ||
      changed_primary_source.write("changed-a") != 9 ||
      !changed_primary_source.flush()) {
    std::cerr << "primary source checksum mutation fixture could not be prepared\n";
    return false;
  }
  changed_primary_source.close();
  const auto changed_primary_source_result =
      timeline_mcp_export_verify_source_manifest(primary_source_records);
  if (changed_primary_source_result.value(QStringLiteral("ok")).toBool() ||
      changed_primary_source_result.value(QStringLiteral("errorCode")).toString() !=
          QStringLiteral("source_checksum_mismatch")) {
    std::cerr << "a primary source changed after snapshot must fail its full-hash verification\n";
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  return run() ? 0 : 1;
}
