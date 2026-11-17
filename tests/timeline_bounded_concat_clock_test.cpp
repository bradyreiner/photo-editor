#include "timeline_bounded_concat_clock.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <iostream>
#include <limits>

using photo_editor::desktop::timeline_motion::bounded_h264::concat_frame_duration_directive;

namespace {
QByteArray run(const QString& executable, const QStringList& arguments) {
  QProcess process;
  process.start(executable, arguments);
  if (!process.waitForStarted(5000) || !process.waitForFinished(30000) ||
      process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    std::cerr << process.readAllStandardError().toStdString();
    return {};
  }
  return process.readAllStandardOutput();
}

QList<QByteArray> frame_hashes(const QString& ffmpeg, const QString& path) {
  const auto text = run(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"),
      QStringLiteral("-i"), path, QStringLiteral("-map"), QStringLiteral("0:v:0"),
      QStringLiteral("-f"), QStringLiteral("framemd5"), QStringLiteral("-")});
  QList<QByteArray> hashes;
  for (const auto& line : text.split('\n')) {
    if (!line.isEmpty() && !line.startsWith('#')) {
      hashes.push_back(line.split(',').last().trimmed());
    }
  }
  return hashes;
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  for (const auto fps : {24.0, 30.0, 60.0, 120.0, 240.0, 30000.0 / 1001.0, 23.976}) {
    qint64 sum = 0;
    constexpr qint64 end = 90001;
    for (qint64 start = 0; start < end; start += 16) {
      auto directive = concat_frame_duration_directive(start, qMin(start + 16, end), fps);
      directive.remove(QStringLiteral("duration "));
      directive.remove(QLatin1Char('.'));
      bool ok = false;
      sum += directive.toLongLong(&ok);
      if (!ok) return 1;
    }
    if (sum != std::llround(static_cast<long double>(end) * 1000000.0L / fps)) {
      std::cerr << "Cumulative frame duration drifted\n";
      return 1;
    }
  }
  for (const auto fps : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                        std::numeric_limits<double>::quiet_NaN()}) {
    if (!concat_frame_duration_directive(0, 1, fps).isEmpty()) return 1;
  }
  if (!concat_frame_duration_directive(-1, 1, 30).isEmpty() ||
      !concat_frame_duration_directive(1, 1, 30).isEmpty() ||
      !concat_frame_duration_directive(0, std::numeric_limits<qint64>::max(), 0.1).isEmpty()) return 1;

  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  if (ffmpeg.isEmpty() || ffprobe.isEmpty()) return 77;
  QTemporaryDir directory;
  if (!directory.isValid()) return 1;
  QStringList playlist{QStringLiteral("ffconcat version 1.0")};
  QList<QByteArray> expected;
  for (int index = 0; index < 9; ++index) {
    const auto name = QStringLiteral("chunk-%1.mp4").arg(index);
    const auto path = QDir(directory.path()).filePath(name);
    run(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
        QStringLiteral("testsrc2=s=64x48:r=30,hue=h=%1").arg(index * 31),
        QStringLiteral("-frames:v"), QStringLiteral("16"),
        QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-bf"), QStringLiteral("3"),
        QStringLiteral("-threads"), QStringLiteral("1"),
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        QStringLiteral("-video_track_timescale"), QStringLiteral("240000"), path});
    const auto hashes = frame_hashes(ffmpeg, path);
    if (hashes.size() != 16) return 1;
    expected.append(hashes);
    playlist.push_back(QStringLiteral("file '%1'").arg(name));
    playlist.push_back(concat_frame_duration_directive(index * 16, (index + 1) * 16, 30));
  }
  const auto concat_path = QDir(directory.path()).filePath(QStringLiteral("video.ffconcat"));
  QFile concat(concat_path);
  const auto bytes = (playlist.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
  if (!concat.open(QIODevice::WriteOnly) || concat.write(bytes) != bytes.size()) return 1;
  concat.close();
  const auto joined = QDir(directory.path()).filePath(QStringLiteral("joined.mp4"));
  run(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-f"),
      QStringLiteral("concat"), QStringLiteral("-safe"), QStringLiteral("1"),
      QStringLiteral("-i"), concat_path, QStringLiteral("-c:v"), QStringLiteral("copy"),
      QStringLiteral("-frames:v"), QStringLiteral("144"),
      QStringLiteral("-video_track_timescale"), QStringLiteral("240000"), joined});
  const auto probe = QJsonDocument::fromJson(run(ffprobe, {
      QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-select_streams"), QStringLiteral("v:0"),
      QStringLiteral("-show_entries"), QStringLiteral("stream=avg_frame_rate,duration,nb_frames"),
      QStringLiteral("-of"), QStringLiteral("json"), joined})).object();
  const auto streams = probe.value(QStringLiteral("streams")).toArray();
  if (streams.size() != 1) return 1;
  const auto stream = streams.first().toObject();
  if (stream.value(QStringLiteral("avg_frame_rate")).toString() != QStringLiteral("30/1") ||
      stream.value(QStringLiteral("nb_frames")).toString() != QStringLiteral("144") ||
      qAbs(stream.value(QStringLiteral("duration")).toString().toDouble() - 4.8) > 1.0e-7 ||
      frame_hashes(ffmpeg, joined) != expected) {
    std::cerr << "Fractional-duration B-frame chunks changed clock or frame order\n";
    return 1;
  }
  return 0;
}
