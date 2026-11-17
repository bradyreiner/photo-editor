#include "timeline_review_output_verification.h"
#include "timeline_bounded_ffmpeg_memory_policy.h"

#include <QJsonParseError>
#include <QTextStream>

using photo_editor::desktop::timeline_review::verify_excerpt_probe_document;
using photo_editor::desktop::timeline_motion::bounded_h264::
    rational_frame_clock_filter;

namespace {

QJsonDocument make_probe(double video_duration,
                         const QString& average_frame_rate,
                         int decoded_frames) {
  const auto json = QStringLiteral(
      R"json({
        "format":{"format_name":"mov,mp4,m4a,3gp,3g2,mj2","duration":"0.533000"},
        "streams":[
          {"codec_type":"video","codec_name":"h264","width":1920,"height":1080,
           "avg_frame_rate":"%1","start_time":"0.000000","duration":"%2",
           "nb_read_frames":"%3"},
          {"codec_type":"audio","codec_name":"aac","channels":2,"sample_rate":"48000",
           "start_time":"0.000000","duration":"0.533000"}
        ]
      })json")
                        .arg(average_frame_rate)
                        .arg(QString::number(video_duration, 'f', 6))
                        .arg(decoded_frames);
  QJsonParseError error;
  const auto parsed = QJsonDocument::fromJson(json.toUtf8(), &error);
  return error.error == QJsonParseError::NoError ? parsed : QJsonDocument{};
}

bool contains_check(const QVariantMap& result, const QString& expected) {
  for (const auto& value : result.value(QStringLiteral("failedChecks")).toList()) {
    if (value.toString() == expected) return true;
  }
  return false;
}

}  // namespace

int main() {
  QTextStream error(stderr);
  if (rational_frame_clock_filter(30.0) !=
          QStringLiteral("settb=expr=1/30,setpts=N") ||
      rational_frame_clock_filter(29.97) !=
          QStringLiteral("settb=expr=1/2997,setpts=N*100") ||
      rational_frame_clock_filter(23.976) !=
          QStringLiteral("settb=expr=1/2997,setpts=N*125")) {
    error << "A stored frame rate should map to its reduced rational frame clock.\n";
    return 1;
  }

  const auto failed_window = verify_excerpt_probe_document(
      make_probe(0.533000, QStringLiteral("16000/533"), 16),
      1290, 1306, 30.0, 1920, 1080);
  if (failed_window.value(QStringLiteral("ok")).toBool() ||
      !contains_check(failed_window, QStringLiteral("frameRate")) ||
      failed_window.value(QStringLiteral("decodedFrameCount")).toLongLong() != 16 ||
      failed_window.value(QStringLiteral("expectedFrameCount")).toLongLong() != 16 ||
      failed_window.value(QStringLiteral("videoDurationSeconds")).toDouble() != 0.533 ||
      failed_window.value(QStringLiteral("width")).toInt() != 1920 ||
      failed_window.value(QStringLiteral("height")).toInt() != 1080) {
    error << "The failed frame-rate probe should retain its complete media measurements.\n";
    return 1;
  }

  const auto valid_window = verify_excerpt_probe_document(
      make_probe(0.533333, QStringLiteral("30/1"), 16),
      1290, 1306, 30.0, 1920, 1080);
  if (!valid_window.value(QStringLiteral("ok")).toBool() ||
      valid_window.contains(QStringLiteral("failedChecks")) ||
      valid_window.value(QStringLiteral("frameRate")).toDouble() != 30.0) {
    error << "A correctly clocked H.264/AAC window should satisfy the unchanged excerpt contract.\n";
    return 1;
  }

  return 0;
}
