#include "timeline_interchange.h"

#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <QUrl>
#include <QUuid>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <limits>

namespace photo_editor::desktop::timeline_interchange {
namespace {

constexpr double kMaximumTimelineSeconds = 100000000.0;

void append_unique(QStringList* values, const QString& value) {
  if (values != nullptr && !value.isEmpty() && !values->contains(value)) {
    values->push_back(value);
  }
}

bool has_nonempty_array(const QJsonObject& object, const QString& key) {
  return object.value(key).isArray() && !object.value(key).toArray().isEmpty();
}

bool has_nonempty_object(const QJsonObject& object, const QString& key) {
  return object.value(key).isObject() && !object.value(key).toObject().isEmpty();
}

bool rational_seconds(const QJsonValue& value, double* seconds) {
  if (seconds == nullptr || !value.isObject()) {
    return false;
  }
  const auto object = value.toObject();
  if (object.value(QStringLiteral("OTIO_SCHEMA")).toString() !=
      QStringLiteral("RationalTime.1")) {
    return false;
  }
  const auto count = object.value(QStringLiteral("value")).toDouble(
      std::numeric_limits<double>::quiet_NaN());
  const auto rate = object.value(QStringLiteral("rate")).toDouble(
      std::numeric_limits<double>::quiet_NaN());
  if (!std::isfinite(count) || !std::isfinite(rate) || rate <= 0.0) {
    return false;
  }
  const auto converted = count / rate;
  if (!std::isfinite(converted) || converted < 0.0 ||
      converted > kMaximumTimelineSeconds) {
    return false;
  }
  *seconds = converted;
  return true;
}

QJsonObject rational_time(double seconds, double rate) {
  return QJsonObject{
      {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("RationalTime.1")},
      {QStringLiteral("value"), seconds * rate},
      {QStringLiteral("rate"), rate},
  };
}

QJsonObject time_range(double start_seconds, double duration_seconds, double rate) {
  return QJsonObject{
      {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("TimeRange.1")},
      {QStringLiteral("start_time"), rational_time(start_seconds, rate)},
      {QStringLiteral("duration"), rational_time(duration_seconds, rate)},
  };
}

bool parse_time_range(
    const QJsonValue& value,
    double* start_seconds,
    double* duration_seconds) {
  if (start_seconds == nullptr || duration_seconds == nullptr ||
      !value.isObject()) {
    return false;
  }
  const auto object = value.toObject();
  if (object.value(QStringLiteral("OTIO_SCHEMA")).toString() !=
      QStringLiteral("TimeRange.1") ||
      !rational_seconds(object.value(QStringLiteral("start_time")), start_seconds) ||
      !rational_seconds(object.value(QStringLiteral("duration")), duration_seconds) ||
      *duration_seconds <= 0.0) {
    return false;
  }
  return *start_seconds + *duration_seconds <= kMaximumTimelineSeconds;
}

QString media_type_for_path(const QString& path, bool audio_track) {
  if (audio_track) {
    return QStringLiteral("audio");
  }
  const auto suffix = QFileInfo(path).suffix().toLower();
  static const QSet<QString> audio_extensions{
      QStringLiteral("aac"), QStringLiteral("aif"), QStringLiteral("aiff"),
      QStringLiteral("alac"), QStringLiteral("flac"), QStringLiteral("m4a"),
      QStringLiteral("mp3"), QStringLiteral("ogg"), QStringLiteral("opus"),
      QStringLiteral("wav"), QStringLiteral("wma")};
  static const QSet<QString> video_extensions{
      QStringLiteral("3gp"), QStringLiteral("avi"), QStringLiteral("m2t"),
      QStringLiteral("m2ts"), QStringLiteral("m4v"), QStringLiteral("mkv"),
      QStringLiteral("mov"), QStringLiteral("mp4"), QStringLiteral("mpeg"),
      QStringLiteral("mpg"), QStringLiteral("mts"), QStringLiteral("webm"),
      QStringLiteral("wmv")};
  static const QSet<QString> image_extensions{
      QStringLiteral("arw"), QStringLiteral("bmp"), QStringLiteral("cr2"),
      QStringLiteral("cr3"), QStringLiteral("dng"), QStringLiteral("exr"),
      QStringLiteral("gif"), QStringLiteral("heic"), QStringLiteral("jpeg"),
      QStringLiteral("jpg"), QStringLiteral("nef"), QStringLiteral("png"),
      QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("webp")};
  if (audio_extensions.contains(suffix)) {
    return QStringLiteral("audio");
  }
  if (video_extensions.contains(suffix)) {
    return QStringLiteral("video");
  }
  if (image_extensions.contains(suffix)) {
    return QStringLiteral("photo");
  }
  return {};
}

QString resolve_target_url(
    const QString& target,
    const QString& otio_source_path,
    QString* error) {
  const auto trimmed = target.trimmed();
  if (trimmed.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("The external media reference has an empty target_url.");
    }
    return {};
  }
  const QUrl url(trimmed);
  QString resolved;
  if (url.isLocalFile()) {
    resolved = url.toLocalFile();
  } else if (url.scheme().isEmpty()) {
    resolved = QUrl::fromPercentEncoding(trimmed.toUtf8());
    if (QDir::isRelativePath(resolved)) {
      resolved = QDir(QFileInfo(otio_source_path).absolutePath()).filePath(resolved);
    }
  } else {
    if (error != nullptr) {
      *error = QStringLiteral("Non-local OTIO target_url is not supported: %1").arg(trimmed);
    }
    return {};
  }
  const QFileInfo info(resolved);
  return QDir::cleanPath(info.absoluteFilePath());
}

QJsonObject clip_media_reference(const QJsonObject& clip) {
  const auto schema = clip.value(QStringLiteral("OTIO_SCHEMA")).toString();
  if (schema == QStringLiteral("Clip.1")) {
    return clip.value(QStringLiteral("media_reference")).toObject();
  }
  if (schema != QStringLiteral("Clip.2")) {
    return {};
  }
  const auto references = clip.value(QStringLiteral("media_references")).toObject();
  auto active_key = clip.value(QStringLiteral("active_media_reference_key")).toString().trimmed();
  if (active_key.isEmpty()) {
    active_key = QStringLiteral("DEFAULT_MEDIA");
  }
  return references.value(active_key).toObject();
}

QJsonObject external_reference(
    const QString& target_path,
    const QString& otio_output_path) {
  const auto output_directory = QFileInfo(otio_output_path).absolutePath();
  auto relative_target = QDir(output_directory).relativeFilePath(target_path);
  relative_target.replace(QLatin1Char('\\'), QLatin1Char('/'));
  const auto target_url = QString::fromLatin1(
      QUrl::toPercentEncoding(relative_target, QByteArray("/")));
  return QJsonObject{
      {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("ExternalReference.1")},
      {QStringLiteral("name"), QFileInfo(target_path).fileName()},
      {QStringLiteral("target_url"), target_url},
      {QStringLiteral("available_range"), QJsonValue::Null},
      {QStringLiteral("available_image_bounds"), QJsonValue::Null},
      {QStringLiteral("metadata"), QJsonObject{}},
  };
}

void report_native_clip_loss(const QJsonObject& clip, QStringList* losses) {
  const auto name = clip.value(QStringLiteral("displayLabel")).toString(
      clip.value(QStringLiteral("clipId")).toString(QStringLiteral("Unnamed clip")));
  const auto note = [losses, &name](const QString& field) {
    append_unique(losses, QStringLiteral("%1: %2 is outside the OTIO cut/trim subset.").arg(name, field));
  };
  if (has_nonempty_array(clip, QStringLiteral("effectStack")) ||
      has_nonempty_object(clip, QStringLiteral("effectSettings"))) {
    note(QStringLiteral("effects and grades"));
  }
  if (clip.value(QStringLiteral("playbackRate")).toDouble(1.0) != 1.0 ||
      has_nonempty_array(clip, QStringLiteral("keyframes"))) {
    note(QStringLiteral("retime/keyframes"));
  }
  if (clip.value(QStringLiteral("transitionInSeconds")).toDouble() > 0.0 ||
      clip.value(QStringLiteral("transitionOutSeconds")).toDouble() > 0.0 ||
      !clip.value(QStringLiteral("transitionInName")).toString().isEmpty() ||
      !clip.value(QStringLiteral("transitionOutName")).toString().isEmpty()) {
    note(QStringLiteral("transitions"));
  }
  if (clip.value(QStringLiteral("frameMode")).toString(QStringLiteral("fit")) !=
          QStringLiteral("fit") ||
      clip.value(QStringLiteral("frameScale")).toDouble(1.0) != 1.0 ||
      clip.value(QStringLiteral("frameOffsetX")).toDouble() != 0.0 ||
      clip.value(QStringLiteral("frameOffsetY")).toDouble() != 0.0 ||
      clip.value(QStringLiteral("frameRotationDegrees")).toDouble() != 0.0 ||
      clip.value(QStringLiteral("framePerspectiveX")).toDouble() != 0.0 ||
      clip.value(QStringLiteral("framePerspectiveY")).toDouble() != 0.0 ||
      clip.value(QStringLiteral("sourceCropLeft")).toDouble() != 0.0 ||
      clip.value(QStringLiteral("sourceCropTop")).toDouble() != 0.0 ||
      clip.value(QStringLiteral("sourceCropRight")).toDouble(1.0) != 1.0 ||
      clip.value(QStringLiteral("sourceCropBottom")).toDouble(1.0) != 1.0) {
    note(QStringLiteral("framing and source crop"));
  }
  if (clip.value(QStringLiteral("audioGainDb")).toDouble() != 0.0 ||
      clip.value(QStringLiteral("fadeInSeconds")).toDouble() != 0.0 ||
      clip.value(QStringLiteral("fadeOutSeconds")).toDouble() != 0.0) {
    note(QStringLiteral("audio gain and fades"));
  }
  if (!clip.value(QStringLiteral("colorLutPath")).toString().isEmpty() ||
      clip.value(QStringLiteral("colorLutEnabled")).toBool() ||
      !clip.value(QStringLiteral("colorGradeRecipe")).toObject().isEmpty() ||
      clip.value(QStringLiteral("colorGradeRecipeEnabled")).toBool() ||
      !clip.value(QStringLiteral("videoInputTransform")).toString().isEmpty() ||
      !clip.value(QStringLiteral("videoInputTransformSource")).toString().isEmpty() ||
      !clip.value(QStringLiteral("sourceColorMatrix")).toString().isEmpty() ||
      !clip.value(QStringLiteral("sourceColorRange")).toString().isEmpty() ||
      !clip.value(QStringLiteral("sourceColorTransfer")).toString().isEmpty() ||
      !clip.value(QStringLiteral("sourceColorPrimaries")).toString().isEmpty() ||
      clip.value(QStringLiteral("shotMatchRedGain")).toDouble(1.0) != 1.0 ||
      clip.value(QStringLiteral("shotMatchGreenGain")).toDouble(1.0) != 1.0 ||
      clip.value(QStringLiteral("shotMatchBlueGain")).toDouble(1.0) != 1.0) {
    note(QStringLiteral("color LUTs and input transforms"));
  }
  if (clip.value(QStringLiteral("mediaType")).toString() == QStringLiteral("title") ||
      clip.value(QStringLiteral("mediaType")).toString() == QStringLiteral("caption") ||
      clip.value(QStringLiteral("mediaType")).toString() == QStringLiteral("adjustment")) {
    note(QStringLiteral("native generated/title/caption content"));
  }
  if (!clip.value(QStringLiteral("compoundDocumentPath")).toString().isEmpty()) {
    note(QStringLiteral("nested compound content"));
  }
  if (clip.value(QStringLiteral("stabilizationEnabled")).toBool() ||
      clip.value(QStringLiteral("rollingShutterEnabled")).toBool() ||
      has_nonempty_object(clip, QStringLiteral("motionTracking")) ||
      has_nonempty_object(clip, QStringLiteral("foregroundCutout"))) {
    note(QStringLiteral("stabilization and tracked masks"));
  }
  if (clip.value(QStringLiteral("opacity")).toDouble(1.0) != 1.0 ||
      !clip.value(QStringLiteral("titleText")).toString().isEmpty() ||
      !clip.value(QStringLiteral("captionWords")).toArray().isEmpty()) {
    note(QStringLiteral("opacity, titles, and captions"));
  }
  if (clip.value(QStringLiteral("audioDetached")).toBool() ||
      clip.value(QStringLiteral("audioChannelMode")).toString(QStringLiteral("source")) !=
          QStringLiteral("source") ||
      clip.value(QStringLiteral("audioSwapChannels")).toBool() ||
      clip.value(QStringLiteral("audioInvertLeftPhase")).toBool() ||
      clip.value(QStringLiteral("audioInvertRightPhase")).toBool() ||
      has_nonempty_object(clip, QStringLiteral("outputVariantOverrides"))) {
    note(QStringLiteral("audio linkage and output-variant overrides"));
  }
}

void report_otio_object_loss(const QJsonObject& object, const QString& label, QStringList* losses) {
  if (has_nonempty_array(object, QStringLiteral("effects"))) {
    append_unique(losses, QStringLiteral("%1 contains OTIO effects; effect processing will not be imported.").arg(label));
  }
  if (has_nonempty_array(object, QStringLiteral("markers"))) {
    append_unique(losses, QStringLiteral("%1 contains markers; markers are not imported in this subset.").arg(label));
  }
  if (has_nonempty_object(object, QStringLiteral("metadata"))) {
    append_unique(losses, QStringLiteral("%1 contains metadata; metadata is not imported in this subset.").arg(label));
  }
}

}  // namespace

ImportPlan preview_otio_import(
    const QByteArray& otio_json,
    const QString& otio_source_path,
    const QJsonObject& current_native_document) {
  ImportPlan plan;
  QJsonParseError parse_error;
  const auto parsed = QJsonDocument::fromJson(otio_json, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !parsed.isObject()) {
    plan.errors.push_back(QStringLiteral("Cannot parse OTIO JSON: %1").arg(parse_error.errorString()));
    return plan;
  }
  const auto timeline = parsed.object();
  if (timeline.value(QStringLiteral("OTIO_SCHEMA")).toString() !=
      QStringLiteral("Timeline.1")) {
    plan.errors.push_back(QStringLiteral("The OTIO root object must use Timeline.1."));
    return plan;
  }
  const auto stack = timeline.value(QStringLiteral("tracks")).toObject();
  if (stack.value(QStringLiteral("OTIO_SCHEMA")).toString() !=
          QStringLiteral("Stack.1") ||
      !stack.value(QStringLiteral("children")).isArray()) {
    plan.errors.push_back(QStringLiteral("Timeline.1 must contain a Stack.1 tracks object with children."));
    return plan;
  }

  report_otio_object_loss(timeline, QStringLiteral("Timeline"), &plan.lossy_fields);
  if (!timeline.value(QStringLiteral("global_start_time")).isNull() &&
      !timeline.value(QStringLiteral("global_start_time")).isUndefined()) {
    append_unique(&plan.lossy_fields, QStringLiteral("OTIO global_start_time is not represented in the native timeline."));
  }
  report_otio_object_loss(stack, QStringLiteral("Timeline tracks stack"), &plan.lossy_fields);
  if (!stack.value(QStringLiteral("source_range")).isNull() &&
      !stack.value(QStringLiteral("source_range")).isUndefined()) {
    append_unique(&plan.lossy_fields, QStringLiteral("The OTIO tracks stack source_range is not applied."));
  }

  QJsonArray native_tracks;
  QJsonArray otio_tracks = stack.value(QStringLiteral("children")).toArray();
  auto video_lane = 0;
  auto audio_lane = 1000;
  auto saw_audio_track = false;
  for (int track_index = 0; track_index < otio_tracks.size(); ++track_index) {
    if (!otio_tracks.at(track_index).isObject()) {
      append_unique(&plan.lossy_fields, QStringLiteral("A non-object OTIO track was skipped."));
      continue;
    }
    const auto otio_track = otio_tracks.at(track_index).toObject();
    if (otio_track.value(QStringLiteral("OTIO_SCHEMA")).toString() !=
        QStringLiteral("Track.1")) {
      append_unique(&plan.lossy_fields, QStringLiteral("A non-Track.1 object in the tracks stack was skipped."));
      continue;
    }
    report_otio_object_loss(otio_track, QStringLiteral("Track %1").arg(track_index + 1), &plan.lossy_fields);
    if (!otio_track.value(QStringLiteral("source_range")).isNull() &&
        !otio_track.value(QStringLiteral("source_range")).isUndefined()) {
      append_unique(&plan.lossy_fields, QStringLiteral("Track %1 source_range is not applied.").arg(track_index + 1));
    }
    const auto kind_text = otio_track.value(QStringLiteral("kind")).toString().toLower();
    const auto is_audio_track = kind_text == QStringLiteral("audio");
    if (!is_audio_track && kind_text != QStringLiteral("video")) {
      append_unique(&plan.lossy_fields, QStringLiteral("Track %1 kind '%2' is unsupported and was skipped.")
                                           .arg(track_index + 1).arg(kind_text));
      continue;
    }
    if (otio_track.value(QStringLiteral("enabled")).isBool() &&
        !otio_track.value(QStringLiteral("enabled")).toBool()) {
      append_unique(&plan.lossy_fields, QStringLiteral("Disabled track '%1' was omitted.")
                                           .arg(otio_track.value(QStringLiteral("name")).toString()));
      continue;
    }
    if (is_audio_track) {
      saw_audio_track = true;
    } else if (saw_audio_track) {
      append_unique(&plan.lossy_fields, QStringLiteral("OTIO interleaves video and audio tracks; the native lane model groups video lanes before independent audio lanes."));
    }
    const auto lane_index = is_audio_track ? audio_lane++ : video_lane++;
    const auto default_track_label = is_audio_track
                                         ? QStringLiteral("A%1").arg(lane_index - 999)
                                         : QStringLiteral("V%1").arg(lane_index + 1);

    QJsonObject native_track{
        {QStringLiteral("trackId"),
         QStringLiteral("otio-%1-%2").arg(is_audio_track ? QStringLiteral("audio") : QStringLiteral("video"))
             .arg(track_index + 1)},
        {QStringLiteral("displayLabel"),
         otio_track.value(QStringLiteral("name")).toString().trimmed().isEmpty()
             ? default_track_label
             : otio_track.value(QStringLiteral("name")).toString().left(48)},
        {QStringLiteral("kind"), is_audio_track ? QStringLiteral("audio") : QStringLiteral("video")},
        {QStringLiteral("role"), QStringLiteral("media")},
        {QStringLiteral("laneIndex"), lane_index},
        {QStringLiteral("independent"), is_audio_track},
        {QStringLiteral("visible"), true},
        {QStringLiteral("targeted"), !is_audio_track && lane_index == 0},
    };

    QJsonArray clips;
    auto cursor_seconds = 0.0;
    const auto advance_cursor = [&cursor_seconds, &plan, track_index](double duration) {
      if (!std::isfinite(duration) || duration < 0.0 ||
          cursor_seconds > kMaximumTimelineSeconds - duration) {
        append_unique(&plan.errors,
                      QStringLiteral("Track %1 cumulative timeline duration exceeds the %2-second import limit.")
                          .arg(track_index + 1)
                          .arg(kMaximumTimelineSeconds, 0, 'f', 0));
        return false;
      }
      cursor_seconds += duration;
      return true;
    };
    const auto children = otio_track.value(QStringLiteral("children")).toArray();
    for (int child_index = 0; child_index < children.size(); ++child_index) {
      if (!children.at(child_index).isObject()) {
        append_unique(&plan.lossy_fields, QStringLiteral("Track %1 child %2 is not an object and was skipped.")
                                             .arg(track_index + 1).arg(child_index + 1));
        continue;
      }
      const auto child = children.at(child_index).toObject();
      const auto schema = child.value(QStringLiteral("OTIO_SCHEMA")).toString();
      if (schema == QStringLiteral("Gap.1")) {
        double gap_start = 0.0;
        double gap_duration = 0.0;
        if (!parse_time_range(child.value(QStringLiteral("source_range")), &gap_start, &gap_duration)) {
          append_unique(&plan.errors, QStringLiteral("Track %1 contains a Gap.1 with an invalid source_range.").arg(track_index + 1));
          continue;
        }
        if (!advance_cursor(gap_duration)) {
          continue;
        }
        report_otio_object_loss(child, QStringLiteral("A gap"), &plan.lossy_fields);
        continue;
      }
      if (schema == QStringLiteral("Transition.1")) {
        append_unique(&plan.lossy_fields, QStringLiteral("Track %1 transition '%2' is omitted; adjacent clips become cuts.")
                                             .arg(track_index + 1)
                                             .arg(child.value(QStringLiteral("name")).toString()));
        continue;
      }
      if (schema != QStringLiteral("Clip.1") && schema != QStringLiteral("Clip.2")) {
        if (child.contains(QStringLiteral("source_range"))) {
          double ignored_start = 0.0;
          double ignored_duration = 0.0;
          if (parse_time_range(child.value(QStringLiteral("source_range")), &ignored_start, &ignored_duration)) {
            advance_cursor(ignored_duration);
          }
        }
        append_unique(&plan.lossy_fields, QStringLiteral("Track %1 item '%2' (%3) is unsupported and was skipped.")
                                             .arg(track_index + 1)
                                             .arg(child.value(QStringLiteral("name")).toString())
                                             .arg(schema.isEmpty() ? QStringLiteral("unknown schema") : schema));
        continue;
      }
      double source_start = 0.0;
      double clip_duration = 0.0;
      if (!parse_time_range(child.value(QStringLiteral("source_range")), &source_start, &clip_duration)) {
        append_unique(&plan.errors, QStringLiteral("Clip '%1' has an invalid or missing source_range.")
                                         .arg(child.value(QStringLiteral("name")).toString()));
        continue;
      }
      if (child.value(QStringLiteral("enabled")).isBool() &&
          !child.value(QStringLiteral("enabled")).toBool()) {
        append_unique(&plan.lossy_fields, QStringLiteral("Disabled clip '%1' was omitted while preserving its timeline duration.")
                                             .arg(child.value(QStringLiteral("name")).toString()));
        advance_cursor(clip_duration);
        continue;
      }
      report_otio_object_loss(child, QStringLiteral("Clip '%1'").arg(child.value(QStringLiteral("name")).toString()), &plan.lossy_fields);
      if (has_nonempty_object(child, QStringLiteral("metadata"))) {
        append_unique(&plan.lossy_fields, QStringLiteral("Clip '%1' metadata is not imported.")
                                             .arg(child.value(QStringLiteral("name")).toString()));
      }
      const auto reference = clip_media_reference(child);
      if (schema == QStringLiteral("Clip.2")) {
        const auto references = child.value(QStringLiteral("media_references")).toObject();
        auto active_key = child.value(QStringLiteral("active_media_reference_key")).toString().trimmed();
        if (active_key.isEmpty()) {
          active_key = QStringLiteral("DEFAULT_MEDIA");
        }
        auto alternate_reference_count = 0;
        for (auto iterator = references.constBegin(); iterator != references.constEnd(); ++iterator) {
          if (iterator.key() != active_key) {
            ++alternate_reference_count;
          }
        }
        if (alternate_reference_count > 0) {
          append_unique(&plan.lossy_fields,
                        QStringLiteral("Clip '%1' has %2 alternate media reference(s); only active key '%3' is imported.")
                            .arg(child.value(QStringLiteral("name")).toString())
                            .arg(alternate_reference_count)
                            .arg(active_key));
        }
      }
      if (reference.value(QStringLiteral("OTIO_SCHEMA")).toString() !=
          QStringLiteral("ExternalReference.1")) {
        append_unique(&plan.lossy_fields, QStringLiteral("Clip '%1' has no ExternalReference.1 and was omitted while preserving its timeline duration.")
                                             .arg(child.value(QStringLiteral("name")).toString()));
        advance_cursor(clip_duration);
        continue;
      }
      if (has_nonempty_object(reference, QStringLiteral("metadata"))) {
        append_unique(&plan.lossy_fields, QStringLiteral("Clip '%1' media-reference metadata is not imported.")
                                             .arg(child.value(QStringLiteral("name")).toString()));
      }
      QString path_error;
      const auto file_path = resolve_target_url(
          reference.value(QStringLiteral("target_url")).toString(), otio_source_path, &path_error);
      if (file_path.isEmpty()) {
        append_unique(&plan.lossy_fields, QStringLiteral("Clip '%1' was omitted: %2 The timeline duration is preserved as a gap.")
                                             .arg(child.value(QStringLiteral("name")).toString(), path_error));
        advance_cursor(clip_duration);
        continue;
      }
      const auto media_type = media_type_for_path(file_path, is_audio_track);
      if (media_type.isEmpty()) {
        append_unique(&plan.lossy_fields, QStringLiteral("Clip '%1' references an unsupported media extension and was omitted while preserving its timeline duration.")
                                             .arg(child.value(QStringLiteral("name")).toString()));
        advance_cursor(clip_duration);
        continue;
      }
      if (is_audio_track != (media_type == QStringLiteral("audio"))) {
        append_unique(&plan.lossy_fields, QStringLiteral("Clip '%1' media type does not match its %2 track and was omitted while preserving its timeline duration.")
                                             .arg(child.value(QStringLiteral("name")).toString(),
                                                  is_audio_track ? QStringLiteral("audio") : QStringLiteral("video")));
        advance_cursor(clip_duration);
        continue;
      }
      const QFileInfo media_info(file_path);
      if (!media_info.exists() || !media_info.isFile()) {
        append_unique(&plan.lossy_fields, QStringLiteral("Clip '%1' media is offline at %2; the source reference is retained for relinking.")
                                             .arg(child.value(QStringLiteral("name")).toString(), file_path));
      }
      const auto clip_start_seconds = cursor_seconds;
      if (!advance_cursor(clip_duration)) {
        continue;
      }
      const auto clip_name = child.value(QStringLiteral("name")).toString().trimmed();
      QJsonObject native_clip{
          {QStringLiteral("clipId"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
          {QStringLiteral("mediaType"), media_type},
          {QStringLiteral("filePath"), file_path},
          {QStringLiteral("sourceFileSize"), media_info.exists() ? static_cast<double>(media_info.size()) : 0.0},
          {QStringLiteral("displayLabel"), clip_name.isEmpty() ? media_info.completeBaseName() : clip_name},
          {QStringLiteral("durationSeconds"), clip_duration},
          {QStringLiteral("trimStartSeconds"), source_start},
          {QStringLiteral("trimEndSeconds"), source_start + clip_duration},
          {QStringLiteral("startSeconds"), clip_start_seconds},
          {QStringLiteral("playbackRate"), 1.0},
          {QStringLiteral("frameMode"), QStringLiteral("fit")},
          {QStringLiteral("frameScale"), 1.0},
          {QStringLiteral("sourceCropLeft"), 0.0},
          {QStringLiteral("sourceCropTop"), 0.0},
          {QStringLiteral("sourceCropRight"), 1.0},
          {QStringLiteral("sourceCropBottom"), 1.0},
          {QStringLiteral("hasAudio"), is_audio_track || media_type == QStringLiteral("audio")},
      };
      clips.push_back(native_clip);
      ++plan.imported_clips;
    }
    native_track.insert(QStringLiteral("clips"), clips);
    native_tracks.push_back(native_track);
    ++plan.imported_tracks;
  }
  if (plan.imported_tracks == 0 || plan.imported_clips == 0) {
    append_unique(&plan.errors, QStringLiteral("No supported referenced clips were found in the OTIO timeline."));
  }
  if (!plan.errors.isEmpty()) {
    return plan;
  }

  auto candidate = current_native_document;
  candidate.insert(QStringLiteral("tracks"), native_tracks);
  QJsonArray legacy_primary_clips;
  for (const auto& track_value : native_tracks) {
    const auto track = track_value.toObject();
    if (track.value(QStringLiteral("kind")).toString() == QStringLiteral("video")) {
      legacy_primary_clips = track.value(QStringLiteral("clips")).toArray();
      break;
    }
  }
  candidate.insert(QStringLiteral("clips"), legacy_primary_clips);
  candidate.insert(QStringLiteral("name"), timeline.value(QStringLiteral("name")).toString(
                                                current_native_document.value(QStringLiteral("name")).toString()));
  candidate.insert(QStringLiteral("markers"), QJsonArray{});
  candidate.insert(QStringLiteral("multicamGroups"), QJsonArray{});
  candidate.insert(QStringLiteral("reviewRanges"), QJsonArray{});
  candidate.insert(QStringLiteral("renderPath"), QString{});
  candidate.insert(QStringLiteral("posterPath"), QString{});
  candidate.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
  plan.native_document = candidate;
  plan.valid = true;
  return plan;
}

ExportPlan export_otio(const QJsonObject& native_document, const QString& otio_output_path) {
  ExportPlan plan;
  const auto tracks = native_document.value(QStringLiteral("tracks")).toArray();
  if (tracks.isEmpty()) {
    plan.errors.push_back(QStringLiteral("The native timeline has no tracks to export."));
    return plan;
  }
  const auto format = native_document.value(QStringLiteral("sequenceFormat")).toObject();
  const auto rate = format.value(QStringLiteral("frameRate")).toDouble(24.0);
  if (!std::isfinite(rate) || rate <= 0.0 || rate > 1000.0) {
    plan.errors.push_back(QStringLiteral("The native sequence frame rate is invalid for OTIO export."));
    return plan;
  }
  QJsonArray otio_tracks;
  for (int track_index = 0; track_index < tracks.size(); ++track_index) {
    if (!tracks.at(track_index).isObject()) {
      continue;
    }
    const auto track = tracks.at(track_index).toObject();
    const auto native_kind = track.value(QStringLiteral("kind")).toString().toLower();
    const auto is_audio_track = native_kind == QStringLiteral("audio");
    if (!is_audio_track && native_kind != QStringLiteral("video")) {
      append_unique(&plan.lossy_fields, QStringLiteral("Native track %1 has an unknown kind and was skipped.").arg(track_index + 1));
      continue;
    }
    if (is_audio_track && track.value(QStringLiteral("clips")).toArray().isEmpty()) {
      continue;
    }
    const auto track_label = track.value(QStringLiteral("customName")).toString().trimmed().isEmpty()
                                 ? track.value(QStringLiteral("displayLabel")).toString()
                                 : track.value(QStringLiteral("customName")).toString();
    QJsonArray children;
    auto cursor_seconds = 0.0;
    const auto source_clips = track.value(QStringLiteral("clips")).toArray();
    QVector<QJsonObject> clips;
    clips.reserve(source_clips.size());
    for (const auto& source_clip : source_clips) {
      if (source_clip.isObject()) {
        clips.push_back(source_clip.toObject());
      }
    }
    std::sort(clips.begin(), clips.end(), [](const QJsonObject& left, const QJsonObject& right) {
      return left.value(QStringLiteral("startSeconds")).toDouble() <
             right.value(QStringLiteral("startSeconds")).toDouble();
    });
    for (const auto& clip : clips) {
      report_native_clip_loss(clip, &plan.lossy_fields);
      const auto media_type = clip.value(QStringLiteral("mediaType")).toString().toLower();
      const auto file_path = clip.value(QStringLiteral("filePath")).toString().trimmed();
      if (media_type != QStringLiteral("video") && media_type != QStringLiteral("photo") &&
          media_type != QStringLiteral("audio")) {
        append_unique(&plan.lossy_fields, QStringLiteral("Clip '%1' is generated/native-only content and was omitted.")
                                             .arg(clip.value(QStringLiteral("displayLabel")).toString()));
        continue;
      }
      if (file_path.isEmpty()) {
        append_unique(&plan.lossy_fields, QStringLiteral("Clip '%1' has no source path and was omitted.")
                                             .arg(clip.value(QStringLiteral("displayLabel")).toString()));
        continue;
      }
      const auto trim_start = clip.value(QStringLiteral("trimStartSeconds")).toDouble();
      const auto trim_end = clip.value(QStringLiteral("trimEndSeconds")).toDouble();
      if (!std::isfinite(trim_start) || !std::isfinite(trim_end) ||
          trim_start < 0.0 || trim_end < 0.0 || trim_end < trim_start) {
        append_unique(&plan.errors, QStringLiteral("Clip '%1' has negative or reversed source timing.")
                                         .arg(clip.value(QStringLiteral("displayLabel")).toString()));
        continue;
      }
      const auto duration = trim_end > trim_start
                                ? trim_end - trim_start
                                : clip.value(QStringLiteral("durationSeconds")).toDouble();
      auto start = clip.value(QStringLiteral("startSeconds")).toDouble();
      if (!std::isfinite(start) || !std::isfinite(duration) || duration <= 0.0 ||
          duration > kMaximumTimelineSeconds || start < 0.0 ||
          start + duration > kMaximumTimelineSeconds ||
          trim_start + duration > kMaximumTimelineSeconds) {
        append_unique(&plan.errors, QStringLiteral("Clip '%1' has invalid timeline or source timing.")
                                         .arg(clip.value(QStringLiteral("displayLabel")).toString()));
        continue;
      }
      if (start > cursor_seconds) {
        children.push_back(QJsonObject{
            {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Gap.1")},
            {QStringLiteral("effects"), QJsonArray{}},
            {QStringLiteral("markers"), QJsonArray{}},
            {QStringLiteral("enabled"), true},
            {QStringLiteral("metadata"), QJsonObject{}},
            {QStringLiteral("name"), QStringLiteral("Gap")},
            {QStringLiteral("source_range"), time_range(0.0, start - cursor_seconds, rate)},
        });
        cursor_seconds = start;
      } else if (start + 1e-9 < cursor_seconds) {
        append_unique(&plan.lossy_fields, QStringLiteral("Track '%1' has overlapping clips; OTIO serializes them as a hard-cut sequence at the prior clip end.")
                                             .arg(track_label));
        start = cursor_seconds;
      }
      const auto normalized_path = QFileInfo(file_path).absoluteFilePath();
      const auto clip_name = clip.value(QStringLiteral("displayLabel")).toString().trimmed().isEmpty()
                                 ? QFileInfo(normalized_path).completeBaseName()
                                 : clip.value(QStringLiteral("displayLabel")).toString();
      const auto external = external_reference(normalized_path, otio_output_path);
      QJsonObject otio_clip{
          {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Clip.2")},
          {QStringLiteral("metadata"), QJsonObject{}},
          {QStringLiteral("name"), clip_name},
          {QStringLiteral("source_range"), time_range(trim_start, duration, rate)},
          {QStringLiteral("effects"), QJsonArray{}},
          {QStringLiteral("markers"), QJsonArray{}},
          {QStringLiteral("enabled"), true},
          {QStringLiteral("media_references"), QJsonObject{{QStringLiteral("DEFAULT_MEDIA"), external}}},
          {QStringLiteral("active_media_reference_key"), QStringLiteral("DEFAULT_MEDIA")},
      };
      children.push_back(otio_clip);
      ++plan.exported_clips;
      cursor_seconds = start + duration;
    }
    QJsonObject otio_track{
        {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Track.1")},
        {QStringLiteral("metadata"), QJsonObject{}},
        {QStringLiteral("name"), track_label.isEmpty()
                                      ? QStringLiteral("Track %1").arg(track_index + 1)
                                      : track_label},
        {QStringLiteral("kind"), is_audio_track ? QStringLiteral("Audio") : QStringLiteral("Video")},
        {QStringLiteral("children"), children},
        {QStringLiteral("effects"), QJsonArray{}},
        {QStringLiteral("markers"), QJsonArray{}},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("source_range"), QJsonValue::Null},
    };
    otio_tracks.push_back(otio_track);
    ++plan.exported_tracks;
    if (track.value(QStringLiteral("audioGainDb")).toDouble() != 0.0 ||
        track.value(QStringLiteral("audioPan")).toDouble() != 0.0 ||
        track.value(QStringLiteral("audioEqLowDb")).toDouble() != 0.0 ||
        track.value(QStringLiteral("audioEqMidDb")).toDouble() != 0.0 ||
        track.value(QStringLiteral("audioEqHighDb")).toDouble() != 0.0 ||
        track.value(QStringLiteral("audioEqEnabled")).toBool() ||
        track.value(QStringLiteral("audioCompressorEnabled")).toBool() ||
        track.value(QStringLiteral("audioLimiterEnabled")).toBool() ||
        track.value(QStringLiteral("audioGateEnabled")).toBool() ||
        track.value(QStringLiteral("audioDeEsserEnabled")).toBool() ||
        track.value(QStringLiteral("audioHighPassEnabled")).toBool() ||
        track.value(QStringLiteral("audioLowPassEnabled")).toBool() ||
        track.value(QStringLiteral("audioDuckingEnabled")).toBool() ||
        track.value(QStringLiteral("audioProcessingBypassed")).toBool() ||
        track.value(QStringLiteral("audioLoudnessMatchedBypass")).toBool() ||
        track.value(QStringLiteral("dialogueCleanupEnabled")).toBool() ||
        (is_audio_track &&
         (track.value(QStringLiteral("audioRole")).toString(QStringLiteral("dialogue")) !=
              QStringLiteral("dialogue") ||
          track.value(QStringLiteral("audioBus")).toString(QStringLiteral("main")) !=
              QStringLiteral("main"))) ||
        has_nonempty_array(track, QStringLiteral("automationKeyframes"))) {
      append_unique(&plan.lossy_fields, QStringLiteral("Track '%1' audio processing is outside the OTIO cut/trim subset.").arg(track_label));
    }
    if (track.value(QStringLiteral("locked")).toBool() || track.value(QStringLiteral("muted")).toBool() ||
        track.value(QStringLiteral("solo")).toBool() || !track.value(QStringLiteral("visible")).toBool(true)) {
      append_unique(&plan.lossy_fields, QStringLiteral("Track '%1' lock/mute/solo/visibility state is not exported.").arg(track_label));
    }
    if (track.value(QStringLiteral("opacity")).toDouble(1.0) != 1.0 ||
        track.value(QStringLiteral("groupTransformScale")).toDouble(1.0) != 1.0 ||
        track.value(QStringLiteral("groupTransformOffsetX")).toDouble() != 0.0 ||
        track.value(QStringLiteral("groupTransformOffsetY")).toDouble() != 0.0 ||
        track.value(QStringLiteral("groupTransformRotationDegrees")).toDouble() != 0.0 ||
        track.value(QStringLiteral("groupVideoHidden")).toBool()) {
      append_unique(&plan.lossy_fields, QStringLiteral("Track '%1' opacity, group transform, or group visibility is not exported.").arg(track_label));
    }
    if (!track.value(QStringLiteral("groupId")).toString().isEmpty() ||
        !track.value(QStringLiteral("groupParentId")).toString().isEmpty()) {
      append_unique(&plan.lossy_fields, QStringLiteral("Track '%1' group membership is not exported.").arg(track_label));
    }
  }
  if (plan.exported_clips == 0 || plan.exported_tracks == 0) {
    plan.errors.push_back(QStringLiteral("No referenced video/audio/photo clips can be exported from this timeline."));
    return plan;
  }
  if (!native_document.value(QStringLiteral("markers")).toArray().isEmpty()) {
    append_unique(&plan.lossy_fields, QStringLiteral("Timeline markers are not exported by the OTIO cut/trim subset."));
  }
  if (!native_document.value(QStringLiteral("multicamGroups")).toArray().isEmpty()) {
    append_unique(&plan.lossy_fields, QStringLiteral("Multicam groups and angle edits are not exported."));
  }
  if (format.value(QStringLiteral("width")).toInt() > 0 || format.value(QStringLiteral("height")).toInt() > 0) {
    append_unique(&plan.lossy_fields, QStringLiteral("Sequence dimensions and color/output settings are not represented by the OTIO subset."));
  }
  const auto root = QJsonObject{
      {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Timeline.1")},
      {QStringLiteral("metadata"), QJsonObject{}},
      {QStringLiteral("name"), native_document.value(QStringLiteral("name")).toString()},
      {QStringLiteral("global_start_time"), QJsonValue::Null},
      {QStringLiteral("tracks"), QJsonObject{
          {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Stack.1")},
          {QStringLiteral("metadata"), QJsonObject{}},
          {QStringLiteral("name"), QStringLiteral("tracks")},
          {QStringLiteral("source_range"), QJsonValue::Null},
          {QStringLiteral("effects"), QJsonArray{}},
          {QStringLiteral("markers"), QJsonArray{}},
          {QStringLiteral("enabled"), true},
          {QStringLiteral("children"), otio_tracks},
      }},
  };
  plan.otio_json = QJsonDocument(root).toJson(QJsonDocument::Indented);
  plan.valid = true;
  return plan;
}

}  // namespace photo_editor::desktop::timeline_interchange
