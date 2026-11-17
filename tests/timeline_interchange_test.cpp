#include "timeline_interchange.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrl>

#include <cmath>
#include <iostream>

using photo_editor::desktop::timeline_interchange::export_otio;
using photo_editor::desktop::timeline_interchange::preview_otio_import;

namespace {

bool check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

bool write_file(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QJsonObject rational(double value, double rate) {
  return {{QStringLiteral("OTIO_SCHEMA"), QStringLiteral("RationalTime.1")},
          {QStringLiteral("value"), value},
          {QStringLiteral("rate"), rate}};
}

QJsonObject range(double start, double duration, double rate) {
  return {{QStringLiteral("OTIO_SCHEMA"), QStringLiteral("TimeRange.1")},
          {QStringLiteral("start_time"), rational(start, rate)},
          {QStringLiteral("duration"), rational(duration, rate)}};
}

QJsonObject external_reference(const QString& target_url) {
  return {{QStringLiteral("OTIO_SCHEMA"), QStringLiteral("ExternalReference.1")},
          {QStringLiteral("name"), QStringLiteral("media")},
          {QStringLiteral("target_url"), target_url},
          {QStringLiteral("available_range"), QJsonValue::Null},
          {QStringLiteral("available_image_bounds"), QJsonValue::Null},
          {QStringLiteral("metadata"), QJsonObject{}}};
}

QJsonObject clip_v1(
    const QString& name,
    const QString& target_url,
    double source_start,
    double duration,
    double rate) {
  return {{QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Clip.1")},
          {QStringLiteral("name"), name},
          {QStringLiteral("source_range"), range(source_start, duration, rate)},
          {QStringLiteral("media_reference"), external_reference(target_url)},
          {QStringLiteral("effects"), QJsonArray{}},
          {QStringLiteral("markers"), QJsonArray{}},
          {QStringLiteral("enabled"), true},
          {QStringLiteral("metadata"), QJsonObject{}}};
}

QJsonObject clip_v2(
    const QString& name,
    const QString& target_url,
    double source_start,
    double duration,
    double rate) {
  return {{QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Clip.2")},
          {QStringLiteral("name"), name},
          {QStringLiteral("source_range"), range(source_start, duration, rate)},
          {QStringLiteral("media_references"),
           QJsonObject{{QStringLiteral("DEFAULT_MEDIA"), external_reference(target_url)}}},
          {QStringLiteral("active_media_reference_key"), QStringLiteral("DEFAULT_MEDIA")},
          {QStringLiteral("effects"), QJsonArray{}},
          {QStringLiteral("markers"), QJsonArray{}},
          {QStringLiteral("enabled"), true},
          {QStringLiteral("metadata"), QJsonObject{}}};
}

QJsonObject track(QString name, QString kind, QJsonArray children) {
  return {{QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Track.1")},
          {QStringLiteral("name"), name},
          {QStringLiteral("kind"), kind},
          {QStringLiteral("children"), children},
          {QStringLiteral("source_range"), QJsonValue::Null},
          {QStringLiteral("effects"), QJsonArray{}},
          {QStringLiteral("markers"), QJsonArray{}},
          {QStringLiteral("enabled"), true},
          {QStringLiteral("metadata"), QJsonObject{}}};
}

bool has_loss(const QStringList& losses, const QString& fragment) {
  for (const auto& loss : losses) {
    if (loss.contains(fragment, Qt::CaseInsensitive)) {
      return true;
    }
  }
  return false;
}

double seconds(const QJsonObject& time) {
  return time.value(QStringLiteral("value")).toDouble() /
         time.value(QStringLiteral("rate")).toDouble();
}

bool test_round_trip_subset_and_unicode_paths() {
  QTemporaryDir temporary;
  if (!check(temporary.isValid(), "interchange fixture directory is available")) {
    return false;
  }
  const auto media_path = QDir(temporary.path()).filePath(QStringLiteral("Crème brûlée 01.mov"));
  const auto second_media_path = QDir(temporary.path()).filePath(QStringLiteral("second.mov"));
  const auto audio_path = QDir(temporary.path()).filePath(QStringLiteral("field audio.wav"));
  const auto source_otio_path = QDir(temporary.path()).filePath(QStringLiteral("Editor Cut.otio"));
  const auto export_otio_path = QDir(temporary.path()).filePath(QStringLiteral("Exported Cut.otio"));
  if (!check(write_file(media_path, QByteArrayLiteral("video-one")) &&
                 write_file(second_media_path, QByteArrayLiteral("video-two")) &&
                 write_file(audio_path, QByteArrayLiteral("audio")),
             "media fixtures with spaces and non-ASCII names are written")) {
    return false;
  }

  const auto relative_media_url = QString::fromLatin1(
      QUrl::toPercentEncoding(QFileInfo(media_path).fileName(), QByteArray("/")));
  const auto otio_root = QJsonObject{
      {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Timeline.1")},
      {QStringLiteral("name"), QStringLiteral("Interchange Demo")},
      {QStringLiteral("metadata"), QJsonObject{}},
      {QStringLiteral("global_start_time"), QJsonValue::Null},
      {QStringLiteral("tracks"), QJsonObject{
          {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Stack.1")},
          {QStringLiteral("name"), QStringLiteral("tracks")},
          {QStringLiteral("children"), QJsonArray{
               track(QStringLiteral("V1"), QStringLiteral("Video"), QJsonArray{
                   QJsonObject{{QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Gap.1")},
                               {QStringLiteral("source_range"), range(0.0, 48.0, 24.0)}},
                   clip_v1(QStringLiteral("Crème brûlée shot"), relative_media_url, 24.0, 72.0, 24.0),
                   clip_v2(QStringLiteral("Second take"),
                           QUrl::fromLocalFile(second_media_path).toString(QUrl::FullyEncoded),
                           12.0, 24.0, 24.0)}),
               track(QStringLiteral("Production audio"), QStringLiteral("Audio"), QJsonArray{
                   clip_v2(QStringLiteral("Field recording"),
                           QUrl::fromLocalFile(audio_path).toString(QUrl::FullyEncoded),
                           0.0, 48.0, 24.0)})}},
          {QStringLiteral("effects"), QJsonArray{}},
          {QStringLiteral("markers"), QJsonArray{}},
          {QStringLiteral("metadata"), QJsonObject{}},
          {QStringLiteral("source_range"), QJsonValue::Null}}}};
  auto current_document = QJsonObject{
      {QStringLiteral("timelineId"), QStringLiteral("existing-timeline")},
      {QStringLiteral("name"), QStringLiteral("Old Timeline")},
      {QStringLiteral("sequenceFormat"), QJsonObject{
           {QStringLiteral("width"), 1920},
           {QStringLiteral("height"), 1080},
           {QStringLiteral("frameRate"), 30.0}}},
      {QStringLiteral("tracks"), QJsonArray{}},
      {QStringLiteral("clips"), QJsonArray{}},
      {QStringLiteral("markers"), QJsonArray{QJsonObject{{QStringLiteral("markerId"), QStringLiteral("old-marker")}}}},
      {QStringLiteral("multicamGroups"), QJsonArray{QJsonObject{{QStringLiteral("groupId"), QStringLiteral("old-group")}}}},
      {QStringLiteral("reviewRanges"), QJsonArray{QJsonObject{{QStringLiteral("rangeId"), QStringLiteral("old-range")}}}},
      {QStringLiteral("renderPath"), QStringLiteral("stale-render.mov")},
      {QStringLiteral("posterPath"), QStringLiteral("stale-poster.jpg")}};
  const auto original_document = current_document;
  const auto source_bytes = QJsonDocument(otio_root).toJson(QJsonDocument::Indented);
  const auto imported = preview_otio_import(source_bytes, source_otio_path, current_document);
  if (!check(imported.valid && imported.errors.isEmpty(), "supported OTIO subset produces a valid import plan") ||
      !check(imported.imported_tracks == 2 && imported.imported_clips == 3,
             "video/audio tracks and supported clip references are counted") ||
      !check(current_document == original_document, "preview does not mutate the current document") ||
      !check(imported.native_document.value(QStringLiteral("timelineId")).toString() ==
                 QStringLiteral("existing-timeline") &&
                 imported.native_document.value(QStringLiteral("name")).toString() ==
                 QStringLiteral("Interchange Demo"),
             "import candidate replaces timeline content while retaining current identity") ||
      !check(imported.native_document.value(QStringLiteral("markers")).toArray().isEmpty() &&
                 imported.native_document.value(QStringLiteral("multicamGroups")).toArray().isEmpty() &&
                 imported.native_document.value(QStringLiteral("reviewRanges")).toArray().isEmpty() &&
                 imported.native_document.value(QStringLiteral("renderPath")).toString().isEmpty(),
             "unsupported timeline annotations and stale renders are cleared explicitly")) {
    return false;
  }

  const auto native_tracks = imported.native_document.value(QStringLiteral("tracks")).toArray();
  const auto video_clips = native_tracks.at(0).toObject().value(QStringLiteral("clips")).toArray();
  const auto first = video_clips.at(0).toObject();
  const auto second = video_clips.at(1).toObject();
  const auto audio_track = native_tracks.at(1).toObject();
  const auto audio_clip = audio_track.value(QStringLiteral("clips")).toArray().at(0).toObject();
  if (!check(first.value(QStringLiteral("filePath")).toString() == QFileInfo(media_path).absoluteFilePath() &&
                 first.value(QStringLiteral("displayLabel")).toString() == QStringLiteral("Crème brûlée shot"),
             "relative percent-encoded media references resolve spaces and Unicode correctly") ||
      !check(std::abs(first.value(QStringLiteral("startSeconds")).toDouble() - 2.0) < 1e-9 &&
                 std::abs(first.value(QStringLiteral("trimStartSeconds")).toDouble() - 1.0) < 1e-9 &&
                 std::abs(first.value(QStringLiteral("trimEndSeconds")).toDouble() - 4.0) < 1e-9,
             "OTIO gap and RationalTime source trim preserve exact seconds") ||
      !check(std::abs(second.value(QStringLiteral("startSeconds")).toDouble() - 5.0) < 1e-9 &&
                 std::abs(second.value(QStringLiteral("trimStartSeconds")).toDouble() - 0.5) < 1e-9,
             "the second source trim follows the first clip on its track") ||
      !check(audio_track.value(QStringLiteral("kind")).toString() == QStringLiteral("audio") &&
                 audio_track.value(QStringLiteral("independent")).toBool() &&
                 audio_clip.value(QStringLiteral("filePath")).toString() == QFileInfo(audio_path).absoluteFilePath(),
             "audio track references remain independent and resolve correctly")) {
    return false;
  }

  const auto exported = export_otio(imported.native_document, export_otio_path);
  if (!check(exported.valid && exported.errors.isEmpty(), "native cut/trim timeline exports as OTIO JSON") ||
      !check(exported.exported_tracks == 2 && exported.exported_clips == 3,
             "export reports the exact supported track and clip counts")) {
    return false;
  }
  QJsonParseError parse_error;
  const auto exported_json = QJsonDocument::fromJson(exported.otio_json, &parse_error);
  if (!check(parse_error.error == QJsonParseError::NoError && exported_json.isObject() &&
                 exported_json.object().value(QStringLiteral("OTIO_SCHEMA")).toString() ==
                     QStringLiteral("Timeline.1"),
             "export follows the OTIO Timeline.1 root schema")) {
    return false;
  }
  const auto exported_tracks = exported_json.object().value(QStringLiteral("tracks")).toObject()
                                  .value(QStringLiteral("children")).toArray();
  const auto exported_video_children = exported_tracks.at(0).toObject()
                                           .value(QStringLiteral("children")).toArray();
  const auto exported_gap = exported_video_children.at(0).toObject();
  const auto exported_first = exported_video_children.at(1).toObject();
  const auto source_range = exported_first.value(QStringLiteral("source_range")).toObject();
  const auto media_references = exported_first.value(QStringLiteral("media_references")).toObject();
  const auto exported_reference = media_references.value(QStringLiteral("DEFAULT_MEDIA")).toObject();
  if (!check(exported_gap.value(QStringLiteral("OTIO_SCHEMA")).toString() == QStringLiteral("Gap.1") &&
                 std::abs(seconds(exported_gap.value(QStringLiteral("source_range")).toObject()
                                     .value(QStringLiteral("duration")).toObject()) - 2.0) < 1e-9,
             "export inserts an OTIO gap to preserve sequence start time") ||
      !check(exported_first.value(QStringLiteral("OTIO_SCHEMA")).toString() == QStringLiteral("Clip.2") &&
                 std::abs(seconds(source_range.value(QStringLiteral("start_time")).toObject()) - 1.0) < 1e-9 &&
                 std::abs(seconds(source_range.value(QStringLiteral("duration")).toObject()) - 3.0) < 1e-9,
             "Clip.2 export retains source trim as RationalTime values") ||
      !check(!exported_reference.value(QStringLiteral("target_url")).toString().startsWith(QStringLiteral("file:")) &&
                 exported_reference.value(QStringLiteral("target_url")).toString().contains(QStringLiteral("%20")) &&
                 exported_reference.value(QStringLiteral("target_url")).toString().contains(QStringLiteral("%C3")),
             "media references are relative URL paths with encoded spaces and Unicode")) {
    return false;
  }

  const auto reimported = preview_otio_import(
      exported.otio_json, export_otio_path, current_document);
  if (!check(reimported.valid && reimported.imported_tracks == 2 && reimported.imported_clips == 3,
             "exported OTIO reopens with the supported track and clip graph") ||
      !check(std::abs(reimported.native_document.value(QStringLiteral("tracks")).toArray()
                          .at(0).toObject().value(QStringLiteral("clips")).toArray()
                          .at(0).toObject().value(QStringLiteral("startSeconds")).toDouble() - 2.0) < 1e-9,
             "exported OTIO reopens with its sequence cut position")) {
    return false;
  }
  return true;
}

bool test_loss_report_and_invalid_ranges() {
  QTemporaryDir temporary;
  if (!check(temporary.isValid(), "loss-report fixture directory is available")) {
    return false;
  }
  const auto media_path = QDir(temporary.path()).filePath(QStringLiteral("supported.mov"));
  if (!check(write_file(media_path, QByteArrayLiteral("media")), "loss fixture media is written")) {
    return false;
  }
  auto supported_clip = clip_v1(
      QStringLiteral("Graded clip"), QUrl::fromLocalFile(media_path).toString(), 0.0, 24.0, 24.0);
  supported_clip.insert(QStringLiteral("effects"), QJsonArray{QJsonObject{
      {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Effect.1")},
      {QStringLiteral("name"), QStringLiteral("Color grade")}}});
  auto remote_clip = clip_v1(
      QStringLiteral("Remote clip"), QStringLiteral("https://example.invalid/source.mov"), 0.0, 24.0, 24.0);
  auto alternate_reference_clip = clip_v2(
      QStringLiteral("Alternate sources"), QUrl::fromLocalFile(media_path).toString(),
      0.0, 24.0, 24.0);
  auto alternate_references = alternate_reference_clip.value(QStringLiteral("media_references")).toObject();
  alternate_references.insert(QStringLiteral("PROXY_MEDIA"), external_reference(
      QUrl::fromLocalFile(media_path).toString()));
  alternate_reference_clip.insert(QStringLiteral("media_references"), alternate_references);
  const auto nested_track = track(QStringLiteral("Nested"), QStringLiteral("Video"), QJsonArray{});
  const auto lossy_root = QJsonObject{
      {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Timeline.1")},
      {QStringLiteral("name"), QStringLiteral("Loss report fixture")},
      {QStringLiteral("tracks"), QJsonObject{
          {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Stack.1")},
          {QStringLiteral("children"), QJsonArray{
               track(QStringLiteral("V1"), QStringLiteral("Video"), QJsonArray{
                   QJsonObject{{QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Transition.1")},
                               {QStringLiteral("name"), QStringLiteral("Dissolve")}},
                   nested_track,
                   supported_clip,
                   alternate_reference_clip,
                   remote_clip})}}}}};
  const auto result = preview_otio_import(
      QJsonDocument(lossy_root).toJson(), QDir(temporary.path()).filePath(QStringLiteral("in.otio")),
      QJsonObject{{QStringLiteral("timelineId"), QStringLiteral("losses")},
                  {QStringLiteral("sequenceFormat"), QJsonObject{{QStringLiteral("frameRate"), 24.0}}}});
  if (!check(result.valid && result.imported_clips == 2,
             "supported media remain importable when unsupported OTIO objects are present") ||
      !check(has_loss(result.lossy_fields, QStringLiteral("effect")) &&
                 has_loss(result.lossy_fields, QStringLiteral("transition")) &&
                 has_loss(result.lossy_fields, QStringLiteral("unsupported")) &&
                 has_loss(result.lossy_fields, QStringLiteral("Non-local")) &&
                 has_loss(result.lossy_fields, QStringLiteral("alternate media reference")),
             "effects, transitions, nested objects, remote references, and inactive source references receive explicit loss entries")) {
    return false;
  }

  auto invalid_root = lossy_root;
  auto invalid_tracks = invalid_root.value(QStringLiteral("tracks")).toObject();
  auto invalid_children = invalid_tracks.value(QStringLiteral("children")).toArray();
  auto invalid_track = invalid_children.first().toObject();
  auto invalid_items = invalid_track.value(QStringLiteral("children")).toArray();
  auto invalid_clip = supported_clip;
  invalid_clip.insert(QStringLiteral("source_range"), range(0.0, 1.0, 0.0));
  invalid_items.replace(2, invalid_clip);
  invalid_track.insert(QStringLiteral("children"), invalid_items);
  invalid_children.replace(0, invalid_track);
  invalid_tracks.insert(QStringLiteral("children"), invalid_children);
  invalid_root.insert(QStringLiteral("tracks"), invalid_tracks);
  const auto invalid = preview_otio_import(
      QJsonDocument(invalid_root).toJson(), QStringLiteral("invalid.otio"),
      QJsonObject{{QStringLiteral("timelineId"), QStringLiteral("invalid")}});
  if (!check(!invalid.valid && !invalid.errors.isEmpty(),
             "invalid rational rates block the candidate before any mutation")) {
    return false;
  }

  const auto overlong_track = track(
      QStringLiteral("Overlong track"), QStringLiteral("Video"), QJsonArray{
          clip_v2(QStringLiteral("First long clip"), QUrl::fromLocalFile(media_path).toString(),
                  0.0, 60000000.0, 1.0),
          clip_v2(QStringLiteral("Second long clip"), QUrl::fromLocalFile(media_path).toString(),
                  0.0, 60000000.0, 1.0)});
  const auto overlong_root = QJsonObject{
      {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Timeline.1")},
      {QStringLiteral("name"), QStringLiteral("Overlong cumulative timeline")},
      {QStringLiteral("tracks"), QJsonObject{
          {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Stack.1")},
          {QStringLiteral("children"), QJsonArray{overlong_track}}}}};
  const auto overlong = preview_otio_import(
      QJsonDocument(overlong_root).toJson(), QStringLiteral("overlong.otio"),
      QJsonObject{{QStringLiteral("timelineId"), QStringLiteral("overlong")},
                  {QStringLiteral("sequenceFormat"), QJsonObject{{QStringLiteral("frameRate"), 24.0}}}});
  if (!check(!overlong.valid &&
                 has_loss(overlong.errors, QStringLiteral("cumulative")) &&
                 has_loss(overlong.errors, QStringLiteral("import limit")),
             "individually valid clips cannot exceed the bounded cumulative track duration")) {
    return false;
  }

  auto unknown_root = lossy_root;
  unknown_root.insert(QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Timeline.2"));
  const auto unknown = preview_otio_import(
      QJsonDocument(unknown_root).toJson(), QStringLiteral("unknown.otio"), QJsonObject{});
  return check(!unknown.valid && has_loss(unknown.errors, QStringLiteral("Timeline.1")),
               "unsupported OTIO top-level schema is rejected explicitly");
}

bool test_fractional_frame_rate_round_trip() {
  QTemporaryDir temporary;
  if (!check(temporary.isValid(), "fractional-rate fixture directory is available")) {
    return false;
  }
  const auto media_path = QDir(temporary.path()).filePath(QStringLiteral("fractional.mov"));
  const auto output_path = QDir(temporary.path()).filePath(QStringLiteral("fractional.otio"));
  if (!check(write_file(media_path, QByteArrayLiteral("fractional media")),
             "fractional-rate media fixture is written")) {
    return false;
  }
  constexpr double rate = 24000.0 / 1001.0;
  const auto root = QJsonObject{
      {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Timeline.1")},
      {QStringLiteral("name"), QStringLiteral("23.976 cut")},
      {QStringLiteral("tracks"), QJsonObject{
          {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Stack.1")},
          {QStringLiteral("children"), QJsonArray{
               track(QStringLiteral("V1"), QStringLiteral("Video"), QJsonArray{
                   clip_v2(QStringLiteral("Fractional source"),
                           QUrl::fromLocalFile(media_path).toString(QUrl::FullyEncoded),
                           rate, rate * 3.0, rate)})}}}}};
  const QJsonObject native{
      {QStringLiteral("timelineId"), QStringLiteral("fractional-rate")},
      {QStringLiteral("sequenceFormat"), QJsonObject{
           {QStringLiteral("frameRate"), rate}}}};
  const auto imported = preview_otio_import(
      QJsonDocument(root).toJson(), QDir(temporary.path()).filePath(QStringLiteral("source.otio")), native);
  if (!check(imported.valid && imported.imported_clips == 1,
             "OTIO accepts a finite 24000/1001 rational time base") ||
      !check(std::abs(imported.native_document.value(QStringLiteral("tracks")).toArray()
                          .at(0).toObject().value(QStringLiteral("clips")).toArray()
                          .at(0).toObject().value(QStringLiteral("trimStartSeconds")).toDouble() - 1.0) < 1e-9 &&
                 std::abs(imported.native_document.value(QStringLiteral("tracks")).toArray()
                              .at(0).toObject().value(QStringLiteral("clips")).toArray()
                              .at(0).toObject().value(QStringLiteral("durationSeconds")).toDouble() - 3.0) < 1e-9,
             "fractional RationalTime values retain exact source seconds")) {
    return false;
  }
  const auto exported = export_otio(imported.native_document, output_path);
  if (!check(exported.valid, "fractional sequence rate exports to OTIO")) {
    return false;
  }
  const auto exported_clip = QJsonDocument::fromJson(exported.otio_json).object()
                                 .value(QStringLiteral("tracks")).toObject()
                                 .value(QStringLiteral("children")).toArray().at(0).toObject()
                                 .value(QStringLiteral("children")).toArray().at(0).toObject();
  const auto source_range = exported_clip.value(QStringLiteral("source_range")).toObject();
  const auto duration = source_range.value(QStringLiteral("duration")).toObject();
  return check(std::abs(duration.value(QStringLiteral("rate")).toDouble() - rate) < 1e-12 &&
                   std::abs(seconds(duration) - 3.0) < 1e-9,
               "export uses the finite fractional sequence rate and retains duration");
}

bool test_export_loss_report_and_negative_timing() {
  QTemporaryDir temporary;
  if (!check(temporary.isValid(), "native export-loss fixture directory is available")) {
    return false;
  }
  const auto media_path = QDir(temporary.path()).filePath(QStringLiteral("graded.mov"));
  if (!check(write_file(media_path, QByteArrayLiteral("media")),
             "native export-loss media fixture is written")) {
    return false;
  }
  const auto make_document = [](const QJsonObject& clip, const QJsonObject& track_overrides) {
    auto track_object = QJsonObject{
        {QStringLiteral("kind"), QStringLiteral("video")},
        {QStringLiteral("displayLabel"), QStringLiteral("V1")},
        {QStringLiteral("clips"), QJsonArray{clip}}};
    for (auto iterator = track_overrides.constBegin(); iterator != track_overrides.constEnd(); ++iterator) {
      track_object.insert(iterator.key(), iterator.value());
    }
    return QJsonObject{
        {QStringLiteral("name"), QStringLiteral("Native loss fixture")},
        {QStringLiteral("sequenceFormat"), QJsonObject{{QStringLiteral("frameRate"), 24.0}}},
        {QStringLiteral("tracks"), QJsonArray{track_object}}};
  };
  auto clip = QJsonObject{
      {QStringLiteral("clipId"), QStringLiteral("graded-clip")},
      {QStringLiteral("displayLabel"), QStringLiteral("Graded clip")},
      {QStringLiteral("mediaType"), QStringLiteral("video")},
      {QStringLiteral("filePath"), media_path},
      {QStringLiteral("startSeconds"), 0.0},
      {QStringLiteral("durationSeconds"), 24.0},
      {QStringLiteral("trimStartSeconds"), 0.0},
      {QStringLiteral("trimEndSeconds"), 24.0}};
  clip.insert(QStringLiteral("shotMatchRedGain"), 1.25);
  clip.insert(QStringLiteral("sourceColorTransfer"), QStringLiteral("smpte2084"));
  clip.insert(QStringLiteral("audioDetached"), true);
  clip.insert(QStringLiteral("outputVariantOverrides"), QJsonObject{{QStringLiteral("portrait"), QJsonObject{}}});
  const QJsonObject track_overrides{
      {QStringLiteral("opacity"), 0.5},
      {QStringLiteral("groupTransformScale"), 1.25},
      {QStringLiteral("groupVideoHidden"), true},
      {QStringLiteral("groupId"), QStringLiteral("group-1")},
      {QStringLiteral("dialogueCleanupEnabled"), true}};
  const auto result = export_otio(
      make_document(clip, track_overrides),
      QDir(temporary.path()).filePath(QStringLiteral("loss-report.otio")));
  if (!check(result.valid, "native media still exports when lossy state is reported") ||
      !check(has_loss(result.lossy_fields, QStringLiteral("color")) &&
                 has_loss(result.lossy_fields, QStringLiteral("audio linkage")) &&
                 has_loss(result.lossy_fields, QStringLiteral("output-variant")) &&
                 has_loss(result.lossy_fields, QStringLiteral("audio processing")) &&
                 has_loss(result.lossy_fields, QStringLiteral("opacity")) &&
                 has_loss(result.lossy_fields, QStringLiteral("group membership")),
             "color overrides, audio linkage, variants, dialogue processing, and track transforms receive explicit loss entries")) {
    return false;
  }

  auto negative_start_clip = clip;
  negative_start_clip.insert(QStringLiteral("startSeconds"), -0.25);
  const auto negative_start = export_otio(
      make_document(negative_start_clip, {}),
      QDir(temporary.path()).filePath(QStringLiteral("negative-start.otio")));
  auto reversed_trim_clip = clip;
  reversed_trim_clip.insert(QStringLiteral("trimStartSeconds"), 2.0);
  reversed_trim_clip.insert(QStringLiteral("trimEndSeconds"), 1.0);
  const auto reversed_trim = export_otio(
      make_document(reversed_trim_clip, {}),
      QDir(temporary.path()).filePath(QStringLiteral("reversed-trim.otio")));
  return check(!negative_start.valid && has_loss(negative_start.errors, QStringLiteral("invalid")) &&
                   !reversed_trim.valid && has_loss(reversed_trim.errors, QStringLiteral("reversed")),
               "negative timeline starts and reversed source trims are rejected instead of silently clamped");
}

}  // namespace

int main() {
  if (!test_round_trip_subset_and_unicode_paths() ||
      !test_loss_report_and_invalid_ranges() ||
      !test_export_loss_report_and_negative_timing() ||
      !test_fractional_frame_rate_round_trip()) {
    return 1;
  }
  std::cout << "timeline_interchange_subset=passed\n";
  return 0;
}
