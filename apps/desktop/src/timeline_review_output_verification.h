#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>
#include <limits>

namespace photo_editor::desktop::timeline_review {

// Keep the complete ffprobe measurements when a review excerpt is rejected.
// The worker can then report the failed media contract without retaining or
// publishing the rejected render artifact.
[[nodiscard]] inline QVariantMap verify_excerpt_probe_document(
    const QJsonDocument& parsed,
    qint64 start_frame,
    qint64 end_frame_exclusive,
    double expected_frame_rate,
    int expected_width,
    int expected_height) {
  const auto fail_parse = [](const QString& error) {
    return QVariantMap{{QStringLiteral("ok"), false},
                       {QStringLiteral("error"), error}};
  };
  if (!parsed.isObject() || end_frame_exclusive <= start_frame ||
      !std::isfinite(expected_frame_rate) || expected_frame_rate <= 0.0 ||
      expected_width <= 0 || expected_height <= 0) {
    return fail_parse(QStringLiteral("ffprobe returned invalid review excerpt metadata."));
  }

  const auto document = parsed.object();
  const auto format = document.value(QStringLiteral("format")).toObject();
  const auto format_name = format.value(QStringLiteral("format_name")).toString();
  bool container_duration_ok = false;
  const auto container_duration =
      format.value(QStringLiteral("duration")).toString().toDouble(&container_duration_ok);
  auto video = QJsonObject{};
  auto audio = QJsonObject{};
  for (const auto& stream_value : document.value(QStringLiteral("streams")).toArray()) {
    const auto stream = stream_value.toObject();
    const auto type = stream.value(QStringLiteral("codec_type")).toString();
    if (type == QStringLiteral("video") && video.isEmpty()) video = stream;
    if (type == QStringLiteral("audio") && audio.isEmpty()) audio = stream;
  }
  const auto parse_finite_number = [](const QJsonObject& object,
                                      const QString& key,
                                      bool* ok = nullptr) {
    bool parsed_ok = false;
    const auto value = object.value(key).toString().toDouble(&parsed_ok);
    parsed_ok = parsed_ok && std::isfinite(value);
    if (ok != nullptr) *ok = parsed_ok;
    return parsed_ok ? value : std::numeric_limits<double>::quiet_NaN();
  };
  const auto parse_frame_rate = [](const QString& value) {
    const auto parts = value.split(QLatin1Char('/'));
    if (parts.size() != 2) return std::numeric_limits<double>::quiet_NaN();
    bool numerator_ok = false;
    bool denominator_ok = false;
    const auto numerator = parts.at(0).toDouble(&numerator_ok);
    const auto denominator = parts.at(1).toDouble(&denominator_ok);
    if (!numerator_ok || !denominator_ok || denominator == 0.0 ||
        !std::isfinite(numerator) || !std::isfinite(denominator)) {
      return std::numeric_limits<double>::quiet_NaN();
    }
    return numerator / denominator;
  };

  bool frame_count_ok = false;
  const auto decoded_frames = video.value(QStringLiteral("nb_read_frames"))
                                 .toString()
                                 .toLongLong(&frame_count_ok);
  const auto expected_frames = end_frame_exclusive - start_frame;
  const auto probed_frame_rate = parse_frame_rate(
      video.value(QStringLiteral("avg_frame_rate")).toString());
  bool video_start_ok = false;
  bool audio_start_ok = false;
  bool video_duration_ok = false;
  bool audio_duration_ok = false;
  const auto video_start = parse_finite_number(
      video, QStringLiteral("start_time"), &video_start_ok);
  const auto audio_start = parse_finite_number(
      audio, QStringLiteral("start_time"), &audio_start_ok);
  const auto video_duration = parse_finite_number(
      video, QStringLiteral("duration"), &video_duration_ok);
  const auto audio_duration = parse_finite_number(
      audio, QStringLiteral("duration"), &audio_duration_ok);
  const auto expected_duration =
      static_cast<double>(expected_frames) / expected_frame_rate;
  const auto start_skew = std::abs(video_start - audio_start);
  const auto tolerance = 1.0 / expected_frame_rate;
  const auto audio_channels = audio.value(QStringLiteral("channels")).toInt(-1);
  const auto video_width = video.value(QStringLiteral("width")).toInt();
  const auto video_height = video.value(QStringLiteral("height")).toInt();
  const auto video_codec = video.value(QStringLiteral("codec_name")).toString();
  const auto audio_codec = audio.value(QStringLiteral("codec_name")).toString();
  const auto has_mp4_container =
      format_name.split(QLatin1Char(',')).contains(QStringLiteral("mp4"));

  QVariantList failed_checks;
  const auto add_failed_check = [&failed_checks](const QString& name, bool passed) {
    if (!passed) failed_checks.push_back(name);
  };
  add_failed_check(QStringLiteral("container"), has_mp4_container);
  add_failed_check(QStringLiteral("videoCodec"), video_codec == QStringLiteral("h264"));
  add_failed_check(QStringLiteral("audioCodec"), audio_codec == QStringLiteral("aac"));
  add_failed_check(QStringLiteral("audioChannels"), audio_channels == 2);
  add_failed_check(QStringLiteral("sequenceGeometry"),
                   video_width == expected_width && video_height == expected_height);
  add_failed_check(QStringLiteral("exactFrameCount"),
                   frame_count_ok && decoded_frames == expected_frames);
  add_failed_check(QStringLiteral("frameRate"),
                   std::isfinite(probed_frame_rate) &&
                       std::abs(probed_frame_rate - expected_frame_rate) <= 1.0e-4);
  add_failed_check(QStringLiteral("videoStart"),
                   video_start_ok && std::abs(video_start) <= tolerance * 0.5);
  add_failed_check(QStringLiteral("avStartAlignment"),
                   video_start_ok && audio_start_ok && start_skew <= tolerance * 0.5);
  add_failed_check(QStringLiteral("videoDuration"),
                   video_duration_ok &&
                       std::abs(video_duration - expected_duration) <= tolerance * 1.1);
  add_failed_check(QStringLiteral("audioDuration"),
                   audio_duration_ok &&
                       audio_duration >= expected_duration - tolerance &&
                       audio_duration <= expected_duration + 0.30);
  add_failed_check(QStringLiteral("containerDuration"),
                   container_duration_ok && std::isfinite(container_duration) &&
                       container_duration >= expected_duration - tolerance &&
                       container_duration <= expected_duration + 0.30);

  const auto ok = failed_checks.isEmpty();
  QVariantMap result{
      {QStringLiteral("ok"), ok},
      {QStringLiteral("formatName"), format_name},
      {QStringLiteral("videoCodec"), video_codec},
      {QStringLiteral("audioCodec"), audio_codec},
      {QStringLiteral("audioChannels"), audio_channels},
      {QStringLiteral("width"), video_width},
      {QStringLiteral("height"), video_height},
      {QStringLiteral("startFrame"), start_frame},
      {QStringLiteral("endFrameExclusive"), end_frame_exclusive},
      {QStringLiteral("expectedFrameCount"), expected_frames},
      {QStringLiteral("decodedFrameCount"), frame_count_ok ? decoded_frames : -1},
      {QStringLiteral("expectedFrameRate"), expected_frame_rate},
      {QStringLiteral("probedFrameRate"), probed_frame_rate},
      {QStringLiteral("videoStartTimeSeconds"), video_start},
      {QStringLiteral("audioStartTimeSeconds"), audio_start},
      {QStringLiteral("avStartSkewSeconds"), start_skew},
      {QStringLiteral("videoDurationSeconds"), video_duration},
      {QStringLiteral("audioDurationSeconds"), audio_duration},
      {QStringLiteral("containerDurationSeconds"), container_duration},
  };
  if (!ok) {
    result.insert(
        QStringLiteral("error"),
        QStringLiteral("The excerpt failed MP4/H.264/AAC, sequence geometry, exact frame count, frame-rate, or audio alignment verification."));
    result.insert(QStringLiteral("failedChecks"), failed_checks);
  } else {
    result.insert(QStringLiteral("frameRate"), expected_frame_rate);
  }
  return result;
}

}  // namespace photo_editor::desktop::timeline_review
