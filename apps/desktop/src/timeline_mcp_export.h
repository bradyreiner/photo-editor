#pragma once

#include "job_store.h"
#include "timeline_generated_source.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QVariantMap>
#include <QUuid>

#include <functional>
#include <cmath>
#include <limits>

namespace photo_editor::desktop {

inline constexpr qint64 kMaximumTimelineMcpExportBytes =
    16LL * 1024LL * 1024LL * 1024LL;
inline constexpr qint64 kMaximumTimelineMcpExportSnapshotBytes =
    256LL * 1024LL * 1024LL;
inline constexpr qint64 kMaximumTimelineMcpReviewExcerptBytes =
    256LL * 1024LL * 1024LL;
inline constexpr double kMinimumTimelineMcpReviewExcerptSeconds = 1.0;
inline constexpr double kMaximumTimelineMcpReviewExcerptSeconds = 15.0;
inline constexpr auto kTimelineMcpExportSchema = "native-mcp-timeline-h264-export:v1";
inline constexpr auto kTimelineMcpExportOrigin = "native_mcp.timeline.export_h264";

inline bool timeline_mcp_export_canonical_uuid(const QString& value) {
  if (value.size() != 36) return false;
  const auto parsed = QUuid::fromString(value);
  return !parsed.isNull() && parsed.toString(QUuid::WithoutBraces) == value;
}

inline bool timeline_mcp_export_canonical_sha256(const QString& value) {
  static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{64}$"));
  return pattern.match(value).hasMatch();
}

inline bool timeline_mcp_export_json_integer(const QJsonValue& value, qint64* result) {
  if (!value.isDouble() || result == nullptr) return false;
  const auto number = value.toDouble();
  if (!std::isfinite(number) || std::floor(number) != number || number < 0.0 ||
      number >= static_cast<double>(std::numeric_limits<qint64>::max())) {
    return false;
  }
  const auto integer = static_cast<qint64>(number);
  if (static_cast<double>(integer) != number) return false;
  *result = integer;
  return true;
}

inline bool timeline_mcp_export_review_range(
    const QJsonObject& range,
    qint64* start_frame,
    qint64* end_frame_exclusive,
    double* frame_rate) {
  if (range.isEmpty() || range.size() != 3 ||
      !range.contains(QStringLiteral("startFrame")) ||
      !range.contains(QStringLiteral("endFrameExclusive")) ||
      !range.contains(QStringLiteral("frameRate")) ||
      start_frame == nullptr || end_frame_exclusive == nullptr || frame_rate == nullptr) {
    return false;
  }
  qint64 parsed_start = 0;
  qint64 parsed_end = 0;
  const auto parsed_frame_rate = range.value(QStringLiteral("frameRate")).toDouble(-1.0);
  if (!timeline_mcp_export_json_integer(
          range.value(QStringLiteral("startFrame")), &parsed_start) ||
      !timeline_mcp_export_json_integer(
          range.value(QStringLiteral("endFrameExclusive")), &parsed_end) ||
      !std::isfinite(parsed_frame_rate) || parsed_frame_rate <= 0.0 ||
      parsed_end <= parsed_start ||
      static_cast<double>(parsed_end - parsed_start) / parsed_frame_rate <
          kMinimumTimelineMcpReviewExcerptSeconds - 1.0e-9 ||
      static_cast<double>(parsed_end - parsed_start) / parsed_frame_rate >
          kMaximumTimelineMcpReviewExcerptSeconds + 1.0e-9) {
    return false;
  }
  *start_frame = parsed_start;
  *end_frame_exclusive = parsed_end;
  *frame_rate = parsed_frame_rate;
  return true;
}

inline qint64 timeline_mcp_export_maximum_output_bytes(
    const QJsonObject& range) {
  return range.isEmpty()
             ? kMaximumTimelineMcpExportBytes
             : kMaximumTimelineMcpReviewExcerptBytes;
}

inline QString timeline_mcp_export_fingerprint(
    const QString& request_id,
    const QString& resource_id,
    const QString& document_hash,
    const QString& output_root,
    const QJsonObject& range = {}) {
  auto request = QJsonObject{
      {QStringLiteral("schema"), QString::fromLatin1(kTimelineMcpExportSchema)},
      {QStringLiteral("requestId"), request_id},
      {QStringLiteral("resourceId"), resource_id},
      {QStringLiteral("documentHash"), document_hash},
      {QStringLiteral("outputRoot"), output_root},
      {QStringLiteral("container"), QStringLiteral("mp4")},
      {QStringLiteral("codec"), QStringLiteral("h264")},
      {QStringLiteral("maximumOutputBytes"), timeline_mcp_export_maximum_output_bytes(range)},
  };
  if (!range.isEmpty()) request.insert(QStringLiteral("range"), range);
  return QString::fromLatin1(QCryptographicHash::hash(
      QJsonDocument(request).toJson(QJsonDocument::Compact),
      QCryptographicHash::Sha256).toHex());
}

inline bool timeline_mcp_export_path_is_generated_child(
    const QString& path,
    const QString& root,
    const QString& expected_name,
    bool require_existing_file = false) {
  const QFileInfo root_info(root);
  const auto canonical_root = root_info.canonicalFilePath();
  if (canonical_root.isEmpty() || !root_info.isDir() || !root_info.isReadable()) return false;
  const QFileInfo path_info(path);
  if (path_info.isSymLink() || path_info.fileName() != expected_name ||
      path_info.dir().canonicalPath() != canonical_root ||
      QDir(canonical_root).filePath(expected_name) != path) {
    return false;
  }
  if (path_info.exists()) {
    return path_info.isFile() && path_info.canonicalFilePath() == path;
  }
  return !require_existing_file;
}

inline QVariantMap timeline_mcp_export_source_manifest(
    const QByteArray& snapshot_bytes,
    const QJsonObject& range = {}) {
  QJsonParseError parse_error;
  const auto parsed = QJsonDocument::fromJson(snapshot_bytes, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !parsed.isObject()) {
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("The Timeline export snapshot is not valid JSON.")}};
  }
  const auto tracks = parsed.object().value(QStringLiteral("tracks")).toArray();
  if (tracks.isEmpty()) {
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("The Timeline export snapshot has no tracks.")}};
  }
  auto range_start = 0.0;
  auto range_end = std::numeric_limits<double>::infinity();
  if (!range.isEmpty()) {
    auto start_frame = qint64{0};
    auto end_frame_exclusive = qint64{0};
    auto frame_rate = 0.0;
    if (!timeline_mcp_export_review_range(
            range, &start_frame, &end_frame_exclusive, &frame_rate)) {
      return {{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("The Timeline review range metadata is invalid.")}};
    }
    range_start = static_cast<double>(start_frame) / frame_rate;
    range_end = static_cast<double>(end_frame_exclusive) / frame_rate;
  }
  QMap<QString, QVariantMap> sources;
  QSet<QString> primary_paths;
  const auto add_stat_dependency = [&sources](const QString& path) {
    const QFileInfo info(path);
    if (path.isEmpty() || !info.exists() || info.isSymLink() || !info.isFile() ||
        !info.isReadable() || info.canonicalFilePath() != path || info.size() <= 0) {
      return false;
    }
    const auto existing = sources.constFind(path);
    if (existing != sources.cend()) {
      if (existing->value(QStringLiteral("sha256")).isValid()) {
        return existing->value(QStringLiteral("fileSize")).toLongLong() == info.size();
      }
      return existing->value(QStringLiteral("fileSize")).toLongLong() == info.size() &&
             existing->value(QStringLiteral("modifiedMs")).toLongLong() ==
                 info.lastModified().toMSecsSinceEpoch();
    }
    sources.insert(path, QVariantMap{
        {QStringLiteral("path"), path},
        {QStringLiteral("fileSize"), static_cast<qint64>(info.size())},
        {QStringLiteral("modifiedMs"), info.lastModified().toMSecsSinceEpoch()},
        {QStringLiteral("identityKind"), QStringLiteral("stat-v1")},
    });
    return true;
  };
  for (const auto& track_value : tracks) {
    const auto track = track_value.toObject();
    const auto clips = track.value(QStringLiteral("clips")).toArray();
    for (const auto& clip_value : clips) {
      const auto clip = clip_value.toObject();
      const auto media_type = clip.value(QStringLiteral("mediaType")).toString();
      if (timeline_source_is_generated(media_type)) continue;
      if (!range.isEmpty()) {
        const auto clip_start = clip.value(QStringLiteral("startSeconds")).toDouble(-1.0);
        auto clip_duration =
            clip.value(QStringLiteral("trimEndSeconds")).toDouble(-1.0) -
            clip.value(QStringLiteral("trimStartSeconds")).toDouble(-1.0);
        const auto playback_rate = clip.value(QStringLiteral("playbackRate")).toDouble(1.0);
        const auto clip_keyframes = clip.value(QStringLiteral("keyframes")).toArray();
        const auto has_animated_retime = std::any_of(
            clip_keyframes.cbegin(), clip_keyframes.cend(),
            [](const QJsonValue& keyframe_value) {
              return keyframe_value.toObject().value(QStringLiteral("parameter")).toString() ==
                     QStringLiteral("playbackRate");
            });
        if (std::isfinite(playback_rate) && playback_rate > 0.0) {
          clip_duration /= qMax(0.25, playback_rate);
        }
        if (!std::isfinite(clip_duration) || clip_duration <= 0.0) {
          clip_duration = clip.value(QStringLiteral("durationSeconds")).toDouble(-1.0);
        }
        if (!has_animated_retime && std::isfinite(clip_start) && clip_start >= 0.0 &&
            std::isfinite(clip_duration) && clip_duration > 0.0) {
          auto render_start = clip_start;
          if (track.value(QStringLiteral("kind")).toString() == QStringLiteral("video") &&
              clip.value(QStringLiteral("transitionInName")).toString().compare(
                  QStringLiteral("Cross Dissolve"), Qt::CaseInsensitive) == 0) {
            render_start = qMax(
                0.0, clip_start - clip.value(QStringLiteral("transitionInSeconds")).toDouble());
          }
          const auto clip_end = clip_start + clip_duration;
          if (clip_end <= range_start + 1.0e-9 || render_start >= range_end - 1.0e-9) {
            continue;
          }
        }
      }
      const auto path = clip.value(QStringLiteral("filePath")).toString();
      const auto source_hash = clip.value(QStringLiteral("sourceContentSha256")).toString();
      auto expected_size = qint64{0};
      if (!timeline_mcp_export_json_integer(
              clip.value(QStringLiteral("sourceFileSize")), &expected_size)) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Every file-backed Timeline source must have an exact integer file size.")}};
      }
      const QFileInfo info(path);
      if (path.isEmpty() || !info.exists() || info.isSymLink() || !info.isFile() ||
          !info.isReadable() || info.canonicalFilePath() != path || info.size() <= 0 ||
          expected_size != info.size() || !timeline_mcp_export_canonical_sha256(source_hash)) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Every file-backed Timeline source must be present, canonical, and fully verified before export.")}};
      }
      const auto existing = sources.constFind(path);
      if (existing != sources.cend()) {
        if (existing->value(QStringLiteral("fileSize")).toLongLong() != expected_size ||
            (existing->value(QStringLiteral("sha256")).isValid() &&
             existing->value(QStringLiteral("sha256")).toString() != source_hash)) {
          return {{QStringLiteral("ok"), false},
                  {QStringLiteral("error"), QStringLiteral("One Timeline source has conflicting recorded integrity data.")}};
        }
      }
      sources.insert(path, QVariantMap{
          {QStringLiteral("path"), path},
          {QStringLiteral("fileSize"), expected_size},
          {QStringLiteral("sha256"), source_hash},
          {QStringLiteral("identityKind"), QStringLiteral("sha256-v1")},
      });
      primary_paths.insert(path);
      const auto source_matte = clip.value(QStringLiteral("sourceMatte")).toObject();
      const auto matte_path = source_matte.value(QStringLiteral("filePath")).toString();
      const auto color_lut_path = clip.value(QStringLiteral("colorLutPath")).toString();
      const auto color_lut_enabled =
          clip.value(QStringLiteral("colorLutEnabled")).toBool(true);
      const auto compound_document_path =
          clip.value(QStringLiteral("compoundDocumentPath")).toString();
      if ((source_matte.value(QStringLiteral("enabled")).toBool() &&
           !add_stat_dependency(matte_path)) ||
          (color_lut_enabled && !color_lut_path.isEmpty() &&
           !add_stat_dependency(color_lut_path)) ||
          (!compound_document_path.isEmpty() &&
           !add_stat_dependency(compound_document_path))) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("A file-backed dependency of an in-range Timeline clip is missing or could not be fingerprinted.")}};
      }
    }
  }
  QVariantList source_records;
  source_records.reserve(sources.size());
  for (auto it = sources.cbegin(); it != sources.cend(); ++it) {
    source_records.push_back(it.value());
  }
  return {{QStringLiteral("ok"), true},
          {QStringLiteral("sourceCount"), source_records.size()},
          {QStringLiteral("primarySourceCount"), primary_paths.size()},
          {QStringLiteral("sources"), source_records}};
}

inline QVariantMap timeline_mcp_export_verify_source_manifest(
    const QVariantList& sources,
    const std::function<bool()>& canceled = {},
    const std::function<void(const QVariantMap&)>& progress = {},
    const QString& phase = QStringLiteral("source-verification")) {
  QVariantList verified_sources;
  auto verified_count = 0;
  const auto total_source_count = sources.size();
  auto total_bytes = qint64{0};
  for (const auto& source_value : sources) {
    const auto size = source_value.toMap().value(QStringLiteral("fileSize")).toLongLong();
    if (size > 0 && size <= std::numeric_limits<qint64>::max() - total_bytes) {
      total_bytes += size;
    }
  }
  auto bytes_read = qint64{0};
  auto next_progress_bytes = qint64{256} * 1024 * 1024;
  const auto publish_progress = [&]() {
    if (!progress) return;
    const auto percent = total_bytes > 0
        ? qBound(0, static_cast<int>(100.0 * static_cast<double>(bytes_read) /
                                     static_cast<double>(total_bytes)), 100)
        : 100;
    progress(QVariantMap{
        {QStringLiteral("stage"), phase},
        {QStringLiteral("phase"), QStringLiteral("verifying-source-media")},
        {QStringLiteral("progressPercent"), percent},
        {QStringLiteral("sourceCount"), total_source_count},
        {QStringLiteral("verifiedSourceCount"), verified_count},
        {QStringLiteral("bytesRead"), bytes_read},
        {QStringLiteral("totalBytes"), total_bytes},
    });
  };
  publish_progress();
  const auto fail = [&verified_sources, &verified_count, total_source_count,
                     &bytes_read, total_bytes, &phase](
                        const QString& code,
                        const QString& error,
                        bool canceled = false) {
    return QVariantMap{
        {QStringLiteral("ok"), false},
        {QStringLiteral("canceled"), canceled},
        {QStringLiteral("errorCode"), code},
        {QStringLiteral("error"), error},
        {QStringLiteral("sourceCount"), total_source_count},
        {QStringLiteral("verifiedSourceCount"), verified_count},
        {QStringLiteral("bytesRead"), bytes_read},
        {QStringLiteral("totalBytes"), total_bytes},
        {QStringLiteral("stage"), phase},
        {QStringLiteral("verifiedSources"), verified_sources},
    };
  };
  for (const auto& source_value : sources) {
    if (canceled && canceled()) {
      return fail(QStringLiteral("canceled"),
                  QStringLiteral("Timeline export canceled while verifying source media."), true);
    }
    auto source = source_value.toMap();
    const auto path = source.value(QStringLiteral("path")).toString();
    const auto expected_size = source.value(QStringLiteral("fileSize")).toLongLong();
    const auto expected_hash = source.value(QStringLiteral("sha256")).toString();
    const auto identity_kind = source.value(QStringLiteral("identityKind")).toString();
    const auto expected_modified_ms = source.value(QStringLiteral("modifiedMs")).toLongLong();
    const QFileInfo info(path);
    if (!info.exists() || info.isSymLink() || !info.isFile() || !info.isReadable() ||
        info.canonicalFilePath() != path || info.size() != expected_size ||
        (identity_kind == QStringLiteral("sha256-v1") &&
         !timeline_mcp_export_canonical_sha256(expected_hash)) ||
        (identity_kind == QStringLiteral("stat-v1") &&
         (expected_modified_ms <= 0 ||
          info.lastModified().toMSecsSinceEpoch() != expected_modified_ms)) ||
        (identity_kind != QStringLiteral("sha256-v1") &&
         identity_kind != QStringLiteral("stat-v1"))) {
      return fail(QStringLiteral("source_identity_changed"),
                  QStringLiteral("A Timeline source changed after export was queued."));
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      return fail(QStringLiteral("source_unreadable"),
                  QStringLiteral("A Timeline source could not be read for checksum verification."));
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
      if (canceled && canceled()) {
        return fail(QStringLiteral("canceled"),
                    QStringLiteral("Timeline export canceled while verifying source media."), true);
      }
      const auto chunk = file.read(1024 * 1024);
      if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
        return fail(QStringLiteral("source_unreadable"),
                    QStringLiteral("A Timeline source could not be read for checksum verification."));
      }
      hash.addData(chunk);
      bytes_read += chunk.size();
      if (bytes_read >= next_progress_bytes) {
        publish_progress();
        next_progress_bytes = bytes_read + 256LL * 1024LL * 1024LL;
      }
    }
    const auto digest = QString::fromLatin1(hash.result().toHex());
    const QFileInfo after_hash(path);
    if (after_hash.size() != expected_size ||
        after_hash.lastModified().toMSecsSinceEpoch() != info.lastModified().toMSecsSinceEpoch() ||
        (identity_kind == QStringLiteral("stat-v1") &&
         after_hash.lastModified().toMSecsSinceEpoch() != expected_modified_ms)) {
      return fail(QStringLiteral("source_identity_changed"),
                  QStringLiteral("A Timeline source changed while its checksum was being verified."));
    }
    if (identity_kind == QStringLiteral("sha256-v1") && digest != expected_hash) {
      return fail(QStringLiteral("source_checksum_mismatch"),
                  QStringLiteral("A Timeline source no longer matches its recorded checksum."));
    }
    source.insert(QStringLiteral("sha256"), digest);
    source.insert(QStringLiteral("identityKind"), QStringLiteral("sha256-v1"));
    source.remove(QStringLiteral("modifiedMs"));
    verified_sources.push_back(source);
    ++verified_count;
    publish_progress();
  }
  return {{QStringLiteral("ok"), true},
          {QStringLiteral("errorCode"), QString{}},
          {QStringLiteral("sourceCount"), sources.size()},
          {QStringLiteral("verifiedSourceCount"), verified_count},
          {QStringLiteral("bytesRead"), bytes_read},
          {QStringLiteral("totalBytes"), total_bytes},
          {QStringLiteral("stage"), phase},
          {QStringLiteral("verifiedSources"), verified_sources}};
}

inline bool timeline_mcp_export_owned_job(
    const PersistentJobRecord& job,
    const QString& resource_id,
    const QString& request_id,
    const QString& document_hash,
    const QString& documents_root,
    const QString& renders_root,
    QString* error_message = nullptr) {
  const auto fail = [error_message](const QString& message) {
    if (error_message != nullptr) *error_message = message;
    return false;
  };
  const auto timeline_id = resource_id.startsWith(QStringLiteral("timeline:"))
                               ? resource_id.mid(QStringLiteral("timeline:").size())
                               : QString{};
  if (!timeline_mcp_export_canonical_uuid(timeline_id) ||
      !timeline_mcp_export_canonical_uuid(request_id) ||
      !timeline_mcp_export_canonical_sha256(document_hash) ||
      !timeline_mcp_export_canonical_uuid(job.job_id) ||
      job.job_kind != QStringLiteral("timeline_render") ||
      job.asset_id != resource_id ||
      !job.metadata_json.value(QStringLiteral("standaloneMcpExport")).toBool()) {
    return fail(QStringLiteral("The requested job is not the exact Timeline export receipt.") );
  }
  const auto metadata = job.metadata_json.value(QStringLiteral("mcpTimelineExport")).toObject();
  const auto has_range = metadata.contains(QStringLiteral("range"));
  const auto range = metadata.value(QStringLiteral("range")).toObject();
  auto range_start_frame = qint64{0};
  auto range_end_frame_exclusive = qint64{0};
  auto range_frame_rate = 0.0;
  const auto range_valid = !has_range ||
      (metadata.value(QStringLiteral("range")).isObject() &&
       timeline_mcp_export_review_range(
           range, &range_start_frame, &range_end_frame_exclusive, &range_frame_rate));
  auto maximum_output_bytes = qint64{0};
  auto estimated_output_bytes = qint64{0};
  auto snapshot_bytes = qint64{0};
  const auto numeric_limits_valid =
      timeline_mcp_export_json_integer(
          metadata.value(QStringLiteral("maximumOutputBytes")), &maximum_output_bytes) &&
      timeline_mcp_export_json_integer(
          metadata.value(QStringLiteral("estimatedOutputBytes")), &estimated_output_bytes) &&
      timeline_mcp_export_json_integer(
          metadata.value(QStringLiteral("snapshotBytes")), &snapshot_bytes);
  const auto canonical_documents_root = QFileInfo(documents_root).canonicalFilePath();
  const auto canonical_renders_root = QFileInfo(renders_root).canonicalFilePath();
  if (canonical_documents_root.isEmpty() || canonical_renders_root.isEmpty()) {
    return fail(QStringLiteral("The app-managed Timeline export folders are unavailable."));
  }
  const auto snapshot_root = QDir(canonical_documents_root).filePath(QStringLiteral("render-snapshots"));
  const auto expected_snapshot_path = QDir(snapshot_root).filePath(
      QStringLiteral("%1-%2.json").arg(timeline_id, request_id));
  const auto expected_output_path = QDir(canonical_renders_root).filePath(
      QStringLiteral("%1-%2.mp4").arg(timeline_id, request_id));
  if (!QFileInfo(snapshot_root).isDir() ||
      metadata.value(QStringLiteral("schema")).toString() != QString::fromLatin1(kTimelineMcpExportSchema) ||
      metadata.value(QStringLiteral("origin")).toString() != QString::fromLatin1(kTimelineMcpExportOrigin) ||
      metadata.value(QStringLiteral("requestId")).toString() != request_id ||
      !timeline_mcp_export_canonical_uuid(metadata.value(QStringLiteral("requestId")).toString()) ||
      metadata.value(QStringLiteral("resourceId")).toString() != resource_id ||
      metadata.value(QStringLiteral("timelineId")).toString() != timeline_id ||
      metadata.value(QStringLiteral("snapshotRoot")).toString() != snapshot_root ||
      metadata.value(QStringLiteral("outputRoot")).toString() != canonical_renders_root ||
      metadata.value(QStringLiteral("documentHash")).toString() != document_hash ||
      !range_valid ||
      metadata.value(QStringLiteral("requestFingerprint")).toString() !=
          timeline_mcp_export_fingerprint(
              request_id, resource_id, document_hash, canonical_renders_root, range) ||
      !numeric_limits_valid || maximum_output_bytes != timeline_mcp_export_maximum_output_bytes(range) ||
      metadata.value(QStringLiteral("container")).toString() != QStringLiteral("mp4") ||
      metadata.value(QStringLiteral("codec")).toString() != QStringLiteral("h264") ||
      job.metadata_json.value(QStringLiteral("exportContainer")).toString() != QStringLiteral("mp4") ||
      job.metadata_json.value(QStringLiteral("exportCodec")).toString() != QStringLiteral("h264") ||
      estimated_output_bytes <= 0 || estimated_output_bytes > maximum_output_bytes ||
      snapshot_bytes <= 0 || snapshot_bytes > kMaximumTimelineMcpExportSnapshotBytes ||
      !timeline_mcp_export_canonical_sha256(metadata.value(QStringLiteral("snapshotSha256")).toString()) ||
      job.source_path != expected_snapshot_path ||
      metadata.value(QStringLiteral("snapshotPath")).toString() != expected_snapshot_path ||
      !timeline_mcp_export_path_is_generated_child(
          expected_snapshot_path, snapshot_root,
          QStringLiteral("%1-%2.json").arg(timeline_id, request_id)) ||
      job.output_path != expected_output_path ||
      metadata.value(QStringLiteral("outputPath")).toString() != expected_output_path ||
      !timeline_mcp_export_path_is_generated_child(
          expected_output_path, canonical_renders_root,
          QStringLiteral("%1-%2.mp4").arg(timeline_id, request_id))) {
    return fail(QStringLiteral("The persisted Timeline export receipt failed its ownership or path checks."));
  }
  return true;
}

inline QVariantMap timeline_mcp_export_receipt(
    const PersistentJobRecord& job,
    bool idempotent_replay = false) {
  const auto metadata = job.metadata_json.value(QStringLiteral("mcpTimelineExport")).toObject();
  QVariantMap result{
      {QStringLiteral("accepted"), true},
      {QStringLiteral("jobId"), job.job_id},
      {QStringLiteral("requestId"), metadata.value(QStringLiteral("requestId")).toString()},
      {QStringLiteral("resourceId"), metadata.value(QStringLiteral("resourceId")).toString()},
      {QStringLiteral("documentHash"), metadata.value(QStringLiteral("documentHash")).toString()},
      {QStringLiteral("snapshotSha256"), metadata.value(QStringLiteral("snapshotSha256")).toString()},
      {QStringLiteral("snapshotBytes"), metadata.value(QStringLiteral("snapshotBytes")).toVariant()},
      {QStringLiteral("outputPath"), job.output_path},
      {QStringLiteral("status"), job.status},
      {QStringLiteral("progress"), job.progress},
      {QStringLiteral("stage"), job.metadata_json.value(QStringLiteral("stage")).toString()},
      {QStringLiteral("maximumOutputBytes"), metadata.value(QStringLiteral("maximumOutputBytes")).toVariant()},
      {QStringLiteral("estimatedOutputBytes"), metadata.value(QStringLiteral("estimatedOutputBytes")).toVariant()},
      {QStringLiteral("renderKind"), metadata.contains(QStringLiteral("range"))
              ? QStringLiteral("review-excerpt") : QStringLiteral("full-timeline")},
      {QStringLiteral("idempotentReplay"), idempotent_replay},
  };
  if (metadata.contains(QStringLiteral("range"))) {
    result.insert(QStringLiteral("renderRange"), metadata.value(QStringLiteral("range")).toObject().toVariantMap());
  }
  if (!job.error_message.isEmpty()) result.insert(QStringLiteral("errorMessage"), job.error_message);
  const auto integrity = job.metadata_json.value(QStringLiteral("outputIntegrity")).toObject();
  if (!integrity.isEmpty()) result.insert(QStringLiteral("outputIntegrity"), integrity.toVariantMap());
  const auto review_verification =
      job.metadata_json.value(QStringLiteral("reviewOutputVerification")).toObject();
  if (!review_verification.isEmpty()) {
    result.insert(QStringLiteral("reviewOutputVerification"), review_verification.toVariantMap());
  }
  const auto source_verification =
      job.metadata_json.value(QStringLiteral("sourceVerificationDiagnostic")).toObject();
  if (!source_verification.isEmpty()) {
    result.insert(QStringLiteral("sourceVerification"), source_verification.toVariantMap());
  }
  const auto runtime_diagnostic =
      job.metadata_json.value(QStringLiteral("runtimeDiagnostic")).toObject();
  if (!runtime_diagnostic.isEmpty()) {
    result.insert(QStringLiteral("runtimeDiagnostic"), runtime_diagnostic.toVariantMap());
  }
  return result;
}

}  // namespace photo_editor::desktop
